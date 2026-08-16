// Project Seoul Boosts - native discovery entry points.

#include "seoul/browser/product/browser/boost_entry_points.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "content/public/browser/web_contents.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/product/browser/seoul_boost_bubble.h"
#include "seoul/browser/product/browser/seoul_runtime_service.h"
#include "seoul/browser/product/browser/seoul_runtime_service_factory.h"

namespace seoul {

BrowserWindowInterface *EligibleBrowserFor(
    content::WebContents *web_contents) {
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

bool CanBoostWebContents(content::WebContents *web_contents) {
  BrowserWindowInterface *browser = EligibleBrowserFor(web_contents);
  return browser && web_contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() &&
         SeoulRuntimeServiceFactory::GetForProfile(browser->GetProfile());
}

bool OpenBoostEditorForWebContents(content::WebContents *web_contents) {
  if (!CanBoostWebContents(web_contents)) {
    return false;
  }
  BrowserWindowInterface *browser = EligibleBrowserFor(web_contents);
  SeoulRuntimeService *runtime =
      browser ? SeoulRuntimeServiceFactory::GetForProfile(browser->GetProfile())
              : nullptr;
  if (!runtime) {
    return false;
  }
  const LiveWindowKey window =
      LiveWindowKey::FromSessionId(browser->GetSessionID().id());
  runtime->RequestBoostEditor(window);
  // The editor is the native bubble - on the page being boosted, the way Arc
  // does it - rather than the Canvas side panel, which was a different surface
  // asking you to edit a site you were no longer looking at.
  return ShowBoostBubbleForWebContents(web_contents);
}

}  // namespace seoul
