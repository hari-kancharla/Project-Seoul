// Project Seoul Boosts - native discovery entry points.

#include "seoul/browser/product/browser/boost_entry_points.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
// Circular implementation dependency: this Seoul product target is linked
// through //chrome/browser, which owns the SidePanelUI implementation.
#include "chrome/browser/ui/side_panel/side_panel_ui.h" // nogncheck
#include "content/public/browser/web_contents.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/product/browser/seoul_runtime_service.h"
#include "seoul/browser/product/browser/seoul_runtime_service_factory.h"

namespace seoul {

namespace {

BrowserWindowInterface *EligibleBrowserFor(content::WebContents *web_contents) {
  if (!web_contents) {
    return nullptr;
  }
  GlobalBrowserCollection *browsers = GlobalBrowserCollection::GetInstance();
  BrowserWindowInterface *browser =
      browsers ? browsers->FindBrowserWithTab(web_contents) : nullptr;
  Profile *profile = browser ? browser->GetProfile() : nullptr;
  if (!browser || !profile || profile->IsOffTheRecord() ||
      browser->IsDeleteScheduled() ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
      !browser->GetSessionID().is_valid()) {
    return nullptr;
  }
  return browser;
}

} // namespace

bool CanBoostWebContents(content::WebContents *web_contents) {
  BrowserWindowInterface *browser = EligibleBrowserFor(web_contents);
  return browser && web_contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() &&
         SeoulRuntimeServiceFactory::GetForProfile(browser->GetProfile());
}

bool OpenBoostEditorForWebContents(content::WebContents *web_contents) {
  BrowserWindowInterface *browser = EligibleBrowserFor(web_contents);
  SeoulRuntimeService *runtime =
      browser ? SeoulRuntimeServiceFactory::GetForProfile(browser->GetProfile())
              : nullptr;
  SidePanelUI *side_panel =
      browser ? browser->GetFeatures().side_panel_ui() : nullptr;
  if (!runtime || !side_panel) {
    return false;
  }
  const LiveWindowKey window =
      LiveWindowKey::FromSessionId(browser->GetSessionID().id());
  runtime->RequestBoostEditor(window);
  side_panel->Show(SidePanelEntryId::kSeoulCanvas);
  return true;
}

} // namespace seoul
