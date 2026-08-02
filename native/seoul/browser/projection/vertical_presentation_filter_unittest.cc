// Project Seoul workspace projection engine V0.

#include "seoul/browser/projection/vertical_presentation_filter.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

TEST(VerticalPresentationFilterTest, HidesInactiveWorkspaceTabs) {
  WindowProjection projection;
  ProjectedTab tab;
  tab.tab = LiveTabKey::FromSessionId(1);
  projection.tabs.push_back(tab);
  VerticalPresentationFilter filter(projection);
  EXPECT_TRUE(filter.ShouldPresentTab(tab.tab));
  EXPECT_FALSE(filter.ShouldPresentTab(LiveTabKey::FromSessionId(2)));
}

TEST(VerticalPresentationFilterTest, FailOpenDisabledFilterShowsAll) {
  WindowProjection projection;
  projection.status = ProjectionStatus::kFailOpen;
  VerticalPresentationFilter filter(projection);
  filter.SetDisabled(true);
  EXPECT_TRUE(filter.ShouldPresentTab(LiveTabKey::FromSessionId(99)));
}

TEST(VerticalPresentationFilterTest,
     ExplicitHiddenTabStaysHiddenDuringFailOpenAndDisabledRecovery) {
  WindowProjection projection;
  projection.status = ProjectionStatus::kFailOpen;
  const LiveTabKey placeholder = LiveTabKey::FromSessionId(1);
  projection.hidden_tabs.push_back(placeholder);
  VerticalPresentationFilter filter(projection);
  filter.SetDisabled(true);

  EXPECT_TRUE(filter.IsExplicitlyHiddenTab(placeholder));
  EXPECT_FALSE(filter.ShouldPresentTab(placeholder));
  EXPECT_TRUE(filter.ShouldPresentTab(LiveTabKey::FromSessionId(2)));
}

TEST(VerticalPresentationFilterTest, SplitRequiresBothPanesProjected) {
  WindowProjection projection;
  ProjectedTab a;
  a.tab = LiveTabKey::FromSessionId(1);
  projection.tabs.push_back(a);
  ProjectedSplit split;
  split.upstream_split_token = "split-1";
  split.pane_a = LiveTabKey::FromSessionId(1);
  split.pane_b = LiveTabKey::FromSessionId(2);
  projection.splits.push_back(split);
  VerticalPresentationFilter filter(projection);
  EXPECT_TRUE(filter.ShouldPresentSplit("split-1"));
  EXPECT_FALSE(filter.ShouldPresentTab(LiveTabKey::FromSessionId(2)));
}

}  // namespace
}  // namespace seoul
