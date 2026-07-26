// Project Seoul native organization engine.

#include "seoul/browser/organization/seoul_organization_service.h"

#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "seoul/browser/commands/chromium_mutation_adapter_impl.h"
#include "seoul/browser/commands/command_confirmation_seam.h"
#include "seoul/browser/commands/command_executor.h"
#include "seoul/browser/commands/live_target_resolver.h"
#include "seoul/browser/lifecycle/lifecycle_coordinator.h"
#include "seoul/browser/lifecycle/lifecycle_events.h"
#include "seoul/browser/lifecycle/live_window_state.h"
#include "seoul/browser/lifecycle/persistence_scheduler.h"
#include "seoul/browser/lifecycle/session_restore_watcher.h"
#include "seoul/browser/lifecycle/window_watcher.h"
#include "seoul/browser/organization/organization_errors.h"
#include "seoul/browser/organization/organization_store.h"
#include "seoul/browser/projection/projection_service.h"
#include "seoul/browser/shell/shell_service.h"

namespace seoul {

SeoulOrganizationService::SeoulOrganizationService(Profile* profile,
                                                   PrefService* prefs)
    : profile_(profile), prefs_(prefs) {
  observation_.Observe(&model_);
  LoadFromPrefs();

  // The writer returns bool (the scheduler re-marks dirty on failure), and a
  // WeakPtr receiver cannot bind to a returning method; passing the WeakPtr
  // as an argument keeps both: a destroyed service reports a failed write.
  scheduler_ = std::make_unique<PersistenceScheduler>(
      base::BindRepeating(
          [](base::WeakPtr<SeoulOrganizationService> service) {
            return service ? service->WriteToPrefs() : false;
          },
          weak_factory_.GetWeakPtr()),
      base::SequencedTaskRunner::GetCurrentDefault());

  coordinator_ = std::make_unique<LifecycleCoordinator>(&model_);
  coordinator_->SetReconciliationRequestCallback(
      base::BindRepeating(&SeoulOrganizationService::RunLifecycleReconciliation,
                          weak_factory_.GetWeakPtr()));

  window_watcher_ =
      std::make_unique<WindowWatcher>(profile_, coordinator_.get(), &model_);
  window_watcher_->StartObserving();

  session_restore_watcher_ = std::make_unique<SessionRestoreWatcher>(
      profile_, window_watcher_.get(), coordinator_.get());
  session_restore_watcher_->StartObserving();

  target_resolver_ = std::make_unique<LiveTargetResolver>();
  mutation_adapter_ = std::make_unique<ChromiumMutationAdapterImpl>();
  command_executor_ = std::make_unique<CommandExecutor>(
      profile_, &model_, coordinator_.get(), target_resolver_.get(),
      mutation_adapter_.get());
  command_confirmation_ =
      std::make_unique<CommandConfirmationSeam>(command_executor_.get());
  coordinator_->SetConfirmationCallback(
      base::BindRepeating(&CommandConfirmationSeam::OnNormalizedEvent,
                          base::Unretained(command_confirmation_.get())));
  coordinator_->SetPinHandlingSuppressor(
      base::BindRepeating(&CommandExecutor::ShouldDeferLifecyclePinRoleMutation,
                          base::Unretained(command_executor_.get())));

  projection_service_ = std::make_unique<ProjectionService>(
      profile_, &model_, coordinator_.get(), command_executor_.get(),
      window_watcher_->live_state_provider());
  shell_service_ = std::make_unique<ShellService>(
      profile_, &model_, projection_service_.get(),
      window_watcher_->live_state_provider(), command_executor_.get(),
      coordinator_.get(), recovery_required_,
      base::BindRepeating(&SeoulOrganizationService::AcknowledgeRecovery,
                          base::Unretained(this)));
}

SeoulOrganizationService::~SeoulOrganizationService() = default;

LiveWindowStateProvider*
SeoulOrganizationService::live_window_state_provider() {
  return window_watcher_ ? window_watcher_->live_state_provider() : nullptr;
}

// static
void SeoulOrganizationService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(kOrganizationPref);
  registry->RegisterDictionaryPref(kOrganizationRecoveryPref);
}

void SeoulOrganizationService::Shutdown() {
  if (command_executor_) {
    command_executor_->Shutdown();
  }
  if (coordinator_) {
    coordinator_->SetConfirmationCallback(
        base::RepeatingCallback<void(const NormalizedEvent&)>());
    coordinator_->SetPinHandlingSuppressor(
        base::RepeatingCallback<bool(LiveTabKey)>());
  }
  command_confirmation_.reset();
  command_executor_.reset();
  mutation_adapter_.reset();
  target_resolver_.reset();
  if (session_restore_watcher_) {
    session_restore_watcher_->StopObserving();
    session_restore_watcher_.reset();
  }
  window_watcher_.reset();
  if (coordinator_) {
    NormalizedEvent event;
    event.type = NormalizedEventType::kShutdownBegan;
    coordinator_->OnNormalizedEvent(event);
  }
  if (scheduler_) {
    scheduler_->Flush();
    scheduler_->Shutdown();
  }
  if (shell_service_) {
    shell_service_->Shutdown();
    shell_service_.reset();
  }
  if (projection_service_) {
    projection_service_->Shutdown();
    projection_service_.reset();
  }
}

void SeoulOrganizationService::OnOrganizationChanged(
    const OrganizationChange& change) {
  if (loading_ || suppress_persist_) {
    return;
  }
  if (projection_service_) {
    projection_service_->OnOrganizationChanged(change);
  }
  if (shell_service_) {
    shell_service_->OnOrganizationChanged(change);
  }
  if (scheduler_) {
    scheduler_->ScheduleWrite();
  }
}

void SeoulOrganizationService::CopyActivePrefToRecovery() {
  const base::DictValue& active = prefs_->GetDict(kOrganizationPref);
  if (active.empty()) {
    return;
  }
  const base::DictValue& existing_recovery =
      prefs_->GetDict(kOrganizationRecoveryPref);
  if (!existing_recovery.empty()) {
    return;  // preserve the first recovery copy
  }
  prefs_->SetDict(kOrganizationRecoveryPref, active.Clone());
}

void SeoulOrganizationService::LoadFromPrefs() {
  loading_ = true;
  suppress_persist_ = false;
  recovery_required_ = false;
  last_load_result_ = OrganizationLoadResult::kEmpty;

  const base::DictValue& stored = prefs_->GetDict(kOrganizationPref);
  if (stored.empty()) {
    std::ignore = model_.EnsureDefaultWorkspace();
    loading_ = false;
    WriteToPrefs();
    return;
  }

  MutationResult<OrganizationSnapshot> parsed = DeserializeSnapshot(stored);
  if (!parsed.has_value()) {
    if (parsed.error() == OrganizationError::kUnsupportedSchema) {
      last_load_result_ = OrganizationLoadResult::kUnsupportedVersion;
    } else {
      last_load_result_ = OrganizationLoadResult::kCorrupt;
    }
    CopyActivePrefToRecovery();
    suppress_persist_ = true;
    recovery_required_ = true;
    std::ignore = model_.EnsureDefaultWorkspace();
    loading_ = false;
    return;
  }

  const MutationStatus loaded = model_.LoadSnapshot(parsed.value());
  if (!loaded.has_value()) {
    last_load_result_ = OrganizationLoadResult::kCorrupt;
    CopyActivePrefToRecovery();
    suppress_persist_ = true;
    recovery_required_ = true;
    std::ignore = model_.EnsureDefaultWorkspace();
    loading_ = false;
    return;
  }

  last_load_result_ = OrganizationLoadResult::kLoaded;
  std::ignore = model_.EnsureDefaultWorkspace();
  loading_ = false;
}

bool SeoulOrganizationService::WriteToPrefs() {
  if (suppress_persist_) {
    return false;
  }
  base::DictValue dict = SerializeSnapshot(model_.ToSnapshot());
  if (!SerializedSizeWithinLimit(dict)) {
    return false;
  }
  prefs_->SetDict(kOrganizationPref, std::move(dict));
  return true;
}

MutationStatus SeoulOrganizationService::AcknowledgeRecovery() {
  if (!recovery_required_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  suppress_persist_ = false;
  recovery_required_ = false;
  if (!WriteToPrefs()) {
    return Err(OrganizationError::kPersistenceFailure);
  }
  return Ok();
}

void SeoulOrganizationService::RunLifecycleReconciliation() {
  if (!coordinator_ || !window_watcher_) {
    return;
  }
  NormalizedEvent began;
  began.type = NormalizedEventType::kReconciliationBegan;
  began.origin = MutationOrigin::kSystemRecovery;
  coordinator_->OnNormalizedEvent(began);

  window_watcher_->RescanExistingWindows();

  NormalizedEvent completed;
  completed.type = NormalizedEventType::kReconciliationCompleted;
  completed.origin = MutationOrigin::kSystemRecovery;
  coordinator_->OnNormalizedEvent(completed);
}

}  // namespace seoul
