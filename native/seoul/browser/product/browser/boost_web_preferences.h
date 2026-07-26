// Project Seoul Boosts - sticky native WebPreferences override.
//
// Automatic dark mode is a typed Site Layer adjustment, but Blink's automatic
// darkening is a WebContents preference rather than CSS. The applicator stores
// one browser-owned bit on the WebContents; ChromeContentBrowserClient calls
// OverrideBoostWebPreferences() during every preference recomputation so the
// setting survives navigation, system-theme changes, and unrelated pref
// updates without changing the profile-wide dark-mode preference.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_BOOST_WEB_PREFERENCES_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_BOOST_WEB_PREFERENCES_H_

namespace blink::web_pref {
struct WebPreferences;
}

namespace content {
class WebContents;
}

namespace seoul {

void SetBoostAutomaticDarkMode(content::WebContents *web_contents,
                               bool enabled);
bool IsBoostAutomaticDarkModeEnabled(content::WebContents *web_contents);
void OverrideBoostWebPreferences(
    content::WebContents *web_contents,
    blink::web_pref::WebPreferences *web_preferences);

} // namespace seoul

#endif // SEOUL_BROWSER_PRODUCT_BROWSER_BOOST_WEB_PREFERENCES_H_
