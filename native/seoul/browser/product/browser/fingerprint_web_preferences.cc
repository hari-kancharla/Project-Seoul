// Project Seoul fingerprinting protection - the browser-side decision.

#include "seoul/browser/product/browser/fingerprint_web_preferences.h"

#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "seoul/browser/adblock/ad_block_request.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "seoul/browser/adblock/ad_block_settings.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"

namespace seoul {

bool OverrideFingerprintWebPreferences(
    content::WebContents* web_contents,
    blink::web_pref::WebPreferences* web_preferences) {
  if (!web_contents || !web_preferences) {
    return false;
  }
  Profile* const profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  adblock::AdBlockService* const service =
      profile ? adblock::AdBlockServiceFactory::GetForProfile(profile)
              : nullptr;
  if (!service) {
    return false;
  }
  const adblock::AdBlockSiteSettings settings =
      service->GetSiteSettings(web_contents->GetLastCommittedURL());
  // The protection follows the shields: a site whose shields are Off gets
  // none of the blocker's interventions, this one included - one switch
  // means one thing.
  const bool block = settings.canvas_fingerprint_blocked &&
                     settings.effective_mode != adblock::AdBlockMode::kOff;
  if (!block) {
    // Never force the field off: another embedder policy (headless, WebView)
    // may have set it for its own reasons.
    return false;
  }
  const bool changed = !web_preferences->disable_reading_from_canvas;
  web_preferences->disable_reading_from_canvas = true;
  return changed;
}

}  // namespace seoul
