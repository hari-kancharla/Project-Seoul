// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.
//
// The device catalogue is generated from Chromium's DevTools emulated-device
// list (scripts/generate-handset-profiles.mjs), so its correctness is a
// contract between a script and this binary rather than something a reviewer
// eyeballs. These tests pin that contract: whatever the generator emits after
// the next Chromium roll, every entry must still be a device this mode can
// actually present, the picker's featured tier must still exist, and the ids
// the rest of the product persists must still resolve.

#include <set>
#include <string>

#include "seoul/browser/handset/handset_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

TEST(HandsetCatalogTest, CatalogIsSubstantialAndUnique) {
  const auto& profiles = HandsetProfiles();
  // The generated list carries the whole DevTools catalogue; a sudden shrink
  // to a handful means the generator's parse silently broke, not that phones
  // stopped existing.
  EXPECT_GE(profiles.size(), 30u);

  std::set<std::string> ids;
  std::set<std::string> labels;
  for (const HandsetProfile& profile : profiles) {
    EXPECT_TRUE(ids.insert(profile.id).second) << "duplicate id " << profile.id;
    EXPECT_TRUE(labels.insert(profile.label).second)
        << "duplicate label " << profile.label;
  }
}

TEST(HandsetCatalogTest, EveryEntryIsPresentable) {
  for (const HandsetProfile& profile : HandsetProfiles()) {
    SCOPED_TRACE(profile.id);
    EXPECT_FALSE(profile.id.empty());
    EXPECT_FALSE(profile.label.empty());
    EXPECT_GE(profile.portrait_width_dip, kMinHandsetWidthDip);
    EXPECT_LE(profile.portrait_width_dip, kMaxHandsetWidthDip);
    EXPECT_GE(profile.portrait_height_dip, kMinHandsetHeightDip);
    EXPECT_LE(profile.portrait_height_dip, kMaxHandsetHeightDip);
    EXPECT_GE(profile.device_scale_factor, kMinHandsetScaleFactor);
    EXPECT_LE(profile.device_scale_factor, kMaxHandsetScaleFactor);
    EXPECT_FALSE(profile.platform_version.empty());
    // Portrait means portrait: a generated entry that is wider than tall
    // slipped through with landscape screen metrics.
    EXPECT_LT(profile.portrait_width_dip, profile.portrait_height_dip);
    if (profile.platform == HandsetPlatform::kAndroid) {
      EXPECT_FALSE(profile.model.empty())
          << "Android reports a client-hint model";
    } else {
      EXPECT_TRUE(profile.model.empty()) << "iOS reports no model hint";
    }
  }
}

TEST(HandsetCatalogTest, FeaturedTierExistsAndResolves) {
  const auto& featured = FeaturedHandsetProfileIds();
  // Fewer than a few means the newest-per-family rule broke; more than ten
  // means it stopped being a tier and became the catalogue again.
  EXPECT_GE(featured.size(), 4u);
  EXPECT_LE(featured.size(), 10u);
  for (const std::string& id : featured) {
    EXPECT_NE(FindHandsetProfile(id), nullptr)
        << "featured id must resolve: " << id;
  }
}

TEST(HandsetCatalogTest, PersistedIdsStillResolve) {
  // Ids the product persists in prefs and tests select by name. The overlay
  // keeps these stable across Chromium rolls; losing one silently breaks a
  // user's saved device choice, so losing one fails here first.
  for (const char* id :
       {"iphone", "iphone-compact", "iphone-max", "android", "android-compact",
        "tablet", "tablet-compact"}) {
    EXPECT_NE(FindHandsetProfile(id), nullptr) << id;
  }
  EXPECT_EQ(DefaultHandsetProfile().id, "iphone");
}

TEST(HandsetCatalogTest, CatalogCoversTheMajorFamilies) {
  bool ios_phone = false, android_phone = false, tablet = false;
  for (const HandsetProfile& profile : HandsetProfiles()) {
    if (profile.form_factor == HandsetFormFactor::kTablet) {
      tablet = true;
    } else if (profile.platform == HandsetPlatform::kIOS) {
      ios_phone = true;
    } else {
      android_phone = true;
    }
  }
  EXPECT_TRUE(ios_phone);
  EXPECT_TRUE(android_phone);
  EXPECT_TRUE(tablet);
}

}  // namespace
}  // namespace seoul
