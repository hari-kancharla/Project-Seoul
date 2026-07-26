// Project Seoul native lifecycle bridge.
// Authored for later compilation on a capable host. Not run on the dev machine.

#include "seoul/browser/lifecycle/lifecycle_coordinator.h"

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "seoul/browser/lifecycle/lifecycle_events.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/organization_observer.h"
#include "seoul/browser/organization/organization_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

NormalizedEvent Event(NormalizedEventType type) {
  NormalizedEvent e;
  e.type = type;
  return e;
}

NormalizedEvent WindowDiscovered(int window) {
  NormalizedEvent e = Event(NormalizedEventType::kWindowDiscovered);
  e.window = LiveWindowKey::FromSessionId(window);
  return e;
}

NormalizedEvent WindowDestroyed(int window) {
  NormalizedEvent e = Event(NormalizedEventType::kWindowDestroyed);
  e.window = LiveWindowKey::FromSessionId(window);
  return e;
}

NormalizedEvent TabInserted(int window, int tab, int order = -1) {
  NormalizedEvent e = Event(NormalizedEventType::kTabInserted);
  e.window = LiveWindowKey::FromSessionId(window);
  e.tab = LiveTabKey::FromSessionId(tab);
  e.order_index = order;
  return e;
}

NormalizedEvent TabRemoved(int window, int tab, TabRemovalKind kind) {
  NormalizedEvent e = Event(NormalizedEventType::kTabRemoved);
  e.window = LiveWindowKey::FromSessionId(window);
  e.tab = LiveTabKey::FromSessionId(tab);
  e.removal_kind = kind;
  return e;
}

NormalizedEvent TabMoved(int window, int tab, int order) {
  NormalizedEvent e = Event(NormalizedEventType::kTabMoved);
  e.window = LiveWindowKey::FromSessionId(window);
  e.tab = LiveTabKey::FromSessionId(tab);
  e.order_index = order;
  return e;
}

NormalizedEvent ActiveTab(int window, int tab) {
  NormalizedEvent e = Event(NormalizedEventType::kActiveTabChanged);
  e.window = LiveWindowKey::FromSessionId(window);
  e.tab = LiveTabKey::FromSessionId(tab);
  return e;
}

class LifecycleCoordinatorTest : public testing::Test {
protected:
  LifecycleCoordinatorTest()
      : model_(base::BindLambdaForTesting([this]() { return now_; })),
        coordinator_(&model_) {}

  // Helpers reading the model.
  size_t MembershipCount() const {
    return model_.ToSnapshot().memberships.size();
  }
  size_t ArchiveCount() const {
    return model_.ToSnapshot().archived_tabs.size();
  }
  const TabMembershipRecord *MembershipForTab(int tab) {
    const TabMembershipId id =
        model_.FindMembershipIdByTabKey(LiveTabKey::FromSessionId(tab).value());
    return id.is_valid() ? model_.FindMembership(id) : nullptr;
  }
  std::string ActiveWorkspaceKey(int window) {
    return model_
        .ActiveWorkspaceForWindow(LiveWindowKey::FromSessionId(window).value())
        .value();
  }

  base::Time now_ = base::Time::UnixEpoch() + base::Seconds(1000);
  OrganizationModel model_;
  LifecycleCoordinator coordinator_;
};

TEST_F(LifecycleCoordinatorTest, RepeatedWindowDiscoveryIsIdempotent) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  EXPECT_EQ(1u, coordinator_.known_window_count());
  EXPECT_TRUE(model_.default_workspace().is_valid());
  EXPECT_EQ(model_.default_workspace().value(), ActiveWorkspaceKey(1));
}

TEST_F(LifecycleCoordinatorTest, InvalidWindowIgnored) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(0)); // invalid session id
  EXPECT_EQ(0u, coordinator_.known_window_count());
}

TEST_F(LifecycleCoordinatorTest, NewTabAssignedToActiveWorkspace) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  const TabMembershipRecord *m = MembershipForTab(10);
  ASSERT_TRUE(m);
  EXPECT_EQ(model_.default_workspace().value(), m->workspace_id.value());
  EXPECT_EQ(TabRole::kTemporary, m->role);
  EXPECT_EQ(1u, MembershipCount());
}

TEST_F(LifecycleCoordinatorTest,
       ExpectedInsertionUsesExactWindowTabAndRetainedRole) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  EXPECT_TRUE(coordinator_.ExpectTabInsertion(LiveWindowKey::FromSessionId(1),
                                              LiveTabKey::FromSessionId(10),
                                              TabRole::kRetained));
  EXPECT_EQ(coordinator_.expected_insertion_count(), 1u);
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  ASSERT_NE(MembershipForTab(10), nullptr);
  EXPECT_EQ(MembershipForTab(10)->role, TabRole::kRetained);
  EXPECT_EQ(coordinator_.expected_insertion_count(), 0u);
}

TEST_F(LifecycleCoordinatorTest,
       MismatchedWindowConsumesExpectationAndUsesSafeDefault) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(WindowDiscovered(2));
  ASSERT_TRUE(coordinator_.ExpectTabInsertion(LiveWindowKey::FromSessionId(1),
                                              LiveTabKey::FromSessionId(10),
                                              TabRole::kRetained));
  coordinator_.OnNormalizedEvent(TabInserted(2, 10));
  ASSERT_NE(MembershipForTab(10), nullptr);
  EXPECT_EQ(MembershipForTab(10)->role, TabRole::kTemporary);
  EXPECT_EQ(coordinator_.expected_insertion_count(), 0u);
}

TEST_F(LifecycleCoordinatorTest, ExpectedInsertionIsBoundedAndCancellable) {
  const LiveWindowKey window = LiveWindowKey::FromSessionId(1);
  EXPECT_FALSE(coordinator_.ExpectTabInsertion(
      window, LiveTabKey::FromSessionId(1), TabRole::kPinned));
  for (size_t i = 0; i < LifecycleCoordinator::kMaxExpectedInsertions; ++i) {
    EXPECT_TRUE(coordinator_.ExpectTabInsertion(
        window, LiveTabKey::FromSessionId(static_cast<int>(i) + 10),
        TabRole::kRetained));
  }
  EXPECT_FALSE(coordinator_.ExpectTabInsertion(
      window, LiveTabKey::FromSessionId(1000), TabRole::kRetained));
  coordinator_.CancelExpectedTabInsertion(LiveTabKey::FromSessionId(10));
  EXPECT_EQ(coordinator_.expected_insertion_count(),
            LifecycleCoordinator::kMaxExpectedInsertions - 1);
  coordinator_.OnNormalizedEvent(WindowDestroyed(1));
  EXPECT_EQ(coordinator_.expected_insertion_count(), 0u);
}

TEST_F(LifecycleCoordinatorTest, DuplicateInsertionCreatesNoSecondMembership) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  EXPECT_EQ(1u, MembershipCount());
}

TEST_F(LifecycleCoordinatorTest,
       RestoredTabRebindsExistingDurableMembershipToNewSessionKey) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10, 4));
  const TabMembershipId durable =
      model_.FindMembershipIdByTabKey(LiveTabKey::FromSessionId(10).value());
  ASSERT_TRUE(durable.is_valid());
  ASSERT_TRUE(model_.RetainTab(durable).has_value());
  const base::Time created_at = model_.FindMembership(durable)->created_at;

  NormalizedEvent restored = TabInserted(1, 20, 2);
  restored.insert_kind = TabInsertKind::kExisting;
  restored.restored_membership = durable;
  coordinator_.OnNormalizedEvent(restored);

  EXPECT_EQ(MembershipCount(), 1u);
  EXPECT_FALSE(
      model_.FindMembershipIdByTabKey(LiveTabKey::FromSessionId(10).value())
          .is_valid());
  EXPECT_EQ(
      model_.FindMembershipIdByTabKey(LiveTabKey::FromSessionId(20).value()),
      durable);
  const TabMembershipRecord *record = model_.FindMembership(durable);
  ASSERT_TRUE(record);
  EXPECT_EQ(record->role, TabRole::kRetained);
  EXPECT_EQ(record->created_at, created_at);
  EXPECT_EQ(record->order, 2);
}

TEST_F(LifecycleCoordinatorTest,
       StaleRestoredMembershipFallsBackToSafeTemporaryMembership) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  NormalizedEvent restored = TabInserted(1, 20, 0);
  restored.insert_kind = TabInsertKind::kExisting;
  restored.restored_membership = TabMembershipId::GenerateNew();
  coordinator_.OnNormalizedEvent(restored);

  ASSERT_EQ(MembershipCount(), 1u);
  const TabMembershipRecord *record = MembershipForTab(20);
  ASSERT_TRUE(record);
  EXPECT_EQ(record->role, TabRole::kTemporary);
}

TEST_F(LifecycleCoordinatorTest,
       InsertionInUndiscoveredWindowFallsBackToDefault) {
  coordinator_.OnNormalizedEvent(
      WindowDiscovered(1)); // ensures a default exists
  coordinator_.OnNormalizedEvent(TabInserted(2, 10)); // window 2 not discovered
  const TabMembershipRecord *m = MembershipForTab(10);
  ASSERT_TRUE(m);
  EXPECT_EQ(model_.default_workspace().value(), m->workspace_id.value());
}

TEST_F(LifecycleCoordinatorTest, ActivationUpdatesLastActive) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  now_ += base::Seconds(60);
  coordinator_.OnNormalizedEvent(ActiveTab(1, 10));
  const TabMembershipRecord *m = MembershipForTab(10);
  ASSERT_TRUE(m);
  EXPECT_EQ(now_, m->last_active_at);
}

TEST_F(LifecycleCoordinatorTest, ActivationSwitchesWindowWorkspace) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  const WorkspaceId other = model_.CreateWorkspace("Other").value();
  // A tab that lives in `other`, activated in window 1, switches the window.
  ASSERT_TRUE(model_
                  .AddTabMembership(other,
                                    LiveTabKey::FromSessionId(20).value(),
                                    TabRole::kRetained)
                  .has_value());
  coordinator_.OnNormalizedEvent(ActiveTab(1, 20));
  EXPECT_EQ(other.value(), ActiveWorkspaceKey(1));
}

TEST_F(LifecycleCoordinatorTest, ActivationOfUntrackedTabCreatesNoMembership) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(ActiveTab(1, 99)); // never inserted
  EXPECT_EQ(0u, MembershipCount());
}

TEST_F(LifecycleCoordinatorTest,
       GenuineCloseRemovesMembershipWithoutArchiving) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  coordinator_.OnNormalizedEvent(
      TabRemoved(1, 10, TabRemovalKind::kGenuineClose));
  EXPECT_EQ(0u, MembershipCount());
  EXPECT_EQ(0u,
            ArchiveCount()); // Chromium owns recently-closed; Seoul doesn't.
}

TEST_F(LifecycleCoordinatorTest,
       ExpectedArchiveCommitsRecoveryMetadataOnExactClose) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  const TabMembershipId original =
      model_.FindMembershipIdByTabKey(LiveTabKey::FromSessionId(10).value());
  ASSERT_TRUE(original.is_valid());
  ASSERT_TRUE(coordinator_.ExpectTabArchival(
      LiveWindowKey::FromSessionId(1), LiveTabKey::FromSessionId(10),
      "https://example.test/article", "Article"));
  EXPECT_EQ(coordinator_.expected_archival_count(), 1u);

  coordinator_.OnNormalizedEvent(
      TabRemoved(1, 10, TabRemovalKind::kGenuineClose));
  EXPECT_EQ(MembershipCount(), 0u);
  EXPECT_EQ(ArchiveCount(), 1u);
  EXPECT_EQ(coordinator_.expected_archival_count(), 0u);
  const ArchivedTabRecord *archived = model_.FindArchivedTab(original);
  ASSERT_TRUE(archived);
  EXPECT_EQ(archived->saved_root_url, "https://example.test/article");
  EXPECT_EQ(archived->title, "Article");
}

TEST_F(LifecycleCoordinatorTest,
       ArchiveExpectationRejectsProtectedRoleAndCancelsOnCloseAbort) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  const TabMembershipId membership =
      model_.FindMembershipIdByTabKey(LiveTabKey::FromSessionId(10).value());
  ASSERT_TRUE(model_.RetainTab(membership).has_value());
  EXPECT_FALSE(coordinator_.ExpectTabArchival(
      LiveWindowKey::FromSessionId(1), LiveTabKey::FromSessionId(10),
      "https://example.test", "Example"));

  ASSERT_TRUE(model_.MarkTabTemporary(membership).has_value());
  ASSERT_TRUE(coordinator_.ExpectTabArchival(
      LiveWindowKey::FromSessionId(1), LiveTabKey::FromSessionId(10),
      "https://example.test", "Example"));
  NormalizedEvent cancel = Event(NormalizedEventType::kTabCloseCancelled);
  cancel.window = LiveWindowKey::FromSessionId(1);
  cancel.tab = LiveTabKey::FromSessionId(10);
  coordinator_.OnNormalizedEvent(cancel);
  EXPECT_EQ(coordinator_.expected_archival_count(), 0u);
  EXPECT_EQ(MembershipCount(), 1u);
  EXPECT_EQ(ArchiveCount(), 0u);
}

TEST_F(LifecycleCoordinatorTest, CloseCancellationPreservesMembership) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  NormalizedEvent cancel = Event(NormalizedEventType::kTabCloseCancelled);
  cancel.window = LiveWindowKey::FromSessionId(1);
  cancel.tab = LiveTabKey::FromSessionId(10);
  coordinator_.OnNormalizedEvent(cancel);
  EXPECT_EQ(1u, MembershipCount());
}

TEST_F(LifecycleCoordinatorTest, DetachPreservesMembershipAndRecordsTransfer) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  coordinator_.OnNormalizedEvent(
      TabRemoved(1, 10, TabRemovalKind::kTransferOut));
  EXPECT_EQ(1u, MembershipCount()); // preserved
  EXPECT_EQ(1u, coordinator_.pending_transfer_count());
}

TEST_F(LifecycleCoordinatorTest, TransferAcrossTwoWindowsKeepsOneMembership) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(WindowDiscovered(2));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  const std::string ws = MembershipForTab(10)->workspace_id.value();
  coordinator_.OnNormalizedEvent(
      TabRemoved(1, 10, TabRemovalKind::kTransferOut));
  coordinator_.OnNormalizedEvent(TabInserted(2, 10)); // lands in window 2
  EXPECT_EQ(0u, coordinator_.pending_transfer_count());
  EXPECT_EQ(1u, MembershipCount());
  EXPECT_EQ(ws, MembershipForTab(10)->workspace_id.value()); // unchanged
}

TEST_F(LifecycleCoordinatorTest,
       TransferThatNeverArrivesStaysPendingAndBounded) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  coordinator_.OnNormalizedEvent(
      TabRemoved(1, 10, TabRemovalKind::kTransferOut));
  EXPECT_EQ(1u, coordinator_.pending_transfer_count());
  EXPECT_EQ(1u, MembershipCount()); // no ghost removal
}

TEST_F(LifecycleCoordinatorTest, PendingTransfersAreCappedOldestEvicted) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  const size_t over = LifecycleCoordinator::kMaxPendingTransfers + 1;
  for (size_t i = 0; i < over; ++i) {
    const int tab = static_cast<int>(i) + 1;
    coordinator_.OnNormalizedEvent(TabInserted(1, tab));
    coordinator_.OnNormalizedEvent(
        TabRemoved(1, tab, TabRemovalKind::kTransferOut));
  }
  EXPECT_EQ(LifecycleCoordinator::kMaxPendingTransfers,
            coordinator_.pending_transfer_count());
}

TEST_F(LifecycleCoordinatorTest, MoveWithinWindowUpdatesOrder) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10, /*order=*/0));
  coordinator_.OnNormalizedEvent(TabMoved(1, 10, /*order=*/3));
  EXPECT_EQ(3, MembershipForTab(10)->order);
}

TEST_F(LifecycleCoordinatorTest, ReplacementPreservesLogicalMembership) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  const TabMembershipId before =
      model_.FindMembershipIdByTabKey(LiveTabKey::FromSessionId(10).value());
  NormalizedEvent replace = Event(NormalizedEventType::kTabReplaced);
  replace.window = LiveWindowKey::FromSessionId(1);
  replace.tab = LiveTabKey::FromSessionId(10);
  coordinator_.OnNormalizedEvent(replace);
  const TabMembershipId after =
      model_.FindMembershipIdByTabKey(LiveTabKey::FromSessionId(10).value());
  EXPECT_EQ(1u, MembershipCount());
  EXPECT_TRUE(before == after);
}

TEST_F(LifecycleCoordinatorTest, TabStripDestructionForgetsWindowKeepsTabs) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  NormalizedEvent destroyed = Event(NormalizedEventType::kTabStripDestroyed);
  destroyed.window = LiveWindowKey::FromSessionId(1);
  coordinator_.OnNormalizedEvent(destroyed);
  EXPECT_EQ(0u, coordinator_.known_window_count());
  EXPECT_EQ(1u, MembershipCount()); // tabs preserved for restoration
}

TEST_F(LifecycleCoordinatorTest, WindowDestroyedPreservesMemberships) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  coordinator_.OnNormalizedEvent(TabInserted(1, 11));
  coordinator_.OnNormalizedEvent(WindowDestroyed(1));
  EXPECT_EQ(0u, coordinator_.known_window_count());
  EXPECT_EQ(2u, MembershipCount());
}

TEST_F(LifecycleCoordinatorTest, ShutdownIgnoresLaterEvents) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(Event(NormalizedEventType::kShutdownBegan));
  EXPECT_TRUE(coordinator_.is_shutting_down());
  coordinator_.OnNormalizedEvent(TabInserted(1, 10)); // ignored after shutdown
  EXPECT_EQ(0u, MembershipCount());
}

TEST_F(LifecycleCoordinatorTest, WindowShutdownRemovalPreservesMembership) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  coordinator_.OnNormalizedEvent(
      TabRemoved(1, 10, TabRemovalKind::kWindowShutdown));
  EXPECT_EQ(1u, MembershipCount());
}

TEST_F(LifecycleCoordinatorTest, ReconciliationClearsPendingTransfers) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  coordinator_.OnNormalizedEvent(
      TabRemoved(1, 10, TabRemovalKind::kTransferOut));
  EXPECT_EQ(1u, coordinator_.pending_transfer_count());
  coordinator_.OnNormalizedEvent(
      Event(NormalizedEventType::kReconciliationBegan));
  coordinator_.OnNormalizedEvent(
      Event(NormalizedEventType::kReconciliationCompleted));
  EXPECT_EQ(0u, coordinator_.pending_transfer_count());
}

class ReentrantObserver : public OrganizationModelObserver {
public:
  explicit ReentrantObserver(LifecycleCoordinator *coordinator)
      : coordinator_(coordinator) {}

  void OnOrganizationChanged(const OrganizationChange &change) override {
    if (change.type == OrganizationChangeType::kMembershipAdded &&
        !reentered_) {
      reentered_ = true;
      coordinator_->OnNormalizedEvent(TabInserted(1, 99));
    }
  }

private:
  raw_ptr<LifecycleCoordinator> coordinator_;
  bool reentered_ = false;
};

TEST_F(LifecycleCoordinatorTest, ReentrantEventIsQueuedAndApplied) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  ReentrantObserver obs(&coordinator_);
  model_.AddObserver(&obs);
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  model_.RemoveObserver(&obs);
  EXPECT_EQ(2u, MembershipCount());
}

TEST_F(LifecycleCoordinatorTest, QueueOverflowSurfacesFailure) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  class OverflowObserver : public OrganizationModelObserver {
  public:
    explicit OverflowObserver(LifecycleCoordinator *c) : c_(c) {}
    void OnOrganizationChanged(const OrganizationChange &change) override {
      if (fired_) {
        return;
      }
      fired_ = true;
      for (size_t i = 0; i < LifecycleCoordinator::kMaxQueuedEvents + 1; ++i) {
        NormalizedEvent e = TabInserted(1, static_cast<int>(100 + i));
        c_->OnNormalizedEvent(e);
      }
    }
    bool fired_ = false;
    raw_ptr<LifecycleCoordinator> c_;
  } overflow(&coordinator_);
  model_.AddObserver(&overflow);
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  model_.RemoveObserver(&overflow);
  EXPECT_TRUE(coordinator_.queue_overflow());
}

TEST_F(LifecycleCoordinatorTest, QueueOverflowRequiresReconciliation) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  class OverflowObserver : public OrganizationModelObserver {
  public:
    explicit OverflowObserver(LifecycleCoordinator *c) : c_(c) {}
    void OnOrganizationChanged(const OrganizationChange &change) override {
      if (fired_) {
        return;
      }
      fired_ = true;
      for (size_t i = 0; i < LifecycleCoordinator::kMaxQueuedEvents + 1; ++i) {
        c_->OnNormalizedEvent(TabInserted(1, static_cast<int>(200 + i)));
      }
    }
    bool fired_ = false;
    raw_ptr<LifecycleCoordinator> c_;
  } overflow(&coordinator_);
  model_.AddObserver(&overflow);
  coordinator_.OnNormalizedEvent(TabInserted(1, 10));
  model_.RemoveObserver(&overflow);
  EXPECT_TRUE(coordinator_.reconciliation_required());
}

TEST_F(LifecycleCoordinatorTest, ReconciliationClearsDegradedState) {
  coordinator_.OnNormalizedEvent(
      Event(NormalizedEventType::kReconciliationBegan));
  coordinator_.OnNormalizedEvent(
      Event(NormalizedEventType::kReconciliationCompleted));
  EXPECT_FALSE(coordinator_.reconciliation_required());
  EXPECT_FALSE(coordinator_.queue_overflow());
}

TEST_F(LifecycleCoordinatorTest, RescanSimulationDetectsNewTab) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  NormalizedEvent existing = TabInserted(1, 10, 0);
  existing.insert_kind = TabInsertKind::kExisting;
  coordinator_.OnNormalizedEvent(existing);
  NormalizedEvent discovered = TabInserted(1, 11, 1);
  discovered.insert_kind = TabInsertKind::kExisting;
  coordinator_.OnNormalizedEvent(discovered);
  EXPECT_EQ(2u, MembershipCount());
}

TEST_F(LifecycleCoordinatorTest, ExistingTabInsertKindCreatesMembershipOnce) {
  coordinator_.OnNormalizedEvent(WindowDiscovered(1));
  NormalizedEvent existing = TabInserted(1, 10, 0);
  existing.insert_kind = TabInsertKind::kExisting;
  coordinator_.OnNormalizedEvent(existing);
  coordinator_.OnNormalizedEvent(existing);
  EXPECT_EQ(1u, MembershipCount());
}

} // namespace
} // namespace seoul
