// Project Seoul native organization engine.
// Precise mutation results. Every mutation returns either a value or a specific
// OrganizationError; there is no single generic failure. A failed mutation
// never leaves partial state (mutations validate fully before committing).

#ifndef SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_ERRORS_H_
#define SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_ERRORS_H_

#include "base/types/expected.h"

namespace seoul {

enum class OrganizationError {
  kInvalidId,
  kInvalidName,
  kInvalidIcon,
  kInvalidUrl,
  kInvalidOrder,
  kWorkspaceNotFound,
  kEssentialNotFound,
  kTabMembershipNotFound,
  kSplitGroupNotFound,
  kRoutingRuleNotFound,
  kDefaultWorkspaceProtected,
  kArchivedWorkspaceCannotActivate,
  kDuplicateEssential,
  kDuplicateMembership,
  kCrossProfileReference,
  kCrossWorkspaceSplit,
  kInvalidSplitArity,
  kInvalidDividerRatio,
  kInvalidActivePane,
  kInvalidUpstreamSplitToken,
  kDuplicateSplitToken,
  kInvalidRoutingRule,
  kUnsupportedSchema,
  kLimitExceeded,
  kProtectedTemporaryTab,
  kProtectedPreview,
  kPersistenceFailure,
  kCorruptState,
  kResourceInUse,
  kNoOpRejected,  // a mutation that would change nothing because of invalid
                  // state
};

const char* OrganizationErrorToString(OrganizationError error);

// A mutation that returns a value on success or a specific error on failure.
template <typename T>
using MutationResult = base::expected<T, OrganizationError>;

// A mutation with no return value on success. .has_value() == success.
using MutationStatus = base::expected<void, OrganizationError>;

inline MutationStatus Ok() {
  return base::ok();
}

inline base::unexpected<OrganizationError> Err(OrganizationError error) {
  return base::unexpected(error);
}

}  // namespace seoul

#endif  // SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_ERRORS_H_
