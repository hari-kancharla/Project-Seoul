// Project Seoul workspace projection engine V0.

#include "seoul/browser/projection/projection_calculator.h"

#include <algorithm>
#include <map>
#include <set>

namespace seoul {

int ProjectionCalculator::RoleSortKey(TabRole role) {
  switch (role) {
    case TabRole::kPinned:
      return 0;
    case TabRole::kRetained:
      return 1;
    case TabRole::kTemporary:
      return 2;
  }
  return 3;
}

bool ProjectionCalculator::TabBelongsToWorkspace(const OrganizationModel& model,
                                                 const LiveTabDescriptor& tab,
                                                 const WorkspaceId& workspace) {
  const TabMembershipId id = model.FindMembershipIdByTabKey(tab.tab.value());
  if (!id.is_valid()) {
    return false;
  }
  const TabMembershipRecord* m = model.FindMembership(id);
  return m && m->workspace_id == workspace;
}

WindowProjection ProjectionCalculator::Compute(const OrganizationModel& model,
                                               const LiveWindowTabState& live,
                                               WorkspaceId active_workspace,
                                               ProjectionGeneration generation,
                                               bool fail_open) {
  WindowProjection projection;
  projection.window = live.window;
  projection.active_workspace = active_workspace;
  projection.generation = generation;
  for (const LiveTabDescriptor& tab : live.tabs) {
    if (tab.is_new_tab_placeholder && tab.tab.is_valid()) {
      projection.hidden_tabs.push_back(tab.tab);
    }
  }

  const WorkspaceRecord* ws = model.FindWorkspace(active_workspace);
  if (!active_workspace.is_valid() || !ws) {
    projection.status = ProjectionStatus::kDegraded;
    ProjectionInconsistency inc;
    inc.kind = ProjectionInconsistencyKind::kMissingMembership;
    inc.workspace_id = active_workspace;
    inc.detail = "missing_workspace";
    projection.inconsistencies.push_back(std::move(inc));
    return projection;
  }
  if (ws->archived) {
    projection.status = ProjectionStatus::kDegraded;
    ProjectionInconsistency inc;
    inc.kind = ProjectionInconsistencyKind::kArchivedWorkspace;
    inc.workspace_id = active_workspace;
    projection.inconsistencies.push_back(std::move(inc));
    return projection;
  }

  if (fail_open) {
    projection.status = ProjectionStatus::kFailOpen;
    for (const LiveTabDescriptor& tab : live.tabs) {
      // Fail-open exposes real tabs across workspace boundaries, but the
      // window's inert new-tab placeholder is browser chrome rather than a
      // user tab and must remain hidden.
      if (tab.is_new_tab_placeholder) {
        continue;
      }
      ProjectedTab pt;
      pt.tab = tab.tab;
      pt.strip_order = tab.strip_order;
      pt.projected_order = static_cast<int>(projection.tabs.size());
      const TabMembershipId mid =
          model.FindMembershipIdByTabKey(tab.tab.value());
      pt.membership_id = mid;
      const TabMembershipRecord* m =
          mid.is_valid() ? model.FindMembership(mid) : nullptr;
      if (m) {
        pt.role = m->role;
      }
      projection.tabs.push_back(pt);
    }
    const auto active =
        std::ranges::find(projection.tabs, live.active_tab, &ProjectedTab::tab);
    if (active != projection.tabs.end()) {
      projection.active_tab = live.active_tab;
    }
    return projection;
  }

  std::set<std::string> seen_keys;
  std::vector<ProjectedTab> candidates;
  for (const LiveTabDescriptor& tab : live.tabs) {
    if (tab.is_new_tab_placeholder) {
      continue;
    }
    if (!tab.tab.is_valid()) {
      continue;
    }
    if (!seen_keys.insert(tab.tab.value()).second) {
      ProjectionInconsistency inc;
      inc.kind = ProjectionInconsistencyKind::kDuplicateTabKey;
      inc.tab = tab.tab;
      projection.inconsistencies.push_back(std::move(inc));
      continue;
    }
    const TabMembershipId mid = model.FindMembershipIdByTabKey(tab.tab.value());
    if (!mid.is_valid()) {
      ProjectionInconsistency inc;
      inc.kind = ProjectionInconsistencyKind::kMissingMembership;
      inc.tab = tab.tab;
      projection.inconsistencies.push_back(std::move(inc));
      continue;
    }
    const TabMembershipRecord* m = model.FindMembership(mid);
    if (!m || !(m->workspace_id == active_workspace)) {
      continue;
    }
    ProjectedTab pt;
    pt.tab = tab.tab;
    pt.membership_id = mid;
    pt.role = m->role;
    pt.strip_order = tab.strip_order;
    candidates.push_back(pt);
  }

  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const ProjectedTab& a, const ProjectedTab& b) {
                     const int ra = RoleSortKey(a.role);
                     const int rb = RoleSortKey(b.role);
                     if (ra != rb) {
                       return ra < rb;
                     }
                     return a.strip_order < b.strip_order;
                   });
  for (size_t i = 0; i < candidates.size(); ++i) {
    candidates[i].projected_order = static_cast<int>(i);
    projection.tabs.push_back(candidates[i]);
  }

  if (projection.tabs.empty()) {
    projection.empty_workspace = true;
    projection.status = ProjectionStatus::kEmptyWorkspace;
  }

  LiveTabKey committed_active;
  bool active_is_placeholder = false;
  for (const LiveTabDescriptor& tab : live.tabs) {
    if (tab.tab == live.active_tab) {
      active_is_placeholder = tab.is_new_tab_placeholder;
      break;
    }
  }
  for (const ProjectedTab& pt : projection.tabs) {
    if (pt.tab == live.active_tab) {
      committed_active = pt.tab;
      break;
    }
  }
  if (live.active_tab.is_valid() && !committed_active.is_valid() &&
      !active_is_placeholder) {
    ProjectionInconsistency inc;
    inc.kind = ProjectionInconsistencyKind::kActiveTabNotProjected;
    inc.tab = live.active_tab;
    projection.inconsistencies.push_back(std::move(inc));
  } else {
    projection.active_tab = committed_active;
  }

  for (const SplitGroupRecord& split : model.ToSnapshot().splits) {
    if (!(split.workspace_id == active_workspace)) {
      continue;
    }
    if (split.pane_tab_keys.size() != 2) {
      continue;
    }
    const LiveTabKey pane_a = LiveTabKey::Parse(split.pane_tab_keys[0]);
    const LiveTabKey pane_b = LiveTabKey::Parse(split.pane_tab_keys[1]);
    bool a_visible = false;
    bool b_visible = false;
    for (const ProjectedTab& pt : projection.tabs) {
      if (pt.tab == pane_a) {
        a_visible = true;
      }
      if (pt.tab == pane_b) {
        b_visible = true;
      }
    }
    if (a_visible && b_visible) {
      ProjectedSplit ps;
      ps.split_id = split.id;
      ps.pane_a = pane_a;
      ps.pane_b = pane_b;
      ps.upstream_split_token = split.upstream_split_token;
      ps.divider_ratio = split.divider_ratio;
      projection.splits.push_back(ps);
    } else if (a_visible != b_visible) {
      ProjectionInconsistency inc;
      inc.kind = ProjectionInconsistencyKind::kCrossWorkspaceSplit;
      inc.workspace_id = active_workspace;
      inc.detail = split.upstream_split_token;
      projection.inconsistencies.push_back(std::move(inc));
    }
  }

  for (const LiveTabDescriptor& tab : live.tabs) {
    if (tab.upstream_group_token.empty()) {
      continue;
    }
    const TabMembershipId mid = model.FindMembershipIdByTabKey(tab.tab.value());
    const TabMembershipRecord* m =
        mid.is_valid() ? model.FindMembership(mid) : nullptr;
    if (!m) {
      continue;
    }
    std::map<std::string, WorkspaceId> group_workspaces;
    for (const LiveTabDescriptor& other : live.tabs) {
      if (other.upstream_group_token != tab.upstream_group_token) {
        continue;
      }
      const TabMembershipId oid =
          model.FindMembershipIdByTabKey(other.tab.value());
      const TabMembershipRecord* om =
          oid.is_valid() ? model.FindMembership(oid) : nullptr;
      if (!om) {
        continue;
      }
      group_workspaces[other.upstream_group_token] = om->workspace_id;
      if (group_workspaces.size() > 1) {
        break;
      }
    }
  }

  if (!projection.inconsistencies.empty() &&
      projection.status == ProjectionStatus::kCoherent) {
    projection.status = ProjectionStatus::kDegraded;
  }

  return projection;
}

}  // namespace seoul
