// Project Seoul product runtime - the profile factory.

#include "seoul/browser/product/browser/seoul_runtime_service_factory.h"

#include <memory>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
// nogncheck: //chrome/browser/ui reaches this target through the side-panel
// Canvas registration, so a declared dep would be a dependency cycle; the
// symbols link through //chrome/browser like the other circular includes.
#include "chrome/browser/ui/tabs/tab_strip_model.h"  // nogncheck
#include "components/keyed_service/core/keyed_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/session_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/organization/seoul_organization_service_factory.h"
#include "seoul/browser/product/browser/seoul_runtime_service.h"

namespace seoul {

namespace {

// Resolves a Seoul live tab key to its WebContents within `profile` by
// matching the tab's SessionID. Returns null when the tab is gone (the page
// agent treats that as an unknown tab).
content::WebContents* ResolveWebContents(Profile* profile,
                                         const LiveTabKey& tab) {
  if (!profile || !tab.is_valid()) {
    return nullptr;
  }
  const SessionID target = SessionID::FromSerializedValue(tab.session_id());
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile);
  if (!collection) {
    return nullptr;
  }
  content::WebContents* found = nullptr;
  collection->ForEach([&](BrowserWindowInterface* browser) {
    if (found) {
      return true;
    }
    TabStripModel* model = browser->GetTabStripModel();
    if (!model) {
      return true;
    }
    for (int index = 0; index < model->count(); ++index) {
      tabs::TabInterface* tab_interface = model->GetTabAtIndex(index);
      content::WebContents* contents =
          tab_interface ? tab_interface->GetContents() : nullptr;
      if (contents &&
          sessions::SessionTabHelper::IdForTab(contents) == target) {
        found = contents;
        return true;
      }
    }
    return true;
  });
  return found;
}

}  // namespace

// static
SeoulRuntimeService* SeoulRuntimeServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<SeoulRuntimeService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
SeoulRuntimeService* SeoulRuntimeServiceFactory::GetForProfileIfExists(
    Profile* profile) {
  return static_cast<SeoulRuntimeService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/false));
}

// static
SeoulRuntimeServiceFactory* SeoulRuntimeServiceFactory::GetInstance() {
  static base::NoDestructor<SeoulRuntimeServiceFactory> instance;
  return instance.get();
}

SeoulRuntimeServiceFactory::SeoulRuntimeServiceFactory()
    : ProfileKeyedServiceFactory(
          "SeoulRuntimeService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .WithAshInternals(ProfileSelection::kNone)
              .Build()) {
  // The product runtime references the organization service; build and tear
  // down in the correct order.
  DependsOn(SeoulOrganizationServiceFactory::GetInstance());
}

SeoulRuntimeServiceFactory::~SeoulRuntimeServiceFactory() = default;

std::unique_ptr<KeyedService>
SeoulRuntimeServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  SeoulOrganizationService* organization =
      SeoulOrganizationServiceFactory::GetForProfile(profile);
  return std::make_unique<SeoulRuntimeService>(
      profile, profile->GetPrefs(), organization,
      base::BindRepeating(&ResolveWebContents, profile));
}

void SeoulRuntimeServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  SeoulRuntimeService::RegisterProfilePrefs(registry);
}

bool SeoulRuntimeServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  // Create eagerly so pinned surfaces, threads, workflows, and provider
  // settings load at profile startup. No UI or tab behavior is touched until
  // the user acts.
  return true;
}

}  // namespace seoul
