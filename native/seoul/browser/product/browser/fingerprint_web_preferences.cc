// Project Seoul fingerprinting protection - the browser-side decision.

#include "seoul/browser/product/browser/fingerprint_web_preferences.h"

#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "seoul/browser/adblock/ad_block_request.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "seoul/browser/adblock/ad_block_settings.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"

namespace seoul {

namespace {

// The canvas taint is a field Seoul shares with content and Chrome, which set
// it for their own reasons. The post-navigation seam hands the override the
// WebContents's *current* preferences rather than a fresh computation, so a
// taint applied for one site would ride into the next unless Seoul remembers
// that it was the one who set it - and what the field held before.
class FingerprintTaintState
    : public content::WebContentsUserData<FingerprintTaintState> {
 public:
  FingerprintTaintState(const FingerprintTaintState&) = delete;
  FingerprintTaintState& operator=(const FingerprintTaintState&) = delete;
  ~FingerprintTaintState() override = default;

  bool applied = false;
  bool value_before = false;

 private:
  friend class content::WebContentsUserData<FingerprintTaintState>;

  explicit FingerprintTaintState(content::WebContents* web_contents)
      : content::WebContentsUserData<FingerprintTaintState>(*web_contents) {}

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(FingerprintTaintState);

// The site whose decision governs this WebContents.
//
// Usually its committed URL, but a popup opened with window.open() and left on
// about:blank never commits one, and a blank main frame is not a site without
// an identity: it inherits its creator's origin, and script in the opener
// reaches straight into it. Keyed on the committed URL alone such a popup is
// not http(s), so the blocker declines it, the token stays 0, and the opener
// reads a completely unfarbled canvas out of a window it owns - the whole
// protection bypassed by one call, permanently, because no later navigation
// ever raises the token. Falling back to the origin the frame actually holds,
// and then to the site its process is locked to, makes a blank popup farble
// exactly as the page that opened it does.
GURL SiteForFingerprintDecision(content::WebContents* web_contents) {
  const GURL committed = web_contents->GetLastCommittedURL();
  if (committed.SchemeIsHTTPOrHTTPS()) {
    return committed;
  }
  content::RenderFrameHost* const frame = web_contents->GetPrimaryMainFrame();
  if (!frame) {
    return committed;
  }
  const GURL inherited = frame->GetLastCommittedOrigin().GetURL();
  if (inherited.SchemeIsHTTPOrHTTPS()) {
    return inherited;
  }
  content::SiteInstance* const site_instance = frame->GetSiteInstance();
  const GURL locked =
      site_instance ? site_instance->GetSiteURL() : GURL();
  return locked.SchemeIsHTTPOrHTTPS() ? locked : committed;
}

}  // namespace

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
  const GURL site_url = SiteForFingerprintDecision(web_contents);
  // Read through this WebContents' own context. One service serves a profile
  // and its private sessions, but their settings are separate: a private
  // window inherits the regular profile's choices and keeps its own to itself.
  const std::unique_ptr<adblock::AdBlockSettings> scoped_settings =
      service->SettingsFor(web_contents->GetBrowserContext());
  const adblock::AdBlockSiteSettings settings =
      scoped_settings ? scoped_settings->GetSiteSettings(site_url)
                      : service->GetSiteSettings(site_url);
  // One switch means one thing: a site whose shields are Off gets none of the
  // blocker's interventions, this one included.
  const adblock::FingerprintMode mode =
      settings.effective_mode == adblock::AdBlockMode::kOff
          ? adblock::FingerprintMode::kOff
          : settings.fingerprint_mode;

  bool changed = false;

  // The token is Seoul's own field: decided for this site and assigned every
  // time, so a token set for the previous site can never ride into this one.
  const uint64_t token =
      mode == adblock::FingerprintMode::kOff
          ? 0u
          : service->GetFarblingToken(
                site_url,
                adblock::AdBlockService::IdentityScopeFor(web_contents));
  changed |= web_preferences->seoul_farbling_token != token;
  web_preferences->seoul_farbling_token = token;

  // The taint is shared. Strict applies it and remembers what it replaced;
  // anything else restores exactly that, once, and otherwise leaves the field
  // to whoever else set it.
  FingerprintTaintState::CreateForWebContents(web_contents);
  FingerprintTaintState* const state =
      FingerprintTaintState::FromWebContents(web_contents);
  if (mode == adblock::FingerprintMode::kStrict) {
    if (!state->applied) {
      state->value_before = web_preferences->disable_reading_from_canvas;
      state->applied = true;
    }
    changed |= !web_preferences->disable_reading_from_canvas;
    web_preferences->disable_reading_from_canvas = true;
  } else if (state->applied) {
    changed |=
        web_preferences->disable_reading_from_canvas != state->value_before;
    web_preferences->disable_reading_from_canvas = state->value_before;
    state->applied = false;
  }
  return changed;
}

}  // namespace seoul
