// Tests for exact-scope agent permissions.

#include "seoul/browser/policy/agent_permission_service.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace seoul {
namespace {

AgentPermissionRequest PageRead(const std::string& origin) {
  AgentPermissionRequest request;
  request.capability = ToolId::FromString("page.observe.text");
  request.approval = ApprovalPolicy::kFirstUsePerScope;
  request.risk = RiskCategory::kReadOnly;
  request.sensitivity = DataSensitivity::kPageContent;
  request.window = LiveWindowKey::FromSessionId(1);
  request.tab = LiveTabKey::FromSessionId(2);
  request.frame_scope = "main";
  request.source_origin = url::Origin::Create(GURL(origin));
  return request;
}

TEST(AgentPermissionServiceTest, FirstUseGrantIsExactAndExpires) {
  base::Time now = base::Time::UnixEpoch() + base::Days(1);
  AgentPermissionService service(
      base::BindRepeating([](base::Time* now) { return *now; }, &now));
  AgentPermissionRequest request = PageRead("https://a.test/path");
  EXPECT_EQ(service.Evaluate(request).kind,
            AgentPermissionDecisionKind::kNeedsApproval);
  ASSERT_TRUE(service.GrantFirstUse(request, base::Minutes(10)));
  // A separately resolved request has distinct opaque "not applicable"
  // origins. Those absent optional scopes must compare equal; otherwise every
  // first-use grant prompts again despite an identical live page scope.
  AgentPermissionRequest same_scope = PageRead("https://a.test/other");
  EXPECT_EQ(service.Evaluate(same_scope).kind,
            AgentPermissionDecisionKind::kAllowed);

  AgentPermissionRequest other_tab = request;
  other_tab.tab = LiveTabKey::FromSessionId(3);
  EXPECT_EQ(service.Evaluate(other_tab).kind,
            AgentPermissionDecisionKind::kNeedsApproval);
  AgentPermissionRequest other_origin = request;
  other_origin.source_origin = url::Origin::Create(GURL("https://b.test"));
  EXPECT_EQ(service.Evaluate(other_origin).kind,
            AgentPermissionDecisionKind::kNeedsApproval);

  now += base::Minutes(11);
  EXPECT_EQ(service.Evaluate(request).kind,
            AgentPermissionDecisionKind::kNeedsApproval);
  EXPECT_EQ(service.grant_count(), 0u);
}

TEST(AgentPermissionServiceTest, DestinationPairIsPartOfExactScope) {
  base::Time now = base::Time::Now();
  AgentPermissionService service(
      base::BindRepeating([](base::Time* now) { return *now; }, &now));
  AgentPermissionRequest request = PageRead("https://a.test");
  request.destination_origin =
      url::Origin::Create(GURL("https://destination.test"));
  ASSERT_TRUE(service.GrantFirstUse(request));
  EXPECT_EQ(service.Evaluate(request).kind,
            AgentPermissionDecisionKind::kAllowed);
  request.destination_origin = url::Origin::Create(GURL("https://other.test"));
  EXPECT_EQ(service.Evaluate(request).kind,
            AgentPermissionDecisionKind::kNeedsApproval);
}

TEST(AgentPermissionServiceTest,
     DestinationOnlyBrowserScopeIsValidAndRemainsExact) {
  base::Time now = base::Time::Now();
  AgentPermissionService service(
      base::BindRepeating([](base::Time* now) { return *now; }, &now));
  AgentPermissionRequest request;
  request.capability = ToolId::FromString("browser.tabs.open");
  request.approval = ApprovalPolicy::kFirstUsePerScope;
  request.risk = RiskCategory::kReversibleMutation;
  request.window = LiveWindowKey::FromSessionId(1);
  request.destination_origin =
      url::Origin::Create(GURL("https://destination.test/path"));

  EXPECT_EQ(service.Evaluate(request).kind,
            AgentPermissionDecisionKind::kNeedsApproval);
  ASSERT_TRUE(service.GrantFirstUse(request));
  EXPECT_EQ(service.Evaluate(request).kind,
            AgentPermissionDecisionKind::kAllowed);

  AgentPermissionRequest other_destination = request;
  other_destination.destination_origin =
      url::Origin::Create(GURL("https://other.test"));
  EXPECT_EQ(service.Evaluate(other_destination).kind,
            AgentPermissionDecisionKind::kNeedsApproval);

  AgentPermissionRequest other_window = request;
  other_window.window = LiveWindowKey::FromSessionId(2);
  EXPECT_EQ(service.Evaluate(other_window).kind,
            AgentPermissionDecisionKind::kNeedsApproval);
}

TEST(AgentPermissionServiceTest, HighRiskApprovalIsNeverReusable) {
  base::Time now = base::Time::Now();
  AgentPermissionService service(
      base::BindRepeating([](base::Time* now) { return *now; }, &now));
  AgentPermissionRequest request = PageRead("https://a.test");
  request.capability = ToolId::FromString("page.act.click");
  request.risk = RiskCategory::kIrreversibleMutation;
  EXPECT_FALSE(service.GrantFirstUse(request));
  EXPECT_EQ(service.Evaluate(request).kind,
            AgentPermissionDecisionKind::kNeedsApproval);
}

TEST(AgentPermissionServiceTest, InvalidOrBroaderPageScopesFailClosed) {
  base::Time now = base::Time::Now();
  AgentPermissionService service(
      base::BindRepeating([](base::Time* now) { return *now; }, &now));
  AgentPermissionRequest missing_tab = PageRead("https://a.test");
  missing_tab.tab = LiveTabKey();
  EXPECT_EQ(service.Evaluate(missing_tab).kind,
            AgentPermissionDecisionKind::kDenied);
  AgentPermissionRequest subframe = PageRead("https://a.test");
  subframe.frame_scope = "child-1";
  EXPECT_EQ(service.Evaluate(subframe).kind,
            AgentPermissionDecisionKind::kDenied);
  AgentPermissionRequest internal = PageRead("chrome://settings");
  EXPECT_EQ(service.Evaluate(internal).kind,
            AgentPermissionDecisionKind::kDenied);
}

TEST(AgentPermissionServiceTest, RevocationIsTabAndWindowScoped) {
  base::Time now = base::Time::Now();
  AgentPermissionService service(
      base::BindRepeating([](base::Time* now) { return *now; }, &now));
  AgentPermissionRequest first = PageRead("https://a.test");
  AgentPermissionRequest second = PageRead("https://b.test");
  second.tab = LiveTabKey::FromSessionId(3);
  ASSERT_TRUE(service.GrantFirstUse(first));
  ASSERT_TRUE(service.GrantFirstUse(second));
  service.RevokeTab(first.tab);
  EXPECT_EQ(service.Evaluate(first).kind,
            AgentPermissionDecisionKind::kNeedsApproval);
  EXPECT_EQ(service.Evaluate(second).kind,
            AgentPermissionDecisionKind::kAllowed);
  service.RevokeWindow(second.window);
  EXPECT_EQ(service.grant_count(), 0u);
}

}  // namespace
}  // namespace seoul
