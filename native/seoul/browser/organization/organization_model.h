// Project Seoul native organization engine.
// The pure, in-memory organization domain model. It has NO Chromium browser
// dependencies (no Browser, no TabStripModel, no Profile) and only uses //base.
// It is deterministic, fully validated, and unit-testable without a browser.
// Chromium lifecycle/command adapters and the KeyedService wrap this model;
// they do not live inside it.
//
// Invariants enforced here (see docs/product/seoul-organization-v0.md):
//  - exactly one default workspace once initialized; the default cannot be
//  deleted
//  - workspace ids never change; names are not ids
//  - an archived workspace cannot be active
//  - removing the active workspace selects a deterministic fallback
//  - a tab_key belongs to at most one workspace (one membership) in this model
//  - a split belongs to exactly one workspace and references only its tabs
//  - a tab cannot be both archived and live
//  - a protected temporary tab is never auto-archived
//  - mutations are atomic: validation happens before any state change
//  - no mutation silently succeeds when it changed nothing due to invalid state

#ifndef SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_MODEL_H_
#define SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_MODEL_H_

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "seoul/browser/organization/organization_errors.h"
#include "seoul/browser/organization/organization_ids.h"
#include "seoul/browser/organization/organization_observer.h"
#include "seoul/browser/organization/organization_types.h"

namespace seoul {

class OrganizationModel {
 public:
  // A clock injection point so tests are deterministic; defaults to
  // base::Time::Now.
  using Clock = base::RepeatingCallback<base::Time()>;

  OrganizationModel();
  explicit OrganizationModel(Clock clock);
  OrganizationModel(const OrganizationModel&) = delete;
  OrganizationModel& operator=(const OrganizationModel&) = delete;
  ~OrganizationModel();

  // --- Initialization ---
  // Creates exactly one default workspace if none exists. Idempotent: calling
  // it again never creates a second default. Returns kNoOpRejected only if the
  // model is mid-notification (reentrancy); creating-when-present is a success
  // no-op.
  MutationStatus EnsureDefaultWorkspace();

  // --- Workspaces ---
  MutationResult<WorkspaceId> CreateWorkspace(std::string_view name);
  MutationStatus RenameWorkspace(const WorkspaceId& id, std::string_view name);
  // Empty clears the explicit icon and restores Zen's neutral no-icon dot.
  // Non-empty values are validated opaque built-in tokens or UTF-8 emoji,
  // never navigable URLs.
  MutationStatus SetWorkspaceIcon(const WorkspaceId& id, std::string_view icon);
  // Turns storage isolation on or off for a Space. Changing it does not move
  // data between partitions - what was stored in one is simply no longer the
  // partition the Space's tabs use - so callers are expected to make that
  // consequence explicit to the user rather than treating this as a toggle
  // with no cost.
  MutationStatus SetWorkspaceIsolated(const WorkspaceId& id, bool isolated);
  MutationStatus ReorderWorkspace(const WorkspaceId& id, int new_order);
  MutationStatus ArchiveWorkspace(const WorkspaceId& id);
  MutationStatus RestoreWorkspace(const WorkspaceId& id);
  MutationStatus DeleteWorkspace(const WorkspaceId& id);
  MutationStatus SetActiveWorkspaceForWindow(std::string_view window_key,
                                             const WorkspaceId& id);
  // Forget a window's active-workspace projection (e.g. the window closed). The
  // workspace itself and its memberships are preserved.
  MutationStatus ForgetWindow(std::string_view window_key);

  // --- Tab membership ---
  MutationResult<TabMembershipId> AddTabMembership(
      const WorkspaceId& workspace_id,
      std::string_view tab_key,
      TabRole role);
  MutationStatus RemoveTabMembership(const TabMembershipId& id);
  MutationStatus MoveTabToWorkspace(const TabMembershipId& id,
                                    const WorkspaceId& target_workspace);
  // Session restore gives a WebContents a new per-session tab key. Atomically
  // rebind the existing durable membership and any split pane references
  // without changing its id, workspace, role, ordering, or timestamps.
  MutationStatus RebindTabMembership(const TabMembershipId& id,
                                     std::string_view new_tab_key);
  MutationStatus MarkTabTemporary(const TabMembershipId& id);
  MutationStatus RetainTab(const TabMembershipId& id);
  MutationStatus PinTab(const TabMembershipId& id,
                        std::string_view saved_root_url);
  MutationStatus UnpinTab(const TabMembershipId& id);
  // Record that a tab was activated (updates its last-active time). Used by the
  // lifecycle bridge on a genuine user activation; does not change
  // role/workspace.
  MutationStatus TouchTabActivated(const TabMembershipId& id);
  // Update a membership's deterministic ordering metadata (intra-window move).
  MutationStatus ReorderTabMembership(const TabMembershipId& id, int order);
  // The name the user gave this tab. Organization metadata only: Chromium's
  // page title, navigation entry, and history are untouched, and an empty
  // title clears the custom name so the live page title shows again.
  MutationStatus SetTabCustomTitle(const TabMembershipId& id,
                                   std::string_view custom_title);
  MutationStatus ClearTabCustomTitle(const TabMembershipId& id);

  // --- Folders (per-workspace tab grouping; presentation metadata only) ---
  MutationResult<FolderId> CreateFolder(const WorkspaceId& workspace_id,
                                        std::string_view name);
  MutationStatus RenameFolder(const FolderId& id, std::string_view name);
  MutationStatus ReorderFolder(const FolderId& id, int order);
  MutationStatus SetFolderCollapsed(const FolderId& id, bool collapsed);
  // Removes the folder and moves every tab in it back to the workspace root.
  // It never closes a tab: Chromium owns tab lifetime, and a folder is only a
  // grouping over tabs it does not own.
  MutationStatus DissolveFolder(const FolderId& id);
  // Moves a tab into a folder in its own workspace, or out of any folder when
  // `folder_id` is invalid. Refuses a folder belonging to another workspace,
  // because a tab cannot be in one workspace and a different workspace's
  // folder at once.
  MutationStatus MoveTabToFolder(const TabMembershipId& id,
                                 const FolderId& folder_id);

  // --- Essentials (profile-global; single identity, never duplicated) ---
  // When id is invalid, creates a new Essential; otherwise updates the existing
  // one. root_url is the saved destination, not a live tab.
  MutationResult<EssentialId> CreateOrUpdateEssential(
      const EssentialId& id,
      std::string_view name,
      std::string_view root_url);
  MutationStatus RemoveEssential(const EssentialId& id);

  // --- Splits (over Chromium's M149 split model) ---
  MutationResult<SplitGroupId> CreateSplitGroup(
      const WorkspaceId& workspace_id,
      const std::vector<std::string>& pane_tab_keys,
      double divider_ratio,
      std::string_view upstream_split_token);
  MutationStatus UpdateSplitLayout(const SplitGroupId& id,
                                   double divider_ratio,
                                   int active_pane_index);
  MutationStatus DissolveSplitGroup(const SplitGroupId& id);
  // Atomically replace split pane membership and layout. Preserves the Seoul
  // split id when `upstream_split_token` matches the existing record. Rejects
  // invalid proposed state without dissolving first.
  MutationStatus ReplaceSplitGroupContents(
      std::string_view upstream_split_token,
      const std::vector<std::string>& pane_tab_keys,
      double divider_ratio,
      int active_pane_index);

  // --- Temporary-tab protection / auto-archive ---
  // Returns the temporary memberships eligible for auto-archive: role ==
  // kTemporary, inactive for at least `inactivity_threshold`, AND not protected
  // by any live condition in `activity` (media, download, task, permission,
  // split, devtools, unsaved form, loading). Pinned/retained tabs are never
  // eligible. Pure query; does not mutate.
  std::vector<TabMembershipId> EligibleForAutoArchive(
      const std::map<std::string, TabLiveActivity>& activity,
      base::Time now,
      base::TimeDelta inactivity_threshold) const;

  // --- Archive ---
  MutationStatus ArchiveTab(const TabMembershipId& id,
                            std::string recovery_url = std::string(),
                            std::string title = std::string());
  const ArchivedTabRecord* FindArchivedTab(
      const TabMembershipId& original_id) const;
  // Reconciles a newly inserted live membership with archived metadata in one
  // commit. Used after Chromium confirms a restore navigation.
  MutationStatus AdoptRestoredTab(const TabMembershipId& original_id,
                                  const TabMembershipId& live_membership_id);
  MutationResult<TabMembershipId> RestoreArchivedTab(
      const TabMembershipId& original_id,
      std::string_view tab_key);

  // --- Routing ---
  MutationResult<RoutingRuleId> AddRoutingRule(const RoutingRule& rule);
  // Atomically replaces an existing rule while preserving its stable id.
  // Validation completes before the stored rule changes.
  MutationStatus UpdateRoutingRule(const RoutingRule& rule);
  MutationStatus RemoveRoutingRule(const RoutingRuleId& id);
  // Deterministic, bounded, loop-safe. Always returns a result: an unmatched
  // request preserves the caller's validated requested disposition and sets
  // used_fallback = true.
  RoutingResolution EvaluateRouting(const RoutingRequest& request) const;
  // Scene-scoped evaluation. Only rules in `eligible_rules` may match; an
  // empty set intentionally means no rule, not "all rules."
  RoutingResolution EvaluateRouting(
      const RoutingRequest& request,
      const std::set<RoutingRuleId>& eligible_rules) const;

  // --- Snapshot / load ---
  OrganizationSnapshot ToSnapshot() const;
  // Replaces all state with a validated snapshot. Strict: rejects
  // cross-workspace splits, dangling references, duplicate default, oversize,
  // etc., leaving the current state untouched on failure (atomic).
  MutationStatus LoadSnapshot(const OrganizationSnapshot& snapshot);

  // --- Read accessors (const) ---
  size_t workspace_count() const { return workspaces_.size(); }
  size_t membership_count() const { return memberships_.size(); }
  size_t folder_count() const { return folders_.size(); }
  size_t essential_count() const { return essentials_.size(); }
  size_t split_count() const { return splits_.size(); }
  size_t routing_rule_count() const { return routing_rules_.size(); }
  size_t archived_count() const { return archived_.size(); }
  WorkspaceId default_workspace() const { return default_workspace_; }
  const WorkspaceRecord* FindWorkspace(const WorkspaceId& id) const;
  const TabMembershipRecord* FindMembership(const TabMembershipId& id) const;
  // O(log n) lookup from an opaque tab_key to its membership id (one tab
  // belongs to at most one workspace). Returns an invalid id when the tab is
  // untracked.
  TabMembershipId FindMembershipIdByTabKey(std::string_view tab_key) const;
  const FolderRecord* FindFolder(const FolderId& id) const;
  const EssentialRecord* FindEssential(const EssentialId& id) const;
  const SplitGroupRecord* FindSplit(const SplitGroupId& id) const;
  // Resolve an opaque upstream split token (the serialized
  // split_tabs::SplitTabId) to its Seoul split id. Returns an invalid id when
  // no split matches. Bounded by the split caps; the lifecycle bridge uses it
  // instead of caching tokens.
  SplitGroupId FindSplitIdByUpstreamToken(
      std::string_view upstream_token) const;
  WorkspaceId ActiveWorkspaceForWindow(std::string_view window_key) const;

  void AddObserver(OrganizationModelObserver* observer);
  void RemoveObserver(OrganizationModelObserver* observer);

 private:
  base::Time Now() const;
  int NextWorkspaceOrder() const;
  int NextOrderInWorkspace(const WorkspaceId& workspace_id) const;
  // Deterministic fallback when the active workspace becomes unavailable: the
  // lowest-order non-archived workspace, default first, then by order, then id.
  WorkspaceId PickFallbackWorkspace(const WorkspaceId& excluded) const;
  size_t MembershipsInWorkspace(const WorkspaceId& workspace_id) const;
  size_t FoldersInWorkspace(const WorkspaceId& workspace_id) const;
  int NextFolderOrderInWorkspace(const WorkspaceId& workspace_id) const;
  size_t SplitsInWorkspace(const WorkspaceId& workspace_id) const;
  void Notify(const OrganizationChange& change);
  bool ValidName(std::string_view name) const;
  bool ValidIcon(std::string_view icon) const;
  RoutingResolution EvaluateRoutingInternal(
      const RoutingRequest& request,
      const std::set<RoutingRuleId>* eligible_rules) const;

  Clock clock_;
  bool notifying_ = false;  // reentrancy guard

  std::map<WorkspaceId, WorkspaceRecord> workspaces_;
  std::map<EssentialId, EssentialRecord> essentials_;
  std::map<TabMembershipId, TabMembershipRecord> memberships_;
  std::map<FolderId, FolderRecord> folders_;
  std::map<SplitGroupId, SplitGroupRecord> splits_;
  std::map<RoutingRuleId, RoutingRule> routing_rules_;
  std::map<std::string, WorkspaceId> window_active_;
  std::map<TabMembershipId, ArchivedTabRecord> archived_;
  // Index enforcing "a tab_key belongs to at most one live workspace".
  std::map<std::string, TabMembershipId> tab_index_;
  WorkspaceId default_workspace_;

  base::ObserverList<OrganizationModelObserver> observers_;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_MODEL_H_
