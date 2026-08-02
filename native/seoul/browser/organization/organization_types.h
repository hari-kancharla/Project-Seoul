// Project Seoul native organization engine.
// Plain-old-data records and enums for the organization domain model. These
// hold ONLY organization metadata. They never hold page content, navigation
// history, form values, credentials, tokens, or model prompts. Live tab state
// is owned by Chromium and is referenced here by opaque bridge-assigned keys.

#ifndef SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_TYPES_H_
#define SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_TYPES_H_

#include <string>
#include <vector>

#include "base/time/time.h"
#include "seoul/browser/organization/organization_ids.h"
#include "seoul/browser/organization/organization_limits.h"

namespace seoul {

// A tab's lifecycle role within its owning workspace.
enum class TabRole {
  kTemporary,  // disposable; eligible for auto-archive when unprotected
  kRetained,   // kept by the user; not auto-archived
  kPinned,  // pinned to the workspace; has a saved root url; not auto-archived
};

// v0 models a single Essential kind: a profile-global persistent destination
// with at most one shared live backing tab. The enum is reserved for future
// kinds.
enum class EssentialKind {
  kPersistentDestination,
};

enum class RoutingMatchType {
  kAnything,     // matches every request (use for a catch-all fallback rule)
  kOriginExact,  // request.origin equals pattern (exact, case-sensitive)
  kUrlPrefix,    // request.url starts with pattern
  kUrlGlob,  // request.url matches a simple '*' glob (no regex, no hardcoding)
};

enum class RoutingDisposition {
  kCurrentTab,
  kNewTemporaryTab,
  kNewRetainedTab,
  kSpecificWorkspace,
  kPreview,
  kSplitPane,
  kExternalApplication,
  kAskUser,
};

struct WorkspaceRecord {
  WorkspaceRecord();
  WorkspaceRecord(const WorkspaceRecord&);
  WorkspaceRecord(WorkspaceRecord&&);
  WorkspaceRecord& operator=(const WorkspaceRecord&);
  WorkspaceRecord& operator=(WorkspaceRecord&&);
  ~WorkspaceRecord();
  WorkspaceId id;
  std::string name;
  std::string
      icon;  // opaque icon reference (resource name / emoji); never a URL
  int order = 0;
  base::Time created_at;
  base::Time last_active_at;
  bool archived = false;
  bool is_default = false;
};

struct EssentialRecord {
  EssentialRecord();
  EssentialRecord(const EssentialRecord&);
  EssentialRecord(EssentialRecord&&);
  EssentialRecord& operator=(const EssentialRecord&);
  EssentialRecord& operator=(EssentialRecord&&);
  ~EssentialRecord();
  EssentialId id;
  EssentialKind kind = EssentialKind::kPersistentDestination;
  std::string name;
  std::string icon;
  std::string
      root_url;  // saved destination; live navigation state is not stored
  int order = 0;
  base::Time created_at;
};

struct TabMembershipRecord {
  TabMembershipRecord();
  TabMembershipRecord(const TabMembershipRecord&);
  TabMembershipRecord(TabMembershipRecord&&);
  TabMembershipRecord& operator=(const TabMembershipRecord&);
  TabMembershipRecord& operator=(TabMembershipRecord&&);
  ~TabMembershipRecord();
  TabMembershipId id;
  WorkspaceId workspace_id;
  // Opaque, stable handle assigned by the lifecycle bridge from Chromium's
  // session tab identity. The pure model treats it as a string and never
  // interprets it.
  std::string tab_key;
  TabRole role = TabRole::kTemporary;
  std::string saved_root_url;  // pinned reset target; empty for non-pinned
  int order = 0;
  base::Time created_at;
  base::Time last_active_at;
};

struct SplitGroupRecord {
  SplitGroupRecord();
  SplitGroupRecord(const SplitGroupRecord&);
  SplitGroupRecord(SplitGroupRecord&&);
  SplitGroupRecord& operator=(const SplitGroupRecord&);
  SplitGroupRecord& operator=(SplitGroupRecord&&);
  ~SplitGroupRecord();
  SplitGroupId id;
  WorkspaceId workspace_id;
  // Ordered panes (v0: exactly two). Each entry is a tab_key in the same
  // workspace.
  std::vector<std::string> pane_tab_keys;
  double divider_ratio = 0.5;
  int active_pane_index = 0;
  // Binding to Chromium's split model (serialized split_tabs::SplitTabId). The
  // pure model never interprets it; the bridge maps it to the live split.
  std::string upstream_split_token;
  base::Time created_at;
};

struct WindowWorkspaceState {
  std::string
      window_key;  // opaque, stable per browser window (bridge-assigned)
  WorkspaceId active_workspace_id;
};

struct RoutingPredicate {
  RoutingMatchType match_type = RoutingMatchType::kAnything;
  std::string pattern;           // origin / prefix / glob; empty for kAnything
  WorkspaceId source_workspace;  // optional: only apply within this workspace
  bool require_user_gesture = false;
};

struct RoutingResult {
  RoutingDisposition disposition = RoutingDisposition::kCurrentTab;
  WorkspaceId
      target_workspace;  // required iff disposition == kSpecificWorkspace
};

struct RoutingRule {
  RoutingRule();
  RoutingRule(const RoutingRule&);
  RoutingRule(RoutingRule&&);
  RoutingRule& operator=(const RoutingRule&);
  RoutingRule& operator=(RoutingRule&&);
  ~RoutingRule();
  RoutingRuleId id;
  int priority = 0;  // higher wins; ties broken deterministically by id
  RoutingPredicate predicate;
  RoutingResult result;
  bool enabled = true;
};

// Input to routing evaluation. Supplied by the bridge; no website behavior is
// baked into the model.
struct RoutingRequest {
  RoutingRequest();
  RoutingRequest(const RoutingRequest&);
  RoutingRequest(RoutingRequest&&);
  RoutingRequest& operator=(const RoutingRequest&);
  RoutingRequest& operator=(RoutingRequest&&);
  ~RoutingRequest();
  std::string url;
  std::string origin;
  WorkspaceId source_workspace;
  std::string source_tab_key;
  bool user_gesture = false;
  RoutingDisposition requested_disposition = RoutingDisposition::kCurrentTab;
};

// Result of routing evaluation: the chosen result plus the rule that produced
// it (invalid id => the safe fallback was used).
struct RoutingResolution {
  RoutingResult result;
  RoutingRuleId matched_rule;  // invalid when fallback used
  bool used_fallback = false;
};

// Live, transient conditions for a tab, supplied by the bridge. The pure model
// never observes the renderer; it consumes this descriptor to decide
// auto-archive eligibility. Any true field protects a temporary tab from
// auto-archive.
struct TabLiveActivity {
  bool playing_media = false;
  bool has_active_download = false;
  bool has_active_task = false;
  bool has_permission_prompt = false;
  bool in_split = false;
  bool has_devtools = false;
  bool has_unsaved_form = false;
  bool loading = false;
};

// Archived tab metadata. Recoverable, but not a live renderer and not a second
// copy of browser history.
struct ArchivedTabRecord {
  ArchivedTabRecord();
  ArchivedTabRecord(const ArchivedTabRecord&);
  ArchivedTabRecord(ArchivedTabRecord&&);
  ArchivedTabRecord& operator=(const ArchivedTabRecord&);
  ArchivedTabRecord& operator=(ArchivedTabRecord&&);
  ~ArchivedTabRecord();
  TabMembershipId original_id;
  WorkspaceId workspace_id;
  TabRole original_role = TabRole::kTemporary;
  // Exact URL used to recover the archived tab. The persisted wire key keeps
  // its original name for schema compatibility.
  std::string saved_root_url;
  std::string title;  // last known title, bounded; optional
  base::Time archived_at;
};

// A deterministic, fully-ordered snapshot of the organization state. Vectors
// are sorted (by order, then id) so serialization is stable.
struct OrganizationSnapshot {
  OrganizationSnapshot();
  OrganizationSnapshot(const OrganizationSnapshot&);
  OrganizationSnapshot(OrganizationSnapshot&&);
  OrganizationSnapshot& operator=(const OrganizationSnapshot&);
  OrganizationSnapshot& operator=(OrganizationSnapshot&&);
  ~OrganizationSnapshot();
  int schema_version = kOrganizationSchemaVersion;
  WorkspaceId default_workspace_id;
  std::vector<WorkspaceRecord> workspaces;
  std::vector<EssentialRecord> essentials;
  std::vector<TabMembershipRecord> memberships;
  std::vector<SplitGroupRecord> splits;
  std::vector<WindowWorkspaceState> window_states;
  std::vector<RoutingRule> routing_rules;
  std::vector<ArchivedTabRecord> archived_tabs;
};

// Change notification. Exactly one is emitted per committed mutation; none on a
// failed mutation.
enum class OrganizationChangeType {
  kInitialized,
  kWorkspaceCreated,
  kWorkspaceRenamed,
  kWorkspaceIconChanged,
  kWorkspaceReordered,
  kWorkspaceArchived,
  kWorkspaceRestored,
  kWorkspaceDeleted,
  kActiveWorkspaceChanged,
  kWindowForgotten,
  kMembershipAdded,
  kMembershipRemoved,
  kMembershipMoved,
  kMembershipRebound,
  kMembershipRoleChanged,
  kMembershipReordered,
  kTabActivated,
  kEssentialChanged,
  kEssentialRemoved,
  kSplitCreated,
  kSplitUpdated,
  kSplitDissolved,
  kRoutingRuleChanged,
  kRoutingRuleRemoved,
  kTabArchived,
  kArchivedTabRestored,
  kSnapshotLoaded,
};

struct OrganizationChange {
  OrganizationChangeType type;
  WorkspaceId workspace_id;       // set when relevant
  TabMembershipId membership_id;  // set when relevant
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_TYPES_H_
