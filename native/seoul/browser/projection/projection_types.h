// Project Seoul workspace projection engine V0.
// Pure projection types: no Chromium pointers, no WebContents, no persistence.

#ifndef SEOUL_BROWSER_PROJECTION_PROJECTION_TYPES_H_
#define SEOUL_BROWSER_PROJECTION_PROJECTION_TYPES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/lifecycle/live_window_snapshot_types.h"
#include "seoul/browser/organization/organization_ids.h"
#include "seoul/browser/organization/organization_types.h"

namespace seoul {

class ProjectionGeneration {
 public:
  ProjectionGeneration() = default;
  explicit ProjectionGeneration(uint64_t value) : value_(value) {}
  bool is_valid() const { return value_ != 0; }
  uint64_t value() const { return value_; }
  ProjectionGeneration Next() const { return ProjectionGeneration(value_ + 1); }
  friend bool operator==(const ProjectionGeneration&,
                         const ProjectionGeneration&) = default;

 private:
  uint64_t value_ = 0;
};

enum class ProjectionStatus {
  kCoherent,
  kEmptyWorkspace,
  kDegraded,
  kFailOpen,
  kReconciliationRequired,
};

enum class ProjectionInconsistencyKind {
  kMissingMembership,
  kDuplicateTabKey,
  kCrossWorkspaceSplit,
  kMixedWorkspaceGroup,
  kArchivedWorkspace,
  kActiveTabNotProjected,
};

struct ProjectionInconsistency {
  ProjectionInconsistencyKind kind =
      ProjectionInconsistencyKind::kMissingMembership;
  LiveTabKey tab;
  WorkspaceId workspace_id;
  std::string detail;
};

struct ProjectedTab {
  LiveTabKey tab;
  TabMembershipId membership_id;
  TabRole role = TabRole::kTemporary;
  int strip_order = -1;
  int projected_order = -1;
};

struct ProjectedSplit {
  SplitGroupId split_id;
  LiveTabKey pane_a;
  LiveTabKey pane_b;
  std::string upstream_split_token;
  double divider_ratio = 0.5;
};

struct WindowProjection {
  WindowProjection();
  WindowProjection(const WindowProjection&);
  WindowProjection(WindowProjection&&);
  WindowProjection& operator=(const WindowProjection&);
  WindowProjection& operator=(WindowProjection&&);
  ~WindowProjection();
  LiveWindowKey window;
  WorkspaceId active_workspace;
  ProjectionGeneration generation;
  ProjectionStatus status = ProjectionStatus::kCoherent;
  std::vector<ProjectedTab> tabs;
  // Tabs that are browser-owned chrome rather than user-visible workspace
  // items. Presentation keeps these hidden even during fail-open recovery.
  std::vector<LiveTabKey> hidden_tabs;
  LiveTabKey active_tab;
  std::vector<ProjectedSplit> splits;
  bool empty_workspace = false;
  std::vector<ProjectionInconsistency> inconsistencies;
};

struct ProjectionSnapshot {
  ProjectionSnapshot();
  ProjectionSnapshot(const ProjectionSnapshot&);
  ProjectionSnapshot(ProjectionSnapshot&&);
  ProjectionSnapshot& operator=(const ProjectionSnapshot&);
  ProjectionSnapshot& operator=(ProjectionSnapshot&&);
  ~ProjectionSnapshot();
  std::vector<WindowProjection> windows;
};

enum class ProjectionChangeType {
  kProjectionUpdated,
  kActiveWorkspaceChanged,
  kDegradedStateEntered,
  kDegradedStateCleared,
  kFailOpenEntered,
};

struct ProjectionChange {
  ProjectionChangeType type;
  LiveWindowKey window;
  ProjectionGeneration generation;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PROJECTION_PROJECTION_TYPES_H_
