# Existing-feature reliability repair — 2026-09-07

Status: in progress. The user's full reliability objective remains open.
No shipping-readiness or universal browser superiority claim is justified.

## Repairs and verification

- Handset uses a single native searchable device list with a bounded height,
  current-device state, keyboard selection, and explicit custom-size/rotate/
  desktop controls. Device actions run after the popup closes, and the popup
  observes the original page's lifetime and visibility.
- Catalog now has 48 supported presets, including current iPhone 17 models,
  Pixel 10 and an explicitly labeled Galaxy S26 Ultra QHD+ reference preview.
  Persisted older device IDs retain their original meaning. Sources and limits
  are recorded in `seoul-handset-spec.md` and the overlay JSON.
- Windows Phone compatibility UAs no longer become Android presets; Galaxy Tab
  S4 is categorized as a tablet.
- Boost editing now closes when the originating page hides, navigates or is
  destroyed. Queued menu commands are invalidated at that boundary so an old
  editor cannot continue applying actions after a tab switch.
- Real pointer tests cover handset toolbar entry, current-device selection and
  desktop return, and repeated tab selection across expanded/collapsed layouts.

## Memory evidence and its limits

The isolated headless component development build was measured using macOS
`proc_pid_rusage` physical footprint for only its own browser process tree.
This avoids treating summed RSS as uniquely owned memory, but it is still not
an optimized shipping-build comparison. Puppeteer supplies automation flags,
including disabled background network and timer throttling; those differ from
normal user browsing. No Brave/Chrome superiority conclusion follows.

A blank-page run settled at 261.6 MiB after 5 seconds and 260.9 MiB after 10.
Three cycles opening ten additional blank tabs and then closing them settled
at 277.9, 248.9, and 249.3 MiB. The run does not show monotonically accumulating
memory after tab closure. Peak totals with eleven blank tabs were 615.9–680.5
MiB. Evidence: baseline measurement (`native/evidence/2026-09-07/reliability/memory-baseline.json`).

The 10-second breakdown was browser 136.3 MiB, three renderers 16.3/16.8/20.0
MiB, GPU 42.4 MiB, network 16.8 MiB, storage 12.4 MiB. A second isolated probe
confirmed only one page target (`about:blank`) despite three renderer processes.
The spare/preloaded renderer costs require attribution before any tuning; no
process-isolation or protection setting has been weakened to reduce a number.

## Validation log

- `build-3.log`: native app, browser tests, and handset unit target built.
- `handset-unit-1.log`: all 35 unit cases passed.
- `handset-browser-1.log`: 9/10 cases passed; catalog restoration assertion
  failed because the test used `InsertOrReplaceText("")`, which does not emulate
  deleting selected text. The corrected test sends a Backspace key. Keyboard
  device selection and closing on tab changes passed in that run.
- `interactions-final.log`: all 23 focused cases passed, including keyboard
  traversal of every catalog entry, native device selection, custom-size
  validation/cancellation, Boost controls/lifetime, 30 pointer tab selections
  across expanded/collapsed layouts, and isolated persisted-layout restart.
- `headful-interactions-1.log`: 3 visible-window pointer/keyboard cases passed.
  `headful-interactions-2.log`: 2 visible handset/custom-size cases passed.
- `static-1.log`: `npm run check` passed. Compiler-backed tests cover native
  units that the static syntax runner explicitly skips for generated headers.
- An optional handset snapshot helper produced cropped or black macOS images.
  It was removed from the interaction tests; those images are not published as
  visual evidence. Visible app inspection uses a separate disposable app/profile.

## Normal-startup failure reproduced after the focused tests

The normally launched app (PID 73033 at observation) showed an empty tab area
and no New Tab control with the user's collapsed, 500-DIP saved sidebar. After
expansion its accessibility tree included both `chrome://webui-toolbar.top-chrome/`
and a native Reload button. Creating a tab did not immediately restore the tab
list; later navigation did. The user was actively using that window, so further
reproduction uses disposable profiles. The normal profile was located under
`~/Library/Application Support/Chromium/Default`; it has not been rewritten.

The build's non-Chrome branding causes Chromium to apply its field-trial
**testing** configuration by default. In this pinned revision,
`WebUIReloadButtonStudy` enables InitialWebUI and experimental toolbar features
on macOS. Puppeteer's normal arguments disable WebUIReloadButton, and focused
browser tests disabled InitialWebUI, masking this product/test difference.
Source: `components/variations/service/variations_field_trial_creator.cc` and
`testing/variations/fieldtrial_testing_config.json` in the pinned checkout.

`disable_fieldtrial_testing_config = true` is now specified in the product's
baseline GN file. The native rebuild passed. Removing the experiments exposed
an independent initialization-order failure in the persisted-sidebar test: the
shell host was seeded from its own default expanded state after missing the
vertical controller's initial collapsed-state notification. Registration now
reads the live vertical controller, and the restart test passes with a real
pointer click on Expand sidebar.
This selects normal feature defaults rather than upstream test studies; it
neither removes site isolation nor disables browser protection defaults.

The separate visible verification app (`app.projectseoul.SeoulVerification`)
uses the same built component libraries and a disposable profile. Its only app
metadata changes distinguish it from the user's active process for UI targeting.
The confirmed process arguments include the explicitly seeded profile, no
`--disable-features` overrides, and no upstream testing studies. At initial
`about:blank`, the accessibility tree and screenshot show the active tab, New
Tab, Expand sidebar, and one Reload button. Clicking Expand moved navigation
into the sidebar; navigating to Example Domain exposed Boost, Phone view and
Shields. The actual user process has not been restarted.

A diagnostic launcher initially ignored its `userDataDir` option when
`ignoreDefaultArgs: true` was set. That earlier probe only verifies a fresh
default profile, not restored preferences. The corrected probe passes the
profile explicitly in `--user-data-dir` and records the actual argv. Product
smoke and preview launchers now follow the same explicit-argument rule and
retain normal feature defaults.

The broader integration run then reproduced a shutdown crash: BrowserView's
scoped observation outlived the organization-owned live-window provider. The provider now notifies its surviving scoped observers before destruction;
BrowserView, Canvas and runtime detach at that point. The rebuilt shutdown
regression passed, as did all 10 lifecycle state units, including two scoped
observers that outlive the provider. Patch 0046 passes the 46-patch apply/reverse
round trip against the pinned baseline and matches the active checkout.

Two additional regressions confirmed page-lifetime defects before their fixes:
Shields remained open after both navigation and a tab switch; delayed site-data
cleanup reloaded a newer page and erased an unsaved test draft. Shields now
closes, stops timers and rejects actions when its page changes. Cleanup retains
a weak reference to the original document and reloads only that document, with
Chromium's repost confirmation retained. Evidence before repair is
`page-lifetime-before.log`. Both regressions pass after repair, along with the
existing Shields settings and site-data deletion cases.

Visible native interaction checks selected Pixel 8 by filtered Enter, switched
the same phone tab to Galaxy S26 Ultra by pointer, rejected a non-numeric custom
width, accepted 500 by 900, and applied UPPER from Boost's real native menu.
The page's visible heading and body changed to uppercase. Boost's size/case
accessible names now include their selected values, matching the visible
controls. The rebuilt Boost interaction test passed in isolation, in the full
195-case browser run, and in the final seven-case regression run. Earlier label
checks failed before the test executable was rebuilt with diagnostics; those
failures are not attributed to focus interference.

Puppeteer also sets a desktop user-agent override when it creates a Page, even
when `defaultViewport:null` avoids its 800 by 600 viewport override. Consequently
Puppeteer-inspected phone user-agent values are not evidence about native
handset behavior. A raw launch and read-only CDP Runtime.evaluate check avoids
both overrides. In the visible disposable app, pointer selection created a
second tab with Galaxy S26 reference metrics (384 × 832, DPR 3.75, Android 16
mobile UA, five touch points), leaving the desktop tab unchanged. Explicitly
focusing search, verifying the single Pixel 8 result and pressing Enter changed
that same second tab to 412 × 915, DPR 2.625, Pixel 8 mobile UA and five touch
points. Rotate changed it to 915 × 412; Desktop restored desktop dimensions,
Mac UA and zero touch points. Evidence: `raw-phone-metrics-history.json`.
An earlier combined type/Enter tool call did not apply a selection; its search
field contents were not observed before Enter, so its cause is unconfirmed.
The verified sequence observes the filtered result before activation. The
native keyboard regression remains necessary in addition to that manual check.
The disposable verification browser was closed after these checks.

## Final reliability checkpoint

The full native browser run selected 195 cases (including restart setup cases),
finished in 337 seconds with no failures and no retries, and skipped the one
optional native screenshot-capture case. This run includes the repaired
collapsed-sidebar startup, provider shutdown, navigation-bound Shields/Boost,
phone picker and adblock cases.

A subsequent review found that site cleanup ignored the remover's failure mask,
including the failure notification sent during profile shutdown. Failed or
partially failed removal now returns its failure mask without rotating identity
or reloading the page. The native Shields panel waits for completion and exposes
an accessible error and confirmed retry path on failure. Its completion callback
is invalidated if the original page changes or the panel is destroyed.

Seven focused browser cases passed after those four final source files changed:
the two new cleanup-failure tests plus successful cleanup, newer-page draft
preservation, Shields navigation/settings and Boost editing. The actual native
retry interaction is tested with pointer events. Both identity assertions now
compare full 64-bit tokens; the previous 32-bit success assertion could pass
because of truncation rather than an identity change. All 10 lifecycle-state
units, all 35 handset units, the static checks, product smoke and actual assistant
preview also passed. Static syntax skips are recorded and are not treated as
native compilation coverage.

The final memory run used the same isolated Puppeteer harness as the earlier
baseline: one blank tab measured 239.7 MiB at five seconds and 238.8 MiB at ten.
Eleven-tab totals were 590.5, 555.0 and 557.6 MiB; after closing the ten extra
tabs, totals settled at 254.1, 220.6 and 222.1 MiB. These single runs show lower
measured footprint and no monotonic retained growth during the three cycles;
they do not establish causation, long-session stability or shipping performance.
The build still uses shared development libraries, and no security boundary was
weakened to obtain these numbers.

Durable evidence is in the run manifest (`native/evidence/2026-09-07/reliability/run-manifest.json`).
It includes logs, before/after memory records, read-only native phone metrics,
46-patch round-trip proof, 825 source-file hashes and build-artifact hashes.
Separate source manifests distinguish the full-suite snapshot from the four-file
cleanup-failure follow-up. The current source has not been committed or pushed.
The user's already-open browser was preserved; its loaded code will only change
when Seoul is restarted.

## Follow-up: actions, dialogs and Boost document state

Nine source files changed after the checkpoint above. The source hashes and
results in the earlier manifest remain historical; the follow-up evidence is
kept separately under `native/evidence/2026-09-07/actions/`.

Four phone regressions failed before repair: a queued preset or custom-size
action could create another tab and take focus after the user switched tabs,
and the custom-size dialog survived navigation or a tab switch. Queued actions
now retain the original document, revalidate its visibility before applying,
and close the custom dialog when that document leaves. All 18 handset browser
and custom-dialog cases passed after this repair.

Boost rename now opens a browser-parented dialog after dismissing the editor,
retains the original document and layer ID, and reads the latest layer on save.
It trims names, rejects empty or over-limit UTF-8 names, and closes if its page
changes. The native regression covers a Unicode name, validation, Enter to
save, Escape to cancel and navigation. The separately launched visible app
also saved a name with its actual Save button and displayed it after reopening.
The CUA text entry did not preserve an attempted emoji, so that visible check
is evidence for a plain-text name; Unicode is covered by the native test.

Two further regressions reproduced real Boost failures: a saved script ran
eight times after five refreshes and a tab switch, and Zap still intercepted a
page button after switching away and back. The applicator now stores applied
CSS, tint and author-program state on Chromium's document lifetime. Unchanged
refreshes do not reinstall styles or rerun scripts; ordinary pages allocate no
Boost document state and receive no style/author-script application work.
Author code waits until the DOM is complete. Editing code or explicitly
turning it off and back on permits one new execution; arbitrary prior script
side effects are not automatically reversible.

Document state survives back/forward caching. A regression verifies that the
same restored document loses its style when Boosts were disabled in history,
reapplies once when enabled, and does not replay unchanged code on another
back/forward cycle. Zap cancels when its tab hides, removes its temporary layer
when appropriate, and releases the page's normal input handlers.

Research references: [Arc's documented Boost controls and rename flow](https://resources.arc.net/hc/en-us/articles/19212718608151-Boosts-Customize-Any-Website),
and [Chromium's documented DOM-ready script timing](https://developer.chrome.com/docs/extensions/develop/concepts/content-scripts).
Document ownership follows the pinned Chromium `content/public/browser/document_user_data.h`
contract, including retention across back/forward cache and deletion with the
document. This is a behavior repair, not a claim of visual parity with Arc.

Verification details and test-harness failures are recorded in the follow-up
manifest. In particular, the original rename test clicked a sheet before its
native lifecycle was synchronized; final automation uses keyboard submission
and waits for complete sheet destruction. A history test also initially tried
to modify the frozen test DNS resolver; using the local server's normal URLs
removed that test-setup error without changing product behavior.

The first wider follow-up run selected 205 cases and failed three: one tint
case crashed on a missing page body, and two sidebar tests assumed a focused address field stayed docked. The new
streaming-response regression reproduces the tint defect deterministically:
an overlay appended directly to `document` became its `DIV` root before the
HTML parser received the page, preventing a normal body from appearing. Tint
now waits for the actual HTML root; Zap also refuses to start until that root
exists. The completed page receives one tint overlay after DOM readiness.
The sidebar tests now navigate to a real page, leave editing through blur
before checking docked identity, and use the Cmd+L command path to verify that
address focus floats without pinning the rail. Tab focus still has to expand
the rail. Separate startup-surface tests retain their original coverage.

The final sidebar test sequence also initiates real address focus before
assigning text. Its earlier synthetic edit left a floating but unfocused
field, so clearing focus could not exercise the blur callback. Both corrected
focus cases pass in `focus-real-input.log`; this is test-setup repair, not a
claim that those two checks revealed a new product defect.

Final follow-up validation: the complete native suite selected **206 cases**
(including restart setup), finished in **336 seconds**, and passed with **zero
failures and zero retries**. The optional native screenshot-capture case was
skipped because its output directory was not configured. The normal-startup
product smoke passed in 4,372 ms. All 16 non-syntax static guards pass on the
final sources; the earlier full static run passed with 206 parsed files and 91
code-generation skips. All changed C++ files compiled in the final native
build. Final materialization, patch overlap/manifest checks and `git diff
--check` pass. The 825 source hashes and 529 build-artifact hashes remained
unchanged throughout the final full run. Evidence: follow-up run manifest (`native/evidence/2026-09-07/actions/run-manifest.json`).

## Follow-up: Boost code editing and preservation

Two more data-loss defects were reproduced with failing native browser tests:
resetting the last appearance adjustment deleted a code-bearing Boost, and
saving through the Library erased CSS and JavaScript omitted from its form.
Both paths now preserve authored code. The Library merges the latest stored
code on the browser side rather than copying an earlier frontend snapshot.

A Code control in the native Boost footer opens separate CSS and JavaScript
tabs. Save applies the draft; Cancel changes nothing. Clear uses native Delete,
with Undo support. The editor enforces each language's 64 KiB UTF-8 bound,
preserves concurrent appearance/name/pause changes, rejects competing code
edits and deleted layers, and closes when its source page changes or hides.
JavaScript remains disabled by default, with an explicit device-level checkbox
committed only on successful Save. Preference changes refresh open pages.
Disabling author code cannot undo arbitrary effects of an already-run script;
the editor explains the reload requirement and possible site-security limits.

All six focused native cases pass, covering the live page, native controls,
and the Library's Mojo handler. The new fixture is included in the regular
browser-suite filter. Detailed logs and final full-suite status are in the
Boost code checkpoint (`native/evidence/2026-09-07/boost-code/README.md`).

The final browser suite passed all 210 selected cases, including PRE setup,
in 354 seconds with zero failures, crashes, or retries. The optional visual
capture remains skipped without its capture directory. `npm run check`, four
frontend Boost tests, and the normal-default startup smoke check (2349 ms)
passed. Hashes of 827 source files and 529 build artifacts were unchanged
during the full run; all source mirrors match. All 46 patch files are unchanged
from the earlier complete apply/reverse proof. No new optimized-build memory
or competitor performance comparison was performed.

Visible verification remains open: CUA reported the Mac was locked. Only the
disposable verification browser was closed; the normal browser/profile was
preserved. No new physical mouse/keyboard pass, screenshot, syntax-aware IDE,
visual parity, or release-readiness claim is made for this editor.

## Remaining product gates

Native menu/selection regressions under sustained real usage; visible Boost
code-editor verification, a general settings entry for profile-wide Boost
enablement, accurate Library summaries for code-only/globally disabled layers,
and wider real-site author-code coverage; current physical-device fidelity;
real phone handoff and terminal/IDE journeys; provider/microphone task journeys;
long-session performance and memory in an optimized build; login/media/adblock
coverage; maintained security baseline, signing/notarization and update delivery.

Earlier repair evidence is in `seoul-visual-and-blocking-revision.md`. It remains
useful history, but its hashes and test counts do not certify this later source.
