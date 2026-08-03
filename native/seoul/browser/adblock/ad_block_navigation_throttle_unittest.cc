// Project Seoul top-level navigation blocker throttle tests.

#include "seoul/browser/adblock/ad_block_navigation_throttle.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "content/public/test/mock_navigation_handle.h"
#include "content/public/test/mock_navigation_throttle_registry.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::adblock {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

class AdBlockNavigationThrottleTest : public testing::Test {
 protected:
  explicit AdBlockNavigationThrottleTest()
      : handle_(GURL("https://news.example/start"), nullptr),
        registry_(
            &handle_,
            content::MockNavigationThrottleRegistry::RegistrationMode::kHold) {
    ON_CALL(handle_, GetRequestMethod()).WillByDefault(Return("GET"));
  }

  NiceMock<content::MockNavigationHandle> handle_;
  content::MockNavigationThrottleRegistry registry_;
};

TEST_F(AdBlockNavigationThrottleTest, AllowsUnmatchedMainFrameNavigation) {
  int checks = 0;
  AdBlockNavigationThrottle throttle(
      registry_, base::BindRepeating(
                     [](int* checks, AdBlockRequest request,
                        AdBlockNavigationThrottle::DecisionCallback callback) {
                       ++*checks;
                       EXPECT_EQ("main_frame", request.request_type);
                       EXPECT_EQ("https://news.example/start", request.url);
                       std::move(callback).Run(AdBlockDecision());
                     },
                     &checks));

  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            throttle.WillStartRequest().action());
  EXPECT_EQ(1, checks);
}

TEST_F(AdBlockNavigationThrottleTest, BlocksMatchedMainFrameNavigation) {
  AdBlockNavigationThrottle throttle(
      registry_, base::BindRepeating(
                     [](AdBlockRequest,
                        AdBlockNavigationThrottle::DecisionCallback callback) {
                       AdBlockDecision decision;
                       decision.action = AdBlockAction::kBlock;
                       decision.matched = true;
                       std::move(callback).Run(std::move(decision));
                     }));

  const content::NavigationThrottle::ThrottleCheckResult result =
      throttle.WillStartRequest();
  EXPECT_EQ(content::NavigationThrottle::BLOCK_REQUEST, result.action());
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, result.net_error_code());
}

TEST_F(AdBlockNavigationThrottleTest, RestartsSafeMainFrameRewrite) {
  std::optional<GURL> restarted_url;
  AdBlockNavigationThrottle throttle(
      registry_, base::BindRepeating(
                     [](AdBlockRequest,
                        AdBlockNavigationThrottle::DecisionCallback callback) {
                       AdBlockDecision decision;
                       decision.action = AdBlockAction::kRewrite;
                       decision.rewritten_url =
                           "https://news.example/start?id=7";
                       std::move(callback).Run(std::move(decision));
                     }));
  handle_.set_url(
      GURL("https://news.example/start?id=7&utm=tracking"));
  throttle.set_restart_callback_for_testing(base::BindRepeating(
      [](std::optional<GURL>* restarted_url, const GURL& url) {
        *restarted_url = url;
      },
      &restarted_url));

  EXPECT_EQ(content::NavigationThrottle::CANCEL,
            throttle.WillStartRequest().action());
  ASSERT_TRUE(restarted_url);
  EXPECT_EQ(GURL("https://news.example/start?id=7"), *restarted_url);
}

TEST_F(AdBlockNavigationThrottleTest, RejectsUnsafeMainFrameRewrite) {
  AdBlockNavigationThrottle throttle(
      registry_, base::BindRepeating(
                     [](AdBlockRequest,
                        AdBlockNavigationThrottle::DecisionCallback callback) {
                       AdBlockDecision decision;
                       decision.action = AdBlockAction::kRewrite;
                       decision.rewritten_url =
                           "https://other.example/start";
                       std::move(callback).Run(std::move(decision));
                     }));

  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            throttle.WillStartRequest().action());
}

TEST_F(AdBlockNavigationThrottleTest,
       AsyncBlockDefersThenCancelsWithBlockedByClient) {
  std::optional<AdBlockNavigationThrottle::DecisionCallback> delayed_decision;
  std::optional<content::NavigationThrottle::ThrottleCheckResult> cancellation;
  AdBlockNavigationThrottle throttle(
      registry_,
      base::BindRepeating(
          [](std::optional<AdBlockNavigationThrottle::DecisionCallback>*
                 delayed_decision,
             AdBlockRequest,
             AdBlockNavigationThrottle::DecisionCallback callback) {
            delayed_decision->emplace(std::move(callback));
          },
          &delayed_decision));
  throttle.set_cancel_deferred_navigation_callback_for_testing(
      base::BindRepeating(
          [](std::optional<content::NavigationThrottle::ThrottleCheckResult>*
                 cancellation,
             content::NavigationThrottle::ThrottleCheckResult result) {
            cancellation->emplace(std::move(result));
          },
          &cancellation));

  EXPECT_EQ(content::NavigationThrottle::DEFER,
            throttle.WillStartRequest().action());
  ASSERT_TRUE(delayed_decision);
  AdBlockDecision decision;
  decision.action = AdBlockAction::kBlock;
  decision.matched = true;
  std::move(*delayed_decision).Run(std::move(decision));

  ASSERT_TRUE(cancellation);
  EXPECT_EQ(content::NavigationThrottle::CANCEL, cancellation->action());
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, cancellation->net_error_code());
}

TEST_F(AdBlockNavigationThrottleTest, AsyncAllowDefersThenResumes) {
  std::optional<AdBlockNavigationThrottle::DecisionCallback> delayed_decision;
  bool resumed = false;
  AdBlockNavigationThrottle throttle(
      registry_,
      base::BindRepeating(
          [](std::optional<AdBlockNavigationThrottle::DecisionCallback>*
                 delayed_decision,
             AdBlockRequest,
             AdBlockNavigationThrottle::DecisionCallback callback) {
            delayed_decision->emplace(std::move(callback));
          },
          &delayed_decision));
  throttle.set_resume_callback_for_testing(
      base::BindRepeating([](bool* resumed) { *resumed = true; }, &resumed));

  EXPECT_EQ(content::NavigationThrottle::DEFER,
            throttle.WillStartRequest().action());
  ASSERT_TRUE(delayed_decision);
  std::move(*delayed_decision).Run(AdBlockDecision());
  EXPECT_TRUE(resumed);
}

TEST_F(AdBlockNavigationThrottleTest, RechecksRedirectDestination) {
  int checks = 0;
  AdBlockNavigationThrottle throttle(
      registry_, base::BindRepeating(
                     [](int* checks, AdBlockRequest request,
                        AdBlockNavigationThrottle::DecisionCallback callback) {
                       ++*checks;
                       AdBlockDecision decision;
                       if (request.url == "https://ads.example/redirected") {
                         decision.action = AdBlockAction::kBlock;
                         decision.matched = true;
                       }
                       std::move(callback).Run(std::move(decision));
                     },
                     &checks));

  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            throttle.WillStartRequest().action());
  handle_.set_url(GURL("https://ads.example/redirected"));
  EXPECT_EQ(content::NavigationThrottle::BLOCK_REQUEST,
            throttle.WillRedirectRequest().action());
  EXPECT_EQ(2, checks);
}

TEST_F(AdBlockNavigationThrottleTest, InternalSchemeBypassesEngine) {
  handle_.set_url(GURL("chrome://settings"));
  int checks = 0;
  AdBlockNavigationThrottle throttle(
      registry_, base::BindRepeating(
                     [](int* checks, AdBlockRequest,
                        AdBlockNavigationThrottle::DecisionCallback callback) {
                       ++*checks;
                       std::move(callback).Run(AdBlockDecision());
                     },
                     &checks));

  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            throttle.WillStartRequest().action());
  EXPECT_EQ(0, checks);
}

}  // namespace
}  // namespace seoul::adblock
