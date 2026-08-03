// Project Seoul signed filter-list component tests.

#include "seoul/browser/adblock/ad_block_component_installer.h"

#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/bind.h"
#include "base/version.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::adblock {
namespace {

base::DictValue ValidManifest() {
  base::DictValue manifest;
  manifest.Set("format", "seoul-adblock-filter-set");
  manifest.Set("schema_version", 1);
  manifest.Set("version", "1.2.3");
  return manifest;
}

std::vector<uint8_t> TestPublicKeyHash() {
  std::vector<uint8_t> hash(32);
  for (size_t index = 0; index < hash.size(); ++index) {
    hash[index] = static_cast<uint8_t>(index);
  }
  return hash;
}

TEST(AdBlockComponentInstallerTest, ParsesOnlyExactSha256Hex) {
  constexpr char kHash[] =
      "000102030405060708090a0b0c0d0e0f"
      "101112131415161718191a1b1c1d1e1f";
  EXPECT_EQ(ParseAdBlockComponentPublicKeyHash(kHash), TestPublicKeyHash());
  EXPECT_TRUE(ParseAdBlockComponentPublicKeyHash("").empty());
  EXPECT_TRUE(ParseAdBlockComponentPublicKeyHash("0011").empty());
  EXPECT_TRUE(ParseAdBlockComponentPublicKeyHash(
                  "zz0102030405060708090a0b0c0d0e0f"
                  "101112131415161718191a1b1c1d1e1f")
                  .empty());
}

TEST(AdBlockComponentInstallerTest, RequiresSignedEncryptedBoundedPackage) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(temp_dir.GetPath().AppendASCII("default.txt"),
                              "||ads.example^\n"));
  ASSERT_TRUE(base::WriteFile(temp_dir.GetPath().AppendASCII("additional.txt"),
                              std::string()));

  bool ready_called = false;
  AdBlockFilterComponentInstallerPolicy policy(
      TestPublicKeyHash(),
      base::BindLambdaForTesting(
          [&](const base::Version&, const base::FilePath&) {
            ready_called = true;
          }));

  EXPECT_TRUE(policy.SupportsGroupPolicyEnabledComponentUpdates());
  EXPECT_TRUE(policy.RequiresNetworkEncryption());
  EXPECT_TRUE(policy.VerifyInstallation(ValidManifest(), temp_dir.GetPath()));
  EXPECT_FALSE(policy.GetName().empty());
  EXPECT_EQ(policy.GetInstallerAttributes().at("seoul_adblock_format"), "1");

  std::vector<uint8_t> actual_hash;
  policy.GetHash(&actual_hash);
  EXPECT_EQ(actual_hash, TestPublicKeyHash());

  policy.ComponentReady(base::Version("1.2.3"), temp_dir.GetPath(),
                        ValidManifest());
  EXPECT_TRUE(ready_called);
}

TEST(AdBlockComponentInstallerTest, RejectsIncompleteOrMalformedPackage) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(temp_dir.GetPath().AppendASCII("default.txt"),
                              "||ads.example^\n"));

  AdBlockFilterComponentInstallerPolicy policy(TestPublicKeyHash(),
                                                base::DoNothing());
  EXPECT_FALSE(policy.VerifyInstallation(ValidManifest(), temp_dir.GetPath()));

  ASSERT_TRUE(base::WriteFile(temp_dir.GetPath().AppendASCII("additional.txt"),
                              std::string()));
  base::DictValue malformed = ValidManifest();
  malformed.Set("schema_version", 99);
  EXPECT_FALSE(policy.VerifyInstallation(malformed, temp_dir.GetPath()));

  base::DictValue missing_format = ValidManifest();
  missing_format.Remove("format");
  EXPECT_FALSE(policy.VerifyInstallation(missing_format, temp_dir.GetPath()));
}

TEST(AdBlockComponentInstallerTest, PersistsAndReadsReadyMetadataAsOneRecord) {
  TestingPrefServiceSimple prefs;
  RegisterAdBlockComponentLocalStatePrefs(prefs.registry());

  EXPECT_FALSE(ReadReadyAdBlockFilterComponent(&prefs));
  const base::FilePath install_dir =
      base::FilePath::FromUTF8Unsafe("/component/1.2.3");
  StoreReadyAdBlockFilterComponent(&prefs, install_dir,
                                   base::Version("1.2.3"));
  const std::optional<AdBlockReadyFilterComponent> ready =
      ReadReadyAdBlockFilterComponent(&prefs);
  ASSERT_TRUE(ready);
  EXPECT_EQ(ready->install_dir, install_dir);
  EXPECT_EQ(ready->version, base::Version("1.2.3"));
}

}  // namespace
}  // namespace seoul::adblock
