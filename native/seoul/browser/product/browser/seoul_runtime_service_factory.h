// Project Seoul product runtime - the profile factory.
// Constructs exactly one SeoulRuntimeService per eligible regular profile and
// declares its dependency on the organization service so ordering and
// teardown are correct. Excludes off-the-record, guest, system, and internal
// profiles.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_RUNTIME_SERVICE_FACTORY_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_RUNTIME_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class KeyedService;
class Profile;

namespace content {
class BrowserContext;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace seoul {

class SeoulRuntimeService;

class SeoulRuntimeServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static SeoulRuntimeService* GetForProfile(Profile* profile);
  static SeoulRuntimeService* GetForProfileIfExists(Profile* profile);
  static SeoulRuntimeServiceFactory* GetInstance();

  SeoulRuntimeServiceFactory(const SeoulRuntimeServiceFactory&) = delete;
  SeoulRuntimeServiceFactory& operator=(const SeoulRuntimeServiceFactory&) =
      delete;

 private:
  friend base::NoDestructor<SeoulRuntimeServiceFactory>;

  SeoulRuntimeServiceFactory();
  ~SeoulRuntimeServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_SEOUL_RUNTIME_SERVICE_FACTORY_H_
