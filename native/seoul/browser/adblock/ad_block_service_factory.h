// Project Seoul profile-keyed native blocker factory.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SERVICE_FACTORY_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SERVICE_FACTORY_H_

#include <memory>

#include "base/files/file_path.h"
#include "base/no_destructor.h"
#include "base/version.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class KeyedService;
class Profile;

namespace content {
class BrowserContext;
}  // namespace content

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace seoul::adblock {

class AdBlockService;

class AdBlockServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static AdBlockService* GetForProfile(Profile* profile);
  static AdBlockServiceFactory* GetInstance();
  static void ActivateVerifiedFilterComponentForLoadedProfiles(
      const base::FilePath& install_dir,
      const base::Version& version);

  AdBlockServiceFactory(const AdBlockServiceFactory&) = delete;
  AdBlockServiceFactory& operator=(const AdBlockServiceFactory&) = delete;

 private:
  friend base::NoDestructor<AdBlockServiceFactory>;

  AdBlockServiceFactory();
  ~AdBlockServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_SERVICE_FACTORY_H_
