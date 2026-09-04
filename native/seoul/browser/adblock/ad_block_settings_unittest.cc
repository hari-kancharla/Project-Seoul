// Project Seoul blocker profile and per-site settings tests.

#include "seoul/browser/adblock/ad_block_settings.h"

#include <memory>

#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/browser/website_settings_info.h"
#include "components/content_settings/core/browser/website_settings_registry.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/prefs/pref_registry.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::adblock {
namespace {

class AdBlockSettingsTest : public testing::Test {
 public:
  AdBlockSettingsTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  void SetUp() override {
    AdBlockSettings::RegisterProfilePrefs(prefs_.registry());
    HostContentSettingsMap::RegisterProfilePrefs(prefs_.registry());
    settings_map_ = base::MakeRefCounted<HostContentSettingsMap>(
        &prefs_, /*is_off_the_record=*/false,
        /*store_last_modified=*/false, /*restore_session=*/false,
        /*should_record_metrics=*/false);
    settings_ = std::make_unique<AdBlockSettings>(&prefs_, settings_map_.get());
  }

  void TearDown() override {
    settings_.reset();
    settings_map_->ShutdownOnUIThread();
    settings_map_.reset();
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  sync_preferences::TestingPrefServiceSyncable prefs_;
  scoped_refptr<HostContentSettingsMap> settings_map_;
  std::unique_ptr<AdBlockSettings> settings_;
};

TEST_F(AdBlockSettingsTest, DefaultAndPerSiteModesRemainIndependent) {
  const GURL configured_site("https://news.example/article");
  const GURL other_site("https://other.example/");

  EXPECT_EQ(AdBlockMode::kStandard, settings_->GetDefaultMode());
  EXPECT_EQ(AdBlockMode::kStandard,
            settings_->GetSiteSettings(configured_site).effective_mode);

  settings_->SetDefaultMode(AdBlockMode::kAggressive);
  EXPECT_EQ(AdBlockMode::kAggressive,
            settings_->GetSiteSettings(configured_site).effective_mode);

  settings_->SetSiteMode(configured_site, AdBlockMode::kOff);
  EXPECT_EQ(AdBlockMode::kOff,
            settings_->GetSiteSettings(configured_site).effective_mode);
  EXPECT_EQ(AdBlockMode::kAggressive,
            settings_->GetSiteSettings(other_site).effective_mode);

  settings_->SetSiteMode(configured_site, std::nullopt);
  EXPECT_FALSE(settings_->GetSiteMode(configured_site));
  EXPECT_EQ(AdBlockMode::kAggressive,
            settings_->GetSiteSettings(configured_site).effective_mode);
}

TEST_F(AdBlockSettingsTest, SiteOverridesStayLocalToTheProfile) {
  const content_settings::WebsiteSettingsInfo* mode_info =
      content_settings::WebsiteSettingsRegistry::GetInstance()->Get(
          ContentSettingsType::SEOUL_AD_BLOCK_MODE);
  const content_settings::WebsiteSettingsInfo* temporary_allow_info =
      content_settings::WebsiteSettingsRegistry::GetInstance()->Get(
          ContentSettingsType::SEOUL_AD_BLOCK_TEMPORARY_ALLOW);

  ASSERT_TRUE(mode_info);
  ASSERT_TRUE(temporary_allow_info);
  EXPECT_EQ(PrefRegistry::NO_REGISTRATION_FLAGS,
            mode_info->GetPrefRegistrationFlags());
  EXPECT_EQ(PrefRegistry::NO_REGISTRATION_FLAGS,
            temporary_allow_info->GetPrefRegistrationFlags());
}

TEST_F(AdBlockSettingsTest, TemporaryDisableExpiresAndRestoresSiteMode) {
  const GURL site("https://news.example/");
  settings_->SetSiteMode(site, AdBlockMode::kAggressive);
  settings_->TemporarilyDisable(site, base::Minutes(15));

  AdBlockSiteSettings disabled = settings_->GetSiteSettings(site);
  EXPECT_TRUE(disabled.temporarily_disabled);
  EXPECT_EQ(AdBlockMode::kOff, disabled.effective_mode);
  EXPECT_FALSE(disabled.temporary_disable_expiration.is_null());

  task_environment_.FastForwardBy(base::Minutes(16));
  AdBlockSiteSettings restored = settings_->GetSiteSettings(site);
  EXPECT_FALSE(restored.temporarily_disabled);
  EXPECT_TRUE(restored.temporary_disable_expiration.is_null());
  EXPECT_EQ(AdBlockMode::kAggressive, restored.effective_mode);
}

TEST_F(AdBlockSettingsTest, ClearTemporaryDisableRestoresImmediately) {
  const GURL site("https://news.example/");
  settings_->SetSiteMode(site, AdBlockMode::kStandard);
  settings_->TemporarilyDisable(site, base::Hours(1));
  ASSERT_TRUE(settings_->IsTemporarilyDisabled(site));

  settings_->ClearTemporaryDisable(site);
  EXPECT_FALSE(settings_->IsTemporarilyDisabled(site));
  EXPECT_EQ(AdBlockMode::kStandard,
            settings_->GetSiteSettings(site).effective_mode);
}

// Fingerprinting protection is the profile default, not an opt-in: a fresh
// profile answers Balanced for any site it governs, and only http(s) sites
// are governed at all.
TEST_F(AdBlockSettingsTest, FingerprintProtectionIsBalancedByDefault) {
  const GURL site("https://news.example/article");
  EXPECT_EQ(FingerprintMode::kBalanced, settings_->GetDefaultFingerprintMode());
  EXPECT_EQ(FingerprintMode::kBalanced, settings_->GetFingerprintMode(site));
  EXPECT_FALSE(settings_->GetSiteFingerprintMode(site).has_value());
  const AdBlockSiteSettings composed = settings_->GetSiteSettings(site);
  EXPECT_EQ(FingerprintMode::kBalanced, composed.fingerprint_mode);
  EXPECT_FALSE(composed.site_fingerprint_mode.has_value());
  EXPECT_FALSE(composed.canvas_fingerprint_blocked);

  // A site the blocker never governs gets nothing, whatever the default.
  EXPECT_EQ(FingerprintMode::kOff,
            settings_->GetFingerprintMode(GURL("chrome://settings/")));
  EXPECT_EQ(FingerprintMode::kOff,
            settings_->GetSiteSettings(GURL("file:///tmp/page.html"))
                .fingerprint_mode);
}

// The default and a site's override stay independent: changing the default
// moves every site without an override and no site with one, and clearing
// the override returns the site to whatever the default is then.
TEST_F(AdBlockSettingsTest,
       FingerprintDefaultAndSiteOverrideRemainIndependent) {
  const GURL configured("https://news.example/article");
  const GURL other("https://other.example/");

  settings_->SetSiteFingerprintMode(configured, FingerprintMode::kStrict);
  EXPECT_EQ(FingerprintMode::kStrict,
            settings_->GetFingerprintMode(configured));
  EXPECT_EQ(FingerprintMode::kBalanced, settings_->GetFingerprintMode(other));

  settings_->SetDefaultFingerprintMode(FingerprintMode::kOff);
  EXPECT_EQ(FingerprintMode::kOff, settings_->GetDefaultFingerprintMode());
  EXPECT_EQ(FingerprintMode::kStrict,
            settings_->GetFingerprintMode(configured))
      << "an override outranks the default";
  EXPECT_EQ(FingerprintMode::kOff, settings_->GetFingerprintMode(other));

  settings_->SetSiteFingerprintMode(configured, std::nullopt);
  EXPECT_FALSE(settings_->GetSiteFingerprintMode(configured).has_value());
  EXPECT_EQ(FingerprintMode::kOff, settings_->GetFingerprintMode(configured));

  // An explicit Off override survives a default that moves back up.
  settings_->SetSiteFingerprintMode(other, FingerprintMode::kOff);
  settings_->SetDefaultFingerprintMode(FingerprintMode::kBalanced);
  EXPECT_EQ(FingerprintMode::kOff, settings_->GetFingerprintMode(other));
  EXPECT_EQ(FingerprintMode::kBalanced,
            settings_->GetFingerprintMode(configured));

  // Out-of-range values never land.
  settings_->SetDefaultFingerprintMode(static_cast<FingerprintMode>(7));
  EXPECT_EQ(FingerprintMode::kBalanced, settings_->GetDefaultFingerprintMode());
}

// The fingerprint override shares its per-site dict with the shield mode;
// each writer must leave the other's key standing.
TEST_F(AdBlockSettingsTest, FingerprintModeAndShieldModeCoexist) {
  const GURL site("https://news.example/article");

  settings_->SetSiteFingerprintMode(site, FingerprintMode::kOff);
  settings_->SetSiteMode(site, AdBlockMode::kAggressive);
  EXPECT_EQ(FingerprintMode::kOff, settings_->GetFingerprintMode(site))
      << "writing the shield mode must not wipe the fingerprint override";
  EXPECT_EQ(AdBlockMode::kAggressive, *settings_->GetSiteMode(site));

  settings_->SetSiteFingerprintMode(site, FingerprintMode::kStrict);
  EXPECT_EQ(AdBlockMode::kAggressive, *settings_->GetSiteMode(site))
      << "writing the fingerprint override must not wipe the shield mode";
  EXPECT_TRUE(settings_->GetCanvasFingerprintBlocked(site))
      << "the strict-only view reflects the mode";

  settings_->SetSiteFingerprintMode(site, std::nullopt);
  settings_->SetSiteMode(site, std::nullopt);
  EXPECT_FALSE(settings_->GetSiteFingerprintMode(site).has_value());
  EXPECT_FALSE(settings_->GetSiteMode(site).has_value());
}

// Callers that predate the modes keep working: the strict-only setter pins
// Strict and clears back to the default, and the v1 bool a profile may still
// hold reads as a Strict override until the next write retires it.
TEST_F(AdBlockSettingsTest, LegacyStrictOnlyEntryPointsMapOntoTheModes) {
  const GURL site("https://news.example/article");

  settings_->SetCanvasFingerprintBlocked(site, true);
  EXPECT_EQ(FingerprintMode::kStrict, *settings_->GetSiteFingerprintMode(site));
  const AdBlockSiteSettings composed = settings_->GetSiteSettings(site);
  EXPECT_EQ(FingerprintMode::kStrict, composed.fingerprint_mode);
  EXPECT_TRUE(composed.canvas_fingerprint_blocked);

  settings_->SetCanvasFingerprintBlocked(site, false);
  EXPECT_FALSE(settings_->GetSiteFingerprintMode(site).has_value());
  EXPECT_EQ(FingerprintMode::kBalanced, settings_->GetFingerprintMode(site));
  EXPECT_FALSE(settings_->GetCanvasFingerprintBlocked(site));

  // A v1 profile wrote only the bool.
  settings_map_->SetWebsiteSettingDefaultScope(
      site, site, ContentSettingsType::SEOUL_AD_BLOCK_MODE,
      base::Value(
          base::DictValue().Set("canvas_fingerprint_blocked", true)));
  EXPECT_EQ(FingerprintMode::kStrict, *settings_->GetSiteFingerprintMode(site));

  // Any write through the modes retires the bool.
  settings_->SetSiteFingerprintMode(site, FingerprintMode::kBalanced);
  const base::Value stored = settings_map_->GetWebsiteSetting(
      site, site, ContentSettingsType::SEOUL_AD_BLOCK_MODE);
  ASSERT_TRUE(stored.is_dict());
  EXPECT_FALSE(stored.GetDict().contains("canvas_fingerprint_blocked"));
  EXPECT_EQ(static_cast<int>(FingerprintMode::kBalanced),
            *stored.GetDict().FindInt("fingerprint_mode"));
}

TEST_F(AdBlockSettingsTest, InternalSchemesNeverReceiveSiteOverrides) {
  const GURL internal_url("chrome://settings/");
  settings_->SetSiteMode(internal_url, AdBlockMode::kOff);
  settings_->TemporarilyDisable(internal_url, base::Hours(1));

  EXPECT_FALSE(settings_->GetSiteMode(internal_url));
  EXPECT_FALSE(settings_->IsTemporarilyDisabled(internal_url));
  EXPECT_EQ(AdBlockMode::kOff,
            settings_->GetSiteSettings(internal_url).effective_mode);
}

}  // namespace
}  // namespace seoul::adblock
