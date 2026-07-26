// Project Seoul Library, Boards, and Live Collections.
//
// The Library is an index of durable user-owned artifacts. It never duplicates
// browser history, download bytes, page contents, credentials, or provider
// tokens. Boards store authored layout plus references to captures/surfaces;
// Live Collections store provider-neutral definitions and the last verified
// refresh result.

#ifndef SEOUL_BROWSER_LIBRARY_LIBRARY_TYPES_H_
#define SEOUL_BROWSER_LIBRARY_LIBRARY_TYPES_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/uuid.h"
#include "seoul/browser/tools/tool_types.h"

namespace seoul {

#define SEOUL_LIBRARY_ID_TYPE(TypeName)                                 \
  class TypeName {                                                      \
   public:                                                              \
    TypeName() = default;                                               \
    static TypeName GenerateNew() {                                     \
      TypeName id;                                                      \
      id.uuid_ = base::Uuid::GenerateRandomV4();                        \
      return id;                                                        \
    }                                                                   \
    static TypeName FromString(std::string_view value) {                \
      TypeName id;                                                      \
      id.uuid_ = base::Uuid::ParseLowercase(value);                     \
      return id;                                                        \
    }                                                                   \
    bool is_valid() const {                                             \
      return uuid_.is_valid();                                          \
    }                                                                   \
    std::string value() const {                                         \
      return is_valid() ? uuid_.AsLowercaseString() : std::string();    \
    }                                                                   \
    friend bool operator==(const TypeName&, const TypeName&) = default; \
    friend bool operator<(const TypeName& a, const TypeName& b) {       \
      return a.uuid_ < b.uuid_;                                         \
    }                                                                   \
                                                                        \
   private:                                                             \
    base::Uuid uuid_;                                                   \
  }

SEOUL_LIBRARY_ID_TYPE(BoardId);
SEOUL_LIBRARY_ID_TYPE(BoardElementId);
SEOUL_LIBRARY_ID_TYPE(LibraryArtifactId);
SEOUL_LIBRARY_ID_TYPE(LiveCollectionId);

#undef SEOUL_LIBRARY_ID_TYPE

inline constexpr int kLibrarySchemaVersion = 1;
inline constexpr size_t kMaxBoards = 200;
inline constexpr size_t kMaxBoardElements = 500;
inline constexpr size_t kMaxLibraryArtifacts = 2000;
inline constexpr size_t kMaxLiveCollections = 100;
inline constexpr size_t kMaxLiveItemsPerCollection = 500;
inline constexpr size_t kMaxLibraryTitleLength = 200;
inline constexpr size_t kMaxLibraryTextLength = 20000;
inline constexpr size_t kMaxLibraryReferenceLength = 4096;
inline constexpr size_t kMaxProviderIdLength = 120;
inline constexpr int kMinRefreshIntervalMinutes = 5;
inline constexpr int kMaxRefreshIntervalMinutes = 24 * 60;
inline constexpr double kMaxBoardCoordinate = 100000.0;
inline constexpr double kMinBoardElementSize = 16.0;
inline constexpr double kMaxBoardElementSize = 10000.0;

enum class BoardElementKind {
  kText,
  kImageReference,
  kCaptureReference,
  kLink,
  kSurfaceReference,
};

enum class LibraryArtifactKind {
  kCapture,
  kImage,
  kMedia,
  kDownloadReference,
};

enum class LiveRefreshState {
  kIdle,
  kRefreshing,
  kReady,
  kError,
};

struct BoardElement {
  BoardElement();
  BoardElement(const BoardElement&);
  BoardElement(BoardElement&&);
  BoardElement& operator=(const BoardElement&);
  BoardElement& operator=(BoardElement&&);
  ~BoardElement();

  BoardElementId id;
  BoardElementKind kind = BoardElementKind::kText;
  std::string title;
  std::string text;
  std::string reference;
  std::string origin;
  double x = 0.0;
  double y = 0.0;
  double width = 320.0;
  double height = 180.0;
  int z_index = 0;

  friend bool operator==(const BoardElement&, const BoardElement&) = default;
};

struct BoardRecord {
  BoardRecord();
  BoardRecord(const BoardRecord&);
  BoardRecord(BoardRecord&&);
  BoardRecord& operator=(const BoardRecord&);
  BoardRecord& operator=(BoardRecord&&);
  ~BoardRecord();

  BoardId id;
  std::string name;
  std::vector<BoardElement> elements;
  base::Time created_at;
  base::Time modified_at;
  bool archived = false;

  friend bool operator==(const BoardRecord&, const BoardRecord&) = default;
};

// Metadata only. `reference` is a browser-owned file handle, capture id, or
// other opaque durable reference; the Library pref never stores binary bytes.
struct LibraryArtifact {
  LibraryArtifact();
  LibraryArtifact(const LibraryArtifact&);
  LibraryArtifact(LibraryArtifact&&);
  LibraryArtifact& operator=(const LibraryArtifact&);
  LibraryArtifact& operator=(LibraryArtifact&&);
  ~LibraryArtifact();

  LibraryArtifactId id;
  LibraryArtifactKind kind = LibraryArtifactKind::kCapture;
  std::string title;
  std::string reference;
  std::string origin;
  std::string mime_type;
  base::Time created_at;
  bool pinned = false;

  friend bool operator==(const LibraryArtifact&,
                         const LibraryArtifact&) = default;
};

struct LiveCollectionDefinition {
  LiveCollectionDefinition();
  LiveCollectionDefinition(const LiveCollectionDefinition&);
  LiveCollectionDefinition(LiveCollectionDefinition&&);
  LiveCollectionDefinition& operator=(const LiveCollectionDefinition&);
  LiveCollectionDefinition& operator=(LiveCollectionDefinition&&);
  ~LiveCollectionDefinition();

  LiveCollectionId id;
  std::string name;
  // Any registered read-only capability can back a collection. The registry
  // owns its provider, input schema, permissions, freshness, and availability;
  // Library core never branches on a business domain or connector name.
  ToolId refresh_capability;
  // Opaque, non-secret provider locator. The capability adapter validates and
  // maps it into its typed input. Credentials remain in the secure store.
  std::string source_locator;
  // Exact browser window used to establish the collection's permission and
  // page/organization scope. Stored as an opaque live-window key string so
  // Library core does not depend on Chromium lifecycle types. The product
  // coordinator validates and resolves it before every refresh.
  std::string scope_window;
  int refresh_interval_minutes = 15;
  bool enabled = true;

  friend bool operator==(const LiveCollectionDefinition&,
                         const LiveCollectionDefinition&) = default;
};

struct LiveCollectionItem {
  LiveCollectionItem();
  LiveCollectionItem(const LiveCollectionItem&);
  LiveCollectionItem(LiveCollectionItem&&);
  LiveCollectionItem& operator=(const LiveCollectionItem&);
  LiveCollectionItem& operator=(LiveCollectionItem&&);
  ~LiveCollectionItem();

  std::string stable_key;
  std::string title;
  std::string subtitle;
  std::string url;
  std::string status;
  base::Time start_time;
  base::Time end_time;
  bool actionable = false;

  friend bool operator==(const LiveCollectionItem&,
                         const LiveCollectionItem&) = default;
};

struct LiveCollectionRecord {
  LiveCollectionRecord();
  LiveCollectionRecord(const LiveCollectionRecord&);
  LiveCollectionRecord(LiveCollectionRecord&&);
  LiveCollectionRecord& operator=(const LiveCollectionRecord&);
  LiveCollectionRecord& operator=(LiveCollectionRecord&&);
  ~LiveCollectionRecord();

  LiveCollectionDefinition definition;
  std::vector<LiveCollectionItem> items;
  LiveRefreshState refresh_state = LiveRefreshState::kIdle;
  uint64_t refresh_generation = 0;
  base::Time last_attempt_at;
  base::Time last_success_at;
  std::string last_error;

  friend bool operator==(const LiveCollectionRecord&,
                         const LiveCollectionRecord&) = default;
};

enum class LibraryError {
  kInvalidId,
  kInvalidName,
  kInvalidElement,
  kInvalidArtifact,
  kInvalidCollection,
  kInvalidLiveItem,
  kUnknownBoard,
  kUnknownElement,
  kUnknownArtifact,
  kUnknownCollection,
  kBoardArchived,
  kLimitExceeded,
  kStaleRefresh,
  kUnsupportedSchema,
};

const char* LibraryErrorToString(LibraryError error);

template <typename T>
using LibraryResult = base::expected<T, LibraryError>;

using LibraryStatusResult = base::expected<void, LibraryError>;

}  // namespace seoul

#endif  // SEOUL_BROWSER_LIBRARY_LIBRARY_TYPES_H_
