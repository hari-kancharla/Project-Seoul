// Project Seoul signed filter-list component registration.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_COMPONENT_INSTALLER_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_COMPONENT_INSTALLER_H_

#include <optional>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/values.h"
#include "base/version.h"
#include "components/component_updater/component_installer.h"

class PrefRegistrySimple;
class PrefService;

namespace component_updater {
class ComponentUpdateService;
}  // namespace component_updater

namespace seoul::adblock {

struct AdBlockReadyFilterComponent {
  base::FilePath install_dir;
  base::Version version;
};

// Chromium verifies the outer CRX signature against `public_key_hash`. This
// policy then performs a cheap structural check before the profile-owned list
// manager validates the inner manifest, hashes, UTF-8 payloads, parse result,
// and replacement engine off the UI thread.
class AdBlockFilterComponentInstallerPolicy final
    : public component_updater::ComponentInstallerPolicy {
 public:
  using ReadyCallback =
      base::RepeatingCallback<void(const base::Version&,
                                   const base::FilePath&)>;

  AdBlockFilterComponentInstallerPolicy(std::vector<uint8_t> public_key_hash,
                                        ReadyCallback ready_callback);
  ~AdBlockFilterComponentInstallerPolicy() override;

  AdBlockFilterComponentInstallerPolicy(
      const AdBlockFilterComponentInstallerPolicy&) = delete;
  AdBlockFilterComponentInstallerPolicy& operator=(
      const AdBlockFilterComponentInstallerPolicy&) = delete;

  // ComponentInstallerPolicy:
  bool SupportsGroupPolicyEnabledComponentUpdates() const override;
  bool VerifyInstallation(const base::DictValue& manifest,
                          const base::FilePath& install_dir) const override;
  bool RequiresNetworkEncryption() const override;
  update_client::CrxInstaller::Result OnCustomInstall(
      const base::DictValue& manifest,
      const base::FilePath& install_dir) override;
  void OnCustomUninstall() override;
  void ComponentReady(const base::Version& version,
                      const base::FilePath& install_dir,
                      base::DictValue manifest) override;
  base::FilePath GetRelativeInstallDir() const override;
  void GetHash(std::vector<uint8_t>* hash) const override;
  std::string GetName() const override;
  update_client::InstallerAttributes GetInstallerAttributes() const override;

 private:
  const std::vector<uint8_t> public_key_hash_;
  ReadyCallback ready_callback_;
};

void RegisterAdBlockComponentLocalStatePrefs(PrefRegistrySimple* registry);
void StoreReadyAdBlockFilterComponent(PrefService* local_state,
                                      const base::FilePath& install_dir,
                                      const base::Version& version);
std::optional<AdBlockReadyFilterComponent> ReadReadyAdBlockFilterComponent(
    const PrefService* local_state);

// Returns an empty vector when the release build has no owner-supplied public
// key hash. An unconfigured build never registers a permissive or placeholder
// component identity.
std::vector<uint8_t> ParseAdBlockComponentPublicKeyHash(
    std::string_view hexadecimal_hash);
bool IsAdBlockFilterComponentConfigured();
void RegisterAdBlockFilterComponent(
    component_updater::ComponentUpdateService* component_update_service);

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_COMPONENT_INSTALLER_H_
