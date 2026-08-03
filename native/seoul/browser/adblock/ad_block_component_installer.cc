// Project Seoul signed filter-list component registration.

#include "seoul/browser/adblock/ad_block_component_installer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/browser_process.h"
#include "components/component_updater/component_updater_service.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"

#ifndef SEOUL_ADBLOCK_COMPONENT_PUBLIC_KEY_HASH_HEX
#define SEOUL_ADBLOCK_COMPONENT_PUBLIC_KEY_HASH_HEX ""
#endif

namespace seoul::adblock {
namespace {

constexpr char kComponentName[] = "Seoul Ad Block Filter Lists";
constexpr char kComponentDirectory[] = "SeoulAdBlockFilterLists";
constexpr char kReadyComponentPref[] = "seoul.adblock.component.ready";
constexpr char kReadyPathKey[] = "install_dir";
constexpr char kReadyVersionKey[] = "version";
constexpr char kPackageFormat[] = "seoul-adblock-filter-set";
constexpr char kDefaultRulesFile[] = "default.txt";
constexpr char kAdditionalRulesFile[] = "additional.txt";
constexpr int kPackageSchemaVersion = 1;
constexpr int64_t kMaximumRuleFileBytes = 32 * 1024 * 1024;
constexpr int64_t kMaximumCombinedRuleBytes = 48 * 1024 * 1024;

bool IsBoundedRegularFile(const base::FilePath& path,
                          int64_t maximum_size,
                          int64_t* size_out) {
  const std::optional<int64_t> size = base::GetFileSize(path);
  if (!size || *size < 0 || *size > maximum_size) {
    return false;
  }
  *size_out = *size;
  return true;
}

void OnFilterComponentReady(const base::Version& version,
                            const base::FilePath& install_dir) {
  CHECK(version.IsValid());
  CHECK(!install_dir.empty());

  PrefService* const local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (local_state) {
    StoreReadyAdBlockFilterComponent(local_state, install_dir, version);
  }
  AdBlockServiceFactory::ActivateVerifiedFilterComponentForLoadedProfiles(
      install_dir, version);
}

std::vector<uint8_t> ConfiguredPublicKeyHash() {
  return ParseAdBlockComponentPublicKeyHash(
      SEOUL_ADBLOCK_COMPONENT_PUBLIC_KEY_HASH_HEX);
}

}  // namespace

AdBlockFilterComponentInstallerPolicy::
    AdBlockFilterComponentInstallerPolicy(
        std::vector<uint8_t> public_key_hash,
        ReadyCallback ready_callback)
    : public_key_hash_(std::move(public_key_hash)),
      ready_callback_(std::move(ready_callback)) {
  CHECK_EQ(public_key_hash_.size(), 32u);
  CHECK(ready_callback_);
}

AdBlockFilterComponentInstallerPolicy::
    ~AdBlockFilterComponentInstallerPolicy() = default;

bool AdBlockFilterComponentInstallerPolicy::
    SupportsGroupPolicyEnabledComponentUpdates() const {
  return true;
}

bool AdBlockFilterComponentInstallerPolicy::VerifyInstallation(
    const base::DictValue& manifest,
    const base::FilePath& install_dir) const {
  const std::string* format = manifest.FindString("format");
  const std::optional<int> schema_version =
      manifest.FindInt("schema_version");
  const std::string* manifest_version = manifest.FindString("version");
  if (!format || *format != kPackageFormat || !schema_version ||
      *schema_version != kPackageSchemaVersion || !manifest_version ||
      !base::Version(*manifest_version).IsValid()) {
    return false;
  }

  int64_t default_size = 0;
  int64_t additional_size = 0;
  if (!IsBoundedRegularFile(install_dir.AppendASCII(kDefaultRulesFile),
                            kMaximumRuleFileBytes, &default_size) ||
      !IsBoundedRegularFile(install_dir.AppendASCII(kAdditionalRulesFile),
                            kMaximumRuleFileBytes, &additional_size)) {
    return false;
  }
  return default_size > 0 &&
         default_size + additional_size <= kMaximumCombinedRuleBytes;
}

bool AdBlockFilterComponentInstallerPolicy::RequiresNetworkEncryption() const {
  return true;
}

update_client::CrxInstaller::Result
AdBlockFilterComponentInstallerPolicy::OnCustomInstall(
    const base::DictValue& manifest,
    const base::FilePath& install_dir) {
  return update_client::CrxInstaller::Result(
      update_client::InstallError::NONE);
}

void AdBlockFilterComponentInstallerPolicy::OnCustomUninstall() {}

void AdBlockFilterComponentInstallerPolicy::ComponentReady(
    const base::Version& version,
    const base::FilePath& install_dir,
    base::DictValue manifest) {
  if (!version.IsValid() || install_dir.empty()) {
    LOG(ERROR) << "Rejected invalid Seoul filter component metadata";
    return;
  }
  ready_callback_.Run(version, install_dir);
}

base::FilePath
AdBlockFilterComponentInstallerPolicy::GetRelativeInstallDir() const {
  return base::FilePath::FromASCII(kComponentDirectory);
}

void AdBlockFilterComponentInstallerPolicy::GetHash(
    std::vector<uint8_t>* hash) const {
  *hash = public_key_hash_;
}

std::string AdBlockFilterComponentInstallerPolicy::GetName() const {
  return kComponentName;
}

update_client::InstallerAttributes
AdBlockFilterComponentInstallerPolicy::GetInstallerAttributes() const {
  return {{"seoul_adblock_format", "1"}};
}

void RegisterAdBlockComponentLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kReadyComponentPref);
}

void StoreReadyAdBlockFilterComponent(PrefService* local_state,
                                      const base::FilePath& install_dir,
                                      const base::Version& version) {
  CHECK(local_state);
  CHECK(!install_dir.empty());
  CHECK(version.IsValid());
  base::DictValue ready;
  ready.Set(kReadyPathKey, install_dir.AsUTF8Unsafe());
  ready.Set(kReadyVersionKey, version.GetString());
  local_state->SetDict(kReadyComponentPref, std::move(ready));
}

std::optional<AdBlockReadyFilterComponent> ReadReadyAdBlockFilterComponent(
    const PrefService* local_state) {
  if (!local_state) {
    return std::nullopt;
  }
  const base::DictValue& ready = local_state->GetDict(kReadyComponentPref);
  const std::string* path = ready.FindString(kReadyPathKey);
  const std::string* version_string = ready.FindString(kReadyVersionKey);
  if (!path || path->empty() || !version_string) {
    return std::nullopt;
  }
  base::Version version(*version_string);
  if (!version.IsValid()) {
    return std::nullopt;
  }
  return AdBlockReadyFilterComponent{
      .install_dir = base::FilePath::FromUTF8Unsafe(*path),
      .version = std::move(version),
  };
}

std::vector<uint8_t> ParseAdBlockComponentPublicKeyHash(
    std::string_view hexadecimal_hash) {
  if (hexadecimal_hash.size() != 64) {
    return {};
  }
  std::vector<uint8_t> hash;
  if (!base::HexStringToBytes(hexadecimal_hash, &hash) || hash.size() != 32u) {
    return {};
  }
  return hash;
}

bool IsAdBlockFilterComponentConfigured() {
  return !ConfiguredPublicKeyHash().empty();
}

void RegisterAdBlockFilterComponent(
    component_updater::ComponentUpdateService* component_update_service) {
  CHECK(component_update_service);
  std::vector<uint8_t> public_key_hash = ConfiguredPublicKeyHash();
  if (public_key_hash.empty()) {
    VLOG(1) << "Seoul ad-block filter component is not release-configured";
    return;
  }
  auto installer =
      base::MakeRefCounted<component_updater::ComponentInstaller>(
          std::make_unique<AdBlockFilterComponentInstallerPolicy>(
              std::move(public_key_hash),
              base::BindRepeating(&OnFilterComponentReady)));
  installer->Register(component_update_service, base::DoNothing());
}

}  // namespace seoul::adblock
