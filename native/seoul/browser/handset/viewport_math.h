// Project Seoul Handset - resolved geometry.
//
// One place that turns a chosen device, an orientation, and a window size into
// the exact numbers the widget is programmed with. The browser layer holds no
// geometry rules of its own, so what a site measures is decided here and is
// testable without a browser.

#ifndef SEOUL_BROWSER_HANDSET_VIEWPORT_MATH_H_
#define SEOUL_BROWSER_HANDSET_VIEWPORT_MATH_H_

#include <string>
#include <utility>

#include "seoul/browser/handset/handset_types.h"

namespace seoul {

// The parse-and-clamp logic behind "type a custom Handset size": a value that
// fails to parse as a positive integer is read as "unset" and returned as 0
// rather than clamped - a live caller (EnableHandsetMode) already treats 0 as
// "leave this dimension at the profile's own size", so clamping it here first
// would turn a blank field into the supported minimum instead of leaving it
// alone. A value that does parse is clamped to
// [kMinHandsetWidthDip, kMaxHandsetWidthDip] (and the height equivalent).
// Returns {0, 0} when both are unset.
//
// Pure so it is directly testable: the UI it actually serves reads its typed
// values through DialogModelTextfield, which only a real platform widget can
// set, so the logic has to live somewhere a test can reach it without one.
std::pair<int, int> ResolveCustomHandsetSize(const std::u16string& width_text,
                                             const std::u16string& height_text);

// The oriented CSS-pixel size of a profile. Landscape swaps the axes; it does
// not rescale, because a rotated phone reports the same pixels transposed.
int HandsetWidthForOrientation(const HandsetProfile& profile,
                               HandsetOrientation orientation);
int HandsetHeightForOrientation(const HandsetProfile& profile,
                                HandsetOrientation orientation);

// Clamps to the supported window range. Out-of-range input is clamped rather
// than rejected: it arrives from a live window drag, where refusing to resolve
// would leave the emulated viewport disagreeing with the window on screen.
int ClampHandsetWidth(int width_dip);
int ClampHandsetHeight(int height_dip);

// The profile whose width in `orientation` is closest to `width_dip`, used in
// free resize to choose a device pixel ratio and User-Agent family that match
// what the window has become.
//
// Orientation is part of the question, not a detail: a landscape window is as
// wide as a portrait device is tall, so comparing a landscape width against
// portrait widths would resolve a rotated phone to a tablet and hand it the
// tablet's pixel ratio.
//
// Ties resolve to the narrower profile, so a window dragged to a boundary
// reports the more constrained layout rather than briefly claiming the roomier
// one. That is enforced by comparing widths explicitly; the catalogue is
// ordered for the picker, not by width, so position in it decides nothing.
const HandsetProfile& NearestHandsetProfileForWidth(
    int width_dip,
    HandsetOrientation orientation);

// Resolves the metrics a live Handset is programmed with.
//
// In kSnapToProfile the view is exactly the oriented profile size and the free
// size is ignored, which is what makes a snapped window reproduce the device.
// In kFree the view follows the window and only the scale factor is inherited,
// from the nearest profile.
//
// Screen size always equals view size. A Handset window carries no browser
// chrome, so the page occupies the whole window exactly as a full-bleed app
// occupies a phone screen, and a site that compares screen to viewport finds
// them consistent instead of finding a phone screen wrapped around a
// desktop-sized view.
HandsetMetrics ResolveHandsetMetrics(const HandsetProfile& profile,
                                     HandsetOrientation orientation,
                                     HandsetSnapMode snap_mode,
                                     int free_width_dip,
                                     int free_height_dip);

}  // namespace seoul

#endif  // SEOUL_BROWSER_HANDSET_VIEWPORT_MATH_H_
