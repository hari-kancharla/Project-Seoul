// Project Seoul native browser shell V0.

#include "seoul/browser/shell/shell_view_model.h"

#include <algorithm>
#include <string>
#include <utility>

#include "url/gurl.h"
#include "url/origin.h"

namespace seoul {
namespace {

ShellItemState EssentialState(const EssentialRecord& essential) {
  if (essential.root_url.empty()) {
    return ShellItemState::kUnavailable;
  }
  return ShellItemState::kNormal;
}

void AddAction(ShellSnapshot* snapshot,
               ShellUtilityAction action,
               bool enabled,
               const std::string& disabled_reason) {
  ShellActionEnablement entry;
  entry.action = action;
  entry.enabled = enabled;
  entry.disabled_reason = disabled_reason;
  snapshot->actions.push_back(std::move(entry));
}

}  // namespace

std::vector<ShellSplitCandidate> ShellViewModel::BuildSplitCandidates(
    const WindowProjection& projection,
    const LiveWindowSnapshot& live) {
  std::vector<ShellSplitCandidate> candidates;
  if (!projection.active_tab.is_valid()) {
    return candidates;
  }
  for (const LiveTabDescriptor& descriptor : live.tabs) {
    if (descriptor.tab == projection.active_tab &&
        !descriptor.upstream_split_token.empty()) {
      return candidates;
    }
  }
  for (const ProjectedTab& projected : projection.tabs) {
    if (!projected.tab.is_valid() || projected.tab == projection.active_tab) {
      continue;
    }
    ShellSplitCandidate candidate;
    candidate.tab = projected.tab;
    bool found_live = false;
    for (const LiveTabDescriptor& descriptor : live.tabs) {
      if (descriptor.tab != projected.tab) {
        continue;
      }
      found_live = true;
      if (!descriptor.upstream_split_token.empty()) {
        candidate.tab = LiveTabKey();
      } else {
        candidate.title = descriptor.title;
        candidate.origin = descriptor.origin;
      }
      break;
    }
    if (found_live && candidate.tab.is_valid()) {
      candidates.push_back(std::move(candidate));
    }
  }
  return candidates;
}

std::string ShellViewModel::TaskButtonLabel(const ShellTaskSummary& tasks,
                                            ShellMode mode) {
  if (mode == ShellMode::kCollapsed) {
    if (tasks.has_attention()) {
      return "!";
    }
    return tasks.active > 0 ? std::to_string(tasks.active) : "○";
  }
  return tasks.total > 0 ? "Tasks " + std::to_string(tasks.total) : "Tasks";
}

std::string ShellViewModel::TaskAccessibleName(const ShellTaskSummary& tasks) {
  if (tasks.total == 0) {
    return "Task Deck, no tasks";
  }
  return "Task Deck: " + std::to_string(tasks.active) + " active, " +
         std::to_string(tasks.waiting_for_user) + " waiting for you, " +
         std::to_string(tasks.paused) + " paused, " +
         std::to_string(tasks.failed) + " failed";
}

ShellSnapshot ShellViewModel::Build(const OrganizationModel& model,
                                    const ShellBuildContext& context,
                                    const WindowProjection& projection,
                                    const LiveWindowSnapshot& live,
                                    uint64_t revision) {
  ShellSnapshot snapshot;
  snapshot.window = context.window;
  snapshot.mode = context.mode;
  snapshot.revision = revision;
  snapshot.switch_phase = context.switch_phase;
  snapshot.tasks = context.tasks;

  const WorkspaceId active =
      model.ActiveWorkspaceForWindow(context.window.value());
  const WorkspaceRecord* ws = model.FindWorkspace(active);
  if (ws) {
    snapshot.workspace.workspace_id = ws->id;
    snapshot.workspace.name = ws->name;
    snapshot.workspace.icon = ws->icon;
    snapshot.workspace.archived = ws->archived;
  }
  snapshot.workspace.switching =
      context.switch_phase == WorkspaceSwitchPhase::kValidating ||
      context.switch_phase == WorkspaceSwitchPhase::kCalculating ||
      context.switch_phase == WorkspaceSwitchPhase::kAwaitingActivation ||
      context.switch_phase == WorkspaceSwitchPhase::kCommitting;

  const OrganizationSnapshot org = model.ToSnapshot();
  for (const WorkspaceRecord& workspace : org.workspaces) {
    if (workspace.archived) {
      continue;
    }
    ShellSpaceItem space;
    space.workspace_id = workspace.id;
    space.name = workspace.name;
    space.icon = workspace.icon;
    space.is_active = workspace.id == active;
    space.switching = space.is_active && snapshot.workspace.switching;
    snapshot.spaces.push_back(std::move(space));
  }

  if (context.recovery_required) {
    snapshot.status = ShellStatus::kRecoveryRequired;
    snapshot.show_status_banner = true;
    snapshot.status_message = "Recovery required.";
    AddAction(&snapshot, ShellUtilityAction::kAcknowledgeRecovery, true, "");
    AddAction(&snapshot, ShellUtilityAction::kCommandLauncher, true, "");
    AddAction(&snapshot, ShellUtilityAction::kOpenCanvas, true, "");
    AddAction(&snapshot, ShellUtilityAction::kOpenBoost, true, "");
  AddAction(&snapshot, ShellUtilityAction::kCapture, true, "");
    AddAction(&snapshot, ShellUtilityAction::kCapture, true, "");
    AddAction(&snapshot, ShellUtilityAction::kOpenTaskDeck, true, "");
    AddAction(&snapshot, ShellUtilityAction::kOpenSettings, true, "");
    AddAction(&snapshot, ShellUtilityAction::kOpenDownloads, true, "");
    AddAction(&snapshot, ShellUtilityAction::kToggleCompactMode, false,
              "Compact mode is unavailable for this window.");
    AddAction(&snapshot, ShellUtilityAction::kSetAppearanceSingle, false,
              "Appearance layouts are unavailable for this window.");
    AddAction(&snapshot, ShellUtilityAction::kSetAppearanceMultiple, false,
              "Appearance layouts are unavailable for this window.");
    AddAction(&snapshot, ShellUtilityAction::kSetAppearanceCollapsed, false,
              "Appearance layouts are unavailable for this window.");
    AddAction(&snapshot, ShellUtilityAction::kNewTemporaryTab, false,
              "Recovery required.");
    AddAction(&snapshot, ShellUtilityAction::kCreateSplit, false,
              "Recovery required.");
    return snapshot;
  }

  const bool switching_now =
      context.switch_phase == WorkspaceSwitchPhase::kValidating ||
      context.switch_phase == WorkspaceSwitchPhase::kCalculating ||
      context.switch_phase == WorkspaceSwitchPhase::kAwaitingActivation ||
      context.switch_phase == WorkspaceSwitchPhase::kCommitting;
  // A Space switch is checked before the degraded states, not after. Mid-switch
  // the projection passes through fail-open and reconciliation-pending for a
  // few frames by design, and surfacing those as banners meant every switch
  // flashed "Showing all tabs while the layout recovers." at the user - a
  // scary message for a working feature. A switch is not an error. Degradation
  // that persists past the switch still shows, because the phase clears.
  if (switching_now) {
    snapshot.status = ShellStatus::kSwitchingWorkspace;
  } else if (context.lifecycle_degraded ||
             projection.status == ProjectionStatus::kReconciliationRequired) {
    snapshot.status = ShellStatus::kReconciliationRequired;
    snapshot.show_status_banner = true;
    snapshot.status_message = "Reconciliation required.";
    AddAction(&snapshot, ShellUtilityAction::kReconcile, true, "");
  } else if (projection.status == ProjectionStatus::kFailOpen) {
    snapshot.status = ShellStatus::kFailOpen;
    snapshot.show_status_banner = true;
    snapshot.status_message = "Showing all tabs while the layout recovers.";
  } else if (projection.empty_workspace) {
    snapshot.status = ShellStatus::kEmptyWorkspace;
    snapshot.show_empty_workspace = true;
  } else {
    snapshot.status = ShellStatus::kCoherent;
  }

  for (const EssentialRecord& essential : org.essentials) {
    ShellEssentialItem item;
    item.id = essential.id;
    item.name = essential.name;
    item.icon = essential.icon;
    item.root_url = essential.root_url;
    item.state = EssentialState(essential);
    const GURL essential_url(essential.root_url);
    const url::Origin essential_origin = url::Origin::Create(essential_url);
    if (!essential_origin.opaque() &&
        essential_origin.GetURL().SchemeIsHTTPOrHTTPS()) {
      const std::string serialized = essential_origin.Serialize();
      auto associate = [&](const LiveWindowSnapshot& candidate_window,
                           bool is_current_window) {
        for (const LiveTabDescriptor& descriptor : candidate_window.tabs) {
          if (descriptor.tab.is_valid() && descriptor.origin == serialized) {
            item.has_live_tab = true;
            item.live_in_current_window = is_current_window;
            item.live_tab = descriptor.tab;
            item.live_window = candidate_window.window;
            item.is_active = descriptor.tab == candidate_window.active_tab;
            return true;
          }
        }
        return false;
      };
      if (!associate(live, true)) {
        for (const LiveWindowSnapshot& other : context.other_live_windows) {
          if (associate(other, false)) {
            break;
          }
        }
      }
    }
    snapshot.essentials.push_back(std::move(item));
  }

  int retained_count = 0;
  int temporary_count = 0;
  int pinned_count = 0;
  for (const ProjectedTab& tab : projection.tabs) {
    if (tab.role == TabRole::kPinned) {
      ShellPinnedItem pinned;
      pinned.membership_id = tab.membership_id;
      pinned.tab = tab.tab;
      pinned.is_active = tab.tab == projection.active_tab;
      pinned.state = ShellItemState::kNormal;
      const TabMembershipRecord* m = model.FindMembership(tab.membership_id);
      if (m) {
        pinned.saved_root_url = m->saved_root_url;
      }
      snapshot.pinned_items.push_back(std::move(pinned));
      pinned_count++;
    } else if (tab.role == TabRole::kRetained) {
      retained_count++;
    } else {
      temporary_count++;
    }
  }

  ShellSectionInfo pinned_section;
  pinned_section.section = ShellSection::kWorkspacePinned;
  pinned_section.label = "Pinned";
  pinned_section.visible =
      pinned_count > 0 || snapshot.mode == ShellMode::kExpanded;
  pinned_section.projected_count = pinned_count;
  snapshot.sections.push_back(pinned_section);

  ShellSectionInfo retained_section;
  retained_section.section = ShellSection::kRetainedTabs;
  retained_section.label = "Retained";
  retained_section.visible = retained_count > 0;
  retained_section.projected_count = retained_count;
  snapshot.sections.push_back(retained_section);

  ShellSectionInfo temporary_section;
  temporary_section.section = ShellSection::kTemporaryTabs;
  temporary_section.label = "Temporary";
  temporary_section.visible = temporary_count > 0;
  temporary_section.projected_count = temporary_count;
  snapshot.sections.push_back(temporary_section);

  const bool mutations_ok =
      snapshot.status != ShellStatus::kReconciliationRequired &&
      snapshot.status != ShellStatus::kRecoveryRequired;
  AddAction(&snapshot, ShellUtilityAction::kNewTemporaryTab, mutations_ok,
            mutations_ok ? "" : "Reconciliation required.");
  AddAction(&snapshot, ShellUtilityAction::kCommandLauncher, true, "");
  AddAction(&snapshot, ShellUtilityAction::kOpenCanvas, true, "");
  AddAction(&snapshot, ShellUtilityAction::kOpenBoost, true, "");
  AddAction(&snapshot, ShellUtilityAction::kOpenTaskDeck, true, "");
  AddAction(&snapshot, ShellUtilityAction::kOpenSettings, true, "");
  AddAction(&snapshot, ShellUtilityAction::kOpenDownloads, true, "");
  AddAction(&snapshot, ShellUtilityAction::kToggleCompactMode, false,
            "Compact mode is unavailable for this window.");
  AddAction(&snapshot, ShellUtilityAction::kSetAppearanceSingle, false,
            "Appearance layouts are unavailable for this window.");
  AddAction(&snapshot, ShellUtilityAction::kSetAppearanceMultiple, false,
            "Appearance layouts are unavailable for this window.");
  AddAction(&snapshot, ShellUtilityAction::kSetAppearanceCollapsed, false,
            "Appearance layouts are unavailable for this window.");

  const bool split_ok =
      mutations_ok && !BuildSplitCandidates(projection, live).empty();
  AddAction(&snapshot, ShellUtilityAction::kCreateSplit,
            split_ok && mutations_ok,
            split_ok ? "" : "Select another tab to split with.");

  return snapshot;
}

ShellBuildContext::ShellBuildContext() = default;
ShellBuildContext::ShellBuildContext(const ShellBuildContext&) = default;
ShellBuildContext::ShellBuildContext(ShellBuildContext&&) = default;
ShellBuildContext& ShellBuildContext::operator=(const ShellBuildContext&) =
    default;
ShellBuildContext& ShellBuildContext::operator=(ShellBuildContext&&) = default;
ShellBuildContext::~ShellBuildContext() = default;

}  // namespace seoul
