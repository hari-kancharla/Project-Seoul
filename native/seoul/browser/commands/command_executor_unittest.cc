// Project Seoul outbound browser command layer.

#include "seoul/browser/commands/command_executor.h"

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "seoul/browser/commands/browser_command.h"
#include "seoul/browser/commands/command_id.h"
#include "seoul/browser/lifecycle/lifecycle_coordinator.h"
#include "seoul/browser/organization/organization_observer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class FakeResolver : public TargetResolver {
 public:
  CommandResult<ResolvedWindowTarget> ResolveWindow(
      Profile* profile,
      LiveWindowKey window) override {
    return ResolvedWindowTarget{window};
  }
  CommandResult<ResolvedTabTarget> ResolveTab(Profile* profile,
                                              LiveWindowKey window,
                                              LiveTabKey tab) override {
    if (!tab_exists) {
      return base::unexpected(CommandError::kTabNotFound);
    }
    return ResolvedTabTarget{window, tab, 0, false, false};
  }
  CommandResult<ResolvedSplitTarget> ResolveSplitPanes(
      Profile* profile,
      LiveWindowKey window,
      LiveTabKey pane_a,
      LiveTabKey pane_b) override {
    return base::unexpected(CommandError::kSplitPreconditionFailure);
  }

  bool tab_exists = false;
};

class FakeAdapter : public ChromiumMutationAdapter {
 public:
  CommandStatusResult OpenNewTab(Profile* profile,
                                 const ResolvedWindowTarget& window,
                                 CommandForegroundDisposition disposition,
                                 LiveTabKey* out_tab) override {
    return CommandOk();
  }
  CommandStatusResult OpenTab(Profile* profile,
                              const ResolvedWindowTarget& window,
                              const GURL& url,
                              CommandForegroundDisposition disposition,
                              LiveTabKey* out_tab) override {
    return CommandOk();
  }
  CommandStatusResult NavigateTab(Profile* profile,
                                  const ResolvedTabTarget& tab,
                                  const GURL& url) override {
    return CommandOk();
  }
  CommandStatusResult OpenTabInSplit(
      Profile* profile,
      const ResolvedWindowTarget& window,
      LiveTabKey existing_tab,
      const GURL& url,
      double ratio,
      LiveTabKey* out_tab,
      std::string* upstream_token) override {
    return CommandOk();
  }
  CommandStatusResult OpenExternal(Profile* profile,
                                   const GURL& url) override {
    return CommandOk();
  }
  CommandStatusResult ActivateTab(Profile* profile,
                                  const ResolvedTabTarget& tab) override {
    return CommandOk();
  }
  CommandStatusResult CloseTab(Profile* profile,
                               const ResolvedTabTarget& tab) override {
    return close_succeeds
               ? CommandOk()
               : CommandErr(CommandError::kDispatchFailure);
  }
  CommandStatusResult SetPinned(Profile* profile,
                                const ResolvedTabTarget& tab,
                                bool pinned) override {
    return CommandOk();
  }
  CommandStatusResult MoveTab(Profile* profile,
                              const ResolvedTabTarget& tab,
                              int destination_index) override {
    return CommandOk();
  }
  CommandStatusResult CreateSplit(Profile* profile,
                                  const ResolvedSplitTarget& split,
                                  double ratio,
                                  std::string* upstream_token) override {
    return CommandOk();
  }
  CommandStatusResult DissolveSplit(
      Profile* profile,
      LiveWindowKey window,
      const std::string& upstream_token) override {
    return CommandOk();
  }

  bool close_succeeds = true;
};

class CommandExecutorTest : public testing::Test {
 protected:
  CommandExecutorTest()
      : model_(base::BindLambdaForTesting([]() { return base::Time(); })),
        coordinator_(&model_),
        executor_(nullptr, &model_, &coordinator_, &resolver_, &adapter_) {
    CHECK(model_.EnsureDefaultWorkspace().has_value());
  }

  OrganizationModel model_;
  LifecycleCoordinator coordinator_;
  FakeResolver resolver_;
  FakeAdapter adapter_;
  CommandExecutor executor_;
};

TEST_F(CommandExecutorTest, RejectsLifecycleQueueOverflow) {
  bool requested = false;
  coordinator_.SetReconciliationRequestCallback(
      base::BindLambdaForTesting([&requested]() { requested = true; }));
  class OverflowObserver : public OrganizationModelObserver {
   public:
    explicit OverflowObserver(LifecycleCoordinator* c) : c_(c) {}
    void OnOrganizationChanged(const OrganizationChange& change) override {
      if (fired_) {
        return;
      }
      fired_ = true;
      for (size_t i = 0; i < LifecycleCoordinator::kMaxQueuedEvents + 1; ++i) {
        NormalizedEvent e;
        e.type = NormalizedEventType::kTabInserted;
        e.window = LiveWindowKey::FromSessionId(1);
        e.tab = LiveTabKey::FromSessionId(static_cast<int>(300 + i));
        c_->OnNormalizedEvent(e);
      }
    }
    bool fired_ = false;
    raw_ptr<LifecycleCoordinator> c_;
  } overflow(&coordinator_);
  model_.AddObserver(&overflow);
  NormalizedEvent window;
  window.type = NormalizedEventType::kWindowDiscovered;
  window.window = LiveWindowKey::FromSessionId(1);
  coordinator_.OnNormalizedEvent(window);
  coordinator_.OnNormalizedEvent(
      NormalizedEvent{NormalizedEventType::kTabInserted});
  model_.RemoveObserver(&overflow);
  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kActivateTab;
  command.window = LiveWindowKey::FromSessionId(1);
  command.tab = LiveTabKey::FromSessionId(10);
  EXPECT_TRUE(coordinator_.reconciliation_required());
  EXPECT_EQ(executor_.Submit(command).error(),
            CommandError::kLifecycleQueueDegraded);
}

TEST_F(CommandExecutorTest, RejectsBrowserCommandDuringReconciliation) {
  NormalizedEvent reconciliation_began;
  reconciliation_began.type = NormalizedEventType::kReconciliationBegan;
  coordinator_.OnNormalizedEvent(reconciliation_began);

  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kActivateTab;
  command.window = LiveWindowKey::FromSessionId(1);
  command.tab = LiveTabKey::FromSessionId(10);
  EXPECT_TRUE(coordinator_.is_reconciling());
  EXPECT_EQ(executor_.Submit(command).error(),
            CommandError::kReconciliationRequired);
}

TEST_F(CommandExecutorTest, ModelOnlyCommandAppliedImmediately) {
  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kCreateWorkspace;
  command.name = "Another";
  auto result = executor_.Submit(command);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), CommandStatus::kApplied);
}

TEST_F(CommandExecutorTest,
       ArchiveCommitsOnlyWhenTheExactCloseIsObserved) {
  const LiveWindowKey window = LiveWindowKey::FromSessionId(1);
  const LiveTabKey tab = LiveTabKey::FromSessionId(10);
  NormalizedEvent discovered;
  discovered.type = NormalizedEventType::kWindowDiscovered;
  discovered.window = window;
  coordinator_.OnNormalizedEvent(discovered);
  NormalizedEvent inserted;
  inserted.type = NormalizedEventType::kTabInserted;
  inserted.window = window;
  inserted.tab = tab;
  coordinator_.OnNormalizedEvent(inserted);
  const TabMembershipId membership =
      model_.FindMembershipIdByTabKey(tab.value());
  ASSERT_TRUE(membership.is_valid());
  resolver_.tab_exists = true;

  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kArchiveTab;
  command.window = window;
  command.tab = tab;
  command.membership_id = membership;
  command.url = GURL("https://example.test/article");
  command.name = "Article";
  const auto submitted = executor_.Submit(command);
  ASSERT_TRUE(submitted.has_value());
  EXPECT_EQ(submitted.value(), CommandStatus::kAwaitingObservation);
  EXPECT_EQ(coordinator_.expected_archival_count(), 1u);
  EXPECT_FALSE(model_.FindArchivedTab(membership));

  NormalizedEvent removed;
  removed.type = NormalizedEventType::kTabRemoved;
  removed.window = window;
  removed.tab = tab;
  removed.removal_kind = TabRemovalKind::kGenuineClose;
  coordinator_.OnNormalizedEvent(removed);
  executor_.OnNormalizedLifecycleEvent(removed);

  EXPECT_FALSE(model_.FindMembershipIdByTabKey(tab.value()).is_valid());
  const ArchivedTabRecord* archived = model_.FindArchivedTab(membership);
  ASSERT_TRUE(archived);
  EXPECT_EQ(archived->saved_root_url, "https://example.test/article");
  EXPECT_EQ(archived->title, "Article");
  EXPECT_EQ(executor_.in_flight_count(), 0u);
}

TEST_F(CommandExecutorTest, FailedArchiveDispatchLeavesTabLive) {
  const LiveWindowKey window = LiveWindowKey::FromSessionId(1);
  const LiveTabKey tab = LiveTabKey::FromSessionId(10);
  NormalizedEvent discovered;
  discovered.type = NormalizedEventType::kWindowDiscovered;
  discovered.window = window;
  coordinator_.OnNormalizedEvent(discovered);
  NormalizedEvent inserted;
  inserted.type = NormalizedEventType::kTabInserted;
  inserted.window = window;
  inserted.tab = tab;
  coordinator_.OnNormalizedEvent(inserted);
  const TabMembershipId membership =
      model_.FindMembershipIdByTabKey(tab.value());
  ASSERT_TRUE(membership.is_valid());
  resolver_.tab_exists = true;
  adapter_.close_succeeds = false;

  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kArchiveTab;
  command.window = window;
  command.tab = tab;
  command.membership_id = membership;
  command.url = GURL("https://example.test/article");
  command.name = "Article";
  EXPECT_EQ(executor_.Submit(command).error(), CommandError::kDispatchFailure);
  EXPECT_EQ(coordinator_.expected_archival_count(), 0u);
  EXPECT_TRUE(model_.FindMembershipIdByTabKey(tab.value()).is_valid());
  EXPECT_FALSE(model_.FindArchivedTab(membership));
}

TEST_F(CommandExecutorTest, RestoreAdoptsTheConfirmedInsertedTab) {
  const LiveWindowKey window = LiveWindowKey::FromSessionId(1);
  const LiveTabKey old_tab = LiveTabKey::FromSessionId(10);
  const LiveTabKey new_tab = LiveTabKey::FromSessionId(11);
  NormalizedEvent discovered;
  discovered.type = NormalizedEventType::kWindowDiscovered;
  discovered.window = window;
  coordinator_.OnNormalizedEvent(discovered);
  NormalizedEvent inserted;
  inserted.type = NormalizedEventType::kTabInserted;
  inserted.window = window;
  inserted.tab = old_tab;
  coordinator_.OnNormalizedEvent(inserted);
  const TabMembershipId original =
      model_.FindMembershipIdByTabKey(old_tab.value());
  ASSERT_TRUE(original.is_valid());
  ASSERT_TRUE(
      model_.ArchiveTab(original, "https://example.test", "Example")
          .has_value());

  BrowserCommand command;
  command.id = CommandId::Next();
  command.kind = CommandKind::kRestoreArchivedTab;
  command.window = window;
  command.membership_id = original;
  command.url = GURL("https://example.test");
  const auto submitted = executor_.Submit(command);
  ASSERT_TRUE(submitted.has_value());
  EXPECT_EQ(submitted.value(), CommandStatus::kAwaitingObservation);

  NormalizedEvent restored_insert;
  restored_insert.type = NormalizedEventType::kTabInserted;
  restored_insert.window = window;
  restored_insert.tab = new_tab;
  coordinator_.OnNormalizedEvent(restored_insert);
  executor_.OnNormalizedLifecycleEvent(restored_insert);

  EXPECT_FALSE(model_.FindArchivedTab(original));
  const TabMembershipId live =
      model_.FindMembershipIdByTabKey(new_tab.value());
  ASSERT_TRUE(live.is_valid());
  EXPECT_EQ(model_.FindMembership(live)->role, TabRole::kTemporary);
  EXPECT_EQ(executor_.in_flight_count(), 0u);
}

}  // namespace
}  // namespace seoul
