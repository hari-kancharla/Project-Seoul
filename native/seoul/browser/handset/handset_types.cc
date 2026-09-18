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

// Pinned DevTools presets plus an explicit, source-documented overlay. These
// are browser preview configurations. Physical devices may expose different
// viewport metrics with browser controls, screen zoom, or resolution settings.
// Regeneration checks consistency with the pinned input, not market freshness
// or fidelity to the operating system and rendering engine of a real phone.
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
  // Preserve the existing default and persisted identity across catalog rolls.
  const HandsetProfile* profile = FindHandsetProfile("iphone");
  return profile ? *profile : HandsetProfiles().front();
}

}  // namespace seoul
