// Project Seoul product runtime - macOS credential store tests.

#include "seoul/browser/product/browser/keychain_credential_store.h"

#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/test/scoped_command_line.h"
#include "base/types/expected.h"
#include "components/os_crypt/common/keychain_password_mac.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "crypto/apple/keychain_v2.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class RecordingKeychain final : public crypto::apple::KeychainV2 {
 public:
  RecordingKeychain() = default;
  ~RecordingKeychain() override = default;

  OSStatus AddGenericPassword(std::string_view service_name,
                              std::string_view account_name,
                              base::span<const uint8_t> password) override {
    added_service_ = service_name;
    added_account_ = account_name;
    added_password_.assign(password.begin(), password.end());
    return noErr;
  }

  base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
      std::string_view service_name,
      std::string_view account_name) override {
    lookups_.emplace_back(service_name, account_name);
    if (service_name == "Chromium Safe Storage" &&
        account_name == "Chromium") {
      constexpr std::string_view kLegacyPassword = "legacy-password";
      return std::vector<uint8_t>(kLegacyPassword.begin(),
                                  kLegacyPassword.end());
    }
    return base::unexpected(errSecItemNotFound);
  }

  const std::vector<std::pair<std::string, std::string>>& lookups() const {
    return lookups_;
  }
  const std::string& added_service() const { return added_service_; }
  const std::string& added_account() const { return added_account_; }
  size_t added_password_size() const { return added_password_.size(); }

 private:
  std::vector<std::pair<std::string, std::string>> lookups_;
  std::string added_service_;
  std::string added_account_;
  std::vector<uint8_t> added_password_;
};

TEST(KeychainCredentialStoreTest, MockSwitchNeverTouchesPersistentKeychain) {
  base::test::ScopedCommandLine command_line;
  command_line.GetProcessCommandLine()->AppendSwitch(
      os_crypt::switches::kUseMockKeychain);

  KeychainCredentialStore store("development-profile");
  ASSERT_TRUE(store.uses_mock_keychain_for_testing());

  EXPECT_FALSE(store.Get("cloud_reasoning").has_value());
  EXPECT_EQ(store.last_status(),
            KeychainCredentialStore::StoreStatus::kNotFound);
  EXPECT_TRUE(store.Set("cloud_reasoning", "ephemeral-secret"));
  EXPECT_EQ(store.Get("cloud_reasoning"),
            std::optional<std::string>("ephemeral-secret"));
  EXPECT_TRUE(store.Delete("cloud_reasoning"));
  EXPECT_FALSE(store.Get("cloud_reasoning").has_value());
}

TEST(KeychainCredentialStoreTest, SafeStorageNeverProbesChromiumCredential) {
  if (KeychainPassword::GetServiceName() != "Seoul Safe Storage") {
    GTEST_SKIP() << "Isolation applies only to the Seoul-branded build";
  }

  RecordingKeychain keychain;
  KeychainPassword keychain_password(keychain);

  EXPECT_EQ(24U, keychain_password.GetPassword().size());
  ASSERT_EQ(1U, keychain.lookups().size());
  EXPECT_EQ(keychain.lookups()[0],
            std::make_pair(std::string("Seoul Safe Storage"),
                           std::string("Seoul")));
  EXPECT_EQ("Seoul Safe Storage", keychain.added_service());
  EXPECT_EQ("Seoul", keychain.added_account());
  EXPECT_EQ(24U, keychain.added_password_size());
}

}  // namespace
}  // namespace seoul
