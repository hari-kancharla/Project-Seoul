# Seoul: native reliability repair and verification

This pass follows the user's direction to repair existing behavior before continuing the Settings redesign. It addresses reproduced creation, focus, and Space-switching failures. It does not certify the entire browser for release.

## Repairs

- **Repeated New Tab preserves input.** Pressing Command-T while its input was already open previously dismissed it and sent subsequent typing back to the underlying page. It now keeps that surface open and focuses the existing input without discarding the query. Escape still dismisses it; Enter creates the foreground tab.
- **Creation has a dedicated menu.** The footer creation control offers New tab, New space, and New container space. It no longer opens the full command-search surface. Its New tab action routes through the same window-owned browser command as Command-T, preserving the source page until a destination is submitted. Internal empty-Space initialization still creates its actual first tab directly. Commands execute after the menu has released capture and completed its close path. Pending callbacks are invalidated if the footer is rebound to another controller.
- **Naming starts in the name field.** Space and Boost naming dialogs explicitly select their initial text field. Native testing exposed a case where merely showing the dialog left the field unfocused; waiting did not resolve it.
- **An empty Space gets its own first tab.** Creating a container previously selected the new Space in the model while keeping the old Space's page active, leaving a persistent recovery banner. After a successful switch to an empty destination, Seoul now creates its first tab through the existing partition-aware command path. The old Space's page and membership remain intact. Other projection failures retain the recovery behavior.
- **Space activation is confirmed against current state.** The live-tab bridge delivered activation confirmation before publishing the updated active-tab snapshot. The switcher could therefore reject a successful activation as a failure. The bridge now publishes the actual selection before confirming it. The native creation regression exposed this when returning to a container after visiting the original Space.
- **Command rows and focus geometry.** The command result viewport ends on a whole row. The address field's focus path uses the same rounded-rectangle geometry as its field instead of a pill outline.

The native regression flow clicks the creation-menu item with a pointer, opens the naming dialog through keyboard selection, types its name, accepts it, checks a non-default storage partition, and switches to the original Space and back without creating extra tabs. Asynchronous dialog activation and Space switching are awaited; assertions about focus, isolation, tab counts, and coherence remain mandatory.

## Research and method

The [Settings research brief](seoul-settings-research-and-direction-2026-09-08.md) records the Arc and Zen references, what their interfaces actually do, and the proposed replacement for Seoul's restyled Chromium Settings. That redesign remains deferred; the interactive proposals are not production Settings.

For native interactions, the implementation was checked against the pinned Chromium sources for MenuRunner, DialogModel initial focus, modal dialogs, and fixed storage partitions. Menu selection and browser commands were tested through real Views events in disposable profiles. The visible application is a separate verification bundle using the same built component libraries. The installed application and the user's browsing profile are not used as test fixtures.

The current handset catalog already includes the Galaxy S26 Ultra, so this pass does not add a duplicate or relabel an older model. Samsung announced the S26 family in February 2026. The catalog calls its chosen dimensions a QHD+ preview; a preview preset is not a claim that every physical device ships with that browser viewport. [Samsung announcement](https://news.samsung.com/uk/samsung-unveils-galaxy-s26-series-the-most-intuitive-galaxy-ai-phone-yet).

Memory is measured using macOS `proc_pid_rusage` physical-footprint accounting for the browser's processes, with JavaScript heap reported separately. Summing process RSS can count shared mappings multiple times. An initial diagnostic attachment also imposed Puppeteer's default viewport; those samples were discarded for native presentation assessment. The blank-tab memory samples used `defaultViewport: null` to preserve native geometry. A second diagnostic artifact was found: Puppeteer still sends a desktop user-agent override when attaching. Therefore handset identity acceptance used a separate raw CDP client that issues only Target and Runtime reads, with no Network or Emulation overrides; the earlier Puppeteer handset readings are excluded. Chrome's own guidance distinguishes operating-system memory from live JavaScript heap. [Chrome memory diagnostics](https://developer.chrome.com/docs/devtools/memory-problems).

## Native interaction and memory evidence

The activation-fix build was exercised in a disposable profile through native macOS controls. The later menu-routing alignment is verified by the native event-driven regression; handset and memory measurements below predate that routing-only change. Repeated Command-T retained the unfinished query, the creation menu opened the naming dialog with the field focused, and a newly created container could return to the original Space and back without a recovery banner.

The handset selector contained 48 entries; searching S26 returned the Galaxy S26 Ultra preset. Its phone tab reported a 384 × 832 CSS viewport and screen, DPR 3.75, coarse pointer, Android 16 / SM-S948W in both user agent and client hints. Rotation changed the viewport to 832 × 384 while retaining typed input and the fixture's click count. Desktop return restored a 1034 × 844 viewport, DPR 1, fine pointer, and macOS identity. Desktop return reloads the document as the user agent changes, so the fixture's form input resets. The original desktop tab remained available throughout. These results cover one preset and local fixture, not all mobile websites.

| Latest native blank-tab cycle | Physical footprint, MiB | JavaScript heap, MiB |
| --- | ---: | ---: |
| One tab | 221.55 | 1.050 |
| Ten tabs | 402.30 | 10.499 |
| Back to one tab | 238.09 | 1.062 |

This is one cycle in a component development build, without forced garbage collection. Memory was substantially released after closing tabs; the remaining difference does not by itself prove either a leak or leak freedom. It is not a benchmark against Brave or a release-build performance claim.

Raw readings and the method are recorded in native phone checks (`native/evidence/2026-09-08/reliability/native-pages-raw.json`), memory samples (`native/evidence/2026-09-08/reliability/native-memory.json`), and native interaction observations (`native/evidence/2026-09-08/reliability/native-interaction-observations.json`).

## Live YouTube: corrected verification passed for one video

The official JISOO “CLICK” video used in the user's examples completed its full **168.781 seconds** in Seoul with default Shields enabled. The corrected run used a new signed-out disposable profile, no seeking, and no user-agent or device overrides. Samples showed advancing main-video time with no visible ad containers or `ad-showing` player state. A read-only event observer captured the natural `ended` event at the full duration, with no player error. A fresh Chrome 152.0.7977.84 comparison played ads first and then completed the same main video. This is evidence for this video and session, not a guarantee about every ad variant or site.

**The earlier failure reports need a methodology correction.** All earlier Seoul, Chrome, and Brave comparisons used `--remote-debugging-port=0`. Chromium treats that as an automation launch and sets `navigator.webdriver` to true; disconnecting the diagnostic client does not undo that startup state. This was confirmed in the pinned Chromium source, `content/child/runtime_features.cc:481–497`, and matches the [documented browser behavior](https://developer.mozilla.org/en-US/docs/Web/API/Navigator/webdriver). Those runs repeatedly stopped near 44–46 seconds. They are retained as failed diagnostic runs, but are excluded from normal-browsing acceptance and cannot establish a Seoul-specific playback defect.

The repeat launch reserved an available local port and supplied its actual nonzero value to Chromium, leaving its normal automation state unchanged. Both watch pages explicitly reported `navigator.webdriver = false`. Raw CDP was used to read state; it did not override browser identity. Clients detached between the initial samples, then used temporary `ended` listeners near completion. Seoul and Chrome both passed the earlier failure point and finished naturally. This result supersedes the cross-browser failure conclusion; it does not prove exactly which YouTube code path caused the original automated runs to stop. No production change was made to hide automation or alter YouTube's player.

The downloaded rules were also checked against current [uBlock quick fixes](https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/quick-fixes.txt) and [Brave Unbreak](https://raw.githubusercontent.com/brave/adblock-lists/master/brave-unbreak.txt). Some upstream rules require trusted scriptlets that Seoul does not authorize from runtime feeds. The corrected playback pass does not establish that those rules are needed for this video, and no speculative scriptlet-permission change was made.

The Seoul samples (`native/evidence/2026-09-08/reliability/youtube-seoul-interactive.json`), Seoul completion event (`native/evidence/2026-09-08/reliability/youtube-seoul-interactive-end.json`), Chrome samples (`native/evidence/2026-09-08/reliability/youtube-chrome-interactive.json`), and Chrome completion event (`native/evidence/2026-09-08/reliability/youtube-chrome-interactive-end.json`) retain the corrected evidence. The Seoul playback screenshot (`native/evidence/2026-09-08/reliability/youtube-seoul-interactive.png`) shows content playing beyond the earlier failure point. Launch arguments and the inspection scripts are retained alongside the results. Older traces contain query-stripped network results; signed media URLs are excluded from the retained evidence.

## Verification status

The final build completed before tests began, and the complete verification command exited **0** against the recorded source hashes. The browser launcher finished in 458 seconds with no failures. Its 221 executions comprise 214 completed test cases, 6 successful `PRE_` setup executions, and 1 opt-in screenshot-capture fixture skipped because `SEOUL_CAPTURE_DIR` was unset. The launcher labels that fixture `SUCCESS`, so its `result_parts` and output were inspected to identify the actual skip.

| Gate | Result |
| --- | --- |
| Settled native build and materialized overlay verification | Passed before testing |
| 34 native unit-test binaries | 864 cases passed |
| Seoul browser suite | 214 cases + 6 PRE setup executions passed; 1 opt-in capture fixture skipped; 0 failures |
| 51-patch scratch apply/compare/reverse | Passed; 240 patched files matched the active checkout and 216 baseline files restored exactly |
| Repository CI | Passed (`npm run ci`) after the native suite |
| Whole working-tree whitespace check | Passed |
| Native input, Space switching, and S26 fixture checks | Passed within the scope above |
| Live YouTube playback | One complete 168.781-second video passed in Seoul and Chrome after correcting the diagnostic launch; broader coverage remains open |

The portable syntax gate's generated-header skips are recorded separately from the real native build and tests. Earlier failing regression runs remain in the temporary work directory and are not described as passes. No libraries were rebuilt while a test suite or the native verification application was using them. All 792 recorded native source inputs remained unchanged during the final full run.

See the machine-readable result (`native/evidence/2026-09-08/reliability/verification-summary.json`), complete build/test/CI log (`native/evidence/2026-09-08/reliability/full-verification-final.log`), and browser launcher output (`native/evidence/2026-09-08/reliability/full-browser-summary.json`).

## Remaining release work

**Update the Chromium release baseline before distribution.** Seoul's lock file is pinned to 149.0.7827.201, resolved June 26, 2026. On September 9, the official [Chromium Dash Mac stable feed](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Mac&num=1) reported milestone 152 / 152.0.7977.83. The installed comparison applications also use milestone 152. The current pass repairs the existing pinned build; it does not rebase the 51-patch integration or establish that milestone 149 contains subsequent stable security fixes. Updating and validating that baseline is a release prerequisite.

Passing deterministic browser tests does not establish that every live advertising variant is blocked, that all websites render correctly in every phone preset, or that memory remains bounded over a long browsing session. Those claims require separate live-site and sustained-workload evidence. The Settings replacement, broad visual acceptance, and final release packaging also remain separate work.

No commit or push is part of this pass.
