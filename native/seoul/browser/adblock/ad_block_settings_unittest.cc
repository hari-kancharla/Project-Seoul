// Project Seoul blocker profile and per-site settings tests.

#include "seoul/browser/adblock/ad_block_settings.h"

#include <memory>

#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/browser/website_settings_info.h"
#include "components/content_settings/core/browser/website_settings_registry.h"
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
