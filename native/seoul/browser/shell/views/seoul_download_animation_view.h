// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_DOWNLOAD_ANIMATION_VIEW_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_DOWNLOAD_ANIMATION_VIEW_H_

#include "base/time/time.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace content {
class WebContents;
}

namespace seoul {

struct SeoulDownloadAnimationParams {
  gfx::Point start_in_screen;
  gfx::Point target_in_screen;
  gfx::Rect window_bounds_in_screen;
  bool target_button_visible = false;
  bool target_on_right = false;
  base::TimeDelta duration = base::Seconds(1);
  SkColor accent_color = SK_ColorTRANSPARENT;
  SkColor toolbar_color = SK_ColorTRANSPARENT;
  SkColor hover_color = SK_ColorTRANSPARENT;
};

// Starts a self-owned, non-interactive current-Zen download animation above
// `web_contents`. The view destroys itself when the motion ends.
void ShowSeoulDownloadStartedAnimation(
    content::WebContents *web_contents,
    const SeoulDownloadAnimationParams &params);

} // namespace seoul

#endif // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_DOWNLOAD_ANIMATION_VIEW_H_
