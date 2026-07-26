// Project Seoul native organization engine.
// Profile-scoped KeyedService that owns the pure OrganizationModel and binds it
// to a single bounded profile preference for persistence. This is the only file
// in the engine that depends on Chromium's PrefService/KeyedService; the model
// itself stays Chromium-free.
//
// It also composes the profile's lifecycle bridge, confirmed outbound command
// path, projection service, and native Shell. A per-profile WindowWatcher
// attaches one TabStripBridge per eligible normal window; LifecycleCoordinator
// reconciles observed changes; PersistenceScheduler coalesces durable writes.

#ifndef SEOUL_BROWSER_ORGANIZATION_SEOUL_ORGANIZATION_SERVICE_H_
#define SEOUL_BROWSER_ORGANIZATION_SEOUL_ORGANIZATION_SERVICE_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "components/keyed_service/core/keyed_service.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/organization_observer.h"

class PrefService;
class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace seoul {

class ProjectionService;
class ShellService;
class CommandExecutor;
class LifecycleCoordinator;
class LiveWindowStateProvider;
class PersistenceScheduler;
class SessionRestoreWatcher;
class WindowWatcher;

// Single dict preference holding the bounded organization snapshot. Versioned
// in the value itself (schema_version), not in the key.
inline constexpr char kOrganizationPref[] = "seoul.organization.v1";
// Recovery copy of a corrupt or unsupported active snapshot. The active pref is
// left untouched until an explicit repair replaces it.
inline constexpr char kOrganizationRecoveryPref[] =
    "seoul.organization.v1.recovery";

enum class OrganizationLoadResult {
  kEmpty,
  kLoaded,
  kCorrupt,
  kUnsupportedVersion,
};

class SeoulOrganizationService : public KeyedService,
                                 public OrganizationModelObserver {
 public:
  // `profile` is the owning regular profile (used only to discover its
  // windows); `prefs` is its PrefService. Both must outlive the service.
  SeoulOrganizationService(Profile* profile, PrefService* prefs);
  SeoulOrganizationService(const SeoulOrganizationService&) = delete;
  SeoulOrganizationService& operator=(const SeoulOrganizationService&) = delete;
  ~SeoulOrganizationService() override;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  OrganizationModel& model() { return model_; }
  const OrganizationModel& model() const { return model_; }
  OrganizationLoadResult last_load_result() const { return last_load_result_; }
  bool recovery_required() const { return recovery_required_; }

  // Explicit recovery transition after corrupt/unsupported pref load. Writes a
  // valid initialized snapshot to the active pref and resumes persistence.
  MutationStatus AcknowledgeRecovery();

  LifecycleCoordinator* lifecycle_coordinator() { return coordinator_.get(); }
  const LifecycleCoordinator* lifecycle_coordinator() const {
    return coordinator_.get();
  }
  CommandExecutor* command_executor() { return command_executor_.get(); }
  const CommandExecutor* command_executor() const {
    return command_executor_.get();
  }
  ProjectionService* projection_service() { return projection_service_.get(); }
  const ProjectionService* projection_service() const {
    return projection_service_.get();
  }
  ShellService* shell_service() { return shell_service_.get(); }
  const ShellService* shell_service() const { return shell_service_.get(); }

  // Narrow accessor for the product runtime service: the per-profile live
  // window-state provider (owned by the window watcher). Used to read the
  // live tab strip for read-only capabilities and to find the active tab.
  LiveWindowStateProvider* live_window_state_provider();

  // KeyedService:
  void Shutdown() override;

  // OrganizationModelObserver:
  void OnOrganizationChanged(const OrganizationChange& change) override;

 private:
  void LoadFromPrefs();
  bool WriteToPrefs();
  void CopyActivePrefToRecovery();
  void RunLifecycleReconciliation();

  raw_ptr<Profile> profile_;
  raw_ptr<PrefService> prefs_;
  OrganizationModel model_;
  OrganizationLoadResult last_load_result_ = OrganizationLoadResult::kEmpty;
  bool loading_ = false;  // suppress save-on-change while loading
  bool suppress_persist_ =
      false;  // corrupt/unsupported load: do not overwrite active pref
  bool recovery_required_ = false;

  std::unique_ptr<PersistenceScheduler> scheduler_;
  std::unique_ptr<LifecycleCoordinator> coordinator_;
  std::unique_ptr<WindowWatcher> window_watcher_;
  std::unique_ptr<SessionRestoreWatcher> session_restore_watcher_;
  std::unique_ptr<class LiveTargetResolver> target_resolver_;
  std::unique_ptr<class ChromiumMutationAdapterImpl> mutation_adapter_;
  std::unique_ptr<CommandExecutor> command_executor_;
  std::unique_ptr<class CommandConfirmationSeam> command_confirmation_;
  std::unique_ptr<ProjectionService> projection_service_;
  std::unique_ptr<ShellService> shell_service_;

  base::ScopedObservation<OrganizationModel, OrganizationModelObserver>
      observation_{this};

  base::WeakPtrFactory<SeoulOrganizationService> weak_factory_{this};
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_ORGANIZATION_SEOUL_ORGANIZATION_SERVICE_H_
