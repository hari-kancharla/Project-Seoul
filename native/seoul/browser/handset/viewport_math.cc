#include "seoul/browser/handset/viewport_math.h"

#include <algorithm>
#include <cstdlib>

#include "base/strings/string_number_conversions.h"

namespace seoul {

namespace {

int ParseDimensionOrZero(const std::u16string& text) {
  int value = 0;
  if (!base::StringToInt(text, &value) || value <= 0) {
    return 0;
  }
  return value;
}

}  // namespace

std::pair<int, int> ResolveCustomHandsetSize(
    const std::u16string& width_text,
    const std::u16string& height_text) {
  const int width = ParseDimensionOrZero(width_text);
  const int height = ParseDimensionOrZero(height_text);
  return {width == 0 ? 0 : ClampHandsetWidth(width),
         height == 0 ? 0 : ClampHandsetHeight(height)};
}

int HandsetWidthForOrientation(const HandsetProfile& profile,
                               HandsetOrientation orientation) {
  return orientation == HandsetOrientation::kPortrait
             ? profile.portrait_width_dip
             : profile.portrait_height_dip;
}

int HandsetHeightForOrientation(const HandsetProfile& profile,
                                HandsetOrientation orientation) {
  return orientation == HandsetOrientation::kPortrait
             ? profile.portrait_height_dip
             : profile.portrait_width_dip;
}

int ClampHandsetWidth(int width_dip) {
  return std::clamp(width_dip, kMinHandsetWidthDip, kMaxHandsetWidthDip);
}

int ClampHandsetHeight(int height_dip) {
  return std::clamp(height_dip, kMinHandsetHeightDip, kMaxHandsetHeightDip);
}

const HandsetProfile& NearestHandsetProfileForWidth(
    int width_dip,
    HandsetOrientation orientation) {
  const std::vector<HandsetProfile>& profiles = HandsetProfiles();
  const HandsetProfile* best = &profiles.front();
  int best_distance =
      std::abs(HandsetWidthForOrientation(profiles.front(), orientation) -
               width_dip);
  for (const HandsetProfile& profile : profiles) {
    const int candidate_width =
        HandsetWidthForOrientation(profile, orientation);
    const int distance = std::abs(candidate_width - width_dip);
    if (distance > best_distance) {
      continue;
    }
    // On an exact tie prefer the narrower device explicitly. Relying on
    // catalogue position would be wrong: the catalogue is ordered for the
    // picker, and its widths are not ascending.
    if (distance == best_distance &&
        candidate_width >= HandsetWidthForOrientation(*best, orientation)) {
      continue;
    }
    best = &profile;
    best_distance = distance;
  }
  return *best;
}

HandsetMetrics ResolveHandsetMetrics(const HandsetProfile& profile,
                                     HandsetOrientation orientation,
                                     HandsetSnapMode snap_mode,
                                     int free_width_dip,
                                     int free_height_dip) {
  HandsetMetrics metrics;
  metrics.orientation = orientation;
  metrics.orientation_angle =
      orientation == HandsetOrientation::kPortrait ? 0 : 90;

  if (snap_mode == HandsetSnapMode::kSnapToProfile) {
    metrics.view_width_dip = HandsetWidthForOrientation(profile, orientation);
    metrics.view_height_dip = HandsetHeightForOrientation(profile, orientation);
    metrics.device_scale_factor = profile.device_scale_factor;
  } else {
    // A caller with no window size yet passes zero: the mode is being switched
    // on before anything has been laid out. Clamping that would start a free
    // Handset at the supported minimum, 240x320, which is narrower than every
    // device in the catalogue and is not a phone anyone owns - and because the
    // size is then stored and re-applied on each navigation, it would stay
    // there. The device the user chose is the honest starting point; free mode
    // means they can resize away from it, not that it begins collapsed.
    metrics.view_width_dip =
        free_width_dip > 0
            ? ClampHandsetWidth(free_width_dip)
            : HandsetWidthForOrientation(profile, orientation);
    metrics.view_height_dip =
        free_height_dip > 0
            ? ClampHandsetHeight(free_height_dip)
            : HandsetHeightForOrientation(profile, orientation);
    metrics.device_scale_factor =
        NearestHandsetProfileForWidth(metrics.view_width_dip, orientation)
            .device_scale_factor;
  }

  metrics.device_scale_factor = std::clamp(
      metrics.device_scale_factor, kMinHandsetScaleFactor,
      kMaxHandsetScaleFactor);
  metrics.screen_width_dip = metrics.view_width_dip;
  metrics.screen_height_dip = metrics.view_height_dip;
  return metrics;
}

}  // namespace seoul
