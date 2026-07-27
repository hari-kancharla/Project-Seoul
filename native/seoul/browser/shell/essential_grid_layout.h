// Copyright 2026 The Project Seoul Authors
// Deterministic Zen-compatible column selection for the Essentials deck.

#ifndef SEOUL_BROWSER_SHELL_ESSENTIAL_GRID_LAYOUT_H_
#define SEOUL_BROWSER_SHELL_ESSENTIAL_GRID_LAYOUT_H_

#include <cstddef>

namespace seoul {

// Zen's adaptive grid settles on these exact column counts for the supported
// 12-item Essentials deck. Compact presentation always becomes one column.
constexpr size_t EssentialColumnsForVisibleCount(size_t visible_count,
                                                 bool collapsed) {
  if (visible_count == 0) {
    return 0;
  }
  if (collapsed) {
    return 1;
  }
  if (visible_count <= 4) {
    return visible_count;
  }
  if (visible_count == 5 || visible_count == 6 || visible_count == 9) {
    return 3;
  }
  return 4;
}

constexpr bool ShouldHighlightEssential(bool is_active,
                                        bool live_in_current_window) {
  return is_active && live_in_current_window;
}

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_ESSENTIAL_GRID_LAYOUT_H_
