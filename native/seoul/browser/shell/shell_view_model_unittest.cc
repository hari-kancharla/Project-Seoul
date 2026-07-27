// Project Seoul native browser shell V0.

#include "seoul/browser/shell/shell_view_model.h"

#include <algorithm>

#include "seoul/browser/organization/organization_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

TEST(ShellViewModelTest, EmptyWorkspaceShowsEmptyState) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  const WorkspaceId ws = model.default_workspace();
  ShellBuildContext context;
  context.window = LiveWindowKey::FromSessionId(1);
  ASSERT_TRUE(model.SetActiveWorkspaceForWindow(context.window.value(), ws)
                  .has_value());
  WindowProjection projection;
  projection.window = context.window;
  projection.active_workspace = ws;
  projection.empty_workspace = true;
  LiveWindowSnapshot live;
  live.window = context.window;
  ShellSnapshot snapshot =
      ShellViewModel::Build(model, context, projection, live, 1);
  EXPECT_TRUE(snapshot.show_empty_workspace);
  EXPECT_EQ(snapshot.status, ShellStatus::kEmptyWorkspace);
}

TEST(ShellViewModelTest,
     AppearanceActionsFailClosedUntilNativeCallbacksAreInstalled) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  ShellBuildContext context;
  context.window = LiveWindowKey::FromSessionId(1);
  WindowProjection projection;
  projection.window = context.window;
  LiveWindowSnapshot live;

  const ShellSnapshot snapshot =
      ShellViewModel::Build(model, context, projection, live, 1);
  for (ShellUtilityAction expected :
       {ShellUtilityAction::kSetAppearanceSingle,
        ShellUtilityAction::kSetAppearanceMultiple,
        ShellUtilityAction::kSetAppearanceCollapsed}) {
    const auto action = std::ranges::find(snapshot.actions, expected,
                                          &ShellActionEnablement::action);
    ASSERT_NE(action, snapshot.actions.end());
    EXPECT_FALSE(action->enabled);
    EXPECT_EQ(action->disabled_reason,
              "Appearance layouts are unavailable for this window.");
  }
}

TEST(ShellViewModelTest, EssentialsComeFromOrganizationModel) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  ASSERT_TRUE(model
                  .CreateOrUpdateEssential(EssentialId(), "Mail",
                                           "https://mail.example/")
                  .has_value());
  ShellBuildContext context;
  context.window = LiveWindowKey::FromSessionId(1);
  WindowProjection projection;
  projection.window = context.window;
  LiveWindowSnapshot live;
  ShellSnapshot snapshot =
      ShellViewModel::Build(model, context, projection, live, 1);
  ASSERT_EQ(snapshot.essentials.size(), 1u);
  EXPECT_EQ(snapshot.essentials[0].name, "Mail");
}

TEST(ShellViewModelTest, SpacesComeFromOrderedUnarchivedWorkspaces) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  const WorkspaceId first = model.default_workspace();
  const auto second_result = model.CreateWorkspace("Research");
  ASSERT_TRUE(second_result.has_value());
  const WorkspaceId second = second_result.value();

  ShellBuildContext context;
  context.window = LiveWindowKey::FromSessionId(1);
  ASSERT_TRUE(model.SetActiveWorkspaceForWindow(context.window.value(), second)
                  .has_value());
  WindowProjection projection;
  projection.window = context.window;
  LiveWindowSnapshot live;
  const ShellSnapshot snapshot =
      ShellViewModel::Build(model, context, projection, live, 1);

  ASSERT_EQ(snapshot.spaces.size(), 2u);
  EXPECT_EQ(snapshot.spaces[0].workspace_id, first);
  EXPECT_FALSE(snapshot.spaces[0].is_active);
  EXPECT_EQ(snapshot.spaces[1].workspace_id, second);
  EXPECT_EQ(snapshot.spaces[1].name, "Research");
  EXPECT_TRUE(snapshot.spaces[1].is_active);
}

TEST(ShellViewModelTest, EssentialAssociatesWithExistingOriginInWindow) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  ASSERT_TRUE(model
                  .CreateOrUpdateEssential(EssentialId(), "Mail",
                                           "https://mail.example/inbox")
                  .has_value());
  ShellBuildContext context;
  context.window = LiveWindowKey::FromSessionId(1);
  WindowProjection projection;
  projection.window = context.window;
  LiveWindowSnapshot live;
  live.window = context.window;
  LiveTabDescriptor tab;
  tab.tab = LiveTabKey::FromSessionId(9);
  tab.origin = "https://mail.example";
  tab.is_active = true;
  live.tabs.push_back(tab);
  live.active_tab = tab.tab;

  ShellSnapshot snapshot =
      ShellViewModel::Build(model, context, projection, live, 1);
  ASSERT_EQ(snapshot.essentials.size(), 1u);
  EXPECT_TRUE(snapshot.essentials[0].has_live_tab);
  EXPECT_TRUE(snapshot.essentials[0].live_in_current_window);
  EXPECT_TRUE(snapshot.essentials[0].is_active);
  EXPECT_EQ(snapshot.essentials[0].live_tab, tab.tab);
  EXPECT_EQ(snapshot.essentials[0].live_window, context.window);
}

TEST(ShellViewModelTest, EssentialAssociatesAcrossWindowsWithoutPathData) {
  OrganizationModel model;
  ASSERT_TRUE(model.EnsureDefaultWorkspace().has_value());
  ASSERT_TRUE(model
                  .CreateOrUpdateEssential(EssentialId(), "Docs",
                                           "https://docs.example/home")
                  .has_value());
  ShellBuildContext context;
  context.window = LiveWindowKey::FromSessionId(1);
  LiveWindowSnapshot other;
  other.window = LiveWindowKey::FromSessionId(2);
  LiveTabDescriptor tab;
  tab.tab = LiveTabKey::FromSessionId(20);
  tab.origin = "https://docs.example";
  other.tabs.push_back(tab);
  other.active_tab = tab.tab;
  context.other_live_windows.push_back(other);
  WindowProjection projection;
  projection.window = context.window;
  LiveWindowSnapshot current;
  current.window = context.window;

  const ShellSnapshot snapshot =
      ShellViewModel::Build(model, context, projection, current, 1);
  ASSERT_EQ(snapshot.essentials.size(), 1u);
  EXPECT_TRUE(snapshot.essentials[0].has_live_tab);
  EXPECT_FALSE(snapshot.essentials[0].live_in_current_window);
  EXPECT_TRUE(snapshot.essentials[0].is_active);
  EXPECT_EQ(snapshot.essentials[0].live_tab, tab.tab);
  EXPECT_EQ(snapshot.essentials[0].live_window, other.window);
}

TEST(ShellViewModelTest, SplitCandidatesAreExplicitTitledAndUnsplit) {
  WindowProjection projection;
  projection.active_tab = LiveTabKey::FromSessionId(1);
  ProjectedTab active;
  active.tab = projection.active_tab;
  ProjectedTab eligible;
  eligible.tab = LiveTabKey::FromSessionId(2);
  ProjectedTab already_split;
  already_split.tab = LiveTabKey::FromSessionId(3);
  ProjectedTab stale;
  stale.tab = LiveTabKey::FromSessionId(4);
  projection.tabs = {active, eligible, already_split, stale};

  LiveWindowSnapshot live;
  LiveTabDescriptor eligible_live;
  eligible_live.tab = eligible.tab;
  eligible_live.title = "Reference";
  eligible_live.origin = "https://reference.test";
  LiveTabDescriptor split_live;
  split_live.tab = already_split.tab;
  split_live.upstream_split_token = "split-existing";
  live.tabs = {eligible_live, split_live};

  const auto candidates =
      ShellViewModel::BuildSplitCandidates(projection, live);
  ASSERT_EQ(candidates.size(), 1u);
  EXPECT_EQ(candidates[0].tab, eligible.tab);
  EXPECT_EQ(candidates[0].title, "Reference");
  EXPECT_EQ(candidates[0].origin, "https://reference.test");

  LiveTabDescriptor active_live;
  active_live.tab = active.tab;
  active_live.upstream_split_token = "active-split";
  live.tabs.push_back(active_live);
  EXPECT_TRUE(ShellViewModel::BuildSplitCandidates(projection, live).empty());
}

TEST(ShellViewModelTest, TaskDeckLabelsAreBoundedAndAttentionFirst) {
  ShellTaskSummary tasks;
  EXPECT_EQ(ShellViewModel::TaskButtonLabel(tasks, ShellMode::kExpanded),
            "Tasks");
  EXPECT_EQ(ShellViewModel::TaskButtonLabel(tasks, ShellMode::kCollapsed), "○");
  EXPECT_EQ(ShellViewModel::TaskAccessibleName(tasks), "Task Deck, no tasks");

  tasks.total = 7;
  tasks.active = 2;
  tasks.waiting_for_user = 1;
  tasks.paused = 1;
  tasks.failed = 1;
  EXPECT_EQ(ShellViewModel::TaskButtonLabel(tasks, ShellMode::kExpanded),
            "Tasks 7");
  EXPECT_EQ(ShellViewModel::TaskButtonLabel(tasks, ShellMode::kCollapsed), "!");
  EXPECT_EQ(ShellViewModel::TaskAccessibleName(tasks),
            "Task Deck: 2 active, 1 waiting for you, 1 paused, 1 failed");

  tasks.active = 0;
  tasks.waiting_for_user = 0;
  tasks.paused = 0;
  EXPECT_EQ(ShellViewModel::TaskButtonLabel(tasks, ShellMode::kCollapsed), "○");
}

}  // namespace
}  // namespace seoul
