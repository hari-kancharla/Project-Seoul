// Project Seoul workspace projection engine V0.

#include "seoul/browser/projection/workspace_switcher.h"

#include <tuple>

#include "seoul/browser/commands/browser_command.h"
#include "seoul/browser/commands/command_id.h"
#include "seoul/browser/projection/projection_calculator.h"
#include "seoul/browser/projection/projection_ordering.h"

namespace seoul {

WorkspaceSwitcher::WorkspaceSwitcher(Profile *profile, OrganizationModel *model,
                                     CommandExecutor *executor,
                                     WindowProjectionController *controller,
                                     LiveWindowStateProvider *live_state)
    : profile_(profile), model_(model), executor_(executor),
      controller_(controller), live_state_(live_state) {
  if (executor_) {
    executor_->AddCompletionObserver(this);
  }
}

WorkspaceSwitcher::~WorkspaceSwitcher() {
  if (executor_) {
    executor_->RemoveCompletionObserver(this);
  }
}

void WorkspaceSwitcher::AddObserver(WorkspaceSwitchObserver *observer) {
  observers_.AddObserver(observer);
}

void WorkspaceSwitcher::RemoveObserver(WorkspaceSwitchObserver *observer) {
  observers_.RemoveObserver(observer);
}

void WorkspaceSwitcher::SetPhase(WorkspaceSwitchPhase phase,
                                 std::optional<ProjectionError> error) {
  phase_ = phase;
  for (WorkspaceSwitchObserver &observer : observers_) {
    observer.OnWorkspaceSwitchPhaseChanged(phase, error);
  }
}

WindowProjection
WorkspaceSwitcher::ComputeTargetProjection(WorkspaceId target) const {
  if (!controller_ || !model_ || !live_state_) {
    return WindowProjection();
  }
  const LiveWindowSnapshot snapshot =
      live_state_->GetSnapshot(controller_->window())
          .value_or(LiveWindowSnapshot());
  return ProjectionCalculator::Compute(*model_, snapshot, target,
                                       ProjectionGeneration(0), false);
}

bool WorkspaceSwitcher::IsTargetTabActive(LiveTabKey tab) const {
  if (!live_state_ || !controller_ || !tab.is_valid()) {
    return false;
  }
  const std::optional<LiveWindowSnapshot> snapshot =
      live_state_->GetSnapshot(controller_->window());
  return snapshot.has_value() && snapshot->active_tab == tab;
}

ProjectionResult<WorkspaceSwitchResult>
WorkspaceSwitcher::RejectSwitch(ProjectionError error) {
  // Surface the rejection terminal before the transaction resets to idle.
  SetPhase(WorkspaceSwitchPhase::kRejected, error);
  ResetTransaction();
  return base::unexpected(error);
}

void WorkspaceSwitcher::ResetTransaction() {
  if (controller_) {
    controller_->SetSwitchInProgress(false);
  }
  SetPhase(WorkspaceSwitchPhase::kIdle);
  pending_target_ = WorkspaceId();
  prior_workspace_ = WorkspaceId();
  pending_activation_tab_ = LiveTabKey();
  pending_command_id_ = CommandId();
}

ProjectionResult<WorkspaceSwitchResult>
WorkspaceSwitcher::SwitchWorkspaceForWindow(WorkspaceId target_workspace) {
  if (phase_ != WorkspaceSwitchPhase::kIdle) {
    return RejectSwitch(ProjectionError::kConcurrentSwitch);
  }
  if (!model_ || !controller_ || !executor_) {
    return RejectSwitch(ProjectionError::kInvalidWindow);
  }
  if (controller_->projection().status ==
      ProjectionStatus::kReconciliationRequired) {
    return RejectSwitch(ProjectionError::kReconciliationRequired);
  }
  const WorkspaceRecord *ws = model_->FindWorkspace(target_workspace);
  if (!target_workspace.is_valid() || !ws) {
    return RejectSwitch(ProjectionError::kMissingWorkspace);
  }
  if (ws->archived) {
    return RejectSwitch(ProjectionError::kArchivedWorkspace);
  }

  SetPhase(WorkspaceSwitchPhase::kValidating);
  if (controller_) {
    controller_->SetSwitchInProgress(true);
  }
  const LiveWindowKey window = controller_->window();
  prior_workspace_ = model_->ActiveWorkspaceForWindow(window.value());
  pending_target_ = target_workspace;

  if (prior_workspace_ == target_workspace) {
    WorkspaceSwitchResult result;
    result.status = ProjectionOk();
    result.phase = WorkspaceSwitchPhase::kApplied;
    result.committed_projection = controller_->projection();
    result.empty_workspace = result.committed_projection.empty_workspace;
    SetPhase(WorkspaceSwitchPhase::kApplied);
    ResetTransaction();
    return result;
  }

  SetPhase(WorkspaceSwitchPhase::kCalculating);
  const WindowProjection target_projection =
      ComputeTargetProjection(target_workspace);
  const LiveTabKey target_tab = ProjectionOrdering::SelectSwitchTarget(
      *model_, target_projection, window.value(), target_workspace);

  if (!target_tab.is_valid()) {
    SetPhase(WorkspaceSwitchPhase::kCommitting);
    auto committed = CommitWorkspace(target_workspace, target_projection);
    ResetTransaction();
    return committed;
  }

  if (IsTargetTabActive(target_tab)) {
    SetPhase(WorkspaceSwitchPhase::kCommitting);
    auto committed = CommitWorkspace(target_workspace, target_projection);
    ResetTransaction();
    return committed;
  }

  SetPhase(WorkspaceSwitchPhase::kAwaitingActivation);
  pending_activation_tab_ = target_tab;
  BrowserCommand cmd;
  cmd.id = CommandId::Next();
  pending_command_id_ = cmd.id;
  cmd.kind = CommandKind::kActivateTab;
  cmd.window = window;
  cmd.tab = target_tab;
  const auto activation = executor_->Submit(std::move(cmd));
  if (!activation.has_value()) {
    if (prior_workspace_.is_valid()) {
      std::ignore =
          model_->SetActiveWorkspaceForWindow(window.value(), prior_workspace_);
    }
    return RejectSwitch(ProjectionError::kActivationFailed);
  }
  // Chromium can publish the active-tab lifecycle event synchronously inside
  // Submit(). In that case OnCommandCompleted has already committed and reset
  // this transaction before Submit returns. Report the observed terminal state
  // instead of returning a stale kAwaitingActivation result.
  if (phase_ == WorkspaceSwitchPhase::kIdle) {
    if (model_->ActiveWorkspaceForWindow(window.value()) == target_workspace &&
        IsTargetTabActive(target_tab)) {
      WorkspaceSwitchResult completed;
      completed.status = ProjectionOk();
      completed.phase = WorkspaceSwitchPhase::kApplied;
      completed.committed_projection = controller_->projection();
      completed.empty_workspace =
          completed.committed_projection.empty_workspace;
      return completed;
    }
    return base::unexpected(ProjectionError::kActivationFailed);
  }
  if (*activation == CommandStatus::kApplied) {
    if (!IsTargetTabActive(target_tab)) {
      if (prior_workspace_.is_valid()) {
        std::ignore = model_->SetActiveWorkspaceForWindow(window.value(),
                                                          prior_workspace_);
      }
      return RejectSwitch(ProjectionError::kActivationFailed);
    }
    SetPhase(WorkspaceSwitchPhase::kCommitting);
    auto committed = CommitWorkspace(target_workspace, target_projection);
    ResetTransaction();
    return committed;
  }
  if (*activation != CommandStatus::kAwaitingObservation) {
    if (prior_workspace_.is_valid()) {
      std::ignore =
          model_->SetActiveWorkspaceForWindow(window.value(), prior_workspace_);
    }
    return RejectSwitch(ProjectionError::kActivationFailed);
  }

  WorkspaceSwitchResult pending;
  pending.status = ProjectionOk();
  pending.phase = WorkspaceSwitchPhase::kAwaitingActivation;
  return pending;
}

ProjectionResult<WorkspaceSwitchResult>
WorkspaceSwitcher::SwitchWorkspaceForWindowExternalActivation(
    WorkspaceId target_workspace, LiveTabKey activated_tab) {
  if (phase_ != WorkspaceSwitchPhase::kIdle) {
    return RejectSwitch(ProjectionError::kConcurrentSwitch);
  }
  if (!model_ || !controller_ || !activated_tab.is_valid()) {
    return RejectSwitch(ProjectionError::kInvalidWindow);
  }
  const WorkspaceRecord *ws = model_->FindWorkspace(target_workspace);
  if (!target_workspace.is_valid() || !ws || ws->archived) {
    controller_->EnterFailOpen();
    return RejectSwitch(ProjectionError::kMissingWorkspace);
  }
  if (!IsTargetTabActive(activated_tab)) {
    controller_->EnterFailOpen();
    return RejectSwitch(ProjectionError::kActivationFailed);
  }
  const WindowProjection target_projection =
      ComputeTargetProjection(target_workspace);
  auto committed = CommitWorkspace(target_workspace, target_projection);
  ResetTransaction();
  return committed;
}

ProjectionResult<WorkspaceSwitchResult>
WorkspaceSwitcher::CommitWorkspace(WorkspaceId target_workspace,
                                   const WindowProjection &target_projection) {
  WorkspaceSwitchResult result;
  const LiveWindowKey window = controller_->window();
  const MutationStatus set_active =
      model_->SetActiveWorkspaceForWindow(window.value(), target_workspace);
  if (!set_active.has_value()) {
    if (prior_workspace_.is_valid()) {
      std::ignore =
          model_->SetActiveWorkspaceForWindow(window.value(), prior_workspace_);
    }
    return RejectSwitch(ProjectionError::kInvalidWorkspace);
  }
  controller_->OnOrganizationChanged();
  result.status = ProjectionOk();
  result.phase = WorkspaceSwitchPhase::kApplied;
  result.committed_projection = controller_->projection();
  result.empty_workspace = target_projection.empty_workspace;
  if (result.committed_projection.active_tab.is_valid() &&
      !target_projection.tabs.empty()) {
    bool active_projected = false;
    for (const ProjectedTab &tab : target_projection.tabs) {
      if (tab.tab == result.committed_projection.active_tab) {
        active_projected = true;
        break;
      }
    }
    if (!active_projected) {
      controller_->EnterFailOpen();
    }
  }
  SetPhase(WorkspaceSwitchPhase::kApplied);
  return result;
}

void WorkspaceSwitcher::OnCommandCompleted(CommandId id, CommandKind kind,
                                           CommandStatus status) {
  if (phase_ != WorkspaceSwitchPhase::kAwaitingActivation ||
      id != pending_command_id_ || kind != CommandKind::kActivateTab) {
    return;
  }
  const LiveWindowKey window = controller_->window();
  const WindowProjection target_projection =
      ComputeTargetProjection(pending_target_);

  if (status == CommandStatus::kOutcomeUnknown) {
    SetPhase(WorkspaceSwitchPhase::kOutcomeUnknown);
    if (pending_activation_tab_.is_valid() &&
        IsTargetTabActive(pending_activation_tab_)) {
      (void)CommitWorkspace(pending_target_, target_projection);
    } else if (prior_workspace_.is_valid()) {
      std::ignore =
          model_->SetActiveWorkspaceForWindow(window.value(), prior_workspace_);
      controller_->OnOrganizationChanged();
    }
    ResetTransaction();
    return;
  }

  if (status == CommandStatus::kCancelled ||
      status == CommandStatus::kRejected) {
    if (prior_workspace_.is_valid()) {
      std::ignore =
          model_->SetActiveWorkspaceForWindow(window.value(), prior_workspace_);
      controller_->OnOrganizationChanged();
    }
    SetPhase(status == CommandStatus::kCancelled
                 ? WorkspaceSwitchPhase::kCancelled
                 : WorkspaceSwitchPhase::kRejected,
             ProjectionError::kActivationFailed);
    ResetTransaction();
    return;
  }

  if (!IsTargetTabActive(pending_activation_tab_)) {
    if (prior_workspace_.is_valid()) {
      std::ignore =
          model_->SetActiveWorkspaceForWindow(window.value(), prior_workspace_);
      controller_->OnOrganizationChanged();
    }
    SetPhase(WorkspaceSwitchPhase::kRejected,
             ProjectionError::kActivationFailed);
    ResetTransaction();
    return;
  }

  SetPhase(WorkspaceSwitchPhase::kCommitting);
  (void)CommitWorkspace(pending_target_, target_projection);
  ResetTransaction();
}

} // namespace seoul
