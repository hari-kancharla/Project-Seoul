# Seoul Settings and site controls: reference fidelity revision

The first two visual candidates in this revision were rejected. Replacing a sparse form with preview tiles, large category targets, and grouped cards moved away from the Arc reference supplied by the user. Passing preference tests did not establish visual acceptance. The final direction replaces that custom category bar on macOS with an actual AppKit preferences toolbar and returns the content to compact, aligned form rows.

This record covers the native Settings window and the consolidation of URL-bar site tools. It does not certify the whole browser as shippable or describe advanced Chromium destination pages as a finished custom Settings experience.

## Research and comparison

The controlling visual reference is Arc's official historical Profiles image, also supplied directly by the user. At its apparent 2× scale, the 1,342-pixel-wide reference corresponds to a window approximately 671 points wide. Its profile list is approximately 240 points wide, with a narrow gap before the details. These are estimates from the image, not measurements of a current installed Arc build. The important structure is unambiguous: a centered native pane title, a compact icon-and-label toolbar, a divider, a brief explanation, and a contained profile list alongside ordinary preference rows. The previous 800-point candidate, 104-point category buttons, smaller profile list, and card treatments did not reproduce those proportions. [1]

Arc's General and contextual-menu images reinforce the same hierarchy. Pane identity belongs to the native title and toolbar; form content does not need another large heading. Ordinary actions are compact controls, not prominent colored calls to action. Arc-specific account, employee, and integration categories are not copied into Seoul because Seoul does not implement their behavior. [2]

Apple's macOS Settings guidance supports a dedicated window, pane toolbar, current pane title, last-pane restoration, and the app-menu Command-comma entry. The local macOS SDK additionally exposes `NSWindowToolbarStylePreference` specifically for Settings windows, with icon-and-label toolbar items. This is now used directly instead of approximating the platform toolbar with Chromium views. AppKit renders the current platform's selection and window treatment, so the result is not represented as a pixel-identical reproduction of a historical macOS screenshot. [3]

Zen's Workspaces documentation and pinned preferences source were reviewed earlier in this task. They are useful for distinguishing container sessions from full browser profiles and for naming layout choices. Zen's preview-card treatment was removed from this Settings revision after the user clarified that Arc's supplied native preferences image should be the visual target. Containers still separate site sign-ins and storage while sharing profile-level history, passwords, bookmarks, and extensions. [4]

For the address bar, Arc's official site-control image shows a grouped entry beside the address, with contextual page tools inside it. Its documentation describes accessing extensions and site-specific behavior through that entry. Seoul previously reserved permanent address-field space for Boost, Handset, and Shields separately. The replacement uses one Site controls button and a short native menu containing those real actions plus site information. A second, visually similar secure-page tuning icon was identified during the top-toolbar comparison and removed on ordinary secure pages only. It does not claim to reproduce Arc's extension-management implementation. [5]

Dia's official help page was also opened during this research. It did not expose a usable Settings screenshot in the rendered help surface. No claim of an installed Dia review or pixel comparison is made. The directly supplied Arc image is the authoritative visual reference for this revision.

## Implemented changes

- **Native window structure:** AppKit preferences toolbar, system symbols, centered pane title, native selected item, disabled minimize/zoom controls, and existing singleton/last-pane behavior. Toolbar actions invoke the existing profile-owned Settings controller.
- **Reference proportions:** 680-point default width, 240-point profile list, 32-point content insets and column gap. The profile list keeps its height when names are long; names elide rather than widening the window.
- **Plain forms:** preview tiles, repeated pane headings, decorative cards, and the idle “Changes apply to …” footer are removed. Neutral separators and compact rectangular action buttons establish hierarchy. Loading and error messages remain visible only when needed.
- **Switches:** larger track and thumb proportions, native switch semantics, and preservation of the existing switch animation instead of resetting it during the preference callback.
- **Profile scope:** selection, renaming, creation, download preferences, and containers continue to use authoritative profile services. No secondary settings store or fake profile state was introduced.
- **Site controls:** one address-field entry opens Boost, Phone view, Shields, and site information. The menu releases native tracking and focus before opening a panel. Pending actions verify the original page and active tab; committed navigation and destruction cancel pending work. Site information opens the existing authoritative page-information panel. The leading indicator remains for non-secure pages and identity labels; permission indicators remain in place. Only the redundant ordinary secure-page icon is consolidated into the menu.
- **Patch integrity:** address-bar changes are recorded as a new patch after the existing series, with the original earlier patches preserved.

## Verification and visual review

The native AppKit build completed before its focused browser tests. The focused Settings gate passed all 14 executions, including restart setup, actual native toolbar action dispatch, external preference observation, profile isolation, policy behavior, window ownership, and size coverage in both appearances.

The actual macOS app was reviewed in an isolated profile. The review confirmed the centered native title, real toolbar, profile list, theme switching, profile creation, profile rename, selected-profile continuity, container name validation/creation, the native folder chooser and cancellation, and an actual window resize. The retained final light captures show Appearance and Profiles. AppKit dark captures and a resized Profiles capture document the earlier native-window review; they precede the site-menu consolidation. The older Privacy capture still shows the former shield-button hint, while the current Privacy code already names Site controls. Those captures establish the window treatment and layout, not the final wording of every pane. Screenshots preserve the operating-system capture indicator and pointer where present; they are not retouched UI mockups.

A Control-F5 toolbar-focus probe could not be sent by the computer-use tool because that tool did not support the F5 key. Native toolbar actions were exercised through the actual AppKit target/action path and pointer interaction. This is not a claim of a completed VoiceOver or exhaustive keyboard accessibility audit.

One earlier, pre-AppKit full focused run intermittently reported an enabled Create button for a blank container name. Five isolated repetitions passed, subsequent full focused gates passed, and the actual native dialog was observed with Create disabled until a name was entered. The initial failure remains in the evidence; its root cause was not established and it is not erased by the subsequent passes.

The added site-information test exercised the real menu and panel, verified one entry on a secure page across all three layouts, and verified that the leading identity entry remains on an insecure page. It passed together with the layout and phone-to-desktop interaction tests (3/3) after the final build. The complete sequential gate exited 0. The native build finished before the unit and browser suites, and repository CI completed afterward. The current implementation supersedes the rejected preview-card candidate. Capture stages are recorded explicitly in the visual-review receipt. User design acceptance remains distinct from engineering verification.

## Completed gates and evidence

| Gate | Verified result |
| --- | --- |
| Native build | Passed before the full test run |
| Native unit suites | 34 binaries, 864 cases passed |
| Browser integration | 229 cases and 7 PRE restart setups passed; 0 failures |
| Settings within that run | 16 executions passed, including the PRE setup and managed-preferences fixture |
| Optional screenshot fixture | 1 skipped because its capture environment variable was unset; not counted as visual verification |
| Repository CI | Passed |
| Patch integrity | 53 patches; no duplicate additions; 245 applied files matched the checkout; 220 baseline files restored exactly |
| Source stability | 1,971 inputs unchanged during the complete gate |

The final screenshot review distinguished an older Privacy capture from current source: the current Privacy instruction already named Site controls. It also found a remaining Boost-button instruction in the Library empty state. That **one help sentence** was corrected after the complete gate. A new native build, two WebUI/Boost browser smoke tests, WebUI type checking, product architecture checks, and repository tests then passed. The exact single-line delta and both source receipts are retained; the full suite was not represented as having been rerun after that copy-only change.

The Mac locked before the added Site information item and the final removal of the duplicate secure-page icon could receive another native pointer/screenshot review. The actual menu-to-page-information interaction and secure/insecure icon behavior passed automated browser tests. Native visual review of that final small change remains pending. No claim of whole-browser release readiness or user design acceptance is made.

- Verification result (`native/evidence/2026-09-09/settings-and-site-controls/verification-summary.json`)
- Complete gate log (`native/evidence/2026-09-09/settings-and-site-controls/full-verification.log`)
- Final copy correction and follow-up checks (`native/evidence/2026-09-09/settings-and-site-controls/copy-followup.log`)
- Visual review and capture provenance (`native/evidence/2026-09-09/settings-and-site-controls/visual-review.json`)
- Profiles, light (`native/evidence/2026-09-09/settings-and-site-controls/settings-profiles-light.jpg`)
- Appearance, light (`native/evidence/2026-09-09/settings-and-site-controls/settings-appearance-light.jpg`)
- Profiles, dark (`native/evidence/2026-09-09/settings-and-site-controls/settings-profiles-dark.jpg`)

## Remaining product work

Several advanced destinations, including comprehensive search-engine, language, permission, password, and reset pages, still open existing browser settings pages. Downloads as a full browser surface is also a separate migration. The native window improves the primary Settings entry and profile/form presentation; it does not represent those destinations, Boost's editor design, the graph, voice, or the entire browser as finished.

## Sources

1. Arc Help Center, [official Profiles Settings image](https://resources.arc.net/hc/article_attachments/20678079725079), historical January 2024 reference supplied by the user and visually inspected in the browser; [Profiles: Separate Work & Personal Browsing](https://resources.arc.net/hc/en-us/articles/19227964556183-Profiles-Separate-Work-Personal-Browsing).
2. Arc Help Center, [General Settings image](https://resources.arc.net/hc/article_attachments/23281349059351) and [historical dark contextual-menu image](https://resources.arc.net/hc/article_attachments/22557798819095), inspected during this task.
3. Apple Developer, [Human Interface Guidelines: Settings](https://developer.apple.com/design/human-interface-guidelines/settings), read in the rendered page; [NSWindow toolbar style](https://developer.apple.com/documentation/appkit/nswindow/toolbarstyle-swift.property), plus the local Xcode macOS 26.5 SDK `NSWindow.h`, `NSToolbar.h`, and `NSImage.h` for the implemented native APIs.
4. Zen Browser, [Workspaces](https://docs.zen-browser.app/user-manual/workspaces) and [Look and Feel preferences at the pinned research revision](https://github.com/zen-browser/desktop/blob/fdf9588b7b1ed58d71254d8ee1a1c8b414728be5/src/browser/components/preferences/zenLooksAndFeel.inc.xhtml).
5. Arc Help Center, [official Site Control Center image](https://resources.arc.net/hc/article_attachments/25701941038103), visually inspected; [Extensions in Arc](https://resources.arc.net/hc/en-us/articles/19434259167767-Extensions-in-Arc-How-to-Import-Add-Open), and [historical macOS release notes](https://start.arc.net/release-notes) documenting the consolidation of Boosts and extensions into Site Control Center.
