# Seoul Phone view (Handset)

Updated 2026-09-07. This describes the implemented browser preview and its
remaining limits. Verification for the current repair is recorded in
`seoul-feature-reliability-2026-09-07.md`; earlier readiness claims are dated
snapshots, not evidence for later source changes.

## User behavior

The phone control beside the address field opens a native, searchable device
picker. All supported devices are in one scrolling list. Search matches device
names, platform, and phone/tablet terms; arrow keys move through results and
Return chooses a single search result. The list has a bounded height and shrinks
for a short result set. There is no nested “More devices” menu.

Selecting a device from a desktop page opens a dedicated tab with the same page
and storage partition. Selecting another device from a phone tab changes that
tab in place and preserves its orientation. The popup closes before tab creation
or selection begins. Changing tabs, navigating, or closing the source page closes
the picker. Deferred actions hold weak references to the original page.

“Rotate” changes the live viewport orientation. “Desktop” restores that tab's
previous user agent and desktop preferences. “Custom size” accepts viewport
width and height within the supported bounds; canceling leaves the page alone.
Custom size follows the same dedicated-tab rule as catalog selection. Both
dimensions must be whole numbers in range; Apply stays disabled for invalid
input. Valid values are applied after the modal dialog has closed.

This implementation is a tab, not a separate frameless phone window. It does
not transfer a task to a physical phone or run native Android/iOS applications.
Physical phone handoff is a separate unfinished capability.

## What reaches a website

`HandsetModeState` owns per-WebContents device identity, orientation and viewport
configuration. The content-layer presentation API applies Blink mobile device
emulation. Preferences, user agent, client hints, and touch presentation are
updated together and restored on exit. Device presentation is reapplied after
committed main-frame navigation so a process swap does not silently revert to
desktop geometry.

Responsive pages with `width=device-width` see the selected CSS width. Pages
without a mobile viewport declaration retain Blink's mobile fallback layout;
forcing every page's `innerWidth` to the phone width would break that behavior.
A DPR-aware page sees the preset device scale factor. Coarse pointer and
hover media features are applied alongside touch-from-mouse presentation.
Trackpad gestures and inertia still need comparisons on real websites; no
measured scrolling-latency advantage is claimed.

An iOS preset still runs Blink on macOS. It does not reproduce Safari/WebKit,
physical sensors, browser bars, safe areas, on-screen keyboards, or every iOS
API. Suppressing the user-agent client-hint headers does not remove the
`navigator.userAgentData` property. This is the same category of first-order
simulation described in [Chrome's Device Mode documentation](https://developer.chrome.com/docs/devtools/device-mode).

## Device data and current models

The generator merges the pinned DevTools device catalog with
`native/seoul/browser/handset/handset_profiles_overlay.json`. Existing IDs stay
stable: `android` still means Pixel 8, and `iphone` still means iPhone 15. New
models get new IDs. The generator validates overlay geometry, identity, and
source metadata instead of accepting arbitrary out-of-range values.

The current overlay adds iPhone 17, 17 Pro, 17 Pro Max, Pixel 10, and a Galaxy
S26 Ultra QHD+ preview. Pixel 10 uses [Google's DevTools entry](https://github.com/ChromeDevTools/devtools-frontend/blob/main/front_end/models/emulation/EmulatedDevices.ts)
reviewed on 2026-09-07. The iPhone presets derive reference dimensions at the
selected 3x scale from [Apple's iPhone 17 specifications](https://www.apple.com/iphone-17/specs/)
and [17 Pro specifications](https://www.apple.com/iphone-17-pro/specs/).

Samsung documents a 1440 × 3120 panel for the Canadian SM-S948W
[Galaxy S26 Ultra](https://www.samsung.com/ca/business/smartphones/galaxy-s/galaxy-s26-ultra-sm-s948wzdaxac/).
That does not establish its default browser viewport. The explicitly labeled
QHD+ preview uses a selected 3.75 DPR, yielding 384 × 832 CSS pixels. This is a
configured reference, not a claim that a factory-reset phone reports those
metrics. Physical-device comparison remains open, including screen zoom and
resolution settings. Conflicting third-party viewport tables were not treated
as authoritative measurements.

Windows Phone compatibility user agents are excluded from Android profiles.
Galaxy Tab entries are classified as tablets even when the pinned source's
type field says phone. The picker covers every supported preset in the catalog;
it does not claim to represent every phone model sold.

`check:handset-profiles` detects drift against pinned input. It cannot detect
new phones released after that input was reviewed. Device freshness therefore
requires a deliberate source review alongside browser version updates.

## Important implementation boundaries

The original storage partition is preserved so the preview can retain the
site's session. Blink device emulation also contains process-global mobile
settings; `HandsetRendererIsShared()` reports whether another page shares the
renderer. This remains an isolation limitation requiring real-site coverage.
No claim of complete per-tab process isolation is made.

The bounds are 240–1366 CSS pixels wide, 320–1600 high, and DPR 1–4. These are
supported preview limits, not statements about all shipping hardware. Free-size
mode resolves missing dimensions from the selected preset and chooses a scale
factor through the existing nearest-width policy. The custom-size dialog validates both fields before invoking this path, so
invalid input cannot silently become a different viewport size.

## Verification required before release

- Real pointer and keyboard use of the toolbar, search, scrolling, selection,
  rotation, custom size and desktop return; repeated open/close and tab changes.
- Actual page-reported geometry, media queries, identity, and touch input after
  same-site and cross-site navigation; desktop restoration afterward.
- Physical-device comparisons for current presets, particularly configurable
  Android resolution/DPR and iOS engine differences.
- Signed-in mobile-site journeys, forms, dialogs, uploads, media, scrolling,
  display scaling and accessibility across supported window sizes.
- Shipping-build latency and memory measurements. Unit tests and development
  build measurements do not certify those outcomes.
