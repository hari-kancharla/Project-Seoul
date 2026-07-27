// Project Seoul native organization engine.
// Bounded limits for the workspace/tab organization model and its persistence.
// Every collection that can grow from user action is capped here so the model
// and the serialized form can never grow without bound.

#ifndef SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_LIMITS_H_
#define SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_LIMITS_H_

#include <cstddef>

namespace seoul {

// Bump only with an explicit migration. Unknown higher versions are rejected,
// not downgraded (see OrganizationStore).
inline constexpr int kOrganizationSchemaVersion = 1;

inline constexpr size_t kMaxWorkspaces = 100;
// New user mutations follow Zen's visible 12-item Essentials deck. The larger
// persisted bound remains for safe, lossless parsing of legacy profiles.
inline constexpr size_t kMaxUserEssentials = 12;
inline constexpr size_t kMaxEssentials = 100;
inline constexpr size_t kMaxMembershipsPerWorkspace = 2000;
inline constexpr size_t kMaxSplitGroupsPerWorkspace = 500;
inline constexpr size_t kMaxRoutingRules = 200;
inline constexpr size_t kMaxArchivedTabs = 1000;

// Aggregate caps enforced before walking untrusted stored lists.
inline constexpr size_t kMaxTotalMemberships = 10000;
inline constexpr size_t kMaxTotalSplits = 2000;
inline constexpr size_t kMaxWindowStates = 256;
inline constexpr size_t kMaxUpstreamSplitTokenLength = 256;

inline constexpr size_t kMaxNameLength = 256;
inline constexpr size_t kMaxIconRefLength = 256;
inline constexpr size_t kMaxUrlLength = 4096;
inline constexpr size_t kMaxTabKeyLength = 256;
inline constexpr size_t kMaxPatternLength = 1024;

// Total serialized organization metadata cap. This is metadata only; Chromium
// owns tab contents, history, cookies, passwords, and form state.
inline constexpr size_t kMaxSerializedBytes = 2u * 1024u * 1024u;  // 2 MiB

// v0 supports exactly two panes per split. The model stores panes as an ordered
// list (not a hardcoded pair) so multi-pane can be enabled later without a
// schema break; only this bound changes.
inline constexpr size_t kMinSplitPanes = 2;
inline constexpr size_t kMaxSplitPanesV0 = 2;

inline constexpr double kMinDividerRatio = 0.1;
inline constexpr double kMaxDividerRatio = 0.9;

// Routing evaluation is bounded: at most this many rules are considered per
// request (also enforced by kMaxRoutingRules), and a single request never
// chains more than this many redirections (loop prevention).
inline constexpr int kMaxRoutingHops = 1;

}  // namespace seoul

#endif  // SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_LIMITS_H_
