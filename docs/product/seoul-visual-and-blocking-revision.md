# Seoul visual and blocking revision

September 7, 2026. Based on `687c18d`.
The earlier implementation was rejected by the user. This revision changes the
actual native browser and compiled WebUI; passing tests do not establish visual
acceptance or public release readiness.

## What was wrong

The Boost editor reproduced a checklist of features but missed Arc's compact
composition, visual font selection and progressive disclosure. Its native Size
control also wrote a selector-dependent adjustment without a selector, so the
backend rejected the change. Explicit per-site dark mode incorrectly depended
on the browser's own dark appearance.

The collapsed sidebar had no direct way back to labelled tabs. A Space without
an assigned icon rendered as an empty outlined square. New Tab text could be
elided in the narrow rail, and macOS material diluted the requested sky tint.
The user also reported a completely empty tab area; that exact startup failure
has not reproduced in isolated restart tests and remains an open investigation.

The assistant exposed an idle "Text ready" status and a long tool menu. The
context graph recreated its scene during selection and presented too much
secondary list content. The green identity conflicted with the requested sky
blue direction.

Seoul used Brave's network matching engine but supplied only four DOM scriptlet
templates. It lacked the resource library, early page-world execution and
response handling needed to prevent player ads. Standard mode discarded native
CSS `:has()` selectors used for sponsored cards. The fallback reacted late and
could leave content muted.

## Research translated into the product

- [Arc's visual walkthrough](https://start.arc.net/paint-the-internet-with-boosts)
  and [official Boost controls](https://resources.arc.net/hc/en-us/articles/19212718608151-Boosts-Customize-Any-Website):
  inspected the actual full-resolution animated editor reference. Implemented a
  260-DIP panel, square color field with a saturated perimeter, connected large
  background/small text handles, three equal utility buttons, a 5-by-4 grid of
  real system-font previews, compact Size/Case menus and a full-width Zap action.
  Advanced sliders appear only when requested. Native focus, keyboard selection,
  persisted settings and reset use the actual backend. This is an independently
  implemented Arc-inspired editor, not a claim of pixel-identical reproduction.
- [Zen Compact Mode](https://docs.zen-browser.app/user-manual/compact-mode):
  browsing remains primary; assistance and supporting destinations are optional.
  Existing layout preferences are preserved. The reported profile had saved
  the fixed collapsed layout and a 500-DIP expanded width. That explains the
  narrow rail/top toolbar arrangement, but does not establish the cause of its
  missing tabs. The rail now exposes an explicit Expand sidebar control that
  restores labelled tabs and integrates navigation into the sidebar.
- [Obsidian graph](https://obsidian.md/help/plugins/graph): added deterministic
  force layout, stable positions/camera during inspection, connected-neighbor
  highlighting, filters/search, local depth, pan/zoom, expand and real-item
  navigation. The overview is bounded at 600 nodes/1,800 edges with truncation
  disclosed. These are browser/workspace relationships, not inferred semantic
  understanding of everything a user reads.
- [Brave's cosmetic handler](https://github.com/brave/brave-core/blob/master/components/cosmetic_filters/renderer/cosmetic_filters_js_handler.cc)
  and [scriptlet resources](https://github.com/brave/adblock-resources): imported
  pinned source implementations and dependencies, retained licensing metadata,
  split page/isolated-world execution, and installed resources synchronously at
  document start. Runtime feeds still receive no trusted-scriptlet permission.
- [Current uBlock quick fixes](https://ublockorigin.github.io/uAssetsCDN/filters/quick-fixes.txt)
  and [filters](https://ublockorigin.github.io/uAssetsCDN/filters/filters.txt):
  network matching alone does not cover same-origin player setup. Native CSS
  `:has()` now works in Standard mode. Baseline rules remove known player ad
  metadata in initial JSON, fetch and XHR responses, including array-wrapped
  `get_watch` player responses used by same-document navigation.

## Implemented visual and interaction changes

Sky blue now spans the native palette, toolbar Boost glyph, application assets,
onboarding, assistant and graph, with separate readable light/dark colors.
Explicit user-authored Space accents are retained. The assistant removes the
idle status row, keeps the composer reachable, uses a compact six-destination
tool grid and renders page outlines without internal interaction handles.

Boost renders the color field at the display's actual pixel scale. Font arrows
move within the real grid and apply the selected family. Size writes an explicit
selectorless page-scale adjustment; older selector-specific font scaling remains
compatible. Explicit per-site dark mode works independently of application
appearance. Menu commands update the bubble after native menu dispatch unwinds.
Disabled Boosts are identified in the header. Reset, Undo Zap and close retain
real behavior rather than decorative controls.

The narrow sidebar now hides New Tab text, retains a persistent active-tab
marker, and uses a workspace glyph for unnamed icons. The expanded sidebar
keeps native tab titles, navigation and site controls together. A stronger sky
tint retains a small amount of macOS material translucency. A restart regression
covers the saved collapsed layout, visible tabs/New Tab, the expansion action,
and restoration of the New Tab label. The running window was later inspected
through native accessibility and a screenshot with four visible labelled tabs;
that observation does not prove the earlier startup defect is eliminated.

The player fallback observes inserted controls and media state, coalesces
mutation work, bounds scanning and restores the user's prior mute state.
Changes to Shields expose a same-origin reload action because already installed
page-world hooks cannot be reliably removed from a live JavaScript realm.

## Live playback findings

A first successful video was insufficient: the earlier revision entered an ad
and stalled on a real recommendation click. Inspection identified ad metadata
under `[].playerResponse` in `/youtubei/v1/get_watch`. That missing path was
repaired and covered through the shipped baseline in a native browser test.

A fresh anonymous profile then completed these real journeys:

| Journey | Time to stable playable content | Sampled player ads | Visible sponsored cards | Ordinary recommendations |
| --- | ---: | ---: | ---: | ---: |
| HRE Factory and Design Tour, initial navigation | 2,528 ms | 0 | 0 | 20 |
| I Bought a Non Running Bugatti Veyron, recommendation click | 5,545 ms | 0 | 0 | 20 |
| I Rebuilt a Wrecked Ferrari Then Gave It to My Dad, recommendation click | 5,650 ms | 0 | 0 | 20 |

Measurement required the target video ID, a playing/non-ad player, sufficient
ready state and advancing media time. It reset on an ad, pause or backward time
jump and continued observing playback. Environment: local build, headless,
anonymous fresh profile, autoplay enabled for measurement. Ads are variable;
three observed journeys establish neither Brave parity nor universal coverage.
Recommendation transitions still took over five seconds in this run.

## Verification record

Evidence directory: `/tmp/seoul-repair-20260907`.
The final native run completed against the materialized source and product
libraries. Earlier failing logs remain in the evidence directory.

| Check | Result | Evidence |
| --- | --- | --- |
| Native unit tests | 858 passed across 34 binaries | `sidebar-native-final.log` |
| Native browser cases | 187 selected cases, zero failures/retries; 333 seconds | `sidebar-native-final.log` |
| Native visual fixture | Passed separately with capture enabled; normally skips in the headless suite | `sidebar-visual-final-blue.log` |
| Restart/expand and renderer focus run | 8 passed | `sidebar-controls-final.log` |
| Repository consistency | `npm run check` passed; 206 sources parsed, 91 need generated build headers and are covered by the real build | `sidebar-static-final.log` |
| JavaScript/prototype tests | 89 passed before the final native-only sidebar changes | `revision-npm-final.log` |
| Launch and local navigation smoke | Passed; 3,563 ms total | `sidebar-smoke-final.log` |
| Tab/history/assistant churn | Passed; 15 remounts, 12 tabs, 60 activations, 8 heavy-page/history cycles; JS heap 3.3 to 3.3 MB | `sidebar-stress-final.log` |
| Chromium patch round trip | 45 patches; 214 patched files match the checkout; 192 baseline files restored exactly | `sidebar-roundtrip-final.log`, `patch-roundtrip.json` |
| Manifest and overlap | Passed | `sidebar-manifest-final.log`, `sidebar-overlap-final.log` |

The earlier integrated browser run caught two renderer timing failures. The
repair drains pending discovery mutations, runs discovery/procedural filtering
at DOMContentLoaded, and waits for the actual browser response and computed
style in the generic-filter regression. All six renderer cases passed both the
focused and final integrated runs.

Saved screenshots: expanded sidebar (`native/evidence/2026-09-07/sidebar-expanded.png`),
collapsed sidebar (`native/evidence/2026-09-07/sidebar-collapsed.png`),
Boost (`native/evidence/2026-09-07/boost-editor.png`),
dark Boost (`native/evidence/2026-09-07/boost-editor-dark.png`),
assistant (`native/evidence/2026-09-07/assistant.png`), and
context graph (`native/evidence/2026-09-07/context-graph.png`).
The assistant/graph images use representative fixture data; real browser graph
integration is independently covered by the native tests.

The interactive review window uses an isolated profile at
`/tmp/seoul-repair-20260907/sidebar-preview-profile`. The normal user profile
was not replaced or rewritten. An already running process must be restarted
to load the rebuilt native libraries. The earlier empty-window process is no
longer running; its original startup defect has not been reproduced.

Visual inspection covered the entire native Boost panel in light/dark modes,
actual font/color state, assistant tool menus and graph at desktop and 390-pixel
width. The 12-node representative WebUI fixture rendered in 9.3 ms with no page
JavaScript errors in that capture run. It is fixture timing, not a general
performance benchmark. Dense graph behavior is separately exercised in the
native browser suite. Captures show real native widgets and browser content.
An earlier full-window capture caught macOS’s window reveal animation; the
fixture now waits 350 ms for it to settle before capturing. The settled
collapsed and expanded window captures are included with the final evidence.
The article inside those browser captures is explicitly labelled as a local
layout test page; it is not a mock browser shell.

## Remaining release gates

- Reproduce and eliminate the reported empty tab area on startup with a restored
  profile. A passing isolated restart test is not sufficient to close it.
- Update Chromium 149.0.7827.201 to the current supported security baseline,
  migrate and verify the complete patch stack. The observed current stable
  release is [152](https://chromereleases.googleblog.com/2026/09/stable-channel-update-for-desktop_01882797386.html).
- Signed/notarized distribution and a working update path; the local app is a
  development build. Complete the [dependency distribution review](../research/native-adblock-filter-licensing.md).
- Real provider-account and microphone journeys: cancellation, interruptions,
  safe action completion, error recovery, latency and enforced usage budgets.
- Real terminal/IDE sessions and device-to-device handoff remain separate work.
  The current phone picker is bounded device emulation, not phone handoff.
- Broader authenticated/anonymous ad, login, media, accessibility and long-session
  testing. Keep last-good individual filter subscriptions on partial update
  failures; the current aggregate fallback does not prove that guarantee.
- Resolve remaining upstream search attribution (`source=csChrome`) separately
  from browser identity. Do not rewrite existing user search preferences blindly.
- Test with intended users and measure completion of a focused core workflow.
  Site CAPTCHAs cannot be promised away. Do not market the current build as
  flawless, universally liked, production ready, or superior to every browser.
