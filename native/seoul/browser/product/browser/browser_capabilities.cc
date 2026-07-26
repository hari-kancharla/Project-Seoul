// Project Seoul product runtime - browser and page capability executors.

#include "seoul/browser/product/browser/browser_capabilities.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "content/public/browser/web_contents.h"
#include "seoul/browser/commands/browser_command.h"
#include "seoul/browser/commands/command_executor.h"
#include "seoul/browser/lifecycle/live_window_state.h"
#include "seoul/browser/organization/organization_limits.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/preview/preview_host_service.h"
#include "seoul/browser/semantic/semantic_validation.h"
#include "seoul/browser/semantic/semantic_wire.h"
#include "url/gurl.h"

namespace seoul {

namespace {

void SetProvenance(SemanticResult *result, const std::string &source) {
  result->provenance.base.source_name = source;
  result->provenance.base.retrieved_at = base::Time::Now();
  result->provenance.base.effective_at = result->provenance.base.retrieved_at;
  result->provenance.base.freshness = FreshnessState::kRealTime;
  result->provenance.provider = "seoul";
}

FieldSpec Field(const std::string &id, FieldPrimitive primitive,
                SemanticRole role) {
  FieldSpec field;
  field.id = id;
  field.label = id;
  field.primitive = primitive;
  field.role = role;
  field.nullable = false;
  return field;
}

CapabilityOutcome FailOutcome(const std::string &summary) {
  CapabilityOutcome outcome;
  outcome.step.status = StepStatus::kFailed;
  outcome.step.observed_summary = summary;
  return outcome;
}

} // namespace

// --- BrowserCommandExecutor -------------------------------------------------

BrowserCommandExecutor::BrowserCommandExecutor(
    const std::string &capability_id, CommandKind kind,
    CommandExecutor *commands, LiveWindowStateProvider *live_state,
    PreviewHostService *preview_host, LinkRoutingResolver link_router,
    OrganizationModel *model, WebContentsResolver web_contents_resolver)
    : id_(ToolId::FromString(capability_id)), kind_(kind), commands_(commands),
      live_state_(live_state), preview_host_(preview_host),
      link_router_(std::move(link_router)), model_(model),
      web_contents_resolver_(std::move(web_contents_resolver)) {}

BrowserCommandExecutor::~BrowserCommandExecutor() {
  if (observing_ && commands_) {
    commands_->RemoveCompletionObserver(this);
  }
}

ToolId BrowserCommandExecutor::capability_id() const { return id_; }

void BrowserCommandExecutor::Execute(CapabilityRequest request,
                                     CapabilityCallback callback) {
  if (!commands_) {
    std::move(callback).Run(FailOutcome("Command layer unavailable."));
    return;
  }
  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = kind_;
  command.window = request.window;

  switch (kind_) {
  case CommandKind::kOpenTemporaryTab:
  case CommandKind::kOpenRetainedTab: {
    const std::string *url = request.args.FindString("url");
    const GURL parsed(url ? *url : std::string());
    if (!parsed.is_valid() || !parsed.SchemeIsHTTPOrHTTPS()) {
      std::move(callback).Run(FailOutcome("A valid http(s) url is required."));
      return;
    }
    command.url = parsed;
    const bool retained = request.args.FindBool("retained")
                              .value_or(kind_ == CommandKind::kOpenRetainedTab);
    const RoutingDisposition requested =
        retained ? RoutingDisposition::kNewRetainedTab
                 : RoutingDisposition::kNewTemporaryTab;
    RoutingResolution routing;
    routing.result.disposition = requested;
    routing.used_fallback = true;
    if (link_router_) {
      routing = link_router_.Run(request.window, parsed, request.user_gesture,
                                 requested);
    }
    RoutingDisposition disposition = routing.result.disposition;
    // kAskUser is approval-gated dynamically by the runtime. Reaching the
    // executor means the user approved this exact step, so continue using
    // the step's requested retained/temporary disposition.
    if (disposition == RoutingDisposition::kAskUser) {
      disposition = requested;
    }
    if (disposition == RoutingDisposition::kPreview) {
      const std::optional<LiveWindowSnapshot> snapshot =
          live_state_ ? live_state_->GetSnapshot(request.window) : std::nullopt;
      if (!preview_host_ || !snapshot.has_value() ||
          !snapshot->active_tab.is_valid()) {
        std::move(callback).Run(
            FailOutcome("No active tab is available for the routed preview."));
        return;
      }
      PreviewResult<PreviewId> opened =
          preview_host_->Open(request.window, snapshot->active_tab, parsed);
      if (!opened.has_value()) {
        std::move(callback).Run(
            FailOutcome(std::string("Routed preview rejected: ") +
                        PreviewErrorToString(opened.error())));
        return;
      }
      CapabilityOutcome outcome;
      outcome.step.status = StepStatus::kSucceeded;
      outcome.step.observed_summary =
          "Routing rule opened an ephemeral link preview.";
      outcome.step.verification.verified = true;
      outcome.step.verification.method = "routing_preview_postcondition";
      std::move(callback).Run(std::move(outcome));
      return;
    }
    if (disposition == RoutingDisposition::kSpecificWorkspace) {
      BrowserCommand switch_workspace;
      switch_workspace.id = CommandId::Next();
      switch_workspace.kind = CommandKind::kSetActiveWorkspace;
      switch_workspace.window = request.window;
      switch_workspace.workspace_id = routing.result.target_workspace;
      const CommandResult<CommandStatus> switched =
          commands_->Submit(std::move(switch_workspace));
      if (!switched.has_value() ||
          switched.value() != CommandStatus::kApplied) {
        std::move(callback).Run(
            FailOutcome("The routing target workspace is unavailable."));
        return;
      }
      command.kind = CommandKind::kOpenRetainedTab;
    } else if (disposition == RoutingDisposition::kCurrentTab ||
               disposition == RoutingDisposition::kSplitPane) {
      const std::optional<LiveWindowSnapshot> snapshot =
          live_state_ ? live_state_->GetSnapshot(request.window) : std::nullopt;
      if (!snapshot.has_value() || !snapshot->active_tab.is_valid()) {
        std::move(callback).Run(
            FailOutcome("No active tab is available for this route."));
        return;
      }
      command.tab = snapshot->active_tab;
      command.kind = disposition == RoutingDisposition::kCurrentTab
                         ? CommandKind::kNavigateTab
                         : CommandKind::kOpenSplitTab;
    } else if (disposition == RoutingDisposition::kExternalApplication) {
      command.kind = CommandKind::kOpenExternal;
    } else {
      command.kind = disposition == RoutingDisposition::kNewRetainedTab
                         ? CommandKind::kOpenRetainedTab
                         : CommandKind::kOpenTemporaryTab;
    }
    break;
  }
  case CommandKind::kRestoreArchivedTab: {
    const std::string *archive_id = request.args.FindString("archive_id");
    const TabMembershipId original =
        archive_id ? TabMembershipId::FromString(*archive_id)
                   : TabMembershipId();
    const ArchivedTabRecord *archived = original.is_valid() && model_
                                            ? model_->FindArchivedTab(original)
                                            : nullptr;
    const GURL recovery_url(archived ? archived->saved_root_url
                                     : std::string());
    const WorkspaceRecord *workspace =
        archived && model_ ? model_->FindWorkspace(archived->workspace_id)
                           : nullptr;
    if (!archived || !workspace || workspace->archived ||
        !recovery_url.is_valid() || !recovery_url.SchemeIsHTTPOrHTTPS()) {
      std::move(callback).Run(
          FailOutcome("The archived tab is missing or cannot be recovered."));
      return;
    }
    BrowserCommand switch_workspace;
    switch_workspace.id = CommandId::Next();
    switch_workspace.kind = CommandKind::kSetActiveWorkspace;
    switch_workspace.window = request.window;
    switch_workspace.workspace_id = archived->workspace_id;
    const CommandResult<CommandStatus> switched =
        commands_->Submit(std::move(switch_workspace));
    if (!switched.has_value() || switched.value() != CommandStatus::kApplied) {
      std::move(callback).Run(
          FailOutcome("The archived tab's workspace is unavailable."));
      return;
    }
    command.membership_id = original;
    command.workspace_id = archived->workspace_id;
    command.url = recovery_url;
    break;
  }
  case CommandKind::kActivateTab:
  case CommandKind::kCloseTab:
  case CommandKind::kArchiveTab: {
    // Field name must match the capability descriptor's schema exactly
    // (generic_capabilities.cc declares "tab_key"); a mismatch makes the
    // capability unrunnable because ValidatePlan rejects unknown fields.
    const std::string *tab = request.args.FindString("tab_key");
    if (!tab || tab->empty()) {
      std::move(callback).Run(FailOutcome("A tab reference is required."));
      return;
    }
    command.tab = LiveTabKey::Parse(*tab);
    if (!command.tab.is_valid()) {
      std::move(callback).Run(FailOutcome("The tab reference is invalid."));
      return;
    }
    if (kind_ == CommandKind::kArchiveTab) {
      const TabMembershipId membership =
          model_ ? model_->FindMembershipIdByTabKey(command.tab.value())
                 : TabMembershipId();
      const TabMembershipRecord *record =
          membership.is_valid() ? model_->FindMembership(membership) : nullptr;
      const std::vector<TabMembershipId> eligible =
          model_ ? model_->EligibleForAutoArchive({}, base::Time::Now(),
                                                  base::TimeDelta())
                 : std::vector<TabMembershipId>();
      content::WebContents *contents =
          web_contents_resolver_ ? web_contents_resolver_.Run(command.tab)
                                 : nullptr;
      if (!record || record->role != TabRole::kTemporary ||
          std::ranges::find(eligible, membership) == eligible.end() ||
          !contents || contents->IsBeingDestroyed() || contents->IsLoading() ||
          contents->IsCurrentlyAudible()) {
        std::move(callback).Run(FailOutcome(
            "The tab is protected, busy, split, or no longer available."));
        return;
      }
      const GURL recovery_url = contents->GetLastCommittedURL();
      if (!recovery_url.is_valid() || !recovery_url.SchemeIsHTTPOrHTTPS()) {
        std::move(callback).Run(
            FailOutcome("The tab does not have a recoverable web URL."));
        return;
      }
      command.membership_id = membership;
      command.url = recovery_url;
      command.name = base::TruncateUTF8ToByteSize(
          base::UTF16ToUTF8(contents->GetTitle()), kMaxNameLength);
    }
    break;
  }
  case CommandKind::kSetActiveWorkspace: {
    const std::string *workspace = request.args.FindString("workspace_id");
    if (!workspace || workspace->empty()) {
      std::move(callback).Run(FailOutcome("A workspace id is required."));
      return;
    }
    command.workspace_id = WorkspaceId::FromString(*workspace);
    break;
  }
  case CommandKind::kCreateSplit: {
    const std::string *first = request.args.FindString("first_tab_key");
    const std::string *second = request.args.FindString("second_tab_key");
    command.tab = first ? LiveTabKey::Parse(*first) : LiveTabKey();
    command.split_pane_b = second ? LiveTabKey::Parse(*second) : LiveTabKey();
    if (!command.tab.is_valid() || !command.split_pane_b.is_valid() ||
        command.tab == command.split_pane_b) {
      std::move(callback).Run(
          FailOutcome("Two different live tab references are required."));
      return;
    }
    command.split_ratio = 0.5;
    break;
  }
  case CommandKind::kOpenNewTab:
    break; // no payload
  default:
    std::move(callback).Run(FailOutcome("Unsupported browser command."));
    return;
  }

  if (!observing_) {
    commands_->AddCompletionObserver(this);
    observing_ = true;
  }
  Pending pending;
  pending.callback = std::move(callback);
  pending.task_id = request.task_id;
  pending.step_id = request.step_id;
  const CommandId command_id = command.id;
  // Chromium mutations may publish their confirming lifecycle event
  // synchronously inside Submit(). Register the callback first so that
  // OnCommandCompleted cannot race ahead and strand the task forever.
  pending_[command_id] = std::move(pending);

  const CommandResult<CommandStatus> submitted =
      commands_->Submit(std::move(command));
  if (!submitted.has_value()) {
    auto waiting = pending_.find(command_id);
    if (waiting != pending_.end()) {
      CapabilityCallback rejected = std::move(waiting->second.callback);
      pending_.erase(waiting);
      std::move(rejected).Run(FailOutcome("The command was rejected."));
    }
    return;
  }
  if (submitted.value() == CommandStatus::kApplied) {
    auto waiting = pending_.find(command_id);
    if (waiting == pending_.end()) {
      return;
    }
    CapabilityCallback applied = std::move(waiting->second.callback);
    pending_.erase(waiting);
    // Model-only fast path: already confirmed, no observation to await.
    CapabilityOutcome outcome;
    outcome.step.status = StepStatus::kSucceeded;
    outcome.step.observed_summary = "Applied.";
    outcome.step.verification.verified = true;
    outcome.step.verification.method = "postcondition";
    std::move(applied).Run(std::move(outcome));
    return;
  }
}

void BrowserCommandExecutor::Cancel(const TaskId &task_id,
                                    const std::string &step_id) {
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (it->second.task_id == task_id && it->second.step_id == step_id) {
      CapabilityOutcome outcome;
      outcome.step.status = StepStatus::kOutcomeUnknown;
      outcome.step.observed_summary = "Cancelled before confirmation.";
      std::move(it->second.callback).Run(std::move(outcome));
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
}

void BrowserCommandExecutor::OnCommandCompleted(CommandId id, CommandKind kind,
                                                CommandStatus status) {
  auto it = pending_.find(id);
  if (it == pending_.end()) {
    return;
  }
  Pending pending = std::move(it->second);
  pending_.erase(it);

  CapabilityOutcome outcome;
  switch (status) {
  case CommandStatus::kApplied:
  case CommandStatus::kAppliedWithOrganizationRepairRequired:
    outcome.step.status = StepStatus::kSucceeded;
    outcome.step.observed_summary = "Confirmed by the lifecycle bridge.";
    outcome.step.verification.verified = true;
    outcome.step.verification.method = "observation";
    break;
  case CommandStatus::kRejected:
    outcome.step.status = StepStatus::kFailed;
    outcome.step.observed_summary = "Rejected during dispatch.";
    break;
  case CommandStatus::kCancelled:
    outcome.step.status = StepStatus::kCancelled;
    outcome.step.observed_summary = "Cancelled.";
    break;
  case CommandStatus::kOutcomeUnknown:
    outcome.step.status = StepStatus::kOutcomeUnknown;
    outcome.step.observed_summary = "No confirmation observed.";
    break;
  default:
    outcome.step.status = StepStatus::kOutcomeUnknown;
    outcome.step.observed_summary = "Indeterminate command status.";
    break;
  }
  std::move(pending.callback).Run(std::move(outcome));
}

// --- PreviewOpenExecutor ----------------------------------------------------

PreviewOpenExecutor::PreviewOpenExecutor(PreviewHostService *preview_host)
    : preview_host_(preview_host) {}

PreviewOpenExecutor::~PreviewOpenExecutor() = default;

ToolId PreviewOpenExecutor::capability_id() const {
  return ToolId::FromString("browser.preview.open");
}

void PreviewOpenExecutor::Execute(CapabilityRequest request,
                                  CapabilityCallback callback) {
  const std::string *url = request.args.FindString("url");
  const std::string *tab = request.args.FindString("tab_key");
  const GURL destination(url ? *url : std::string());
  const LiveTabKey parent = tab ? LiveTabKey::Parse(*tab) : LiveTabKey();
  if (!preview_host_ || !request.window.is_valid() || !parent.is_valid() ||
      !destination.is_valid() || !destination.SchemeIsHTTPOrHTTPS()) {
    std::move(callback).Run(
        FailOutcome("An exact parent tab and valid http(s) URL are required."));
    return;
  }
  PreviewResult<PreviewId> opened =
      preview_host_->Open(request.window, parent, destination);
  if (!opened.has_value()) {
    std::move(callback).Run(FailOutcome(std::string("Preview rejected: ") +
                                        PreviewErrorToString(opened.error())));
    return;
  }
  CapabilityOutcome outcome;
  outcome.step.status = StepStatus::kSucceeded;
  outcome.step.observed_summary = "Opened an ephemeral link preview.";
  outcome.step.verification.verified = true;
  outcome.step.verification.method = "preview_host_postcondition";
  std::move(callback).Run(std::move(outcome));
}

// --- SceneActivateExecutor -------------------------------------------------

SceneActivateExecutor::SceneActivateExecutor(SceneActivationHandler activate)
    : activate_(std::move(activate)) {}

SceneActivateExecutor::~SceneActivateExecutor() = default;

ToolId SceneActivateExecutor::capability_id() const {
  return ToolId::FromString("scene.activate");
}

void SceneActivateExecutor::Execute(CapabilityRequest request,
                                    CapabilityCallback callback) {
  const std::string *scene_id = request.args.FindString("scene_id");
  if (!activate_ || !scene_id || scene_id->empty()) {
    std::move(callback).Run(FailOutcome("A valid Scene id is required."));
    return;
  }
  const SceneStatusResult activated = activate_.Run(*scene_id, request.window);
  if (!activated.has_value()) {
    std::move(callback).Run(
        FailOutcome(std::string("Scene activation failed: ") +
                    SceneErrorToString(activated.error())));
    return;
  }
  CapabilityOutcome outcome;
  outcome.step.status = StepStatus::kSucceeded;
  outcome.step.observed_summary =
      "Scene workspace, appearance, routing, lifecycle, and assistant policy "
      "were applied.";
  outcome.step.verification.verified = true;
  outcome.step.verification.method = "scene_runtime_postcondition";
  std::move(callback).Run(std::move(outcome));
}

// --- CompactModeExecutor ---------------------------------------------------

CompactModeExecutor::Pending::Pending() = default;
CompactModeExecutor::Pending::~Pending() = default;

CompactModeExecutor::CompactModeExecutor(CompactModeRequestHandler request,
                                         CompactModeStateHandler state,
                                         CompactModeAppliedHandler applied)
    : request_(std::move(request)), state_(std::move(state)),
      applied_(std::move(applied)) {}

CompactModeExecutor::~CompactModeExecutor() = default;

ToolId CompactModeExecutor::capability_id() const {
  return ToolId::FromString("browser.compact.set");
}

void CompactModeExecutor::Execute(CapabilityRequest request,
                                  CapabilityCallback callback) {
  const std::optional<bool> enabled = request.args.FindBool("enabled");
  if (!enabled.has_value() || !request.window.is_valid() || !request_ ||
      !state_ || !applied_) {
    std::move(callback).Run(
        FailOutcome("A valid window and explicit compact state are required."));
    return;
  }
  if (!state_.Run(request.window).has_value()) {
    std::move(callback).Run(FailOutcome(
        "Compact mode is unavailable while a Scene controls presentation or "
        "the browser window is no longer available."));
    return;
  }
  if (!request_.Run(*enabled, request.window)) {
    std::move(callback).Run(
        FailOutcome("The native browser rejected the compact-mode request."));
    return;
  }
  if (applied_.Run(*enabled, request.window)) {
    CapabilityOutcome outcome;
    outcome.step.status = StepStatus::kSucceeded;
    outcome.step.observed_summary = *enabled
                                        ? "Compact browser chrome is active."
                                        : "Full browser chrome is restored.";
    outcome.step.verification.verified = true;
    outcome.step.verification.method = "vertical_tab_state_observation";
    std::move(callback).Run(std::move(outcome));
    return;
  }

  const PendingKey key{request.task_id, request.step_id};
  if (pending_.contains(key)) {
    std::move(callback).Run(
        FailOutcome("A compact-mode request is already pending."));
    return;
  }
  auto pending = std::make_unique<Pending>();
  pending->callback = std::move(callback);
  pending->window = request.window;
  pending->enabled = *enabled;
  pending->deadline = base::TimeTicks::Now() + base::Seconds(5);
  pending->timer.Start(FROM_HERE, base::Milliseconds(25),
                       base::BindRepeating(&CompactModeExecutor::Poll,
                                           weak_factory_.GetWeakPtr(), key));
  pending_.emplace(key, std::move(pending));
}

void CompactModeExecutor::Cancel(const TaskId &task_id,
                                 const std::string &step_id) {
  Complete({task_id, step_id}, StepStatus::kOutcomeUnknown,
           "Cancelled before the native compact state was confirmed.",
           /*verified=*/false);
}

void CompactModeExecutor::Poll(PendingKey key) {
  const auto found = pending_.find(key);
  if (found == pending_.end()) {
    return;
  }
  if (applied_.Run(found->second->enabled, found->second->window)) {
    Complete(key, StepStatus::kSucceeded,
             found->second->enabled ? "Compact browser chrome is active."
                                    : "Full browser chrome is restored.",
             /*verified=*/true);
    return;
  }
  if (!state_.Run(found->second->window).has_value()) {
    Complete(key, StepStatus::kOutcomeUnknown,
             "The browser window became unavailable before compact mode was "
             "confirmed.",
             /*verified=*/false);
    return;
  }
  if (base::TimeTicks::Now() >= found->second->deadline) {
    Complete(key, StepStatus::kOutcomeUnknown,
             "The native browser did not confirm compact mode in time.",
             /*verified=*/false);
  }
}

void CompactModeExecutor::Complete(PendingKey key, StepStatus status,
                                   const std::string &summary, bool verified) {
  const auto found = pending_.find(key);
  if (found == pending_.end()) {
    return;
  }
  CapabilityCallback callback = std::move(found->second->callback);
  pending_.erase(found);
  CapabilityOutcome outcome;
  outcome.step.status = status;
  outcome.step.observed_summary = summary;
  outcome.step.verification.verified = verified;
  if (verified) {
    outcome.step.verification.method = "vertical_tab_state_observation";
  }
  std::move(callback).Run(std::move(outcome));
}

// --- EnumerateTabsExecutor --------------------------------------------------

EnumerateTabsExecutor::EnumerateTabsExecutor(
    OrganizationModel *model, LiveWindowStateProvider *live_state)
    : model_(model), live_state_(live_state) {}

EnumerateTabsExecutor::~EnumerateTabsExecutor() = default;

ToolId EnumerateTabsExecutor::capability_id() const {
  return ToolId::FromString("browser.tabs.enumerate");
}

void EnumerateTabsExecutor::Execute(CapabilityRequest request,
                                    CapabilityCallback callback) {
  CapabilityOutcome outcome;
  SemanticResult result;
  result.schema.shape = SemanticShape::kEntityCollection;
  result.schema.fields = {
      Field("tab", FieldPrimitive::kString, SemanticRole::kIdentifier),
      Field("title", FieldPrimitive::kString, SemanticRole::kName),
      Field("origin", FieldPrimitive::kString, SemanticRole::kUrl),
      Field("state", FieldPrimitive::kString, SemanticRole::kStatus),
      Field("order", FieldPrimitive::kInteger, SemanticRole::kDimension),
      Field("pinned", FieldPrimitive::kBoolean, SemanticRole::kCategory),
      Field("active", FieldPrimitive::kBoolean, SemanticRole::kStatus)};

  base::ListValue rows;
  if (live_state_) {
    const std::optional<LiveWindowSnapshot> snapshot =
        live_state_->GetSnapshot(request.window);
    if (snapshot.has_value()) {
      for (const LiveTabDescriptor &tab : snapshot->tabs) {
        base::DictValue row;
        row.Set("tab", tab.tab.value());
        row.Set("title", tab.title.empty() ? "Untitled tab" : tab.title);
        const GURL origin(tab.origin);
        row.Set("origin",
                origin.is_valid() && origin.SchemeIsHTTPOrHTTPS()
                    ? origin.spec()
                    : std::string());
        row.Set("state", tab.is_active
                             ? "active"
                             : (tab.chromium_pinned ? "pinned" : "open"));
        row.Set("order", tab.strip_order);
        row.Set("pinned", tab.chromium_pinned);
        row.Set("active", tab.is_active);
        rows.Append(std::move(row));
      }
    }
  }
  result.data = base::Value(std::move(rows));
  SetProvenance(&result, "browser.tabs.enumerate");

  outcome.step.status = StepStatus::kSucceeded;
  outcome.step.observed_summary = "Read the live tab strip.";
  outcome.step.verification.verified = true;
  outcome.step.verification.method = "observation";
  outcome.semantic = std::move(result);
  std::move(callback).Run(std::move(outcome));
}

// --- PageObserveExecutor ----------------------------------------------------

PageObserveExecutor::PageObserveExecutor(PageAgent *page_agent,
                                         LiveWindowStateProvider *live_state)
    : page_agent_(page_agent), live_state_(live_state) {}

PageObserveExecutor::~PageObserveExecutor() = default;

ToolId PageObserveExecutor::capability_id() const {
  return ToolId::FromString("page.observe.text");
}

void PageObserveExecutor::Execute(CapabilityRequest request,
                                  CapabilityCallback callback) {
  if (!page_agent_ || !live_state_) {
    std::move(callback).Run(FailOutcome("Page agent unavailable."));
    return;
  }
  const std::optional<LiveWindowSnapshot> snapshot =
      live_state_->GetSnapshot(request.window);
  if (!snapshot.has_value() || !snapshot->active_tab.is_valid()) {
    std::move(callback).Run(FailOutcome("No active tab to observe."));
    return;
  }
  page_agent_->Observe(snapshot->active_tab,
                       base::BindOnce(&PageObserveExecutor::OnObserved,
                                      weak_factory_.GetWeakPtr(),
                                      std::move(callback)));
}

void PageObserveExecutor::OnObserved(
    CapabilityCallback callback, std::optional<PageObservation> observation) {
  if (!observation.has_value()) {
    std::move(callback).Run(FailOutcome("The page could not be observed."));
    return;
  }
  SemanticResult result;
  result.schema.shape = SemanticShape::kEntityCollection;
  result.schema.fields = {
      Field("handle", FieldPrimitive::kString, SemanticRole::kIdentifier),
      Field("role", FieldPrimitive::kString, SemanticRole::kCategory),
      Field("name", FieldPrimitive::kString, SemanticRole::kName),
      Field("editable", FieldPrimitive::kBoolean, SemanticRole::kStatus),
      Field("agent_writable", FieldPrimitive::kBoolean, SemanticRole::kStatus),
      Field("sensitivity", FieldPrimitive::kString, SemanticRole::kCategory)};
  base::ListValue rows;
  for (const PageObservation::Element &element : observation->elements) {
    base::DictValue row;
    row.Set("handle", element.handle);
    row.Set("role", element.role);
    row.Set("name", element.name);
    row.Set("editable", element.editable);
    row.Set("agent_writable", element.agent_writable);
    row.Set("sensitivity", PageFieldSensitivityName(element.sensitivity));
    rows.Append(std::move(row));
  }
  result.data = base::Value(std::move(rows));
  SetProvenance(&result, "page.observe.text");
  result.provenance.base.source_url = observation->url;

  CapabilityOutcome outcome;
  outcome.step.status = StepStatus::kSucceeded;
  outcome.step.observed_summary = "Observed the active document.";
  outcome.step.verification.verified = true;
  outcome.step.verification.method = "observation";
  outcome.semantic = std::move(result);
  std::move(callback).Run(std::move(outcome));
}

// --- PageExtractStructuredExecutor -----------------------------------------

PageExtractStructuredExecutor::PageExtractStructuredExecutor(
    PageAgent *page_agent, LiveWindowStateProvider *live_state)
    : page_agent_(page_agent), live_state_(live_state) {}

PageExtractStructuredExecutor::~PageExtractStructuredExecutor() = default;

ToolId PageExtractStructuredExecutor::capability_id() const {
  return ToolId::FromString("page.extract.structured");
}

void PageExtractStructuredExecutor::Execute(CapabilityRequest request,
                                            CapabilityCallback callback) {
  constexpr size_t kMaxSchemaJsonBytes = 64 * 1024;
  const std::string *tab_key = request.args.FindString("tab_key");
  const std::string *schema_json =
      request.args.FindString("wanted_schema_json");
  const LiveTabKey tab = tab_key ? LiveTabKey::Parse(*tab_key) : LiveTabKey();
  if (!page_agent_ || !live_state_ || !tab.is_valid() || !schema_json ||
      schema_json->empty() || schema_json->size() > kMaxSchemaJsonBytes) {
    std::move(callback).Run(
        FailOutcome("An exact tab and bounded semantic schema are required."));
    return;
  }
  const std::optional<LiveWindowSnapshot> window =
      live_state_->GetSnapshot(request.window);
  if (!window.has_value() ||
      std::ranges::find(window->tabs, tab, &LiveTabDescriptor::tab) ==
          window->tabs.end()) {
    std::move(callback).Run(
        FailOutcome("The requested tab is not live in this task window."));
    return;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(*schema_json, base::JSON_PARSE_RFC);
  if (!parsed.has_value()) {
    std::move(callback).Run(
        FailOutcome("The requested semantic schema is not valid JSON."));
    return;
  }
  base::expected<SemanticSchema, SemanticViolation> schema =
      ParseSemanticSchema(*parsed);
  if (!schema.has_value() ||
      !ValidateSemanticSchema(schema.value()).has_value() ||
      schema->shape != SemanticShape::kEntityCollection ||
      schema->fields.empty() || !schema->edge_fields.empty() ||
      !schema->parts.empty()) {
    std::move(callback).Run(FailOutcome(
        "Structured page extraction requires a valid entity-collection "
        "schema."));
    return;
  }

  const std::map<std::string, FieldPrimitive> supported = {
      {"handle", FieldPrimitive::kString},
      {"role", FieldPrimitive::kString},
      {"name", FieldPrimitive::kString},
      {"enabled", FieldPrimitive::kBoolean},
      {"focusable", FieldPrimitive::kBoolean},
      {"editable", FieldPrimitive::kBoolean},
      {"agent_writable", FieldPrimitive::kBoolean},
      {"sensitivity", FieldPrimitive::kString},
  };
  std::set<std::string> field_ids;
  for (const FieldSpec &field : schema->fields) {
    const auto supported_field = supported.find(field.id);
    if (supported_field == supported.end() ||
        supported_field->second != field.primitive ||
        !field_ids.insert(field.id).second) {
      std::move(callback).Run(FailOutcome(
          "The schema requests a page field that cannot be extracted "
          "truthfully."));
      return;
    }
  }
  page_agent_->Observe(
      tab, base::BindOnce(&PageExtractStructuredExecutor::OnObserved,
                          weak_factory_.GetWeakPtr(), std::move(schema.value()),
                          std::move(callback)));
}

void PageExtractStructuredExecutor::OnObserved(
    SemanticSchema schema, CapabilityCallback callback,
    std::optional<PageObservation> observation) {
  if (!observation.has_value()) {
    std::move(callback).Run(FailOutcome("The page could not be observed."));
    return;
  }
  base::ListValue rows;
  for (const PageObservation::Element &element : observation->elements) {
    base::DictValue row;
    for (const FieldSpec &field : schema.fields) {
      if (field.id == "handle") {
        row.Set(field.id, element.handle);
      } else if (field.id == "role") {
        row.Set(field.id, element.role);
      } else if (field.id == "name") {
        row.Set(field.id, element.name);
      } else if (field.id == "enabled") {
        row.Set(field.id, element.enabled);
      } else if (field.id == "focusable") {
        row.Set(field.id, element.focusable);
      } else if (field.id == "editable") {
        row.Set(field.id, element.editable);
      } else if (field.id == "agent_writable") {
        row.Set(field.id, element.agent_writable);
      } else if (field.id == "sensitivity") {
        row.Set(field.id, PageFieldSensitivityName(element.sensitivity));
      }
    }
    rows.Append(std::move(row));
  }

  SemanticResult result;
  result.schema = std::move(schema);
  result.data = base::Value(std::move(rows));
  SetProvenance(&result, "page.extract.structured");
  result.provenance.base.source_url = observation->url;
  const SemanticValidationResult valid = ValidateSemanticResult(result);
  if (!valid.has_value()) {
    std::move(callback).Run(
        FailOutcome("The extracted page data did not satisfy its schema."));
    return;
  }
  CapabilityOutcome outcome;
  outcome.step.status = StepStatus::kSucceeded;
  outcome.step.observed_summary =
      "Extracted bounded page elements into the requested schema.";
  outcome.step.verification.verified = true;
  outcome.step.verification.method = "semantic_schema_validation";
  outcome.semantic = std::move(result);
  std::move(callback).Run(std::move(outcome));
}

// --- PageActionExecutor -----------------------------------------------------

PageActionExecutor::PageActionExecutor(const std::string &capability_id,
                                       PageActionKind action,
                                       PageAgent *page_agent,
                                       LiveWindowStateProvider *live_state)
    : id_(ToolId::FromString(capability_id)), action_(action),
      page_agent_(page_agent), live_state_(live_state) {}

PageActionExecutor::~PageActionExecutor() = default;

ToolId PageActionExecutor::capability_id() const { return id_; }

void PageActionExecutor::Execute(CapabilityRequest request,
                                 CapabilityCallback callback) {
  if (!page_agent_ || !live_state_) {
    std::move(callback).Run(FailOutcome("Page agent unavailable."));
    return;
  }
  const std::optional<LiveWindowSnapshot> snapshot =
      live_state_->GetSnapshot(request.window);
  if (!snapshot.has_value() || !snapshot->active_tab.is_valid()) {
    std::move(callback).Run(FailOutcome("No active tab for the action."));
    return;
  }
  const std::string *handle = request.args.FindString("handle");
  if (!handle || handle->empty()) {
    std::move(callback).Run(FailOutcome("An element handle is required."));
    return;
  }
  PageActionRequest action;
  action.kind = action_;
  action.handle = *handle;
  if (const std::string *value = request.args.FindString("value")) {
    action.value = *value;
  }
  const auto key = std::make_pair(request.task_id, request.step_id);
  if (pending_.contains(key)) {
    std::move(callback).Run(
        FailOutcome("This page action is already running."));
    return;
  }
  pending_.emplace(key, std::move(callback));
  page_agent_->PerformActionAndVerify(
      snapshot->active_tab, action,
      base::BindOnce(&PageActionExecutor::OnVerified,
                     weak_factory_.GetWeakPtr(), request.task_id,
                     request.step_id));
}

void PageActionExecutor::OnVerified(TaskId task_id, std::string step_id,
                                    PageActionStatus status) {
  const auto key = std::make_pair(task_id, step_id);
  auto waiting = pending_.find(key);
  if (waiting == pending_.end()) {
    return;
  }
  CapabilityCallback callback = std::move(waiting->second);
  pending_.erase(waiting);
  CapabilityOutcome outcome;
  if (status == PageActionStatus::kOk) {
    outcome.step.status = StepStatus::kSucceeded;
    outcome.step.observed_summary =
        "Performed the page action and observed a document change.";
    outcome.step.verification.verified = true;
    outcome.step.verification.method =
        "accessibility_tree_change_or_navigation";
  } else if (status == PageActionStatus::kExpiredHandle) {
    // A stale handle is an invalid assumption, not a silent failure: replan.
    outcome.step.status = StepStatus::kFailed;
    outcome.step.observed_summary =
        "The element handle expired; re-observe the page first.";
  } else if (status == PageActionStatus::kSensitiveField) {
    outcome.step.status = StepStatus::kFailed;
    outcome.step.observed_summary =
        "Sensitive field values require browser autofill or direct user "
        "takeover; no value was sent to the page.";
  } else {
    outcome.step.status = StepStatus::kFailed;
    outcome.step.observed_summary = "The page action could not be performed.";
  }
  std::move(callback).Run(std::move(outcome));
}

void PageActionExecutor::Cancel(const TaskId &task_id,
                                const std::string &step_id) {
  const auto key = std::make_pair(task_id, step_id);
  auto waiting = pending_.find(key);
  if (waiting == pending_.end()) {
    return;
  }
  CapabilityCallback callback = std::move(waiting->second);
  pending_.erase(waiting);
  CapabilityOutcome outcome;
  outcome.step.status = StepStatus::kOutcomeUnknown;
  outcome.step.observed_summary =
      "Cancelled after dispatch but before page-change confirmation.";
  std::move(callback).Run(std::move(outcome));
}

BrowserCommandExecutor::Pending::Pending() = default;
BrowserCommandExecutor::Pending::Pending(Pending &&) = default;
BrowserCommandExecutor::Pending &
BrowserCommandExecutor::Pending::operator=(Pending &&) = default;
BrowserCommandExecutor::Pending::~Pending() = default;

} // namespace seoul
