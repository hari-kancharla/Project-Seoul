// Project Seoul Preview lifecycle types.
// A Preview is an ephemeral, non-tab browsing surface owned by one live
// window and parent tab. It can end only by dismissal or an explicit,
// two-phase promotion to a normal tab/split.

#ifndef SEOUL_BROWSER_PREVIEW_PREVIEW_TYPES_H_
#define SEOUL_BROWSER_PREVIEW_PREVIEW_TYPES_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/uuid.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "url/gurl.h"

namespace seoul {

class PreviewId {
 public:
  PreviewId() = default;
  static PreviewId GenerateNew();
  static PreviewId FromString(std::string_view value);
  bool is_valid() const { return uuid_.is_valid(); }
  std::string value() const;

  friend bool operator==(const PreviewId&, const PreviewId&) = default;
  friend bool operator<(const PreviewId& a, const PreviewId& b) {
    return a.uuid_ < b.uuid_;
  }

 private:
  base::Uuid uuid_;
};

inline constexpr size_t kMaxConcurrentPreviews = 32;
inline constexpr int kMaxPreviewNavigations = 32;

enum class PreviewState {
  kLoading,
  kReady,
  kFailed,
  kPromoting,
};

enum class PreviewPromotionTarget {
  kTab,
  kSplit,
};

enum class PreviewDismissReason {
  kUserDismissed,
  kReplaced,
  kParentTabRemoved,
  kWindowClosed,
  kCrashed,
};

enum class PreviewError {
  kInvalidId,
  kInvalidParent,
  kUnsafeUrl,
  kUnknownPreview,
  kInvalidState,
  kLimitExceeded,
  kNavigationLimitExceeded,
  kRoutingRejected,
  kPromotionFailed,
};

const char* PreviewErrorToString(PreviewError error);

struct PreviewRecord {
  PreviewRecord();
  PreviewRecord(const PreviewRecord&);
  PreviewRecord(PreviewRecord&&);
  PreviewRecord& operator=(const PreviewRecord&);
  PreviewRecord& operator=(PreviewRecord&&);
  ~PreviewRecord();
  PreviewId id;
  LiveWindowKey window;
  LiveTabKey parent_tab;
  GURL initial_url;
  GURL current_url;
  PreviewState state = PreviewState::kLoading;
  std::optional<PreviewPromotionTarget> promotion_target;
  base::Time created_at;
  int navigation_count = 0;

  friend bool operator==(const PreviewRecord&, const PreviewRecord&) = default;
};

struct PreviewOpenResult {
  PreviewOpenResult();
  PreviewOpenResult(const PreviewOpenResult&);
  PreviewOpenResult(PreviewOpenResult&&);
  PreviewOpenResult& operator=(const PreviewOpenResult&);
  PreviewOpenResult& operator=(PreviewOpenResult&&);
  ~PreviewOpenResult();
  PreviewId id;
  std::optional<PreviewId> replaced;
};

template <typename T>
using PreviewResult = base::expected<T, PreviewError>;

using PreviewStatusResult = base::expected<void, PreviewError>;

}  // namespace seoul

#endif  // SEOUL_BROWSER_PREVIEW_PREVIEW_TYPES_H_
