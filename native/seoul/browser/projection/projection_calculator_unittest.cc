// Project Seoul workspace projection engine V0.

#include "seoul/browser/projection/projection_calculator.h"

#include "base/functional/bind.h"
#include "base/test/bind.h"
#include "seoul/browser/organization/organization_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

LiveTabKey Tab(int id) {
  return LiveTabKey::FromSessionId(id);
}

LiveWindowTabState MakeLive(const std::vector<std::pair<int, int>>& tabs) {
  LiveWindowTabState live;
  live.window = LiveWindowKey::FromSessionId(1);
  for (const auto& [session, order] : tabs) {
    LiveTabDescriptor d;
    d.tab = Tab(session);
    d.strip_order = order;
    live.tabs.push_back(d);
  }
  return live;
}

TEST(ProjectionCalculatorTest, ProjectsSingleWorkspaceTabs) {
  OrganizationModel model(base::BindLambdaForTesting(
      []() { return base::Time::FromSecondsSinceUnixEpoch(100); }));
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  const WorkspaceId ws = model.default_workspace();
  ASSERT_TRUE(model.AddTabMembership(ws, Tab(10).value(), TabRole::kRetained)
                  .has_value());
  ASSERT_TRUE(model.AddTabMembership(ws, Tab(20).value(), TabRole::kTemporary)
                  .has_value());

  LiveWindowTabState live = MakeLive({{10, 0}, {20, 1}, {30, 2}});
  ASSERT_TRUE(model.AddTabMembership(ws, Tab(30).value(), TabRole::kRetained)
                  .has_value());
  const WorkspaceId other = model.CreateWorkspace("other").value();
  ASSERT_TRUE(model
                  .MoveTabToWorkspace(
                      model.FindMembershipIdByTabKey(Tab(30).value()), other)
                  .has_value());

  WindowProjection projection = ProjectionCalculator::Compute(
      model, live, ws, ProjectionGeneration(1), false);
  EXPECT_EQ(projection.tabs.size(), 2u);
  EXPECT_FALSE(projection.empty_workspace);
}

TEST(ProjectionCalculatorTest, EmptyWorkspaceState) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  LiveWindowTabState live = MakeLive({});
  WindowProjection projection = ProjectionCalculator::Compute(
      model, live, model.default_workspace(), ProjectionGeneration(1), false);
  EXPECT_TRUE(projection.empty_workspace);
  EXPECT_EQ(projection.status, ProjectionStatus::kEmptyWorkspace);
}

TEST(ProjectionCalculatorTest, FailOpenShowsAllTabs) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  LiveWindowTabState live = MakeLive({{10, 0}, {20, 1}});
  WindowProjection projection = ProjectionCalculator::Compute(
      model, live, model.default_workspace(), ProjectionGeneration(1), true);
  EXPECT_EQ(projection.status, ProjectionStatus::kFailOpen);
  EXPECT_EQ(projection.tabs.size(), 2u);
}

TEST(ProjectionCalculatorTest,
     ActiveNewTabPlaceholderIsHiddenWithoutDegradingProjection) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  const WorkspaceId workspace = model.default_workspace();
  const LiveTabKey placeholder = Tab(10);
  ASSERT_TRUE(
      model
          .AddTabMembership(workspace, placeholder.value(), TabRole::kTemporary)
          .has_value());

  LiveWindowTabState live = MakeLive({{10, 0}});
  live.active_tab = placeholder;
  live.tabs.front().is_active = true;
  live.tabs.front().is_new_tab_placeholder = true;

  WindowProjection projection = ProjectionCalculator::Compute(
      model, live, workspace, ProjectionGeneration(1), false);
  EXPECT_TRUE(projection.tabs.empty());
  EXPECT_TRUE(projection.empty_workspace);
  EXPECT_EQ(projection.status, ProjectionStatus::kEmptyWorkspace);
  EXPECT_FALSE(projection.active_tab.is_valid());
  EXPECT_TRUE(projection.inconsistencies.empty());
  ASSERT_EQ(projection.hidden_tabs.size(), 1u);
  EXPECT_EQ(projection.hidden_tabs.front(), placeholder);
}

TEST(ProjectionCalculatorTest,
     FailOpenStillHidesPlaceholderButKeepsOrdinaryNewTabPage) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  LiveWindowTabState live = MakeLive({{10, 0}, {20, 1}});
  live.active_tab = Tab(10);
  live.tabs[0].is_active = true;
  live.tabs[0].is_new_tab_placeholder = true;
  // The second descriptor deliberately resembles an NTP but lacks the
  // lifecycle marker. URL/title resemblance alone must never hide it.
  live.tabs[1].title = "New Tab";

  WindowProjection projection = ProjectionCalculator::Compute(
      model, live, model.default_workspace(), ProjectionGeneration(1), true);
  ASSERT_EQ(projection.tabs.size(), 1u);
  EXPECT_EQ(projection.tabs.front().tab, Tab(20));
  EXPECT_FALSE(projection.active_tab.is_valid());
  ASSERT_EQ(projection.hidden_tabs.size(), 1u);
  EXPECT_EQ(projection.hidden_tabs.front(), Tab(10));
}

TEST(ProjectionCalculatorTest,
     NormalProjectionHidesPlaceholderButKeepsOrdinaryNewTabPage) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  const WorkspaceId workspace = model.default_workspace();
  ASSERT_TRUE(
      model.AddTabMembership(workspace, Tab(10).value(), TabRole::kTemporary)
          .has_value());
  ASSERT_TRUE(
      model.AddTabMembership(workspace, Tab(20).value(), TabRole::kTemporary)
          .has_value());

  LiveWindowTabState live = MakeLive({{10, 0}, {20, 1}});
  live.active_tab = Tab(10);
  live.tabs[0].is_active = true;
  live.tabs[0].is_new_tab_placeholder = true;
  live.tabs[1].title = "New Tab";

  WindowProjection projection = ProjectionCalculator::Compute(
      model, live, workspace, ProjectionGeneration(1), false);
  ASSERT_EQ(projection.tabs.size(), 1u);
  EXPECT_EQ(projection.tabs.front().tab, Tab(20));
  EXPECT_FALSE(projection.active_tab.is_valid());
  ASSERT_EQ(projection.hidden_tabs.size(), 1u);
  EXPECT_EQ(projection.hidden_tabs.front(), Tab(10));
  EXPECT_TRUE(projection.inconsistencies.empty());
}

TEST(ProjectionCalculatorTest, RestoredActiveNewTabPageRemainsVisible) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  const WorkspaceId workspace = model.default_workspace();
  ASSERT_TRUE(
      model.AddTabMembership(workspace, Tab(10).value(), TabRole::kRetained)
          .has_value());

  LiveWindowTabState live = MakeLive({{10, 0}});
  live.active_tab = Tab(10);
  live.tabs[0].is_active = true;
  live.tabs[0].title = "New Tab";
  // Session restore intentionally carries no synthetic-placeholder marker.
  live.tabs[0].is_new_tab_placeholder = false;

  WindowProjection projection = ProjectionCalculator::Compute(
      model, live, workspace, ProjectionGeneration(1), false);
  ASSERT_EQ(projection.tabs.size(), 1u);
  EXPECT_EQ(projection.tabs.front().tab, Tab(10));
  EXPECT_EQ(projection.active_tab, Tab(10));
  EXPECT_TRUE(projection.hidden_tabs.empty());
  EXPECT_TRUE(projection.inconsistencies.empty());
}

}  // namespace
}  // namespace seoul
