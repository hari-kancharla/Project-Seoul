// Project Seoul native organization engine.
// Unit tests for deterministic link routing. Authored for a capable compile
// host.

#include "base/test/bind.h"
#include "seoul/browser/organization/organization_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class RoutingTest : public testing::Test {
protected:
  RoutingTest() {
    EXPECT_TRUE(model_.EnsureDefaultWorkspace().has_value());
    def_ = model_.default_workspace();
    work_ = model_.CreateWorkspace("Work").value();
  }

  RoutingRule MakeRule(int priority, RoutingMatchType type,
                       const std::string &pattern,
                       RoutingDisposition disposition,
                       WorkspaceId target = WorkspaceId()) {
    RoutingRule r;
    r.priority = priority;
    r.predicate.match_type = type;
    r.predicate.pattern = pattern;
    r.result.disposition = disposition;
    r.result.target_workspace = target;
    return r;
  }

  RoutingRequest Req(const std::string &url, const std::string &origin) {
    RoutingRequest req;
    req.url = url;
    req.origin = origin;
    req.source_workspace = def_;
    return req;
  }

  OrganizationModel model_;
  WorkspaceId def_;
  WorkspaceId work_;
};

TEST_F(RoutingTest, NoRulesFallsBackToCurrentTab) {
  RoutingResolution res =
      model_.EvaluateRouting(Req("https://a.test/x", "https://a.test"));
  EXPECT_TRUE(res.used_fallback);
  EXPECT_EQ(res.result.disposition, RoutingDisposition::kCurrentTab);
}

TEST_F(RoutingTest, FallbackPreservesRequestedDisposition) {
  RoutingRequest request = Req("https://a.test/x", "https://a.test");
  request.requested_disposition = RoutingDisposition::kNewRetainedTab;
  const RoutingResolution resolution = model_.EvaluateRouting(request);
  EXPECT_TRUE(resolution.used_fallback);
  EXPECT_EQ(resolution.result.disposition,
            RoutingDisposition::kNewRetainedTab);
}

TEST_F(RoutingTest, ExactOriginMatch) {
  ASSERT_TRUE(model_
                  .AddRoutingRule(MakeRule(10, RoutingMatchType::kOriginExact,
                                           "https://mail.test",
                                           RoutingDisposition::kNewRetainedTab))
                  .has_value());
  RoutingResolution res = model_.EvaluateRouting(
      Req("https://mail.test/inbox", "https://mail.test"));
  EXPECT_FALSE(res.used_fallback);
  EXPECT_EQ(res.result.disposition, RoutingDisposition::kNewRetainedTab);

  RoutingResolution miss =
      model_.EvaluateRouting(Req("https://other.test/", "https://other.test"));
  EXPECT_TRUE(miss.used_fallback);
}

TEST_F(RoutingTest, UrlPrefixAndGlob) {
  ASSERT_TRUE(model_
                  .AddRoutingRule(MakeRule(
                      5, RoutingMatchType::kUrlPrefix, "https://docs.test/",
                      RoutingDisposition::kSpecificWorkspace, work_))
                  .has_value());
  ASSERT_TRUE(model_
                  .AddRoutingRule(MakeRule(5, RoutingMatchType::kUrlGlob,
                                           "https://*.media.test/*",
                                           RoutingDisposition::kPreview))
                  .has_value());

  RoutingResolution prefix = model_.EvaluateRouting(
      Req("https://docs.test/spec", "https://docs.test"));
  EXPECT_EQ(prefix.result.disposition, RoutingDisposition::kSpecificWorkspace);
  EXPECT_TRUE(prefix.result.target_workspace == work_);

  RoutingResolution glob = model_.EvaluateRouting(
      Req("https://cdn.media.test/video", "https://cdn.media.test"));
  EXPECT_EQ(glob.result.disposition, RoutingDisposition::kPreview);
}

TEST_F(RoutingTest, PriorityWinsAndTiesAreDeterministic) {
  ASSERT_TRUE(
      model_
          .AddRoutingRule(MakeRule(1, RoutingMatchType::kAnything, "",
                                   RoutingDisposition::kNewTemporaryTab))
          .has_value());
  ASSERT_TRUE(model_
                  .AddRoutingRule(MakeRule(100, RoutingMatchType::kAnything, "",
                                           RoutingDisposition::kSplitPane))
                  .has_value());
  RoutingResolution res =
      model_.EvaluateRouting(Req("https://a.test/", "https://a.test"));
  EXPECT_EQ(res.result.disposition, RoutingDisposition::kSplitPane);

  // Deterministic across repeated evaluations.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(model_.EvaluateRouting(Req("https://a.test/", "https://a.test"))
                  .matched_rule.value(),
              res.matched_rule.value());
  }
}

TEST_F(RoutingTest, SceneScopeRestrictsEligibleRules) {
  const RoutingRuleId temporary =
      model_
          .AddRoutingRule(MakeRule(100, RoutingMatchType::kAnything, "",
                                   RoutingDisposition::kNewTemporaryTab))
          .value();
  const RoutingRuleId retained =
      model_
          .AddRoutingRule(MakeRule(10, RoutingMatchType::kAnything, "",
                                   RoutingDisposition::kNewRetainedTab))
          .value();
  const RoutingRequest request =
      Req("https://a.test/", "https://a.test");
  const RoutingResolution scoped =
      model_.EvaluateRouting(request, std::set<RoutingRuleId>{retained});
  EXPECT_EQ(scoped.matched_rule, retained);
  EXPECT_EQ(scoped.result.disposition,
            RoutingDisposition::kNewRetainedTab);
  const RoutingResolution none =
      model_.EvaluateRouting(request, std::set<RoutingRuleId>());
  EXPECT_TRUE(none.used_fallback);
  EXPECT_NE(none.matched_rule, temporary);
}

TEST_F(RoutingTest, SourceWorkspaceScope) {
  RoutingRule scoped = MakeRule(10, RoutingMatchType::kAnything, "",
                                RoutingDisposition::kNewRetainedTab);
  scoped.predicate.source_workspace = work_; // only applies inside `work_`
  ASSERT_TRUE(model_.AddRoutingRule(scoped).has_value());

  // Request originates in def_: scoped rule does not apply -> fallback.
  EXPECT_TRUE(model_.EvaluateRouting(Req("https://a.test/", "https://a.test"))
                  .used_fallback);

  RoutingRequest from_work = Req("https://a.test/", "https://a.test");
  from_work.source_workspace = work_;
  EXPECT_FALSE(model_.EvaluateRouting(from_work).used_fallback);
}

TEST_F(RoutingTest, RequireUserGesture) {
  RoutingRule rule = MakeRule(10, RoutingMatchType::kAnything, "",
                              RoutingDisposition::kExternalApplication);
  rule.predicate.require_user_gesture = true;
  ASSERT_TRUE(model_.AddRoutingRule(rule).has_value());

  RoutingRequest no_gesture = Req("https://a.test/", "https://a.test");
  no_gesture.user_gesture = false;
  EXPECT_TRUE(model_.EvaluateRouting(no_gesture).used_fallback);

  RoutingRequest gesture = Req("https://a.test/", "https://a.test");
  gesture.user_gesture = true;
  EXPECT_FALSE(model_.EvaluateRouting(gesture).used_fallback);
}

TEST_F(RoutingTest, InvalidRulesRejected) {
  // Non-anything match type requires a pattern.
  EXPECT_EQ(model_
                .AddRoutingRule(MakeRule(1, RoutingMatchType::kOriginExact, "",
                                         RoutingDisposition::kCurrentTab))
                .error(),
            OrganizationError::kInvalidRoutingRule);
  // Specific-workspace destination must reference an existing workspace.
  EXPECT_EQ(model_
                .AddRoutingRule(MakeRule(1, RoutingMatchType::kAnything, "",
                                         RoutingDisposition::kSpecificWorkspace,
                                         WorkspaceId::GenerateNew()))
                .error(),
            OrganizationError::kInvalidRoutingRule);
  EXPECT_EQ(model_
                .AddRoutingRule(MakeRule(
                    1, RoutingMatchType::kAnything, "",
                    RoutingDisposition::kExternalApplication))
                .error(),
            OrganizationError::kInvalidRoutingRule);
}

TEST_F(RoutingTest, UpdateIsAtomicAndPreservesStableIdentity) {
  RoutingRule rule = MakeRule(10, RoutingMatchType::kAnything, "",
                              RoutingDisposition::kNewTemporaryTab);
  const RoutingRuleId id = model_.AddRoutingRule(rule).value();
  rule.id = id;
  rule.priority = 42;
  rule.result.disposition = RoutingDisposition::kPreview;
  ASSERT_TRUE(model_.UpdateRoutingRule(rule).has_value());
  const RoutingResolution updated =
      model_.EvaluateRouting(Req("https://a.test/", "https://a.test"));
  EXPECT_EQ(updated.matched_rule, id);
  EXPECT_EQ(updated.result.disposition, RoutingDisposition::kPreview);

  RoutingRule invalid = rule;
  invalid.predicate.match_type = RoutingMatchType::kOriginExact;
  invalid.predicate.pattern.clear();
  EXPECT_EQ(model_.UpdateRoutingRule(invalid).error(),
            OrganizationError::kInvalidRoutingRule);
  const RoutingResolution unchanged =
      model_.EvaluateRouting(Req("https://a.test/", "https://a.test"));
  EXPECT_EQ(unchanged.matched_rule, id);
  EXPECT_EQ(unchanged.result.disposition, RoutingDisposition::kPreview);
}

TEST_F(RoutingTest, RuleTargetingArchivedWorkspaceIsSkipped) {
  ASSERT_TRUE(model_
                  .AddRoutingRule(
                      MakeRule(10, RoutingMatchType::kAnything, "",
                               RoutingDisposition::kSpecificWorkspace, work_))
                  .has_value());
  ASSERT_TRUE(model_.ArchiveWorkspace(work_).has_value());
  // The matching rule points at an archived workspace; evaluation skips it and
  // falls back safely rather than routing somewhere invalid.
  EXPECT_TRUE(model_.EvaluateRouting(Req("https://a.test/", "https://a.test"))
                  .used_fallback);
}

} // namespace
} // namespace seoul
