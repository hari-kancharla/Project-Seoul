// Project Seoul fingerprinting protection - the browser-side decision.
//
// Two Blink-level levers, both riding WebPreferences so the content layer
// recomputes them on every navigation exactly as it does the Boost and
// Handset overrides. `seoul_farbling_token` (patch 0040) is the per-site
// session secret from which the renderer derives every perturbation: canvas
// and WebGL readbacks, and the hardware profile (patch 0041). Balanced sets
// only the token. Strict sets the token and `disable_reading_from_canvas`
// besides, so every canvas is tainted and pixel readbacks are refused
// outright while the hardware profile stays farbled. The mode is per-site
// state on the blocker - a profile default with a per-site override, set from
// the Shields panel - and it stands down while the site's shields are Off.
//
// Both seams are re-decided from scratch on every call. The post-navigation
// seam starts from the previous page's preferences, so the override assigns
// the token unconditionally and tracks the taint it applied per WebContents;
// nothing decided for one site can ride into the next.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_FINGERPRINT_WEB_PREFERENCES_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_FINGERPRINT_WEB_PREFERENCES_H_

namespace blink::web_pref {
struct WebPreferences;
}

namespace content {
class WebContents;
}

namespace seoul {

// Applies the per-site fingerprint decision to `web_preferences`. Returns
// true when it changed anything, so the caller's prefs-changed accounting
// stays exact.
bool OverrideFingerprintWebPreferences(
    content::WebContents* web_contents,
    blink::web_pref::WebPreferences* web_preferences);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_FINGERPRINT_WEB_PREFERENCES_H_
