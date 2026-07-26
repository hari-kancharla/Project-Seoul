// Project Seoul Boosts - sticky native WebPreferences override.

#include "seoul/browser/product/browser/boost_web_preferences.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "third_party/blink/public/mojom/css/preferred_color_scheme.mojom.h"
#include "ui/color/color_provider_key.h"

namespace seoul {

namespace {

class BoostWebPreferencesState
    : public content::WebContentsUserData<BoostWebPreferencesState> {
public:
  BoostWebPreferencesState(const BoostWebPreferencesState &) = delete;
  BoostWebPreferencesState &
  operator=(const BoostWebPreferencesState &) = delete;
  ~BoostWebPreferencesState() override = default;

  bool automatic_dark_mode() const { return automatic_dark_mode_; }
  void set_automatic_dark_mode(bool enabled) { automatic_dark_mode_ = enabled; }

private:
  friend class content::WebContentsUserData<BoostWebPreferencesState>;

  explicit BoostWebPreferencesState(content::WebContents *web_contents)
      : content::WebContentsUserData<BoostWebPreferencesState>(*web_contents) {}

  bool automatic_dark_mode_ = false;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(BoostWebPreferencesState);

} // namespace

void SetBoostAutomaticDarkMode(content::WebContents *web_contents,
                               bool enabled) {
  if (!web_contents) {
    return;
  }
  BoostWebPreferencesState::CreateForWebContents(web_contents);
  BoostWebPreferencesState *state =
      BoostWebPreferencesState::FromWebContents(web_contents);
  if (!state || state->automatic_dark_mode() == enabled) {
    return;
  }
  state->set_automatic_dark_mode(enabled);
  // Recompute the baseline first; the Seoul override is applied from
  // ChromeContentBrowserClient at the end of that same computation.
  web_contents->OnWebPreferencesChanged();
}

bool IsBoostAutomaticDarkModeEnabled(content::WebContents *web_contents) {
  BoostWebPreferencesState *state =
      web_contents ? BoostWebPreferencesState::FromWebContents(web_contents)
                   : nullptr;
  return state && state->automatic_dark_mode();
}

void OverrideBoostWebPreferences(
    content::WebContents *web_contents,
    blink::web_pref::WebPreferences *web_preferences) {
  if (!web_preferences || !IsBoostAutomaticDarkModeEnabled(web_contents)) {
    return;
  }
  if (web_contents->GetColorMode() != ui::ColorProviderKey::ColorMode::kDark) {
    Profile *profile =
        Profile::FromBrowserContext(web_contents->GetBrowserContext());
    if (profile) {
      web_preferences->force_dark_mode_enabled =
          profile->GetPrefs()->GetBoolean(prefs::kWebKitForceDarkModeEnabled);
    }
    return;
  }
  web_preferences->force_dark_mode_enabled = true;
  web_preferences->preferred_color_scheme =
      blink::mojom::PreferredColorScheme::kDark;
}

} // namespace seoul
