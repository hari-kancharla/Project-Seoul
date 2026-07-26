// Project Seoul native organization engine.

#include "seoul/browser/organization/organization_model.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "seoul/browser/organization/organization_limits.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace seoul {

namespace {

// Trim ASCII whitespace; used only to reject empty/whitespace-only names.
std::string_view TrimView(std::string_view s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string_view::npos) {
    return std::string_view();
  }
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

bool GlobMatch(std::string_view pattern, std::string_view text) {
  // Simple '*' glob (no regex). '*' matches any run including empty. Linear DP.
  size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
  while (t < text.size()) {
    if (p < pattern.size() && (pattern[p] == text[t])) {
      ++p;
      ++t;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      mark = t;
    } else if (star != std::string_view::npos) {
      p = star + 1;
      t = ++mark;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') {
    ++p;
  }
  return p == pattern.size();
}

} // namespace

OrganizationModel::OrganizationModel()
    : OrganizationModel(base::BindRepeating(&base::Time::Now)) {}

OrganizationModel::OrganizationModel(Clock clock) : clock_(std::move(clock)) {}

OrganizationModel::~OrganizationModel() = default;

base::Time OrganizationModel::Now() const { return clock_.Run(); }

bool OrganizationModel::ValidName(std::string_view name) const {
  return !TrimView(name).empty() && name.size() <= kMaxNameLength;
}

int OrganizationModel::NextWorkspaceOrder() const {
  int max_order = -1;
  for (const auto &[id, w] : workspaces_) {
    max_order = std::max(max_order, w.order);
  }
  return max_order + 1;
}

int OrganizationModel::NextOrderInWorkspace(
    const WorkspaceId &workspace_id) const {
  int max_order = -1;
  for (const auto &[id, m] : memberships_) {
    if (m.workspace_id == workspace_id) {
      max_order = std::max(max_order, m.order);
    }
  }
  return max_order + 1;
}

size_t OrganizationModel::MembershipsInWorkspace(
    const WorkspaceId &workspace_id) const {
  size_t n = 0;
  for (const auto &[id, m] : memberships_) {
    if (m.workspace_id == workspace_id) {
      ++n;
    }
  }
  return n;
}

size_t
OrganizationModel::SplitsInWorkspace(const WorkspaceId &workspace_id) const {
  size_t n = 0;
  for (const auto &[id, s] : splits_) {
    if (s.workspace_id == workspace_id) {
      ++n;
    }
  }
  return n;
}

const WorkspaceRecord *
OrganizationModel::FindWorkspace(const WorkspaceId &id) const {
  auto it = workspaces_.find(id);
  return it == workspaces_.end() ? nullptr : &it->second;
}

const TabMembershipRecord *
OrganizationModel::FindMembership(const TabMembershipId &id) const {
  auto it = memberships_.find(id);
  return it == memberships_.end() ? nullptr : &it->second;
}

TabMembershipId
OrganizationModel::FindMembershipIdByTabKey(std::string_view tab_key) const {
  auto it = tab_index_.find(std::string(tab_key));
  return it == tab_index_.end() ? TabMembershipId() : it->second;
}

const EssentialRecord *
OrganizationModel::FindEssential(const EssentialId &id) const {
  auto it = essentials_.find(id);
  return it == essentials_.end() ? nullptr : &it->second;
}

const SplitGroupRecord *
OrganizationModel::FindSplit(const SplitGroupId &id) const {
  auto it = splits_.find(id);
  return it == splits_.end() ? nullptr : &it->second;
}

SplitGroupId OrganizationModel::FindSplitIdByUpstreamToken(
    std::string_view upstream_token) const {
  if (upstream_token.empty()) {
    return SplitGroupId();
  }
  // Bounded scan: the number of splits is capped by organization_limits.h.
  for (const auto &[id, s] : splits_) {
    if (s.upstream_split_token == upstream_token) {
      return id;
    }
  }
  return SplitGroupId();
}

WorkspaceId
OrganizationModel::ActiveWorkspaceForWindow(std::string_view window_key) const {
  auto it = window_active_.find(std::string(window_key));
  return it == window_active_.end() ? WorkspaceId() : it->second;
}

WorkspaceId
OrganizationModel::PickFallbackWorkspace(const WorkspaceId &excluded) const {
  // Deterministic: prefer the default (if eligible), otherwise the lowest-order
  // non-archived workspace, ties broken by id.
  const WorkspaceRecord *best = nullptr;
  for (const auto &[id, w] : workspaces_) {
    if (id == excluded || w.archived) {
      continue;
    }
    if (w.is_default) {
      return id;
    }
    if (!best || w.order < best->order ||
        (w.order == best->order && id < best->id)) {
      best = &w;
    }
  }
  return best ? best->id : WorkspaceId();
}

void OrganizationModel::Notify(const OrganizationChange &change) {
  notifying_ = true;
  for (auto &observer : observers_) {
    observer.OnOrganizationChanged(change);
  }
  notifying_ = false;
}

void OrganizationModel::AddObserver(OrganizationModelObserver *observer) {
  observers_.AddObserver(observer);
}

void OrganizationModel::RemoveObserver(OrganizationModelObserver *observer) {
  observers_.RemoveObserver(observer);
}

// --- Initialization ---

MutationStatus OrganizationModel::EnsureDefaultWorkspace() {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  for (const auto &[id, w] : workspaces_) {
    if (w.is_default) {
      return Ok(); // already initialized; idempotent
    }
  }
  if (workspaces_.size() >= kMaxWorkspaces) {
    return Err(OrganizationError::kLimitExceeded);
  }
  WorkspaceRecord w;
  w.id = WorkspaceId::GenerateNew();
  w.name = "Default";
  w.order = NextWorkspaceOrder();
  w.created_at = Now();
  w.last_active_at = w.created_at;
  w.archived = false;
  w.is_default = true;
  default_workspace_ = w.id;
  const WorkspaceId id = w.id;
  workspaces_.emplace(id, std::move(w));
  Notify({OrganizationChangeType::kInitialized, id, TabMembershipId()});
  return Ok();
}

// --- Workspaces ---

MutationResult<WorkspaceId>
OrganizationModel::CreateWorkspace(std::string_view name) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (!ValidName(name)) {
    return Err(OrganizationError::kInvalidName);
  }
  if (workspaces_.size() >= kMaxWorkspaces) {
    return Err(OrganizationError::kLimitExceeded);
  }
  WorkspaceRecord w;
  w.id = WorkspaceId::GenerateNew();
  w.name = std::string(name);
  w.order = NextWorkspaceOrder();
  w.created_at = Now();
  w.last_active_at = w.created_at;
  const WorkspaceId id = w.id;
  workspaces_.emplace(id, std::move(w));
  Notify({OrganizationChangeType::kWorkspaceCreated, id, TabMembershipId()});
  return id;
}

MutationStatus OrganizationModel::RenameWorkspace(const WorkspaceId &id,
                                                  std::string_view name) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (!id.is_valid()) {
    return Err(OrganizationError::kInvalidId);
  }
  if (!ValidName(name)) {
    return Err(OrganizationError::kInvalidName);
  }
  auto it = workspaces_.find(id);
  if (it == workspaces_.end()) {
    return Err(OrganizationError::kWorkspaceNotFound);
  }
  it->second.name = std::string(name);
  Notify({OrganizationChangeType::kWorkspaceRenamed, id, TabMembershipId()});
  return Ok();
}

MutationStatus OrganizationModel::ReorderWorkspace(const WorkspaceId &id,
                                                   int new_order) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (new_order < 0) {
    return Err(OrganizationError::kInvalidOrder);
  }
  auto it = workspaces_.find(id);
  if (it == workspaces_.end()) {
    return Err(OrganizationError::kWorkspaceNotFound);
  }
  it->second.order = new_order;
  Notify({OrganizationChangeType::kWorkspaceReordered, id, TabMembershipId()});
  return Ok();
}

MutationStatus OrganizationModel::ArchiveWorkspace(const WorkspaceId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = workspaces_.find(id);
  if (it == workspaces_.end()) {
    return Err(OrganizationError::kWorkspaceNotFound);
  }
  if (it->second.is_default) {
    return Err(OrganizationError::kDefaultWorkspaceProtected);
  }
  if (it->second.archived) {
    return Err(OrganizationError::kNoOpRejected);
  }
  it->second.archived = true;
  // An archived workspace cannot be active: reassign any windows showing it to
  // a deterministic fallback, atomically within this mutation.
  const WorkspaceId fallback = PickFallbackWorkspace(id);
  for (auto &[window_key, active] : window_active_) {
    if (active == id) {
      active = fallback;
    }
  }
  Notify({OrganizationChangeType::kWorkspaceArchived, id, TabMembershipId()});
  return Ok();
}

MutationStatus OrganizationModel::RestoreWorkspace(const WorkspaceId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = workspaces_.find(id);
  if (it == workspaces_.end()) {
    return Err(OrganizationError::kWorkspaceNotFound);
  }
  if (!it->second.archived) {
    return Err(OrganizationError::kNoOpRejected);
  }
  it->second.archived = false;
  Notify({OrganizationChangeType::kWorkspaceRestored, id, TabMembershipId()});
  return Ok();
}

MutationStatus OrganizationModel::DeleteWorkspace(const WorkspaceId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = workspaces_.find(id);
  if (it == workspaces_.end()) {
    return Err(OrganizationError::kWorkspaceNotFound);
  }
  if (it->second.is_default) {
    return Err(OrganizationError::kDefaultWorkspaceProtected);
  }
  // Cascade: remove memberships (and their tab index entries), splits, and
  // routing rules scoped to this workspace. Atomic: all removals happen
  // together.
  for (auto m = memberships_.begin(); m != memberships_.end();) {
    if (m->second.workspace_id == id) {
      tab_index_.erase(m->second.tab_key);
      m = memberships_.erase(m);
    } else {
      ++m;
    }
  }
  for (auto s = splits_.begin(); s != splits_.end();) {
    s = (s->second.workspace_id == id) ? splits_.erase(s) : std::next(s);
  }
  for (auto r = routing_rules_.begin(); r != routing_rules_.end();) {
    const bool references_source = r->second.predicate.source_workspace == id;
    const bool references_target = r->second.result.disposition ==
                                       RoutingDisposition::kSpecificWorkspace &&
                                   r->second.result.target_workspace == id;
    r = (references_source || references_target) ? routing_rules_.erase(r)
                                                 : std::next(r);
  }
  const WorkspaceId fallback = PickFallbackWorkspace(id);
  for (auto &[window_key, active] : window_active_) {
    if (active == id) {
      active = fallback;
    }
  }
  workspaces_.erase(it);
  Notify({OrganizationChangeType::kWorkspaceDeleted, id, TabMembershipId()});
  return Ok();
}

MutationStatus
OrganizationModel::SetActiveWorkspaceForWindow(std::string_view window_key,
                                               const WorkspaceId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (window_key.empty()) {
    return Err(OrganizationError::kInvalidId);
  }
  auto it = workspaces_.find(id);
  if (it == workspaces_.end()) {
    return Err(OrganizationError::kWorkspaceNotFound);
  }
  if (it->second.archived) {
    return Err(OrganizationError::kArchivedWorkspaceCannotActivate);
  }
  window_active_[std::string(window_key)] = id;
  it->second.last_active_at = Now();
  Notify(
      {OrganizationChangeType::kActiveWorkspaceChanged, id, TabMembershipId()});
  return Ok();
}

MutationStatus OrganizationModel::ForgetWindow(std::string_view window_key) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = window_active_.find(std::string(window_key));
  if (it == window_active_.end()) {
    return Err(OrganizationError::kNoOpRejected);
  }
  window_active_.erase(it);
  Notify({OrganizationChangeType::kWindowForgotten, WorkspaceId(),
          TabMembershipId()});
  return Ok();
}

// --- Tab membership ---

MutationResult<TabMembershipId>
OrganizationModel::AddTabMembership(const WorkspaceId &workspace_id,
                                    std::string_view tab_key, TabRole role) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (tab_key.empty() || tab_key.size() > kMaxTabKeyLength) {
    return Err(OrganizationError::kInvalidId);
  }
  if (!FindWorkspace(workspace_id)) {
    return Err(OrganizationError::kWorkspaceNotFound);
  }
  const WorkspaceRecord *workspace = FindWorkspace(workspace_id);
  if (workspace->archived) {
    return Err(OrganizationError::kArchivedWorkspaceCannotActivate);
  }
  if (tab_index_.count(std::string(tab_key))) {
    return Err(OrganizationError::kDuplicateMembership);
  }
  if (MembershipsInWorkspace(workspace_id) >= kMaxMembershipsPerWorkspace) {
    return Err(OrganizationError::kLimitExceeded);
  }
  TabMembershipRecord m;
  m.id = TabMembershipId::GenerateNew();
  m.workspace_id = workspace_id;
  m.tab_key = std::string(tab_key);
  m.role = role;
  m.order = NextOrderInWorkspace(workspace_id);
  m.created_at = Now();
  m.last_active_at = m.created_at;
  const TabMembershipId id = m.id;
  tab_index_[m.tab_key] = id;
  memberships_.emplace(id, std::move(m));
  Notify({OrganizationChangeType::kMembershipAdded, workspace_id, id});
  return id;
}

MutationStatus
OrganizationModel::RemoveTabMembership(const TabMembershipId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = memberships_.find(id);
  if (it == memberships_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  const std::string tab_key = it->second.tab_key;
  const WorkspaceId workspace_id = it->second.workspace_id;
  // Remove from any split; dissolve the split if it falls below the minimum
  // arity.
  for (auto s = splits_.begin(); s != splits_.end();) {
    auto &panes = s->second.pane_tab_keys;
    panes.erase(std::remove(panes.begin(), panes.end(), tab_key), panes.end());
    if (panes.size() < kMinSplitPanes) {
      s = splits_.erase(s);
    } else {
      if (s->second.active_pane_index >= static_cast<int>(panes.size())) {
        s->second.active_pane_index = 0;
      }
      ++s;
    }
  }
  tab_index_.erase(tab_key);
  memberships_.erase(it);
  Notify({OrganizationChangeType::kMembershipRemoved, workspace_id, id});
  return Ok();
}

MutationStatus
OrganizationModel::MoveTabToWorkspace(const TabMembershipId &id,
                                      const WorkspaceId &target_workspace) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = memberships_.find(id);
  if (it == memberships_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  if (!FindWorkspace(target_workspace)) {
    return Err(OrganizationError::kWorkspaceNotFound);
  }
  const WorkspaceRecord *target = FindWorkspace(target_workspace);
  if (target->archived) {
    return Err(OrganizationError::kArchivedWorkspaceCannotActivate);
  }
  if (it->second.workspace_id == target_workspace) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (MembershipsInWorkspace(target_workspace) >= kMaxMembershipsPerWorkspace) {
    return Err(OrganizationError::kLimitExceeded);
  }
  const std::string tab_key = it->second.tab_key;
  // Moving across workspaces would make any split referencing this tab invalid,
  // so remove the tab from its split(s) first (dissolving below-minimum
  // splits).
  for (auto s = splits_.begin(); s != splits_.end();) {
    auto &panes = s->second.pane_tab_keys;
    panes.erase(std::remove(panes.begin(), panes.end(), tab_key), panes.end());
    if (panes.size() < kMinSplitPanes) {
      s = splits_.erase(s);
    } else {
      if (s->second.active_pane_index >= static_cast<int>(panes.size())) {
        s->second.active_pane_index = 0;
      }
      ++s;
    }
  }
  it->second.workspace_id = target_workspace;
  it->second.order = NextOrderInWorkspace(target_workspace);
  Notify({OrganizationChangeType::kMembershipMoved, target_workspace, id});
  return Ok();
}

MutationStatus
OrganizationModel::RebindTabMembership(const TabMembershipId &id,
                                       std::string_view new_tab_key) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (!id.is_valid() || new_tab_key.empty()) {
    return Err(OrganizationError::kInvalidId);
  }
  if (new_tab_key.size() > kMaxTabKeyLength) {
    return Err(OrganizationError::kLimitExceeded);
  }
  auto membership = memberships_.find(id);
  if (membership == memberships_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  const std::string old_tab_key = membership->second.tab_key;
  if (old_tab_key == new_tab_key) {
    return Ok();
  }
  const auto collision = tab_index_.find(std::string(new_tab_key));
  if (collision != tab_index_.end() && collision->second != id) {
    return Err(OrganizationError::kDuplicateMembership);
  }

  // All validation is complete before the durable identity and every split
  // reference are changed as one model mutation.
  tab_index_.erase(old_tab_key);
  membership->second.tab_key = std::string(new_tab_key);
  tab_index_.emplace(membership->second.tab_key, id);
  for (auto &[split_id, split] : splits_) {
    for (std::string &pane : split.pane_tab_keys) {
      if (pane == old_tab_key) {
        pane = membership->second.tab_key;
      }
    }
  }
  Notify({OrganizationChangeType::kMembershipRebound,
          membership->second.workspace_id, id});
  return Ok();
}

MutationStatus OrganizationModel::MarkTabTemporary(const TabMembershipId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = memberships_.find(id);
  if (it == memberships_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  if (it->second.role == TabRole::kTemporary) {
    return Err(OrganizationError::kNoOpRejected);
  }
  it->second.role = TabRole::kTemporary;
  it->second.saved_root_url.clear();
  Notify({OrganizationChangeType::kMembershipRoleChanged,
          it->second.workspace_id, id});
  return Ok();
}

MutationStatus OrganizationModel::RetainTab(const TabMembershipId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = memberships_.find(id);
  if (it == memberships_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  if (it->second.role == TabRole::kRetained) {
    return Err(OrganizationError::kNoOpRejected);
  }
  it->second.role = TabRole::kRetained;
  it->second.saved_root_url.clear();
  Notify({OrganizationChangeType::kMembershipRoleChanged,
          it->second.workspace_id, id});
  return Ok();
}

MutationStatus OrganizationModel::PinTab(const TabMembershipId &id,
                                         std::string_view saved_root_url) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (saved_root_url.size() > kMaxUrlLength) {
    return Err(OrganizationError::kInvalidId);
  }
  auto it = memberships_.find(id);
  if (it == memberships_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  it->second.role = TabRole::kPinned;
  it->second.saved_root_url = std::string(saved_root_url);
  Notify({OrganizationChangeType::kMembershipRoleChanged,
          it->second.workspace_id, id});
  return Ok();
}

MutationStatus OrganizationModel::UnpinTab(const TabMembershipId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = memberships_.find(id);
  if (it == memberships_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  if (it->second.role != TabRole::kPinned) {
    return Err(OrganizationError::kNoOpRejected);
  }
  // Unpinning keeps the tab; it becomes retained (not closed, not temporary).
  it->second.role = TabRole::kRetained;
  it->second.saved_root_url.clear();
  Notify({OrganizationChangeType::kMembershipRoleChanged,
          it->second.workspace_id, id});
  return Ok();
}

MutationStatus OrganizationModel::TouchTabActivated(const TabMembershipId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = memberships_.find(id);
  if (it == memberships_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  it->second.last_active_at = Now();
  Notify({OrganizationChangeType::kTabActivated, it->second.workspace_id, id});
  return Ok();
}

MutationStatus
OrganizationModel::ReorderTabMembership(const TabMembershipId &id, int order) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (order < 0) {
    return Err(OrganizationError::kInvalidOrder);
  }
  auto it = memberships_.find(id);
  if (it == memberships_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  if (it->second.order == order) {
    return Err(OrganizationError::kNoOpRejected);
  }
  it->second.order = order;
  Notify({OrganizationChangeType::kMembershipReordered, it->second.workspace_id,
          id});
  return Ok();
}

// --- Essentials ---

MutationResult<EssentialId> OrganizationModel::CreateOrUpdateEssential(
    const EssentialId &id, std::string_view name, std::string_view root_url) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (!ValidName(name)) {
    return Err(OrganizationError::kInvalidName);
  }
  const GURL parsed_url(root_url);
  if (root_url.empty() || root_url.size() > kMaxUrlLength ||
      !parsed_url.is_valid() || !parsed_url.SchemeIsHTTPOrHTTPS()) {
    return Err(OrganizationError::kInvalidUrl);
  }
  auto existing = essentials_.end();
  if (id.is_valid()) {
    existing = essentials_.find(id);
    if (existing == essentials_.end()) {
      return Err(OrganizationError::kEssentialNotFound);
    }
  }
  const std::string origin = url::Origin::Create(parsed_url).Serialize();
  for (const auto &[candidate_id, candidate] : essentials_) {
    if (candidate_id == id) {
      continue;
    }
    const GURL candidate_url(candidate.root_url);
    if (candidate_url.is_valid() &&
        url::Origin::Create(candidate_url).Serialize() == origin) {
      return Err(OrganizationError::kDuplicateEssential);
    }
  }
  if (id.is_valid()) {
    existing->second.name = std::string(name);
    existing->second.root_url = parsed_url.spec();
    Notify({OrganizationChangeType::kEssentialChanged, WorkspaceId(),
            TabMembershipId()});
    return id;
  }
  if (essentials_.size() >= kMaxEssentials) {
    return Err(OrganizationError::kLimitExceeded);
  }
  EssentialRecord e;
  e.id = EssentialId::GenerateNew();
  e.name = std::string(name);
  e.root_url = parsed_url.spec();
  int max_order = -1;
  for (const auto &[eid, rec] : essentials_) {
    max_order = std::max(max_order, rec.order);
  }
  e.order = max_order + 1;
  e.created_at = Now();
  const EssentialId new_id = e.id;
  essentials_.emplace(new_id, std::move(e));
  Notify({OrganizationChangeType::kEssentialChanged, WorkspaceId(),
          TabMembershipId()});
  return new_id;
}

MutationStatus OrganizationModel::RemoveEssential(const EssentialId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = essentials_.find(id);
  if (it == essentials_.end()) {
    return Err(OrganizationError::kEssentialNotFound);
  }
  essentials_.erase(it);
  Notify({OrganizationChangeType::kEssentialRemoved, WorkspaceId(),
          TabMembershipId()});
  return Ok();
}

// --- Splits ---

MutationResult<SplitGroupId> OrganizationModel::CreateSplitGroup(
    const WorkspaceId &workspace_id,
    const std::vector<std::string> &pane_tab_keys, double divider_ratio,
    std::string_view upstream_split_token) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (!FindWorkspace(workspace_id)) {
    return Err(OrganizationError::kWorkspaceNotFound);
  }
  const WorkspaceRecord *workspace = FindWorkspace(workspace_id);
  if (workspace->archived) {
    return Err(OrganizationError::kArchivedWorkspaceCannotActivate);
  }
  if (pane_tab_keys.size() < kMinSplitPanes ||
      pane_tab_keys.size() > kMaxSplitPanesV0) {
    return Err(OrganizationError::kInvalidSplitArity);
  }
  if (divider_ratio < kMinDividerRatio || divider_ratio > kMaxDividerRatio) {
    return Err(OrganizationError::kInvalidDividerRatio);
  }
  if (upstream_split_token.empty() ||
      upstream_split_token.size() > kMaxUpstreamSplitTokenLength) {
    return Err(OrganizationError::kInvalidUpstreamSplitToken);
  }
  if (FindSplitIdByUpstreamToken(upstream_split_token).is_valid()) {
    return Err(OrganizationError::kDuplicateSplitToken);
  }
  std::set<std::string> seen;
  for (const std::string &key : pane_tab_keys) {
    if (!seen.insert(key).second) {
      return Err(OrganizationError::kInvalidSplitArity); // duplicate pane
    }
    auto ti = tab_index_.find(key);
    if (ti == tab_index_.end()) {
      return Err(OrganizationError::kTabMembershipNotFound);
    }
    const TabMembershipRecord *m = FindMembership(ti->second);
    if (!m || m->workspace_id != workspace_id) {
      return Err(OrganizationError::kCrossWorkspaceSplit);
    }
  }
  if (SplitsInWorkspace(workspace_id) >= kMaxSplitGroupsPerWorkspace) {
    return Err(OrganizationError::kLimitExceeded);
  }
  SplitGroupRecord s;
  s.id = SplitGroupId::GenerateNew();
  s.workspace_id = workspace_id;
  s.pane_tab_keys = pane_tab_keys;
  s.divider_ratio = divider_ratio;
  s.active_pane_index = 0;
  s.upstream_split_token = std::string(upstream_split_token);
  s.created_at = Now();
  const SplitGroupId id = s.id;
  splits_.emplace(id, std::move(s));
  Notify(
      {OrganizationChangeType::kSplitCreated, workspace_id, TabMembershipId()});
  return id;
}

MutationStatus OrganizationModel::UpdateSplitLayout(const SplitGroupId &id,
                                                    double divider_ratio,
                                                    int active_pane_index) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = splits_.find(id);
  if (it == splits_.end()) {
    return Err(OrganizationError::kSplitGroupNotFound);
  }
  if (divider_ratio < kMinDividerRatio || divider_ratio > kMaxDividerRatio) {
    return Err(OrganizationError::kInvalidDividerRatio);
  }
  if (active_pane_index < 0 ||
      active_pane_index >= static_cast<int>(it->second.pane_tab_keys.size())) {
    return Err(OrganizationError::kInvalidActivePane);
  }
  it->second.divider_ratio = divider_ratio;
  it->second.active_pane_index = active_pane_index;
  Notify({OrganizationChangeType::kSplitUpdated, it->second.workspace_id,
          TabMembershipId()});
  return Ok();
}

MutationStatus OrganizationModel::DissolveSplitGroup(const SplitGroupId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = splits_.find(id);
  if (it == splits_.end()) {
    return Err(OrganizationError::kSplitGroupNotFound);
  }
  const WorkspaceId workspace_id = it->second.workspace_id;
  splits_.erase(it);
  Notify({OrganizationChangeType::kSplitDissolved, workspace_id,
          TabMembershipId()});
  return Ok();
}

MutationStatus OrganizationModel::ReplaceSplitGroupContents(
    std::string_view upstream_split_token,
    const std::vector<std::string> &pane_tab_keys, double divider_ratio,
    int active_pane_index) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (upstream_split_token.empty() ||
      upstream_split_token.size() > kMaxUpstreamSplitTokenLength) {
    return Err(OrganizationError::kInvalidUpstreamSplitToken);
  }
  const SplitGroupId existing_id =
      FindSplitIdByUpstreamToken(upstream_split_token);
  if (!existing_id.is_valid()) {
    return Err(OrganizationError::kSplitGroupNotFound);
  }
  auto it = splits_.find(existing_id);
  if (it == splits_.end()) {
    return Err(OrganizationError::kSplitGroupNotFound);
  }
  const WorkspaceId workspace_id = it->second.workspace_id;
  const WorkspaceRecord *workspace = FindWorkspace(workspace_id);
  if (!workspace || workspace->archived) {
    return Err(OrganizationError::kArchivedWorkspaceCannotActivate);
  }
  if (pane_tab_keys.size() < kMinSplitPanes ||
      pane_tab_keys.size() > kMaxSplitPanesV0) {
    return Err(OrganizationError::kInvalidSplitArity);
  }
  if (divider_ratio < kMinDividerRatio || divider_ratio > kMaxDividerRatio) {
    return Err(OrganizationError::kInvalidDividerRatio);
  }
  if (active_pane_index < 0 ||
      active_pane_index >= static_cast<int>(pane_tab_keys.size())) {
    return Err(OrganizationError::kInvalidActivePane);
  }
  std::set<std::string> seen;
  for (const std::string &key : pane_tab_keys) {
    if (!seen.insert(key).second) {
      return Err(OrganizationError::kInvalidSplitArity);
    }
    auto ti = tab_index_.find(key);
    if (ti == tab_index_.end()) {
      return Err(OrganizationError::kTabMembershipNotFound);
    }
    const TabMembershipRecord *m = FindMembership(ti->second);
    if (!m || m->workspace_id != workspace_id) {
      return Err(OrganizationError::kCrossWorkspaceSplit);
    }
  }
  it->second.pane_tab_keys = pane_tab_keys;
  it->second.divider_ratio = divider_ratio;
  it->second.active_pane_index = active_pane_index;
  Notify(
      {OrganizationChangeType::kSplitUpdated, workspace_id, TabMembershipId()});
  return Ok();
}

// --- Temporary-tab protection / auto-archive ---

std::vector<TabMembershipId> OrganizationModel::EligibleForAutoArchive(
    const std::map<std::string, TabLiveActivity> &activity, base::Time now,
    base::TimeDelta inactivity_threshold) const {
  std::set<std::string> in_split;
  for (const auto &[id, s] : splits_) {
    for (const std::string &key : s.pane_tab_keys) {
      in_split.insert(key);
    }
  }
  std::vector<TabMembershipId> eligible;
  for (const auto &[id, m] : memberships_) {
    if (m.role != TabRole::kTemporary) {
      continue; // only temporary tabs are auto-archive candidates
    }
    if (now - m.last_active_at < inactivity_threshold) {
      continue; // still active recently
    }
    if (in_split.count(m.tab_key)) {
      continue; // participating in a split is protective
    }
    auto a = activity.find(m.tab_key);
    if (a != activity.end()) {
      const TabLiveActivity &t = a->second;
      if (t.playing_media || t.has_active_download || t.has_active_task ||
          t.has_permission_prompt || t.in_split || t.has_devtools ||
          t.has_unsaved_form || t.loading) {
        continue; // protected by a live condition
      }
    }
    eligible.push_back(id);
  }
  return eligible;
}

// --- Archive ---

MutationStatus OrganizationModel::ArchiveTab(const TabMembershipId &id,
                                             std::string recovery_url,
                                             std::string title) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (recovery_url.size() > kMaxUrlLength || title.size() > kMaxNameLength) {
    return Err(OrganizationError::kInvalidId);
  }
  auto it = memberships_.find(id);
  if (it == memberships_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  const std::string tab_key = it->second.tab_key;
  const WorkspaceId workspace_id = it->second.workspace_id;
  // Remove from any split (dissolving below-minimum splits): an archived tab is
  // not live and cannot participate in a split.
  for (auto s = splits_.begin(); s != splits_.end();) {
    auto &panes = s->second.pane_tab_keys;
    panes.erase(std::remove(panes.begin(), panes.end(), tab_key), panes.end());
    if (panes.size() < kMinSplitPanes) {
      s = splits_.erase(s);
    } else {
      if (s->second.active_pane_index >= static_cast<int>(panes.size())) {
        s->second.active_pane_index = 0;
      }
      ++s;
    }
  }
  ArchivedTabRecord a;
  a.original_id = id;
  a.workspace_id = workspace_id;
  a.original_role = it->second.role;
  a.saved_root_url = recovery_url.empty() ? it->second.saved_root_url
                                          : std::move(recovery_url);
  a.title = std::move(title);
  a.archived_at = Now();
  // Bounded retention: evict the oldest archived record if at capacity.
  if (archived_.size() >= kMaxArchivedTabs) {
    auto oldest = archived_.begin();
    for (auto a2 = archived_.begin(); a2 != archived_.end(); ++a2) {
      if (a2->second.archived_at < oldest->second.archived_at) {
        oldest = a2;
      }
    }
    archived_.erase(oldest);
  }
  tab_index_.erase(tab_key);
  memberships_.erase(it);
  archived_.emplace(id, std::move(a)); // a tab is now archived, not live
  Notify({OrganizationChangeType::kTabArchived, workspace_id, id});
  return Ok();
}

const ArchivedTabRecord *
OrganizationModel::FindArchivedTab(const TabMembershipId &original_id) const {
  auto it = archived_.find(original_id);
  return it == archived_.end() ? nullptr : &it->second;
}

MutationStatus
OrganizationModel::AdoptRestoredTab(const TabMembershipId &original_id,
                                    const TabMembershipId &live_membership_id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto archived = archived_.find(original_id);
  auto live = memberships_.find(live_membership_id);
  if (archived == archived_.end() || live == memberships_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  WorkspaceId workspace_id = archived->second.workspace_id;
  const WorkspaceRecord *workspace = FindWorkspace(workspace_id);
  if (!workspace) {
    workspace_id = default_workspace_;
    workspace = FindWorkspace(workspace_id);
  }
  if (!workspace) {
    return Err(OrganizationError::kWorkspaceNotFound);
  }
  if (workspace->archived) {
    return Err(OrganizationError::kArchivedWorkspaceCannotActivate);
  }
  if (live->second.workspace_id != workspace_id &&
      MembershipsInWorkspace(workspace_id) >= kMaxMembershipsPerWorkspace) {
    return Err(OrganizationError::kLimitExceeded);
  }

  TabMembershipRecord &membership = live->second;
  membership.workspace_id = workspace_id;
  membership.role = archived->second.original_role == TabRole::kPinned
                        ? TabRole::kRetained
                        : archived->second.original_role;
  membership.saved_root_url.clear();
  membership.order = NextOrderInWorkspace(workspace_id);
  membership.last_active_at = Now();
  archived_.erase(archived);
  Notify({OrganizationChangeType::kArchivedTabRestored, workspace_id,
          live_membership_id});
  return Ok();
}

MutationResult<TabMembershipId>
OrganizationModel::RestoreArchivedTab(const TabMembershipId &original_id,
                                      std::string_view tab_key) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (tab_key.empty() || tab_key.size() > kMaxTabKeyLength) {
    return Err(OrganizationError::kInvalidId);
  }
  auto it = archived_.find(original_id);
  if (it == archived_.end()) {
    return Err(OrganizationError::kTabMembershipNotFound);
  }
  if (tab_index_.count(std::string(tab_key))) {
    return Err(OrganizationError::kDuplicateMembership);
  }
  // Restore into the original workspace if it still exists, otherwise the
  // default.
  WorkspaceId workspace_id = it->second.workspace_id;
  if (!FindWorkspace(workspace_id)) {
    workspace_id = default_workspace_;
    if (!FindWorkspace(workspace_id)) {
      return Err(OrganizationError::kWorkspaceNotFound);
    }
  }
  const WorkspaceRecord *target_workspace = FindWorkspace(workspace_id);
  if (target_workspace->archived) {
    return Err(OrganizationError::kArchivedWorkspaceCannotActivate);
  }
  if (MembershipsInWorkspace(workspace_id) >= kMaxMembershipsPerWorkspace) {
    return Err(OrganizationError::kLimitExceeded);
  }
  TabMembershipRecord m;
  m.id = TabMembershipId::GenerateNew();
  m.workspace_id = workspace_id;
  m.tab_key = std::string(tab_key);
  if (it->second.original_role == TabRole::kPinned) {
    m.role = TabRole::kRetained;
    m.saved_root_url.clear();
  } else {
    m.role = it->second.original_role;
    m.saved_root_url.clear();
  }
  m.order = NextOrderInWorkspace(workspace_id);
  m.created_at = Now();
  m.last_active_at = m.created_at;
  const TabMembershipId new_id = m.id;
  tab_index_[m.tab_key] = new_id;
  memberships_.emplace(new_id, std::move(m));
  archived_.erase(it);
  Notify({OrganizationChangeType::kArchivedTabRestored, workspace_id, new_id});
  return new_id;
}

// --- Routing ---

MutationResult<RoutingRuleId>
OrganizationModel::AddRoutingRule(const RoutingRule &rule) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (routing_rules_.size() >= kMaxRoutingRules) {
    return Err(OrganizationError::kLimitExceeded);
  }
  // Validate the predicate/result.
  if (rule.predicate.match_type != RoutingMatchType::kAnything &&
      (rule.predicate.pattern.empty() ||
       rule.predicate.pattern.size() > kMaxPatternLength)) {
    return Err(OrganizationError::kInvalidRoutingRule);
  }
  if (rule.predicate.source_workspace.is_valid() &&
      !FindWorkspace(rule.predicate.source_workspace)) {
    return Err(OrganizationError::kInvalidRoutingRule);
  }
  if (rule.result.disposition == RoutingDisposition::kSpecificWorkspace &&
      !FindWorkspace(rule.result.target_workspace)) {
    return Err(OrganizationError::kInvalidRoutingRule);
  }
  if ((rule.result.disposition == RoutingDisposition::kExternalApplication ||
       rule.result.disposition == RoutingDisposition::kAskUser) &&
      !rule.predicate.require_user_gesture) {
    return Err(OrganizationError::kInvalidRoutingRule);
  }
  RoutingRule stored = rule;
  if (!stored.id.is_valid()) {
    stored.id = RoutingRuleId::GenerateNew();
  } else if (routing_rules_.count(stored.id)) {
    return Err(OrganizationError::kInvalidRoutingRule); // duplicate id
  }
  const RoutingRuleId id = stored.id;
  routing_rules_.emplace(id, std::move(stored));
  Notify({OrganizationChangeType::kRoutingRuleChanged, WorkspaceId(),
          TabMembershipId()});
  return id;
}

MutationStatus OrganizationModel::UpdateRoutingRule(const RoutingRule &rule) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  if (!rule.id.is_valid()) {
    return Err(OrganizationError::kInvalidRoutingRule);
  }
  auto existing = routing_rules_.find(rule.id);
  if (existing == routing_rules_.end()) {
    return Err(OrganizationError::kRoutingRuleNotFound);
  }
  if (rule.predicate.match_type != RoutingMatchType::kAnything &&
      (rule.predicate.pattern.empty() ||
       rule.predicate.pattern.size() > kMaxPatternLength)) {
    return Err(OrganizationError::kInvalidRoutingRule);
  }
  if (rule.predicate.source_workspace.is_valid() &&
      !FindWorkspace(rule.predicate.source_workspace)) {
    return Err(OrganizationError::kInvalidRoutingRule);
  }
  if (rule.result.disposition == RoutingDisposition::kSpecificWorkspace &&
      !FindWorkspace(rule.result.target_workspace)) {
    return Err(OrganizationError::kInvalidRoutingRule);
  }
  if ((rule.result.disposition == RoutingDisposition::kExternalApplication ||
       rule.result.disposition == RoutingDisposition::kAskUser) &&
      !rule.predicate.require_user_gesture) {
    return Err(OrganizationError::kInvalidRoutingRule);
  }
  existing->second = rule;
  Notify({OrganizationChangeType::kRoutingRuleChanged, WorkspaceId(),
          TabMembershipId()});
  return Ok();
}

MutationStatus OrganizationModel::RemoveRoutingRule(const RoutingRuleId &id) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  auto it = routing_rules_.find(id);
  if (it == routing_rules_.end()) {
    return Err(OrganizationError::kRoutingRuleNotFound);
  }
  routing_rules_.erase(it);
  Notify({OrganizationChangeType::kRoutingRuleRemoved, WorkspaceId(),
          TabMembershipId()});
  return Ok();
}

RoutingResolution
OrganizationModel::EvaluateRouting(const RoutingRequest &request) const {
  return EvaluateRoutingInternal(request, nullptr);
}

RoutingResolution OrganizationModel::EvaluateRouting(
    const RoutingRequest &request,
    const std::set<RoutingRuleId> &eligible_rules) const {
  return EvaluateRoutingInternal(request, &eligible_rules);
}

RoutingResolution OrganizationModel::EvaluateRoutingInternal(
    const RoutingRequest &request,
    const std::set<RoutingRuleId> *eligible_rules) const {
  // Gather matching, enabled, currently-valid rules. Single pass; bounded by
  // routing_rules_.size() (<= kMaxRoutingRules). No chaining
  // (kMaxRoutingHops==1), so evaluation cannot loop.
  const RoutingRule *best = nullptr;
  for (const auto &[id, rule] : routing_rules_) {
    if (eligible_rules && !eligible_rules->contains(id)) {
      continue;
    }
    if (!rule.enabled) {
      continue;
    }
    if (rule.predicate.source_workspace.is_valid() &&
        !(rule.predicate.source_workspace == request.source_workspace)) {
      continue;
    }
    if (rule.predicate.require_user_gesture && !request.user_gesture) {
      continue;
    }
    bool matched = false;
    switch (rule.predicate.match_type) {
    case RoutingMatchType::kAnything:
      matched = true;
      break;
    case RoutingMatchType::kOriginExact:
      matched = (request.origin == rule.predicate.pattern);
      break;
    case RoutingMatchType::kUrlPrefix:
      matched = request.url.size() >= rule.predicate.pattern.size() &&
                request.url.compare(0, rule.predicate.pattern.size(),
                                    rule.predicate.pattern) == 0;
      break;
    case RoutingMatchType::kUrlGlob:
      matched = GlobMatch(rule.predicate.pattern, request.url);
      break;
    }
    if (!matched) {
      continue;
    }
    // A rule pointing at a now-invalid/archived workspace is skipped (safe).
    if (rule.result.disposition == RoutingDisposition::kSpecificWorkspace) {
      const WorkspaceRecord *w = FindWorkspace(rule.result.target_workspace);
      if (!w || w->archived) {
        continue;
      }
    }
    // Highest priority wins; ties broken deterministically by rule id.
    if (!best || rule.priority > best->priority ||
        (rule.priority == best->priority && rule.id < best->id)) {
      best = &rule;
    }
  }
  RoutingResolution resolution;
  if (best) {
    resolution.result = best->result;
    resolution.matched_rule = best->id;
    resolution.used_fallback = false;
  } else {
    resolution.result.disposition = request.requested_disposition;
    resolution.used_fallback = true;
  }
  return resolution;
}

// --- Snapshot / load ---

OrganizationSnapshot OrganizationModel::ToSnapshot() const {
  OrganizationSnapshot snap;
  snap.schema_version = kOrganizationSchemaVersion;
  snap.default_workspace_id = default_workspace_;
  for (const auto &[id, w] : workspaces_) {
    snap.workspaces.push_back(w);
  }
  std::sort(snap.workspaces.begin(), snap.workspaces.end(),
            [](const WorkspaceRecord &a, const WorkspaceRecord &b) {
              return a.order != b.order ? a.order < b.order : a.id < b.id;
            });
  for (const auto &[id, e] : essentials_) {
    snap.essentials.push_back(e);
  }
  std::sort(snap.essentials.begin(), snap.essentials.end(),
            [](const EssentialRecord &a, const EssentialRecord &b) {
              return a.order != b.order ? a.order < b.order : a.id < b.id;
            });
  for (const auto &[id, m] : memberships_) {
    snap.memberships.push_back(m);
  }
  std::sort(snap.memberships.begin(), snap.memberships.end(),
            [](const TabMembershipRecord &a, const TabMembershipRecord &b) {
              if (!(a.workspace_id == b.workspace_id)) {
                return a.workspace_id < b.workspace_id;
              }
              return a.order != b.order ? a.order < b.order : a.id < b.id;
            });
  for (const auto &[id, s] : splits_) {
    snap.splits.push_back(s);
  }
  std::sort(snap.splits.begin(), snap.splits.end(),
            [](const SplitGroupRecord &a, const SplitGroupRecord &b) {
              return a.id < b.id;
            });
  for (const auto &[key, ws] : window_active_) {
    snap.window_states.push_back({key, ws});
  }
  for (const auto &[id, r] : routing_rules_) {
    snap.routing_rules.push_back(r);
  }
  std::sort(snap.routing_rules.begin(), snap.routing_rules.end(),
            [](const RoutingRule &a, const RoutingRule &b) {
              return a.priority != b.priority ? a.priority > b.priority
                                              : a.id < b.id;
            });
  for (const auto &[id, a] : archived_) {
    snap.archived_tabs.push_back(a);
  }
  std::sort(snap.archived_tabs.begin(), snap.archived_tabs.end(),
            [](const ArchivedTabRecord &a, const ArchivedTabRecord &b) {
              return a.archived_at != b.archived_at
                         ? a.archived_at < b.archived_at
                         : a.original_id < b.original_id;
            });
  return snap;
}

MutationStatus
OrganizationModel::LoadSnapshot(const OrganizationSnapshot &snapshot) {
  if (notifying_) {
    return Err(OrganizationError::kNoOpRejected);
  }
  // Reject unknown/unsupported versions instead of downgrading.
  if (snapshot.schema_version != kOrganizationSchemaVersion) {
    return Err(OrganizationError::kUnsupportedSchema);
  }
  // Bounds.
  if (snapshot.workspaces.size() > kMaxWorkspaces ||
      snapshot.essentials.size() > kMaxEssentials ||
      snapshot.memberships.size() > kMaxTotalMemberships ||
      snapshot.splits.size() > kMaxTotalSplits ||
      snapshot.window_states.size() > kMaxWindowStates ||
      snapshot.routing_rules.size() > kMaxRoutingRules ||
      snapshot.archived_tabs.size() > kMaxArchivedTabs) {
    return Err(OrganizationError::kLimitExceeded);
  }

  // Build candidate state in locals; only commit if everything validates
  // (atomic).
  std::map<WorkspaceId, WorkspaceRecord> workspaces;
  int default_count = 0;
  for (const WorkspaceRecord &w : snapshot.workspaces) {
    if (!w.id.is_valid() || !ValidName(w.name)) {
      return Err(OrganizationError::kCorruptState);
    }
    if (w.order < 0) {
      return Err(OrganizationError::kInvalidOrder);
    }
    if (!workspaces.emplace(w.id, w).second) {
      return Err(OrganizationError::kCorruptState); // duplicate id
    }
    if (w.is_default) {
      ++default_count;
      if (w.archived) {
        return Err(
            OrganizationError::kCorruptState); // default cannot be archived
      }
    }
  }
  if (!workspaces.empty()) {
    if (default_count != 1 || !snapshot.default_workspace_id.is_valid() ||
        workspaces.find(snapshot.default_workspace_id) == workspaces.end() ||
        !workspaces.at(snapshot.default_workspace_id).is_default) {
      return Err(OrganizationError::kCorruptState);
    }
  }

  std::map<EssentialId, EssentialRecord> essentials;
  std::set<std::string> essential_origins;
  for (const EssentialRecord &e : snapshot.essentials) {
    const GURL parsed_url(e.root_url);
    if (!e.id.is_valid() || !ValidName(e.name) ||
        e.root_url.size() > kMaxUrlLength || !parsed_url.is_valid() ||
        !parsed_url.SchemeIsHTTPOrHTTPS()) {
      return Err(OrganizationError::kCorruptState);
    }
    if (!essential_origins
             .insert(url::Origin::Create(parsed_url).Serialize())
             .second) {
      return Err(OrganizationError::kCorruptState);
    }
    if (!essentials.emplace(e.id, e).second) {
      return Err(OrganizationError::kCorruptState);
    }
  }

  std::map<TabMembershipId, TabMembershipRecord> memberships;
  std::map<std::string, TabMembershipId> tab_index;
  std::map<WorkspaceId, size_t> per_workspace;
  for (const TabMembershipRecord &m : snapshot.memberships) {
    if (!m.id.is_valid() || m.tab_key.empty() ||
        m.tab_key.size() > kMaxTabKeyLength) {
      return Err(OrganizationError::kCorruptState);
    }
    if (m.order < 0) {
      return Err(OrganizationError::kInvalidOrder);
    }
    if (workspaces.find(m.workspace_id) == workspaces.end()) {
      return Err(OrganizationError::kCorruptState); // dangling workspace ref
    }
    if (!tab_index.emplace(m.tab_key, m.id).second) {
      return Err(OrganizationError::kCorruptState); // tab in two workspaces
    }
    if (!memberships.emplace(m.id, m).second) {
      return Err(OrganizationError::kCorruptState);
    }
    if (++per_workspace[m.workspace_id] > kMaxMembershipsPerWorkspace) {
      return Err(OrganizationError::kLimitExceeded);
    }
  }

  std::map<SplitGroupId, SplitGroupRecord> splits;
  std::map<WorkspaceId, size_t> splits_per_workspace;
  std::set<std::string> upstream_tokens;
  for (const SplitGroupRecord &s : snapshot.splits) {
    if (!s.id.is_valid() ||
        workspaces.find(s.workspace_id) == workspaces.end()) {
      return Err(OrganizationError::kCorruptState);
    }
    if (s.upstream_split_token.empty() ||
        s.upstream_split_token.size() > kMaxUpstreamSplitTokenLength) {
      return Err(OrganizationError::kInvalidUpstreamSplitToken);
    }
    if (!upstream_tokens.insert(s.upstream_split_token).second) {
      return Err(OrganizationError::kDuplicateSplitToken);
    }
    if (s.pane_tab_keys.size() < kMinSplitPanes ||
        s.pane_tab_keys.size() > kMaxSplitPanesV0) {
      return Err(OrganizationError::kInvalidSplitArity);
    }
    if (s.divider_ratio < kMinDividerRatio ||
        s.divider_ratio > kMaxDividerRatio) {
      return Err(OrganizationError::kInvalidDividerRatio);
    }
    if (s.active_pane_index < 0 ||
        s.active_pane_index >= static_cast<int>(s.pane_tab_keys.size())) {
      return Err(OrganizationError::kInvalidActivePane);
    }
    std::set<std::string> seen;
    for (const std::string &key : s.pane_tab_keys) {
      if (!seen.insert(key).second) {
        return Err(OrganizationError::kInvalidSplitArity);
      }
      auto ti = tab_index.find(key);
      if (ti == tab_index.end() ||
          memberships.at(ti->second).workspace_id != s.workspace_id) {
        return Err(OrganizationError::kCrossWorkspaceSplit);
      }
    }
    if (!splits.emplace(s.id, s).second) {
      return Err(OrganizationError::kCorruptState);
    }
    if (++splits_per_workspace[s.workspace_id] > kMaxSplitGroupsPerWorkspace) {
      return Err(OrganizationError::kLimitExceeded);
    }
  }

  std::map<RoutingRuleId, RoutingRule> routing;
  for (const RoutingRule &r : snapshot.routing_rules) {
    if (!r.id.is_valid()) {
      return Err(OrganizationError::kInvalidRoutingRule);
    }
    if (r.predicate.match_type != RoutingMatchType::kAnything &&
        (r.predicate.pattern.empty() ||
         r.predicate.pattern.size() > kMaxPatternLength)) {
      return Err(OrganizationError::kInvalidRoutingRule);
    }
    if (r.predicate.source_workspace.is_valid() &&
        workspaces.find(r.predicate.source_workspace) == workspaces.end()) {
      return Err(OrganizationError::kInvalidRoutingRule);
    }
    if (r.result.disposition == RoutingDisposition::kSpecificWorkspace &&
        workspaces.find(r.result.target_workspace) == workspaces.end()) {
      return Err(OrganizationError::kInvalidRoutingRule);
    }
    if ((r.result.disposition == RoutingDisposition::kExternalApplication ||
         r.result.disposition == RoutingDisposition::kAskUser) &&
        !r.predicate.require_user_gesture) {
      return Err(OrganizationError::kInvalidRoutingRule);
    }
    if (!routing.emplace(r.id, r).second) {
      return Err(OrganizationError::kInvalidRoutingRule);
    }
  }

  std::map<TabMembershipId, ArchivedTabRecord> archived;
  for (const ArchivedTabRecord &a : snapshot.archived_tabs) {
    if (!a.original_id.is_valid()) {
      return Err(OrganizationError::kCorruptState);
    }
    if (workspaces.find(a.workspace_id) == workspaces.end()) {
      return Err(OrganizationError::kCorruptState);
    }
    // A tab cannot be both archived and live.
    if (memberships.find(a.original_id) != memberships.end()) {
      return Err(OrganizationError::kCorruptState);
    }
    if (!archived.emplace(a.original_id, a).second) {
      return Err(OrganizationError::kCorruptState);
    }
  }

  std::map<std::string, WorkspaceId> window_active;
  for (const WindowWorkspaceState &ws : snapshot.window_states) {
    if (ws.window_key.empty() || ws.window_key.size() > kMaxTabKeyLength) {
      return Err(OrganizationError::kCorruptState);
    }
    if (!window_active.emplace(ws.window_key, ws.active_workspace_id).second) {
      return Err(OrganizationError::kCorruptState); // duplicate window key
    }
    auto w = workspaces.find(ws.active_workspace_id);
    if (w == workspaces.end() || w->second.archived) {
      return Err(
          OrganizationError::kCorruptState); // active must exist + be live
    }
  }

  // Commit (atomic swap).
  workspaces_ = std::move(workspaces);
  essentials_ = std::move(essentials);
  memberships_ = std::move(memberships);
  tab_index_ = std::move(tab_index);
  splits_ = std::move(splits);
  routing_rules_ = std::move(routing);
  archived_ = std::move(archived);
  window_active_ = std::move(window_active);
  default_workspace_ = snapshot.default_workspace_id;
  Notify({OrganizationChangeType::kSnapshotLoaded, default_workspace_,
          TabMembershipId()});
  return Ok();
}

} // namespace seoul
