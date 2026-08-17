// Project Seoul Library, Boards, and Live Collections.

#include "seoul/browser/library/library_service.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include "base/json/values_util.h"
#include "base/strings/string_number_conversions.h"
#include "url/gurl.h"

namespace seoul {
namespace {

bool ValidText(const std::string& value, size_t max_length, bool allow_empty) {
  return (allow_empty || !value.empty()) && value.size() <= max_length;
}

bool ValidHttpUrl(const std::string& value) {
  const GURL url(value);
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

bool ValidOptionalHttpUrl(const std::string& value) {
  return value.empty() || ValidHttpUrl(value);
}

std::string StringOrEmpty(const base::DictValue& value, const char* key) {
  const std::string* found = value.FindString(key);
  return found ? *found : std::string();
}

const char* BoardElementKindKey(BoardElementKind kind) {
  switch (kind) {
    case BoardElementKind::kText:
      return "text";
    case BoardElementKind::kImageReference:
      return "image_reference";
    case BoardElementKind::kCaptureReference:
      return "capture_reference";
    case BoardElementKind::kLink:
      return "link";
    case BoardElementKind::kSurfaceReference:
      return "surface_reference";
  }
  return "text";
}

std::optional<BoardElementKind> BoardElementKindFromKey(
    const std::string& key) {
  static constexpr BoardElementKind kKinds[] = {
      BoardElementKind::kText, BoardElementKind::kImageReference,
      BoardElementKind::kCaptureReference, BoardElementKind::kLink,
      BoardElementKind::kSurfaceReference};
  for (BoardElementKind kind : kKinds) {
    if (key == BoardElementKindKey(kind)) {
      return kind;
    }
  }
  return std::nullopt;
}

const char* ArtifactKindKey(LibraryArtifactKind kind) {
  switch (kind) {
    case LibraryArtifactKind::kCapture:
      return "capture";
    case LibraryArtifactKind::kImage:
      return "image";
    case LibraryArtifactKind::kMedia:
      return "media";
    case LibraryArtifactKind::kDownloadReference:
      return "download_reference";
  }
  return "capture";
}

std::optional<LibraryArtifactKind> ArtifactKindFromKey(const std::string& key) {
  static constexpr LibraryArtifactKind kKinds[] = {
      LibraryArtifactKind::kCapture, LibraryArtifactKind::kImage,
      LibraryArtifactKind::kMedia, LibraryArtifactKind::kDownloadReference};
  for (LibraryArtifactKind kind : kKinds) {
    if (key == ArtifactKindKey(kind)) {
      return kind;
    }
  }
  return std::nullopt;
}

const char* RefreshStateKey(LiveRefreshState state) {
  switch (state) {
    case LiveRefreshState::kIdle:
      return "idle";
    case LiveRefreshState::kRefreshing:
      return "refreshing";
    case LiveRefreshState::kReady:
      return "ready";
    case LiveRefreshState::kError:
      return "error";
  }
  return "idle";
}

LiveRefreshState RefreshStateFromKey(const std::string& key) {
  if (key == "ready") {
    return LiveRefreshState::kReady;
  }
  if (key == "error") {
    return LiveRefreshState::kError;
  }
  // A persisted active refresh cannot still be running after restart.
  return LiveRefreshState::kIdle;
}

base::DictValue BoardElementToValue(const BoardElement& element) {
  base::DictValue value;
  value.Set("id", element.id.value());
  value.Set("kind", BoardElementKindKey(element.kind));
  value.Set("title", element.title);
  value.Set("text", element.text);
  value.Set("reference", element.reference);
  value.Set("origin", element.origin);
  value.Set("x", element.x);
  value.Set("y", element.y);
  value.Set("width", element.width);
  value.Set("height", element.height);
  value.Set("z_index", element.z_index);
  return value;
}

std::optional<BoardElement> BoardElementFromValue(
    const base::DictValue& value) {
  const std::string* id = value.FindString("id");
  const std::string* kind_key = value.FindString("kind");
  std::optional<BoardElementKind> kind =
      kind_key ? BoardElementKindFromKey(*kind_key) : std::nullopt;
  if (!id || !kind.has_value()) {
    return std::nullopt;
  }
  BoardElement element;
  element.id = BoardElementId::FromString(*id);
  element.kind = *kind;
  element.title = StringOrEmpty(value, "title");
  element.text = StringOrEmpty(value, "text");
  element.reference = StringOrEmpty(value, "reference");
  element.origin = StringOrEmpty(value, "origin");
  element.x = value.FindDouble("x").value_or(0.0);
  element.y = value.FindDouble("y").value_or(0.0);
  element.width = value.FindDouble("width").value_or(320.0);
  element.height = value.FindDouble("height").value_or(180.0);
  element.z_index = value.FindInt("z_index").value_or(0);
  return element;
}

base::DictValue LiveItemToValue(const LiveCollectionItem& item) {
  base::DictValue value;
  value.Set("stable_key", item.stable_key);
  value.Set("title", item.title);
  value.Set("subtitle", item.subtitle);
  value.Set("url", item.url);
  value.Set("status", item.status);
  value.Set("start_time", base::TimeToValue(item.start_time));
  value.Set("end_time", base::TimeToValue(item.end_time));
  value.Set("actionable", item.actionable);
  return value;
}

std::optional<LiveCollectionItem> LiveItemFromValue(
    const base::DictValue& value) {
  const std::string* stable_key = value.FindString("stable_key");
  const std::string* title = value.FindString("title");
  if (!stable_key || !title) {
    return std::nullopt;
  }
  LiveCollectionItem item;
  item.stable_key = *stable_key;
  item.title = *title;
  item.subtitle = StringOrEmpty(value, "subtitle");
  item.url = StringOrEmpty(value, "url");
  item.status = StringOrEmpty(value, "status");
  if (const base::Value* start = value.Find("start_time")) {
    item.start_time = base::ValueToTime(*start).value_or(base::Time());
  }
  if (const base::Value* end = value.Find("end_time")) {
    item.end_time = base::ValueToTime(*end).value_or(base::Time());
  }
  item.actionable = value.FindBool("actionable").value_or(false);
  return item;
}

}  // namespace

const char* LibraryErrorToString(LibraryError error) {
  switch (error) {
    case LibraryError::kInvalidId:
      return "invalid_id";
    case LibraryError::kInvalidName:
      return "invalid_name";
    case LibraryError::kInvalidElement:
      return "invalid_element";
    case LibraryError::kInvalidArtifact:
      return "invalid_artifact";
    case LibraryError::kInvalidCollection:
      return "invalid_collection";
    case LibraryError::kInvalidLiveItem:
      return "invalid_live_item";
    case LibraryError::kUnknownBoard:
      return "unknown_board";
    case LibraryError::kUnknownElement:
      return "unknown_element";
    case LibraryError::kUnknownArtifact:
      return "unknown_artifact";
    case LibraryError::kUnknownCollection:
      return "unknown_collection";
    case LibraryError::kBoardArchived:
      return "board_archived";
    case LibraryError::kLimitExceeded:
      return "limit_exceeded";
    case LibraryError::kStaleRefresh:
      return "stale_refresh";
    case LibraryError::kUnsupportedSchema:
      return "unsupported_schema";
  }
  return "invalid_collection";
}

LibraryService::LibraryService(Clock clock, base::RepeatingClosure changed)
    : clock_(std::move(clock)), changed_(std::move(changed)) {}
LibraryService::~LibraryService() = default;

base::Time LibraryService::Now() const {
  return clock_ ? clock_.Run() : base::Time::Now();
}

void LibraryService::NotifyChanged() {
  // Zero represents the initial restored state. Saturate instead of wrapping
  // so snapshot ordering can never move backward, even at the type boundary.
  if (revision_ < std::numeric_limits<uint64_t>::max()) {
    revision_ = revision_ + 1;
  }
  if (changed_) {
    changed_.Run();
  }
  for (LibraryServiceObserver& observer : observers_) {
    observer.OnLibraryChanged(revision_);
  }
}

void LibraryService::AddObserver(LibraryServiceObserver* observer) {
  observers_.AddObserver(observer);
}

void LibraryService::RemoveObserver(LibraryServiceObserver* observer) {
  observers_.RemoveObserver(observer);
}

LibraryResult<BoardId> LibraryService::CreateBoard(const std::string& name) {
  if (!ValidText(name, kMaxLibraryTitleLength, false)) {
    return base::unexpected(LibraryError::kInvalidName);
  }
  if (boards_.size() >= kMaxBoards) {
    return base::unexpected(LibraryError::kLimitExceeded);
  }
  BoardRecord board;
  board.id = BoardId::GenerateNew();
  board.name = name;
  board.created_at = board.modified_at = Now();
  const BoardId id = board.id;
  boards_.emplace(id, std::move(board));
  NotifyChanged();
  return id;
}

LibraryStatusResult LibraryService::RenameBoard(const BoardId& id,
                                                const std::string& name) {
  auto it = boards_.find(id);
  if (it == boards_.end()) {
    return base::unexpected(LibraryError::kUnknownBoard);
  }
  if (it->second.archived) {
    return base::unexpected(LibraryError::kBoardArchived);
  }
  if (!ValidText(name, kMaxLibraryTitleLength, false)) {
    return base::unexpected(LibraryError::kInvalidName);
  }
  it->second.name = name;
  it->second.modified_at = Now();
  NotifyChanged();
  return base::ok();
}

LibraryStatusResult LibraryService::SetBoardArchived(const BoardId& id,
                                                     bool archived) {
  auto it = boards_.find(id);
  if (it == boards_.end()) {
    return base::unexpected(LibraryError::kUnknownBoard);
  }
  it->second.archived = archived;
  it->second.modified_at = Now();
  NotifyChanged();
  return base::ok();
}

LibraryStatusResult LibraryService::DeleteBoard(const BoardId& id) {
  if (boards_.erase(id) == 0) {
    return base::unexpected(LibraryError::kUnknownBoard);
  }
  NotifyChanged();
  return base::ok();
}

LibraryStatusResult LibraryService::ValidateBoardElement(
    const BoardElement& element) const {
  if (!element.id.is_valid() ||
      !ValidText(element.title, kMaxLibraryTitleLength, true) ||
      !ValidText(element.text, kMaxLibraryTextLength, true) ||
      !ValidText(element.reference, kMaxLibraryReferenceLength, true) ||
      !ValidText(element.origin, kMaxLibraryReferenceLength, true) ||
      !std::isfinite(element.x) || !std::isfinite(element.y) ||
      !std::isfinite(element.width) || !std::isfinite(element.height) ||
      std::abs(element.x) > kMaxBoardCoordinate ||
      std::abs(element.y) > kMaxBoardCoordinate ||
      element.width < kMinBoardElementSize ||
      element.height < kMinBoardElementSize ||
      element.width > kMaxBoardElementSize ||
      element.height > kMaxBoardElementSize) {
    return base::unexpected(LibraryError::kInvalidElement);
  }
  switch (element.kind) {
    case BoardElementKind::kText:
      if (element.text.empty()) {
        return base::unexpected(LibraryError::kInvalidElement);
      }
      break;
    case BoardElementKind::kLink:
      if (!ValidHttpUrl(element.reference)) {
        return base::unexpected(LibraryError::kInvalidElement);
      }
      break;
    case BoardElementKind::kImageReference:
    case BoardElementKind::kCaptureReference:
    case BoardElementKind::kSurfaceReference:
      if (element.reference.empty()) {
        return base::unexpected(LibraryError::kInvalidElement);
      }
      break;
  }
  return base::ok();
}

LibraryResult<BoardElementId> LibraryService::AddBoardElement(
    const BoardId& board_id,
    BoardElement element) {
  auto it = boards_.find(board_id);
  if (it == boards_.end()) {
    return base::unexpected(LibraryError::kUnknownBoard);
  }
  if (it->second.archived) {
    return base::unexpected(LibraryError::kBoardArchived);
  }
  if (it->second.elements.size() >= kMaxBoardElements) {
    return base::unexpected(LibraryError::kLimitExceeded);
  }
  if (!element.id.is_valid()) {
    element.id = BoardElementId::GenerateNew();
  }
  if (!ValidateBoardElement(element).has_value()) {
    return base::unexpected(LibraryError::kInvalidElement);
  }
  for (const BoardElement& existing : it->second.elements) {
    if (existing.id == element.id) {
      return base::unexpected(LibraryError::kInvalidId);
    }
  }
  const BoardElementId id = element.id;
  it->second.elements.push_back(std::move(element));
  it->second.modified_at = Now();
  NotifyChanged();
  return id;
}

LibraryResult<BoardElementId> LibraryService::PlaceCaptureOnBoard(
    const BoardId& board_id,
    const LibraryArtifactId& artifact_id,
    double x,
    double y) {
  const LibraryArtifact* const artifact = FindArtifact(artifact_id);
  if (!artifact) {
    return base::unexpected(LibraryError::kUnknownArtifact);
  }
  if (artifact->kind != LibraryArtifactKind::kCapture) {
    // Only a capture goes on as a capture reference. Anything else would
    // render as a picture the board cannot actually show.
    return base::unexpected(LibraryError::kInvalidArtifact);
  }
  BoardElement element;
  element.id = BoardElementId::GenerateNew();
  element.kind = BoardElementKind::kCaptureReference;
  element.title = artifact->title;
  // The board points at the Library's artifact; it never copies the image.
  element.reference = artifact->id.value();
  element.origin = artifact->origin;
  element.x = x;
  element.y = y;
  return AddBoardElement(board_id, std::move(element));
}

LibraryStatusResult LibraryService::UpdateBoardElement(const BoardId& board_id,
                                                       BoardElement element) {
  auto board = boards_.find(board_id);
  if (board == boards_.end()) {
    return base::unexpected(LibraryError::kUnknownBoard);
  }
  if (board->second.archived) {
    return base::unexpected(LibraryError::kBoardArchived);
  }
  if (!ValidateBoardElement(element).has_value()) {
    return base::unexpected(LibraryError::kInvalidElement);
  }
  auto existing =
      std::find_if(board->second.elements.begin(), board->second.elements.end(),
                   [&](const BoardElement& candidate) {
                     return candidate.id == element.id;
                   });
  if (existing == board->second.elements.end()) {
    return base::unexpected(LibraryError::kUnknownElement);
  }
  *existing = std::move(element);
  board->second.modified_at = Now();
  NotifyChanged();
  return base::ok();
}

LibraryStatusResult LibraryService::RemoveBoardElement(
    const BoardId& board_id,
    const BoardElementId& element_id) {
  auto board = boards_.find(board_id);
  if (board == boards_.end()) {
    return base::unexpected(LibraryError::kUnknownBoard);
  }
  if (board->second.archived) {
    return base::unexpected(LibraryError::kBoardArchived);
  }
  auto& elements = board->second.elements;
  const size_t old_size = elements.size();
  std::erase_if(elements, [&](const BoardElement& element) {
    return element.id == element_id;
  });
  if (elements.size() == old_size) {
    return base::unexpected(LibraryError::kUnknownElement);
  }
  board->second.modified_at = Now();
  NotifyChanged();
  return base::ok();
}

LibraryStatusResult LibraryService::ValidateArtifact(
    const LibraryArtifact& artifact) const {
  if (!artifact.id.is_valid() ||
      !ValidText(artifact.title, kMaxLibraryTitleLength, false) ||
      !ValidText(artifact.reference, kMaxLibraryReferenceLength, false) ||
      !ValidText(artifact.origin, kMaxLibraryReferenceLength, true) ||
      !ValidText(artifact.mime_type, kMaxProviderIdLength, true)) {
    return base::unexpected(LibraryError::kInvalidArtifact);
  }
  return base::ok();
}

LibraryResult<LibraryArtifactId> LibraryService::AddArtifact(
    LibraryArtifact artifact) {
  if (artifacts_.size() >= kMaxLibraryArtifacts) {
    return base::unexpected(LibraryError::kLimitExceeded);
  }
  if (!artifact.id.is_valid()) {
    artifact.id = LibraryArtifactId::GenerateNew();
  }
  if (artifact.created_at.is_null()) {
    artifact.created_at = Now();
  }
  if (!ValidateArtifact(artifact).has_value() ||
      artifacts_.contains(artifact.id)) {
    return base::unexpected(LibraryError::kInvalidArtifact);
  }
  const LibraryArtifactId id = artifact.id;
  artifacts_.emplace(id, std::move(artifact));
  NotifyChanged();
  return id;
}

LibraryStatusResult LibraryService::RemoveArtifact(
    const LibraryArtifactId& id) {
  if (artifacts_.erase(id) == 0) {
    return base::unexpected(LibraryError::kUnknownArtifact);
  }
  NotifyChanged();
  return base::ok();
}

LibraryStatusResult LibraryService::ValidateLiveCollection(
    const LiveCollectionDefinition& definition) const {
  if (!definition.id.is_valid() ||
      !ValidText(definition.name, kMaxLibraryTitleLength, false) ||
      !definition.refresh_capability.is_valid() ||
      !ValidText(definition.source_locator, kMaxLibraryReferenceLength, true) ||
      !ValidText(definition.scope_window, kMaxProviderIdLength, true) ||
      definition.refresh_interval_minutes < kMinRefreshIntervalMinutes ||
      definition.refresh_interval_minutes > kMaxRefreshIntervalMinutes) {
    return base::unexpected(LibraryError::kInvalidCollection);
  }
  return base::ok();
}

LibraryResult<LiveCollectionId> LibraryService::CreateLiveCollection(
    LiveCollectionDefinition definition) {
  if (live_collections_.size() >= kMaxLiveCollections) {
    return base::unexpected(LibraryError::kLimitExceeded);
  }
  if (!definition.id.is_valid()) {
    definition.id = LiveCollectionId::GenerateNew();
  }
  if (!ValidateLiveCollection(definition).has_value() ||
      live_collections_.contains(definition.id)) {
    return base::unexpected(LibraryError::kInvalidCollection);
  }
  LiveCollectionRecord record;
  record.definition = std::move(definition);
  const LiveCollectionId id = record.definition.id;
  live_collections_.emplace(id, std::move(record));
  NotifyChanged();
  return id;
}

LibraryStatusResult LibraryService::UpdateLiveCollection(
    LiveCollectionDefinition definition) {
  auto it = live_collections_.find(definition.id);
  if (it == live_collections_.end()) {
    return base::unexpected(LibraryError::kUnknownCollection);
  }
  if (!ValidateLiveCollection(definition).has_value()) {
    return base::unexpected(LibraryError::kInvalidCollection);
  }
  const bool execution_changed =
      it->second.definition.refresh_capability !=
          definition.refresh_capability ||
      it->second.definition.source_locator != definition.source_locator ||
      it->second.definition.scope_window != definition.scope_window ||
      (it->second.definition.enabled && !definition.enabled);
  if (execution_changed &&
      it->second.refresh_state == LiveRefreshState::kRefreshing) {
    // The old adapter may still call back. Leaving the generation unchanged
    // but exiting kRefreshing makes that completion stale without consuming a
    // new token or guessing whether cancellation reached the provider.
    it->second.refresh_state = LiveRefreshState::kIdle;
    it->second.last_error.clear();
  }
  it->second.definition = std::move(definition);
  NotifyChanged();
  return base::ok();
}

LibraryStatusResult LibraryService::DeleteLiveCollection(
    const LiveCollectionId& id) {
  if (live_collections_.erase(id) == 0) {
    return base::unexpected(LibraryError::kUnknownCollection);
  }
  NotifyChanged();
  return base::ok();
}

LibraryResult<uint64_t> LibraryService::BeginLiveRefresh(
    const LiveCollectionId& id) {
  auto it = live_collections_.find(id);
  if (it == live_collections_.end()) {
    return base::unexpected(LibraryError::kUnknownCollection);
  }
  if (!it->second.definition.enabled) {
    return base::unexpected(LibraryError::kInvalidCollection);
  }
  if (it->second.refresh_generation == std::numeric_limits<uint64_t>::max()) {
    return base::unexpected(LibraryError::kLimitExceeded);
  }
  it->second.refresh_state = LiveRefreshState::kRefreshing;
  it->second.last_attempt_at = Now();
  it->second.last_error.clear();
  const uint64_t generation = ++it->second.refresh_generation;
  NotifyChanged();
  return generation;
}

LibraryStatusResult LibraryService::ValidateLiveItem(
    const LiveCollectionItem& item) const {
  if (!ValidText(item.stable_key, kMaxProviderIdLength, false) ||
      !ValidText(item.title, kMaxLibraryTitleLength, false) ||
      !ValidText(item.subtitle, kMaxLibraryTextLength, true) ||
      !ValidText(item.url, kMaxLibraryReferenceLength, true) ||
      !ValidText(item.status, kMaxProviderIdLength, true) ||
      !ValidOptionalHttpUrl(item.url) ||
      (item.actionable && item.url.empty()) ||
      (!item.start_time.is_null() && !item.end_time.is_null() &&
       item.end_time < item.start_time)) {
    return base::unexpected(LibraryError::kInvalidLiveItem);
  }
  return base::ok();
}

LibraryStatusResult LibraryService::CompleteLiveRefresh(
    const LiveCollectionId& id,
    uint64_t generation,
    std::vector<LiveCollectionItem> items,
    const std::optional<std::string>& error) {
  auto it = live_collections_.find(id);
  if (it == live_collections_.end()) {
    return base::unexpected(LibraryError::kUnknownCollection);
  }
  LiveCollectionRecord& record = it->second;
  if (generation == 0 || generation != record.refresh_generation ||
      record.refresh_state != LiveRefreshState::kRefreshing) {
    return base::unexpected(LibraryError::kStaleRefresh);
  }
  if (error.has_value()) {
    record.refresh_state = LiveRefreshState::kError;
    record.last_error = error->substr(0, kMaxLibraryTitleLength);
    NotifyChanged();
    return base::ok();
  }
  if (items.size() > kMaxLiveItemsPerCollection) {
    record.refresh_state = LiveRefreshState::kError;
    record.last_error = "Provider returned too many items.";
    NotifyChanged();
    return base::unexpected(LibraryError::kLimitExceeded);
  }
  std::set<std::string> keys;
  for (const LiveCollectionItem& item : items) {
    if (!ValidateLiveItem(item).has_value() ||
        !keys.insert(item.stable_key).second) {
      record.refresh_state = LiveRefreshState::kError;
      record.last_error = "Provider returned invalid or duplicate items.";
      NotifyChanged();
      return base::unexpected(LibraryError::kInvalidLiveItem);
    }
  }
  record.items = std::move(items);
  record.refresh_state = LiveRefreshState::kReady;
  record.last_success_at = Now();
  record.last_error.clear();
  NotifyChanged();
  return base::ok();
}

bool LibraryService::IsRefreshDue(const LiveCollectionId& id,
                                  base::Time now) const {
  auto it = live_collections_.find(id);
  if (it == live_collections_.end() || !it->second.definition.enabled ||
      it->second.refresh_state == LiveRefreshState::kRefreshing) {
    return false;
  }
  const base::Time reference =
      std::max(it->second.last_success_at, it->second.last_attempt_at);
  if (reference.is_null()) {
    return true;
  }
  return now - reference >=
         base::Minutes(it->second.definition.refresh_interval_minutes);
}

const BoardRecord* LibraryService::FindBoard(const BoardId& id) const {
  auto it = boards_.find(id);
  return it == boards_.end() ? nullptr : &it->second;
}

const LibraryArtifact* LibraryService::FindArtifact(
    const LibraryArtifactId& id) const {
  auto it = artifacts_.find(id);
  return it == artifacts_.end() ? nullptr : &it->second;
}

const LiveCollectionRecord* LibraryService::FindLiveCollection(
    const LiveCollectionId& id) const {
  auto it = live_collections_.find(id);
  return it == live_collections_.end() ? nullptr : &it->second;
}

std::vector<BoardId> LibraryService::Boards() const {
  std::vector<BoardId> result;
  for (const auto& [id, board] : boards_) {
    result.push_back(id);
  }
  return result;
}

std::vector<LibraryArtifactId> LibraryService::Artifacts() const {
  std::vector<LibraryArtifactId> result;
  for (const auto& [id, artifact] : artifacts_) {
    result.push_back(id);
  }
  return result;
}

std::vector<LiveCollectionId> LibraryService::LiveCollections() const {
  std::vector<LiveCollectionId> result;
  for (const auto& [id, collection] : live_collections_) {
    result.push_back(id);
  }
  return result;
}

base::DictValue LibraryService::TakePersistedState() const {
  base::DictValue state;
  state.Set("schema_version", kLibrarySchemaVersion);
  base::ListValue boards;
  for (const auto& [id, board] : boards_) {
    base::DictValue value;
    value.Set("id", id.value());
    value.Set("name", board.name);
    value.Set("created_at", base::TimeToValue(board.created_at));
    value.Set("modified_at", base::TimeToValue(board.modified_at));
    value.Set("archived", board.archived);
    base::ListValue elements;
    for (const BoardElement& element : board.elements) {
      elements.Append(BoardElementToValue(element));
    }
    value.Set("elements", std::move(elements));
    boards.Append(std::move(value));
  }
  state.Set("boards", std::move(boards));

  base::ListValue artifacts;
  for (const auto& [id, artifact] : artifacts_) {
    base::DictValue value;
    value.Set("id", id.value());
    value.Set("kind", ArtifactKindKey(artifact.kind));
    value.Set("title", artifact.title);
    value.Set("reference", artifact.reference);
    value.Set("origin", artifact.origin);
    value.Set("mime_type", artifact.mime_type);
    value.Set("created_at", base::TimeToValue(artifact.created_at));
    value.Set("pinned", artifact.pinned);
    artifacts.Append(std::move(value));
  }
  state.Set("artifacts", std::move(artifacts));

  base::ListValue collections;
  for (const auto& [id, collection] : live_collections_) {
    base::DictValue value;
    value.Set("id", id.value());
    value.Set("name", collection.definition.name);
    value.Set("refresh_capability",
              collection.definition.refresh_capability.value());
    value.Set("source_locator", collection.definition.source_locator);
    value.Set("scope_window", collection.definition.scope_window);
    value.Set("refresh_interval_minutes",
              collection.definition.refresh_interval_minutes);
    value.Set("enabled", collection.definition.enabled);
    value.Set("refresh_state", RefreshStateKey(collection.refresh_state));
    // JSON numbers cannot exactly represent every uint64_t. Persist the token
    // as text so stale-response protection survives long-lived profiles.
    value.Set("refresh_generation",
              base::NumberToString(collection.refresh_generation));
    value.Set("last_attempt_at", base::TimeToValue(collection.last_attempt_at));
    value.Set("last_success_at", base::TimeToValue(collection.last_success_at));
    value.Set("last_error", collection.last_error);
    base::ListValue items;
    for (const LiveCollectionItem& item : collection.items) {
      items.Append(LiveItemToValue(item));
    }
    value.Set("items", std::move(items));
    collections.Append(std::move(value));
  }
  state.Set("live_collections", std::move(collections));
  return state;
}

void LibraryService::RestorePersistedState(const base::DictValue& state) {
  if (state.FindInt("schema_version").value_or(0) != kLibrarySchemaVersion) {
    return;
  }

  if (const base::ListValue* boards = state.FindList("boards")) {
    for (const base::Value& entry : *boards) {
      if (boards_.size() >= kMaxBoards) {
        break;
      }
      const base::DictValue* value = entry.GetIfDict();
      const std::string* id = value ? value->FindString("id") : nullptr;
      const std::string* name = value ? value->FindString("name") : nullptr;
      if (!value || !id || !name ||
          !ValidText(*name, kMaxLibraryTitleLength, false)) {
        continue;
      }
      BoardRecord board;
      board.id = BoardId::FromString(*id);
      board.name = *name;
      if (!board.id.is_valid()) {
        continue;
      }
      if (const base::Value* created = value->Find("created_at")) {
        board.created_at = base::ValueToTime(*created).value_or(Now());
      }
      if (const base::Value* modified = value->Find("modified_at")) {
        board.modified_at = base::ValueToTime(*modified).value_or(Now());
      }
      board.archived = value->FindBool("archived").value_or(false);
      if (const base::ListValue* elements = value->FindList("elements")) {
        std::set<BoardElementId> element_ids;
        for (const base::Value& element_entry : *elements) {
          if (board.elements.size() >= kMaxBoardElements) {
            break;
          }
          const base::DictValue* element_value = element_entry.GetIfDict();
          std::optional<BoardElement> element =
              element_value ? BoardElementFromValue(*element_value)
                            : std::nullopt;
          if (element.has_value() &&
              ValidateBoardElement(*element).has_value() &&
              element_ids.insert(element->id).second) {
            board.elements.push_back(std::move(*element));
          }
        }
      }
      boards_.emplace(board.id, std::move(board));
    }
  }

  if (const base::ListValue* artifacts = state.FindList("artifacts")) {
    for (const base::Value& entry : *artifacts) {
      if (artifacts_.size() >= kMaxLibraryArtifacts) {
        break;
      }
      const base::DictValue* value = entry.GetIfDict();
      const std::string* id = value ? value->FindString("id") : nullptr;
      const std::string* kind_key = value ? value->FindString("kind") : nullptr;
      std::optional<LibraryArtifactKind> kind =
          kind_key ? ArtifactKindFromKey(*kind_key) : std::nullopt;
      if (!value || !id || !kind.has_value()) {
        continue;
      }
      LibraryArtifact artifact;
      artifact.id = LibraryArtifactId::FromString(*id);
      artifact.kind = *kind;
      artifact.title = StringOrEmpty(*value, "title");
      artifact.reference = StringOrEmpty(*value, "reference");
      artifact.origin = StringOrEmpty(*value, "origin");
      artifact.mime_type = StringOrEmpty(*value, "mime_type");
      if (const base::Value* created = value->Find("created_at")) {
        artifact.created_at = base::ValueToTime(*created).value_or(Now());
      }
      artifact.pinned = value->FindBool("pinned").value_or(false);
      if (ValidateArtifact(artifact).has_value()) {
        artifacts_.emplace(artifact.id, std::move(artifact));
      }
    }
  }

  if (const base::ListValue* collections = state.FindList("live_collections")) {
    for (const base::Value& entry : *collections) {
      if (live_collections_.size() >= kMaxLiveCollections) {
        break;
      }
      const base::DictValue* value = entry.GetIfDict();
      const std::string* id = value ? value->FindString("id") : nullptr;
      const std::string* capability =
          value ? value->FindString("refresh_capability") : nullptr;
      if (!value || !id || !capability) {
        continue;
      }
      LiveCollectionRecord record;
      record.definition.id = LiveCollectionId::FromString(*id);
      record.definition.name = StringOrEmpty(*value, "name");
      record.definition.refresh_capability = ToolId::FromString(*capability);
      record.definition.source_locator =
          StringOrEmpty(*value, "source_locator");
      record.definition.scope_window = StringOrEmpty(*value, "scope_window");
      record.definition.refresh_interval_minutes =
          value->FindInt("refresh_interval_minutes").value_or(15);
      record.definition.enabled = value->FindBool("enabled").value_or(true);
      if (!ValidateLiveCollection(record.definition).has_value()) {
        continue;
      }
      record.refresh_state =
          RefreshStateFromKey(StringOrEmpty(*value, "refresh_state"));
      if (const std::string* generation =
              value->FindString("refresh_generation")) {
        base::StringToUint64(*generation, &record.refresh_generation);
      }
      if (const base::Value* attempt = value->Find("last_attempt_at")) {
        record.last_attempt_at =
            base::ValueToTime(*attempt).value_or(base::Time());
      }
      if (const base::Value* success = value->Find("last_success_at")) {
        record.last_success_at =
            base::ValueToTime(*success).value_or(base::Time());
      }
      record.last_error =
          StringOrEmpty(*value, "last_error").substr(0, kMaxLibraryTitleLength);
      if (const base::ListValue* items = value->FindList("items")) {
        std::set<std::string> keys;
        for (const base::Value& item_entry : *items) {
          if (record.items.size() >= kMaxLiveItemsPerCollection) {
            break;
          }
          const base::DictValue* item_value = item_entry.GetIfDict();
          std::optional<LiveCollectionItem> item =
              item_value ? LiveItemFromValue(*item_value) : std::nullopt;
          if (item.has_value() && ValidateLiveItem(*item).has_value() &&
              keys.insert(item->stable_key).second) {
            record.items.push_back(std::move(*item));
          }
        }
      }
      live_collections_.emplace(record.definition.id, std::move(record));
    }
  }
}

}  // namespace seoul
