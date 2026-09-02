// Project Seoul profile and per-site native blocker settings.

#include "seoul/browser/adblock/ad_block_settings.h"

#include <algorithm>

#include "base/values.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_constraints.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"

namespace seoul::adblock {
namespace {

constexpr base::TimeDelta kMaximumTemporaryDisable = base::Days(30);
constexpr char kModeKey[] = "mode";
constexpr char kTemporarilyDisabledKey[] = "disabled";
// The v1 strict-only bool, still read so a profile that set it keeps its
// choice; never written again.
constexpr char kLegacyCanvasFingerprintKey[] = "canvas_fingerprint_blocked";
constexpr char kFingerprintModeKey[] = "fingerprint_mode";

std::optional<int> ReadIntegerWebsiteSetting(
    HostContentSettingsMap* settings_map,
    const GURL& site_url,
    ContentSettingsType type) {
  if (!settings_map) {
    return std::nullopt;
  }
  base::Value value = settings_map->GetWebsiteSetting(site_url, site_url, type);
  if (!value.is_dict()) {
    return std::nullopt;
  }
  return value.GetDict().FindInt(kModeKey);
}

}  // namespace

AdBlockSettings::AdBlockSettings(
    PrefService* prefs,
    HostContentSettingsMap* host_content_settings_map)
    : prefs_(prefs), host_content_settings_map_(host_content_settings_map) {}

AdBlockSettings::~AdBlockSettings() = default;

// static
void AdBlockSettings::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(kDefaultAdBlockModePref,
                                static_cast<int>(AdBlockMode::kStandard));
  registry->RegisterIntegerPref(kDefaultFingerprintModePref,
                                static_cast<int>(FingerprintMode::kBalanced));
}

AdBlockMode AdBlockSettings::GetDefaultMode() const {
  if (!prefs_) {
    return AdBlockMode::kStandard;
  }
  const int value = prefs_->GetInteger(kDefaultAdBlockModePref);
  if (!IsValidModeValue(value)) {
    return AdBlockMode::kStandard;
  }
  return static_cast<AdBlockMode>(value);
}

void AdBlockSettings::SetDefaultMode(AdBlockMode mode) {
  if (!prefs_ || !IsValidModeValue(static_cast<int>(mode))) {
    return;
  }
  prefs_->SetInteger(kDefaultAdBlockModePref, static_cast<int>(mode));
}

std::optional<AdBlockMode> AdBlockSettings::GetSiteMode(
    const GURL& site_url) const {
  if (!IsEligibleSite(site_url)) {
    return std::nullopt;
  }
  std::optional<int> value =
      ReadIntegerWebsiteSetting(host_content_settings_map_, site_url,
                                ContentSettingsType::SEOUL_AD_BLOCK_MODE);
  if (!value || !IsValidModeValue(*value)) {
    return std::nullopt;
  }
  return static_cast<AdBlockMode>(*value);
}

void AdBlockSettings::SetSiteMode(const GURL& site_url,
                                  std::optional<AdBlockMode> mode) {
  if (!host_content_settings_map_ || !IsEligibleSite(site_url)) {
    return;
  }
  // The mode shares its dict with the fingerprint mode; writing one must
  // never wipe the other.
  base::Value existing = host_content_settings_map_->GetWebsiteSetting(
      site_url, site_url, ContentSettingsType::SEOUL_AD_BLOCK_MODE);
  base::DictValue dict =
      existing.is_dict() ? std::move(existing.GetDict()) : base::DictValue();
  if (mode && IsValidModeValue(static_cast<int>(*mode))) {
    dict.Set(kModeKey, static_cast<int>(*mode));
  } else {
    dict.Remove(kModeKey);
  }
  host_content_settings_map_->SetWebsiteSettingDefaultScope(
      site_url, site_url, ContentSettingsType::SEOUL_AD_BLOCK_MODE,
      dict.empty() ? base::Value() : base::Value(std::move(dict)));
}

bool AdBlockSettings::IsTemporarilyDisabled(const GURL& site_url) const {
  if (!host_content_settings_map_ || !IsEligibleSite(site_url)) {
    return false;
  }
  base::Value value = host_content_settings_map_->GetWebsiteSetting(
      site_url, site_url, ContentSettingsType::SEOUL_AD_BLOCK_TEMPORARY_ALLOW);
  return value.is_dict() &&
         value.GetDict().FindBool(kTemporarilyDisabledKey).value_or(false);
}

base::Time AdBlockSettings::GetTemporaryDisableExpiration(
    const GURL& site_url) const {
  if (!host_content_settings_map_ || !IsEligibleSite(site_url)) {
    return base::Time();
  }
  content_settings::SettingInfo info;
  base::Value value = host_content_settings_map_->GetWebsiteSetting(
      site_url, site_url, ContentSettingsType::SEOUL_AD_BLOCK_TEMPORARY_ALLOW,
      &info);
  if (!value.is_dict() ||
      !value.GetDict().FindBool(kTemporarilyDisabledKey).value_or(false)) {
    return base::Time();
  }
  return info.metadata.expiration();
}

void AdBlockSettings::TemporarilyDisable(const GURL& site_url,
                                         base::TimeDelta duration) {
  if (!host_content_settings_map_ || !IsEligibleSite(site_url)) {
    return;
  }
  if (!duration.is_positive()) {
    ClearTemporaryDisable(site_url);
    return;
  }

  content_settings::ContentSettingConstraints constraints;
  constraints.set_lifetime(std::min(duration, kMaximumTemporaryDisable));
  host_content_settings_map_->SetWebsiteSettingDefaultScope(
      site_url, site_url, ContentSettingsType::SEOUL_AD_BLOCK_TEMPORARY_ALLOW,
      base::Value(base::DictValue().Set(kTemporarilyDisabledKey, true)),
      constraints);
}

void AdBlockSettings::ClearTemporaryDisable(const GURL& site_url) {
  if (!host_content_settings_map_ || !IsEligibleSite(site_url)) {
    return;
  }
  host_content_settings_map_->SetWebsiteSettingDefaultScope(
      site_url, site_url, ContentSettingsType::SEOUL_AD_BLOCK_TEMPORARY_ALLOW,
      base::Value());
}

FingerprintMode AdBlockSettings::GetDefaultFingerprintMode() const {
  if (!prefs_) {
    return FingerprintMode::kBalanced;
  }
  const int value = prefs_->GetInteger(kDefaultFingerprintModePref);
  if (!IsValidFingerprintModeValue(value)) {
    return FingerprintMode::kBalanced;
  }
  return static_cast<FingerprintMode>(value);
}

void AdBlockSettings::SetDefaultFingerprintMode(FingerprintMode mode) {
  if (!prefs_ || !IsValidFingerprintModeValue(static_cast<int>(mode))) {
    return;
  }
  prefs_->SetInteger(kDefaultFingerprintModePref, static_cast<int>(mode));
}

std::optional<FingerprintMode> AdBlockSettings::GetSiteFingerprintMode(
    const GURL& site_url) const {
  if (!host_content_settings_map_ || !IsEligibleSite(site_url)) {
    return std::nullopt;
  }
  const base::Value value = host_content_settings_map_->GetWebsiteSetting(
      site_url, site_url, ContentSettingsType::SEOUL_AD_BLOCK_MODE);
  if (!value.is_dict()) {
    return std::nullopt;
  }
  const std::optional<int> mode = value.GetDict().FindInt(kFingerprintModeKey);
  if (mode && IsValidFingerprintModeValue(*mode)) {
    return static_cast<FingerprintMode>(*mode);
  }
  // The v1 strict-only bool, written before the modes existed.
  if (value.GetDict().FindBool(kLegacyCanvasFingerprintKey).value_or(false)) {
    return FingerprintMode::kStrict;
  }
  return std::nullopt;
}

void AdBlockSettings::SetSiteFingerprintMode(
    const GURL& site_url,
    std::optional<FingerprintMode> mode) {
  if (!host_content_settings_map_ || !IsEligibleSite(site_url)) {
    return;
  }
  // Shares its dict with the shield mode; writing one must never wipe the
  // other.
  base::Value existing = host_content_settings_map_->GetWebsiteSetting(
      site_url, site_url, ContentSettingsType::SEOUL_AD_BLOCK_MODE);
  base::DictValue dict =
      existing.is_dict() ? std::move(existing.GetDict()) : base::DictValue();
  dict.Remove(kLegacyCanvasFingerprintKey);
  if (mode && IsValidFingerprintModeValue(static_cast<int>(*mode))) {
    dict.Set(kFingerprintModeKey, static_cast<int>(*mode));
  } else {
    dict.Remove(kFingerprintModeKey);
  }
  host_content_settings_map_->SetWebsiteSettingDefaultScope(
      site_url, site_url, ContentSettingsType::SEOUL_AD_BLOCK_MODE,
      dict.empty() ? base::Value() : base::Value(std::move(dict)));
}

FingerprintMode AdBlockSettings::GetFingerprintMode(
    const GURL& site_url) const {
  if (!IsEligibleSite(site_url)) {
    return FingerprintMode::kOff;
  }
  return GetSiteFingerprintMode(site_url).value_or(GetDefaultFingerprintMode());
}

bool AdBlockSettings::GetCanvasFingerprintBlocked(
    const GURL& site_url) const {
  return GetFingerprintMode(site_url) == FingerprintMode::kStrict;
}

void AdBlockSettings::SetCanvasFingerprintBlocked(const GURL& site_url,
                                                  bool blocked) {
  SetSiteFingerprintMode(site_url, blocked ? std::optional<FingerprintMode>(
                                                 FingerprintMode::kStrict)
                                           : std::nullopt);
}

AdBlockSiteSettings AdBlockSettings::GetSiteSettings(
    const GURL& site_url) const {
  AdBlockSiteSettings result;
  if (!IsEligibleSite(site_url)) {
    result.effective_mode = AdBlockMode::kOff;
    return result;
  }
  result.site_mode = GetSiteMode(site_url);
  result.temporarily_disabled = IsTemporarilyDisabled(site_url);
  result.temporary_disable_expiration = GetTemporaryDisableExpiration(site_url);
  result.effective_mode = result.temporarily_disabled
                              ? AdBlockMode::kOff
                              : result.site_mode.value_or(GetDefaultMode());
  result.site_fingerprint_mode = GetSiteFingerprintMode(site_url);
  result.fingerprint_mode =
      result.site_fingerprint_mode.value_or(GetDefaultFingerprintMode());
  result.canvas_fingerprint_blocked =
      result.fingerprint_mode == FingerprintMode::kStrict;
  return result;
}

// static
bool AdBlockSettings::IsValidModeValue(int value) {
  return value >= static_cast<int>(AdBlockMode::kOff) &&
         value <= static_cast<int>(AdBlockMode::kAggressive);
}

// static
bool AdBlockSettings::IsEligibleSite(const GURL& site_url) {
  return site_url.is_valid() && site_url.SchemeIsHTTPOrHTTPS();
}

}  // namespace seoul::adblock
