// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef SEOUL_BROWSER_SHELL_DOWNLOAD_ANIMATION_GEOMETRY_H_
#define SEOUL_BROWSER_SHELL_DOWNLOAD_ANIMATION_GEOMETRY_H_

#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"

namespace seoul {

struct DownloadArcFrame {
  gfx::PointF center;
  float opacity = 0.0f;
  float scale = 1.0f;
  float rotation_degrees = 0.0f;
};

// Computes the current-Zen download token motion at `progress` in [0, 1].
// Positions are in screen DIPs. The parabolic arc automatically bends toward
// the side of the browser window with more available room.
DownloadArcFrame CalculateDownloadArcFrame(const gfx::PointF &start,
                                           const gfx::PointF &end,
                                           const gfx::RectF &window_bounds,
                                           double progress);

} // namespace seoul

#endif // SEOUL_BROWSER_SHELL_DOWNLOAD_ANIMATION_GEOMETRY_H_
