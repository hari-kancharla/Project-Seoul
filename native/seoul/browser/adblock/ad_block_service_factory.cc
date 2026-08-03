// Project Seoul profile-keyed native blocker factory.

#include "seoul/browser/adblock/ad_block_service_factory.h"

#include <memory>

#include "base/functional/callback_helpers.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/browser_context.h"
#include "seoul/browser/adblock/ad_block_component_installer.h"
#include "seoul/browser/adblock/ad_block_service.h"

namespace seoul::adblock {

// static
AdBlockService* AdBlockServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<AdBlockService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
AdBlockServiceFactory* AdBlockServiceFactory::GetInstance() {
  static base::NoDestructor<AdBlockServiceFactory> instance;
  return instance.get();
}

// static
void AdBlockServiceFactory::ActivateVerifiedFilterComponentForLoadedProfiles(
    const base::FilePath& install_dir,
    const base::Version& version) {
  if (install_dir.empty() || !version.IsValid() || !g_browser_process ||
      !g_browser_process->profile_manager()) {
    return;
  }
  AdBlockServiceFactory* const factory = GetInstance();
  for (Profile* profile :
       g_browser_process->profile_manager()->GetLoadedProfiles()) {
    auto* const service = static_cast<AdBlockService*>(
        factory->GetServiceForBrowserContext(profile, /*create=*/false));
    if (service) {
      service->ActivateVerifiedFilterComponent(install_dir, version,
                                                base::DoNothing());
    }
  }
}

AdBlockServiceFactory::AdBlockServiceFactory()
    : ProfileKeyedServiceFactory(
          "SeoulAdBlockService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .WithAshInternals(ProfileSelection::kNone)
              .Build()) {}

AdBlockServiceFactory::~AdBlockServiceFactory() = default;

std::unique_ptr<KeyedService>
AdBlockServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  auto service =
      std::make_unique<AdBlockService>(Profile::FromBrowserContext(context));
  const auto ready_component = ReadReadyAdBlockFilterComponent(
      g_browser_process ? g_browser_process->local_state() : nullptr);
  if (ready_component) {
    service->ActivateVerifiedFilterComponent(ready_component->install_dir,
                                              ready_component->version,
                                              base::DoNothing());
  }
  return service;
}

void AdBlockServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  AdBlockSettings::RegisterProfilePrefs(registry);
  AdBlockFilterListManager::RegisterProfilePrefs(registry);
}

bool AdBlockServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

}  // namespace seoul::adblock
