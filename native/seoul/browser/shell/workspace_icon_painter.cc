// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "seoul/browser/shell/workspace_icon_painter.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/no_destructor.h"
#include "cc/paint/paint_flags.h"
#include "seoul/browser/shell/workspace_icon_data.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/utils/SkParsePath.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"

namespace seoul {
namespace {

constexpr float kZenIconCanvasSize = 512.0f;

struct ParsedWorkspaceIcon {
  std::string_view name;
  SkPath path;
};

std::vector<ParsedWorkspaceIcon> ParseWorkspaceIcons() {
  std::vector<ParsedWorkspaceIcon> parsed;
  parsed.reserve(WorkspaceBuiltinIconCatalog().size());
  for (const WorkspaceBuiltinIconData& icon : WorkspaceBuiltinIconCatalog()) {
    std::optional<SkPath> path =
        SkParsePath::FromSVGString(icon.svg_path.data());
    CHECK(path.has_value()) << "Invalid generated Zen icon: " << icon.name;
    parsed.push_back({icon.name, std::move(path.value())});
  }
  return parsed;
}

const ParsedWorkspaceIcon* FindParsedIcon(std::string_view name) {
  static const base::NoDestructor<const std::vector<ParsedWorkspaceIcon>> icons(
      ParseWorkspaceIcons());
  for (const ParsedWorkspaceIcon& icon : *icons) {
    if (icon.name == name) {
      return &icon;
    }
  }
  return nullptr;
}

}  // namespace

std::string WorkspaceBuiltinIconRef(std::string_view name) {
  return std::string(kWorkspaceBuiltinIconPrefix) + std::string(name);
}

std::string_view WorkspaceBuiltinIconName(std::string_view icon_ref) {
  if (!icon_ref.starts_with(kWorkspaceBuiltinIconPrefix)) {
    return std::string_view();
  }
  return icon_ref.substr(kWorkspaceBuiltinIconPrefix.size());
}

bool IsWorkspaceBuiltinIcon(std::string_view icon_ref) {
  const std::string_view name = WorkspaceBuiltinIconName(icon_ref);
  return !name.empty() && FindWorkspaceBuiltinIcon(name);
}

bool PaintWorkspaceBuiltinIcon(gfx::Canvas* canvas,
                               std::string_view icon_ref,
                               const gfx::Rect& bounds,
                               SkColor color) {
  if (!canvas || bounds.IsEmpty()) {
    return false;
  }
  const ParsedWorkspaceIcon* icon =
      FindParsedIcon(WorkspaceBuiltinIconName(icon_ref));
  if (!icon) {
    return false;
  }

  const int icon_size = std::min(bounds.width(), bounds.height());
  const int x = bounds.x() + (bounds.width() - icon_size) / 2;
  const int y = bounds.y() + (bounds.height() - icon_size) / 2;
  const float scale = static_cast<float>(icon_size) / kZenIconCanvasSize;

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  flags.setStyle(cc::PaintFlags::kFill_Style);

  canvas->Save();
  canvas->Translate(gfx::Vector2d(x, y));
  canvas->Scale(scale, scale);
  canvas->DrawPath(icon->path, flags);
  canvas->Restore();
  return true;
}

}  // namespace seoul
