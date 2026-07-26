// Project Seoul Canvas - Mojo page handler (browser side).
//
// STATE OWNERSHIP (product-wide state rules):
//   owner:        the SeoulCanvasUI that created it (one per Canvas WebUI).
//   lifetime:     the WebUI document; destroyed on teardown/reconnect.
//   persistence:  per-window ephemeral (the Mojo remote and observer
//                 registrations); no durable state of its own.
//   recovery:     the renderer re-requests initial state on reconnect.
//   teardown:     receiver/remote reset and observers removed in the
//                 destructor; no observer leak, no callback after teardown.
//   bounds:       events are bounded on receipt; surfaces come pre-validated.
//   isolation:    one profile.
//
// It observes the profile runtime's surface and task services and pushes
// validated SAUI surfaces and status to the renderer; it validates typed
// component events and turns and routes them into the runtime (planner/task/
// surface). It never sends arbitrary HTML to the renderer and never receives
// DOM or coordinates from it.

#ifndef SEOUL_BROWSER_CANVAS_SEOUL_CANVAS_PAGE_HANDLER_H_
#define SEOUL_BROWSER_CANVAS_SEOUL_CANVAS_PAGE_HANDLER_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/unguessable_token.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "seoul/browser/canvas/canvas.mojom.h"
#include "seoul/browser/library/library_service.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/lifecycle/live_window_state.h"
#include "seoul/browser/product/realtime_voice_agent.h"
#include "seoul/browser/product/surface_service.h"
#include "seoul/browser/product/task_service.h"
#include "seoul/browser/tasks/task_types.h"

class Profile;
class BrowserWindowInterface;

namespace seoul {

class SeoulRuntimeService;

class SeoulCanvasPageHandler : public canvas::mojom::PageHandler,
                               public SurfaceServiceObserver,
                               public TaskServiceObserver,
                               public LiveWindowStateObserver,
                               public LibraryServiceObserver {
public:
  SeoulCanvasPageHandler(
      mojo::PendingReceiver<canvas::mojom::PageHandler> receiver,
      mojo::PendingRemote<canvas::mojom::Page> page, Profile *profile,
      BrowserWindowInterface *browser_window);
  SeoulCanvasPageHandler(const SeoulCanvasPageHandler &) = delete;
  SeoulCanvasPageHandler &operator=(const SeoulCanvasPageHandler &) = delete;
  ~SeoulCanvasPageHandler() override;

  // canvas::mojom::PageHandler:
  void RequestInitialState() override;
  void NotifyComponentEvent(canvas::mojom::ComponentEventPtr event) override;
  void SubmitTurn(canvas::mojom::TurnInputPtr input) override;
  void GetThreadSnapshot(const std::string& thread_id,
                         GetThreadSnapshotCallback callback) override;
  void StartVoice() override;
  void StopVoice() override;
  void CreateRealtimeVoiceSession(
      CreateRealtimeVoiceSessionCallback callback) override;
  void SubmitRealtimeToolCall(canvas::mojom::RealtimeToolCallPtr call,
                              SubmitRealtimeToolCallCallback callback) override;
  void ListTasks(ListTasksCallback callback) override;
  void PauseTask(const std::string &task_id) override;
  void ResumeTask(const std::string &task_id) override;
  void CancelActiveTask(const std::string &task_id) override;
  void ApproveStep(const std::string &task_id, const std::string &step_id,
                   bool approved) override;
  void ProvideTaskInput(const std::string &task_id, const std::string &step_id,
                        const std::string &input) override;
  void ListTaskSurfaces(const std::string &task_id,
                        ListTaskSurfacesCallback callback) override;
  void SaveTaskAsWorkflow(const std::string &task_id, const std::string &name,
                          SaveTaskAsWorkflowCallback callback) override;
  void GetLibrarySnapshot(GetLibrarySnapshotCallback callback) override;
  void CreateBoard(const std::string &name,
                   CreateBoardCallback callback) override;
  void RenameBoard(const std::string &board_id, const std::string &name,
                   RenameBoardCallback callback) override;
  void SetBoardArchived(const std::string &board_id, bool archived,
                        SetBoardArchivedCallback callback) override;
  void DeleteBoard(const std::string &board_id,
                   DeleteBoardCallback callback) override;
  void AddBoardElement(const std::string &board_id,
                       const std::string &element_id, const std::string &kind,
                       const std::string &title, const std::string &text,
                       const std::string &reference, const std::string &origin,
                       double x, double y, double width, double height,
                       int32_t z_index,
                       AddBoardElementCallback callback) override;
  void UpdateBoardElement(const std::string &board_id,
                          const std::string &element_id,
                          const std::string &kind, const std::string &title,
                          const std::string &text, const std::string &reference,
                          const std::string &origin, double x, double y,
                          double width, double height, int32_t z_index,
                          UpdateBoardElementCallback callback) override;
  void RemoveBoardElement(const std::string &board_id,
                          const std::string &element_id,
                          RemoveBoardElementCallback callback) override;
  void UpsertLiveCollection(
      const std::string &collection_id, const std::string &name,
      const std::string &refresh_capability,
      const std::string &source_locator, int32_t refresh_interval_minutes,
      bool enabled, UpsertLiveCollectionCallback callback) override;
  void SetLiveCollectionEnabled(
      const std::string &collection_id, bool enabled,
      SetLiveCollectionEnabledCallback callback) override;
  void RefreshLiveCollection(
      const std::string &collection_id,
      RefreshLiveCollectionCallback callback) override;
  void DeleteLiveCollection(
      const std::string &collection_id,
      DeleteLiveCollectionCallback callback) override;
  void OpenLiveCollectionItem(
      const std::string &collection_id, const std::string &stable_key,
      OpenLiveCollectionItemCallback callback) override;
  void GetSiteLayerSnapshot(GetSiteLayerSnapshotCallback callback) override;
  void UpsertSiteLayer(
      const std::string &layer_id, const std::string &name,
      const std::string &origin_pattern, const std::string &scene_scope,
      bool enabled,
      std::vector<canvas::mojom::SiteLayerAdjustmentInputPtr> adjustments,
      UpsertSiteLayerCallback callback) override;
  void SetSiteLayerEnabled(const std::string &layer_id, bool enabled,
                           SetSiteLayerEnabledCallback callback) override;
  void DeleteSiteLayer(const std::string &layer_id,
                       DeleteSiteLayerCallback callback) override;
  void ZapSiteLayer(const std::string &layer_id,
                    ZapSiteLayerCallback callback) override;
  void CancelSiteLayerZap() override;
  void GetStudioSnapshot(GetStudioSnapshotCallback callback) override;
  void SaveLocalProvider(const std::string &endpoint_url,
                         const std::string &model_id,
                         SaveLocalProviderCallback callback) override;
  void ClearLocalProvider(ClearLocalProviderCallback callback) override;
  void CheckLocalProvider(CheckLocalProviderCallback callback) override;
  void SaveCloudProvider(const std::string &model_id, bool enabled,
                         const std::string &reasoning_secret,
                         const std::string &voice_secret,
                         SaveCloudProviderCallback callback) override;
  void ClearCloudProvider(ClearCloudProviderCallback callback) override;
  void UpsertEssential(const std::string &essential_id,
                       const std::string &name,
                       const std::string &root_url,
                       UpsertEssentialCallback callback) override;
  void DeleteEssential(const std::string &essential_id,
                       DeleteEssentialCallback callback) override;
  void UpsertTheme(canvas::mojom::StudioThemeInputPtr theme,
                   UpsertThemeCallback callback) override;
  void DeleteTheme(const std::string &theme_id,
                   DeleteThemeCallback callback) override;
  void ActivateTheme(const std::string &theme_id,
                     ActivateThemeCallback callback) override;
  void UpsertScene(canvas::mojom::StudioSceneInputPtr scene,
                   UpsertSceneCallback callback) override;
  void DeleteScene(const std::string &scene_id,
                   DeleteSceneCallback callback) override;
  void ActivateScene(const std::string &scene_id,
                     ActivateSceneCallback callback) override;
  void UpsertRoutingRule(canvas::mojom::StudioRoutingRuleInputPtr rule,
                         UpsertRoutingRuleCallback callback) override;
  void DeleteRoutingRule(const std::string &rule_id,
                         DeleteRoutingRuleCallback callback) override;
  void UpsertWorkflow(const std::string &workflow_json,
                      UpsertWorkflowCallback callback) override;
  void DeleteWorkflow(const std::string &workflow_id,
                      DeleteWorkflowCallback callback) override;
  void DuplicateWorkflow(const std::string &workflow_id,
                         DuplicateWorkflowCallback callback) override;
  void RunWorkflow(const std::string &workflow_id,
                   RunWorkflowCallback callback) override;

  // SurfaceServiceObserver:
  void OnSurfaceUpdated(const SurfaceId &id,
                        const std::string &surface_json) override;
  void OnSurfaceRemoved(const SurfaceId &id) override;

  // TaskServiceObserver:
  void OnTaskUpdated(const TaskId &task_id) override;
  void OnTaskNeedsApproval(const TaskId &task_id, const std::string &step_id,
                           const std::string &prompt) override;
  void OnTaskFinished(const TaskId &task_id) override;

  // LiveWindowStateObserver:
  void OnLiveWindowSnapshotChanged(const LiveWindowSnapshot &snapshot) override;
  void OnLiveWindowRemoved(LiveWindowKey window) override;

  // LibraryServiceObserver:
  void OnLibraryChanged(uint64_t revision) override;

private:
  // Pushes the canonical task-snapshot JSON for `task_id` when it belongs to
  // the bound window; other windows' tasks never reach this Canvas.
  void PushTaskSnapshot(const TaskId &task_id);
  // The task's snapshot iff it exists and is bound to this Canvas's window.
  std::optional<TaskSnapshot> BoundTask(const std::string &task_id) const;
  // Applies one typed workflow edit from a surface action payload; false is
  // reported to the renderer as workflow_edit_rejected.
  bool ApplyWorkflowEdit(const std::string &node_id,
                         const base::DictValue &payload);
  void
  OnRealtimeVoiceSessionCreated(CreateRealtimeVoiceSessionCallback callback,
                                RealtimeVoiceAgent::CreateSessionResult result);
  void OnLocalProviderChecked(CheckLocalProviderCallback callback,
                              bool healthy);

  // Pushes a compact status document (provider/voice/task state) to the
  // renderer so it never shows a blank page when the runtime is initializing.
  void PushStatus(const std::string &detail);
  void PushPageContext();
  void OnBoostEditorRequested(const LiveWindowKey &window);
  std::optional<LiveWindowKey> ResolveBoundWindow() const;
  TaskId StartBoundGoal(const std::string &goal);
  std::string LibrarySnapshotJson() const;
  std::string ThreadSnapshotJson(const std::string& thread_id) const;
  void PushThreadSnapshot(const std::string& thread_id);
  std::string SiteLayerSnapshotJson() const;
  std::string StudioSnapshotJson() const;

  mojo::Receiver<canvas::mojom::PageHandler> receiver_;
  mojo::Remote<canvas::mojom::Page> page_;
  raw_ptr<Profile> profile_;
  raw_ptr<SeoulRuntimeService> runtime_; // null when the profile is ineligible
  base::UnguessableToken window_binding_token_;
  bool observing_ = false;
  base::ScopedObservation<LiveWindowStateProvider, LiveWindowStateObserver>
      live_window_observation_{this};
  base::ScopedObservation<LibraryService, LibraryServiceObserver>
      library_observation_{this};
  base::CallbackListSubscription boost_editor_request_subscription_;
  std::map<std::string, std::string> task_threads_;
  base::WeakPtrFactory<SeoulCanvasPageHandler> weak_factory_{this};
};

} // namespace seoul

#endif // SEOUL_BROWSER_CANVAS_SEOUL_CANVAS_PAGE_HANDLER_H_
