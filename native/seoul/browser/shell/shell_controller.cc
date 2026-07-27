// Project Seoul native browser shell V0.

#include "seoul/browser/shell/shell_controller.h"

#include <algorithm>
#include <utility>

#include "seoul/browser/commands/browser_command.h"
#include "seoul/browser/commands/command_id.h"
#include "seoul/browser/commands/model_command_facade.h"
#include "seoul/browser/lifecycle/lifecycle_coordinator.h"
#include "seoul/browser/lifecycle/lifecycle_events.h"
#include "seoul/browser/organization/organization_errors.h"
#include "seoul/browser/projection/projection_service.h"
#include "seoul/browser/projection/workspace_switcher.h"
#include "seoul/browser/shell/shell_view_model.h"
#include "url/gurl.h"

namespace seoul {

ShellController::ShellController(ShellWindowKey window,
                                 Profile* profile,
                                 OrganizationModel* model,
                                 ProjectionService* projection_service,
                                 LiveWindowStateProvider* live_state,
                                 CommandExecutor* executor,
                                 LifecycleCoordinator* lifecycle,
                                 bool recovery_required)
    : window_(window),
      profile_(profile),
      model_(model),
      projection_service_(projection_service),
      live_state_(live_state),
      executor_(executor),
      lifecycle_(lifecycle),
      recovery_required_(recovery_required) {
  if (live_state_) {
    live_state_->AddObserver(this);
  }
  if (model_) {
    model_->AddObserver(this);
  }
  WindowProjectionController* controller =
      projection_service_ ? projection_service_->GetController(window_)
                          : nullptr;
  if (controller) {
    controller->AddObserver(this);
  }
  if (WorkspaceSwitcher* switcher =
          projection_service_ ? projection_service_->GetSwitcher(window_)
                              : nullptr) {
    switcher->AddObserver(this);
  }
  Recompute(true);
}

ShellController::~ShellController() {
  Shutdown();
}

void ShellController::Shutdown() {
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  if (live_state_) {
    live_state_->RemoveObserver(this);
  }
  if (model_) {
    model_->RemoveObserver(this);
  }
  if (projection_service_) {
    WindowProjectionController* controller =
        projection_service_->GetController(window_);
    if (controller) {
      controller->RemoveObserver(this);
    }
    if (WorkspaceSwitcher* switcher =
            projection_service_->GetSwitcher(window_)) {
      switcher->RemoveObserver(this);
    }
  }
  observers_.Clear();
}

void ShellController::AddObserver(ShellObserver* observer) {
  observers_.AddObserver(observer);
}

void ShellController::RemoveObserver(ShellObserver* observer) {
  observers_.RemoveObserver(observer);
}

void ShellController::SetCollapsed(bool collapsed) {
  if (collapsed_ == collapsed) {
    return;
  }
  collapsed_ = collapsed;
  Recompute(true);
}

void ShellController::RefreshCompactModeState() {
  Recompute(true);
}

void ShellController::RefreshAppearanceLayoutModeState() {
  Recompute(true);
}

void ShellController::RefreshProjectResources() {
  Recompute(true);
}

void ShellController::SetTaskSummary(ShellTaskSummary summary) {
  if (task_summary_ == summary) {
    return;
  }
  task_summary_ = summary;
  Recompute(true);
}

bool ShellController::SnapshotsEqual(const ShellSnapshot& a,
                                     const ShellSnapshot& b) const {
  // Semantic equality: compares every user-visible and command-relevant field
  // but NOT the monotonic revision (which changes on every recompute). When
  // this returns true the Views hierarchy must not be rebuilt.
  return a.window == b.window && a.mode == b.mode && a.status == b.status &&
         a.workspace == b.workspace && a.spaces == b.spaces &&
         a.essentials == b.essentials && a.pinned_items == b.pinned_items &&
         a.sections == b.sections && a.actions == b.actions &&
         a.tasks == b.tasks && a.project_resources == b.project_resources &&
         a.compact_mode == b.compact_mode &&
         a.appearance_layout == b.appearance_layout &&
         a.show_empty_workspace == b.show_empty_workspace &&
         a.show_status_banner == b.show_status_banner &&
         a.status_message == b.status_message &&
         a.switch_phase == b.switch_phase;
}

void ShellController::Recompute(bool publish) {
  if (shutting_down_ || !model_) {
    return;
  }
  ShellBuildContext context;
  context.window = window_;
  context.mode = collapsed_ ? ShellMode::kCollapsed : ShellMode::kExpanded;
  context.recovery_required = recovery_required_;
  context.lifecycle_degraded = lifecycle_ && lifecycle_->lifecycle_degraded();
  // Use the directly observed switch phase (captures terminal failure phases
  // before the transaction resets), not an inferred snapshot of switcher state.
  context.switch_phase = observed_switch_phase_;
  context.tasks = task_summary_;

  WindowProjection projection;
  if (projection_service_) {
    WindowProjectionController* controller =
        projection_service_->GetController(window_);
    if (controller) {
      projection = controller->projection();
    }
  }
  if (!live_.window.is_valid() && live_state_) {
    if (auto snap = live_state_->GetSnapshot(window_)) {
      live_ = *snap;
    }
  }
  if (live_state_) {
    for (const LiveWindowKey& candidate : live_state_->Windows()) {
      if (candidate == window_) {
        continue;
      }
      if (std::optional<LiveWindowSnapshot> snapshot =
              live_state_->GetSnapshot(candidate)) {
        context.other_live_windows.push_back(std::move(*snapshot));
      }
    }
  }

  // Build with the current revision so it does not perturb semantic comparison.
  ShellSnapshot next = ShellViewModel::Build(*model_, context, projection,
                                             live_, snapshot_.revision);
  if (project_resources_callback_ && next.workspace.workspace_id.is_valid()) {
    next.project_resources =
        project_resources_callback_.Run(next.workspace.workspace_id);
  }
  if (compact_mode_state_callback_) {
    next.compact_mode = compact_mode_state_callback_.Run(window_);
  } else {
    next.compact_mode.disabled_reason =
        "Compact mode is unavailable for this window.";
  }
  for (ShellActionEnablement& action : next.actions) {
    if (action.action != ShellUtilityAction::kToggleCompactMode) {
      continue;
    }
    action.enabled = next.compact_mode.available;
    action.disabled_reason = next.compact_mode.available
                                 ? std::string()
                                 : next.compact_mode.disabled_reason;
    break;
  }
  if (appearance_layout_state_callback_) {
    next.appearance_layout = appearance_layout_state_callback_.Run(window_);
  } else {
    next.appearance_layout.disabled_reason =
        "Appearance layouts are unavailable for this window.";
  }
  for (ShellActionEnablement& action : next.actions) {
    if (!AppearanceLayoutModeForAction(action.action).has_value()) {
      continue;
    }
    action.enabled = next.appearance_layout.available;
    action.disabled_reason = next.appearance_layout.available
                                 ? std::string()
                                 : next.appearance_layout.disabled_reason;
  }
  // Surface a directly-observed switch failure as a user-facing banner, unless
  // a higher-priority status (recovery/reconciliation/fail-open) already owns
  // it.
  if (switch_failed_ && (next.status == ShellStatus::kCoherent ||
                         next.status == ShellStatus::kEmptyWorkspace)) {
    next.status = ShellStatus::kSwitchingWorkspace;
    next.show_status_banner = true;
    next.status_message = "Workspace switch failed.";
  }
  if (initialized_ && SnapshotsEqual(snapshot_, next)) {
    // Identical semantic state: keep the existing snapshot/revision, do not
    // republish, do not rebuild the Views hierarchy.
    return;
  }
  next.revision = ++revision_;
  snapshot_ = std::move(next);
  initialized_ = true;
  if (publish) {
    Publish();
  }
}

void ShellController::Publish() {
  ShellChange change;
  change.window = window_;
  change.revision = snapshot_.revision;
  for (ShellObserver& observer : observers_) {
    observer.OnShellSnapshotChanged(change, snapshot_);
  }
}

void ShellController::OnOrganizationChanged(const OrganizationChange& change) {
  (void)change;
  Recompute(true);
}

void ShellController::OnLiveWindowSnapshotChanged(
    const LiveWindowSnapshot& snapshot) {
  if (snapshot.window == window_) {
    live_ = snapshot;
  }
  // Other windows can gain/lose the origin backing an Essential, so every
  // profile-local snapshot change may alter this window's Essential routing.
  Recompute(true);
}

void ShellController::OnLiveWindowRemoved(LiveWindowKey window) {
  if (window == window_) {
    Shutdown();
    return;
  }
  Recompute(true);
}

void ShellController::OnProjectionChanged(const ProjectionChange& change,
                                          const WindowProjection& projection) {
  (void)change;
  (void)projection;
  Recompute(true);
}

void ShellController::OnWorkspaceSwitchPhaseChanged(
    WorkspaceSwitchPhase phase,
    std::optional<ProjectionError> error) {
  (void)error;
  observed_switch_phase_ = phase;
  switch (phase) {
    case WorkspaceSwitchPhase::kValidating:
    case WorkspaceSwitchPhase::kApplied:
      switch_failed_ = false;  // a new switch started or one succeeded
      break;
    case WorkspaceSwitchPhase::kRejected:
    case WorkspaceSwitchPhase::kCancelled:
    case WorkspaceSwitchPhase::kOutcomeUnknown:
      switch_failed_ = true;  // sticky until the next switch starts/succeeds
      break;
    default:
      break;
  }
  Recompute(true);
}

ShellResult<WorkspaceId> ShellController::SwitchWorkspace(WorkspaceId target) {
  if (shutting_down_ || !projection_service_) {
    return ShellErr(ShellError::kInvalidWindow);
  }
  if (target.is_valid() && target == snapshot_.workspace.workspace_id) {
    return target;
  }
  WorkspaceSwitcher* switcher = projection_service_->GetSwitcher(window_);
  if (!switcher) {
    return ShellErr(ShellError::kInvalidWindow);
  }
  auto result = switcher->SwitchWorkspaceForWindow(target);
  Recompute(true);
  if (!result.has_value()) {
    return ShellErr(ShellError::kSwitchFailed);
  }
  if (result->phase == WorkspaceSwitchPhase::kAwaitingActivation) {
    return target;
  }
  return target;
}

ShellStatusResult ShellController::OpenNewTemporaryTab() {
  if (shutting_down_ || !executor_) {
    return ShellErr(ShellError::kInvalidWindow);
  }
  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kOpenNewTab;
  command.window = window_;
  // No URL: the new-tab command opens Chromium's New Tab Page through the
  // dedicated path, never through URL validation. The inserted tab is assigned
  // to the active workspace and marked temporary by the lifecycle coordinator.
  auto result = executor_->Submit(std::move(command));
  Recompute(true);
  return result.has_value() ? ShellOk()
                            : ShellErr(ShellError::kCommandRejected);
}

std::vector<ShellSplitCandidate> ShellController::SplitCandidates() const {
  std::vector<ShellSplitCandidate> candidates;
  if (shutting_down_ || !projection_service_) {
    return candidates;
  }
  const WindowProjectionController* projection_controller =
      projection_service_->GetController(window_);
  if (!projection_controller) {
    return candidates;
  }
  return ShellViewModel::BuildSplitCandidates(
      projection_controller->projection(), live_);
}

ShellStatusResult ShellController::CreateSplitWithPartner(LiveTabKey partner) {
  if (shutting_down_ || !executor_ || !projection_service_) {
    return ShellErr(ShellError::kInvalidWindow);
  }
  bool permitted_partner = false;
  for (const ShellSplitCandidate& candidate : SplitCandidates()) {
    if (candidate.tab == partner) {
      permitted_partner = true;
      break;
    }
  }
  const WindowProjectionController* projection_controller =
      projection_service_->GetController(window_);
  const LiveTabKey active = projection_controller
                                ? projection_controller->projection().active_tab
                                : LiveTabKey();
  if (!active.is_valid() || !partner.is_valid() || !permitted_partner) {
    return ShellErr(ShellError::kCommandRejected);
  }
  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kCreateSplit;
  command.window = window_;
  command.tab = active;
  command.split_pane_b = partner;
  auto result = executor_->Submit(std::move(command));
  Recompute(true);
  return result.has_value() ? ShellOk()
                            : ShellErr(ShellError::kCommandRejected);
}

ShellStatusResult ShellController::CreateSplitFromActive() {
  const std::vector<ShellSplitCandidate> candidates = SplitCandidates();
  if (candidates.size() != 1u) {
    return ShellErr(ShellError::kCommandRejected);
  }
  return CreateSplitWithPartner(candidates.front().tab);
}

ShellStatusResult ShellController::OpenCanvas() {
  if (shutting_down_ || !open_canvas_callback_ ||
      !open_canvas_callback_.Run()) {
    return ShellErr(ShellError::kCommandRejected);
  }
  return ShellOk();
}

ShellStatusResult ShellController::OpenBoost() {
  if (shutting_down_ || !open_boost_callback_ || !open_boost_callback_.Run()) {
    return ShellErr(ShellError::kCommandRejected);
  }
  return ShellOk();
}

void ShellController::SetProjectCallbacks(
    base::RepeatingCallback<ShellProjectResources(WorkspaceId)> resources,
    base::RepeatingCallback<bool(WorkspaceId)> create_chat,
    base::RepeatingCallback<bool(const std::string&)> open_chat,
    base::RepeatingCallback<bool(WorkspaceId)> open_files) {
  project_resources_callback_ = std::move(resources);
  create_project_chat_callback_ = std::move(create_chat);
  open_project_chat_callback_ = std::move(open_chat);
  open_project_files_callback_ = std::move(open_files);
  Recompute(true);
}

ShellStatusResult ShellController::CreateProjectChat() {
  const WorkspaceId workspace = snapshot_.workspace.workspace_id;
  if (shutting_down_ || !workspace.is_valid() ||
      !create_project_chat_callback_ ||
      !create_project_chat_callback_.Run(workspace)) {
    return ShellErr(ShellError::kCommandRejected);
  }
  Recompute(true);
  return ShellOk();
}

ShellStatusResult ShellController::OpenProjectChat(
    const std::string& thread_id) {
  if (shutting_down_ || thread_id.empty() || !open_project_chat_callback_ ||
      !open_project_chat_callback_.Run(thread_id)) {
    return ShellErr(ShellError::kCommandRejected);
  }
  return ShellOk();
}

ShellStatusResult ShellController::OpenProjectFiles() {
  const WorkspaceId workspace = snapshot_.workspace.workspace_id;
  if (shutting_down_ || !workspace.is_valid() ||
      !open_project_files_callback_ ||
      !open_project_files_callback_.Run(workspace)) {
    return ShellErr(ShellError::kCommandRejected);
  }
  return ShellOk();
}

ShellStatusResult ShellController::RunReconciliation() {
  if (shutting_down_ || !lifecycle_) {
    return ShellErr(ShellError::kInvalidWindow);
  }
  // Delegate to the coordinator's real reconciliation path, which emits
  // reconciliation-began, performs the bounded WindowWatcher rescan, and emits
  // reconciliation-completed only after the rescan returns. The shell never
  // synthesizes completion itself.
  lifecycle_->RequestReconciliation();
  Recompute(true);
  return ShellOk();
}

ShellStatusResult ShellController::AcknowledgeRecovery() {
  if (acknowledge_recovery_callback_) {
    auto result = acknowledge_recovery_callback_.Run();
    recovery_required_ = !result.has_value();
    Recompute(true);
    return result.has_value() ? ShellOk()
                              : ShellErr(ShellError::kMutationRejected);
  }
  return ShellErr(ShellError::kMutationRejected);
}

ShellStatusResult ShellController::RunUtilityAction(ShellUtilityAction action) {
  switch (action) {
    case ShellUtilityAction::kNewTemporaryTab:
      return OpenNewTemporaryTab();
    case ShellUtilityAction::kOpenCanvas:
    case ShellUtilityAction::kOpenTaskDeck:
      // Task Deck and Canvas share the single Canvas side-panel entry today, so
      // both open it. They stay distinct actions so a later Task Deck deep link
      // can diverge without reworking the launcher catalog.
      return OpenCanvas();
    case ShellUtilityAction::kOpenBoost:
      return OpenBoost();
    case ShellUtilityAction::kCreateSplit:
      return CreateSplitFromActive();
    case ShellUtilityAction::kToggleCompactMode:
      return ToggleCompactMode();
    case ShellUtilityAction::kSetAppearanceSingle:
      return SetAppearanceLayoutMode(ShellAppearanceLayoutMode::kSingle);
    case ShellUtilityAction::kSetAppearanceMultiple:
      return SetAppearanceLayoutMode(ShellAppearanceLayoutMode::kMultiple);
    case ShellUtilityAction::kSetAppearanceCollapsed:
      return SetAppearanceLayoutMode(ShellAppearanceLayoutMode::kCollapsed);
    case ShellUtilityAction::kOpenSettings:
      return open_settings_callback_ && open_settings_callback_.Run()
                 ? ShellOk()
                 : ShellErr(ShellError::kCommandRejected);
    case ShellUtilityAction::kOpenDownloads:
      return open_downloads_callback_ && open_downloads_callback_.Run()
                 ? ShellOk()
                 : ShellErr(ShellError::kCommandRejected);
    case ShellUtilityAction::kReconcile:
      return RunReconciliation();
    case ShellUtilityAction::kAcknowledgeRecovery:
      return AcknowledgeRecovery();
    case ShellUtilityAction::kCommandLauncher:
      // Opening UI is owned by the bound Views control, not a model action.
      return ShellErr(ShellError::kCommandRejected);
  }
  return ShellErr(ShellError::kCommandRejected);
}

void ShellController::SetBrowserPageCallbacks(
    base::RepeatingCallback<bool()> settings,
    base::RepeatingCallback<bool()> downloads) {
  open_settings_callback_ = std::move(settings);
  open_downloads_callback_ = std::move(downloads);
}

std::vector<CommandLauncherEntry> ShellController::CommandLauncherEntries()
    const {
  if (shutting_down_ || !model_) {
    return CommandLauncherCatalog::BuildEntries(snapshot_,
                                                OrganizationSnapshot(), {});
  }
  std::vector<LiveWindowSnapshot> live_windows;
  if (live_state_) {
    for (const LiveWindowKey& candidate : live_state_->Windows()) {
      if (std::optional<LiveWindowSnapshot> live =
              live_state_->GetSnapshot(candidate)) {
        live_windows.push_back(std::move(*live));
      }
    }
  }
  return CommandLauncherCatalog::BuildEntries(snapshot_, model_->ToSnapshot(),
                                              live_windows);
}

ShellStatusResult ShellController::ExecuteCommandLauncherEntry(
    const CommandLauncherEntry& entry) {
  if (shutting_down_ || !entry.enabled) {
    return ShellErr(ShellError::kCommandRejected);
  }
  switch (entry.kind) {
    case CommandLauncherEntryKind::kUtility:
      return RunUtilityAction(entry.action);
    case CommandLauncherEntryKind::kWorkspace: {
      const ShellResult<WorkspaceId> switched =
          SwitchWorkspace(entry.workspace_id);
      return switched.has_value() ? ShellOk() : ShellErr(switched.error());
    }
    case CommandLauncherEntryKind::kEssential:
      return OpenEssential(entry.essential_id);
    case CommandLauncherEntryKind::kTab:
      return ActivateLiveTab(entry.live_window, entry.live_tab);
  }
  return ShellErr(ShellError::kCommandRejected);
}

ShellStatusResult ShellController::ActivateLiveTab(LiveWindowKey window,
                                                   LiveTabKey tab) {
  if (shutting_down_ || !executor_ || !live_state_ || !window.is_valid() ||
      !tab.is_valid()) {
    return ShellErr(ShellError::kInvalidWindow);
  }
  const std::optional<LiveWindowSnapshot> current =
      live_state_->GetSnapshot(window);
  if (!current || !current->eligible) {
    return ShellErr(ShellError::kCommandRejected);
  }
  const bool tab_exists = std::ranges::any_of(
      current->tabs, [tab](const LiveTabDescriptor& candidate) {
        return candidate.tab == tab;
      });
  if (!tab_exists) {
    return ShellErr(ShellError::kCommandRejected);
  }
  if (window == window_ && current->active_tab == tab) {
    return ShellOk();
  }

  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kActivateTab;
  command.window = window;
  command.tab = tab;
  const auto result = executor_->Submit(std::move(command));
  if (!result.has_value()) {
    return ShellErr(ShellError::kCommandRejected);
  }
  Recompute(true);
  if (window != window_ &&
      (!focus_window_callback_ || !focus_window_callback_.Run(window))) {
    return ShellErr(ShellError::kCommandRejected);
  }
  return ShellOk();
}

void ShellController::SetAcknowledgeRecoveryCallback(
    base::RepeatingCallback<MutationStatus()> callback) {
  acknowledge_recovery_callback_ = std::move(callback);
}

void ShellController::SetOpenCanvasCallback(
    base::RepeatingCallback<bool()> callback) {
  open_canvas_callback_ = std::move(callback);
}

void ShellController::SetOpenBoostCallback(
    base::RepeatingCallback<bool()> callback) {
  open_boost_callback_ = std::move(callback);
}

void ShellController::SetCompactModeCallbacks(
    base::RepeatingCallback<ShellCompactModeState(LiveWindowKey)> state,
    base::RepeatingCallback<bool(LiveWindowKey, bool)> set) {
  compact_mode_state_callback_ = std::move(state);
  set_compact_mode_callback_ = std::move(set);
  Recompute(true);
}

void ShellController::SetAppearanceLayoutModeCallbacks(
    base::RepeatingCallback<ShellAppearanceLayoutState(LiveWindowKey)> state,
    base::RepeatingCallback<bool(LiveWindowKey, ShellAppearanceLayoutMode)>
        set) {
  appearance_layout_state_callback_ = std::move(state);
  set_appearance_layout_callback_ = std::move(set);
  Recompute(true);
}

ShellStatusResult ShellController::ToggleCompactMode() {
  if (shutting_down_ || !compact_mode_state_callback_ ||
      !set_compact_mode_callback_) {
    return ShellErr(ShellError::kCommandRejected);
  }
  const ShellCompactModeState state = compact_mode_state_callback_.Run(window_);
  if (!state.available ||
      !set_compact_mode_callback_.Run(window_, !state.enabled)) {
    Recompute(true);
    return ShellErr(ShellError::kCommandRejected);
  }
  Recompute(true);
  return ShellOk();
}

ShellStatusResult ShellController::SetAppearanceLayoutMode(
    ShellAppearanceLayoutMode mode) {
  if (shutting_down_ || !appearance_layout_state_callback_ ||
      !set_appearance_layout_callback_) {
    return ShellErr(ShellError::kCommandRejected);
  }
  const ShellAppearanceLayoutState state =
      appearance_layout_state_callback_.Run(window_);
  if (!state.available) {
    Recompute(true);
    return ShellErr(ShellError::kCommandRejected);
  }
  if (state.mode == mode) {
    return ShellOk();
  }
  if (!set_appearance_layout_callback_.Run(window_, mode)) {
    Recompute(true);
    return ShellErr(ShellError::kCommandRejected);
  }
  Recompute(true);
  return ShellOk();
}

void ShellController::SetFocusWindowCallback(
    base::RepeatingCallback<bool(LiveWindowKey)> callback) {
  focus_window_callback_ = std::move(callback);
}

ShellStatusResult ShellController::OpenEssential(const EssentialId& id) {
  if (shutting_down_ || !executor_ || !model_) {
    return ShellErr(ShellError::kInvalidWindow);
  }
  const EssentialRecord* essential = model_->FindEssential(id);
  if (!essential || essential->root_url.empty()) {
    return ShellErr(ShellError::kCommandRejected);
  }
  const ShellEssentialItem* shell_item = nullptr;
  for (const ShellEssentialItem& candidate : snapshot_.essentials) {
    if (candidate.id == id) {
      shell_item = &candidate;
      break;
    }
  }
  if (shell_item && shell_item->is_active &&
      shell_item->live_in_current_window) {
    return ShellOk();
  }
  BrowserCommand command;
  command.id = CommandId::Next();
  command.window = window_;
  if (shell_item && shell_item->has_live_tab &&
      shell_item->live_tab.is_valid() && shell_item->live_window.is_valid()) {
    command.kind = CommandKind::kActivateTab;
    command.window = shell_item->live_window;
    command.tab = shell_item->live_tab;
  } else {
    command.kind = CommandKind::kOpenRetainedTab;
    command.url = GURL(essential->root_url);
  }
  const bool focus_other_window = shell_item && shell_item->has_live_tab &&
                                  !shell_item->live_in_current_window;
  const LiveWindowKey target_window =
      shell_item ? shell_item->live_window : LiveWindowKey();
  auto result = executor_->Submit(std::move(command));
  if (!result.has_value()) {
    return ShellErr(ShellError::kCommandRejected);
  }
  // The activation already committed, so refresh the snapshot before reporting
  // the outcome: even when the best-effort focus of another window fails below,
  // the model must not keep a stale view of the tab that is now active.
  Recompute(true);
  if (focus_other_window &&
      (!focus_window_callback_ || !focus_window_callback_.Run(target_window))) {
    return ShellErr(ShellError::kCommandRejected);
  }
  return ShellOk();
}

ShellStatusResult ShellController::DispatchModelCommand(
    BrowserCommand command) {
  if (shutting_down_ || !model_) {
    return ShellErr(ShellError::kInvalidWindow);
  }
  ModelCommandFacade facade(model_);
  auto result = facade.Execute(command);
  Recompute(true);
  return result.has_value() ? ShellOk()
                            : ShellErr(ShellError::kMutationRejected);
}

}  // namespace seoul
