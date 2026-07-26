// Project Seoul Live Collections runtime.

#include "seoul/browser/product/live_collection_coordinator.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/strings/string_number_conversions.h"
#include "base/timer/timer.h"
#include "seoul/browser/data/data_validation.h"
#include "seoul/browser/semantic/semantic_validation.h"
#include "seoul/browser/tools/tool_schema.h"
#include "url/gurl.h"

namespace seoul {
namespace {

LiveCollectionRuntimeError LibraryErrorToRuntime(LibraryError error) {
  switch (error) {
    case LibraryError::kUnknownCollection:
      return LiveCollectionRuntimeError::kUnknownCollection;
    case LibraryError::kLimitExceeded:
      return LiveCollectionRuntimeError::kLimitExceeded;
    case LibraryError::kInvalidId:
    case LibraryError::kInvalidName:
    case LibraryError::kInvalidElement:
    case LibraryError::kInvalidArtifact:
    case LibraryError::kInvalidCollection:
    case LibraryError::kInvalidLiveItem:
    case LibraryError::kUnknownBoard:
    case LibraryError::kUnknownElement:
    case LibraryError::kUnknownArtifact:
    case LibraryError::kBoardArchived:
    case LibraryError::kStaleRefresh:
    case LibraryError::kUnsupportedSchema:
      return LiveCollectionRuntimeError::kInvalidDefinition;
  }
  return LiveCollectionRuntimeError::kInvalidDefinition;
}

bool BackgroundSafe(const ToolDescriptor& descriptor) {
  return descriptor.risk == RiskCategory::kReadOnly &&
         descriptor.approval == ApprovalPolicy::kNeverRequired &&
         descriptor.idempotency == IdempotencyClass::kIdempotent;
}

std::optional<LiveCollectionSource> SourceFromDescriptor(
    const ToolDescriptor& descriptor) {
  if (!BackgroundSafe(descriptor)) {
    return std::nullopt;
  }
  const SchemaField* required = nullptr;
  for (const SchemaField& field : descriptor.input_schema.fields) {
    if (!field.required) {
      continue;
    }
    if (required ||
        (field.kind != SchemaFieldKind::kString &&
         field.kind != SchemaFieldKind::kUrl)) {
      return std::nullopt;
    }
    required = &field;
  }

  LiveCollectionSource source;
  source.capability = descriptor.id;
  source.version = descriptor.version;
  source.name = descriptor.name;
  source.description = descriptor.description;
  source.provider = descriptor.provider;
  if (required) {
    source.source_required = true;
    source.source_field = required->name;
    source.source_description = required->description;
    source.source_kind = required->kind;
  }
  return source;
}

const FieldSpec* FindRole(const SemanticSchema& schema, SemanticRole role) {
  auto found = std::ranges::find_if(
      schema.fields,
      [role](const FieldSpec& field) { return field.role == role; });
  return found == schema.fields.end() ? nullptr : &*found;
}

const FieldSpec* FindFirstRole(const SemanticSchema& schema,
                               std::initializer_list<SemanticRole> roles) {
  for (SemanticRole role : roles) {
    if (const FieldSpec* field = FindRole(schema, role)) {
      return field;
    }
  }
  return nullptr;
}

std::optional<std::string> DisplayValue(const base::Value* value) {
  if (!value || value->is_none()) {
    return std::nullopt;
  }
  if (value->is_string()) {
    return value->GetString();
  }
  if (value->is_int()) {
    return base::NumberToString(value->GetInt());
  }
  if (value->is_double()) {
    return base::NumberToString(value->GetDouble());
  }
  if (value->is_bool()) {
    return value->GetBool() ? "true" : "false";
  }
  return std::nullopt;
}

std::optional<base::Time> TimeValue(const base::Value* value) {
  if (!value || (!value->is_int() && !value->is_double())) {
    return std::nullopt;
  }
  const double milliseconds = value->GetDouble();
  if (!std::isfinite(milliseconds)) {
    return std::nullopt;
  }
  if (milliseconds < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
      milliseconds > static_cast<double>(std::numeric_limits<int64_t>::max())) {
    return std::nullopt;
  }
  return base::Time::FromMillisecondsSinceUnixEpoch(
      static_cast<int64_t>(milliseconds));
}

LiveCollectionRuntimeStatus SemanticToItems(
    const SemanticResult& result,
    std::vector<LiveCollectionItem>* items) {
  if (!items || result.state != ResultState::kComplete ||
      !result.errors.empty() ||
      !ValidateSemanticResult(result).has_value() ||
      ValidateProvenance(result.provenance.base) !=
          DataContractViolation::kEligible ||
      !result.data.is_list()) {
    return base::unexpected(LiveCollectionRuntimeError::kInvalidResult);
  }
  const FieldSpec* identifier =
      FindRole(result.schema, SemanticRole::kIdentifier);
  const FieldSpec* title = FindFirstRole(
      result.schema, {SemanticRole::kName, SemanticRole::kDescription});
  if (!identifier || !title) {
    return base::unexpected(LiveCollectionRuntimeError::kInvalidResult);
  }
  const FieldSpec* subtitle = FindFirstRole(
      result.schema, {SemanticRole::kDescription, SemanticRole::kBody});
  if (subtitle == title) {
    subtitle = nullptr;
  }
  const FieldSpec* url = FindFirstRole(
      result.schema, {SemanticRole::kUrl, SemanticRole::kMediaUrl});
  const FieldSpec* status = FindFirstRole(
      result.schema, {SemanticRole::kStatus, SemanticRole::kCategory,
                      SemanticRole::kSeverity});
  const FieldSpec* start = FindFirstRole(
      result.schema,
      {SemanticRole::kIntervalStart, SemanticRole::kTimestamp});
  const FieldSpec* end = FindRole(result.schema, SemanticRole::kIntervalEnd);

  const base::ListValue& rows = result.data.GetList();
  if (rows.size() > kMaxLiveItemsPerCollection) {
    return base::unexpected(LiveCollectionRuntimeError::kLimitExceeded);
  }
  std::vector<LiveCollectionItem> proposed;
  proposed.reserve(rows.size());
  for (const base::Value& row_value : rows) {
    const base::DictValue* row = row_value.GetIfDict();
    if (!row) {
      return base::unexpected(LiveCollectionRuntimeError::kInvalidResult);
    }
    const std::optional<std::string> stable_key =
        DisplayValue(row->Find(identifier->id));
    const std::optional<std::string> item_title =
        DisplayValue(row->Find(title->id));
    if (!stable_key.has_value() || stable_key->empty() ||
        !item_title.has_value() || item_title->empty()) {
      return base::unexpected(LiveCollectionRuntimeError::kInvalidResult);
    }

    LiveCollectionItem item;
    item.stable_key = *stable_key;
    item.title = *item_title;
    if (subtitle) {
      item.subtitle = DisplayValue(row->Find(subtitle->id)).value_or("");
    }
    if (status) {
      item.status = DisplayValue(row->Find(status->id)).value_or("");
    }
    if (url) {
      item.url = DisplayValue(row->Find(url->id)).value_or("");
      if (!item.url.empty()) {
        const GURL parsed(item.url);
        if (!parsed.is_valid() || !parsed.SchemeIsHTTPOrHTTPS()) {
          return base::unexpected(
              LiveCollectionRuntimeError::kInvalidResult);
        }
        item.actionable = true;
      }
    }
    if (start) {
      item.start_time =
          TimeValue(row->Find(start->id)).value_or(base::Time());
    }
    if (end) {
      item.end_time = TimeValue(row->Find(end->id)).value_or(base::Time());
    }
    proposed.push_back(std::move(item));
  }
  *items = std::move(proposed);
  return base::ok();
}

std::string FailureDetail(const CapabilityOutcome& outcome) {
  if (!outcome.step.observed_summary.empty()) {
    return outcome.step.observed_summary;
  }
  return "The source did not return a verified result.";
}

}  // namespace

struct LiveCollectionCoordinator::PendingRefresh {
  uint64_t generation = 0;
  ToolId capability;
  int version = 1;
  TaskId task_id;
  std::string step_id;
  LiveWindowKey window;
  base::OneShotTimer timer;
  RefreshCallback callback;
};

const char* LiveCollectionRuntimeErrorToString(
    LiveCollectionRuntimeError error) {
  switch (error) {
    case LiveCollectionRuntimeError::kInvalidDefinition:
      return "invalid_live_collection";
    case LiveCollectionRuntimeError::kUnknownCollection:
      return "unknown_collection";
    case LiveCollectionRuntimeError::kLimitExceeded:
      return "limit_exceeded";
    case LiveCollectionRuntimeError::kSourceUnavailable:
      return "collection_source_unavailable";
    case LiveCollectionRuntimeError::kSourceNotBackgroundSafe:
      return "collection_source_not_background_safe";
    case LiveCollectionRuntimeError::kSourceSchemaUnsupported:
      return "collection_source_schema_unsupported";
    case LiveCollectionRuntimeError::kSourceInputInvalid:
      return "collection_source_input_invalid";
    case LiveCollectionRuntimeError::kRefreshInProgress:
      return "collection_refresh_in_progress";
    case LiveCollectionRuntimeError::kWindowUnavailable:
      return "collection_window_unavailable";
    case LiveCollectionRuntimeError::kProviderFailed:
      return "collection_provider_failed";
    case LiveCollectionRuntimeError::kInvalidResult:
      return "collection_invalid_result";
    case LiveCollectionRuntimeError::kRefreshTimedOut:
      return "collection_refresh_timed_out";
    case LiveCollectionRuntimeError::kCancelled:
      return "collection_refresh_cancelled";
    case LiveCollectionRuntimeError::kShuttingDown:
      return "collection_runtime_stopped";
  }
  return "invalid_live_collection";
}

LiveCollectionSource::LiveCollectionSource() = default;
LiveCollectionSource::LiveCollectionSource(const LiveCollectionSource&) =
    default;
LiveCollectionSource::LiveCollectionSource(LiveCollectionSource&&) = default;
LiveCollectionSource& LiveCollectionSource::operator=(
    const LiveCollectionSource&) = default;
LiveCollectionSource& LiveCollectionSource::operator=(
    LiveCollectionSource&&) = default;
LiveCollectionSource::~LiveCollectionSource() = default;

LiveCollectionCoordinator::LiveCollectionCoordinator(
    LibraryService* library,
    ToolRegistry* capabilities,
    CapabilityExecutorRegistry* executors)
    : library_(library), capabilities_(capabilities), executors_(executors) {}

LiveCollectionCoordinator::~LiveCollectionCoordinator() {
  DCHECK(pending_.empty());
}

std::vector<LiveCollectionSource>
LiveCollectionCoordinator::EligibleSources(
    const ToolPermissionContext& context) const {
  std::vector<LiveCollectionSource> sources;
  if (shutting_down_ || !capabilities_ || !executors_) {
    return sources;
  }
  for (const ToolDescriptor* descriptor :
       capabilities_->ListAvailable(context)) {
    if (!descriptor ||
        !executors_->Find(descriptor->id, descriptor->version)) {
      continue;
    }
    std::optional<LiveCollectionSource> source =
        SourceFromDescriptor(*descriptor);
    if (source.has_value()) {
      sources.push_back(std::move(*source));
    }
  }
  return sources;
}

const LiveCollectionSource* LiveCollectionCoordinator::FindEligibleSource(
    const ToolId& capability,
    const ToolPermissionContext& context,
    std::vector<LiveCollectionSource>* storage) const {
  if (!storage) {
    return nullptr;
  }
  *storage = EligibleSources(context);
  auto found = std::ranges::find_if(
      *storage, [&capability](const LiveCollectionSource& source) {
        return source.capability == capability;
      });
  return found == storage->end() ? nullptr : &*found;
}

LiveCollectionRuntimeStatus LiveCollectionCoordinator::BuildArgs(
    const LiveCollectionSource& source,
    const ToolDescriptor& descriptor,
    const std::string& source_locator,
    base::DictValue* args) const {
  if (!args) {
    return base::unexpected(
        LiveCollectionRuntimeError::kSourceInputInvalid);
  }
  if (source.source_required) {
    if (source_locator.empty()) {
      return base::unexpected(
          LiveCollectionRuntimeError::kSourceInputInvalid);
    }
    args->Set(source.source_field, source_locator);
  }
  if (!ValidateArgs(descriptor.input_schema, *args).has_value()) {
    return base::unexpected(
        LiveCollectionRuntimeError::kSourceInputInvalid);
  }
  return base::ok();
}

LiveCollectionUpsertResult LiveCollectionCoordinator::Upsert(
    LiveCollectionDefinition definition,
    const ToolPermissionContext& context) {
  if (shutting_down_) {
    return base::unexpected(LiveCollectionRuntimeError::kShuttingDown);
  }
  if (!library_ || !capabilities_ || !executors_) {
    return base::unexpected(LiveCollectionRuntimeError::kSourceUnavailable);
  }

  if (definition.enabled) {
    const ToolDescriptor* descriptor =
        capabilities_->Find(definition.refresh_capability);
    if (!descriptor) {
      return base::unexpected(LiveCollectionRuntimeError::kSourceUnavailable);
    }
    if (!BackgroundSafe(*descriptor)) {
      return base::unexpected(
          LiveCollectionRuntimeError::kSourceNotBackgroundSafe);
    }
    std::optional<LiveCollectionSource> source =
        SourceFromDescriptor(*descriptor);
    if (!source.has_value()) {
      return base::unexpected(
          LiveCollectionRuntimeError::kSourceSchemaUnsupported);
    }
    std::vector<LiveCollectionSource> available;
    if (!FindEligibleSource(
            definition.refresh_capability, context, &available)) {
      return base::unexpected(LiveCollectionRuntimeError::kSourceUnavailable);
    }
    base::DictValue args;
    if (LiveCollectionRuntimeStatus valid =
            BuildArgs(*source, *descriptor, definition.source_locator, &args);
        !valid.has_value()) {
      return base::unexpected(valid.error());
    }
  }

  RefreshCallback cancelled;
  if (definition.id.is_valid()) {
    const LiveCollectionRecord* current =
        library_->FindLiveCollection(definition.id);
    if (!current) {
      return base::unexpected(LiveCollectionRuntimeError::kUnknownCollection);
    }
    const LiveCollectionId id = current->definition.id;
    const bool execution_changed =
        current->definition.refresh_capability !=
            definition.refresh_capability ||
        current->definition.source_locator != definition.source_locator ||
        current->definition.scope_window != definition.scope_window ||
        (current->definition.enabled && !definition.enabled);
    const LibraryStatusResult updated =
        library_->UpdateLiveCollection(std::move(definition));
    if (!updated.has_value()) {
      return base::unexpected(LibraryErrorToRuntime(updated.error()));
    }
    // Validate and commit the definition before touching an active
    // provider request. A rejected edit must leave the existing refresh and
    // its Library state intact rather than stranding it in kRefreshing.
    if (execution_changed) {
      cancelled = CancelPending(id);
    }
    if (cancelled) {
      std::move(cancelled).Run(
          base::unexpected(LiveCollectionRuntimeError::kCancelled));
    }
    return id;
  }

  const LibraryResult<LiveCollectionId> created =
      library_->CreateLiveCollection(std::move(definition));
  if (!created.has_value()) {
    return base::unexpected(LibraryErrorToRuntime(created.error()));
  }
  return *created;
}

LiveCollectionRuntimeStatus LiveCollectionCoordinator::SetEnabled(
    const LiveCollectionId& id,
    bool enabled,
    const std::string& scope_window,
    const ToolPermissionContext& context) {
  if (!library_) {
    return base::unexpected(LiveCollectionRuntimeError::kSourceUnavailable);
  }
  const LiveCollectionRecord* record = library_->FindLiveCollection(id);
  if (!record) {
    return base::unexpected(LiveCollectionRuntimeError::kUnknownCollection);
  }
  LiveCollectionDefinition definition = record->definition;
  definition.enabled = enabled;
  if (enabled && !scope_window.empty()) {
    definition.scope_window = scope_window;
  }
  LiveCollectionUpsertResult result = Upsert(std::move(definition), context);
  if (!result.has_value()) {
    return base::unexpected(result.error());
  }
  return base::ok();
}

LiveCollectionRuntimeStatus LiveCollectionCoordinator::Delete(
    const LiveCollectionId& id) {
  if (shutting_down_) {
    return base::unexpected(LiveCollectionRuntimeError::kShuttingDown);
  }
  if (!library_) {
    return base::unexpected(LiveCollectionRuntimeError::kSourceUnavailable);
  }
  RefreshCallback cancelled = CancelPending(id);
  const LibraryStatusResult result = library_->DeleteLiveCollection(id);
  if (!result.has_value()) {
    if (cancelled) {
      std::move(cancelled).Run(
          base::unexpected(LiveCollectionRuntimeError::kCancelled));
    }
    return base::unexpected(LibraryErrorToRuntime(result.error()));
  }
  if (cancelled) {
    std::move(cancelled).Run(
        base::unexpected(LiveCollectionRuntimeError::kCancelled));
  }
  return base::ok();
}

LiveCollectionRuntimeStatus LiveCollectionCoordinator::Refresh(
    const LiveCollectionId& id,
    const LiveWindowKey& window,
    const ToolPermissionContext& context,
    RefreshCallback callback) {
  auto reject = [&callback](LiveCollectionRuntimeError error) {
    LiveCollectionRuntimeStatus result = base::unexpected(error);
    if (callback) {
      std::move(callback).Run(base::unexpected(error));
    }
    return result;
  };
  if (shutting_down_) {
    return reject(LiveCollectionRuntimeError::kShuttingDown);
  }
  if (!library_ || !capabilities_ || !executors_) {
    return reject(LiveCollectionRuntimeError::kSourceUnavailable);
  }
  if (!window.is_valid()) {
    return reject(LiveCollectionRuntimeError::kWindowUnavailable);
  }
  const LiveCollectionRecord* record = library_->FindLiveCollection(id);
  if (!record) {
    return reject(LiveCollectionRuntimeError::kUnknownCollection);
  }
  if (!record->definition.enabled) {
    return reject(LiveCollectionRuntimeError::kInvalidDefinition);
  }
  if (pending_.contains(id)) {
    return reject(LiveCollectionRuntimeError::kRefreshInProgress);
  }

  std::vector<LiveCollectionSource> sources;
  const LiveCollectionSource* source = FindEligibleSource(
      record->definition.refresh_capability, context, &sources);
  if (!source) {
    const LibraryResult<uint64_t> generation = library_->BeginLiveRefresh(id);
    if (generation.has_value()) {
      std::ignore = CompleteWithError(
          id, *generation, LiveCollectionRuntimeError::kSourceUnavailable,
          "The collection source is unavailable.");
    }
    return reject(LiveCollectionRuntimeError::kSourceUnavailable);
  }
  const ToolDescriptor* descriptor =
      capabilities_->Find(source->capability);
  CapabilityExecutor* executor = descriptor
      ? executors_->Find(descriptor->id, descriptor->version)
      : nullptr;
  if (!descriptor || !executor) {
    return reject(LiveCollectionRuntimeError::kSourceUnavailable);
  }
  base::DictValue args;
  if (LiveCollectionRuntimeStatus valid =
          BuildArgs(*source, *descriptor, record->definition.source_locator,
                    &args);
      !valid.has_value()) {
    return reject(valid.error());
  }
  const LibraryResult<uint64_t> generation = library_->BeginLiveRefresh(id);
  if (!generation.has_value()) {
    return reject(LibraryErrorToRuntime(generation.error()));
  }

  auto pending = std::make_unique<PendingRefresh>();
  pending->generation = *generation;
  pending->capability = descriptor->id;
  pending->version = descriptor->version;
  pending->task_id = TaskId::GenerateNew();
  pending->step_id = "live_collection_refresh";
  pending->window = window;
  pending->callback = std::move(callback);
  pending->timer.Start(
      FROM_HERE, descriptor->timeout,
      base::BindOnce(&LiveCollectionCoordinator::OnRefreshTimeout,
                     weak_factory_.GetWeakPtr(), id, *generation));
  const TaskId task_id = pending->task_id;
  const std::string step_id = pending->step_id;
  pending_.emplace(id, std::move(pending));

  CapabilityRequest request;
  request.capability = descriptor->id;
  request.version = descriptor->version;
  request.args = std::move(args);
  request.task_id = task_id;
  request.step_id = step_id;
  request.window = window;
  request.user_gesture = false;
  executor->Execute(
      std::move(request),
      base::BindOnce(&LiveCollectionCoordinator::OnRefreshOutcome,
                     weak_factory_.GetWeakPtr(), id, *generation));
  return base::ok();
}

LiveCollectionRuntimeStatus LiveCollectionCoordinator::CompleteWithError(
    const LiveCollectionId& id,
    uint64_t generation,
    LiveCollectionRuntimeError error,
    const std::string& detail) {
  const LibraryStatusResult completed =
      library_->CompleteLiveRefresh(id, generation, {}, detail);
  if (!completed.has_value()) {
    return base::unexpected(LibraryErrorToRuntime(completed.error()));
  }
  return base::unexpected(error);
}

void LiveCollectionCoordinator::OnRefreshOutcome(
    const LiveCollectionId& id,
    uint64_t generation,
    CapabilityOutcome outcome) {
  auto found = pending_.find(id);
  if (found == pending_.end() || found->second->generation != generation) {
    return;
  }
  found->second->timer.Stop();
  RefreshCallback callback = std::move(found->second->callback);
  pending_.erase(found);

  LiveCollectionRuntimeStatus result = base::ok();
  if (outcome.step.status != StepStatus::kSucceeded ||
      !outcome.step.verification.verified || !outcome.semantic.has_value()) {
    result = CompleteWithError(id, generation,
                               LiveCollectionRuntimeError::kProviderFailed,
                               FailureDetail(outcome));
  } else {
    std::vector<LiveCollectionItem> items;
    result = SemanticToItems(*outcome.semantic, &items);
    if (result.has_value()) {
      const LibraryStatusResult completed = library_->CompleteLiveRefresh(
          id, generation, std::move(items), std::nullopt);
      if (!completed.has_value()) {
        result =
            base::unexpected(LibraryErrorToRuntime(completed.error()));
      }
    } else {
      result = CompleteWithError(
          id, generation, result.error(),
          "The source returned data that cannot safely back a Live Collection.");
    }
  }
  if (callback) {
    std::move(callback).Run(std::move(result));
  }
}

void LiveCollectionCoordinator::OnRefreshTimeout(
    const LiveCollectionId& id,
    uint64_t generation) {
  auto found = pending_.find(id);
  if (found == pending_.end() || found->second->generation != generation) {
    return;
  }
  CapabilityExecutor* executor =
      executors_->Find(found->second->capability, found->second->version);
  if (executor) {
    executor->Cancel(found->second->task_id, found->second->step_id);
  }
  RefreshCallback callback = std::move(found->second->callback);
  pending_.erase(found);
  LiveCollectionRuntimeStatus result = CompleteWithError(
      id, generation, LiveCollectionRuntimeError::kRefreshTimedOut,
      "The collection source timed out.");
  if (callback) {
    std::move(callback).Run(std::move(result));
  }
}

LiveCollectionCoordinator::RefreshCallback
LiveCollectionCoordinator::CancelPending(const LiveCollectionId& id) {
  auto found = pending_.find(id);
  if (found == pending_.end()) {
    return {};
  }
  found->second->timer.Stop();
  if (CapabilityExecutor* executor =
          executors_->Find(found->second->capability,
                           found->second->version)) {
    executor->Cancel(found->second->task_id, found->second->step_id);
  }
  RefreshCallback callback = std::move(found->second->callback);
  pending_.erase(found);
  return callback;
}

void LiveCollectionCoordinator::CancelForWindow(
    const LiveWindowKey& window) {
  std::vector<LiveCollectionId> ids;
  for (const auto& [id, pending] : pending_) {
    if (pending->window == window) {
      ids.push_back(id);
    }
  }
  for (const LiveCollectionId& id : ids) {
    const LiveCollectionRecord* record = library_->FindLiveCollection(id);
    const uint64_t generation =
        record ? record->refresh_generation : 0;
    RefreshCallback callback = CancelPending(id);
    LiveCollectionRuntimeStatus result = base::unexpected(
        LiveCollectionRuntimeError::kWindowUnavailable);
    if (record && generation != 0) {
      result = CompleteWithError(
          id, generation, LiveCollectionRuntimeError::kWindowUnavailable,
          "The collection's browser window was closed.");
    }
    if (callback) {
      std::move(callback).Run(std::move(result));
    }
  }
}

bool LiveCollectionCoordinator::IsRefreshing(
    const LiveCollectionId& id) const {
  return pending_.contains(id);
}

void LiveCollectionCoordinator::Shutdown() {
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  weak_factory_.InvalidateWeakPtrs();
  std::vector<LiveCollectionId> ids;
  for (const auto& [id, pending] : pending_) {
    ids.push_back(id);
  }
  for (const LiveCollectionId& id : ids) {
    RefreshCallback callback = CancelPending(id);
    if (callback) {
      std::move(callback).Run(
          base::unexpected(LiveCollectionRuntimeError::kShuttingDown));
    }
  }
}

}  // namespace seoul
