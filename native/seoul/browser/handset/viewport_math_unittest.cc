// Project Seoul Handset.
// Unit tests for the resolved geometry a live Handset is programmed with.

#include "seoul/browser/handset/viewport_math.h"

#include "base/check.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

const HandsetProfile& Phone() {
  const HandsetProfile* profile = FindHandsetProfile("iphone");
  CHECK(profile);
  return *profile;
}

TEST(HandsetViewportMathTest, CatalogueIsInternallyConsistent) {
  EXPECT_FALSE(HandsetProfiles().empty());
  for (const HandsetProfile& profile : HandsetProfiles()) {
    EXPECT_FALSE(profile.id.empty());
    EXPECT_FALSE(profile.label.empty());
    EXPECT_FALSE(profile.platform_version.empty());
    // Portrait means taller than wide. A transposed entry would emulate a
    // device that does not exist and would break landscape in both directions.
    EXPECT_GT(profile.portrait_height_dip, profile.portrait_width_dip)
        << profile.id;
    EXPECT_GE(profile.portrait_width_dip, kMinHandsetWidthDip) << profile.id;
    EXPECT_LE(profile.portrait_height_dip, kMaxHandsetHeightDip) << profile.id;
    EXPECT_GE(profile.device_scale_factor, kMinHandsetScaleFactor)
        << profile.id;
    EXPECT_LE(profile.device_scale_factor, kMaxHandsetScaleFactor)
        << profile.id;
    // Android reports a model client hint; iOS has no model hint to report.
    if (profile.platform == HandsetPlatform::kAndroid) {
      EXPECT_FALSE(profile.model.empty()) << profile.id;
    } else {
      EXPECT_TRUE(profile.model.empty()) << profile.id;
    }
  }
}

TEST(HandsetViewportMathTest, ProfileIdsAreUnique) {
  for (const HandsetProfile& outer : HandsetProfiles()) {
    int matches = 0;
    for (const HandsetProfile& inner : HandsetProfiles()) {
      if (inner.id == outer.id) {
        ++matches;
      }
    }
    EXPECT_EQ(1, matches) << outer.id;
  }
}

TEST(HandsetViewportMathTest, UnknownProfileIdFailsClosed) {
  EXPECT_EQ(nullptr, FindHandsetProfile("no-such-device"));
  EXPECT_EQ(nullptr, FindHandsetProfile(""));
}

TEST(HandsetViewportMathTest, SnapReproducesTheDeviceExactly) {
  const HandsetMetrics metrics = ResolveHandsetMetrics(
      Phone(), HandsetOrientation::kPortrait, HandsetSnapMode::kSnapToProfile,
      /*free_width_dip=*/1200, /*free_height_dip=*/800);

  EXPECT_EQ(Phone().portrait_width_dip, metrics.view_width_dip);
  EXPECT_EQ(Phone().portrait_height_dip, metrics.view_height_dip);
  EXPECT_EQ(Phone().device_scale_factor, metrics.device_scale_factor);
  EXPECT_EQ(0, metrics.orientation_angle);
}

TEST(HandsetViewportMathTest, SnapIgnoresTheFreeWindowSize) {
  const HandsetMetrics wide = ResolveHandsetMetrics(
      Phone(), HandsetOrientation::kPortrait, HandsetSnapMode::kSnapToProfile,
      1200, 800);
  const HandsetMetrics narrow = ResolveHandsetMetrics(
      Phone(), HandsetOrientation::kPortrait, HandsetSnapMode::kSnapToProfile,
      100, 100);
  EXPECT_EQ(wide.view_width_dip, narrow.view_width_dip);
  EXPECT_EQ(wide.view_height_dip, narrow.view_height_dip);
}

TEST(HandsetViewportMathTest, LandscapeTransposesWithoutRescaling) {
  const HandsetMetrics portrait = ResolveHandsetMetrics(
      Phone(), HandsetOrientation::kPortrait, HandsetSnapMode::kSnapToProfile,
      0, 0);
  const HandsetMetrics landscape = ResolveHandsetMetrics(
      Phone(), HandsetOrientation::kLandscape, HandsetSnapMode::kSnapToProfile,
      0, 0);

  EXPECT_EQ(portrait.view_height_dip, landscape.view_width_dip);
  EXPECT_EQ(portrait.view_width_dip, landscape.view_height_dip);
  EXPECT_EQ(portrait.device_scale_factor, landscape.device_scale_factor);
  EXPECT_EQ(90, landscape.orientation_angle);
}

TEST(HandsetViewportMathTest, ScreenAlwaysMatchesTheView) {
  for (const HandsetProfile& profile : HandsetProfiles()) {
    for (const auto orientation :
         {HandsetOrientation::kPortrait, HandsetOrientation::kLandscape}) {
      const HandsetMetrics snapped = ResolveHandsetMetrics(
          profile, orientation, HandsetSnapMode::kSnapToProfile, 0, 0);
      EXPECT_EQ(snapped.view_width_dip, snapped.screen_width_dip) << profile.id;
      EXPECT_EQ(snapped.view_height_dip, snapped.screen_height_dip)
          << profile.id;

      const HandsetMetrics free = ResolveHandsetMetrics(
          profile, orientation, HandsetSnapMode::kFree, 500, 900);
      EXPECT_EQ(free.view_width_dip, free.screen_width_dip) << profile.id;
      EXPECT_EQ(free.view_height_dip, free.screen_height_dip) << profile.id;
    }
  }
}

TEST(HandsetViewportMathTest, FreeResizeFollowsTheWindow) {
  const HandsetMetrics metrics =
      ResolveHandsetMetrics(Phone(), HandsetOrientation::kPortrait,
                            HandsetSnapMode::kFree, 500, 900);
  EXPECT_EQ(500, metrics.view_width_dip);
  EXPECT_EQ(900, metrics.view_height_dip);
}

TEST(HandsetViewportMathTest, FreeResizeClampsToTheSupportedRange) {
  const HandsetMetrics too_small = ResolveHandsetMetrics(
      Phone(), HandsetOrientation::kPortrait, HandsetSnapMode::kFree, 10, 10);
  EXPECT_EQ(kMinHandsetWidthDip, too_small.view_width_dip);
  EXPECT_EQ(kMinHandsetHeightDip, too_small.view_height_dip);

  const HandsetMetrics too_large =
      ResolveHandsetMetrics(Phone(), HandsetOrientation::kPortrait,
                            HandsetSnapMode::kFree, 99999, 99999);
  EXPECT_EQ(kMaxHandsetWidthDip, too_large.view_width_dip);
  EXPECT_EQ(kMaxHandsetHeightDip, too_large.view_height_dip);
}

TEST(HandsetViewportMathTest, FreeResizeInheritsTheNearestScaleFactor) {
  // A window dragged to a tablet width must stop claiming a phone's pixel
  // ratio, or raster sharpness and any DPR-driven asset choice go wrong.
  const HandsetProfile& tablet = *FindHandsetProfile("tablet");
  const HandsetMetrics metrics = ResolveHandsetMetrics(
      Phone(), HandsetOrientation::kPortrait, HandsetSnapMode::kFree,
      tablet.portrait_width_dip, tablet.portrait_height_dip);
  EXPECT_EQ(tablet.device_scale_factor, metrics.device_scale_factor);
}

TEST(HandsetViewportMathTest, NearestProfileResolvesExactAndBetweenWidths) {
  for (const HandsetProfile& profile : HandsetProfiles()) {
    EXPECT_EQ(profile.device_scale_factor,
              NearestHandsetProfileForWidth(profile.portrait_width_dip,
                                            HandsetOrientation::kPortrait)
                  .device_scale_factor)
        << profile.id;
  }
  // Far below every catalogue width resolves to the narrowest profile.
  EXPECT_EQ(360, NearestHandsetProfileForWidth(
                     0, HandsetOrientation::kPortrait)
                     .portrait_width_dip);
  // Far above resolves to the widest.
  EXPECT_EQ(834, NearestHandsetProfileForWidth(
                     99999, HandsetOrientation::kPortrait)
                     .portrait_width_dip);
}

TEST(HandsetViewportMathTest, NearestProfileIsOrientationAware) {
  // A landscape window is as wide as a portrait device is tall. Matching that
  // width against portrait widths would resolve a rotated phone to a tablet
  // and hand it the tablet's pixel ratio.
  const HandsetProfile& phone = Phone();
  const int landscape_width =
      HandsetWidthForOrientation(phone, HandsetOrientation::kLandscape);
  const HandsetProfile& resolved = NearestHandsetProfileForWidth(
      landscape_width, HandsetOrientation::kLandscape);
  EXPECT_EQ(phone.id, resolved.id);
  EXPECT_EQ(phone.device_scale_factor, resolved.device_scale_factor);
}

TEST(HandsetViewportMathTest, FreeResizeKeepsScaleFactorAcrossRotation) {
  // The regression: rotating a free-resized Handset must not silently change
  // its device pixel ratio.
  const HandsetProfile& phone = Phone();
  const HandsetMetrics portrait = ResolveHandsetMetrics(
      phone, HandsetOrientation::kPortrait, HandsetSnapMode::kFree,
      phone.portrait_width_dip, phone.portrait_height_dip);
  const HandsetMetrics landscape = ResolveHandsetMetrics(
      phone, HandsetOrientation::kLandscape, HandsetSnapMode::kFree,
      phone.portrait_height_dip, phone.portrait_width_dip);
  EXPECT_EQ(portrait.device_scale_factor, landscape.device_scale_factor);
}

TEST(HandsetViewportMathTest, FreeModeWithNoSizeYetStartsAtTheDevice) {
  // A caller that has no window size yet - the mode is being switched on
  // before anything has been laid out - passes 0. Clamping that to the
  // supported minimum would start a free Handset at 240x320, which is not a
  // device anyone owns and is narrower than every profile in the catalogue.
  // The honest starting point is the device the user chose; they can resize
  // from there, which is what free mode is for.
  const HandsetProfile& phone = Phone();
  const HandsetMetrics unsized = ResolveHandsetMetrics(
      phone, HandsetOrientation::kPortrait, HandsetSnapMode::kFree, 0, 0);
  EXPECT_EQ(phone.portrait_width_dip, unsized.view_width_dip);
  EXPECT_EQ(phone.portrait_height_dip, unsized.view_height_dip);
  EXPECT_EQ(phone.device_scale_factor, unsized.device_scale_factor);

  // A real size is still honoured, and still clamped.
  const HandsetMetrics sized = ResolveHandsetMetrics(
      phone, HandsetOrientation::kPortrait, HandsetSnapMode::kFree, 500, 900);
  EXPECT_EQ(500, sized.view_width_dip);
  EXPECT_EQ(900, sized.view_height_dip);

  const HandsetMetrics tiny = ResolveHandsetMetrics(
      phone, HandsetOrientation::kPortrait, HandsetSnapMode::kFree, 10, 10);
  EXPECT_EQ(kMinHandsetWidthDip, tiny.view_width_dip);
  EXPECT_EQ(kMinHandsetHeightDip, tiny.view_height_dip);
}

TEST(HandsetViewportMathTest, NearestProfileTieResolvesToTheNarrower) {
  // Midway between two catalogue widths, the narrower must win regardless of
  // where either sits in the picker order.
  const HandsetProfile& compact = *FindHandsetProfile("android-compact");
  const HandsetProfile& standard = *FindHandsetProfile("iphone-compact");
  ASSERT_LT(compact.portrait_width_dip, standard.portrait_width_dip);
  const int gap = standard.portrait_width_dip - compact.portrait_width_dip;
  if (gap % 2 == 0) {
    const int midpoint = compact.portrait_width_dip + gap / 2;
    EXPECT_LE(NearestHandsetProfileForWidth(midpoint,
                                            HandsetOrientation::kPortrait)
                  .portrait_width_dip,
              standard.portrait_width_dip);
  }
}

TEST(HandsetViewportMathTest, CustomSizeClampsTwoValidValuesIndependently) {
  const auto [w, h] = ResolveCustomHandsetSize(u"500", u"900");
  EXPECT_EQ(w, 500);
  EXPECT_EQ(h, 900);
}

TEST(HandsetViewportMathTest, CustomSizeOutOfRangeValuesClampToTheSupportedRange) {
  const auto [w, h] = ResolveCustomHandsetSize(u"20", u"5000");
  EXPECT_EQ(w, kMinHandsetWidthDip);
  EXPECT_EQ(h, kMaxHandsetHeightDip);
}

// The regression: leaving one field blank must pass 0 straight through so a
// caller like EnableHandsetMode falls back to the profile's own dimension for
// that axis, not silently collapse it to the supported minimum. Fixed after
// tracing the original `width == 0 ? 1 : width` pre-clamp step, which turned
// a blank field into 1 and then into the floor - exactly the outcome a "leave
// this alone" signal is supposed to prevent.
TEST(HandsetViewportMathTest, CustomSizeBlankFieldPassesThroughAsZero) {
  const auto [w1, h1] = ResolveCustomHandsetSize(std::u16string(), u"900");
  EXPECT_EQ(w1, 0);
  EXPECT_EQ(h1, 900);

  const auto [w2, h2] = ResolveCustomHandsetSize(u"500", std::u16string());
  EXPECT_EQ(w2, 500);
  EXPECT_EQ(h2, 0);
}

TEST(HandsetViewportMathTest, CustomSizeGarbageTextIsTreatedAsBlank) {
  const auto [w, h] = ResolveCustomHandsetSize(u"not a number", u"-5");
  EXPECT_EQ(w, 0);
  EXPECT_EQ(h, 0);
}

TEST(HandsetViewportMathTest, CustomSizeBothBlankResolvesToZeroZero) {
  const auto [w, h] =
      ResolveCustomHandsetSize(std::u16string(), std::u16string());
  EXPECT_EQ(w, 0);
  EXPECT_EQ(h, 0);
}

}  // namespace
}  // namespace seoul
