// Project Seoul native browser shell V0.

#include "seoul/browser/shell/shell_service.h"

#include "base/functional/bind.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
// nogncheck: //chrome/browser/ui reaches this target through the side-panel
// Canvas registration, so a declared dep would be a dependency cycle; the
// symbols link through //chrome/browser like the other circular includes.
#include "chrome/browser/ui/side_panel/side_panel_ui.h"  // nogncheck
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"  // nogncheck
#include "components/sessions/core/session_id.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/views/seoul_shell_region_host.h"
#include "seoul/browser/shell/views/seoul_workspace_name_dialog.h"
#include "seoul/browser/shell/workspace_icon_painter.h"
#include "ui/base/base_window.h"

namespace seoul {
namespace {

BrowserWindowInterface* FindBrowser(Profile* profile,
                                    LiveWindowKey bound_window) {
  if (!profile || !bound_window.is_valid()) {
    return nullptr;
  }
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile);
  BrowserWindowInterface* browser =
      collection ? collection->FindBrowserWithID(SessionID::FromSerializedValue(
                       bound_window.session_id()))
                 : nullptr;
  if (!browser || browser->GetProfile() != profile ||
      browser->IsDeleteScheduled() ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    return nullptr;
  }
  return browser;
}

}  // namespace

ShellService::ShellService(Profile* profile,
                           OrganizationModel* model,
                           ProjectionService* projection_service,
                           LiveWindowStateProvider* live_state,
                           CommandExecutor* executor,
                           LifecycleCoordinator* lifecycle,
                           bool recovery_required,
                           AcknowledgeRecoveryCallback acknowledge_recovery)
    : profile_(profile),
      model_(model),
      projection_service_(projection_service),
      live_state_(live_state),
      executor_(executor),
      lifecycle_(lifecycle),
      recovery_required_(recovery_required),
      acknowledge_recovery_(std::move(acknowledge_recovery)) {
  if (model_) {
    model_->AddObserver(this);
  }
}

ShellService::~ShellService() {
  Shutdown();
}

void ShellService::Shutdown() {
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  weak_factory_.InvalidateWeakPtrs();
  if (model_) {
    model_->RemoveObserver(this);
  }
  // Destroy hosts first: each host detaches its shell child views (which
  // unobserve their controller) while the controllers are still alive.
  hosts_.clear();
  for (auto& [window, controller] : controllers_) {
    controller->Shutdown();
    (void)window;
  }
  controllers_.clear();
  task_summaries_.clear();
}

ShellController* ShellService::GetController(ShellWindowKey window) {
  auto it = controllers_.find(window);
  return it != controllers_.end() ? it->second.get() : nullptr;
}

ShellController& ShellService::EnsureController(ShellWindowKey window) {
  auto it = controllers_.find(window);
  if (it == controllers_.end()) {
    auto controller = std::make_unique<ShellController>(
        window, profile_, model_, projection_service_, live_state_, executor_,
        lifecycle_, recovery_required_);
    controller->SetAcknowledgeRecoveryCallback(acknowledge_recovery_);
    controller->SetCompactModeCallbacks(compact_mode_state_callback_,
                                        set_compact_mode_callback_);
    controller->SetAppearanceLayoutModeCallbacks(
        appearance_layout_state_callback_, set_appearance_layout_callback_);
    controller->SetOpenBoostCallback(base::BindRepeating(
        [](OpenBoostCallback* callback, LiveWindowKey window) {
          return callback && !callback->is_null() && callback->Run(window);
        },
        base::Unretained(&open_boost_callback_), window));
    controller->SetBeginCaptureCallback(base::BindRepeating(
        [](BeginCaptureCallback* callback, LiveWindowKey window) {
          return callback && !callback->is_null() && callback->Run(window);
        },
        base::Unretained(&begin_capture_callback_), window));
    controller->SetProjectCallbacks(
        project_resources_callback_,
        base::BindRepeating(
            [](CreateProjectChatCallback* callback, LiveWindowKey window,
               WorkspaceId workspace) {
              return callback && !callback->is_null() &&
                     callback->Run(window, workspace);
            },
            base::Unretained(&create_project_chat_callback_), window),
        base::BindRepeating(
            [](OpenProjectChatCallback* callback, LiveWindowKey window,
               const std::string& thread_id) {
              return callback && !callback->is_null() &&
                     callback->Run(window, thread_id);
            },
            base::Unretained(&open_project_chat_callback_), window),
        base::BindRepeating(
            [](OpenProjectFilesCallback* callback, LiveWindowKey window,
               WorkspaceId workspace) {
              return callback && !callback->is_null() &&
                     callback->Run(window, workspace);
            },
            base::Unretained(&open_project_files_callback_), window));
    controller->SetFocusWindowCallback(base::BindRepeating(
        [](Profile* profile, LiveWindowKey target) {
          if (!profile || !target.is_valid()) {
            return false;
          }
          ProfileBrowserCollection* collection =
              ProfileBrowserCollection::GetForProfile(profile);
          BrowserWindowInterface* browser =
              collection
                  ? collection->FindBrowserWithID(
                        SessionID::FromSerializedValue(target.session_id()))
                  : nullptr;
          if (!browser || browser->GetProfile() != profile ||
              browser->IsDeleteScheduled() ||
              browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
              !browser->GetWindow()) {
            return false;
          }
          browser->GetWindow()->Activate();
          return true;
        },
        profile_.get()));
    if (auto summary = task_summaries_.find(window);
        summary != task_summaries_.end()) {
      controller->SetTaskSummary(summary->second);
    }
    it = controllers_.emplace(window, std::move(controller)).first;
  }
  return *it->second;
}

bool ShellService::ShowCreateWorkspaceDialog(ShellWindowKey window,
                                             bool isolated) {
  auto* browser = FindBrowser(profile_, window);
  if (shutting_down_ || !browser || !browser->GetWindow()) {
    return false;
  }
  ShowWorkspaceNameDialog(
      browser->GetWindow()->GetNativeWindow(),
      isolated ? u"New Container Space" : u"New Space", u"Space name", u"",
      base::BindOnce(&ShellService::CreateWorkspaceFromDialog,
                     weak_factory_.GetWeakPtr(), window, isolated),
      isolated
          ? u"Keep accounts, cookies, and site data separate in this Space. "
            u"History, bookmarks, and extensions remain shared."
          : u"Organize tabs in a Space using your shared browser accounts.");
  return true;
}

void ShellService::CreateWorkspaceFromDialog(ShellWindowKey window,
                                             bool isolated,
                                             std::string name) {
  if (shutting_down_ || !FindBrowser(profile_, window) || !model_) {
    return;
  }
  auto created = model_->CreateWorkspace(name, isolated);
  if (created.has_value()) {
    if (isolated) {
      std::ignore = model_->SetWorkspaceIcon(
          created.value(), WorkspaceBuiltinIconRef("lock-closed"));
    }
    if (auto* controller = GetController(window)) {
      std::ignore = controller->SwitchWorkspace(created.value());
    }
  }
}

void ShellService::RegisterVerticalRegion(
    ShellWindowKey window,
    VerticalTabStripRegionView* region,
    BrowserWindowInterface* browser_window) {
  if (shutting_down_ || !window.is_valid() || !region || !browser_window) {
    return;
  }
  const SessionID& browser_id = browser_window->GetSessionID();
  if (browser_window->GetProfile() != profile_ ||
      browser_window->IsDeleteScheduled() ||
      browser_window->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
      !browser_id.is_valid() || browser_id.id() != window.session_id()) {
    return;
  }
  ShellController& controller = EnsureController(window);
  // The vertical region publishes its initial collapse state while building
  // its tab tree, before this host exists. Seed from the live controller on
  // every attachment instead of waiting for a subsequent collapse event.
  const auto* vertical_tabs =
      tabs::VerticalTabStripStateController::From(browser_window);
  const bool collapsed = vertical_tabs && vertical_tabs->IsCollapsed();
  controller.SetCollapsed(collapsed);
  controller.SetNewTabInputCallback(base::BindRepeating(
      [](Profile* profile, LiveWindowKey bound_window) {
        auto* browser = FindBrowser(profile, bound_window);
        return browser && chrome::ExecuteCommand(browser, IDC_NEW_TAB);
      },
      profile_.get(), window));
  controller.SetCreateWorkspaceCallback(base::BindRepeating(
      [](base::WeakPtr<ShellService> service, ShellWindowKey window,
         bool isolated) {
        return service && service->ShowCreateWorkspaceDialog(window, isolated);
      },
      weak_factory_.GetWeakPtr(), window));
  controller.SetBrowserPageCallbacks(
      base::BindRepeating(
          [](Profile* profile, LiveWindowKey bound_window) {
            BrowserWindowInterface* browser =
                FindBrowser(profile, bound_window);
            if (!browser) {
              return false;
            }
            chrome::ShowSettings(browser);
            return true;
          },
          profile_.get(), window),
      base::BindRepeating(
          [](Profile* profile, LiveWindowKey bound_window) {
            BrowserWindowInterface* browser =
                FindBrowser(profile, bound_window);
            if (!browser) {
              return false;
            }
            chrome::ShowDownloads(browser);
            return true;
          },
          profile_.get(), window));
  controller.SetOpenCanvasCallback(base::BindRepeating(
      [](Profile* profile, LiveWindowKey bound_window) {
        if (!profile || !bound_window.is_valid()) {
          return false;
        }
        ProfileBrowserCollection* collection =
            ProfileBrowserCollection::GetForProfile(profile);
        BrowserWindowInterface* browser =
            collection
                ? collection->FindBrowserWithID(
                      SessionID::FromSerializedValue(bound_window.session_id()))
                : nullptr;
        if (!browser || browser->GetProfile() != profile ||
            browser->IsDeleteScheduled() ||
            browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
          return false;
        }
        SidePanelUI* side_panel = browser->GetFeatures().side_panel_ui();
        if (!side_panel) {
          return false;
        }
        if (side_panel->IsSidePanelShowing() &&
            side_panel->GetCurrentEntryId() == SidePanelEntryId::kSeoulCanvas) {
          side_panel->Close();
        } else {
          side_panel->Show(SidePanelEntryId::kSeoulCanvas);
        }
        return true;
      },
      profile_.get(), window));
  // Deterministic duplicate handling: replacing the entry destroys any prior
  // host (detaching its child views) before the new one attaches. One host per
  // initialized region; no process-global state.
  std::unique_ptr<SeoulShellRegionHost>& host = hosts_[window];
  host = std::make_unique<SeoulShellRegionHost>();
  host->Attach(region, &controller, browser_window, profile_);
  host->SetPresentationCollapsed(collapsed && !region->is_expanded_on_hover());
}

void ShellService::UnregisterVerticalRegion(ShellWindowKey window) {
  // Destroy the host first (detaches shell child views while the controller is
  // still alive), then tear down the per-window controller binding.
  hosts_.erase(window);
  if (auto it = controllers_.find(window); it != controllers_.end()) {
    it->second->Shutdown();
    controllers_.erase(it);
  }
}

SeoulShellHeaderView* ShellService::GetHeaderForTesting(ShellWindowKey window) {
  auto host = hosts_.find(window);
  return host != hosts_.end() && host->second
             ? host->second->header_for_testing()
             : nullptr;
}

SeoulShellFooterView* ShellService::GetFooterForTesting(ShellWindowKey window) {
  auto host = hosts_.find(window);
  return host != hosts_.end() && host->second
             ? host->second->footer_for_testing()
             : nullptr;
}

void ShellService::OnCollapseStateChanged(ShellWindowKey window,
                                          bool collapsed) {
  if (ShellController* controller = GetController(window)) {
    controller->SetCollapsed(collapsed);
  }
}

void ShellService::OnSidebarPresentationChanged(ShellWindowKey window,
                                                bool collapsed) {
  if (auto it = hosts_.find(window); it != hosts_.end() && it->second) {
    it->second->SetPresentationCollapsed(collapsed);
  }
}

bool ShellService::ShowCommandLauncher(ShellWindowKey window) {
  const auto it = hosts_.find(window);
  return it != hosts_.end() && it->second && it->second->ShowCommandLauncher();
}

void ShellService::SetCommandLauncherVisible(ShellWindowKey window,
                                             bool visible) {
  if (auto it = hosts_.find(window); it != hosts_.end() && it->second) {
    it->second->SetCommandLauncherVisible(visible);
  }
}

void ShellService::RefreshCompactModeState(ShellWindowKey window) {
  if (ShellController* controller = GetController(window)) {
    controller->RefreshCompactModeState();
  }
}

void ShellService::RefreshAppearanceLayoutModeState(ShellWindowKey window) {
  if (ShellController* controller = GetController(window)) {
    controller->RefreshAppearanceLayoutModeState();
  }
}

void ShellService::RefreshProjectResources() {
  for (auto& [window, controller] : controllers_) {
    controller->RefreshProjectResources();
    (void)window;
  }
}

void ShellService::SetCompactModeCallbacks(CompactModeStateCallback state,
                                           SetCompactModeCallback set) {
  compact_mode_state_callback_ = std::move(state);
  set_compact_mode_callback_ = std::move(set);
  for (auto& [window, controller] : controllers_) {
    controller->SetCompactModeCallbacks(compact_mode_state_callback_,
                                        set_compact_mode_callback_);
    (void)window;
  }
}

void ShellService::SetAppearanceLayoutModeCallbacks(
    AppearanceLayoutModeStateCallback state,
    SetAppearanceLayoutModeCallback set) {
  appearance_layout_state_callback_ = std::move(state);
  set_appearance_layout_callback_ = std::move(set);
  for (auto& [window, controller] : controllers_) {
    controller->SetAppearanceLayoutModeCallbacks(
        appearance_layout_state_callback_, set_appearance_layout_callback_);
    (void)window;
  }
}

void ShellService::SetOpenBoostCallback(OpenBoostCallback callback) {
  open_boost_callback_ = std::move(callback);
  for (auto& [window, controller] : controllers_) {
    controller->SetOpenBoostCallback(base::BindRepeating(
        [](OpenBoostCallback* callback, LiveWindowKey bound_window) {
          return callback && !callback->is_null() &&
                 callback->Run(bound_window);
        },
        base::Unretained(&open_boost_callback_), window));
  }
}

void ShellService::SetBeginCaptureCallback(BeginCaptureCallback callback) {
  begin_capture_callback_ = std::move(callback);
  for (auto& [window, controller] : controllers_) {
    controller->SetBeginCaptureCallback(base::BindRepeating(
        [](BeginCaptureCallback* callback, LiveWindowKey bound_window) {
          return callback && !callback->is_null() &&
                 callback->Run(bound_window);
        },
        base::Unretained(&begin_capture_callback_), window));
  }
}

void ShellService::SetProjectCallbacks(ProjectResourcesCallback resources,
                                       CreateProjectChatCallback create_chat,
                                       OpenProjectChatCallback open_chat,
                                       OpenProjectFilesCallback open_files) {
  project_resources_callback_ = std::move(resources);
  create_project_chat_callback_ = std::move(create_chat);
  open_project_chat_callback_ = std::move(open_chat);
  open_project_files_callback_ = std::move(open_files);
  for (auto& [window, controller] : controllers_) {
    controller->SetProjectCallbacks(
        project_resources_callback_,
        base::BindRepeating(
            [](CreateProjectChatCallback* callback, LiveWindowKey bound_window,
               WorkspaceId workspace) {
              return callback && !callback->is_null() &&
                     callback->Run(bound_window, workspace);
            },
            base::Unretained(&create_project_chat_callback_), window),
        base::BindRepeating(
            [](OpenProjectChatCallback* callback, LiveWindowKey bound_window,
               const std::string& thread_id) {
              return callback && !callback->is_null() &&
                     callback->Run(bound_window, thread_id);
            },
            base::Unretained(&open_project_chat_callback_), window),
        base::BindRepeating(
            [](OpenProjectFilesCallback* callback, LiveWindowKey bound_window,
               WorkspaceId workspace) {
              return callback && !callback->is_null() &&
                     callback->Run(bound_window, workspace);
            },
            base::Unretained(&open_project_files_callback_), window));
  }
}

void ShellService::UpdateTaskSummary(ShellWindowKey window,
                                     ShellTaskSummary summary) {
  if (shutting_down_ || !window.is_valid()) {
    return;
  }
  task_summaries_[window] = summary;
  if (ShellController* controller = GetController(window)) {
    controller->SetTaskSummary(summary);
  }
}

void ShellService::ClearTaskSummaries() {
  task_summaries_.clear();
  for (auto& [window, controller] : controllers_) {
    controller->SetTaskSummary(ShellTaskSummary());
    (void)window;
  }
}

void ShellService::OnOrganizationChanged(const OrganizationChange& change) {
  (void)change;
}

}  // namespace seoul
