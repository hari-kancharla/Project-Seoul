// Copyright 2026 The Project Seoul Authors

#include "seoul/browser/shell/space_visuals.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::space_visuals {
namespace {

TEST(SpaceVisualsTest, InactiveButtonIsMutedWithoutHoverTile) {
  EXPECT_EQ(GetSwitcherButtonVisualState(false, false),
            (SwitcherButtonVisualState{
                .opacity = 0.7f,
                .grayscale = 1.0f,
                .show_hover_highlight = false,
            }));
}

TEST(SpaceVisualsTest, ActiveButtonIsFullStrengthWithoutSelectedTile) {
  EXPECT_EQ(GetSwitcherButtonVisualState(true, false),
            (SwitcherButtonVisualState{
                .opacity = 1.0f,
                .grayscale = 0.0f,
                .show_hover_highlight = false,
            }));
}

TEST(SpaceVisualsTest, HoverTemporarilyRestoresColorAndHighlight) {
  EXPECT_EQ(GetSwitcherButtonVisualState(false, true),
            (SwitcherButtonVisualState{
                .opacity = 1.0f,
                .grayscale = 0.0f,
                .show_hover_highlight = true,
            }));
}

TEST(SpaceVisualsTest, UsesZenSwitcherAndIndicatorMetrics) {
  EXPECT_EQ(kSwitcherButtonSize, 28);
  EXPECT_EQ(kSwitcherGap, 3);
  EXPECT_EQ(kEmptyIconDiameter, 6);
  EXPECT_EQ(GetIndicatorHeight(false), 44);
  EXPECT_EQ(GetIndicatorHeight(true), 38);
  EXPECT_EQ(kIndicatorActionSize, 26);
}

TEST(SpaceVisualsTest, IndicatorActionAppearsOnlyOnInteraction) {
  EXPECT_EQ(GetIndicatorActionOpacity(false), 0.0f);
  EXPECT_EQ(GetIndicatorActionOpacity(true), 1.0f);
}

}  // namespace
}  // namespace seoul::space_visuals
