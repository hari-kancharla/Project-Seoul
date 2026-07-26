// Project Seoul product runtime - the profile-scoped product owner.

#include "seoul/browser/product/browser/seoul_runtime_service.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "components/download/public/common/download_item.h"
#include "components/permissions/permission_request_manager.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/session_id.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "seoul/browser/commands/command_executor.h"
#include "seoul/browser/connectors/connector.h"
#include "seoul/browser/connectors/connector_registry.h"
#include "seoul/browser/library/library_service.h"
#include "seoul/browser/lifecycle/persistence_scheduler.h"
#include "seoul/browser/organization/organization_limits.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/preview/preview_host_service.h"
#include "seoul/browser/product/browser/browser_capabilities.h"
#include "seoul/browser/product/browser/boost_entry_points.h"
#include "seoul/browser/product/browser/keychain_credential_store.h"
#include "seoul/browser/product/browser/network_http_transport.h"
#include "seoul/browser/product/browser/page_agent.h"
#include "seoul/browser/product/browser/site_layer_applicator.h"
#include "seoul/browser/product/live_collection_coordinator.h"
#include "seoul/browser/scenes/scene_registry.h"
#include "seoul/browser/shell/shell_service.h"
#include "seoul/browser/site_layers/site_layer_registry.h"
#include "seoul/browser/themes/theme_registry.h"
#include "url/gurl.h"

namespace seoul {

namespace {

base::Time Now() { return base::Time::Now(); }

constexpr base::TimeDelta kSceneLifecycleSweepInterval = base::Minutes(1);
constexpr base::TimeDelta kLiveCollectionSweepInterval = base::Minutes(1);
constexpr base::TimeDelta kSceneRestoreRetryDelay = base::Milliseconds(100);
constexpr base::TimeDelta kSceneRestoreContentionBudget = base::Seconds(30);
constexpr int kPresentationSchemaVersion = 1;
constexpr int kCompactModeSchemaVersion = 1;

bool OpenSeoulProjectRoute(Profile* profile,
                           LiveWindowKey window,
                           const GURL& url) {
  if (!profile || !window.is_valid() || !url.is_valid()) {
    return false;
  }
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile);
  BrowserWindowInterface* browser =
      collection
          ? collection->FindBrowserWithID(
                SessionID::FromSerializedValue(window.session_id()))
          : nullptr;
  if (!browser || browser->GetProfile() != profile ||
      browser->IsDeleteScheduled() ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    return false;
  }
  chrome::AddTabAt(browser, url, -1, true);
  return true;
}

// Builds the scene resolvers over the organization model plus runtime-owned
// catalogs. Every reference resolves against its authoritative owner so a
// Scene cannot retain a phantom workspace, Theme, or Site Layer.
SceneResolvers MakeSceneResolvers(SeoulOrganizationService *organization,
                                  ThemeRegistry *themes,
                                  SiteLayerRegistry *site_layers) {
  SceneResolvers resolvers;
  resolvers.workspace_exists = base::BindRepeating(
      [](SeoulOrganizationService *org, const std::string &workspace_id) {
        return org && org->model().FindWorkspace(
                          WorkspaceId::FromString(workspace_id)) != nullptr;
      },
      organization);
  resolvers.theme_exists = base::BindRepeating(
      [](ThemeRegistry *registry, const std::string &theme_id) {
        return registry && registry->Exists(theme_id);
      },
      themes);
  resolvers.site_layer_exists = base::BindRepeating(
      [](SiteLayerRegistry *registry, const std::string &site_layer_id) {
        return registry && registry->Exists(site_layer_id);
      },
      site_layers);
  resolvers.routing_rule_exists = base::BindRepeating(
      [](SeoulOrganizationService *org, const std::string &routing_rule_id) {
        if (!org) {
          return false;
        }
        const RoutingRuleId id = RoutingRuleId::FromString(routing_rule_id);
        if (!id.is_valid()) {
          return false;
        }
        const OrganizationSnapshot snapshot = org->model().ToSnapshot();
        return std::ranges::any_of(
            snapshot.routing_rules,
            [&id](const RoutingRule &rule) { return rule.id == id; });
      },
      organization);
  return resolvers;
}

} // namespace

SeoulRuntimeService::SeoulRuntimeService(
    Profile *profile, PrefService *prefs,
    SeoulOrganizationService *organization,
    WebContentsResolver web_contents_resolver)
    : profile_(profile), prefs_(prefs), organization_(organization),
      web_contents_resolver_(web_contents_resolver),
      themes_(std::make_unique<ThemeRegistry>()),
      site_layers_(std::make_unique<SiteLayerRegistry>()),
      library_service_(std::make_unique<LibraryService>(
          base::BindRepeating(&Now),
          base::BindRepeating(&SeoulRuntimeService::OnProjectResourcesChanged,
                              base::Unretained(this)))),
      runtime_(
          MakeSceneResolvers(organization, themes_.get(), site_layers_.get())) {
  // Concrete transports: the general one for cloud/connectors, a
  // loopback-only one for the local reasoning provider.
  scoped_refptr<network::SharedURLLoaderFactory> factory =
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess();
  cloud_transport_ = std::make_unique<NetworkHttpTransport>(
      factory, NetworkHttpTransport::Mode::kGeneral);
  local_transport_ = std::make_unique<NetworkHttpTransport>(
      factory, NetworkHttpTransport::Mode::kLoopbackOnly);
  credentials_ = std::make_unique<KeychainCredentialStore>(
      profile_->GetBaseName().MaybeAsASCII());
  page_agent_ = std::make_unique<PageAgent>(web_contents_resolver_);

  provider_registry_ = std::make_unique<ProviderRegistry>(
      local_transport_.get(), cloud_transport_.get(), credentials_.get());
  preview_manager_ = std::make_unique<PreviewManager>(
      base::BindRepeating(&Now), base::BindRepeating(&PreviewId::GenerateNew));
  preview_host_service_ = std::make_unique<PreviewHostService>(
      profile_, preview_manager_.get(),
      organization_ ? organization_->lifecycle_coordinator() : nullptr,
      base::BindRepeating(&SeoulRuntimeService::ResolvePreviewPromotionRoute,
                          base::Unretained(this)),
      base::BindRepeating(&SeoulRuntimeService::SwitchWorkspaceForPreview,
                          base::Unretained(this)));
  realtime_voice_agent_ = std::make_unique<RealtimeVoiceAgent>(
      cloud_transport_.get(), credentials_.get());
  planner_ = std::make_unique<Planner>(runtime_.capabilities(),
                                       provider_registry_->MakePlanRequester());
  agent_permissions_ =
      std::make_unique<AgentPermissionService>(base::BindRepeating(&Now));
  if (organization_) {
    if (LiveWindowStateProvider *live_state =
            organization_->live_window_state_provider()) {
      for (const LiveWindowKey &window : live_state->Windows()) {
        if (std::optional<LiveWindowSnapshot> snapshot =
                live_state->GetSnapshot(window)) {
          OnLiveWindowSnapshotChanged(*snapshot);
        }
      }
      live_window_observation_.Observe(live_state);
    }
  }
  task_service_ = std::make_unique<TaskService>(
      &runtime_.capabilities(), &executors_, planner_.get(),
      base::BindRepeating(&Now), agent_permissions_.get(),
      base::BindRepeating(&SeoulRuntimeService::ResolveAgentPermissionRequest,
                          base::Unretained(this)));
  task_service_->AddObserver(this);
  voice_controller_ = std::make_unique<VoiceRuntimeController>(
      task_service_.get(), speech_to_text_.get(), text_to_speech_.get(),
      base::BindRepeating(&SeoulRuntimeService::StartGoal,
                          base::Unretained(this)),
      base::BindRepeating(&Now));
  surface_service_ = std::make_unique<SurfaceService>();
  // The bridge is the production path that turns a verified task result into a
  // Canvas surface; without it, tasks would complete with no artifact.
  task_surface_bridge_ = std::make_unique<TaskSurfaceBridge>(
      task_service_.get(), surface_service_.get());
  thread_service_ = std::make_unique<ThreadService>(
      base::BindRepeating(&Now),
      base::BindRepeating(&SeoulRuntimeService::OnProjectResourcesChanged,
                          base::Unretained(this)));
  workflow_service_ = std::make_unique<WorkflowService>(
      task_service_.get(), base::BindRepeating(&Now));
  SceneResolvers scene_resolvers =
      MakeSceneResolvers(organization_, themes_.get(), site_layers_.get());
  scene_resolvers.workflow_exists = base::BindRepeating(
      &SeoulRuntimeService::WorkflowExists, base::Unretained(this));
  runtime_.scenes().SetResolvers(std::move(scene_resolvers));

  RegisterBuiltinExecutors();
  LoadState();
  live_collection_coordinator_ =
      std::make_unique<LiveCollectionCoordinator>(
          library_service_.get(), &runtime_.capabilities(), &executors_);
  product_state_loaded_ = true;
  // Live windows can be discovered before the product catalogs and
  // presentations are loaded. Bind restored presentation intent only after
  // every referenced Workspace, Scene, and Theme is authoritative.
  for (const auto &[window, tabs] : live_tabs_by_window_) {
    RestorePresentationForWindow(window);
    ApplyStandaloneCompactMode(window, /*seed_if_missing=*/true);
  }
  if (organization_ && organization_->shell_service()) {
    organization_->shell_service()->SetOpenBoostCallback(
        base::BindRepeating(
            [](SeoulRuntimeService* runtime, LiveWindowKey window) {
              if (!runtime) {
                return false;
              }
              const std::optional<LiveTabDescriptor> active =
                  runtime->ActiveTabDescriptor(window);
              content::WebContents* contents =
                  active.has_value() && runtime->web_contents_resolver_
                      ? runtime->web_contents_resolver_.Run(active->tab)
                      : nullptr;
              return OpenBoostEditorForWebContents(contents);
            },
            base::Unretained(this)));
    organization_->shell_service()->SetCompactModeCallbacks(
        base::BindRepeating(
            [](SeoulRuntimeService *runtime, LiveWindowKey window) {
              ShellCompactModeState state;
              if (!runtime) {
                state.disabled_reason =
                    "Compact mode is unavailable for this window.";
                return state;
              }
              const std::optional<bool> enabled =
                  runtime->CompactModeForWindow(window);
              state.available = enabled.has_value();
              state.enabled = enabled.value_or(false);
              if (!state.available) {
                state.disabled_reason =
                    runtime->ActiveSceneForWindow(window).empty()
                        ? "Compact mode is unavailable for this window."
                        : "Compact mode is controlled by the active Scene.";
              }
              return state;
            },
            base::Unretained(this)),
        base::BindRepeating(
            [](SeoulRuntimeService *runtime, LiveWindowKey window,
               bool enabled) {
              return runtime && runtime->SetCompactMode(enabled, window);
            },
            base::Unretained(this)));
    organization_->shell_service()->SetProjectCallbacks(
        base::BindRepeating(
            [](SeoulRuntimeService* runtime, WorkspaceId workspace) {
              ShellProjectResources resources;
              if (!runtime || !runtime->thread_service_) {
                return resources;
              }
              const std::string workspace_id = workspace.value();
              const WorkspaceId default_workspace =
                  runtime->organization_
                      ? runtime->organization_->model().default_workspace()
                      : WorkspaceId();
              for (const ThreadSummary& summary :
                   runtime->thread_service_->Summaries()) {
                const bool legacy_default =
                    summary.workspace_id.empty() &&
                    workspace == default_workspace;
                if (summary.workspace_id != workspace_id &&
                    !legacy_default) {
                  continue;
                }
                ShellChatItem chat;
                chat.id = summary.id;
                chat.name = summary.name;
                chat.archived = summary.archived;
                chat.item_count = summary.item_count;
                resources.chats.push_back(std::move(chat));
              }
              if (runtime->library_service_) {
                resources.file_count =
                    runtime->library_service_->artifact_count();
                resources.board_count =
                    runtime->library_service_->board_count();
              }
              return resources;
            },
            base::Unretained(this)),
        base::BindRepeating(
            [](SeoulRuntimeService* runtime, LiveWindowKey window,
               WorkspaceId workspace) {
              if (!runtime || !runtime->thread_service_ ||
                  !workspace.is_valid()) {
                return false;
              }
              size_t existing = 0;
              for (const ThreadSummary& summary :
                   runtime->thread_service_->Summaries()) {
                if (summary.workspace_id == workspace.value()) {
                  ++existing;
                }
              }
              std::string name = "New chat";
              if (existing > 0) {
                name += " " + base::NumberToString(existing + 1);
              }
              const std::string id =
                  runtime->thread_service_->CreateThread(name,
                                                         workspace.value());
              return !id.empty() &&
                     OpenSeoulProjectRoute(
                         runtime->profile_, window,
                         GURL("chrome://seoul-canvas/?view=chat&thread=" + id));
            },
            base::Unretained(this)),
        base::BindRepeating(
            [](SeoulRuntimeService* runtime, LiveWindowKey window,
               const std::string& thread_id) {
              return runtime && runtime->thread_service_ &&
                     runtime->thread_service_->FindThread(thread_id) &&
                     OpenSeoulProjectRoute(
                         runtime->profile_, window,
                         GURL("chrome://seoul-canvas/?view=chat&thread=" +
                              thread_id));
            },
            base::Unretained(this)),
        base::BindRepeating(
            [](SeoulRuntimeService* runtime, LiveWindowKey window,
               WorkspaceId workspace) {
              return runtime && workspace.is_valid() &&
                     OpenSeoulProjectRoute(
                         runtime->profile_, window,
                         GURL("chrome://seoul-canvas/?view=library&workspace=" +
                              workspace.value()));
            },
            base::Unretained(this)));
  }
  // Applicators may already exist because live-window state is replayed during
  // construction, while the registry is restored only by LoadState(). Apply
  // the restored layers now rather than waiting for a later tab event.
  RefreshSiteLayers();
  persistence_scheduler_ = std::make_unique<PersistenceScheduler>(
      base::BindRepeating(&SeoulRuntimeService::PersistState,
                          base::Unretained(this)),
      base::SequencedTaskRunner::GetCurrentDefault());
  if (organization_ && organization_->command_executor()) {
    organization_->command_executor()->AddCompletionObserver(this);
  }
  if (organization_) {
    organization_->model().AddObserver(this);
  }
  scene_lifecycle_timer_.Start(
      FROM_HERE, kSceneLifecycleSweepInterval,
      base::BindRepeating(&SeoulRuntimeService::RunSceneLifecycleMaintenance,
                          base::Unretained(this)));
  live_collection_timer_.Start(
      FROM_HERE, kLiveCollectionSweepInterval,
      base::BindRepeating(
          &SeoulRuntimeService::RunLiveCollectionMaintenance,
          base::Unretained(this)));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&SeoulRuntimeService::RunLiveCollectionMaintenance,
                     weak_factory_.GetWeakPtr()));
}

SeoulRuntimeService::~SeoulRuntimeService() = default;

LiveWindowStateProvider *
SeoulRuntimeService::live_window_state_provider() const {
  return organization_ ? organization_->live_window_state_provider() : nullptr;
}

void SeoulRuntimeService::OnLiveWindowSnapshotChanged(
    const LiveWindowSnapshot &snapshot) {
  if (shutting_down_ || !snapshot.window.is_valid()) {
    return;
  }
  std::set<LiveTabKey> current_tabs;
  for (const LiveTabDescriptor &descriptor : snapshot.tabs) {
    if (descriptor.tab.is_valid()) {
      current_tabs.insert(descriptor.tab);
    }
  }

  auto it =
      live_tabs_by_window_.try_emplace(snapshot.window, std::set<LiveTabKey>())
          .first;
  for (const LiveTabKey &previous : it->second) {
    if (!current_tabs.contains(previous)) {
      if (agent_permissions_) {
        agent_permissions_->RevokeTab(previous);
      }
      if (preview_host_service_) {
        preview_host_service_->DismissForParent(previous);
      }
      site_layer_applicators_.erase(previous);
    }
  }
  for (const LiveTabKey &current : current_tabs) {
    content::WebContents *contents = web_contents_resolver_.Run(current);
    if (!contents) {
      site_layer_applicators_.erase(current);
      continue;
    }
    auto applicator = site_layer_applicators_.find(current);
    if (applicator != site_layer_applicators_.end() &&
        !applicator->second->IsAttachedTo(contents)) {
      site_layer_applicators_.erase(applicator);
      applicator = site_layer_applicators_.end();
    }
    if (applicator == site_layer_applicators_.end()) {
      applicator = site_layer_applicators_
                       .emplace(current, std::make_unique<SiteLayerApplicator>(
                                             contents, site_layers_.get()))
                       .first;
    }
    applicator->second->Refresh(ActiveSceneForWindow(snapshot.window));
  }
  it->second = std::move(current_tabs);
  if (RestorePresentationForWindow(snapshot.window)) {
    RefreshSiteLayers();
  }
  if (product_state_loaded_) {
    ApplyStandaloneCompactMode(snapshot.window, /*seed_if_missing=*/true);
    // A restored collection may have been waiting for this exact live window.
    // Post the sweep so a synchronous lifecycle publication cannot recursively
    // execute a source inside the observer stack.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&SeoulRuntimeService::RunLiveCollectionMaintenance,
                       weak_factory_.GetWeakPtr()));
  }
}

void SeoulRuntimeService::OnLiveWindowRemoved(LiveWindowKey window) {
  if (live_collection_coordinator_) {
    live_collection_coordinator_->CancelForWindow(window);
  }
  auto it = live_tabs_by_window_.find(window);
  if (it != live_tabs_by_window_.end()) {
    for (const LiveTabKey &tab : it->second) {
      site_layer_applicators_.erase(tab);
    }
    live_tabs_by_window_.erase(it);
  }
  if (preview_host_service_) {
    preview_host_service_->DismissForWindow(window);
  }
  active_scenes_by_window_.erase(window);
  pending_boost_editor_windows_.erase(window);
  active_themes_by_window_.erase(window);
  pre_scene_themes_by_window_.erase(window);
  pre_scene_vertical_tabs_by_window_.erase(window);
  for (auto owner = presentation_owners_.begin();
       owner != presentation_owners_.end();) {
    if (owner->second == window) {
      owner = presentation_owners_.erase(owner);
    } else {
      ++owner;
    }
  }
  pending_scene_restores_.erase(window);
  applied_compact_workspace_by_window_.erase(window);
  if (!shutting_down_ && agent_permissions_) {
    agent_permissions_->RevokeWindow(window);
  }
}

void SeoulRuntimeService::OnTaskUpdated(const TaskId &task_id) {
  if (!task_service_ || shutting_down_) {
    return;
  }
  if (std::optional<TaskSnapshot> snapshot = task_service_->Snapshot(task_id)) {
    PublishShellTaskSummary(snapshot->window);
  }
}

void SeoulRuntimeService::OnTaskFinished(const TaskId &task_id) {
  OnTaskUpdated(task_id);
}

void SeoulRuntimeService::OnCommandCompleted(CommandId id, CommandKind kind,
                                             CommandStatus status) {
  if (shutting_down_ || kind != CommandKind::kRestoreArchivedTab) {
    return;
  }
  for (auto &[window, pending] : pending_scene_restores_) {
    if (!pending || pending->active_command != id) {
      continue;
    }
    pending->active_command = CommandId();
    if (!pending->archive_ids.empty()) {
      pending->archive_ids.pop_front();
    }
    // A lifecycle confirmation can arrive synchronously inside Submit().
    // Continue from a timer task instead of recursively submitting hundreds of
    // restored tabs on the same stack.
    ScheduleSceneRestoreContinuation(window, base::TimeDelta());
    return;
  }
}

void SeoulRuntimeService::OnOrganizationChanged(
    const OrganizationChange &change) {
  if (shutting_down_ || scene_reconciliation_pending_) {
    return;
  }
  switch (change.type) {
  case OrganizationChangeType::kWorkspaceArchived:
  case OrganizationChangeType::kWorkspaceDeleted:
  case OrganizationChangeType::kActiveWorkspaceChanged:
  case OrganizationChangeType::kRoutingRuleRemoved:
  case OrganizationChangeType::kSnapshotLoaded:
    break;
  default:
    return;
  }
  scene_reconciliation_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&SeoulRuntimeService::ReconcileScenesWithOrganization,
                     weak_factory_.GetWeakPtr()));
}

void SeoulRuntimeService::ReconcileScenesWithOrganization() {
  scene_reconciliation_pending_ = false;
  if (shutting_down_ || !organization_) {
    return;
  }
  size_t removed = 0;
  if (workflow_service_) {
    const auto scene_exists = base::BindRepeating(
        [](SceneRegistry *scenes, const std::string &scene_id) {
          return scenes && scenes->Find(scene_id);
        },
        &runtime_.scenes());
    while (true) {
      const size_t removed_workflows =
          workflow_service_->PruneInvalidSceneReferences(scene_exists);
      const size_t removed_scenes = runtime_.scenes().PruneInvalidEntries();
      removed += removed_workflows + removed_scenes;
      if (removed_workflows == 0 && removed_scenes == 0) {
        break;
      }
    }
  } else {
    removed += runtime_.scenes().PruneInvalidEntries();
  }

  const auto active_scenes = active_scenes_by_window_;
  for (const auto &[window, scene_id] : active_scenes) {
    const SceneDefinition *scene = runtime_.scenes().Find(scene_id);
    const WorkspaceId active =
        organization_->model().ActiveWorkspaceForWindow(window.value());
    if (!scene || active != WorkspaceId::FromString(scene->workspace_id)) {
      // Clearing a Scene restores its exact pre-Scene Theme/compact baseline.
      std::ignore = ActivateScene(std::string(), window);
    }
  }
  for (auto presentation = presentations_by_workspace_.begin();
       presentation != presentations_by_workspace_.end();) {
    const WorkspacePresentation &value = presentation->second;
    const SceneDefinition *scene =
        value.active_scene_id.empty()
            ? nullptr
            : runtime_.scenes().Find(value.active_scene_id);
    const bool valid_scene =
        value.active_scene_id.empty() ||
        (scene &&
         WorkspaceId::FromString(scene->workspace_id) == presentation->first &&
         scene->theme_id == value.active_theme_id);
    const bool valid_theme =
        value.active_theme_id.empty() ||
        (themes_ && themes_->Exists(value.active_theme_id));
    const bool valid_pre_scene_theme =
        value.pre_scene_theme_id.empty() ||
        (themes_ && themes_->Exists(value.pre_scene_theme_id));
    if (!organization_->model().FindWorkspace(presentation->first) ||
        !valid_scene || !valid_theme || !valid_pre_scene_theme) {
      presentation_owners_.erase(presentation->first);
      presentation = presentations_by_workspace_.erase(presentation);
      ++removed;
    } else {
      ++presentation;
    }
  }
  for (auto compact = compact_mode_by_workspace_.begin();
       compact != compact_mode_by_workspace_.end();) {
    const WorkspaceRecord *workspace =
        organization_->model().FindWorkspace(compact->first);
    if (!workspace || workspace->archived) {
      compact = compact_mode_by_workspace_.erase(compact);
      ++removed;
    } else {
      ++compact;
    }
  }
  for (const auto &[window, tabs] : live_tabs_by_window_) {
    RestorePresentationForWindow(window);
    ApplyStandaloneCompactMode(window, /*seed_if_missing=*/false);
  }
  if (removed > 0) {
    SchedulePersist();
  }
}

bool SeoulRuntimeService::RestorePresentationForWindow(
    const LiveWindowKey &window) {
  if (shutting_down_ || !organization_ || !window.is_valid() ||
      !live_tabs_by_window_.contains(window)) {
    return false;
  }
  const WorkspaceId workspace =
      organization_->model().ActiveWorkspaceForWindow(window.value());
  auto presentation = presentations_by_workspace_.find(workspace);
  if (!workspace.is_valid() ||
      presentation == presentations_by_workspace_.end()) {
    return false;
  }
  auto owner = presentation_owners_.find(workspace);
  if (owner != presentation_owners_.end() && owner->second != window &&
      live_tabs_by_window_.contains(owner->second)) {
    return false;
  }
  const WorkspacePresentation &value = presentation->second;
  const SceneDefinition *scene =
      value.active_scene_id.empty()
          ? nullptr
          : runtime_.scenes().Find(value.active_scene_id);
  if ((!value.active_scene_id.empty() &&
       (!scene || WorkspaceId::FromString(scene->workspace_id) != workspace ||
        scene->theme_id != value.active_theme_id)) ||
      (!value.active_theme_id.empty() &&
       (!themes_ || !themes_->Exists(value.active_theme_id))) ||
      (!value.pre_scene_theme_id.empty() &&
       (!themes_ || !themes_->Exists(value.pre_scene_theme_id)))) {
    return false;
  }
  BrowserWindowInterface *browser = BrowserWindowInterface::FromSessionID(
      SessionID::FromSerializedValue(window.session_id()));
  if (!browser || browser->GetProfile() != profile_ ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
      browser->IsDeleteScheduled()) {
    return false;
  }
  tabs::VerticalTabStripStateController *vertical_tabs =
      tabs::VerticalTabStripStateController::From(browser);
  if (scene && scene->prefer_compact && !vertical_tabs) {
    return false;
  }
  if (owner != presentation_owners_.end() && owner->second == window &&
      ActiveSceneForWindow(window) == value.active_scene_id &&
      ActiveThemeForWindow(window) == value.active_theme_id) {
    return false;
  }

  if (value.active_scene_id.empty()) {
    active_scenes_by_window_.erase(window);
  } else {
    active_scenes_by_window_[window] = value.active_scene_id;
  }
  if (value.active_theme_id.empty()) {
    active_themes_by_window_.erase(window);
  } else {
    active_themes_by_window_[window] = value.active_theme_id;
  }
  if (scene) {
    pre_scene_themes_by_window_[window] = value.pre_scene_theme_id;
    if (value.has_vertical_tabs_baseline) {
      pre_scene_vertical_tabs_by_window_[window] = value.vertical_tabs_baseline;
    } else {
      pre_scene_vertical_tabs_by_window_.erase(window);
    }
    if (vertical_tabs) {
      vertical_tabs->SetExpandOnHoverEnabledForWindow(
          scene->prefer_compact ||
          (value.has_vertical_tabs_baseline &&
           value.vertical_tabs_baseline.expand_on_hover));
      vertical_tabs->RequestCollapse(scene->prefer_compact);
    }
  } else {
    pre_scene_themes_by_window_.erase(window);
    pre_scene_vertical_tabs_by_window_.erase(window);
  }
  presentation_owners_[workspace] = window;
  return true;
}

bool SeoulRuntimeService::ApplyStandaloneCompactMode(
    const LiveWindowKey &window, bool seed_if_missing) {
  if (shutting_down_ || !organization_ || !window.is_valid() ||
      !live_tabs_by_window_.contains(window) ||
      !ActiveSceneForWindow(window).empty()) {
    return false;
  }
  const WorkspaceId workspace =
      organization_->model().ActiveWorkspaceForWindow(window.value());
  const WorkspaceRecord *workspace_record =
      organization_->model().FindWorkspace(workspace);
  BrowserWindowInterface *browser = BrowserWindowInterface::FromSessionID(
      SessionID::FromSerializedValue(window.session_id()));
  tabs::VerticalTabStripStateController *vertical_tabs =
      browser && browser->GetProfile() == profile_ &&
              browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
              !browser->IsDeleteScheduled()
          ? tabs::VerticalTabStripStateController::From(browser)
          : nullptr;
  if (!workspace.is_valid() || !workspace_record ||
      workspace_record->archived || !vertical_tabs ||
      !vertical_tabs->ShouldDisplayVerticalTabs()) {
    return false;
  }

  if (const auto applied = applied_compact_workspace_by_window_.find(window);
      applied != applied_compact_workspace_by_window_.end() &&
      applied->second == workspace) {
    return true;
  }

  auto preference = compact_mode_by_workspace_.find(workspace);
  bool added = false;
  if (preference == compact_mode_by_workspace_.end()) {
    const bool enabled = seed_if_missing &&
                         vertical_tabs->GetCollapseState() !=
                             tabs::VerticalTabStripCollapseState::kExpanded &&
                         vertical_tabs->IsExpandOnHoverEnabled();
    preference = compact_mode_by_workspace_.emplace(workspace, enabled).first;
    added = true;
  }
  vertical_tabs->SetExpandOnHoverEnabledForWindow(preference->second);
  vertical_tabs->RequestCollapse(preference->second);
  applied_compact_workspace_by_window_[window] = workspace;
  if (added) {
    SchedulePersist();
  }
  if (ShellService *shell = organization_->shell_service()) {
    shell->RefreshCompactModeState(window);
  }
  return true;
}

bool SeoulRuntimeService::SetCompactMode(bool enabled,
                                         const LiveWindowKey &window) {
  if (shutting_down_ || !organization_ || !window.is_valid() ||
      !live_tabs_by_window_.contains(window) ||
      !ActiveSceneForWindow(window).empty()) {
    return false;
  }
  const WorkspaceId workspace =
      organization_->model().ActiveWorkspaceForWindow(window.value());
  const WorkspaceRecord *workspace_record =
      organization_->model().FindWorkspace(workspace);
  BrowserWindowInterface *browser = BrowserWindowInterface::FromSessionID(
      SessionID::FromSerializedValue(window.session_id()));
  tabs::VerticalTabStripStateController *vertical_tabs =
      browser && browser->GetProfile() == profile_ &&
              browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
              !browser->IsDeleteScheduled()
          ? tabs::VerticalTabStripStateController::From(browser)
          : nullptr;
  if (!workspace.is_valid() || !workspace_record ||
      workspace_record->archived || !vertical_tabs ||
      !vertical_tabs->ShouldDisplayVerticalTabs()) {
    return false;
  }
  compact_mode_by_workspace_[workspace] = enabled;
  applied_compact_workspace_by_window_.erase(window);
  if (!ApplyStandaloneCompactMode(window, /*seed_if_missing=*/false)) {
    return false;
  }
  SchedulePersist();
  return true;
}

std::optional<bool>
SeoulRuntimeService::CompactModeForWindow(const LiveWindowKey &window) const {
  if (shutting_down_ || !organization_ || !window.is_valid() ||
      !live_tabs_by_window_.contains(window) ||
      !ActiveSceneForWindow(window).empty()) {
    return std::nullopt;
  }
  const WorkspaceId workspace =
      organization_->model().ActiveWorkspaceForWindow(window.value());
  const WorkspaceRecord *workspace_record =
      organization_->model().FindWorkspace(workspace);
  BrowserWindowInterface *browser = BrowserWindowInterface::FromSessionID(
      SessionID::FromSerializedValue(window.session_id()));
  const tabs::VerticalTabStripStateController *vertical_tabs =
      browser && browser->GetProfile() == profile_ &&
              browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
              !browser->IsDeleteScheduled()
          ? tabs::VerticalTabStripStateController::From(browser)
          : nullptr;
  if (!workspace.is_valid() || !workspace_record ||
      workspace_record->archived || !vertical_tabs ||
      !vertical_tabs->ShouldDisplayVerticalTabs()) {
    return std::nullopt;
  }
  if (const auto preference = compact_mode_by_workspace_.find(workspace);
      preference != compact_mode_by_workspace_.end()) {
    return preference->second;
  }
  return vertical_tabs->GetCollapseState() !=
             tabs::VerticalTabStripCollapseState::kExpanded &&
         vertical_tabs->IsExpandOnHoverEnabled();
}

bool SeoulRuntimeService::IsCompactModeApplied(
    bool enabled, const LiveWindowKey &window) const {
  if (!CompactModeForWindow(window).has_value()) {
    return false;
  }
  BrowserWindowInterface *browser = BrowserWindowInterface::FromSessionID(
      SessionID::FromSerializedValue(window.session_id()));
  const tabs::VerticalTabStripStateController *vertical_tabs =
      browser && browser->GetProfile() == profile_
          ? tabs::VerticalTabStripStateController::From(browser)
          : nullptr;
  if (!vertical_tabs) {
    return false;
  }
  const bool collapsed = vertical_tabs->GetCollapseState() ==
                         tabs::VerticalTabStripCollapseState::kCollapsed;
  return enabled ? collapsed && vertical_tabs->IsExpandOnHoverEnabled()
                 : !collapsed && !vertical_tabs->IsExpandOnHoverEnabled() &&
                       vertical_tabs->GetCollapseState() ==
                           tabs::VerticalTabStripCollapseState::kExpanded;
}

base::DictValue SeoulRuntimeService::TakeCompactModeState() const {
  base::DictValue state;
  state.Set("schema_version", kCompactModeSchemaVersion);
  base::ListValue items;
  size_t count = 0;
  for (const auto &[workspace, enabled] : compact_mode_by_workspace_) {
    if (++count > kMaxWindowStates) {
      break;
    }
    base::DictValue item;
    item.Set("workspace_id", workspace.value());
    item.Set("enabled", enabled);
    items.Append(std::move(item));
  }
  state.Set("items", std::move(items));
  return state;
}

void SeoulRuntimeService::RestoreCompactModeState(
    const base::DictValue &state) {
  if (!organization_ || state.FindInt("schema_version").value_or(0) !=
                            kCompactModeSchemaVersion) {
    return;
  }
  const base::ListValue *items = state.FindList("items");
  if (!items || items->size() > kMaxWindowStates) {
    return;
  }
  std::map<WorkspaceId, bool> restored;
  for (const base::Value &entry : *items) {
    const base::DictValue *item = entry.GetIfDict();
    const std::string *workspace_value =
        item ? item->FindString("workspace_id") : nullptr;
    const std::optional<bool> enabled =
        item ? item->FindBool("enabled") : std::nullopt;
    if (!workspace_value || !enabled.has_value() ||
        workspace_value->size() > kMaxTabKeyLength) {
      return;
    }
    const WorkspaceId workspace = WorkspaceId::FromString(*workspace_value);
    const WorkspaceRecord *workspace_record =
        organization_->model().FindWorkspace(workspace);
    if (!workspace.is_valid() || !workspace_record ||
        workspace_record->archived || restored.contains(workspace)) {
      return;
    }
    restored.emplace(workspace, *enabled);
  }
  compact_mode_by_workspace_ = std::move(restored);
  applied_compact_workspace_by_window_.clear();
}

base::DictValue SeoulRuntimeService::TakePresentationState() const {
  base::DictValue state;
  state.Set("schema_version", kPresentationSchemaVersion);
  base::ListValue items;
  size_t count = 0;
  for (const auto &[workspace, presentation] : presentations_by_workspace_) {
    if (++count > kMaxWindowStates) {
      break;
    }
    base::DictValue item;
    item.Set("workspace_id", workspace.value());
    item.Set("active_scene_id", presentation.active_scene_id);
    item.Set("active_theme_id", presentation.active_theme_id);
    item.Set("pre_scene_theme_id", presentation.pre_scene_theme_id);
    item.Set("has_vertical_tabs_baseline",
             presentation.has_vertical_tabs_baseline);
    item.Set("vertical_tabs_collapsed",
             presentation.vertical_tabs_baseline.collapsed);
    item.Set("vertical_tabs_expand_on_hover",
             presentation.vertical_tabs_baseline.expand_on_hover);
    items.Append(std::move(item));
  }
  state.Set("items", std::move(items));
  return state;
}

void SeoulRuntimeService::RestorePresentationState(
    const base::DictValue &state) {
  if (!organization_ || state.FindInt("schema_version").value_or(0) !=
                            kPresentationSchemaVersion) {
    return;
  }
  const base::ListValue *items = state.FindList("items");
  if (!items || items->size() > kMaxWindowStates) {
    return;
  }
  std::map<WorkspaceId, WorkspacePresentation> restored;
  for (const base::Value &entry : *items) {
    const base::DictValue *item = entry.GetIfDict();
    if (!item) {
      return;
    }
    const std::string *workspace_value = item->FindString("workspace_id");
    const std::string *scene_id = item->FindString("active_scene_id");
    const std::string *theme_id = item->FindString("active_theme_id");
    const std::string *pre_scene_theme_id =
        item->FindString("pre_scene_theme_id");
    const std::optional<bool> has_baseline =
        item->FindBool("has_vertical_tabs_baseline");
    const std::optional<bool> collapsed =
        item->FindBool("vertical_tabs_collapsed");
    const std::optional<bool> expand_on_hover =
        item->FindBool("vertical_tabs_expand_on_hover");
    if (!workspace_value || !scene_id || !theme_id || !pre_scene_theme_id ||
        !has_baseline.has_value() || !collapsed.has_value() ||
        !expand_on_hover.has_value() ||
        workspace_value->size() > kMaxTabKeyLength ||
        scene_id->size() > kMaxNameLength ||
        theme_id->size() > kMaxNameLength ||
        pre_scene_theme_id->size() > kMaxNameLength) {
      return;
    }
    const WorkspaceId workspace = WorkspaceId::FromString(*workspace_value);
    const WorkspaceRecord *workspace_record =
        organization_->model().FindWorkspace(workspace);
    const SceneDefinition *scene =
        scene_id->empty() ? nullptr : runtime_.scenes().Find(*scene_id);
    if (!workspace.is_valid() || !workspace_record ||
        workspace_record->archived || restored.contains(workspace) ||
        (!scene_id->empty() &&
         (!scene || WorkspaceId::FromString(scene->workspace_id) != workspace ||
          scene->theme_id != *theme_id)) ||
        (!theme_id->empty() && (!themes_ || !themes_->Exists(*theme_id))) ||
        (!pre_scene_theme_id->empty() &&
         (!themes_ || !themes_->Exists(*pre_scene_theme_id))) ||
        (scene_id->empty() &&
         (!pre_scene_theme_id->empty() || *has_baseline))) {
      return;
    }
    WorkspacePresentation presentation;
    presentation.active_scene_id = *scene_id;
    presentation.active_theme_id = *theme_id;
    presentation.pre_scene_theme_id = *pre_scene_theme_id;
    presentation.has_vertical_tabs_baseline = *has_baseline;
    presentation.vertical_tabs_baseline = {
        .collapsed = *collapsed,
        .expand_on_hover = *expand_on_hover,
    };
    restored.emplace(workspace, std::move(presentation));
  }
  presentations_by_workspace_ = std::move(restored);
  presentation_owners_.clear();
}

void SeoulRuntimeService::PublishShellTaskSummary(const LiveWindowKey &window) {
  if (!window.is_valid() || !organization_ || !task_service_) {
    return;
  }
  ShellService *shell = organization_->shell_service();
  if (!shell) {
    return;
  }
  ShellTaskSummary summary;
  for (const TaskStateSummary &task : task_service_->StateSummaries()) {
    if (task.window != window) {
      continue;
    }
    ++summary.total;
    switch (task.state) {
    case TaskState::kDraft:
    case TaskState::kPlanning:
    case TaskState::kExecuting:
    case TaskState::kMonitoring:
      ++summary.active;
      break;
    case TaskState::kAwaitingApproval:
      ++summary.waiting_for_user;
      break;
    case TaskState::kPaused:
      ++summary.paused;
      break;
    case TaskState::kFailed:
      ++summary.failed;
      break;
    case TaskState::kCompleted:
    case TaskState::kCancelled:
      break;
    }
  }
  shell->UpdateTaskSummary(window, summary);
}

void SeoulRuntimeService::RunSceneLifecycleMaintenanceForTesting() {
  RunSceneLifecycleMaintenance();
}

void SeoulRuntimeService::RunSceneLifecycleMaintenance() {
  if (shutting_down_ || !organization_) {
    return;
  }
  // Copy the bindings: closing an eligible tab publishes lifecycle snapshots
  // synchronously and may otherwise invalidate iterators into this map.
  const auto active_scenes = active_scenes_by_window_;
  for (const auto &[window, scene_id] : active_scenes) {
    const SceneDefinition *scene = runtime_.scenes().Find(scene_id);
    if (!scene || !scene->lifecycle.archive_temporary_tabs) {
      continue;
    }
    RunAutoArchiveSweep(*scene, window);
  }
}

void SeoulRuntimeService::RunLiveCollectionMaintenance() {
  if (shutting_down_ || !library_service_ ||
      !live_collection_coordinator_) {
    return;
  }
  LiveWindowStateProvider* live_state = live_window_state_provider();
  if (!live_state) {
    return;
  }
  const base::Time now = Now();
  for (const LiveCollectionId& id : library_service_->LiveCollections()) {
    const LiveCollectionRecord* record =
        library_service_->FindLiveCollection(id);
    if (!record || !library_service_->IsRefreshDue(id, now) ||
        live_collection_coordinator_->IsRefreshing(id)) {
      continue;
    }
    const LiveWindowKey window =
        LiveWindowKey::Parse(record->definition.scope_window);
    if (!window.is_valid() || !live_state->GetSnapshot(window).has_value()) {
      continue;
    }
    // Maintenance is best effort. The coordinator records provider and result
    // failures on the collection while preserving its last verified items.
    std::ignore = live_collection_coordinator_->Refresh(
        id, window, BuildPermissionContext(window),
        base::BindOnce([](LiveCollectionRuntimeStatus) {}));
  }
}

void SeoulRuntimeService::RunAutoArchiveSweep(const SceneDefinition &scene,
                                              const LiveWindowKey &window) {
  OrganizationModel *model = organization_ ? &organization_->model() : nullptr;
  CommandExecutor *commands =
      organization_ ? organization_->command_executor() : nullptr;
  LiveWindowStateProvider *live_state =
      organization_ ? organization_->live_window_state_provider() : nullptr;
  const std::optional<LiveWindowSnapshot> snapshot =
      live_state ? live_state->GetSnapshot(window) : std::nullopt;
  const WorkspaceId workspace = WorkspaceId::FromString(scene.workspace_id);
  if (!model || !commands || !snapshot.has_value() ||
      snapshot->lifecycle_degraded || !workspace.is_valid() ||
      model->ActiveWorkspaceForWindow(window.value()) != workspace) {
    return;
  }

  bool window_has_protective_task = false;
  if (task_service_) {
    for (const TaskStateSummary &task : task_service_->StateSummaries()) {
      if (task.window != window) {
        continue;
      }
      switch (task.state) {
      case TaskState::kDraft:
      case TaskState::kPlanning:
      case TaskState::kExecuting:
      case TaskState::kMonitoring:
      case TaskState::kAwaitingApproval:
      case TaskState::kPaused:
        window_has_protective_task = true;
        break;
      case TaskState::kCompleted:
      case TaskState::kFailed:
      case TaskState::kCancelled:
        break;
      }
      if (window_has_protective_task) {
        break;
      }
    }
  }

  std::set<content::WebContents *> download_tabs;
  if (profile_) {
    content::DownloadManager *manager = profile_->GetDownloadManager();
    if (manager) {
      download::SimpleDownloadManager::DownloadVector downloads;
      manager->GetAllDownloads(&downloads);
      manager->GetUninitializedActiveDownloadsIfAny(&downloads);
      for (download::DownloadItem *download : downloads) {
        if (!download ||
            download->GetState() != download::DownloadItem::IN_PROGRESS) {
          continue;
        }
        if (content::WebContents *contents =
                content::DownloadItemUtils::GetWebContents(download)) {
          download_tabs.insert(contents);
        }
        if (content::WebContents *contents =
                content::DownloadItemUtils::GetOriginalWebContents(download)) {
          download_tabs.insert(contents);
        }
      }
    }
  }

  std::map<std::string, TabLiveActivity> activity;
  std::set<std::string> exact_live_tabs;
  for (const LiveTabDescriptor &descriptor : snapshot->tabs) {
    if (!descriptor.tab.is_valid()) {
      continue;
    }
    exact_live_tabs.insert(descriptor.tab.value());
    TabLiveActivity &state = activity[descriptor.tab.value()];
    state.has_active_task = window_has_protective_task;
    state.in_split = !descriptor.upstream_split_token.empty();
    content::WebContents *contents =
        web_contents_resolver_ ? web_contents_resolver_.Run(descriptor.tab)
                               : nullptr;
    if (!contents || contents->IsBeingDestroyed()) {
      // A missing or tearing-down renderer is uncertain, never idle.
      state.loading = true;
      continue;
    }
    state.playing_media = contents->IsCurrentlyAudible();
    state.has_active_download = download_tabs.contains(contents);
    state.has_unsaved_form = contents->NeedToFireBeforeUnloadOrUnloadEvents();
    state.loading = contents->IsLoading();
    if (permissions::PermissionRequestManager *permission_manager =
            permissions::PermissionRequestManager::FromWebContents(contents)) {
      state.has_permission_prompt = permission_manager->IsRequestInProgress();
    }
    if (content::DevToolsAgentHost::HasFor(contents)) {
      scoped_refptr<content::DevToolsAgentHost> agent =
          content::DevToolsAgentHost::GetForTab(contents);
      state.has_devtools = agent && agent->IsAttached();
    }
  }

  const std::vector<TabMembershipId> eligible = model->EligibleForAutoArchive(
      activity, Now(), base::Minutes(scene.lifecycle.idle_archive_minutes));
  for (const TabMembershipId &membership : eligible) {
    const TabMembershipRecord *record = model->FindMembership(membership);
    if (!record || record->workspace_id != workspace ||
        !exact_live_tabs.contains(record->tab_key)) {
      continue;
    }
    const LiveTabKey tab = LiveTabKey::Parse(record->tab_key);
    // Never sweep away the currently visible tab. Its activity timestamp
    // should already be recent, but the live identity is the stronger guard
    // during session restore and clock changes.
    if (!tab.is_valid() || tab == snapshot->active_tab) {
      continue;
    }
    content::WebContents *contents =
        web_contents_resolver_ ? web_contents_resolver_.Run(tab) : nullptr;
    if (!contents || contents->IsBeingDestroyed()) {
      continue;
    }
    const GURL recovery_url = contents->GetLastCommittedURL();
    if (!recovery_url.is_valid() || !recovery_url.SchemeIsHTTPOrHTTPS()) {
      continue;
    }
    BrowserCommand archive;
    archive.id = CommandId::Next();
    archive.kind = CommandKind::kArchiveTab;
    archive.origin = CommandOrigin::kSystem;
    archive.window = window;
    archive.tab = tab;
    archive.membership_id = membership;
    archive.url = recovery_url;
    archive.name = base::TruncateUTF8ToByteSize(
        base::UTF16ToUTF8(contents->GetTitle()), kMaxNameLength);
    // Submission is transactional: the model archive is committed only after
    // Chromium confirms removal of this exact tab. A rejected close leaves the
    // live membership untouched and the next sweep may reconsider it.
    const CommandResult<CommandStatus> submitted =
        commands->Submit(std::move(archive));
    if (!submitted.has_value()) {
      continue;
    }
  }
}

void SeoulRuntimeService::RestoreArchivedTabsForScene(
    const SceneDefinition &scene, const LiveWindowKey &window) {
  pending_scene_restores_.erase(window);
  if (!scene.lifecycle.restore_on_activation || !organization_) {
    return;
  }
  const WorkspaceId workspace = WorkspaceId::FromString(scene.workspace_id);
  if (!workspace.is_valid()) {
    return;
  }
  auto pending = std::make_unique<PendingSceneRestore>();
  pending->scene_id = scene.id;
  pending->workspace_id = workspace;
  pending->deadline = base::TimeTicks::Now() + kSceneRestoreContentionBudget;
  for (const ArchivedTabRecord &archived :
       organization_->model().ToSnapshot().archived_tabs) {
    const GURL recovery_url(archived.saved_root_url);
    if (archived.workspace_id == workspace && recovery_url.is_valid() &&
        recovery_url.SchemeIsHTTPOrHTTPS()) {
      pending->archive_ids.push_back(archived.original_id);
    }
  }
  if (pending->archive_ids.empty()) {
    return;
  }
  pending_scene_restores_[window] = std::move(pending);
  ContinueSceneRestore(window);
}

void SeoulRuntimeService::ScheduleSceneRestoreContinuation(
    const LiveWindowKey &window, base::TimeDelta delay) {
  auto it = pending_scene_restores_.find(window);
  if (shutting_down_ || it == pending_scene_restores_.end() || !it->second) {
    return;
  }
  it->second->retry_timer.Start(
      FROM_HERE, delay,
      base::BindOnce(&SeoulRuntimeService::ContinueSceneRestore,
                     base::Unretained(this), window));
}

void SeoulRuntimeService::ContinueSceneRestore(const LiveWindowKey &window) {
  auto it = pending_scene_restores_.find(window);
  if (shutting_down_ || it == pending_scene_restores_.end() || !it->second ||
      it->second->active_command.is_valid()) {
    return;
  }
  PendingSceneRestore &pending = *it->second;
  const SceneDefinition *scene = runtime_.scenes().Find(pending.scene_id);
  CommandExecutor *commands =
      organization_ ? organization_->command_executor() : nullptr;
  if (!scene || ActiveSceneForWindow(window) != pending.scene_id ||
      scene->workspace_id != pending.workspace_id.value() || !commands) {
    pending.archive_ids.clear();
    return;
  }
  if (commands->in_flight_count() > 0) {
    if (base::TimeTicks::Now() >= pending.deadline) {
      // Existing browser work owns the mutation lane. Keep every archive
      // durable and stop polling; the next explicit Scene activation retries.
      pending.archive_ids.clear();
      return;
    }
    ScheduleSceneRestoreContinuation(window, kSceneRestoreRetryDelay);
    return;
  }

  while (!pending.archive_ids.empty()) {
    const ArchivedTabRecord *archived =
        organization_->model().FindArchivedTab(pending.archive_ids.front());
    const GURL recovery_url(archived ? archived->saved_root_url
                                     : std::string());
    if (archived && archived->workspace_id == pending.workspace_id &&
        recovery_url.is_valid() && recovery_url.SchemeIsHTTPOrHTTPS()) {
      break;
    }
    pending.archive_ids.pop_front();
  }
  if (pending.archive_ids.empty()) {
    return;
  }

  const TabMembershipId archive_id = pending.archive_ids.front();
  const ArchivedTabRecord *archived =
      organization_->model().FindArchivedTab(archive_id);
  if (!archived) {
    pending.archive_ids.pop_front();
    ScheduleSceneRestoreContinuation(window, base::TimeDelta());
    return;
  }
  BrowserCommand restore;
  restore.id = CommandId::Next();
  restore.kind = CommandKind::kRestoreArchivedTab;
  restore.origin = CommandOrigin::kSystem;
  restore.window = window;
  restore.membership_id = archive_id;
  restore.workspace_id = pending.workspace_id;
  restore.url = GURL(archived->saved_root_url);
  restore.foreground = CommandForegroundDisposition::kBackground;
  const CommandId command_id = restore.id;
  // Install the identity before Submit because a Chromium insertion can be
  // observed synchronously inside it.
  pending.active_command = command_id;
  const CommandResult<CommandStatus> submitted =
      commands->Submit(std::move(restore));
  it = pending_scene_restores_.find(window);
  if (!submitted.has_value() && it != pending_scene_restores_.end() &&
      it->second && it->second->active_command == command_id) {
    it->second->active_command = CommandId();
    if (!it->second->archive_ids.empty()) {
      // Leave the archived record durable, but do not spin forever on one
      // failed insertion during this activation.
      it->second->archive_ids.pop_front();
    }
    ScheduleSceneRestoreContinuation(window, kSceneRestoreRetryDelay);
  }
}

// static
void SeoulRuntimeService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable *registry) {
  registry->RegisterDictionaryPref(kProductRuntimePref);
}

void SeoulRuntimeService::RegisterBuiltinExecutors() {
  CommandExecutor *commands =
      organization_ ? organization_->command_executor() : nullptr;
  OrganizationModel *model = organization_ ? &organization_->model() : nullptr;
  LiveWindowStateProvider *live_state =
      organization_ ? organization_->live_window_state_provider() : nullptr;

  // Browser-mutating capabilities: one executor per descriptor id, each
  // mapping to a typed browser command observed to completion.
  executors_.Register(std::make_unique<BrowserCommandExecutor>(
      "browser.tabs.open", CommandKind::kOpenTemporaryTab, commands, live_state,
      preview_host_service_.get(),
      base::BindRepeating(&SeoulRuntimeService::ResolveLinkRouting,
                          base::Unretained(this))));
  executors_.Register(std::make_unique<BrowserCommandExecutor>(
      "browser.tabs.activate", CommandKind::kActivateTab, commands));
  executors_.Register(std::make_unique<BrowserCommandExecutor>(
      "browser.tabs.close", CommandKind::kCloseTab, commands));
  executors_.Register(std::make_unique<BrowserCommandExecutor>(
      "browser.tabs.archive", CommandKind::kArchiveTab, commands, live_state,
      nullptr, LinkRoutingResolver(), model, web_contents_resolver_));
  executors_.Register(std::make_unique<BrowserCommandExecutor>(
      "browser.tabs.restore", CommandKind::kRestoreArchivedTab, commands,
      live_state, nullptr, LinkRoutingResolver(), model,
      web_contents_resolver_));
  executors_.Register(std::make_unique<BrowserCommandExecutor>(
      "browser.workspace.switch", CommandKind::kSetActiveWorkspace, commands));
  executors_.Register(std::make_unique<BrowserCommandExecutor>(
      "browser.split.create", CommandKind::kCreateSplit, commands));

  // Browser surfaces and read-only browser/page capabilities.
  executors_.Register(
      std::make_unique<EnumerateTabsExecutor>(model, live_state));
  executors_.Register(
      std::make_unique<PreviewOpenExecutor>(preview_host_service_.get()));
  executors_.Register(
      std::make_unique<SceneActivateExecutor>(base::BindRepeating(
          &SeoulRuntimeService::ActivateScene, base::Unretained(this))));
  executors_.Register(std::make_unique<CompactModeExecutor>(
      base::BindRepeating(&SeoulRuntimeService::SetCompactMode,
                          base::Unretained(this)),
      base::BindRepeating(&SeoulRuntimeService::CompactModeForWindow,
                          base::Unretained(this)),
      base::BindRepeating(&SeoulRuntimeService::IsCompactModeApplied,
                          base::Unretained(this))));
  executors_.Register(
      std::make_unique<PageObserveExecutor>(page_agent_.get(), live_state));
  executors_.Register(std::make_unique<PageExtractStructuredExecutor>(
      page_agent_.get(), live_state));
  executors_.Register(std::make_unique<PageActionExecutor>(
      "page.act.click", PageActionKind::kClick, page_agent_.get(), live_state));
  executors_.Register(std::make_unique<PageActionExecutor>(
      "page.act.type", PageActionKind::kType, page_agent_.get(), live_state));
  executors_.Register(std::make_unique<PageActionExecutor>(
      "page.act.submit", PageActionKind::kClick, page_agent_.get(),
      live_state));

  // Any registered capability descriptor that has no executor yet is marked
  // unavailable so the planner never offers a capability that cannot run.
  // (Provider-backed descriptors like info.search.web become available when a
  // connector supplies them.)
  const ToolPermissionContext everything = [] {
    ToolPermissionContext context;
    context.max_sensitivity = DataSensitivity::kCredentialAdjacent;
    context.allow_network = true;
    return context;
  }();
  for (const ToolDescriptor *descriptor :
       runtime_.capabilities().ListAvailable(everything)) {
    if (!executors_.Find(descriptor->id, descriptor->version)) {
      runtime_.capabilities().SetAvailability(
          descriptor->id, AvailabilityState::kUnavailable,
          "No executor registered for this capability yet.");
    }
  }
}

ToolPermissionContext SeoulRuntimeService::BuildPermissionContext() const {
  return BuildPermissionContext(LiveWindowKey());
}

ToolPermissionContext
SeoulRuntimeService::BuildPermissionContext(const LiveWindowKey &window) const {
  ToolPermissionContext context;
  // Organization-level reads are always allowed; page content is allowed
  // because the page agent is browser-owned and returns only semantics.
  context.max_sensitivity = DataSensitivity::kPageContent;
  context.allow_network =
      provider_registry_ &&
      provider_registry_->HasUsableProvider(AllowCloudModels(window));
  // Connector providers add themselves here as they connect.
  for (const Connector *connector : runtime_.connectors().Connected()) {
    context.connected_providers.insert(connector->provider());
  }
  const std::string active_scene = ActiveSceneForWindow(window);
  if (!active_scene.empty()) {
    if (const SceneDefinition *scene = runtime_.scenes().Find(active_scene)) {
      context.allow_network =
          context.allow_network && scene->assistant.allow_network;
      context.max_sensitivity =
          static_cast<int>(scene->assistant.max_sensitivity) <
                  static_cast<int>(context.max_sensitivity)
              ? scene->assistant.max_sensitivity
              : context.max_sensitivity;
    }
  }
  return context;
}

AgentPermissionRequest SeoulRuntimeService::ResolveAgentPermissionRequest(
    const LiveWindowKey &window, const ToolDescriptor &descriptor,
    const base::DictValue &args, bool user_gesture) const {
  AgentPermissionRequest request;
  request.capability = descriptor.id;
  request.approval = descriptor.approval;
  request.risk = descriptor.risk;
  request.sensitivity = descriptor.sensitivity;
  request.window = window;
  if (descriptor.provider != "seoul") {
    request.service_scope = descriptor.provider;
  }

  if (const std::string *explicit_tab = args.FindString("tab_key")) {
    request.tab = LiveTabKey::Parse(*explicit_tab);
  }
  if (descriptor.id.root_namespace() == "page") {
    if (!request.tab.is_valid() && organization_) {
      if (LiveWindowStateProvider *live_state =
              organization_->live_window_state_provider()) {
        if (std::optional<LiveWindowSnapshot> snapshot =
                live_state->GetSnapshot(window)) {
          request.tab = snapshot->active_tab;
        }
      }
    }
    request.frame_scope = "main";
  } else if (descriptor.id.value() == "browser.preview.open") {
    request.frame_scope = "main";
  }
  if (request.tab.is_valid() && web_contents_resolver_) {
    if (content::WebContents *contents =
            web_contents_resolver_.Run(request.tab)) {
      request.source_origin =
          url::Origin::Create(contents->GetLastCommittedURL());
    }
  }

  if (const std::string *destination = args.FindString("url")) {
    const GURL url(*destination);
    if (url.is_valid() && url.SchemeIsHTTPOrHTTPS()) {
      request.destination_origin = url::Origin::Create(url);
      if (descriptor.id.value() == "browser.tabs.open") {
        const bool retained = args.FindBool("retained").value_or(false);
        const RoutingResolution routing =
            ResolveLinkRouting(window, url, user_gesture,
                               retained ? RoutingDisposition::kNewRetainedTab
                                        : RoutingDisposition::kNewTemporaryTab);
        if (routing.result.disposition == RoutingDisposition::kAskUser) {
          request.approval = ApprovalPolicy::kAlwaysRequired;
        } else if (routing.result.disposition ==
                   RoutingDisposition::kExternalApplication) {
          request.approval = ApprovalPolicy::kAlwaysRequired;
          request.risk = RiskCategory::kExternalSideEffect;
        }
      }
    }
  }
  return request;
}

WindowRuntimeBinding
SeoulRuntimeService::CreateWindowBinding(BrowserWindowInterface *browser) {
  if (shutting_down_ || !browser || browser->GetProfile() != profile_ ||
      browser->IsDeleteScheduled() ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    return {};
  }
  const SessionID &session_id = browser->GetSessionID();
  if (!session_id.is_valid()) {
    return {};
  }

  WindowRuntimeBinding binding;
  binding.token = WindowRuntimeBindingToken::Create();
  binding.window = LiveWindowKey::FromSessionId(session_id.id());

  WindowBindingRecord record;
  record.browser = browser;
  record.window = binding.window;
  record.close_subscription = browser->RegisterBrowserDidClose(
      base::BindRepeating(&SeoulRuntimeService::OnWindowBindingClosed,
                          base::Unretained(this), binding.token));
  window_bindings_.emplace(binding.token, std::move(record));
  return binding;
}

std::optional<LiveWindowKey> SeoulRuntimeService::ResolveWindowBinding(
    const WindowRuntimeBindingToken &token) const {
  if (shutting_down_ || token.is_empty()) {
    return std::nullopt;
  }
  auto it = window_bindings_.find(token);
  if (it == window_bindings_.end()) {
    return std::nullopt;
  }
  BrowserWindowInterface *browser = it->second.browser.get();
  if (!browser || browser->GetProfile() != profile_ ||
      browser->IsDeleteScheduled() ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    return std::nullopt;
  }
  const SessionID &session_id = browser->GetSessionID();
  if (!session_id.is_valid()) {
    return std::nullopt;
  }
  const LiveWindowKey current = LiveWindowKey::FromSessionId(session_id.id());
  if (current != it->second.window ||
      BrowserWindowInterface::FromSessionID(session_id) != browser) {
    return std::nullopt;
  }
  return current;
}

void SeoulRuntimeService::InvalidateWindowBinding(
    const WindowRuntimeBindingToken &token) {
  if (!token.is_empty()) {
    window_bindings_.erase(token);
  }
}

std::optional<LiveTabDescriptor>
SeoulRuntimeService::ActiveTabDescriptor(const LiveWindowKey &window) const {
  if (shutting_down_ || !organization_ || !window.is_valid()) {
    return std::nullopt;
  }
  LiveWindowStateProvider *live_state =
      organization_->live_window_state_provider();
  if (!live_state) {
    return std::nullopt;
  }
  const std::optional<LiveWindowSnapshot> snapshot =
      live_state->GetSnapshot(window);
  if (!snapshot.has_value() || !snapshot->active_tab.is_valid()) {
    return std::nullopt;
  }
  for (const LiveTabDescriptor &descriptor : snapshot->tabs) {
    if (descriptor.tab == snapshot->active_tab) {
      return descriptor;
    }
  }
  return std::nullopt;
}

void SeoulRuntimeService::OnWindowBindingClosed(
    WindowRuntimeBindingToken token, BrowserWindowInterface *browser) {
  auto it = window_bindings_.find(token);
  if (it == window_bindings_.end() || it->second.browser != browser) {
    return;
  }
  if (agent_permissions_) {
    agent_permissions_->RevokeWindow(it->second.window);
  }
  window_bindings_.erase(it);
}

TaskId SeoulRuntimeService::StartCapability(const std::string &capability_id,
                                            base::DictValue args,
                                            const LiveWindowKey &window) {
  if (shutting_down_ || !task_service_ || !window.is_valid()) {
    return TaskId();
  }
  const ToolId id = ToolId::FromString(capability_id);
  const ToolDescriptor *descriptor =
      id.is_valid() ? runtime_.capabilities().Find(id) : nullptr;
  if (!descriptor) {
    return TaskId();
  }
  Plan plan;
  plan.goal = capability_id;
  PlanStep step;
  step.id = "step_1";
  step.kind = PlanStepKind::kToolCall;
  step.tool = id;
  step.args = std::move(args);
  step.requires_approval =
      descriptor->approval == ApprovalPolicy::kAlwaysRequired ||
      descriptor->approval == ApprovalPolicy::kFirstUsePerScope ||
      descriptor->risk == RiskCategory::kIrreversibleMutation ||
      descriptor->risk == RiskCategory::kExternalSideEffect;
  plan.steps.push_back(std::move(step));
  // Approval policy is enforced from the descriptor, never from the surface.
  Planner::EnforceApprovalPolicy(plan, runtime_.capabilities());
  return task_service_->StartTaskWithPlan(capability_id, std::move(plan),
                                          PlanOrigin::kDeterministic, window,
                                          BuildPermissionContext(window));
}

TaskId SeoulRuntimeService::StartGoal(const std::string &goal,
                                      const LiveWindowKey &window) {
  if (shutting_down_ || !task_service_ || !window.is_valid()) {
    return TaskId();
  }
  const bool allow_cloud_models = AllowCloudModels(window);
  const bool use_model =
      provider_registry_ &&
      provider_registry_->HasUsableProvider(allow_cloud_models);
  const bool prefer_local = !allow_cloud_models;
  return task_service_->StartTask(goal, window, BuildPermissionContext(window),
                                  use_model, prefer_local, allow_cloud_models);
}

VoiceStatusResult SeoulRuntimeService::StartVoice(const LiveWindowKey &window) {
  if (shutting_down_ || !voice_controller_) {
    return VoiceErr(VoiceError::kProviderUnavailable);
  }
  return voice_controller_->StartVoice(window);
}

VoiceStatusResult SeoulRuntimeService::StopVoice() {
  if (shutting_down_ || !voice_controller_) {
    return VoiceErr(VoiceError::kProviderUnavailable);
  }
  return voice_controller_->StopVoice();
}

VoiceRuntimeSnapshot SeoulRuntimeService::VoiceSnapshot() const {
  return voice_controller_ ? voice_controller_->Snapshot()
                           : VoiceRuntimeSnapshot();
}

void SeoulRuntimeService::CreateRealtimeVoiceSession(
    const std::string &safety_identifier, const LiveWindowKey &window,
    RealtimeVoiceAgent::CreateSessionCallback callback) {
  if (shutting_down_ || !realtime_voice_agent_) {
    std::move(callback).Run(
        base::unexpected("Realtime voice agent is unavailable."));
    return;
  }
  if (!window.is_valid() || !live_tabs_by_window_.contains(window)) {
    std::move(callback).Run(
        base::unexpected("The browser window is no longer available."));
    return;
  }
  if (!AllowCloudModels(window)) {
    std::move(callback).Run(base::unexpected(
        "Realtime voice is disabled by the active Scene's cloud policy."));
    return;
  }
  realtime_voice_agent_->CreateSession(safety_identifier, std::move(callback));
}

RealtimeVoiceAgentSnapshot SeoulRuntimeService::RealtimeVoiceSnapshot() const {
  return realtime_voice_agent_ ? realtime_voice_agent_->Snapshot()
                               : RealtimeVoiceAgentSnapshot();
}

SiteLayerStatusResult SeoulRuntimeService::UpsertSiteLayer(SiteLayer layer) {
  if (shutting_down_ || !site_layers_) {
    return base::unexpected(SiteLayerError::kUnknownLayer);
  }
  SiteLayerStatusResult result = site_layers_->Upsert(std::move(layer));
  if (!result.has_value()) {
    return result;
  }
  RefreshSiteLayers();
  SchedulePersist();
  return base::ok();
}

SiteLayerStatusResult
SeoulRuntimeService::RemoveSiteLayer(const std::string &layer_id) {
  if (shutting_down_ || !site_layers_) {
    return base::unexpected(SiteLayerError::kUnknownLayer);
  }
  for (const SceneDefinition *scene : runtime_.scenes().List()) {
    if (std::ranges::find(scene->site_layer_ids, layer_id) !=
        scene->site_layer_ids.end()) {
      return base::unexpected(SiteLayerError::kInUse);
    }
  }
  SiteLayerStatusResult result = site_layers_->Remove(layer_id);
  if (!result.has_value()) {
    return result;
  }
  RefreshSiteLayers();
  SchedulePersist();
  return base::ok();
}

void SeoulRuntimeService::RefreshSiteLayers() {
  if (shutting_down_) {
    return;
  }
  for (auto &[tab, applicator] : site_layer_applicators_) {
    if (applicator) {
      applicator->Refresh(SceneForTab(tab));
    }
  }
}

void SeoulRuntimeService::BeginSiteLayerZap(
    const std::string &layer_id, const LiveWindowKey &window,
    SiteLayerZapCallback callback) {
  const SiteLayer *layer =
      site_layers_ ? site_layers_->Find(layer_id) : nullptr;
  const std::optional<LiveTabDescriptor> active = ActiveTabDescriptor(window);
  if (!layer) {
    std::move(callback).Run(
        false, base::unexpected(SiteLayerError::kUnknownLayer));
    return;
  }
  if (!active.has_value() || active->origin.empty() ||
      !SiteLayerMatchesOrigin(*layer, active->origin)) {
    std::move(callback).Run(
        false, base::unexpected(SiteLayerError::kInvalidOrigin));
    return;
  }
  auto applicator = site_layer_applicators_.find(active->tab);
  if (applicator == site_layer_applicators_.end()) {
    std::move(callback).Run(
        false, base::unexpected(SiteLayerError::kInvalidOrigin));
    return;
  }

  applicator->second->BeginZap(base::BindOnce(
      [](base::WeakPtr<SeoulRuntimeService> runtime, std::string layer_id,
         std::string origin, SiteLayerZapCallback callback,
         std::optional<std::string> selector) {
        if (!runtime) {
          return;
        }
        if (!selector.has_value()) {
          std::move(callback).Run(false, base::ok());
          return;
        }
        const SiteLayer *stored = runtime->site_layers_
                                      ? runtime->site_layers_->Find(layer_id)
                                      : nullptr;
        if (!stored || !SiteLayerMatchesOrigin(*stored, origin)) {
          std::move(callback).Run(
              false, base::unexpected(SiteLayerError::kUnknownLayer));
          return;
        }

        SiteLayer updated = *stored;
        for (const SiteAdjustment &adjustment : updated.adjustments) {
          if (adjustment.kind == SiteAdjustmentKind::kHide &&
              std::ranges::find(adjustment.selectors, *selector) !=
                  adjustment.selectors.end()) {
            std::move(callback).Run(false, base::ok());
            return;
          }
        }
        SiteAdjustment *target = nullptr;
        for (SiteAdjustment &adjustment : updated.adjustments) {
          if (adjustment.kind == SiteAdjustmentKind::kHide &&
              adjustment.selectors.size() < kMaxSelectorsPerRule) {
            target = &adjustment;
            break;
          }
        }
        if (!target) {
          if (updated.adjustments.size() >= kMaxLayerRules) {
            std::move(callback).Run(
                false, base::unexpected(SiteLayerError::kTooManyRules));
            return;
          }
          SiteAdjustment hide;
          hide.kind = SiteAdjustmentKind::kHide;
          updated.adjustments.push_back(std::move(hide));
          target = &updated.adjustments.back();
        }
        target->selectors.push_back(*selector);
        SiteLayerStatusResult result =
            runtime->UpsertSiteLayer(std::move(updated));
        std::move(callback).Run(result.has_value(), std::move(result));
      },
      weak_factory_.GetWeakPtr(), layer_id, active->origin,
      std::move(callback)));
}

void SeoulRuntimeService::CancelSiteLayerZap(const LiveWindowKey &window) {
  auto tabs = live_tabs_by_window_.find(window);
  if (tabs == live_tabs_by_window_.end()) {
    return;
  }
  for (const LiveTabKey &tab : tabs->second) {
    auto applicator = site_layer_applicators_.find(tab);
    if (applicator != site_layer_applicators_.end() && applicator->second) {
      applicator->second->CancelZap();
    }
  }
}

base::CallbackListSubscription
SeoulRuntimeService::AddBoostEditorRequestCallback(
    BoostEditorRequestCallback callback) {
  return boost_editor_request_callbacks_.Add(std::move(callback));
}

void SeoulRuntimeService::RequestBoostEditor(const LiveWindowKey &window) {
  if (!window.is_valid() || !live_tabs_by_window_.contains(window)) {
    return;
  }
  pending_boost_editor_windows_.insert(window);
  boost_editor_request_callbacks_.Notify(window);
}

bool SeoulRuntimeService::ConsumeBoostEditorRequest(
    const LiveWindowKey &window) {
  return pending_boost_editor_windows_.erase(window) > 0;
}

ThemeStatusResult SeoulRuntimeService::UpsertTheme(Theme theme) {
  if (shutting_down_ || !themes_) {
    return base::unexpected(ThemeError::kUnknownTheme);
  }
  ThemeStatusResult result = themes_->Upsert(std::move(theme));
  if (result.has_value()) {
    SchedulePersist();
  }
  return result;
}

ThemeStatusResult
SeoulRuntimeService::RemoveTheme(const std::string &theme_id) {
  if (shutting_down_ || !themes_) {
    return base::unexpected(ThemeError::kUnknownTheme);
  }
  for (const SceneDefinition *scene : runtime_.scenes().List()) {
    if (scene->theme_id == theme_id) {
      return base::unexpected(ThemeError::kInUse);
    }
  }
  if (std::ranges::any_of(
          active_themes_by_window_,
          [&theme_id](const auto &item) { return item.second == theme_id; })) {
    return base::unexpected(ThemeError::kInUse);
  }
  if (std::ranges::any_of(presentations_by_workspace_,
                          [&theme_id](const auto &item) {
                            return item.second.active_theme_id == theme_id ||
                                   item.second.pre_scene_theme_id == theme_id;
                          })) {
    return base::unexpected(ThemeError::kInUse);
  }
  ThemeStatusResult result = themes_->Remove(theme_id);
  if (result.has_value()) {
    SchedulePersist();
  }
  return result;
}

ThemeStatusResult
SeoulRuntimeService::ActivateTheme(const std::string &theme_id,
                                   const LiveWindowKey &window) {
  if (shutting_down_ || !window.is_valid() ||
      !live_tabs_by_window_.contains(window)) {
    return base::unexpected(ThemeError::kUnknownTheme);
  }
  if (active_scenes_by_window_.contains(window)) {
    return base::unexpected(ThemeError::kInUse);
  }
  const WorkspaceId workspace =
      organization_
          ? organization_->model().ActiveWorkspaceForWindow(window.value())
          : WorkspaceId();
  if (!workspace.is_valid()) {
    return base::unexpected(ThemeError::kUnknownTheme);
  }
  if (theme_id.empty()) {
    active_themes_by_window_.erase(window);
    for (auto owner = presentation_owners_.begin();
         owner != presentation_owners_.end();) {
      if (owner->second == window) {
        presentations_by_workspace_.erase(owner->first);
        owner = presentation_owners_.erase(owner);
      } else {
        ++owner;
      }
    }
    SchedulePersist();
    return base::ok();
  }
  if (!themes_ || !themes_->Exists(theme_id)) {
    return base::unexpected(ThemeError::kUnknownTheme);
  }
  active_themes_by_window_[window] = theme_id;
  for (auto owner = presentation_owners_.begin();
       owner != presentation_owners_.end();) {
    if (owner->second == window && owner->first != workspace) {
      presentations_by_workspace_.erase(owner->first);
      owner = presentation_owners_.erase(owner);
    } else {
      ++owner;
    }
  }
  WorkspacePresentation presentation;
  presentation.active_theme_id = theme_id;
  presentations_by_workspace_[workspace] = std::move(presentation);
  presentation_owners_[workspace] = window;
  SchedulePersist();
  return base::ok();
}

SceneStatusResult SeoulRuntimeService::UpsertScene(SceneDefinition scene) {
  if (shutting_down_) {
    return base::unexpected(SceneError::kUnknownScene);
  }
  // Editing an active durable presentation in place could otherwise change
  // its Workspace, Theme, compact policy, lifecycle, or workflow side effects
  // without a fresh activation transaction. Require the caller to clear the
  // Scene first so persisted intent and live presentation never diverge.
  if (runtime_.scenes().Find(scene.id) &&
      (std::ranges::any_of(
           active_scenes_by_window_,
           [&scene](const auto &item) { return item.second == scene.id; }) ||
       std::ranges::any_of(presentations_by_workspace_,
                           [&scene](const auto &item) {
                             return item.second.active_scene_id == scene.id;
                           }))) {
    return base::unexpected(SceneError::kInUse);
  }
  SceneStatusResult result = runtime_.scenes().Upsert(std::move(scene));
  if (result.has_value()) {
    SchedulePersist();
  }
  return result;
}

SceneStatusResult
SeoulRuntimeService::RemoveScene(const std::string &scene_id) {
  if (shutting_down_) {
    return base::unexpected(SceneError::kUnknownScene);
  }
  if (std::ranges::any_of(
          active_scenes_by_window_,
          [&scene_id](const auto &item) { return item.second == scene_id; })) {
    return base::unexpected(SceneError::kInUse);
  }
  if (std::ranges::any_of(presentations_by_workspace_,
                          [&scene_id](const auto &item) {
                            return item.second.active_scene_id == scene_id;
                          })) {
    return base::unexpected(SceneError::kInUse);
  }
  if (site_layers_) {
    for (const SiteLayer *layer : site_layers_->List()) {
      if (layer->scene_scope == scene_id) {
        return base::unexpected(SceneError::kInUse);
      }
    }
  }
  if (workflow_service_) {
    for (const WorkflowId &id : workflow_service_->All()) {
      const WorkflowDefinition *workflow = workflow_service_->Find(id);
      if (workflow &&
          (workflow->scene_scope == scene_id ||
           (workflow->trigger.kind == WorkflowTriggerKind::kSceneActivation &&
            workflow->trigger.scene_id == scene_id))) {
        return base::unexpected(SceneError::kInUse);
      }
    }
  }
  SceneStatusResult result = runtime_.scenes().Remove(scene_id);
  if (result.has_value()) {
    SchedulePersist();
  }
  return result;
}

SceneStatusResult
SeoulRuntimeService::ActivateScene(const std::string &scene_id,
                                   const LiveWindowKey &window) {
  if (shutting_down_ || !window.is_valid() ||
      !live_tabs_by_window_.contains(window)) {
    return base::unexpected(SceneError::kActivationFailed);
  }
  if (scene_id.empty()) {
    const std::string old_scene_id = ActiveSceneForWindow(window);
    WorkspaceId presentation_workspace;
    if (const SceneDefinition *old_scene =
            runtime_.scenes().Find(old_scene_id)) {
      presentation_workspace = WorkspaceId::FromString(old_scene->workspace_id);
    }
    if (!presentation_workspace.is_valid()) {
      for (const auto &[workspace, owner] : presentation_owners_) {
        if (owner == window) {
          presentation_workspace = workspace;
          break;
        }
      }
    }
    active_scenes_by_window_.erase(window);
    pending_scene_restores_.erase(window);
    auto saved_theme = pre_scene_themes_by_window_.find(window);
    if (saved_theme == pre_scene_themes_by_window_.end() ||
        saved_theme->second.empty()) {
      active_themes_by_window_.erase(window);
    } else {
      active_themes_by_window_[window] = saved_theme->second;
    }
    pre_scene_themes_by_window_.erase(window);
    auto saved_tabs = pre_scene_vertical_tabs_by_window_.find(window);
    BrowserWindowInterface *browser = BrowserWindowInterface::FromSessionID(
        SessionID::FromSerializedValue(window.session_id()));
    tabs::VerticalTabStripStateController *vertical_tabs =
        browser && browser->GetProfile() == profile_
            ? tabs::VerticalTabStripStateController::From(browser)
            : nullptr;
    if (saved_tabs != pre_scene_vertical_tabs_by_window_.end()) {
      if (vertical_tabs) {
        vertical_tabs->SetExpandOnHoverEnabledForWindow(
            saved_tabs->second.expand_on_hover);
        vertical_tabs->RequestCollapse(saved_tabs->second.collapsed);
      }
      pre_scene_vertical_tabs_by_window_.erase(saved_tabs);
    }
    if (presentation_workspace.is_valid()) {
      const std::string restored_theme = ActiveThemeForWindow(window);
      if (restored_theme.empty()) {
        presentations_by_workspace_.erase(presentation_workspace);
        presentation_owners_.erase(presentation_workspace);
      } else {
        WorkspacePresentation presentation;
        presentation.active_theme_id = restored_theme;
        presentations_by_workspace_[presentation_workspace] =
            std::move(presentation);
        presentation_owners_[presentation_workspace] = window;
      }
    }
    RefreshSiteLayers();
    if (organization_ && organization_->shell_service()) {
      organization_->shell_service()->RefreshCompactModeState(window);
    }
    SchedulePersist();
    return base::ok();
  }
  SceneResult<std::vector<SceneActivationStep>> activation =
      runtime_.scenes().BuildActivationPlan(scene_id);
  if (!activation.has_value()) {
    return base::unexpected(activation.error());
  }
  const SceneDefinition *scene = runtime_.scenes().Find(scene_id);
  CommandExecutor *commands =
      organization_ ? organization_->command_executor() : nullptr;
  if (!scene || !commands) {
    return base::unexpected(SceneError::kActivationFailed);
  }
  BrowserWindowInterface *browser = BrowserWindowInterface::FromSessionID(
      SessionID::FromSerializedValue(window.session_id()));
  if (!browser || browser->GetProfile() != profile_ ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
      browser->IsDeleteScheduled()) {
    return base::unexpected(SceneError::kActivationFailed);
  }
  tabs::VerticalTabStripStateController *vertical_tabs =
      tabs::VerticalTabStripStateController::From(browser);
  if (scene->prefer_compact && !vertical_tabs) {
    return base::unexpected(SceneError::kActivationFailed);
  }
  BrowserCommand switch_workspace;
  switch_workspace.id = CommandId::Next();
  switch_workspace.kind = CommandKind::kSetActiveWorkspace;
  switch_workspace.window = window;
  switch_workspace.workspace_id = WorkspaceId::FromString(scene->workspace_id);
  if (!switch_workspace.workspace_id->is_valid() ||
      !commands->Submit(std::move(switch_workspace)).has_value()) {
    return base::unexpected(SceneError::kActivationFailed);
  }
  if (!active_scenes_by_window_.contains(window)) {
    pre_scene_themes_by_window_[window] = ActiveThemeForWindow(window);
    if (vertical_tabs) {
      pre_scene_vertical_tabs_by_window_[window] = {
          .collapsed = vertical_tabs->GetCollapseState() !=
                       tabs::VerticalTabStripCollapseState::kExpanded,
          .expand_on_hover = vertical_tabs->IsExpandOnHoverEnabled(),
      };
    }
  }
  active_scenes_by_window_[window] = scene_id;
  if (scene->theme_id.empty()) {
    active_themes_by_window_.erase(window);
  } else {
    active_themes_by_window_[window] = scene->theme_id;
  }
  if (vertical_tabs) {
    const auto baseline = pre_scene_vertical_tabs_by_window_.find(window);
    vertical_tabs->SetExpandOnHoverEnabledForWindow(
        scene->prefer_compact ||
        (baseline != pre_scene_vertical_tabs_by_window_.end() &&
         baseline->second.expand_on_hover));
    vertical_tabs->RequestCollapse(scene->prefer_compact);
  }
  const WorkspaceId scene_workspace =
      WorkspaceId::FromString(scene->workspace_id);
  for (auto owner = presentation_owners_.begin();
       owner != presentation_owners_.end();) {
    if (owner->second == window && owner->first != scene_workspace) {
      presentations_by_workspace_.erase(owner->first);
      owner = presentation_owners_.erase(owner);
    } else {
      ++owner;
    }
  }
  WorkspacePresentation presentation;
  presentation.active_scene_id = scene_id;
  presentation.active_theme_id = scene->theme_id;
  presentation.pre_scene_theme_id = pre_scene_themes_by_window_[window];
  if (const auto baseline = pre_scene_vertical_tabs_by_window_.find(window);
      baseline != pre_scene_vertical_tabs_by_window_.end()) {
    presentation.has_vertical_tabs_baseline = true;
    presentation.vertical_tabs_baseline = baseline->second;
  }
  presentations_by_workspace_[scene_workspace] = std::move(presentation);
  presentation_owners_[scene_workspace] = window;
  RefreshSiteLayers();
  RestoreArchivedTabsForScene(*scene, window);
  RunSceneActivationWorkflows(scene_id, window);
  if (organization_ && organization_->shell_service()) {
    organization_->shell_service()->RefreshCompactModeState(window);
  }
  SchedulePersist();
  return base::ok();
}

MutationResult<EssentialId> SeoulRuntimeService::UpsertEssential(
    const EssentialId &essential_id,
    const std::string &name,
    const std::string &root_url) {
  if (shutting_down_ || !organization_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  return organization_->model().CreateOrUpdateEssential(essential_id, name,
                                                        root_url);
}

MutationStatus
SeoulRuntimeService::RemoveEssential(const EssentialId &essential_id) {
  if (shutting_down_ || !organization_) {
    return Err(OrganizationError::kEssentialNotFound);
  }
  return organization_->model().RemoveEssential(essential_id);
}

MutationResult<RoutingRuleId>
SeoulRuntimeService::UpsertRoutingRule(RoutingRule rule) {
  if (shutting_down_ || !organization_) {
    return Err(OrganizationError::kInvalidRoutingRule);
  }
  if (rule.id.is_valid()) {
    MutationStatus updated = organization_->model().UpdateRoutingRule(rule);
    return updated.has_value() ? MutationResult<RoutingRuleId>(rule.id)
                               : base::unexpected(updated.error());
  }
  return organization_->model().AddRoutingRule(rule);
}

MutationStatus
SeoulRuntimeService::RemoveRoutingRule(const RoutingRuleId &rule_id) {
  if (shutting_down_ || !organization_) {
    return Err(OrganizationError::kRoutingRuleNotFound);
  }
  for (const SceneDefinition *scene : runtime_.scenes().List()) {
    if (std::ranges::find(scene->routing_rule_ids, rule_id.value()) !=
        scene->routing_rule_ids.end()) {
      return Err(OrganizationError::kResourceInUse);
    }
  }
  return organization_->model().RemoveRoutingRule(rule_id);
}

WorkflowId SeoulRuntimeService::UpsertWorkflow(WorkflowDefinition definition) {
  if (shutting_down_ || !workflow_service_) {
    return WorkflowId();
  }
  if ((!definition.scene_scope.empty() &&
       !runtime_.scenes().Find(definition.scene_scope)) ||
      (definition.trigger.kind == WorkflowTriggerKind::kSceneActivation &&
       (!runtime_.scenes().Find(definition.trigger.scene_id) ||
        (!definition.scene_scope.empty() &&
         definition.scene_scope != definition.trigger.scene_id)))) {
    return WorkflowId();
  }
  const WorkflowId id = workflow_service_->SaveWorkflow(std::move(definition));
  if (id.is_valid()) {
    SchedulePersist();
  }
  return id;
}

WorkflowStatusResult
SeoulRuntimeService::RemoveWorkflow(const WorkflowId &workflow_id) {
  if (shutting_down_ || !workflow_service_ ||
      !workflow_service_->Find(workflow_id)) {
    return base::unexpected(WorkflowError::kUnknownWorkflow);
  }
  for (const SceneDefinition *scene : runtime_.scenes().List()) {
    if (std::ranges::find(scene->workflow_shortcut_ids, workflow_id.value()) !=
        scene->workflow_shortcut_ids.end()) {
      return base::unexpected(WorkflowError::kInUse);
    }
  }
  if (!workflow_service_->DeleteWorkflow(workflow_id)) {
    return base::unexpected(WorkflowError::kUnknownWorkflow);
  }
  SchedulePersist();
  return base::ok();
}

WorkflowResult<WorkflowId>
SeoulRuntimeService::DuplicateWorkflowForStudio(const WorkflowId &workflow_id) {
  if (shutting_down_ || !workflow_service_) {
    return base::unexpected(WorkflowError::kUnknownWorkflow);
  }
  std::optional<WorkflowId> duplicate =
      workflow_service_->DuplicateWorkflow(workflow_id);
  if (!duplicate.has_value()) {
    return base::unexpected(workflow_service_->Find(workflow_id)
                                ? WorkflowError::kLimitExceeded
                                : WorkflowError::kUnknownWorkflow);
  }
  SchedulePersist();
  return duplicate.value();
}

TaskId SeoulRuntimeService::RunWorkflowForStudio(const WorkflowId &workflow_id,
                                                 const LiveWindowKey &window) {
  if (shutting_down_ || !workflow_service_ || !window.is_valid()) {
    return TaskId();
  }
  return workflow_service_->RunWorkflow(workflow_id, window,
                                        BuildPermissionContext(window));
}

OrganizationSnapshot SeoulRuntimeService::StudioOrganizationSnapshot() const {
  return organization_ ? organization_->model().ToSnapshot()
                       : OrganizationSnapshot();
}

std::string
SeoulRuntimeService::ActiveSceneForWindow(const LiveWindowKey &window) const {
  const auto found = active_scenes_by_window_.find(window);
  return found == active_scenes_by_window_.end() ? std::string()
                                                 : found->second;
}

std::string
SeoulRuntimeService::ActiveThemeForWindow(const LiveWindowKey &window) const {
  const auto found = active_themes_by_window_.find(window);
  return found == active_themes_by_window_.end() ? std::string()
                                                 : found->second;
}

bool SeoulRuntimeService::AllowCloudModels(const LiveWindowKey &window) const {
  const std::string scene_id = ActiveSceneForWindow(window);
  if (scene_id.empty()) {
    return true;
  }
  const SceneDefinition *scene = runtime_.scenes().Find(scene_id);
  // Active Scene ids are registry-owned and cannot be deleted while active.
  // Fail closed if invariants are ever violated instead of silently widening
  // a policy after catalog corruption.
  return scene && scene->assistant.allow_cloud_models;
}

RoutingResolution SeoulRuntimeService::ResolveLinkRouting(
    const LiveWindowKey &window, const GURL &destination, bool user_gesture,
    RoutingDisposition requested_disposition) const {
  RoutingResolution fallback;
  fallback.result.disposition = requested_disposition;
  fallback.used_fallback = true;
  if (!organization_ || !window.is_valid() || !destination.is_valid() ||
      !destination.SchemeIsHTTPOrHTTPS()) {
    return fallback;
  }
  RoutingRequest request;
  request.url = destination.spec();
  request.origin = url::Origin::Create(destination).Serialize();
  request.source_workspace =
      organization_->model().ActiveWorkspaceForWindow(window.value());
  if (const std::optional<LiveTabDescriptor> active =
          ActiveTabDescriptor(window)) {
    request.source_tab_key = active->tab.value();
  }
  request.user_gesture = user_gesture;
  request.requested_disposition = requested_disposition;

  const std::string scene_id = ActiveSceneForWindow(window);
  if (scene_id.empty()) {
    return organization_->model().EvaluateRouting(request);
  }
  const SceneDefinition *scene = runtime_.scenes().Find(scene_id);
  if (!scene) {
    return fallback;
  }
  std::set<RoutingRuleId> eligible;
  for (const std::string &rule_id : scene->routing_rule_ids) {
    const RoutingRuleId id = RoutingRuleId::FromString(rule_id);
    if (id.is_valid()) {
      eligible.insert(id);
    }
  }
  return organization_->model().EvaluateRouting(request, eligible);
}

PreviewPromotionRoute SeoulRuntimeService::ResolvePreviewPromotionRoute(
    const PreviewRecord &preview,
    PreviewPromotionTarget requested_target) const {
  PreviewPromotionRoute route;
  route.target = requested_target;
  route.source_workspace =
      organization_ ? organization_->model().ActiveWorkspaceForWindow(
                          preview.window.value())
                    : WorkspaceId();
  route.target_workspace = route.source_workspace;
  if (!organization_ || !route.source_workspace.is_valid() ||
      !preview.current_url.is_valid() ||
      !preview.current_url.SchemeIsHTTPOrHTTPS()) {
    route.allowed = false;
    route.blocked_reason =
        "This Preview is no longer attached to a valid Workspace.";
    return route;
  }

  const RoutingDisposition requested_disposition =
      requested_target == PreviewPromotionTarget::kSplit
          ? RoutingDisposition::kSplitPane
          : RoutingDisposition::kNewRetainedTab;
  const RoutingResolution resolution =
      ResolveLinkRouting(preview.window, preview.current_url,
                         /*user_gesture=*/true, requested_disposition);
  switch (resolution.result.disposition) {
  case RoutingDisposition::kSpecificWorkspace: {
    const WorkspaceRecord *workspace = organization_->model().FindWorkspace(
        resolution.result.target_workspace);
    if (!workspace || workspace->archived) {
      route.allowed = false;
      route.blocked_reason =
          "The routed Workspace is unavailable. Update the routing rule.";
      return route;
    }
    route.target = PreviewPromotionTarget::kTab;
    route.target_workspace = workspace->id;
    return route;
  }
  case RoutingDisposition::kSplitPane:
    route.target = PreviewPromotionTarget::kSplit;
    return route;
  case RoutingDisposition::kAskUser:
    route.allowed = false;
    route.blocked_reason =
        "This routing rule requires approval before the Preview can be "
        "promoted.";
    return route;
  case RoutingDisposition::kExternalApplication:
    route.allowed = false;
    route.blocked_reason =
        "This destination is routed to an external application.";
    return route;
  case RoutingDisposition::kCurrentTab:
  case RoutingDisposition::kNewTemporaryTab:
  case RoutingDisposition::kNewRetainedTab:
  case RoutingDisposition::kPreview:
    // Explicit promotion always creates a retained tab. A Preview disposition
    // is treated as the current overlay rather than recursively opening one.
    route.target = PreviewPromotionTarget::kTab;
    return route;
  }
  route.allowed = false;
  route.blocked_reason = "This Preview route is not available.";
  return route;
}

bool SeoulRuntimeService::SwitchWorkspaceForPreview(LiveWindowKey window,
                                                    WorkspaceId workspace) {
  if (!organization_ || !window.is_valid() || !workspace.is_valid()) {
    return false;
  }
  // Promotion activates the transferred tab immediately in this same UI
  // sequence. Updating the model first keeps membership attribution atomic and
  // avoids briefly activating an unrelated existing tab in the destination
  // Workspace before the promoted WebContents is inserted.
  return organization_->model()
             .SetActiveWorkspaceForWindow(window.value(), workspace)
             .has_value() &&
         organization_->model().ActiveWorkspaceForWindow(window.value()) ==
             workspace;
}

void SeoulRuntimeService::RunSceneActivationWorkflows(
    const std::string &scene_id, const LiveWindowKey &window) {
  if (!workflow_service_ || !window.is_valid()) {
    return;
  }
  // Snapshot ids before starting tasks. A task may synchronously notify
  // observers, but it cannot invalidate this stable workflow catalog walk.
  const std::vector<WorkflowId> workflow_ids = workflow_service_->All();
  const ToolPermissionContext context = BuildPermissionContext(window);
  for (const WorkflowId &workflow_id : workflow_ids) {
    const WorkflowDefinition *workflow = workflow_service_->Find(workflow_id);
    if (!workflow ||
        workflow->trigger.kind != WorkflowTriggerKind::kSceneActivation ||
        workflow->trigger.scene_id != scene_id ||
        (!workflow->scene_scope.empty() && workflow->scene_scope != scene_id)) {
      continue;
    }
    // Every triggered run enters the Task Deck through the same compiler,
    // validation, permission, approval, and receipt path as a manual run.
    std::ignore = workflow_service_->RunWorkflow(workflow_id, window, context,
                                                 /*user_gesture=*/false);
  }
}

std::string SeoulRuntimeService::SceneForTab(const LiveTabKey &tab) const {
  for (const auto &[window, tabs] : live_tabs_by_window_) {
    if (tabs.contains(tab)) {
      return ActiveSceneForWindow(window);
    }
  }
  return std::string();
}

bool SeoulRuntimeService::RoutingRuleExists(const std::string &rule_id) const {
  if (!organization_) {
    return false;
  }
  const RoutingRuleId id = RoutingRuleId::FromString(rule_id);
  if (!id.is_valid()) {
    return false;
  }
  const OrganizationSnapshot snapshot = organization_->model().ToSnapshot();
  return std::ranges::any_of(
      snapshot.routing_rules,
      [&id](const RoutingRule &rule) { return rule.id == id; });
}

bool SeoulRuntimeService::WorkflowExists(const std::string &workflow_id) const {
  return workflow_service_ &&
         workflow_service_->Find(WorkflowId::FromString(workflow_id));
}

bool SeoulRuntimeService::ConfigureLocalProvider(
    const std::string &endpoint_url, const std::string &model_id) {
  if (shutting_down_ || !provider_registry_ ||
      !provider_registry_->ConfigureLocal(endpoint_url, model_id)) {
    return false;
  }
  SchedulePersist();
  return true;
}

void SeoulRuntimeService::ClearLocalProvider() {
  if (shutting_down_ || !provider_registry_) {
    return;
  }
  provider_registry_->ClearLocal();
  SchedulePersist();
}

bool SeoulRuntimeService::ConfigureCloudProvider(
    const std::string &model_id, bool enabled,
    const std::string &reasoning_secret, const std::string &voice_secret) {
  if (shutting_down_ || !provider_registry_ || !credentials_ ||
      model_id.empty()) {
    return false;
  }

  const std::optional<std::string> previous_reasoning =
      credentials_->Get(kCloudReasoningCredentialAccount);
  const std::optional<std::string> previous_voice =
      credentials_->Get(kRealtimeVoiceCredentialAccount);
  auto restore = [&](const char *account,
                     const std::optional<std::string> &previous) {
    if (previous.has_value()) {
      std::ignore = credentials_->Set(account, *previous);
    } else {
      std::ignore = credentials_->Delete(account);
    }
  };

  if (!reasoning_secret.empty() &&
      !credentials_->Set(kCloudReasoningCredentialAccount, reasoning_secret)) {
    return false;
  }
  if (!voice_secret.empty() &&
      !credentials_->Set(kRealtimeVoiceCredentialAccount, voice_secret)) {
    restore(kCloudReasoningCredentialAccount, previous_reasoning);
    return false;
  }
  if (!provider_registry_->ConfigureCloud(model_id, enabled)) {
    restore(kCloudReasoningCredentialAccount, previous_reasoning);
    restore(kRealtimeVoiceCredentialAccount, previous_voice);
    return false;
  }
  SchedulePersist();
  return true;
}

bool SeoulRuntimeService::ClearCloudProviderAndCredentials() {
  if (shutting_down_ || !provider_registry_ || !credentials_) {
    return false;
  }
  const std::optional<std::string> previous_reasoning =
      credentials_->Get(kCloudReasoningCredentialAccount);
  const std::optional<std::string> previous_voice =
      credentials_->Get(kRealtimeVoiceCredentialAccount);
  if (previous_reasoning.has_value() &&
      !credentials_->Delete(kCloudReasoningCredentialAccount)) {
    return false;
  }
  if (previous_voice.has_value() &&
      !credentials_->Delete(kRealtimeVoiceCredentialAccount)) {
    if (previous_reasoning.has_value()) {
      std::ignore = credentials_->Set(kCloudReasoningCredentialAccount,
                                      *previous_reasoning);
    }
    return false;
  }
  provider_registry_->ClearCloud();
  SchedulePersist();
  return true;
}

bool SeoulRuntimeService::SetCredential(const std::string &account_key,
                                        const std::string &secret) {
  return credentials_ && credentials_->Set(account_key, secret);
}

bool SeoulRuntimeService::DeleteCredential(const std::string &account_key) {
  return credentials_ && credentials_->Delete(account_key);
}

bool SeoulRuntimeService::PersistState() {
  if (!prefs_ || shutting_down_) {
    return false;
  }
  base::DictValue state;
  if (surface_service_) {
    state.Set("surfaces", surface_service_->TakePersistedState());
  }
  if (thread_service_) {
    state.Set("threads", thread_service_->TakePersistedState());
  }
  if (workflow_service_) {
    state.Set("workflows", workflow_service_->TakePersistedState());
  }
  if (provider_registry_) {
    state.Set("providers", provider_registry_->TakePersistedState());
  }
  if (library_service_) {
    state.Set("library", library_service_->TakePersistedState());
  }
  if (themes_) {
    state.Set("themes", themes_->TakePersistedState());
  }
  if (site_layers_) {
    state.Set("site_layers", site_layers_->TakePersistedState());
  }
  state.Set("scenes", runtime_.scenes().TakePersistedState());
  state.Set("presentations", TakePresentationState());
  state.Set("compact_mode", TakeCompactModeState());
  prefs_->SetDict(kProductRuntimePref, std::move(state));
  return true;
}

void SeoulRuntimeService::SchedulePersist() {
  if (persistence_scheduler_ && !shutting_down_) {
    persistence_scheduler_->ScheduleWrite();
  }
}

void SeoulRuntimeService::OnProjectResourcesChanged() {
  SchedulePersist();
  if (!shutting_down_ && organization_ &&
      organization_->shell_service()) {
    organization_->shell_service()->RefreshProjectResources();
  }
}

void SeoulRuntimeService::LoadState() {
  if (!prefs_) {
    return;
  }
  const base::DictValue &state = prefs_->GetDict(kProductRuntimePref);
  if (const base::DictValue *surfaces = state.FindDict("surfaces")) {
    surface_service_->RestorePersistedState(*surfaces);
  }
  if (const base::DictValue *threads = state.FindDict("threads")) {
    thread_service_->RestorePersistedState(*threads);
  }
  if (const base::DictValue *workflows = state.FindDict("workflows")) {
    workflow_service_->RestorePersistedState(*workflows);
  }
  if (const base::DictValue *providers = state.FindDict("providers")) {
    provider_registry_->RestorePersistedState(*providers);
  }
  if (const base::DictValue *library = state.FindDict("library")) {
    library_service_->RestorePersistedState(*library);
  }
  if (const base::DictValue *themes = state.FindDict("themes")) {
    themes_->RestorePersistedState(*themes);
  }
  if (const base::DictValue *site_layers = state.FindDict("site_layers")) {
    site_layers_->RestorePersistedState(*site_layers);
  }
  // Scenes load after all referenced catalogs so invalid/removed references
  // are rejected during restore rather than retained as latent failures.
  if (const base::DictValue *scenes = state.FindDict("scenes")) {
    runtime_.scenes().RestorePersistedState(*scenes);
  }
  // Scenes can reference workflows while workflows can be Scene-scoped, so
  // neither catalog can be fully validated in a single restore order. Reduce
  // both to a valid fixpoint. The finite catalogs only shrink, guaranteeing
  // termination without retaining latent activation failures.
  if (workflow_service_) {
    const auto scene_exists = base::BindRepeating(
        [](SceneRegistry *scenes, const std::string &scene_id) {
          return scenes && scenes->Find(scene_id);
        },
        &runtime_.scenes());
    while (true) {
      const size_t removed_workflows =
          workflow_service_->PruneInvalidSceneReferences(scene_exists);
      const size_t removed_scenes = runtime_.scenes().PruneInvalidEntries();
      if (removed_workflows == 0 && removed_scenes == 0) {
        break;
      }
    }
  }
  // Presentation references are accepted only after the two cross-referencing
  // catalogs reach their valid fixpoint.
  if (const base::DictValue *presentations = state.FindDict("presentations")) {
    RestorePresentationState(*presentations);
  }
  if (const base::DictValue *compact_mode = state.FindDict("compact_mode")) {
    RestoreCompactModeState(*compact_mode);
  }
}

void SeoulRuntimeService::Shutdown() {
  if (shutting_down_) {
    return;
  }
  if (organization_ && organization_->shell_service()) {
    organization_->shell_service()->SetOpenBoostCallback({});
    organization_->shell_service()->SetCompactModeCallbacks({}, {});
    organization_->shell_service()->SetProjectCallbacks({}, {}, {}, {});
  }
  weak_factory_.InvalidateWeakPtrs();
  scene_reconciliation_pending_ = false;
  scene_lifecycle_timer_.Stop();
  live_collection_timer_.Stop();
  pending_scene_restores_.clear();
  if (live_collection_coordinator_) {
    live_collection_coordinator_->Shutdown();
  }
  if (organization_) {
    organization_->model().RemoveObserver(this);
  }
  if (organization_ && organization_->command_executor()) {
    organization_->command_executor()->RemoveCompletionObserver(this);
  }
  // Persist durable product state before anything is torn down.
  // PersistState intentionally refuses writes after shutdown begins, so the
  // ordering here is a correctness invariant.
  bool persisted = false;
  if (persistence_scheduler_) {
    persistence_scheduler_->Shutdown();
    persisted = persistence_scheduler_->Flush();
  }
  // Flush is a no-op when no Library mutation is pending. The explicit write
  // still captures changes owned by the other runtime services; when Flush did
  // write, avoid issuing the same preference write twice.
  if (!persisted) {
    PersistState();
  }
  shutting_down_ = true;
  live_window_observation_.Reset();
  live_tabs_by_window_.clear();
  site_layer_applicators_.clear();
  window_bindings_.clear();
  // Reverse dependency order: tasks depend on providers/executors; workflows
  // depend on tasks; the runtime registries are torn down last.
  voice_controller_.reset();
  if (task_service_) {
    task_service_->RemoveObserver(this);
    if (organization_) {
      if (ShellService *shell = organization_->shell_service()) {
        shell->ClearTaskSummaries();
      }
    }
    task_service_->Shutdown();
  }
  if (agent_permissions_) {
    agent_permissions_->RevokeAll();
  }
  // The bridge observes the task service and drives the surface service; drop
  // it before either so no notification arrives after teardown.
  task_surface_bridge_.reset();
  workflow_service_.reset();
  thread_service_.reset();
  surface_service_.reset();
  live_collection_coordinator_.reset();
  library_service_.reset();
  persistence_scheduler_.reset();
  task_service_.reset();
  // Concrete browser executors observe services owned by the organization
  // keyed service. Destroy them during Shutdown, while that dependency is
  // guaranteed alive, rather than from this object's later destructor.
  executors_.Clear();
  if (preview_host_service_) {
    preview_host_service_->Shutdown();
    preview_host_service_.reset();
  }
  preview_manager_.reset();
  agent_permissions_.reset();
  text_to_speech_.reset();
  speech_to_text_.reset();
  if (realtime_voice_agent_) {
    realtime_voice_agent_->Cancel();
    realtime_voice_agent_.reset();
  }
  planner_.reset();
  if (provider_registry_) {
    provider_registry_->Shutdown();
    provider_registry_.reset();
  }
  page_agent_.reset();
  runtime_.Shutdown();
  credentials_.reset();
  local_transport_.reset();
  cloud_transport_.reset();
}

SeoulRuntimeService::WindowBindingRecord::WindowBindingRecord() = default;
SeoulRuntimeService::WindowBindingRecord::WindowBindingRecord(
    WindowBindingRecord &&) = default;
SeoulRuntimeService::WindowBindingRecord &
SeoulRuntimeService::WindowBindingRecord::operator=(WindowBindingRecord &&) =
    default;
SeoulRuntimeService::WindowBindingRecord::~WindowBindingRecord() = default;

SeoulRuntimeService::PendingSceneRestore::PendingSceneRestore() = default;
SeoulRuntimeService::PendingSceneRestore::~PendingSceneRestore() = default;

SeoulRuntimeService::WorkspacePresentation::WorkspacePresentation() = default;
SeoulRuntimeService::WorkspacePresentation::WorkspacePresentation(
    const WorkspacePresentation &) = default;
SeoulRuntimeService::WorkspacePresentation::WorkspacePresentation(
    WorkspacePresentation &&) = default;
SeoulRuntimeService::WorkspacePresentation &
SeoulRuntimeService::WorkspacePresentation::operator=(
    const WorkspacePresentation &) = default;
SeoulRuntimeService::WorkspacePresentation &
SeoulRuntimeService::WorkspacePresentation::operator=(
    WorkspacePresentation &&) = default;
SeoulRuntimeService::WorkspacePresentation::~WorkspacePresentation() = default;

} // namespace seoul
