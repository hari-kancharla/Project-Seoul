// Project Seoul native lifecycle bridge.
// The coordinator consumes NormalizedEvents and updates the OrganizationModel.
// Outbound mutations remain owned by CommandExecutor; the coordinator is the
// Chromium-free inbound authority that reconciles their observer events and
// ordinary user/browser lifecycle changes into the organization model.

#ifndef SEOUL_BROWSER_LIFECYCLE_LIFECYCLE_COORDINATOR_H_
#define SEOUL_BROWSER_LIFECYCLE_LIFECYCLE_COORDINATOR_H_

#include <cstddef>
#include <deque>
#include <map>
#include <set>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "seoul/browser/lifecycle/lifecycle_event_sink.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/organization/organization_ids.h"
#include "seoul/browser/organization/organization_types.h"

namespace seoul {

class OrganizationModel;

using ReconciliationRequestCallback = base::RepeatingClosure;

class LifecycleCoordinator : public LifecycleEventSink {
 public:
  explicit LifecycleCoordinator(OrganizationModel* model);
  LifecycleCoordinator(const LifecycleCoordinator&) = delete;
  LifecycleCoordinator& operator=(const LifecycleCoordinator&) = delete;
  ~LifecycleCoordinator() override;

  // LifecycleEventSink:
  void OnNormalizedEvent(const NormalizedEvent& event) override;

  void SetReconciliationRequestCallback(ReconciliationRequestCallback callback);
  // Trigger the real reconciliation path (the request callback set above). Used
  // by the shell recovery control; safe no-op if no callback is set.
  void RequestReconciliation();
  void SetConfirmationCallback(
      base::RepeatingCallback<void(const NormalizedEvent&)> callback);
  void SetPinHandlingSuppressor(
      base::RepeatingCallback<bool(LiveTabKey tab)> suppressor);

  // Classifies one exact, imminent Chromium tab insertion before observers run.
  // Preview promotion uses this to create a retained membership atomically;
  // unknown or mismatched insertions keep the normal temporary default.
  bool ExpectTabInsertion(LiveWindowKey window,
                          LiveTabKey tab,
                          TabRole role);
  void CancelExpectedTabInsertion(LiveTabKey tab);
  // Marks one imminent close as a recoverable Seoul archive instead of a
  // normal close. The normalized removal event performs the model mutation,
  // so Chromium and organization state commit on the same observed event.
  bool ExpectTabArchival(LiveWindowKey window,
                         LiveTabKey tab,
                         std::string recovery_url,
                         std::string title);
  void CancelExpectedTabArchival(LiveTabKey tab);

  static constexpr TabRole kNewTabRole = TabRole::kTemporary;
  static constexpr size_t kMaxPendingTransfers = 256;
  static constexpr size_t kMaxQueuedEvents = 128;
  static constexpr size_t kMaxExpectedInsertions = 64;
  static constexpr size_t kMaxExpectedArchivals = 64;

  MutationOrigin current_origin() const { return current_origin_; }
  bool is_reconciling() const { return reconciling_; }
  bool is_shutting_down() const { return shutting_down_; }
  bool queue_overflow() const { return queue_overflow_; }
  bool reconciliation_required() const { return reconciliation_required_; }
  bool lifecycle_degraded() const {
    return queue_overflow_ || reconciliation_required_;
  }
  size_t pending_transfer_count() const { return pending_transfers_.size(); }
  size_t queued_event_count() const { return pending_events_.size(); }
  size_t known_window_count() const { return known_windows_.size(); }
  size_t expected_insertion_count() const {
    return expected_insertions_.size();
  }
  size_t expected_archival_count() const {
    return expected_archivals_.size();
  }

 private:
  struct PendingTransfer {
    WorkspaceId workspace;
    int sequence = 0;
  };
  struct ExpectedInsertion {
    LiveWindowKey window;
    TabRole role = TabRole::kTemporary;
  };
  struct ExpectedArchival {
    LiveWindowKey window;
    std::string recovery_url;
    std::string title;
  };

  void ProcessEvent(const NormalizedEvent& event);
  void DrainPendingEvents();
  static bool EventsAreDuplicate(const NormalizedEvent& a,
                                 const NormalizedEvent& b);

  void HandleWindowDiscovered(const NormalizedEvent& event);
  void HandleWindowGone(const NormalizedEvent& event);
  void HandleTabInserted(const NormalizedEvent& event);
  void HandleTabRemoved(const NormalizedEvent& event);
  void HandleTabMoved(const NormalizedEvent& event);
  void HandleActiveTabChanged(const NormalizedEvent& event);
  void HandlePinnedStateChanged(const NormalizedEvent& event);
  void HandleSplitAdded(const NormalizedEvent& event);
  void HandleSplitRemoved(const NormalizedEvent& event);
  void HandleSplitContentsChanged(const NormalizedEvent& event);
  void HandleSplitVisualsChanged(const NormalizedEvent& event);
  void HandleReconciliationCompleted(const NormalizedEvent& event);

  WorkspaceId ActiveOrDefaultWorkspace(const LiveWindowKey& window);
  SplitGroupId FindSplitByToken(const std::string& token) const;
  void EvictOldestTransferIfNeeded();
  void ExpireTransfersForWindow(const LiveWindowKey& window);
  void ExpireExpectedInsertionsForWindow(const LiveWindowKey& window);
  void ExpireExpectedArchivalsForWindow(const LiveWindowKey& window);
  void HandleQueueOverflow();
  bool ShouldAcceptEvent(const NormalizedEvent& event) const;

  raw_ptr<OrganizationModel> model_;

  MutationOrigin current_origin_ = MutationOrigin::kChromiumObservation;
  bool applying_ = false;
  bool draining_ = false;
  bool reconciling_ = false;
  bool shutting_down_ = false;
  bool queue_overflow_ = false;
  bool reconciliation_required_ = false;
  int sequence_ = 0;

  ReconciliationRequestCallback reconciliation_request_callback_;
  base::RepeatingCallback<void(const NormalizedEvent&)> confirmation_callback_;
  base::RepeatingCallback<bool(LiveTabKey)> pin_handling_suppressor_;

  std::deque<NormalizedEvent> pending_events_;
  std::set<LiveWindowKey> known_windows_;
  std::map<LiveTabKey, PendingTransfer> pending_transfers_;
  std::map<LiveTabKey, ExpectedInsertion> expected_insertions_;
  std::map<LiveTabKey, ExpectedArchival> expected_archivals_;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_LIFECYCLE_LIFECYCLE_COORDINATOR_H_
