#include "seoul/browser/handset/handset_types.h"

#include <utility>

#include "base/no_destructor.h"

namespace seoul {

HandsetProfile::HandsetProfile() = default;

HandsetProfile::HandsetProfile(std::string id,
                               std::string label,
                               HandsetPlatform platform,
                               HandsetFormFactor form_factor,
                               int portrait_width_dip,
                               int portrait_height_dip,
                               float device_scale_factor,
                               std::string platform_version,
                               std::string model)
    : id(std::move(id)),
      label(std::move(label)),
      platform(platform),
      form_factor(form_factor),
      portrait_width_dip(portrait_width_dip),
      portrait_height_dip(portrait_height_dip),
      device_scale_factor(device_scale_factor),
      platform_version(std::move(platform_version)),
      model(std::move(model)) {}

HandsetProfile::HandsetProfile(const HandsetProfile&) = default;
HandsetProfile& HandsetProfile::operator=(const HandsetProfile&) = default;
HandsetProfile::~HandsetProfile() = default;

namespace {

// CSS-pixel portrait sizes and device pixel ratios for the emulated classes.
// These are the layout viewport sizes the real devices report, not their
// physical panel sizes; a site's breakpoints see exactly what they would see
// on the device. Widths drive the whole product experience, so a wrong number
// here is a visible bug rather than a cosmetic one.
//
// The list spans the breakpoints that actually differ in the wild: a small
// phone, a standard phone, a large phone, the two dominant Android widths, and
// two tablet widths. It is deliberately short. Every extra near-duplicate
// costs picker clarity and buys no additional layout coverage.
const std::vector<HandsetProfile>& BuildProfiles() {
  static const base::NoDestructor<const std::vector<HandsetProfile>> profiles(
      std::vector<HandsetProfile>{
          {"iphone-compact", "iPhone SE", HandsetPlatform::kIOS,
           HandsetFormFactor::kPhone, 375, 667, 2.0f, "17_6_1", ""},
          {"iphone", "iPhone 15", HandsetPlatform::kIOS,
           HandsetFormFactor::kPhone, 393, 852, 3.0f, "18_0", ""},
          {"iphone-max", "iPhone 15 Pro Max", HandsetPlatform::kIOS,
           HandsetFormFactor::kPhone, 430, 932, 3.0f, "18_0", ""},
          {"android-compact", "Galaxy S23", HandsetPlatform::kAndroid,
           HandsetFormFactor::kPhone, 360, 780, 3.0f, "15", "SM-S911B"},
          {"android", "Pixel 8", HandsetPlatform::kAndroid,
           HandsetFormFactor::kPhone, 412, 915, 2.625f, "15", "Pixel 8"},
          {"tablet-compact", "iPad mini", HandsetPlatform::kIOS,
           HandsetFormFactor::kTablet, 744, 1133, 2.0f, "18_0", ""},
          {"tablet", "iPad Pro 11\"", HandsetPlatform::kIOS,
           HandsetFormFactor::kTablet, 834, 1194, 2.0f, "18_0", ""},
      });
  return *profiles;
}

}  // namespace

const std::vector<HandsetProfile>& HandsetProfiles() {
  return BuildProfiles();
}

const HandsetProfile* FindHandsetProfile(const std::string& id) {
  for (const HandsetProfile& profile : HandsetProfiles()) {
    if (profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

const HandsetProfile& DefaultHandsetProfile() {
  // The standard-phone width. Anything narrower under-reports what most
  // visitors see; anything wider hides the layout the site actually ships to
  // phones.
  const HandsetProfile* profile = FindHandsetProfile("iphone");
  return profile ? *profile : HandsetProfiles().front();
}

}  // namespace seoul
