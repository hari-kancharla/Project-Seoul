// Project Seoul product runtime - browser and page capability executors.
// Concrete CapabilityExecutors that drive the confirmed outbound command
// layer (CommandExecutor + mutation adapter) and the browser-owned page agent.
// Each executor owns one capability id: it decodes args, submits the typed
// browser command or page action, observes the real lifecycle confirmation,
// verifies the outcome, and reports a receipt. There is no central action
// switch; the runtime registers one executor per capability descriptor.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_BROWSER_CAPABILITIES_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_BROWSER_CAPABILITIES_H_

#include <map>
#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "seoul/browser/commands/command_completion_observer.h"
#include "seoul/browser/commands/command_id.h"
#include "seoul/browser/commands/command_types.h"
#include "seoul/browser/organization/organization_types.h"
#include "seoul/browser/product/browser/page_agent.h"
#include "seoul/browser/product/capability_executor.h"
#include "seoul/browser/scenes/scene_types.h"
#include "url/gurl.h"

namespace seoul {

class CommandExecutor;
class OrganizationModel;
class LiveWindowStateProvider;
class PreviewHostService;

using LinkRoutingResolver = base::RepeatingCallback<RoutingResolution(
    const LiveWindowKey &window, const GURL &destination, bool user_gesture,
    RoutingDisposition requested_disposition)>;
using SceneActivationHandler = base::RepeatingCallback<SceneStatusResult(
    const std::string &, const LiveWindowKey &)>;
using CompactModeRequestHandler =
    base::RepeatingCallback<bool(bool, const LiveWindowKey &)>;
using CompactModeStateHandler =
    base::RepeatingCallback<std::optional<bool>(const LiveWindowKey &)>;
using CompactModeAppliedHandler =
    base::RepeatingCallback<bool(bool, const LiveWindowKey &)>;

// Executes a single browser-mutating capability by submitting a typed
// BrowserCommand and resolving when the lifecycle bridge confirms it.
class BrowserCommandExecutor : public CapabilityExecutor,
                               public CommandCompletionObserver {
public:
  BrowserCommandExecutor(const std::string &capability_id, CommandKind kind,
                         CommandExecutor *commands,
                         LiveWindowStateProvider *live_state = nullptr,
                         PreviewHostService *preview_host = nullptr,
                         LinkRoutingResolver link_router = {},
                         OrganizationModel *model = nullptr,
                         WebContentsResolver web_contents_resolver = {});
  ~BrowserCommandExecutor() override;

  // CapabilityExecutor:
  ToolId capability_id() const override;
  void Execute(CapabilityRequest request, CapabilityCallback callback) override;
  void Cancel(const TaskId &task_id, const std::string &step_id) override;

  // CommandCompletionObserver:
  void OnCommandCompleted(CommandId id, CommandKind kind,
                          CommandStatus status) override;

private:
  struct Pending {
    // Move-only: holds a base::OnceCallback.
    Pending();
    Pending(Pending &&);
    Pending &operator=(Pending &&);
    ~Pending();

    CapabilityCallback callback;
    TaskId task_id;
    std::string step_id;
  };

  const ToolId id_;
  const CommandKind kind_;
  raw_ptr<CommandExecutor> commands_;
  raw_ptr<LiveWindowStateProvider> live_state_;
  raw_ptr<PreviewHostService> preview_host_;
  LinkRoutingResolver link_router_;
  raw_ptr<OrganizationModel> model_;
  WebContentsResolver web_contents_resolver_;
  std::map<CommandId, Pending> pending_;
  bool observing_ = false;
};

// Reads the live tabs of the request window into an entity-collection
// SemanticResult. Read-only; resolves synchronously from the current snapshot.
class EnumerateTabsExecutor : public CapabilityExecutor {
public:
  EnumerateTabsExecutor(OrganizationModel *model,
                        LiveWindowStateProvider *live_state);
  ~EnumerateTabsExecutor() override;

  ToolId capability_id() const override;
  void Execute(CapabilityRequest request, CapabilityCallback callback) override;

private:
  raw_ptr<OrganizationModel> model_;
  raw_ptr<LiveWindowStateProvider> live_state_;
};

class PreviewOpenExecutor : public CapabilityExecutor {
public:
  explicit PreviewOpenExecutor(PreviewHostService *preview_host);
  ~PreviewOpenExecutor() override;

  ToolId capability_id() const override;
  void Execute(CapabilityRequest request, CapabilityCallback callback) override;

private:
  raw_ptr<PreviewHostService> preview_host_;
};

// Applies a validated Scene through the profile runtime. This remains a
// capability executor rather than bypassing the runtime so voice, workflows,
// and typed Canvas turns share the same activation behavior and receipt.
class SceneActivateExecutor : public CapabilityExecutor {
public:
  explicit SceneActivateExecutor(SceneActivationHandler activate);
  ~SceneActivateExecutor() override;

  ToolId capability_id() const override;
  void Execute(CapabilityRequest request, CapabilityCallback callback) override;

private:
  SceneActivationHandler activate_;
};

// Sets standalone compact browser chrome and completes only after the native
// vertical-tab controller reaches the requested collapse/hover postcondition.
class CompactModeExecutor : public CapabilityExecutor {
public:
  CompactModeExecutor(CompactModeRequestHandler request,
                      CompactModeStateHandler state,
                      CompactModeAppliedHandler applied);
  ~CompactModeExecutor() override;

  ToolId capability_id() const override;
  void Execute(CapabilityRequest request, CapabilityCallback callback) override;
  void Cancel(const TaskId &task_id, const std::string &step_id) override;

private:
  using PendingKey = std::pair<TaskId, std::string>;
  struct Pending {
    Pending();
    ~Pending();

    CapabilityCallback callback;
    LiveWindowKey window;
    bool enabled = false;
    base::TimeTicks deadline;
    base::RepeatingTimer timer;
  };

  void Poll(PendingKey key);
  void Complete(PendingKey key, StepStatus status, const std::string &summary,
                bool verified);

  CompactModeRequestHandler request_;
  CompactModeStateHandler state_;
  CompactModeAppliedHandler applied_;
  std::map<PendingKey, std::unique_ptr<Pending>> pending_;
  base::WeakPtrFactory<CompactModeExecutor> weak_factory_{this};
};

// Observes the active tab of the request window through the page agent and
// returns a record SemanticResult of visible semantic elements.
class PageObserveExecutor : public CapabilityExecutor {
public:
  PageObserveExecutor(PageAgent *page_agent,
                      LiveWindowStateProvider *live_state);
  ~PageObserveExecutor() override;

  ToolId capability_id() const override;
  void Execute(CapabilityRequest request, CapabilityCallback callback) override;

private:
  void OnObserved(CapabilityCallback callback,
                  std::optional<PageObservation> observation);

  raw_ptr<PageAgent> page_agent_;
  raw_ptr<LiveWindowStateProvider> live_state_;
  base::WeakPtrFactory<PageObserveExecutor> weak_factory_{this};
};

// Extracts a caller-declared subset of the bounded page observation fields
// into a canonical entity collection. Unsupported field ids/types fail rather
// than being guessed or fabricated.
class PageExtractStructuredExecutor : public CapabilityExecutor {
public:
  PageExtractStructuredExecutor(PageAgent *page_agent,
                                LiveWindowStateProvider *live_state);
  ~PageExtractStructuredExecutor() override;

  ToolId capability_id() const override;
  void Execute(CapabilityRequest request, CapabilityCallback callback) override;

private:
  void OnObserved(SemanticSchema schema, CapabilityCallback callback,
                  std::optional<PageObservation> observation);

  raw_ptr<PageAgent> page_agent_;
  raw_ptr<LiveWindowStateProvider> live_state_;
  base::WeakPtrFactory<PageExtractStructuredExecutor> weak_factory_{this};
};

// Performs one typed page action (click/type) on the active tab via the page
// agent. The action targets an element handle from a prior observation.
class PageActionExecutor : public CapabilityExecutor {
public:
  PageActionExecutor(const std::string &capability_id, PageActionKind action,
                     PageAgent *page_agent,
                     LiveWindowStateProvider *live_state);
  ~PageActionExecutor() override;

  ToolId capability_id() const override;
  void Execute(CapabilityRequest request, CapabilityCallback callback) override;
  void Cancel(const TaskId &task_id, const std::string &step_id) override;

private:
  void OnVerified(TaskId task_id, std::string step_id, PageActionStatus status);

  const ToolId id_;
  const PageActionKind action_;
  raw_ptr<PageAgent> page_agent_;
  raw_ptr<LiveWindowStateProvider> live_state_;
  std::map<std::pair<TaskId, std::string>, CapabilityCallback> pending_;
  base::WeakPtrFactory<PageActionExecutor> weak_factory_{this};
};

} // namespace seoul

#endif // SEOUL_BROWSER_PRODUCT_BROWSER_BROWSER_CAPABILITIES_H_
