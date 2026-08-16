// Copyright 2026 The Project Seoul Authors

#ifndef SEOUL_BROWSER_SHELL_SPACE_VISUALS_H_
#define SEOUL_BROWSER_SHELL_SPACE_VISUALS_H_

namespace seoul::space_visuals {

// These values mirror Zen's workspace switcher and current-workspace
// indicator rather than Chromium's toolbar-button metrics.
inline constexpr int kSwitcherButtonSize = 28;
inline constexpr int kSwitcherGap = 3;
inline constexpr int kSwitcherCornerRadius = 7;
inline constexpr int kEmptyIconDiameter = 6;
// The current Space is a labelled pill - its icon and its name on a filled
// rounded background - the way Arc and Zen show the space you are in. The other
// Spaces stay as icon-sized buttons, so the strip has exactly one wide member
// and reads at a glance without needing colour.
inline constexpr int kCurrentSpacePillHeight = 26;
inline constexpr int kCurrentSpacePillCornerRadius = 13;
// Room for the icon, the gap, and a short name before elision.
inline constexpr int kCurrentSpacePillMinWidth = 64;
inline constexpr int kCurrentSpacePillMaxWidth = 132;
inline constexpr int kCurrentSpacePillHorizontalPadding = 9;
inline constexpr int kCurrentSpacePillIconLabelGap = 6;
inline constexpr int kExpandedIndicatorHeight = 44;
inline constexpr int kCollapsedIndicatorHeight = 38;
inline constexpr int kIndicatorActionSize = 26;

inline constexpr float kInactiveOpacity = 0.7f;
inline constexpr float kInactiveGrayscale = 1.0f;
inline constexpr float kActiveOpacity = 1.0f;
inline constexpr float kActiveGrayscale = 0.0f;

struct SwitcherButtonVisualState {
  float opacity;
  float grayscale;
  bool show_hover_highlight;

  constexpr bool operator==(
      const SwitcherButtonVisualState&) const = default;
};

constexpr SwitcherButtonVisualState GetSwitcherButtonVisualState(
    bool active,
    bool hovered_or_pressed) {
  const bool emphasized = active || hovered_or_pressed;
  return {
      .opacity = emphasized ? kActiveOpacity : kInactiveOpacity,
      .grayscale = emphasized ? kActiveGrayscale : kInactiveGrayscale,
      // Zen highlights hover/drag only. Being the current Space does not
      // produce a large persistent selected tile.
      .show_hover_highlight = hovered_or_pressed,
  };
}

constexpr int GetIndicatorHeight(bool collapsed) {
  return collapsed ? kCollapsedIndicatorHeight : kExpandedIndicatorHeight;
}

constexpr float GetIndicatorActionOpacity(bool hovered_focused_or_open) {
  return hovered_focused_or_open ? 1.0f : 0.0f;
}

}  // namespace seoul::space_visuals

#endif  // SEOUL_BROWSER_SHELL_SPACE_VISUALS_H_
