// Project Seoul profile and per-site native blocker settings.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SETTINGS_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SETTINGS_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "seoul/browser/adblock/ad_block_request.h"
#include "url/gurl.h"

class HostContentSettingsMap;
class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace seoul::adblock {

inline constexpr char kDefaultAdBlockModePref[] = "seoul.adblock.default_mode";

struct AdBlockSiteSettings {
  AdBlockMode effective_mode = AdBlockMode::kStandard;
  std::optional<AdBlockMode> site_mode;
  bool temporarily_disabled = false;
  base::Time temporary_disable_expiration;
};

// Owns no profile state. All persistent settings are stored through PrefService
// or HostContentSettingsMap.
class AdBlockSettings {
 public:
  AdBlockSettings(PrefService* prefs,
                  HostContentSettingsMap* host_content_settings_map);
  ~AdBlockSettings();

  AdBlockSettings(const AdBlockSettings&) = delete;
  AdBlockSettings& operator=(const AdBlockSettings&) = delete;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  AdBlockMode GetDefaultMode() const;
  void SetDefaultMode(AdBlockMode mode);

  std::optional<AdBlockMode> GetSiteMode(const GURL& site_url) const;
  void SetSiteMode(const GURL& site_url, std::optional<AdBlockMode> mode);

  bool IsTemporarilyDisabled(const GURL& site_url) const;
  base::Time GetTemporaryDisableExpiration(const GURL& site_url) const;
  void TemporarilyDisable(const GURL& site_url, base::TimeDelta duration);
  void ClearTemporaryDisable(const GURL& site_url);

  AdBlockSiteSettings GetSiteSettings(const GURL& site_url) const;

 private:
  static bool IsValidModeValue(int value);
  static bool IsEligibleSite(const GURL& site_url);

  const raw_ptr<PrefService> prefs_;
  const raw_ptr<HostContentSettingsMap> host_content_settings_map_;
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SETTINGS_H_
