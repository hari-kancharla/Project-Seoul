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
inline constexpr char kDefaultFingerprintModePref[] =
    "seoul.adblock.default_fingerprint_mode";

// How fingerprinting is answered on a site. Balanced farbles what a
// fingerprinter reads - canvas and WebGL pixels, the hardware profile -
// deterministically per site and session, invisibly on screen. Strict adds
// the canvas taint, so pixel readbacks are refused outright. Off is off.
// Balanced is the profile default: protection is what a person gets without
// configuring anything, and a site can be moved off it individually.
enum class FingerprintMode {
  kOff = 0,
  kBalanced = 1,
  kStrict = 2,
};

struct AdBlockSiteSettings {
  AdBlockMode effective_mode = AdBlockMode::kStandard;
  std::optional<AdBlockMode> site_mode;
  bool temporarily_disabled = false;
  base::Time temporary_disable_expiration;
  // Fingerprinting protection for this site while its shields are up: the
  // site's own choice when it has one, otherwise the profile default. Off
  // for sites the blocker never governs (non-http(s)).
  FingerprintMode fingerprint_mode = FingerprintMode::kOff;
  std::optional<FingerprintMode> site_fingerprint_mode;
  // The strict-mode view of the same state, for callers that only ask
  // whether canvas reads are blocked.
  bool canvas_fingerprint_blocked = false;
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

  FingerprintMode GetDefaultFingerprintMode() const;
  void SetDefaultFingerprintMode(FingerprintMode mode);

  // The site's own override, if it has one; nullopt means "the default".
  std::optional<FingerprintMode> GetSiteFingerprintMode(
      const GURL& site_url) const;
  void SetSiteFingerprintMode(const GURL& site_url,
                              std::optional<FingerprintMode> mode);
  // What applies to the site: its override, else the profile default; Off
  // for a site the blocker never governs.
  FingerprintMode GetFingerprintMode(const GURL& site_url) const;

  // Strict-only entry points kept for callers that predate the modes: true
  // pins the site to Strict, false returns it to the profile default.
  bool GetCanvasFingerprintBlocked(const GURL& site_url) const;
  void SetCanvasFingerprintBlocked(const GURL& site_url, bool blocked);

  bool IsTemporarilyDisabled(const GURL& site_url) const;
  base::Time GetTemporaryDisableExpiration(const GURL& site_url) const;
  void TemporarilyDisable(const GURL& site_url, base::TimeDelta duration);
  void ClearTemporaryDisable(const GURL& site_url);

  AdBlockSiteSettings GetSiteSettings(const GURL& site_url) const;

 private:
  static bool IsValidModeValue(int value);
  static bool IsValidFingerprintModeValue(int value);
  static bool IsEligibleSite(const GURL& site_url);

  const raw_ptr<PrefService> prefs_;
  const raw_ptr<HostContentSettingsMap> host_content_settings_map_;
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SETTINGS_H_
