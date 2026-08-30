#include "seoul/browser/handset/handset_types.h"

#include <utility>

#include "seoul/browser/handset/handset_profiles_generated.h"

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

// The device catalogue is generated, not curated: Chromium's own DevTools
// emulated-device list (which Google keeps current with real phones) merged
// with a small overlay for devices newer than the pinned checkout. See
// scripts/generate-handset-profiles.mjs; check:handset-profiles fails CI when
// the generated list is stale for the checkout, so a Chromium roll that adds
// a phone adds it here too. CSS-pixel portrait sizes and device pixel ratios
// are the layout viewport the real device reports - a site's breakpoints see
// exactly what they would see on the device.
const std::vector<HandsetProfile>& HandsetProfiles() {
  return GeneratedHandsetProfiles();
}

const std::vector<std::string>& FeaturedHandsetProfileIds() {
  return GeneratedFeaturedHandsetProfileIds();
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
