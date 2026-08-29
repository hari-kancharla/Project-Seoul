// Project Seoul Handset - live WebContents application.
//
// Handset mode presents one page as a phone. This is the browser-owned half:
// it puts the User-Agent override on the WebContents and supplies the mobile
// half of the WebPreferences that ChromeContentBrowserClient recomputes.
//
// State lives on the WebContents rather than in a window-keyed map, so it
// survives navigation, cross-process swaps, and moving the tab between
// windows, and it is torn down with the contents rather than outliving it.
//
// The device catalogue, geometry, and User-Agent rules are not here. They are
// in //seoul/browser/handset, which has no browser dependency and is unit
// tested on its own; this file decides nothing about what a site sees.

#ifndef SEOUL_BROWSER_PRODUCT_BROWSER_HANDSET_MODE_H_
#define SEOUL_BROWSER_PRODUCT_BROWSER_HANDSET_MODE_H_

#include <string>

#include "seoul/browser/handset/handset_types.h"

namespace blink::web_pref {
struct WebPreferences;
}

namespace content {
class WebContents;
}

namespace seoul {

// Whether Handset mode can be offered for `web_contents` at all. A Handset is
// a web page presented as a phone, so it needs a real http(s) page in an
// ordinary window; internal pages and app windows have nothing to present.
// Shared by the site-control button and any other entry point, so they cannot
// disagree about what is eligible.
bool CanHandsetWebContents(content::WebContents* web_contents);

// Turns Handset mode on with the default device, or off if it is already on.
// Returns the resulting state. Ineligible contents return false and change
// nothing.
bool ToggleHandsetForWebContents(content::WebContents* web_contents);

// Turns Handset mode on for `web_contents`, or updates the live device.
// Returns false and changes nothing when `profile_id` names no catalogue
// profile, so a stale persisted id fails closed rather than silently
// selecting some other device.
//
// `free_width_dip`/`free_height_dip` are consulted only in
// HandsetSnapMode::kFree, where they are the size the caller wants - a device
// picker's typed custom dimensions, or a window layer's current content size
// once one exists. Left at their default of 0, kFree falls back to the
// profile's own portrait/landscape size rather than the unrelated 240x320
// clamp floor; see ResolveHandsetMetrics for why that fallback exists. The
// chosen size is stored, not just applied once - it survives navigation the
// same way the device and orientation do.
bool EnableHandsetMode(content::WebContents* web_contents,
                       const std::string& profile_id,
                       HandsetOrientation orientation,
                       HandsetSnapMode snap_mode,
                       int free_width_dip = 0,
                       int free_height_dip = 0);

// Returns the page to its normal desktop presentation. Safe to call when
// Handset mode is already off.
void DisableHandsetMode(content::WebContents* web_contents);

bool IsHandsetModeEnabled(content::WebContents* web_contents);

// The live profile, or nullptr when Handset mode is off.
const HandsetProfile* HandsetProfileFor(content::WebContents* web_contents);

// The resolved geometry for the live Handset, which the window layer uses to
// size itself. `free_width_dip`/`free_height_dip` are the window's current
// content size and are consulted only in HandsetSnapMode::kFree.
HandsetMetrics HandsetMetricsFor(content::WebContents* web_contents,
                                 int free_width_dip,
                                 int free_height_dip);

// Whether the renderer process backing `web_contents` also hosts frames that
// belong to some other page.
//
// This exists because Blink's device emulation installs process-global
// overrides - overlay scrollbars, OrientationEvent, the mobile layout theme -
// which have no per-page equivalent and so cannot be scoped. Enabling it in a
// shared renderer silently changes pages the user never asked to present as a
// phone.
//
// There is no way to demand a dedicated process for one WebContents while
// keeping its storage: a RenderProcessHost serves exactly one StoragePartition,
// so isolation and a shared cookie jar are mutually exclusive, and the
// embedder hook that influences reuse
// (ContentBrowserClient::ShouldTryToUseExistingProcessHost) is keyed on a
// BrowserContext and a URL with no way to name a particular tab. Handset
// therefore keeps the originating partition - a Handset signed out of the site
// it is presenting would be useless - and reports the residual here instead of
// pretending it away, so the emulation layer can decide what to do about it.
bool HandsetRendererIsShared(content::WebContents* web_contents);

// Applies the mobile half of the preferences.
//
// Must be called from BOTH ChromeContentBrowserClient::OverrideWebPreferences
// and ::OverrideWebPreferencesAfterNavigation, the way the Boost dark-mode bit
// is wired. Wiring only the first is the easy mistake and a quiet one: the
// preferences would be dropped on the next navigation, and while device
// emulation is active the drop is invisible until emulation is turned off and
// the page snaps back to desktop layout.
// Returns true when it changed a field, which the after-navigation caller must
// fold into its own prefs_changed bookkeeping - that variant only pushes the
// recomputed preferences to the renderer when something reports a change, so a
// silent return would leave the mobile layout unapplied after every
// navigation.
bool OverrideHandsetWebPreferences(
    content::WebContents* web_contents,
    blink::web_pref::WebPreferences* web_preferences);

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_BROWSER_HANDSET_MODE_H_
