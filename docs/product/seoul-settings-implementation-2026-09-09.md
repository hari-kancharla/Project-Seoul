# Seoul Settings implementation

The primary macOS Settings entry now opens a dedicated native window. The existing [Arc and Zen research](seoul-settings-research-and-direction-2026-09-08.md) establishes the presentation and profile/container distinctions. This implementation retains Chromium's preference services and Seoul's organization model as the data owners. **Implementation is awaiting final native visual and interaction review; it is not accepted as finished design work.**

## Implementation decisions

| Concern | Decision and reason |
| --- | --- |
| Window | One modeless Settings window with six compact top categories, native titlebar, normal close behavior, and a bounded scrollable content area. App-menu Settings and Command-comma share the same entry point. Reopening activates the existing window. |
| Profile selection | Existing profile metadata supplies the list. Loading another profile is asynchronous; settings remain disabled until the requested profile has loaded successfully. Stale callbacks cannot change the current selection. Locked and hidden profiles are excluded from direct editing. |
| Lifetime | The selected profile's runtime service owns the window; choosing another profile transfers ownership of the same window to that profile's service. A dedicated profile keepalive covers that selection. Service shutdown destroys the window before dependent services disappear. Closing Settings releases the keepalive. Open-window discovery reads existing profile services and does not keep mutable process-global state. |
| Preferences | Controls write through the selected profile's existing services, then read back actual values. Preference observers update existing controls without rebuilding the focused pane. Managed controls remain disabled and identify their policy state. |
| Profiles | Name, download behavior, and container spaces reflect the selected profile. Containers separate site storage; profile-level history, passwords, bookmarks, and extensions remain shared. Creating a container uses the existing organization model. |
| Appearance | Sidebar, top toolbar, and compact sidebar use the existing layout preference and update real browser windows. System/light/dark follows ThemeService. Address display and download animation use existing preferences. |
| Compatibility | Existing advanced settings URLs continue to work. Primary Settings routing changes without blocking specialized browser permission, password, or policy pages. Any remaining advanced-page link is named accurately. |
| Shape and interaction | Rounded rectangles, sky-blue selection, system typography, complete rows, keyboard navigation, and native focus indicators. Profile changes and ordinary preference updates do not destroy an active control. |

## Verification requirements

The settled build precedes every native test run. Browser tests must cover singleton routing, unchanged source tabs, close/reopen and last pane, profile selection and scoped writes, managed preferences, external preference updates, container creation, and teardown. Process-relaunch coverage must verify persisted changes. Native screenshots and keyboard interaction must inspect actual light and dark windows at the supported minimum size. Patch validation includes manifest hashes, overlap detection, and full scratch apply/compare/reverse.

No earlier test result is evidence for this new Settings implementation. The new focused suite covers primary routing, unchanged source tabs, window ownership, profile-scoped writes, restart persistence, policy changes, category keyboard navigation, and real profile/container naming dialogs. The final settled build and broader test receipts are recorded below.

## Scope and remaining review

- Appearance controls change all three real browser layouts, color mode, sidebar address display, and download animation. Native layout-preview cards and further visual refinements remain subject to review.
- Profiles can be selected, created, and renamed. Downloads use the selected profile's actual preference and native folder chooser. Container creation uses the existing organization model and does not switch or replace the browsing tab.
- General, Privacy, and Advanced include accurate links to specialized existing browser settings. Those destination pages have **not** been redesigned by this change. Shortcuts currently lists supported bindings; it does not claim to provide a shortcut editor.
- Live macOS visual review was blocked when the computer-use tool reported that the Mac was locked. A manual unlock was requested. The disposable verification application was closed before further builds.
- An attempted offscreen Views render omitted compositor-owned controls and was rejected as visual acceptance evidence. It also exposed a scroll-content sizing defect: narrowing the window could retain the previous content width. The implementation now requests preferred content sizing from ScrollView, and the test checks every pane at both sizes and appearances. Full native-window capture support replaces the incomplete rendering approach.
- The first full run passed native tests but failed the repository architecture gate because the initial window registry owned mutable process-global state. That design was replaced with ownership in the selected profile's runtime service; a profile switch transfers the same window, and service shutdown removes it before dependent services. The architecture rule was retained unchanged. A dedicated regression checks transfer, shutdown of the old profile without closing the moved window, and teardown of the selected profile.
- Further source review found that the profile list's fixed preferred size erased its natural height. The replacement constrains its width to 175 points while calculating height from its real rows. Native bounds checks now require the profile heading, Add profile control, and a long-name profile to be visible at both sizes. Profile actions sit beside their own labels with compact buttons, and the default window height follows the 620-point research direction. The intervening full run was stopped for this correction and is recorded as interrupted, not passed.
- Pending acceptance: inspect actual light/dark windows, pointer interactions, scrolling, category focus, native window close/reopen, folder selection/cancellation, and long profile/container names. VoiceOver, localization, multiple displays, and text scaling are not certified by the current automated checks.

### Resume the live review

Use an unlocked desktop session and a settled build with no other test/build owner. The `SeoulSettingsBrowserTest.AllPanesFitSupportedWindowSizes` case can capture its own disposable native window when `SEOUL_SETTINGS_CAPTURE_DIR` points to an output directory and the launcher runs **without** `--headless`. It captures 24 pane/appearance/size combinations. Inspect the resulting frames; a nonempty image is not, by itself, visual acceptance.

Then use a disposable Seoul Verification profile for actual app-menu/Command-comma entry, pointer and keyboard navigation, profile creation/switching/rename, container creation, native folder selection/cancel, titlebar close and Command-W, and reopening. Verify that browser tabs remain unchanged and the selected profile is clear throughout. The Settings window must release its profile when closed. Keep screenshots and observed outcomes with this record before marking the design accepted or beginning the Downloads replacement.

## Completed automated verification

The complete sequential gate exited **0**. It built the native targets before running tests and finished with `npm run ci`. All 797 recorded native inputs remained unchanged during the run.

| Gate | Result |
| --- | --- |
| Native build | Passed before tests |
| Native unit suites | 34 binaries; 864 cases passed |
| Browser suites | 226 cases plus 7 PRE setup executions passed; 1 optional screenshot fixture skipped; 0 failures |
| Settings-focused gate | 13 executions passed, including one PRE setup; all six panes checked in both appearances at 740 × 500 and 760 × 620 |
| Repository CI | Passed |
| Patch overlap | 52 patches, no duplicate additions |
| Scratch patch round trip | 244 patched files matched; 220 baseline files restored exactly |
| Live native visual/interaction acceptance | Pending Mac unlock; no completion claim |

The browser launcher took 489 seconds. The optional `SeoulVisualCaptureTest.DesignReviewCaptures` fixture was skipped because its capture environment variable was unset; its launcher `SUCCESS` label is not counted as an executed visual check. The Settings size check itself ran normally. Earlier failed resize checks are retained and were resolved by changing the production ScrollView sizing mode, not by removing the assertion.

Evidence: machine-readable result (`native/evidence/2026-09-09/settings-native/verification-summary.json`), complete log (`native/evidence/2026-09-09/settings-native/full-verification.log`), and source hashes (`native/evidence/2026-09-09/settings-native/source-inputs.json`).

## Sources

- Arc, [Profiles: Separate Work & Personal Browsing](https://resources.arc.net/hc/en-us/articles/19227964556183-Profiles-Separate-Work-Personal-Browsing), revisited September 9, 2026. Its linked historical screenshots establish the separate-window and profile-detail arrangement.
- Zen, [Workspaces and container tabs](https://docs.zen-browser.app/user-manual/workspaces), revisited September 9, 2026. Container separation and shared history/extensions inform the boundary description.
- Apple, [Human Interface Guidelines: Settings](https://developer.apple.com/design/human-interface-guidelines/settings), previously visually researched in the linked brief. Native window conventions inform routing and category presentation.
- Pinned Chromium source: `chrome/browser/ui/chrome_pages.cc`, `chrome/browser/profiles/profile_manager.h`, `chrome/browser/profiles/profile_observer.h`, `chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h`, `chrome/browser/themes/theme_service.h`, and `ui/shell_dialogs/select_file_dialog.h`. These establish the actual integration and lifetime contracts for milestone 149.
