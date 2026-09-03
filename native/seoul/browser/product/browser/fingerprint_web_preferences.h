// Project Seoul fingerprinting protection - the browser-side decision.
//
// The strongest canvas defence Blink already carries: with
// `disable_reading_from_canvas` every canvas is treated as tainted, so the
// readbacks a fingerprinter needs - toDataURL, getImageData, toBlob - throw a
// SecurityError instead of yielding pixels. Brave answers the same attack by
// farbling the pixels; blocking the read outright is the honest v1 that needs
// no Blink patch, applied per site from the Shields panel and only while the
// site's shields are up. ChromeContentBrowserClient calls the override during
// every preference recomputation, so the protection survives navigation and
// unrelated preference updates exactly the way the Boost and Handset
// overrides do.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_FINGERPRINT_WEB_PREFERENCES_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_FINGERPRINT_WEB_PREFERENCES_H_

namespace blink::web_pref {
struct WebPreferences;
}

namespace content {
class WebContents;
}

namespace seoul {

// Applies the per-site canvas-fingerprint decision to `web_preferences`.
// Returns true when it changed anything, so the caller's prefs-changed
// accounting stays exact.
bool OverrideFingerprintWebPreferences(
    content::WebContents* web_contents,
    blink::web_pref::WebPreferences* web_preferences);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_FINGERPRINT_WEB_PREFERENCES_H_
