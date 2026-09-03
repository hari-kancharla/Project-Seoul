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
          // Off-the-record profiles are served by the original profile's
          // instance rather than being given no service at all.
          //
          // `kOriginalOnly` meant a private window had no blocker and no
          // fingerprinting protection whatsoever: the engine was never
          // consulted, the canvas was never farbled, and the shields control
          // opened nothing. That is precisely backwards, because a private
          // window is where a person has most explicitly asked not to be
          // followed. Redirecting shares one engine and one set of filter
          // lists, so a private window costs no second download and no second
          // copy of the rules.
          //
          // The farbling identity does NOT come along with it: the token is
          // keyed on the requesting WebContents' own browser context (see
          // AdBlockService::IdentityScopeFor), so a private window still gets a
          // pattern of its own that cannot be linked to the regular profile's.
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kRedirectedToOriginal)
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
