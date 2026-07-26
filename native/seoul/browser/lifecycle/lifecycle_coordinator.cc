// Project Seoul native lifecycle bridge.

#include "seoul/browser/lifecycle/lifecycle_coordinator.h"

#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "seoul/browser/organization/organization_errors.h"
#include "seoul/browser/organization/organization_limits.h"
#include "seoul/browser/organization/organization_model.h"

namespace seoul {

LifecycleCoordinator::LifecycleCoordinator(OrganizationModel *model)
    : model_(model) {}

LifecycleCoordinator::~LifecycleCoordinator() = default;

void LifecycleCoordinator::SetReconciliationRequestCallback(
    ReconciliationRequestCallback callback) {
  reconciliation_request_callback_ = std::move(callback);
}

void LifecycleCoordinator::RequestReconciliation() {
  // Run the real reconciliation path (began -> bounded window rescan ->
  // completed) wired by the owning service. The shell recovery control routes
  // here instead of synthesizing reconciliation events itself.
  if (reconciliation_request_callback_) {
    reconciliation_request_callback_.Run();
  }
}

void LifecycleCoordinator::SetConfirmationCallback(
    base::RepeatingCallback<void(const NormalizedEvent &)> callback) {
  confirmation_callback_ = std::move(callback);
}

void LifecycleCoordinator::SetPinHandlingSuppressor(
    base::RepeatingCallback<bool(LiveTabKey tab)> suppressor) {
  pin_handling_suppressor_ = std::move(suppressor);
}

bool LifecycleCoordinator::ExpectTabInsertion(LiveWindowKey window,
                                              LiveTabKey tab, TabRole role) {
  if (shutting_down_ || reconciling_ || !window.is_valid() || !tab.is_valid() ||
      role == TabRole::kPinned ||
      expected_insertions_.size() >= kMaxExpectedInsertions ||
      expected_insertions_.contains(tab) ||
      model_->FindMembershipIdByTabKey(tab.value()).is_valid()) {
    return false;
  }
  expected_insertions_.emplace(tab, ExpectedInsertion{window, role});
  return true;
}

void LifecycleCoordinator::CancelExpectedTabInsertion(LiveTabKey tab) {
  expected_insertions_.erase(tab);
}

bool LifecycleCoordinator::ExpectTabArchival(LiveWindowKey window,
                                             LiveTabKey tab,
                                             std::string recovery_url,
                                             std::string title) {
  if (shutting_down_ || reconciling_ || !window.is_valid() || !tab.is_valid() ||
      recovery_url.empty() ||
      expected_archivals_.size() >= kMaxExpectedArchivals ||
      expected_archivals_.contains(tab)) {
    return false;
  }
  const TabMembershipId membership =
      model_->FindMembershipIdByTabKey(tab.value());
  const TabMembershipRecord *record =
      membership.is_valid() ? model_->FindMembership(membership) : nullptr;
  if (!record || record->role != TabRole::kTemporary) {
    return false;
  }
  expected_archivals_.emplace(
      tab, ExpectedArchival{window, std::move(recovery_url), std::move(title)});
  return true;
}

void LifecycleCoordinator::CancelExpectedTabArchival(LiveTabKey tab) {
  expected_archivals_.erase(tab);
}

bool LifecycleCoordinator::ShouldAcceptEvent(
    const NormalizedEvent &event) const {
  if (shutting_down_ && event.type != NormalizedEventType::kShutdownBegan) {
    return false;
  }
  if (!reconciliation_required_) {
    return true;
  }
  return event.type == NormalizedEventType::kReconciliationBegan ||
         event.type == NormalizedEventType::kReconciliationCompleted ||
         event.type == NormalizedEventType::kShutdownBegan;
}

void LifecycleCoordinator::HandleQueueOverflow() {
  queue_overflow_ = true;
  reconciliation_required_ = true;
  pending_events_.clear();
  expected_insertions_.clear();
  expected_archivals_.clear();
  if (reconciliation_request_callback_) {
    reconciliation_request_callback_.Run();
  }
}

bool LifecycleCoordinator::EventsAreDuplicate(const NormalizedEvent &a,
                                              const NormalizedEvent &b) {
  return a.type == b.type && a.window == b.window && a.tab == b.tab &&
         a.insert_kind == b.insert_kind && a.removal_kind == b.removal_kind &&
         a.pinned == b.pinned && a.order_index == b.order_index &&
         a.upstream_split_token == b.upstream_split_token &&
         a.split_pane_a == b.split_pane_a && a.split_pane_b == b.split_pane_b &&
         a.divider_ratio == b.divider_ratio &&
         a.split_visuals_intermediate == b.split_visuals_intermediate &&
         a.batch_sequence == b.batch_sequence;
}

void LifecycleCoordinator::OnNormalizedEvent(const NormalizedEvent &event) {
  if (!ShouldAcceptEvent(event)) {
    return;
  }
  if (applying_) {
    if (pending_events_.size() >= kMaxQueuedEvents) {
      HandleQueueOverflow();
      return;
    }
    if (!pending_events_.empty() &&
        EventsAreDuplicate(pending_events_.back(), event)) {
      return;
    }
    pending_events_.push_back(event);
    return;
  }

  ProcessEvent(event);
  DrainPendingEvents();
}

void LifecycleCoordinator::ProcessEvent(const NormalizedEvent &event) {
  base::AutoReset<bool> applying(&applying_, true);
  const MutationOrigin effective =
      reconciling_ ? MutationOrigin::kStartupReconciliation : event.origin;
  base::AutoReset<MutationOrigin> origin(&current_origin_, effective);

  switch (event.type) {
  case NormalizedEventType::kWindowDiscovered:
    HandleWindowDiscovered(event);
    break;
  case NormalizedEventType::kWindowClosing:
    break;
  case NormalizedEventType::kWindowDestroyed:
  case NormalizedEventType::kTabStripDestroyed:
    HandleWindowGone(event);
    break;
  case NormalizedEventType::kTabInserted:
    HandleTabInserted(event);
    break;
  case NormalizedEventType::kTabRemoved:
    HandleTabRemoved(event);
    break;
  case NormalizedEventType::kTabMoved:
    HandleTabMoved(event);
    break;
  case NormalizedEventType::kActiveTabChanged:
    HandleActiveTabChanged(event);
    break;
  case NormalizedEventType::kPinnedStateChanged:
    HandlePinnedStateChanged(event);
    break;
  case NormalizedEventType::kTabReplaced:
    break;
  case NormalizedEventType::kTabCloseCancelled:
    CancelExpectedTabArchival(event.tab);
    break;
  case NormalizedEventType::kSplitAdded:
    HandleSplitAdded(event);
    break;
  case NormalizedEventType::kSplitRemoved:
    HandleSplitRemoved(event);
    break;
  case NormalizedEventType::kSplitContentsChanged:
    HandleSplitContentsChanged(event);
    break;
  case NormalizedEventType::kSplitVisualsChanged:
    HandleSplitVisualsChanged(event);
    break;
  case NormalizedEventType::kReconciliationBegan:
    reconciling_ = true;
    pending_transfers_.clear();
    expected_insertions_.clear();
    expected_archivals_.clear();
    break;
  case NormalizedEventType::kReconciliationCompleted:
    HandleReconciliationCompleted(event);
    break;
  case NormalizedEventType::kShutdownBegan:
    shutting_down_ = true;
    pending_transfers_.clear();
    expected_insertions_.clear();
    expected_archivals_.clear();
    pending_events_.clear();
    break;
  }

  if (confirmation_callback_) {
    confirmation_callback_.Run(event);
  }
}

void LifecycleCoordinator::DrainPendingEvents() {
  if (draining_ || shutting_down_) {
    return;
  }
  base::AutoReset<bool> draining(&draining_, true);
  while (!pending_events_.empty() && !applying_) {
    NormalizedEvent next = std::move(pending_events_.front());
    pending_events_.pop_front();
    ProcessEvent(next);
  }
}

void LifecycleCoordinator::HandleWindowDiscovered(
    const NormalizedEvent &event) {
  if (!event.window.is_valid()) {
    return;
  }
  if (known_windows_.count(event.window)) {
    return;
  }
  known_windows_.insert(event.window);

  std::ignore = model_->EnsureDefaultWorkspace();
  const std::string window_key = event.window.value();
  const WorkspaceId active = model_->ActiveWorkspaceForWindow(window_key);
  if (!active.is_valid()) {
    std::ignore = model_->SetActiveWorkspaceForWindow(
        window_key, model_->default_workspace());
  }
}

void LifecycleCoordinator::HandleWindowGone(const NormalizedEvent &event) {
  if (!event.window.is_valid()) {
    return;
  }
  known_windows_.erase(event.window);
  ExpireTransfersForWindow(event.window);
  ExpireExpectedInsertionsForWindow(event.window);
  ExpireExpectedArchivalsForWindow(event.window);
  std::ignore = model_->ForgetWindow(event.window.value());
}

void LifecycleCoordinator::HandleTabInserted(const NormalizedEvent &event) {
  if (!event.tab.is_valid()) {
    return;
  }
  const std::string tab_key = event.tab.value();
  TabMembershipId existing = model_->FindMembershipIdByTabKey(tab_key);
  if (!existing.is_valid() && event.restored_membership.is_valid() &&
      model_->RebindTabMembership(event.restored_membership, tab_key)
          .has_value()) {
    existing = event.restored_membership;
  }
  TabRole insertion_role = kNewTabRole;
  auto expected = expected_insertions_.find(event.tab);
  if (expected != expected_insertions_.end()) {
    if (expected->second.window == event.window) {
      insertion_role = expected->second.role;
    }
    expected_insertions_.erase(expected);
  }

  auto pending = pending_transfers_.find(event.tab);
  if (pending != pending_transfers_.end()) {
    pending_transfers_.erase(pending);
    if (existing.is_valid() && event.order_index >= 0) {
      std::ignore = model_->ReorderTabMembership(existing, event.order_index);
    }
    return;
  }

  if (existing.is_valid()) {
    if (event.order_index >= 0) {
      std::ignore = model_->ReorderTabMembership(existing, event.order_index);
    }
    return;
  }

  if (event.insert_kind == TabInsertKind::kRestored) {
    return;
  }

  const WorkspaceId ws = ActiveOrDefaultWorkspace(event.window);
  if (!ws.is_valid()) {
    return;
  }
  const MutationResult<TabMembershipId> result =
      model_->AddTabMembership(ws, tab_key, insertion_role);
  if (result.has_value() && event.order_index >= 0) {
    std::ignore =
        model_->ReorderTabMembership(result.value(), event.order_index);
  }
}

void LifecycleCoordinator::HandleTabRemoved(const NormalizedEvent &event) {
  if (!event.tab.is_valid()) {
    return;
  }
  auto expected_archive = expected_archivals_.find(event.tab);
  switch (event.removal_kind) {
  case TabRemovalKind::kGenuineClose: {
    const TabMembershipId id =
        model_->FindMembershipIdByTabKey(event.tab.value());
    if (id.is_valid()) {
      const bool should_archive =
          expected_archive != expected_archivals_.end() &&
          expected_archive->second.window == event.window;
      if (should_archive) {
        const MutationStatus archived =
            model_->ArchiveTab(id, expected_archive->second.recovery_url,
                               expected_archive->second.title);
        if (!archived.has_value()) {
          std::ignore = model_->RemoveTabMembership(id);
        }
      } else {
        std::ignore = model_->RemoveTabMembership(id);
      }
    }
    expected_archivals_.erase(event.tab);
    pending_transfers_.erase(event.tab);
    break;
  }
  case TabRemovalKind::kTransferOut: {
    expected_archivals_.erase(event.tab);
    const TabMembershipId id =
        model_->FindMembershipIdByTabKey(event.tab.value());
    const TabMembershipRecord *m =
        id.is_valid() ? model_->FindMembership(id) : nullptr;
    if (m) {
      EvictOldestTransferIfNeeded();
      pending_transfers_[event.tab] = {m->workspace_id, ++sequence_};
    }
    break;
  }
  case TabRemovalKind::kSidePanel:
  case TabRemovalKind::kReplaced:
  case TabRemovalKind::kWindowShutdown:
  case TabRemovalKind::kStripDestroyed:
  case TabRemovalKind::kUnknown:
    expected_archivals_.erase(event.tab);
    pending_transfers_.erase(event.tab);
    break;
  }
}

void LifecycleCoordinator::HandleTabMoved(const NormalizedEvent &event) {
  if (!event.tab.is_valid() || event.order_index < 0) {
    return;
  }
  const TabMembershipId id =
      model_->FindMembershipIdByTabKey(event.tab.value());
  if (!id.is_valid()) {
    return;
  }
  std::ignore = model_->ReorderTabMembership(id, event.order_index);
}

void LifecycleCoordinator::HandleActiveTabChanged(
    const NormalizedEvent &event) {
  if (!event.tab.is_valid()) {
    return;
  }
  const TabMembershipId id =
      model_->FindMembershipIdByTabKey(event.tab.value());
  if (!id.is_valid()) {
    return;
  }
  std::ignore = model_->TouchTabActivated(id);

  const TabMembershipRecord *m = model_->FindMembership(id);
  if (m && event.window.is_valid() && m->workspace_id.is_valid()) {
    const WorkspaceId current =
        model_->ActiveWorkspaceForWindow(event.window.value());
    if (!(m->workspace_id == current)) {
      std::ignore = model_->SetActiveWorkspaceForWindow(event.window.value(),
                                                        m->workspace_id);
    }
  }
}

void LifecycleCoordinator::HandlePinnedStateChanged(
    const NormalizedEvent &event) {
  if (!event.tab.is_valid()) {
    return;
  }
  if (pin_handling_suppressor_ && pin_handling_suppressor_.Run(event.tab)) {
    return;
  }
  const TabMembershipId id =
      model_->FindMembershipIdByTabKey(event.tab.value());
  if (!id.is_valid()) {
    return;
  }
  const TabMembershipRecord *m = model_->FindMembership(id);
  if (!m) {
    return;
  }
  if (event.pinned) {
    if (m->role == TabRole::kPinned) {
      return;
    }
    std::ignore = model_->PinTab(id, std::string());
  } else {
    if (m->role != TabRole::kPinned) {
      return;
    }
    std::ignore = model_->UnpinTab(id);
  }
}

void LifecycleCoordinator::HandleSplitAdded(const NormalizedEvent &event) {
  if (event.upstream_split_token.empty() || !event.split_pane_a.is_valid() ||
      !event.split_pane_b.is_valid()) {
    return;
  }
  if (FindSplitByToken(event.upstream_split_token).is_valid()) {
    return;
  }
  const TabMembershipId a =
      model_->FindMembershipIdByTabKey(event.split_pane_a.value());
  const TabMembershipId b =
      model_->FindMembershipIdByTabKey(event.split_pane_b.value());
  const TabMembershipRecord *ma =
      a.is_valid() ? model_->FindMembership(a) : nullptr;
  const TabMembershipRecord *mb =
      b.is_valid() ? model_->FindMembership(b) : nullptr;
  if (!ma || !mb) {
    return;
  }
  if (!(ma->workspace_id == mb->workspace_id)) {
    return;
  }
  const std::vector<std::string> panes = {event.split_pane_a.value(),
                                          event.split_pane_b.value()};
  std::ignore = model_->CreateSplitGroup(
      ma->workspace_id, panes, event.divider_ratio, event.upstream_split_token);
}

void LifecycleCoordinator::HandleSplitRemoved(const NormalizedEvent &event) {
  const SplitGroupId id = FindSplitByToken(event.upstream_split_token);
  if (id.is_valid()) {
    std::ignore = model_->DissolveSplitGroup(id);
  }
}

void LifecycleCoordinator::HandleSplitContentsChanged(
    const NormalizedEvent &event) {
  if (event.upstream_split_token.empty() || !event.split_pane_a.is_valid() ||
      !event.split_pane_b.is_valid()) {
    const SplitGroupId stale = FindSplitByToken(event.upstream_split_token);
    if (stale.is_valid()) {
      std::ignore = model_->DissolveSplitGroup(stale);
    }
    return;
  }

  const SplitGroupId existing = FindSplitByToken(event.upstream_split_token);
  double ratio = event.divider_ratio;
  int active_pane = 0;
  if (existing.is_valid()) {
    const SplitGroupRecord *s = model_->FindSplit(existing);
    if (s) {
      if (event.has_divider_ratio) {
        if (ratio < kMinDividerRatio || ratio > kMaxDividerRatio) {
          return;
        }
      } else {
        ratio = s->divider_ratio;
      }
      active_pane = s->active_pane_index;
    }
  } else if (event.has_divider_ratio &&
             (ratio < kMinDividerRatio || ratio > kMaxDividerRatio)) {
    return;
  }

  const std::vector<std::string> panes = {event.split_pane_a.value(),
                                          event.split_pane_b.value()};
  const MutationStatus replaced = model_->ReplaceSplitGroupContents(
      event.upstream_split_token, panes, ratio, active_pane);
  if (!replaced.has_value() && existing.is_valid()) {
    std::ignore = model_->DissolveSplitGroup(existing);
  }
}

void LifecycleCoordinator::HandleSplitVisualsChanged(
    const NormalizedEvent &event) {
  if (event.split_visuals_intermediate) {
    return;
  }
  const SplitGroupId id = FindSplitByToken(event.upstream_split_token);
  if (!id.is_valid()) {
    return;
  }
  const SplitGroupRecord *s = model_->FindSplit(id);
  const int active = s ? s->active_pane_index : 0;
  std::ignore = model_->UpdateSplitLayout(id, event.divider_ratio, active);
}

void LifecycleCoordinator::HandleReconciliationCompleted(
    const NormalizedEvent &event) {
  (void)event;
  reconciling_ = false;
  pending_transfers_.clear();
  if (reconciliation_required_) {
    reconciliation_required_ = false;
    queue_overflow_ = false;
  }
}

WorkspaceId
LifecycleCoordinator::ActiveOrDefaultWorkspace(const LiveWindowKey &window) {
  WorkspaceId ws;
  if (window.is_valid()) {
    ws = model_->ActiveWorkspaceForWindow(window.value());
  }
  if (!ws.is_valid()) {
    ws = model_->default_workspace();
  }
  return ws;
}

SplitGroupId
LifecycleCoordinator::FindSplitByToken(const std::string &token) const {
  return model_->FindSplitIdByUpstreamToken(token);
}

void LifecycleCoordinator::EvictOldestTransferIfNeeded() {
  if (pending_transfers_.size() < kMaxPendingTransfers) {
    return;
  }
  auto oldest = pending_transfers_.begin();
  for (auto it = pending_transfers_.begin(); it != pending_transfers_.end();
       ++it) {
    if (it->second.sequence < oldest->second.sequence) {
      oldest = it;
    }
  }
  pending_transfers_.erase(oldest);
}

void LifecycleCoordinator::ExpireTransfersForWindow(
    const LiveWindowKey &window) {
  (void)window;
  pending_transfers_.clear();
}

void LifecycleCoordinator::ExpireExpectedInsertionsForWindow(
    const LiveWindowKey &window) {
  std::erase_if(expected_insertions_, [&window](const auto &entry) {
    return entry.second.window == window;
  });
}

void LifecycleCoordinator::ExpireExpectedArchivalsForWindow(
    const LiveWindowKey &window) {
  std::erase_if(expected_archivals_, [&window](const auto &entry) {
    return entry.second.window == window;
  });
}

} // namespace seoul
