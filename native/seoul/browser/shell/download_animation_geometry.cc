// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "seoul/browser/shell/download_animation_geometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace seoul {
namespace {

constexpr double kArcHeightRatio = 0.8;
constexpr double kMaximumArcHeight = 1200.0;
constexpr float kStartScale = 0.5f;
constexpr float kMaximumScale = 1.8f;
constexpr float kEndScale = 0.45f;

double EaseInOutQuad(double value) {
  return value < 0.5 ? 2.0 * value * value : -1.0 + (4.0 - 2.0 * value) * value;
}

gfx::PointF CalculateCenter(const gfx::PointF &start, const gfx::PointF &end,
                            double eased, double arc_height,
                            double arc_direction) {
  const double x = (end.x() - start.x()) * eased;
  const double y =
      (end.y() - start.y()) * eased +
      arc_direction * arc_height * (1.0 - std::pow(2.0 * eased - 1.0, 2.0));
  return gfx::PointF(static_cast<float>(start.x() + x),
                     static_cast<float>(start.y() + y));
}

} // namespace

DownloadArcFrame CalculateDownloadArcFrame(const gfx::PointF &start,
                                           const gfx::PointF &end,
                                           const gfx::RectF &window_bounds,
                                           double progress) {
  progress = std::clamp(progress, 0.0, 1.0);
  const double eased = EaseInOutQuad(progress);
  const double distance_x = end.x() - start.x();
  const double distance_y = end.y() - start.y();
  const double distance =
      std::sqrt(distance_x * distance_x + distance_y * distance_y);

  const double available_top =
      std::max(0.0, static_cast<double>(std::min(start.y(), end.y()) -
                                        window_bounds.y()));
  const double available_bottom =
      std::max(0.0, static_cast<double>(window_bounds.bottom() -
                                        std::max(start.y(), end.y())));
  const bool arc_downward = available_bottom > available_top;
  const double available_arc_space =
      arc_downward ? available_bottom : available_top;
  const double arc_height =
      std::min({distance * kArcHeightRatio, kMaximumArcHeight,
                available_arc_space * kArcHeightRatio});
  const double arc_direction = arc_downward ? 1.0 : -1.0;

  DownloadArcFrame frame;
  frame.center = CalculateCenter(start, end, eased, arc_height, arc_direction);

  if (progress < 0.3) {
    frame.opacity = static_cast<float>(0.3 + (progress / 0.3) * 0.6);
  } else if (progress < 0.98) {
    frame.opacity = static_cast<float>(0.9 + ((progress - 0.3) / 0.6) * 0.1);
  } else {
    frame.opacity = static_cast<float>(1.0 - ((progress - 0.9) / 0.1));
  }
  frame.opacity = std::clamp(frame.opacity, 0.0f, 1.0f);
  if (progress >= 1.0) {
    frame.opacity = 0.0f;
  }

  if (progress < 0.5) {
    frame.scale = static_cast<float>(
        kStartScale + (progress / 0.5) * (kMaximumScale - kStartScale));
  } else {
    frame.scale = static_cast<float>(
        kMaximumScale - ((progress - 0.5) / 0.5) * (kMaximumScale - kEndScale));
  }

  constexpr double kDirectionSample = 1.0 / 60.0;
  const double previous_progress = std::max(0.0, progress - kDirectionSample);
  const gfx::PointF previous_center = CalculateCenter(
      start, end, EaseInOutQuad(previous_progress), arc_height, arc_direction);
  if (previous_center != frame.center) {
    frame.rotation_degrees =
        static_cast<float>(std::atan2(frame.center.y() - previous_center.y(),
                                      frame.center.x() - previous_center.x()) *
                           180.0 / std::numbers::pi);
  }
  return frame;
}

} // namespace seoul
