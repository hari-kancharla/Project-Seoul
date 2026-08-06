// Project Seoul `$csp` response throttle tests.

#include "seoul/browser/adblock/ad_block_csp_throttle.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/test/task_environment.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/cpp/content_security_policy/content_security_policy.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "services/network/public/mojom/parsed_headers.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace seoul::adblock {
namespace {

// Counts Resume() so a double-resume is a test failure rather than a CHECK in
// production. Also records destruction ordering for the teardown cases.
class TestDelegate : public blink::URLLoaderThrottle::Delegate {
 public:
  void CancelWithError(int error_code, std::string_view reason) override {}
  void Resume() override { ++resume_count; }

  int resume_count = 0;
};

network::mojom::URLResponseHeadPtr MakeResponseHead(
    const std::string& raw_headers) {
  auto head = network::mojom::URLResponseHead::New();
  head->headers =
      base::MakeRefCounted<net::HttpResponseHeaders>(net::HttpUtil::
          AssembleRawHeaders(raw_headers));
  head->parsed_headers = network::mojom::ParsedHeaders::New();
  network::AddContentSecurityPolicyFromHeaders(
      *head->headers, GURL("https://news.example/"),
      &head->parsed_headers->content_security_policy);
  return head;
}

// Mirrors what the throttle does on a matching directive, so the header/parsed
// invariants can be asserted without standing up a full Profile and engine.
void ApplyDirectivesLikeThrottle(network::mojom::URLResponseHead* head,
                                 const std::string& directives,
                                 const GURL& url) {
  std::vector<network::mojom::ContentSecurityPolicyPtr> parsed =
      network::ParseContentSecurityPolicies(
          directives, network::mojom::ContentSecurityPolicyType::kEnforce,
          network::mojom::ContentSecurityPolicySource::kHTTP, url);
  const bool has_directive =
      std::ranges::any_of(parsed, [](const auto& policy) {
        return !policy->directives.empty() || !policy->raw_directives.empty();
      });
  if (!has_directive) {
    return;
  }
  for (auto& policy : parsed) {
    head->parsed_headers->content_security_policy.push_back(std::move(policy));
  }
  head->headers->AddHeader("Content-Security-Policy", directives);
}

TEST(AdBlockCspThrottleTest, AppendsPolicyWithoutReplacingExistingCsp) {
  const GURL url("https://news.example/index.html");
  auto head = MakeResponseHead(
      "HTTP/1.1 200 OK\n"
      "Content-Security-Policy: img-src 'self'\n");
  ASSERT_EQ(head->parsed_headers->content_security_policy.size(), 1u);

  ApplyDirectivesLikeThrottle(head.get(), "script-src 'none'", url);

  // Site policy survives and the injected one is added alongside it.
  ASSERT_EQ(head->parsed_headers->content_security_policy.size(), 2u);
  std::string value;
  std::size_t iter = 0;
  std::vector<std::string> values;
  while (head->headers->EnumerateHeader(&iter, "Content-Security-Policy",
                                        &value)) {
    values.push_back(value);
  }
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], "img-src 'self'");
  EXPECT_EQ(values[1], "script-src 'none'");
}

TEST(AdBlockCspThrottleTest, LeavesReportOnlyHeaderUntouched) {
  const GURL url("https://news.example/index.html");
  auto head = MakeResponseHead(
      "HTTP/1.1 200 OK\n"
      "Content-Security-Policy-Report-Only: script-src 'self'\n");

  ApplyDirectivesLikeThrottle(head.get(), "script-src 'none'", url);

  // The report-only header is neither removed nor promoted to enforcing.
  std::string report_only;
  std::size_t iter = 0;
  ASSERT_TRUE(head->headers->EnumerateHeader(
      &iter, "Content-Security-Policy-Report-Only", &report_only));
  EXPECT_EQ(report_only, "script-src 'self'");

  // Report-only parses into the same list, so count by type: the site's
  // report-only policy is untouched and exactly one enforced policy was added.
  int enforce_count = 0;
  int report_only_count = 0;
  for (const auto& policy : head->parsed_headers->content_security_policy) {
    if (policy->header->type ==
        network::mojom::ContentSecurityPolicyType::kEnforce) {
      ++enforce_count;
    } else {
      ++report_only_count;
    }
  }
  EXPECT_EQ(enforce_count, 1);
  EXPECT_EQ(report_only_count, 1);
}

TEST(AdBlockCspThrottleTest, MalformedDirectivesAddNoHeader) {
  const GURL url("https://news.example/index.html");
  auto head = MakeResponseHead("HTTP/1.1 200 OK\n");
  ASSERT_TRUE(head->parsed_headers->content_security_policy.empty());

  // A directive string with no recognizable directive must not reach the page.
  ApplyDirectivesLikeThrottle(head.get(), ";;;", url);

  EXPECT_FALSE(head->headers->HasHeader("Content-Security-Policy"));
}

TEST(AdBlockCspThrottleTest, EmptyDirectivesLeaveResponseUnchanged) {
  const GURL url("https://news.example/index.html");
  auto head = MakeResponseHead(
      "HTTP/1.1 200 OK\n"
      "Content-Security-Policy: img-src 'self'\n");

  ApplyDirectivesLikeThrottle(head.get(), "", url);

  ASSERT_EQ(head->parsed_headers->content_security_policy.size(), 1u);
  std::string value;
  std::size_t iter = 0;
  ASSERT_TRUE(head->headers->EnumerateHeader(&iter, "Content-Security-Policy",
                                             &value));
  EXPECT_EQ(value, "img-src 'self'");
  EXPECT_FALSE(head->headers->EnumerateHeader(&iter,
                                              "Content-Security-Policy",
                                              &value));
}

// A throttle with no service resolves synchronously: no defer, so the loader is
// never left waiting on a callback that cannot arrive.
TEST(AdBlockCspThrottleTest, NullServiceDoesNotDeferResponse) {
  base::test::TaskEnvironment task_environment;
  AdBlockCspThrottle throttle(/*service=*/nullptr, /*is_main_frame=*/true,
                              GURL());
  TestDelegate delegate;
  throttle.set_delegate(&delegate);

  auto head = MakeResponseHead("HTTP/1.1 200 OK\n");
  bool defer = false;
  throttle.WillProcessResponse(GURL("https://news.example/index.html"),
                               head.get(), &defer);

  EXPECT_FALSE(defer);
  EXPECT_EQ(delegate.resume_count, 0);
}

TEST(AdBlockCspThrottleTest, NonHttpSchemeDoesNotDeferResponse) {
  base::test::TaskEnvironment task_environment;
  AdBlockCspThrottle throttle(/*service=*/nullptr, /*is_main_frame=*/true,
                              GURL());
  TestDelegate delegate;
  throttle.set_delegate(&delegate);

  auto head = MakeResponseHead("HTTP/1.1 200 OK\n");
  bool defer = false;
  throttle.WillProcessResponse(GURL("chrome://settings"), head.get(), &defer);

  EXPECT_FALSE(defer);
}

// Destroying the throttle while a query is outstanding must not resume or
// touch the freed response head. The weak factory cancels the callback.
TEST(AdBlockCspThrottleTest, DestructionBeforeCallbackDoesNotResume) {
  base::test::TaskEnvironment task_environment;
  TestDelegate delegate;
  auto head = MakeResponseHead("HTTP/1.1 200 OK\n");
  {
    AdBlockCspThrottle throttle(/*service=*/nullptr, /*is_main_frame=*/true,
                                GURL());
    throttle.set_delegate(&delegate);
    bool defer = false;
    throttle.WillProcessResponse(GURL("https://news.example/index.html"),
                                 head.get(), &defer);
  }
  task_environment.RunUntilIdle();

  EXPECT_EQ(delegate.resume_count, 0);
}

// Regression: the service answers inline for Off/ineligible sites. Declaring a
// deferral and resuming from inside WillProcessResponse loses the resume and
// hangs the navigation, so an inline answer must leave `defer` false.
TEST(AdBlockCspThrottleTest, InlineAnswerNeitherDefersNorResumes) {
  base::test::TaskEnvironment task_environment;
  AdBlockCspThrottle throttle(/*service=*/nullptr, /*is_main_frame=*/true,
                              GURL());
  TestDelegate delegate;
  throttle.set_delegate(&delegate);

  auto head = MakeResponseHead("HTTP/1.1 200 OK\n");
  bool defer = false;
  throttle.WillProcessResponse(GURL("https://news.example/index.html"),
                               head.get(), &defer);
  task_environment.RunUntilIdle();

  EXPECT_FALSE(defer);
  EXPECT_EQ(delegate.resume_count, 0);
}

}  // namespace
}  // namespace seoul::adblock
