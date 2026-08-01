// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "seoul/browser/shell/download_animation_geometry.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

TEST(DownloadAnimationGeometryTest, PreservesMeasuredEndpoints) {
  const gfx::PointF start(700.0f, 400.0f);
  const gfx::PointF end(42.0f, 760.0f);
  const gfx::RectF window(0.0f, 0.0f, 1280.0f, 800.0f);

  const DownloadArcFrame first =
      CalculateDownloadArcFrame(start, end, window, 0.0);
  EXPECT_FLOAT_EQ(start.x(), first.center.x());
  EXPECT_FLOAT_EQ(start.y(), first.center.y());
  EXPECT_FLOAT_EQ(0.3f, first.opacity);
  EXPECT_FLOAT_EQ(0.5f, first.scale);

  const DownloadArcFrame middle =
      CalculateDownloadArcFrame(start, end, window, 0.5);
  EXPECT_FLOAT_EQ(1.8f, middle.scale);
  EXPECT_LT(middle.center.y(), start.y())
      << "The arc must bend toward the larger available top region";

  const DownloadArcFrame last =
      CalculateDownloadArcFrame(start, end, window, 1.0);
  EXPECT_FLOAT_EQ(end.x(), last.center.x());
  EXPECT_FLOAT_EQ(end.y(), last.center.y());
  EXPECT_FLOAT_EQ(0.0f, last.opacity);
  EXPECT_FLOAT_EQ(0.45f, last.scale);
}

TEST(DownloadAnimationGeometryTest, ChoosesLowerArcWhenBottomHasMoreRoom) {
  const gfx::PointF start(800.0f, 100.0f);
  const gfx::PointF end(80.0f, 120.0f);
  const gfx::RectF window(0.0f, 0.0f, 1280.0f, 900.0f);

  const DownloadArcFrame middle =
      CalculateDownloadArcFrame(start, end, window, 0.5);
  EXPECT_GT(middle.center.y(), end.y());
}

TEST(DownloadAnimationGeometryTest, ClampsOutOfRangeProgress) {
  const gfx::PointF start(100.0f, 100.0f);
  const gfx::PointF end(200.0f, 200.0f);
  const gfx::RectF window(0.0f, 0.0f, 400.0f, 400.0f);

  const DownloadArcFrame before =
      CalculateDownloadArcFrame(start, end, window, -1.0);
  EXPECT_FLOAT_EQ(start.x(), before.center.x());
  EXPECT_FLOAT_EQ(start.y(), before.center.y());
  const DownloadArcFrame after =
      CalculateDownloadArcFrame(start, end, window, 2.0);
  EXPECT_FLOAT_EQ(end.x(), after.center.x());
  EXPECT_FLOAT_EQ(end.y(), after.center.y());
}

} // namespace
} // namespace seoul
