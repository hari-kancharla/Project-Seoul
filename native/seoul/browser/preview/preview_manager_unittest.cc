// Project Seoul Preview lifecycle tests.

#include "seoul/browser/preview/preview_manager.h"

#include "base/test/bind.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class PreviewManagerTest : public testing::Test {
protected:
  PreviewManagerTest()
      : manager_(base::BindLambdaForTesting([this] { return now_; }),
                 base::BindRepeating(&PreviewId::GenerateNew)) {}

  LiveWindowKey Window(int id = 1) { return LiveWindowKey::FromSessionId(id); }
  LiveTabKey Tab(int id = 10) { return LiveTabKey::FromSessionId(id); }

  base::Time now_ = base::Time::FromSecondsSinceUnixEpoch(1000);
  PreviewManager manager_;
};

TEST(PreviewErrorTest, NamesRoutingAndTransferFailuresPrecisely) {
  EXPECT_STREQ(PreviewErrorToString(PreviewError::kRoutingRejected),
               "routing_rejected");
  EXPECT_STREQ(PreviewErrorToString(PreviewError::kPromotionFailed),
               "promotion_failed");
}

TEST_F(PreviewManagerTest, RejectsUnsafeOrUnboundOpen) {
  EXPECT_EQ(manager_.Open(LiveWindowKey(), Tab(), GURL("https://example.com"))
                .error(),
            PreviewError::kInvalidParent);
  EXPECT_EQ(manager_.Open(Window(), Tab(), GURL("file:///tmp/private")).error(),
            PreviewError::kUnsafeUrl);
  EXPECT_EQ(manager_
                .Open(Window(), Tab(),
                      GURL("https://user:secret@example.com/private"))
                .error(),
            PreviewError::kUnsafeUrl);
}

TEST_F(PreviewManagerTest, ReplacesOnlyTheSameWindowsPreview) {
  auto first = manager_.Open(Window(), Tab(), GURL("https://example.com/one"));
  ASSERT_TRUE(first.has_value());
  auto other =
      manager_.Open(Window(2), Tab(20), GURL("https://example.org/other"));
  ASSERT_TRUE(other.has_value());
  auto replacement =
      manager_.Open(Window(), Tab(), GURL("https://example.com/two"));
  ASSERT_TRUE(replacement.has_value());
  ASSERT_TRUE(replacement->replaced.has_value());
  EXPECT_EQ(*replacement->replaced, first->id);
  EXPECT_EQ(manager_.size(), 2u);
  EXPECT_EQ(manager_.Find(first->id), nullptr);
  ASSERT_NE(manager_.FindForWindow(Window()), nullptr);
  EXPECT_EQ(manager_.FindForWindow(Window())->current_url,
            GURL("https://example.com/two"));
}

TEST_F(PreviewManagerTest, TracksBoundedNavigationAndReadiness) {
  auto opened =
      manager_.Open(Window(), Tab(), GURL("https://example.com/start"));
  ASSERT_TRUE(opened.has_value());
  EXPECT_TRUE(manager_.MarkReady(opened->id).has_value());
  EXPECT_TRUE(manager_.MarkLoading(opened->id).has_value());
  EXPECT_EQ(manager_.Find(opened->id)->state, PreviewState::kLoading);
  EXPECT_TRUE(manager_.DidNavigate(opened->id, GURL("https://example.com/next"))
                  .has_value());
  EXPECT_EQ(manager_.Find(opened->id)->state, PreviewState::kLoading);
  EXPECT_TRUE(manager_.MarkReady(opened->id).has_value());
  EXPECT_EQ(manager_.Find(opened->id)->navigation_count, 2);
  EXPECT_EQ(manager_.DidNavigate(opened->id, GURL("chrome://settings")).error(),
            PreviewError::kUnsafeUrl);
  EXPECT_EQ(manager_.Find(opened->id)->current_url,
            GURL("https://example.com/next"));
}

TEST_F(PreviewManagerTest, FailedNavigationCanRetryButCannotPromoteMidLoad) {
  auto opened = manager_.Open(Window(), Tab(), GURL("https://example.com"));
  ASSERT_TRUE(opened.has_value());
  ASSERT_TRUE(manager_.MarkFailed(opened->id).has_value());
  ASSERT_TRUE(manager_.MarkLoading(opened->id).has_value());
  EXPECT_EQ(
      manager_.BeginPromotion(opened->id, PreviewPromotionTarget::kTab).error(),
      PreviewError::kInvalidState);
  EXPECT_TRUE(
      manager_.DidNavigate(opened->id, GURL("https://example.com/recovered"))
          .has_value());
  EXPECT_TRUE(manager_.MarkReady(opened->id).has_value());
  EXPECT_EQ(manager_.Find(opened->id)->state, PreviewState::kReady);
}

TEST_F(PreviewManagerTest, PromotionIsExplicitAndTwoPhase) {
  auto opened = manager_.Open(Window(), Tab(), GURL("https://example.com"));
  ASSERT_TRUE(opened.has_value());
  ASSERT_TRUE(manager_.MarkReady(opened->id).has_value());

  EXPECT_TRUE(manager_.BeginPromotion(opened->id, PreviewPromotionTarget::kTab)
                  .has_value());
  ASSERT_NE(manager_.Find(opened->id), nullptr);
  EXPECT_EQ(manager_.Find(opened->id)->state, PreviewState::kPromoting);
  EXPECT_EQ(manager_.Dismiss(opened->id, PreviewDismissReason::kUserDismissed)
                .error(),
            PreviewError::kInvalidState);

  EXPECT_TRUE(manager_.AbortPromotion(opened->id).has_value());
  EXPECT_EQ(manager_.Find(opened->id)->state, PreviewState::kReady);
  EXPECT_TRUE(
      manager_.BeginPromotion(opened->id, PreviewPromotionTarget::kSplit)
          .has_value());
  auto committed = manager_.CommitPromotion(opened->id);
  ASSERT_TRUE(committed.has_value());
  EXPECT_EQ(committed->promotion_target, PreviewPromotionTarget::kSplit);
  EXPECT_EQ(manager_.Find(opened->id), nullptr);
}

TEST_F(PreviewManagerTest, ParentAndWindowRemovalDismissEphemeralState) {
  auto first = manager_.Open(Window(), Tab(), GURL("https://example.com"));
  auto second = manager_.Open(Window(2), Tab(20), GURL("https://example.org"));
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(manager_.DismissForParent(Tab()), 1u);
  EXPECT_EQ(manager_.Find(first->id), nullptr);
  EXPECT_EQ(manager_.DismissForWindow(Window(2)), 1u);
  EXPECT_EQ(manager_.size(), 0u);
}

TEST_F(PreviewManagerTest, FailedPreviewCannotPromote) {
  auto opened = manager_.Open(Window(), Tab(), GURL("https://example.com"));
  ASSERT_TRUE(opened.has_value());
  ASSERT_TRUE(manager_.MarkFailed(opened->id).has_value());
  EXPECT_EQ(
      manager_.BeginPromotion(opened->id, PreviewPromotionTarget::kTab).error(),
      PreviewError::kInvalidState);
  EXPECT_TRUE(
      manager_.Dismiss(opened->id, PreviewDismissReason::kCrashed).has_value());
}

TEST_F(PreviewManagerTest, FailedAdmissionDoesNotReplaceExistingPreview) {
  auto opened = manager_.Open(Window(), Tab(), GURL("https://example.com"));
  ASSERT_TRUE(opened.has_value());

  PreviewManager invalid_manager(
      base::BindLambdaForTesting([this] { return now_; }),
      base::BindRepeating([] { return PreviewId(); }));
  EXPECT_EQ(invalid_manager.Open(Window(), Tab(), GURL("https://example.org"))
                .error(),
            PreviewError::kInvalidId);
  EXPECT_EQ(invalid_manager.size(), 0u);

  // A colliding generator also leaves its already-admitted record intact.
  const PreviewId collision = opened->id;
  PreviewManager colliding_manager(
      base::BindLambdaForTesting([this] { return now_; }),
      base::BindRepeating([](PreviewId id) { return id; }, collision));
  ASSERT_TRUE(
      colliding_manager.Open(Window(), Tab(), GURL("https://example.com"))
          .has_value());
  EXPECT_EQ(colliding_manager.Open(Window(), Tab(), GURL("https://example.org"))
                .error(),
            PreviewError::kInvalidId);
  ASSERT_NE(colliding_manager.FindForWindow(Window()), nullptr);
  EXPECT_EQ(colliding_manager.FindForWindow(Window())->current_url,
            GURL("https://example.com"));
}

} // namespace
} // namespace seoul
