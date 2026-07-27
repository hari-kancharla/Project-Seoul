// Project Seoul product runtime - the profile-scoped product owner.
// The single authoritative product runtime for a regular profile. It does NOT
// duplicate the organization service; it references it (through the factory
// DependsOn) for the model, lifecycle, command executor, projection, and
// shell, and adds the product layer on top: the capability graph and its
// executors, the connector/provider registries, the planner, task, surface,
// thread, and workflow services, the page agent, and the concrete network and
// credential transports.
//
// STATE OWNERSHIP
//   owner:        the profile (via SeoulRuntimeServiceFactory, a
//                 ProfileKeyedServiceFactory that DependsOn the organization
//                 service factory).
//   lifetime:     the regular original profile; excluded for OTR/guest/system.
//   persistence:  pinned surfaces, threads, workflows, and provider settings
//                 serialize to one bounded profile pref; secrets never do.
//   recovery:     product services rebuild from that pref on construction.
//   teardown:     Shutdown() tears down in reverse dependency order, removes
//                 observers before their subjects, and cancels in-progress
//                 work; no callback runs afterward.
//   isolation:    per profile; never process-global.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_RUNTIME_SERVICE_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_RUNTIME_SERVICE_H_

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "base/unguessable_token.h"
#include "components/keyed_service/core/keyed_service.h"
#include "seoul/browser/commands/command_completion_observer.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/lifecycle/live_window_state.h"
#include "seoul/browser/organization/organization_errors.h"
#include "seoul/browser/organization/organization_observer.h"
#include "seoul/browser/organization/organization_types.h"
#include "seoul/browser/policy/agent_permission_service.h"
#include "seoul/browser/preview/preview_manager.h"
#include "seoul/browser/product/browser/page_agent.h"
#include "seoul/browser/product/capability_executor.h"
#include "seoul/browser/product/planner.h"
#include "seoul/browser/product/provider_registry.h"
#include "seoul/browser/product/realtime_voice_agent.h"
#include "seoul/browser/product/surface_service.h"
#include "seoul/browser/product/task_service.h"
#include "seoul/browser/product/task_surface_bridge.h"
#include "seoul/browser/product/thread_service.h"
#include "seoul/browser/product/voice_runtime_controller.h"
#include "seoul/browser/product/workflow_service.h"
#include "seoul/browser/runtime/seoul_runtime.h"
#include "seoul/browser/scenes/scene_registry.h"
#include "seoul/browser/site_layers/site_layer_types.h"
#include "seoul/browser/themes/theme_registry.h"
#include "seoul/browser/tools/tool_registry.h"

class PrefService;
class Profile;
class BrowserWindowInterface;
class GURL;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace content {
class WebContents;
}

namespace seoul {

class SeoulOrganizationService;
class CredentialStore;
class HttpTransport;
class SceneRegistry;
class SiteLayerRegistry;
class SiteLayerApplicator;
class ThemeRegistry;
class LibraryService;
class LiveCollectionCoordinator;
class PersistenceScheduler;
class PreviewHostService;
struct PreviewPromotionRoute;

// Single dict pref holding the bounded product state (pinned surfaces, Library,
// threads, workflows, Scenes, Themes, Site Layers, provider settings). Secrets
// are excluded by construction.
inline constexpr char kProductRuntimePref[] = "seoul.product.v1";

using WindowRuntimeBindingToken = base::UnguessableToken;

struct WindowRuntimeBinding {
  WindowRuntimeBindingToken token;
  LiveWindowKey window;

  bool is_valid() const { return !token.is_empty() && window.is_valid(); }
};

class SeoulRuntimeService : public KeyedService,
                            public LiveWindowStateObserver,
                            public TaskServiceObserver,
                            public CommandCompletionObserver,
                            public OrganizationModelObserver {
public:
  // `organization` must be the same profile's organization service (the
  // factory guarantees it via DependsOn). `web_contents_resolver` maps a
  // live tab key to its WebContents for the page agent.
  SeoulRuntimeService(Profile *profile, PrefService *prefs,
                      SeoulOrganizationService *organization,
                      WebContentsResolver web_contents_resolver);
  SeoulRuntimeService(const SeoulRuntimeService &) = delete;
  SeoulRuntimeService &operator=(const SeoulRuntimeService &) = delete;
  ~SeoulRuntimeService() override;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable *registry);

  // Narrow accessors (never the whole mutable runtime).
  TaskService *tasks() { return task_service_.get(); }
  SurfaceService *surfaces() { return surface_service_.get(); }
  ThreadService *threads() { return thread_service_.get(); }
  WorkflowService *workflows() { return workflow_service_.get(); }
  TaskSurfaceBridge *task_surface_bridge() {
    return task_surface_bridge_.get();
  }
  ProviderRegistry *providers() { return provider_registry_.get(); }
  PreviewManager *previews() { return preview_manager_.get(); }
  PreviewHostService *preview_host() { return preview_host_service_.get(); }
  ToolRegistry &capabilities() { return runtime_.capabilities(); }
  // Exact runtime invariant used by diagnostics and integration tests: an
  // available descriptor is runnable only when the matching version has a
  // concrete executor.
  bool HasCapabilityExecutor(const ToolId &id, int version) const {
    return executors_.Find(id, version) != nullptr;
  }
  SceneRegistry *scenes() { return &runtime_.scenes(); }
  ThemeRegistry *themes() { return themes_.get(); }
  PageAgent *page_agent() { return page_agent_.get(); }
  SiteLayerRegistry *site_layers() { return site_layers_.get(); }
  LibraryService *library() { return library_service_.get(); }
  LiveCollectionCoordinator *live_collections() {
    return live_collection_coordinator_.get();
  }
  VoiceRuntimeController *voice() { return voice_controller_.get(); }
  AgentPermissionService *agent_permissions() {
    return agent_permissions_.get();
  }
  LiveWindowStateProvider *live_window_state_provider() const;

  // The permission context for a user-initiated turn in `window`, built from
  // provider availability and the connected connector providers.
  ToolPermissionContext BuildPermissionContext() const;
  ToolPermissionContext
  BuildPermissionContext(const LiveWindowKey &window) const;

  // Creates an opaque binding from a Canvas WebContents' embedding browser
  // window to the exact Seoul live window. A turn must present this token back
  // before it can act on browser state. Tokens are per profile, per window, and
  // invalidated when the browser window closes.
  WindowRuntimeBinding CreateWindowBinding(BrowserWindowInterface *browser);
  std::optional<LiveWindowKey>
  ResolveWindowBinding(const WindowRuntimeBindingToken &token) const;
  void InvalidateWindowBinding(const WindowRuntimeBindingToken &token);
  std::optional<LiveTabDescriptor>
  ActiveTabDescriptor(const LiveWindowKey &window) const;

  // Entry point for a text goal from Canvas/voice: plans and runs a task in
  // `window`, returning the task id. The final semantic result flows to the
  // surface service; the Canvas observes both services.
  TaskId StartGoal(const std::string &goal, const LiveWindowKey &window);
  VoiceStatusResult StartVoice(const LiveWindowKey &window);
  VoiceStatusResult StopVoice();
  VoiceRuntimeSnapshot VoiceSnapshot() const;
  void CreateRealtimeVoiceSession(
      const std::string &safety_identifier, const LiveWindowKey &window,
      RealtimeVoiceAgent::CreateSessionCallback callback);
  RealtimeVoiceAgentSnapshot RealtimeVoiceSnapshot() const;

  // Validated Site Layer mutations. Successful changes persist through the
  // profile owner and are applied immediately to every matching live page.
  SiteLayerStatusResult UpsertSiteLayer(SiteLayer layer);
  SiteLayerStatusResult RemoveSiteLayer(const std::string &layer_id);
  void RefreshSiteLayers();
  using SiteLayerZapCallback =
      base::OnceCallback<void(bool changed, SiteLayerStatusResult result)>;
  void BeginSiteLayerZap(const std::string &layer_id,
                         const LiveWindowKey &window,
                         bool remove_layer_on_cancel,
                         SiteLayerZapCallback callback);
  void CancelSiteLayerZap(const LiveWindowKey &window);

  // Native discovery points (site controls and the omnibox action) request the
  // same Canvas editor. Requests are retained until the WebUI binds so a cold
  // side-panel open cannot lose the intent.
  using BoostEditorRequestCallback =
      base::RepeatingCallback<void(const LiveWindowKey &window)>;
  base::CallbackListSubscription AddBoostEditorRequestCallback(
      BoostEditorRequestCallback callback);
  void RequestBoostEditor(const LiveWindowKey &window);
  bool ConsumeBoostEditorRequest(const LiveWindowKey &window);

  // Studio mutations. Every operation validates against the authoritative
  // profile owners before committing and schedules durable persistence only
  // after success. Deletions that would leave a Scene dangling fail with
  // kInUse instead of silently corrupting the catalog.
  ThemeStatusResult UpsertTheme(Theme theme);
  ThemeStatusResult RemoveTheme(const std::string &theme_id);
  ThemeStatusResult ActivateTheme(const std::string &theme_id,
                                  const LiveWindowKey &window);
  SceneStatusResult UpsertScene(SceneDefinition scene);
  SceneStatusResult RemoveScene(const std::string &scene_id);
  SceneStatusResult ActivateScene(const std::string &scene_id,
                                  const LiveWindowKey &window);
  // Standalone compact chrome is a user-owned Workspace preference. A Scene
  // temporarily owns the same native controls, so direct changes fail while a
  // Scene is active and resume from the exact saved baseline when it leaves.
  bool SetCompactMode(bool enabled, const LiveWindowKey &window);
  std::optional<bool> CompactModeForWindow(const LiveWindowKey &window) const;
  bool IsCompactModeApplied(bool enabled, const LiveWindowKey &window) const;
  MutationResult<EssentialId> UpsertEssential(const EssentialId &essential_id,
                                              const std::string &name,
                                              const std::string &root_url);
  MutationStatus RemoveEssential(const EssentialId &essential_id);
  MutationResult<RoutingRuleId> UpsertRoutingRule(RoutingRule rule);
  MutationStatus RemoveRoutingRule(const RoutingRuleId &rule_id);
  WorkflowId UpsertWorkflow(WorkflowDefinition definition);
  WorkflowStatusResult RemoveWorkflow(const WorkflowId &workflow_id);
  WorkflowResult<WorkflowId>
  DuplicateWorkflowForStudio(const WorkflowId &workflow_id);
  TaskId RunWorkflowForStudio(const WorkflowId &workflow_id,
                              const LiveWindowKey &window);
  OrganizationSnapshot StudioOrganizationSnapshot() const;
  std::string ActiveSceneForWindow(const LiveWindowKey &window) const;
  std::string ActiveThemeForWindow(const LiveWindowKey &window) const;

  // Runs the same Scene lifecycle sweep used by the production timer. Exposed
  // only so an in-process browser test can age real membership state without
  // waiting a wall-clock minute.
  void RunSceneLifecycleMaintenanceForTesting();

  // Studio provider settings. Local endpoints remain loopback-only; cloud and
  // realtime secrets are written only to the OS credential store and are
  // never persisted in the product pref or returned to Canvas.
  bool ConfigureLocalProvider(const std::string &endpoint_url,
                              const std::string &model_id);
  void ClearLocalProvider();
  bool ConfigureCloudProvider(const std::string &model_id, bool enabled,
                              const std::string &reasoning_secret,
                              const std::string &voice_secret);
  bool ClearCloudProviderAndCredentials();

  // Runs one already-chosen capability with an explicit typed payload as a
  // single-step task. A surface action that declared a tool_call executes
  // exactly this capability with exactly its declared arguments; the payload
  // is never re-inferred from text. Returns an invalid id for an unknown
  // capability or window (StartTaskWithPlan re-validates against the schema
  // and permission context).
  TaskId StartCapability(const std::string &capability_id, base::DictValue args,
                         const LiveWindowKey &window);

  // Credential writes go straight to the OS store; the value never returns to
  // any caller (the Canvas only learns presence via ProviderRegistry).
  bool SetCredential(const std::string &account_key, const std::string &secret);
  bool DeleteCredential(const std::string &account_key);

  // KeyedService:
  void Shutdown() override;

private:
  // LiveWindowStateObserver. Revocation follows the browser lifecycle rather
  // than any particular Canvas document or surface binding.
  void OnLiveWindowSnapshotChanged(const LiveWindowSnapshot &snapshot) override;
  void OnLiveWindowRemoved(LiveWindowKey window) override;
  // TaskServiceObserver: publishes only bounded state counts into the native
  // shell. Detailed goals, prompts, receipts, and results stay in TaskService.
  void OnTaskUpdated(const TaskId &task_id) override;
  void OnTaskFinished(const TaskId &task_id) override;
  // CommandCompletionObserver: serializes Scene restore insertions so an
  // observed tab can never be adopted by the wrong restore command.
  void OnCommandCompleted(CommandId id, CommandKind kind,
                          CommandStatus status) override;
  // OrganizationModelObserver: keeps active Scene state from drifting if a
  // workspace/routing command changes a referenced organization object.
  void OnOrganizationChanged(const OrganizationChange &change) override;
  void PublishShellTaskSummary(const LiveWindowKey &window);
  std::string SceneForTab(const LiveTabKey &tab) const;
  bool RoutingRuleExists(const std::string &rule_id) const;
  bool WorkflowExists(const std::string &workflow_id) const;
  bool AllowCloudModels(const LiveWindowKey &window) const;
  RoutingResolution
  ResolveLinkRouting(const LiveWindowKey &window, const GURL &destination,
                     bool user_gesture,
                     RoutingDisposition requested_disposition) const;
  PreviewPromotionRoute
  ResolvePreviewPromotionRoute(const PreviewRecord &preview,
                               PreviewPromotionTarget requested_target) const;
  bool SwitchWorkspaceForPreview(LiveWindowKey window, WorkspaceId workspace);
  void RunSceneActivationWorkflows(const std::string &scene_id,
                                   const LiveWindowKey &window);
  void RunSceneLifecycleMaintenance();
  void RunLiveCollectionMaintenance();
  void RunAutoArchiveSweep(const SceneDefinition &scene,
                           const LiveWindowKey &window);
  void RestoreArchivedTabsForScene(const SceneDefinition &scene,
                                   const LiveWindowKey &window);
  void ContinueSceneRestore(const LiveWindowKey &window);
  void ScheduleSceneRestoreContinuation(const LiveWindowKey &window,
                                        base::TimeDelta delay);
  void ReconcileScenesWithOrganization();
  bool RestorePresentationForWindow(const LiveWindowKey &window);
  base::DictValue TakePresentationState() const;
  void RestorePresentationState(const base::DictValue &state);
  bool ApplyStandaloneCompactMode(const LiveWindowKey &window,
                                  bool seed_if_missing);
  base::DictValue TakeCompactModeState() const;
  void RestoreCompactModeState(const base::DictValue &state);

  void RegisterBuiltinExecutors();
  AgentPermissionRequest ResolveAgentPermissionRequest(
      const LiveWindowKey &window, const ToolDescriptor &descriptor,
      const base::DictValue &args, bool user_gesture) const;
  void OnWindowBindingClosed(WindowRuntimeBindingToken token,
                             BrowserWindowInterface *browser);
  bool PersistState();
  void SchedulePersist();
  void OnProjectResourcesChanged();
  void LoadState();

  struct WindowBindingRecord {
    // Move-only: holds a base::CallbackListSubscription.
    WindowBindingRecord();
    WindowBindingRecord(WindowBindingRecord &&);
    WindowBindingRecord &operator=(WindowBindingRecord &&);
    ~WindowBindingRecord();

    raw_ptr<BrowserWindowInterface> browser = nullptr;
    LiveWindowKey window;
    std::optional<base::CallbackListSubscription> close_subscription;
  };

  raw_ptr<Profile> profile_;
  raw_ptr<PrefService> prefs_;
  raw_ptr<SeoulOrganizationService> organization_;
  WebContentsResolver web_contents_resolver_;

  // Chromium-facing transports and agent (owned).
  std::unique_ptr<HttpTransport> cloud_transport_;
  std::unique_ptr<HttpTransport> local_transport_;
  std::unique_ptr<CredentialStore> credentials_;
  std::unique_ptr<PageAgent> page_agent_;

  // Runtime-owned appearance catalogs that Scene resolvers reference.
  std::unique_ptr<ThemeRegistry> themes_;
  std::unique_ptr<SiteLayerRegistry> site_layers_;
  std::unique_ptr<LibraryService> library_service_;
  std::unique_ptr<LiveCollectionCoordinator> live_collection_coordinator_;
  std::unique_ptr<PersistenceScheduler> persistence_scheduler_;

  // The pure runtime composition (capability graph, connectors, scenes,
  // routing policy) - this is what instantiates SeoulRuntime.
  SeoulRuntime runtime_;

  // Product services (owned; pure product_core).
  CapabilityExecutorRegistry executors_;
  std::unique_ptr<ProviderRegistry> provider_registry_;
  std::unique_ptr<PreviewManager> preview_manager_;
  std::unique_ptr<PreviewHostService> preview_host_service_;
  std::unique_ptr<RealtimeVoiceAgent> realtime_voice_agent_;
  std::unique_ptr<Planner> planner_;
  std::unique_ptr<AgentPermissionService> agent_permissions_;
  std::unique_ptr<TaskService> task_service_;
  std::unique_ptr<SpeechToTextProvider> speech_to_text_;
  std::unique_ptr<TextToSpeechProvider> text_to_speech_;
  std::unique_ptr<VoiceRuntimeController> voice_controller_;
  std::unique_ptr<SurfaceService> surface_service_;
  // Projects verified task results into surfaces; observes task_service_ and
  // drives surface_service_, so it is constructed after both and destroyed
  // before either.
  std::unique_ptr<TaskSurfaceBridge> task_surface_bridge_;
  std::unique_ptr<ThreadService> thread_service_;
  std::unique_ptr<WorkflowService> workflow_service_;

  std::map<WindowRuntimeBindingToken, WindowBindingRecord> window_bindings_;
  std::map<LiveWindowKey, std::set<LiveTabKey>> live_tabs_by_window_;
  // Scene and Theme activation is window-scoped. The keys are live Chromium
  // window identities and are removed with the window, so stale session ids
  // never leak into a later browser session.
  std::map<LiveWindowKey, std::string> active_scenes_by_window_;
  std::map<LiveWindowKey, std::string> active_themes_by_window_;
  // Entering a Scene temporarily owns Theme and compact-mode policy. Preserve
  // the exact pre-Scene state once, across Scene-to-Scene switches, and
  // restore it when the Scene is cleared.
  struct VerticalTabsBaseline {
    bool collapsed = false;
    bool expand_on_hover = false;
  };
  // Live Chromium window ids are regenerated during session restore. Persist
  // presentation intent against the Scene's durable Workspace identity, then
  // bind it to at most one matching live window in the current process.
  struct WorkspacePresentation {
    WorkspacePresentation();
    WorkspacePresentation(const WorkspacePresentation &);
    WorkspacePresentation(WorkspacePresentation &&);
    WorkspacePresentation &operator=(const WorkspacePresentation &);
    WorkspacePresentation &operator=(WorkspacePresentation &&);
    ~WorkspacePresentation();

    std::string active_scene_id;
    std::string active_theme_id;
    std::string pre_scene_theme_id;
    bool has_vertical_tabs_baseline = false;
    VerticalTabsBaseline vertical_tabs_baseline;
  };
  std::map<LiveWindowKey, std::string> pre_scene_themes_by_window_;
  std::map<LiveWindowKey, VerticalTabsBaseline>
      pre_scene_vertical_tabs_by_window_;
  std::map<WorkspaceId, WorkspacePresentation> presentations_by_workspace_;
  std::map<WorkspaceId, LiveWindowKey> presentation_owners_;
  // Standalone compact preference is independent of Theme/Scene presentation:
  // it survives workspace switches, while a Scene temporarily overrides the
  // native collapse/hover state and later restores it exactly.
  std::map<WorkspaceId, bool> compact_mode_by_workspace_;
  std::map<LiveWindowKey, WorkspaceId> applied_compact_workspace_by_window_;
  bool product_state_loaded_ = false;
  struct PendingSceneRestore {
    PendingSceneRestore();
    ~PendingSceneRestore();

    std::string scene_id;
    WorkspaceId workspace_id;
    std::deque<TabMembershipId> archive_ids;
    CommandId active_command;
    base::TimeTicks deadline;
    base::OneShotTimer retry_timer;
  };
  std::map<LiveWindowKey, std::unique_ptr<PendingSceneRestore>>
      pending_scene_restores_;
  std::map<LiveTabKey, std::unique_ptr<SiteLayerApplicator>>
      site_layer_applicators_;
  base::RepeatingCallbackList<void(const LiveWindowKey &)>
      boost_editor_request_callbacks_;
  std::set<LiveWindowKey> pending_boost_editor_windows_;
  base::RepeatingTimer scene_lifecycle_timer_;
  base::RepeatingTimer live_collection_timer_;
  base::ScopedObservation<LiveWindowStateProvider, LiveWindowStateObserver>
      live_window_observation_{this};
  bool scene_reconciliation_pending_ = false;
  bool shutting_down_ = false;
  base::WeakPtrFactory<SeoulRuntimeService> weak_factory_{this};
};

} // namespace seoul

#endif // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_RUNTIME_SERVICE_H_
