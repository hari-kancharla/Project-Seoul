// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SEOUL_BROWSER_SHELL_WORKSPACE_ICON_PAINTER_H_
#define SEOUL_BROWSER_SHELL_WORKSPACE_ICON_PAINTER_H_

#include <string>
#include <string_view>

#include "third_party/skia/include/core/SkColor.h"

namespace gfx {
class Canvas;
class Rect;
}  // namespace gfx

namespace seoul {

inline constexpr std::string_view kWorkspaceBuiltinIconPrefix = "zen-icon:";

std::string WorkspaceBuiltinIconRef(std::string_view name);
std::string_view WorkspaceBuiltinIconName(std::string_view icon_ref);
bool IsWorkspaceBuiltinIcon(std::string_view icon_ref);

// Paints an exact Zen selectable icon into `bounds`, preserving its square
// aspect ratio. Returns false when `icon_ref` is not a recognized built-in.
bool PaintWorkspaceBuiltinIcon(gfx::Canvas* canvas,
                               std::string_view icon_ref,
                               const gfx::Rect& bounds,
                               SkColor color);

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_WORKSPACE_ICON_PAINTER_H_
