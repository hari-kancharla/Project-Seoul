// Project Seoul native lifecycle bridge.

#include "seoul/browser/lifecycle/live_window_state.h"

#include "base/scoped_observation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class RecordingLiveObserver : public LiveWindowStateObserver {
 public:
  explicit RecordingLiveObserver(LiveWindowStateProvider* provider) {
    observation_.Observe(provider);
  }
  void OnLiveWindowStateProviderDestroying() override { observation_.Reset(); }
  void OnLiveWindowSnapshotChanged(
      const LiveWindowSnapshot& snapshot) override {
    snapshots_.push_back(snapshot);
  }
  void OnLiveWindowRemoved(LiveWindowKey window) override {
    removed_.push_back(window);
  }
  const std::vector<LiveWindowSnapshot>& snapshots() const {
    return snapshots_;
  }
  const std::vector<LiveWindowKey>& removed() const { return removed_; }

 private:
  base::ScopedObservation<LiveWindowStateProvider, LiveWindowStateObserver>
      observation_{this};
  std::vector<LiveWindowSnapshot> snapshots_;
  std::vector<LiveWindowKey> removed_;
};

TEST(LiveWindowStateProviderTest, DestructionDetachesSurvivingScopedObservers) {
  class ScopedLiveObserver : public LiveWindowStateObserver {
   public:
    void OnLiveWindowStateProviderDestroying() override {
      ++destroying;
      observation.Reset();
    }
    int destroying = 0;
    base::ScopedObservation<LiveWindowStateProvider, LiveWindowStateObserver>
        observation{this};
  } first, second;
  {
    LiveWindowStateProvider provider;
    first.observation.Observe(&provider);
    second.observation.Observe(&provider);
  }
  EXPECT_EQ(1, first.destroying);
  EXPECT_EQ(1, second.destroying);
  EXPECT_FALSE(first.observation.IsObserving());
  EXPECT_FALSE(second.observation.IsObserving());
}

TEST(NewTabPlaceholderPolicyTest, AllowsOnlyInertBlankOrNewTabNavigation) {
  EXPECT_TRUE(IsSafeNewTabPlaceholderCandidate(
      {.has_synthetic_startup_provenance = true,
       .is_initial_blank_navigation = true}));
  EXPECT_TRUE(IsSafeNewTabPlaceholderCandidate(
      {.has_synthetic_startup_provenance = true,
       .entry_count = 1,
       .visible_or_committed_placeholder_page = true}));
  EXPECT_TRUE(IsSafeNewTabPlaceholderCandidate(
      {.has_synthetic_startup_provenance = true,
       .has_pending_entry = true,
       .pending_entry_is_placeholder_page = true}));
}

TEST(NewTabPlaceholderPolicyTest,
     RejectsRestoredNewTabPageWithoutSyntheticProvenance) {
  EXPECT_FALSE(IsSafeNewTabPlaceholderCandidate(
      {.entry_count = 1, .visible_or_committed_placeholder_page = true}));
  EXPECT_FALSE(IsSafeNewTabPlaceholderCandidate(
      {.is_initial_blank_navigation = true,
       .has_pending_entry = true,
       .pending_entry_is_placeholder_page = true}));
}

TEST(NewTabPlaceholderPolicyTest,
     RejectsPendingRealNavigationFromInitialBlank) {
  EXPECT_FALSE(IsSafeNewTabPlaceholderCandidate(
      {.has_synthetic_startup_provenance = true,
       .is_initial_blank_navigation = true,
       .has_pending_entry = true,
       .pending_entry_is_placeholder_page = false}));
}

TEST(NewTabPlaceholderPolicyTest, RejectsPendingRealNavigationFromNewTabPage) {
  EXPECT_FALSE(IsSafeNewTabPlaceholderCandidate(
      {.has_synthetic_startup_provenance = true,
       .entry_count = 1,
       .visible_or_committed_placeholder_page = true,
       .has_pending_entry = true,
       .pending_entry_is_placeholder_page = false}));
}

TEST(NewTabPlaceholderPolicyTest, RejectsOpenerAndBrowsingHistory) {
  EXPECT_FALSE(IsSafeNewTabPlaceholderCandidate(
      {.has_synthetic_startup_provenance = true,
       .has_opener = true,
       .is_initial_blank_navigation = true}));
  EXPECT_FALSE(IsSafeNewTabPlaceholderCandidate(
      {.has_synthetic_startup_provenance = true,
       .entry_count = 2,
       .visible_or_committed_placeholder_page = true}));
}

TEST(LiveWindowStateProviderTest, PublishesInitialSnapshot) {
  LiveWindowStateProvider provider;
  RecordingLiveObserver observer(&provider);
  LiveWindowSnapshot snapshot;
  snapshot.window = LiveWindowKey::FromSessionId(1);
  snapshot.active_tab = LiveTabKey::FromSessionId(10);
  LiveTabDescriptor tab;
  tab.tab = snapshot.active_tab;
  tab.strip_order = 0;
  snapshot.tabs.push_back(tab);
  provider.SetSnapshotForTesting(snapshot.window, snapshot);
  ASSERT_EQ(observer.snapshots().size(), 1u);
  EXPECT_EQ(observer.snapshots()[0].active_tab, snapshot.active_tab);
}

TEST(LiveWindowStateProviderTest, SkipsDuplicateSnapshots) {
  LiveWindowStateProvider provider;
  RecordingLiveObserver observer(&provider);
  LiveWindowSnapshot snapshot;
  snapshot.window = LiveWindowKey::FromSessionId(2);
  provider.SetSnapshotForTesting(snapshot.window, snapshot);
  provider.SetSnapshotForTesting(snapshot.window, snapshot);
  EXPECT_EQ(observer.snapshots().size(), 1u);
}

TEST(LiveWindowStateProviderTest, PlaceholderMarkerPublishesStateChange) {
  LiveWindowStateProvider provider;
  RecordingLiveObserver observer(&provider);

  LiveWindowSnapshot snapshot;
  snapshot.window = LiveWindowKey::FromSessionId(4);
  LiveTabDescriptor tab;
  tab.tab = LiveTabKey::FromSessionId(40);
  snapshot.tabs.push_back(tab);
  provider.SetSnapshotForTesting(snapshot.window, snapshot);

  snapshot.tabs.front().is_new_tab_placeholder = true;
  provider.SetSnapshotForTesting(snapshot.window, snapshot);

  ASSERT_EQ(observer.snapshots().size(), 2u);
  EXPECT_FALSE(observer.snapshots()[0].tabs.front().is_new_tab_placeholder);
  EXPECT_TRUE(observer.snapshots()[1].tabs.front().is_new_tab_placeholder);
}

TEST(LiveWindowStateProviderTest, RemoveWindowNotifiesObserver) {
  LiveWindowStateProvider provider;
  RecordingLiveObserver observer(&provider);
  const LiveWindowKey window = LiveWindowKey::FromSessionId(3);
  provider.RemoveWindow(window);
  ASSERT_EQ(observer.removed().size(), 1u);
  EXPECT_EQ(observer.removed()[0], window);
}

}  // namespace
}  // namespace seoul
