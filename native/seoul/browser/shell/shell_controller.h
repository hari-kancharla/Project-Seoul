// Project Seoul native browser shell V0.

#ifndef SEOUL_BROWSER_SHELL_SHELL_CONTROLLER_H_
#define SEOUL_BROWSER_SHELL_SHELL_CONTROLLER_H_

#include <optional>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "seoul/browser/commands/browser_command.h"
#include "seoul/browser/lifecycle/live_window_state.h"
#include "seoul/browser/organization/organization_observer.h"
#include "seoul/browser/projection/projection_observer.h"
#include "seoul/browser/shell/command_launcher_catalog.h"
#include "seoul/browser/shell/shell_errors.h"
#include "seoul/browser/shell/shell_observer.h"
#include "seoul/browser/shell/shell_types.h"

class Profile;

namespace seoul {

class CommandExecutor;
class LifecycleCoordinator;
class ModelCommandFacade;
class OrganizationModel;
class ProjectionService;
class WorkspaceSwitcher;

class ShellController : public OrganizationModelObserver,
                        public LiveWindowStateObserver,
                        public ProjectionObserver,
                        public WorkspaceSwitchObserver {
 public:
  ShellController(ShellWindowKey window,
                  Profile* profile,
                  OrganizationModel* model,
                  ProjectionService* projection_service,
                  LiveWindowStateProvider* live_state,
                  CommandExecutor* executor,
                  LifecycleCoordinator* lifecycle,
                  bool recovery_required);
  ShellController(const ShellController&) = delete;
  ShellController& operator=(const ShellController&) = delete;
  ~ShellController() override;

  const ShellSnapshot& snapshot() const { return snapshot_; }
  OrganizationModel* model() { return model_; }
  void SetCollapsed(bool collapsed);
  void RefreshCompactModeState();
  void RefreshAppearanceLayoutModeState();
  void RefreshProjectResources();
  void SetTaskSummary(ShellTaskSummary summary);
  void Shutdown();

  void AddObserver(ShellObserver* observer);
  void RemoveObserver(ShellObserver* observer);

  ShellResult<WorkspaceId> SwitchWorkspace(WorkspaceId target);
  ShellStatusResult OpenNewTemporaryTab();
  std::vector<ShellSplitCandidate> SplitCandidates() const;
  ShellStatusResult CreateSplitWithPartner(LiveTabKey partner);
  // Convenience for non-UI callers. It succeeds only when exactly one valid
  // partner exists; it never chooses the first of several candidates.
  ShellStatusResult CreateSplitFromActive();
  ShellStatusResult OpenCanvas();
  ShellStatusResult OpenBoost();
  ShellStatusResult RunReconciliation();
  ShellStatusResult AcknowledgeRecovery();
  // Typed utility dispatch shared by native controls and the command
  // launcher. Unknown/inapplicable actions fail closed; no string-id chain.
  ShellStatusResult RunUtilityAction(ShellUtilityAction action);
  std::vector<CommandLauncherEntry> CommandLauncherEntries() const;
  // Revalidates and executes a launcher snapshot entry. Views never dispatch
  // identifiers or stale model objects themselves.
  ShellStatusResult ExecuteCommandLauncherEntry(
      const CommandLauncherEntry& entry);
  // Activates only a tab that still exists in the live-state provider. The
  // launcher never trusts its potentially stale entry snapshot.
  ShellStatusResult ActivateLiveTab(LiveWindowKey window, LiveTabKey tab);

  void SetAcknowledgeRecoveryCallback(
      base::RepeatingCallback<MutationStatus()> callback);
  void SetOpenCanvasCallback(base::RepeatingCallback<bool()> callback);
  void SetOpenBoostCallback(base::RepeatingCallback<bool()> callback);
  void SetBrowserPageCallbacks(base::RepeatingCallback<bool()> settings,
                               base::RepeatingCallback<bool()> downloads);
  void SetProjectCallbacks(
      base::RepeatingCallback<ShellProjectResources(WorkspaceId)> resources,
      base::RepeatingCallback<bool(WorkspaceId)> create_chat,
      base::RepeatingCallback<bool(const std::string&)> open_chat,
      base::RepeatingCallback<bool(WorkspaceId)> open_files);
  ShellStatusResult CreateProjectChat();
  ShellStatusResult OpenProjectChat(const std::string& thread_id);
  ShellStatusResult OpenProjectFiles();
  void SetCompactModeCallbacks(
      base::RepeatingCallback<ShellCompactModeState(LiveWindowKey)> state,
      base::RepeatingCallback<bool(LiveWindowKey, bool)> set);
  void SetAppearanceLayoutModeCallbacks(
      base::RepeatingCallback<ShellAppearanceLayoutState(LiveWindowKey)> state,
      base::RepeatingCallback<bool(LiveWindowKey, ShellAppearanceLayoutMode)>
          set);
  void SetFocusWindowCallback(
      base::RepeatingCallback<bool(LiveWindowKey)> callback);
  ShellStatusResult ToggleCompactMode();
  ShellStatusResult SetAppearanceLayoutMode(ShellAppearanceLayoutMode mode);
  ShellStatusResult OpenEssential(const EssentialId& id);
  ShellStatusResult DispatchModelCommand(BrowserCommand command);

  void OnOrganizationChanged(const OrganizationChange& change) override;
  void OnLiveWindowSnapshotChanged(const LiveWindowSnapshot& snapshot) override;
  void OnLiveWindowRemoved(LiveWindowKey window) override;
  void OnProjectionChanged(const ProjectionChange& change,
                           const WindowProjection& projection) override;

  // WorkspaceSwitchObserver:
  void OnWorkspaceSwitchPhaseChanged(
      WorkspaceSwitchPhase phase,
      std::optional<ProjectionError> error) override;

 private:
  void Recompute(bool publish);
  void Publish();
  bool SnapshotsEqual(const ShellSnapshot& a, const ShellSnapshot& b) const;

  ShellWindowKey window_;
  raw_ptr<Profile> profile_;
  raw_ptr<OrganizationModel> model_;
  raw_ptr<ProjectionService> projection_service_;
  raw_ptr<LiveWindowStateProvider> live_state_;
  raw_ptr<CommandExecutor> executor_;
  raw_ptr<LifecycleCoordinator> lifecycle_;
  bool recovery_required_ = false;
  bool collapsed_ = false;
  ShellTaskSummary task_summary_;
  bool shutting_down_ = false;
  bool initialized_ = false;  // first snapshot established + published
  // Directly observed workspace-switch transaction state (not inferred).
  WorkspaceSwitchPhase observed_switch_phase_ = WorkspaceSwitchPhase::kIdle;
  bool switch_failed_ = false;
  LiveWindowSnapshot live_;
  ShellSnapshot snapshot_;
  uint64_t revision_ = 0;
  base::RepeatingCallback<MutationStatus()> acknowledge_recovery_callback_;
  base::RepeatingCallback<bool()> open_canvas_callback_;
  base::RepeatingCallback<bool()> open_boost_callback_;
  base::RepeatingCallback<bool()> open_settings_callback_;
  base::RepeatingCallback<bool()> open_downloads_callback_;
  base::RepeatingCallback<ShellProjectResources(WorkspaceId)>
      project_resources_callback_;
  base::RepeatingCallback<bool(WorkspaceId)> create_project_chat_callback_;
  base::RepeatingCallback<bool(const std::string&)> open_project_chat_callback_;
  base::RepeatingCallback<bool(WorkspaceId)> open_project_files_callback_;
  base::RepeatingCallback<ShellCompactModeState(LiveWindowKey)>
      compact_mode_state_callback_;
  base::RepeatingCallback<bool(LiveWindowKey, bool)> set_compact_mode_callback_;
  base::RepeatingCallback<ShellAppearanceLayoutState(LiveWindowKey)>
      appearance_layout_state_callback_;
  base::RepeatingCallback<bool(LiveWindowKey, ShellAppearanceLayoutMode)>
      set_appearance_layout_callback_;
  base::RepeatingCallback<bool(LiveWindowKey)> focus_window_callback_;
  base::ObserverList<ShellObserver> observers_;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_SHELL_CONTROLLER_H_
