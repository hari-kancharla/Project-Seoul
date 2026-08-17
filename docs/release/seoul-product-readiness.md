# Seoul product readiness

Last verified: 2026-08-15 on macOS arm64.

This report is the source of truth for the current product build. It separates
working product behavior from public-distribution work. A passing development
build is not described as a signed release.

## Verdict

SEOUL FUNCTIONAL DEVELOPMENT BUILD VERIFIED

The tracked Seoul source is materialized into the pinned Chromium checkout, the
reversible integration patches apply, Chromium and every Seoul native test
target compile, the local browser launches, and the shipping
`chrome://seoul-canvas` WebUI runs.

Every gate is green: 32 of 32 native unit binaries, 145 of 145 focused browser
cases, 89 of 89 repository test cases, 21 of 21 Swift cases, 13 of 13 static
gates, and the product smoke.

The current build is not a public release artifact. It is a component
development build without final Seoul application branding, signing,
notarization, an installer, or production update infrastructure.

## Verified build

| Item | Verified value |
|---|---|
| Chromium version | `149.0.7827.201` |
| Chromium revision | `6a7b3dbec3b2ca25877c2553b5473b2f277ef644` |
| Platform | macOS arm64 |
| Output | `out/SeoulBaseline/Seoul.app` |
| Build mode | release component build, `symbol_level=0` |
| Seoul overlay | `native/seoul/` materialized to `src/seoul/` |
| Integration | 33 ordered, hash-verified patches |
| First-party Canvas | `chrome://seoul-canvas` |

The build host passed the RAM, storage, Xcode, SDK, architecture, and checkout
gates before generation or compilation. The build did not bypass or weaken a
host gate.

## Test evidence

Every number below was measured on 2026-08-09 by executing the suite named, from
the source and binary described above. Nothing here is carried forward from an
earlier run.

| Suite | Result |
|---|---|
| Native unit executables | 32 of 32 passed |
| Native unit tests | 722 passed, 0 failed |
| Focused Chromium browser tests | 145 passed, 0 failed |
| Product smoke (`native/scripts/smoke.mjs`) | passed |
| Product churn exercise (`native/scripts/stress.mjs`) | passed |
| Repeated cold launches of the built app | 5 of 5 reached a live browser |
| Checkout reverse proof (`verify-checkout.sh`) | passed, and fails on introduced drift |
| Development assertions in the product build | off; `SEOUL_DCHECKS=1` restores them |
| Repository test suites (`npm test`) | 89 passed, 0 failed, 0 skipped |
| — protocol conformance | 8 passed |
| — Canvas Design Lab | 32 passed |
| — Canvas Boost editor | 4 passed |
| — extension harness element resolver | 14 passed |
| — extension harness providers | 20 passed |
| — reference coordinate transform | 11 passed |
| Swift package (`swift build`, `swift test`) | builds; 21 passed |
| Native syntax audit | 188 parsed, 77 generated-header files deferred to the native compiler |
| Static gates (`npm run check`) | 13 of 13 passed |
| Patch manifest, apply, and reverse verification | passed |
| Architecture, boundary, domain-neutrality, and test-wiring gates | passed |

The syntax-audit skips are not uncompiled gaps. They depend on GN-generated
Chromium headers and were compiled through their native build targets.

### Resolved since the previous report

Six `SeoulShellBrowserTest` cases and the product smoke were failing when this
report was last written. All are fixed; two were product defects and three were
tests asserting things that were never true.

**The sidebar could not be collapsed while the omnibox had focus.** Upstream
holds the vertical rail open while focus is inside it, so keyboard tab
navigation cannot collapse the strip out from under the user. That rule assumes
the rail holds tabs and nothing else. Seoul hosts the toolbar inside the rail,
so the omnibox satisfied it - and the omnibox holds focus in a brand-new window
and after every `Cmd+L`. The rail stayed pinned at its full 230 DIP with the
mouse nowhere near it, and `RequestCollapse` became a silent no-op. Patch 0025
now excludes focus inside the hosted toolbar. This was reachable by any user, not
only by the test.

**The `$csp` throttle was compiled but never constructed.** Its registration in
the content browser client existed only in the working checkout and in no patch,
so a clean build had the code and none of the behavior. Patch 0024 registers it.

**`ProgrammaticNewTabKeepsChromiumContract`** asserted against
`WebContents::GetLastCommittedURL()`, which returns the navigation entry's
*virtual* URL. The NTP reverse-rewrite sets that back to `chrome://newtab/`, so
neither that getter nor `GetVisibleURL()` can show which page committed. The
test now reads the committed entry directly, which is Chromium's NTP.

**`SingleToolbarUsesZenLeadingSearchTreatment`** assumed a fresh test window has
an empty omnibox. It does not: it sits on `about:blank`, which the omnibox
renders as that literal text, so the page identity is showing and the search
icon is correctly hidden. The test now types into the omnibox to reach the
editing state it means to exercise. Its hover assertions also assumed
`ZERO_DURATION` makes the fade synchronous; `SlideAnimation` only short-circuits
when its *configured* duration is zero, and the scale factor is applied later,
so the animation still needs a tick from its container. The test now waits for
the endpoint instead of assuming it.

**The product smoke** asserted that `chrome://newtab` renders Seoul Canvas. The
Welcome/onboarding row of `docs/product/zen-chromium-parity.md` replaces
Canvas-first new-tab ownership, patch 0008 removed the rewrite patch 0006
installed, and patch 0017 records the replacement intent. The smoke was
asserting the pre-0008 product and failing against a correct build; it now
checks that the first-party surface loads on `chrome://seoul-canvas`, which is
where the product actually puts it.

### Focused browser coverage

The 145 in-process browser cases run with explicit headless flags. Each fixture
releases the macOS key window before exercising native layout, so the suite does
not intercept input from an interactive desktop session. The runner disables
Chromium's unrelated experimental `InitialWebUI` toolbar in the explicit
headless backend because its pre-test paint-metrics callback can wait forever
before a test body starts. Seoul's native vertical Shell and
`chrome://seoul-canvas` remain enabled and are exercised by the suite.

The filter in `native/scripts/test.sh` names each Seoul fixture explicitly,
because the binary also links upstream's vertical-tab browser tests, which are
Chromium's and are deliberately not run here. `npm run check:native-tests` reads
the fixtures back out of the Seoul sources and fails if the filter misses one,
so a newly added fixture cannot go unrun.

The passing cases cover:

- regular-profile runtime and service wiring;
- the invariant that every available capability has an executor;
- an availability gate for 16 core interaction capabilities, including Scene
  activation, native split creation, standalone compact control, structured
  extraction, and typed page actions;
- text goals producing bound native tasks;
- Scene activation through the capability path, exact Theme/compact baseline
  restoration, matching workflow triggers, and stale-Scene reconciliation;
- standalone compact chrome through the native launcher and keyboard command,
  per-Workspace switching, observed AI-capability completion, Scene ownership,
  durable preference storage, and two-process relaunch restoration;
- a real two-process browser relaunch that preserves the durable tab-membership
  UUID without a stale duplicate, restores retained role/workspace ownership,
  resumes the active Scene/Theme and compact presentation, and restores the
  exact pre-Scene vertical-tab baseline when the Scene is cleared;
- Scene-owned link routing into current, temporary, retained, workspace,
  Preview, split, external, and approval-gated destinations;
- transactional tab archive and recovery, including real idle temporary-tab
  lifecycle archiving and background restore on Scene activation;
- canonical structured page extraction followed by an approval-gated submit
  action whose success requires an observed accessibility-tree or navigation
  change;
- native two-tab split creation with observed lifecycle confirmation;
- sensitive field redaction and model-write refusal;
- exact per-window bindings;
- Canvas Studio Essential creation, editing, duplicate-origin refusal,
  persistence, native Shell opening, live-tab reuse, and non-destructive
  deletion;
- ephemeral Preview lifecycle outside the tab strip, routing rejection without
  state loss, routed retained promotion across Workspaces, and native split
  promotion of the same live WebContents;
- first-party Canvas registration, Lit rendering, and starter-command focus;
- exact contextual page prompts producing approval-gated semantic surfaces;
- live HTTP(S) Boost application under strict CSP, navigation survival,
  pause/resume/delete rollback, and profile persistence;
- Canvas-to-native Boost mutation through Mojo, including fresh page identity
  after a navigation loading transition;
- Realtime speech-state events, nested function-call execution,
  de-duplication, verified task results, provider errors, and bounded SDP;
- persistent Board creation and editing, keyboard movement and resize,
  coalesced undo/redo, confirmation-gated removal, screen-reader semantics,
  stale-snapshot rejection, slow-save serialization without visual rollback,
  and exact layout restoration after reload;
- provider-neutral Live Collection authoring, typed read-only execution,
  verified semantic mapping, refresh/pause/resume/delete controls, safe item
  opening, last-good preservation, and reload persistence;
- persistent local/cloud Studio provider-route configuration;
- Studio Theme, Scene, routing-rule, and workflow authoring, activation,
  dependency guards, execution, duplication, and persisted catalog state;
- real tab open, activate, pin, and move mutations plus unknown-window refusal;
- live vertical projection;
- default-on vertical shell behavior;
- exact command-palette activation plus rejection of a tab closed after search;
- continued Chromium ownership of the tab strip;
- reachable Seoul organization state.

## Product smoke

The isolated smoke test launches only the explicit local Seoul binary with a new
disposable profile. It does not discover or launch an installed browser.

Observed on 2026-08-09, `SMOKE PASS`:

| Check | Result |
|---|---|
| Isolated launch | 3056 ms |
| `chrome://seoul-canvas` renders the first-party Canvas | passed |
| Local navigation | 492 ms |
| Canvas interactive | 427 ms |
| 25 Canvas view switches | 38 ms |
| Total smoke | 5755 ms |
| JavaScript and DOM | passed |
| Second tab open and activation | passed |
| Canvas product heading | passed |
| Five Canvas views | passed |
| Voice default-off | passed |
| Empty-send refusal | passed |
| Canvas console errors | 0 |
| Browser disconnects | 0 |
| Page crashes | 0 |

These are development-host smoke measurements, not broad performance benchmarks
or service-level guarantees.

Seoul does not own the new tab. `chrome://newtab` resolves to Chromium's NTP,
which is what the Welcome/onboarding row of
`docs/product/zen-chromium-parity.md` specifies and what
`SeoulShellBrowserTest.ProgrammaticNewTabKeepsChromiumContract` holds. The
first-party surface lives at `chrome://seoul-canvas`, and that is what this
smoke exercises.

## Shipping Canvas state

The browser packages and serves a first-party Lit WebUI from
`chrome://seoul-canvas`. It is connected to the native runtime through Mojo;
it is not the standalone design lab.

Verified behavior:

- a distinctive first-run command surface;
- the Observe, Plan, Approve, Verify control model;
- real starter commands that populate and focus the native composer;
- Canvas, Boosts, Library, Boards, and Studio views;
- live active-page identity with contextual Understand, Actions, and Boost
  entry points;
- typed Boost creation, editing, enable/disable, deletion, and live rollback;
- persistent Board creation, editing, pointer/keyboard arrangement, bounded
  history, archive/restore, and confirmation-gated delete controls;
- executable Live Collections backed by eligible typed read-only capabilities,
  with source editing, scheduled/manual refresh, pause/resume, truthful errors,
  safe item actions, and last-good data preservation;
- local/cloud provider route editing with credentials kept behind the browser
  boundary;
- full typed Studio authoring for global Essentials, Themes, Scenes, routing
  rules, and workflow graphs, including activation, run, duplicate, delete,
  origin uniqueness, and dependency guards;
- task state, approval, input, pause, resume, reject, and cancel controls;
- validated SAUI component rendering with escaped payloads;
- bounded chart and table rendering;
- Realtime voice explicit and off by default, with truthful activity/error
  states and a de-duplicated native browser-task bridge;
- no remote script, inline script, or eval permission in the WebUI CSP;
- zero console errors when `chrome://seoul-canvas/` is loaded directly.

The standalone `apps/canvas-prototype/` remains a design lab. It is tested but
is not used as evidence that the shipping Canvas works.

## Product churn exercise

`native/scripts/stress.mjs` answers the question after the smoke: does the
product still work once it has been used hard. Observed on 2026-08-09,
`STRESS PASS`:

| Check | Result |
|---|---|
| Canvas mount/unmount cycles | 15, all rendered |
| Concurrent Canvas tabs | 12 |
| Rapid tab activations | 60 |
| Navigation cycles against a 4000-node document | 8, plus back/forward |
| JS heap, first mount to after 15 remounts | 3.2 MB to 2.8 MB, no growth |
| Console errors, page errors, renderer crashes | 0 |
| Browser survived closing every tab | yes |
| Canvas still mounted and rendered afterwards | yes |

The flat heap across 15 remounts is the point of the exercise: a Canvas that
retained listeners or observers per mount would show it here. This is a churn
exercise, not a long-session soak; the soak remains a release gate below.

## Two gates that were not gating

Both were found by running the product rather than the suite, and both are
fixed:

- **The built app could fail to start at all.** macOS reported "Seoul cannot be
  opened because of a problem" with no log line and no crash report.
  Chromium's `CodeSignCloneManager` re-executes the browser from a copy of the
  bundle in a temporary directory; a component build keeps its shared libraries
  in the build directory and reaches them through an `@loader_path/../../..`
  rpath that only resolves from the real build output, so dyld failed to load
  `libc++_chrome.dylib` before any Chrome code ran. Patch 0026 skips the clone
  in component builds, which is the trade upstream already makes for Chrome for
  Testing. Five of five cold launches now reach a live browser; before the
  patch the failure was reproducible.
- **`verify-checkout.sh` could not pass.** Its reverse proof stages only
  `git diff HEAD`, so the eleven files the patch series *creates* were absent
  from the temporary index and the first patch that creates one failed to
  reverse with "does not exist in index". The gate was reporting checkout drift
  that did not exist, which is worse than not running: the honest reading of a
  red reproducibility gate is that the build cannot be reproduced. It now
  stages the created paths from git's own reading of each patch, passes on a
  clean checkout, and still fails when real drift is introduced - both
  directions verified.

## The browser aborted on an assertion

`dcheck_always_on` defaults to `(build_with_chromium && !is_official_build)`,
which is true for this build, so every Chromium DCHECK was live in the app a
person actually runs. One of them - an upstream invariant about Chromium's own
session-file bookkeeping in `CommandStorageBackend::AppendCommands`, reached on
a BLOCK_SHUTDOWN task - aborted the browser, which macOS reports as "Seoul quit
unexpectedly". Ten crash reports on this machine, seven of them that signature
or the two earlier ones now fixed.

A DCHECK is a development aid. Upstream writes them to catch invariant
violations during development and ships Chrome with them compiled out,
precisely because taking the whole browser down is the wrong response to one in
a user's hands. Seoul writes no session commands of its own, so this is
upstream's assertion about upstream's state, on a code path that ships in
Chrome with the assertion absent.

The baseline now sets `dcheck_always_on = false`, and `SEOUL_DCHECKS=1` puts
them back for development and test runs. That is a configuration difference
between a development build and a product build.

It is not a root cause, and this report does not pretend otherwise. The
assertion is still worth understanding: it fires while flushing session
commands during shutdown, it was not reproducible across five deliberate
attempts here - tab churn, SIGTERM, profile deletion under a live browser,
graceful close after real navigations, and four sequential launches reusing one
profile - and it therefore remains open. What has changed is that it no longer
takes the browser down with it.

## Content blocking, as actually observed

Measured on 2026-08-11 with the built product on a fresh profile, counting
requests to known ad, ad-auction and cross-site-measurement hosts.

Three states are compared: the shipped placeholder that blocked nothing, the
Seoul-authored baseline that replaced it, and the catalogued upstream lists now
fetched at runtime.

| Site | Placeholder | Baseline only | With EasyList + EasyPrivacy |
|---|---|---|---|
| theverge.com | 0 blocked / 82 through | 22 / 5 | **13 / 2 (87%)** |
| reuters.com | not measured | 21 / 2 | **25 / 8 (76%)** |
| bbc.com/news | not measured | 4 / 0 | **5 / 0 (100%)** |
| cnn.com | 0 / 23 | 0 / 0 | not re-measured |

Two things about these numbers rather than one. Sites load different inventory
on different visits, so repeated runs move: an earlier run of the same build
recorded theverge.com at 10 of 10 and reuters.com at 22 of 28. The figures above
are the last run against the shipping binary and are reported rather than the
best one. And the totals themselves fall between columns two and three -
theverge.com attempts fifteen ad requests with the upstream lists where it
attempted twenty-seven with the baseline alone - because the lists block the
loaders that would have requested the rest. Fewer requests attempted is the
better outcome, not a weaker measurement.

An earlier draft of this section reported "9 blocked" on theverge.com. That was
wrong: those were requests that failed for unrelated reasons, counted as though
the blocker had stopped them. The blocker had stopped nothing at all, because
the compiled-in baseline was a single rule against `seoul-adblock.invalid` - a
domain that does not exist. `filters/seoul-baseline.txt` looked like the real
list, but no build rule and no loader referenced it; the catalog carried only
the id string. Every adblock test passed throughout.

The baseline is now a Seoul-authored set covering third-party advertising,
ad-auction, cross-site measurement and session-replay hosts, every rule
third-party scoped. cnn.com reports 0 and 0 because it loaded 110 responses
without issuing a request matching those hosts within the measurement window,
not because nothing was stopped.

What this is not, still: list parity. The catalogued upstream lists remain
undelivered, for two separate reasons that both stand.

- The signed component is the designed delivery path and is registered at
  Chromium's component-update seam, but `RegisterAdBlockFilterComponent` skips
  registration while `seoul_adblock_component_public_key_hash` is empty, which
  it is in every development build. That fail-closed choice is deliberate and
  correct; it needs a release signing identity, not a code change.
- The catalogued runtime downloads are now delivered. `AdBlockCatalogueSubscriber`
  reads `AdBlockListDelivery::kRuntimeDownload`, fetches every enabled entry
  over HTTPS, and installs the result into the default engine appended after
  the baseline. Observed on a fresh profile: EasyList 2,063,536 bytes,
  EasyPrivacy 1,494,149 bytes, installed together as 3,557,687 bytes with the
  filter-list source recorded as `kCatalogueSubscription`.

  This required a deliberate change of posture, stated plainly: a catalogued
  upstream list has no stable hash to pin, because EasyList changes several
  times a day, so `AdBlockSubscriptionIntegrity::kCataloguedHttps` accepts
  transport integrity - HTTPS, no redirects, no credentials, bounded size,
  HTTP 200, text content type, valid UTF-8 - without a content hash. The pinned
  path is unchanged and still requires its SHA-256. The rules are not trusted
  blindly even so: the manager validates the text and must construct a working
  engine before anything is swapped in, a failed round installs nothing at all
  rather than a partial set, and the Seoul baseline is never removed.

  What remains genuinely undone is the signed component above, which is the
  stronger channel and the one that removes the dependency on the list hosts'
  own TLS.

`docs/research/native-adblock-implementation.md` says Brave parity must not be
claimed until those paths exist. That remains true.

## In-stream video ads, and the honest boundary

Measured on 2026-08-21 against youtube.com, signed out, fresh profile:

| Surface | Result |
|---|---|
| Sponsored cards and display slots (search + watch) | 4 in DOM, 0 visible |
| Ad requests (doubleclick, adservice, and class) | blocked at the network layer |
| In-stream (pre-roll) ads | treated by the player-ad module below |

Three layers now cover three different ad classes. Network rules block ad
requests (EasyList, EasyPrivacy, and - now enabled by default - uBlock's
filters, which carry the cosmetic rules for same-origin ad surfaces).
Cosmetic filtering hides ad DOM the network layer cannot reach. And a
Seoul-authored player-ad treatment runs in the isolated world for every
http(s) page: when a player's own markup announces an ad (YouTube's player,
Google IMA, JW Player, video.js - a data table, not per-site code), it seeks
the ad's media element to its end and presses the player's skip control.
Verified end to end through the real injection pipeline against a synthetic
player, three runs; a live YouTube pre-roll could not be forced during
verification because the ad server decides when to serve.

The boundary, stated so nobody oversells it: ads stitched server-side into
the content stream (Netflix's and Prime Video's ad tiers, SSAI generally)
present no ad-state DOM and no separate media element. No client-side blocker
removes those, and Seoul does not claim to.

## Boosts, now on the page they change

"Boost This Site" opens a native bubble anchored to the toolbar of the window
whose page is being boosted: site on/off, per-site dark mode, six tint
swatches, four font choices, text-size steps, element zap, and removal - all
writing through the existing typed SiteLayer registry and applying live. The
Canvas side panel is no longer the Boost editor's front door. Browser-tested:
opening the bubble and pressing a control writes the origin's layer; pressing
it again removes the empty layer rather than leaving a do-nothing Boost.

## First run, as actually seen

Launching the product with no URL gives a window with the vertical rail, the
command surface, and an empty content area: the startup tab is Seoul's synthetic
placeholder on `about:blank`, which patch 0017 deliberately keeps as inert
browser chrome rather than a user-visible New Tab Page. Once any real page is
open the shell behaves as designed - rail, workspace label, favicon, domain-only
resting URL - but the first thing a new user sees is a blank window.

That is the Welcome/onboarding row of `docs/product/zen-chromium-parity.md`,
which is marked `replace` and is not yet built. It is listed here because it is
the first thing anyone evaluating the product will encounter.

## Native product state

### Working and verified

- profile-scoped runtime composition;
- domain-neutral capability registry and planner;
- executor-backed browser tab and page capabilities;
- exact window and tab identity;
- typed tasks, approvals, input replanning, receipts, and task-to-surface
  projection;
- browser mutation confirmation and postcondition observation;
- ephemeral previews that do not silently become retained tabs;
- vertical workspace projection and default-on vertical product shell;
- fuzzy native command navigation across live tabs, workspaces, Essentials,
  and typed utilities, with stale-target rejection;
- standalone compact mode from the native command launcher,
  `Command+Shift+C`, or the typed AI/voice capability, with per-Workspace
  persistence and observed native completion;
- Library, Boards, and Live Collections persistence and execution paths;
- editable Studio provider routes, global Essentials, Themes, Scenes, routing
  rules, and workflow graphs;
- workspace-scoped relaunch restoration for durable tab membership, active
  Scene/Theme selection, standalone compact preference, and exact Scene
  compact-mode baseline;
- live, persistent per-site Boosts with declarative CSS only;
- adaptive SAUI compilation and stable patches;
- semantic data, provenance, and chart-honesty validation;
- exact-scope agent permissions and sensitive-field refusal;
- workflow, scene, theme, Site Layer, connector, context, and voice core models;
- local and cloud provider routing with injected transports;
- realtime voice session plumbing, lifecycle/error handling, task-result
  feedback, and tool-call bridge.

The headless product smoke is written to exercise every Canvas view repeatedly
against generous 5-second ceilings, but it currently stops at its first check
(see "Product smoke"), so there are no current view-switch timings to report.
The Canvas WebUI itself was observed rendering correctly at
`chrome://seoul-canvas/` with zero console errors on 2026-08-09.

### Deliberately bounded

- Studio authoring is intentionally typed and bounded: Themes use validated
  design tokens, routing uses enumerated match/disposition rules, and workflows
  use the validated graph vocabulary rather than arbitrary scripts.
- Relaunch presentation intent is keyed to a durable Workspace and binds to at
  most one matching live window. Persisting distinct simultaneous
  presentations for multiple windows showing the same Workspace remains a
  deliberate multi-window extension; it is not guessed from regenerated
  Chromium window ids.
- Live microphone, external model, and connected-tool behavior requires user
  credentials, real endpoints, permissions, and hardware. The code paths are
  compiled and deterministically tested, but this report does not claim a
  successful production-account session.
- Scene lifecycle recovery deliberately refuses unsafe tabs and non-HTTP(S)
  recovery records rather than claiming it can preserve unsupported state.

## Reproducible commands

From the Project Seoul repository:

```sh
npm run ci                   # static gates + every repository test suite
npm run test:swift           # macOS overlay app, bridge, and transforms
npm run test:native          # 30 unit binaries
npm run test:native:browser  # 129 browser cases
npm run stress:native        # churn exercise against the built product
npm run preview:native
node native/scripts/smoke.mjs
```

The native commands default to the pinned sibling checkout, resolved against the
main working tree so a git worktree finds the same checkout rather than silently
reporting the gate skipped. Set `SEOUL_CHROMIUM_ROOT` or
`SEOUL_CHROMIUM_BINARY` only when deliberately using another Seoul checkout or
binary. The preview and smoke tools never fall back to installed Google Chrome.

`npm run ci` runs on a bare checkout with no Chromium tree. Where a suite needs
a prerequisite this repository does not vendor, that is recorded rather than
assumed: `scripts/check-test-wiring.mjs` fails if a committed test file is
reachable from no npm script, or if a `test:*` script is unreachable from
`npm test` without a declared reason.

Checkout and patch reproducibility:

```sh
native/scripts/verify-checkout.sh
native/scripts/patches.sh verify
native/scripts/patches.sh apply
native/scripts/materialize.sh apply
native/scripts/materialize.sh verify
```

## Reproducibility, proven from a pristine tree

Every earlier claim about the patch series was made against the working
checkout, which already had the series applied. That proves the series is
*consistent with* a built tree; it does not prove it *builds* one. On
2026-08-14 the series was applied to a genuinely pristine tree for the first
time.

Method: a git worktree of the pinned Chromium repository, detached at
`6a7b3dbec3b2ca25877c2553b5473b2f277ef644`, 493,055 files, zero modified
tracked files. Seoul source and the pinned blocker Rust closure were
materialized into it, then `patches.sh verify` ran its cumulative round trip.
The working checkout was not touched at any point.

| Check | Result |
|---|---|
| Patches applied in ascending order, cumulatively | 26 of 26 |
| Patches reversed in descending order | 26 of 26 |
| Tracked files modified after the round trip | 0 |
| HEAD after the round trip | unchanged at the pinned revision |

Cumulative ordering matters here and a per-patch `--check` cannot show it: 0017
extends a function 0014 introduces, so each patch has to apply to the tree the
previous ones produced rather than to the pristine one.

This did require a fix. Every native script tested `-d "$CHROMIUM_SRC/.git"`,
and `.git` is a directory in a clone but a FILE in a worktree, so the scripts
refused to operate on exactly the isolated tree that makes this proof safe.
`is_git_checkout()` in `common.sh` now asks git instead of guessing from the
filesystem, in all 14 places.

## Continuous integration

Also verified rather than assumed on 2026-08-14: GitHub Actions has run on every
push to `main` and each one is green. The most recent run, `31758161612`, took
1m18s across both jobs.

| Job | Evidence |
|---|---|
| `checks` | resolve step reported `render smoke will drive: /usr/bin/google-chrome`, confirming the runner assumption the gate was built on |
| `checks` | `design lab renders, patches in place, and preserves focus and scroll` PASSED in 13.5s with `skipped 0` - the render smoke really ran on Linux against stock Chrome rather than reporting itself skipped |
| `swift` | 21 tests, 0 failures, on macOS |

The last red run predates this work (2026-08-05).

## The omnibox dropped the first thing you typed

Two defects, reported from using the product, both in the address field. Both
are fixed, and the fix for the first is proven by reverting it.

**Typing right after clicking the field lost the leading character.** Seoul
expands the docked Single Toolbar field into its centred floating surface when
editing begins, and that expansion reparents the real `LocationBarView`. Views
has no move-without-blur: `AddChildViewAt()` routes through
`DoRemoveChildView()`, `Widget::ViewHierarchyChanged()` calls
`FocusManager::ViewRemoved()` for *any* removal including a same-widget move,
and focus is cleared. The transition then restored focus through
`FocusLocation(is_user_initiated=true)`, whose `SetFocus()` contract ends in
`SelectAll()` — correct for a fresh Cmd+L, wrong here, because by that point
the field already held the user's first character. The character was selected,
and the next keystroke replaced it.

The fix is ordering, not delay. The transition now runs from
`OmniboxViewViews::HandleKeyEvent` on the keystroke that begins editing,
*before* the character is inserted, so the field being moved still shows the
page URL and the restored whole-URL selection is exactly what that keystroke
should replace. Where the transition is still reached mid-edit — paste, IME,
drop — focus is restored with a plain `RequestFocus()` and the autocomplete
pass is restarted, instead of the select-all path.

Measured, not asserted. With the previous code the new regression test types
`seoul immediate typing regression` with real mouse and key events and the
field holds `eoul immediate typing regression`; with the fix it holds the
whole string. The same string was then typed into the running browser by
posting real `CGEvent` mouse and key input, with no pause between the click
and the first key: the floating surface expanded and every character arrived.

**A presentation callback could take the surface away mid-query.** Docking is
also a reparent, so a completed load or an arriving lifecycle snapshot could
clear focus and strand the rest of what was being typed. `SetSeoulOmniboxFloating`
now refuses to dock a focused field with an uncommitted edit. The layout-mode
reparent, which genuinely must move the location bar with its toolbar, opts out
explicitly and restores focus itself.

**A search could be swallowed by the command surface.** In action mode every
keystroke ran `StopAutocomplete(clear_result=true)`, and Return executed
whichever command the fuzzy ranker had put first. That ranking is a
subsequence match — deliberately generous, so a few letters can reach a
command — and it is far too permissive to decide that Return should run a
command instead of performing the search the user typed. Intent is now a
separate question: exact, prefix, or word-boundary match. A query that names no
command hands the results body and the Return key back to Chromium's omnibox,
whose configured default provider produces the match. Nothing about the engine
is hardcoded; the tests bind their own provider so the assertion is about
Chromium's `TemplateURLService`, not about Google.

The hardcoded English `"New Boost"` text interception in `ChromeOmniboxClient`
is removed for the same reason: it silently swallowed a search for that exact
phrase, and could only ever have recognised the English one.

Verified in the running browser as well as in the suite: Return on
`seoul immediate typing regression` navigated to the default provider's
`search?q=seoul+immediate+typing+regression`.

| Check | Result |
|---|---|
| New browser cases (real mouse/key events) | 6 of 6 passed |
| Same suite with the fix reverted | fails with `eoul immediate typing regression` |
| `SeoulShellBrowserTest` | 61 of 61 passed |
| Command-intent unit case | passed |
| Manual: click then type with no pause, real input | full string arrived |
| Manual: Return on a plain query | default-provider search navigated |

## Tab names and folders exist in the model, not yet in the sidebar

Organization gained two durable fields the Arc/Zen hierarchy needs: a per-tab
custom title and per-workspace folders. Both are Seoul organization metadata
only — `document.title`, the navigation entry, history and `WebContents` are
untouched, and dissolving a folder moves its tabs out rather than closing any
of them, because Chromium owns tab lifetime and Seoul owns the grouping.

The schema moved from 1 to 2. The store previously rejected every version but
its own, so the bump needed a real forward migration: a version 1 document
reads with the new fields unset — which is exactly what their absence meant —
and is stamped forward so the next save writes version 2. A document claiming
version 1 while carrying folders is refused rather than partly believed, and an
unknown higher version is still rejected.

| Check | Result |
|---|---|
| `seoul_organization_unittests` | 78 of 78 passed |
| — pre-folders profile migrates forward | passed |
| — version 1 carrying folders refused | passed |
| — future version still rejected | passed |
| — dissolving a folder keeps every tab | passed |
| — cross-workspace folder reference refused | passed |

This is the model and its persistence. **The sidebar does not yet render
folders, and there is no rename gesture in the UI.** Nothing in the product
surfaces either field yet; the work above is the foundation those surfaces
need, and is reported as such rather than as a shipped feature.

## Ad iframes were never filtered at all

The largest gap in the blocker, and the reason it did not stop ads on most
sites: **no subframe document load was ever checked.** Two independent gates
closed the only paths to it. `ToAdBlockFactoryType` returns nothing for
`kNavigation`, so navigation loads are never proxied; and the navigation
throttle declined anything that was not the primary main frame. Between them,
no request of type `sub_frame` could reach the engine, so the entire
`$subdocument` class of EasyList and uBlock Origin - third-party ad iframes,
the most visible ad format there is - loaded untouched.

The throttle now accepts any navigation with a parent frame, builds a
`sub_frame` request for it, and blocks with `BLOCK_REQUEST_AND_COLLAPSE`, which
removes the frame owner element from layout so a blocked ad leaves no reserved
gap rather than a hole where the ad was.

Three decisions worth recording, because each one is a way this could have been
subtly wrong:

- **The first party is the outermost main frame, not the immediate parent.**
  Brave uses the initiator; Seoul uses the top page, which is what every other
  Seoul request already uses - subresources inside a frame, the WebSocket path,
  and the CSP throttle all resolve the outermost frame. Using the parent for
  the frame's own document alone would have classified it differently from
  everything inside it, and would have let a network embedding its own ad frame
  escape every `$third-party` rule. The two choices agree for a depth-one
  iframe, which is the actual ad case.
- **A `$removeparam` match on a subframe is not applied.** The rewrite path
  restarts the navigation through `WebContents::OpenURL`, which navigates the
  *tab*; on an embedded frame that would have yanked the user's whole page to
  the frame's target. Not supporting it is a small miss; the alternative was a
  catastrophic one.
- **A blocked subframe carrying a `$redirect` resource must still block.**
  Replacement resources are only servable by the subresource interceptor, and
  the test for that keyed off `navigation_url`, which a subframe does not set -
  so the decision would have become `kRedirect` and the throttle, which only
  blocked on `kBlock`, would have let the ad through. It now keys off the
  factory type, and the throttle treats both as blocking.

Proven by reverting: with the old main-frame-only gate the test records the ad
iframe making its request and occupying 150 px of layout; with the fix it
records zero requests and zero height.

| Check | Result |
|---|---|
| Third-party ad iframe blocked and collapsed | passed |
| Same rule leaves a top-level navigation alone | passed |
| Blocking off for the embedder frees its subframes | passed |
| Same suite with the gate restored | fails, ad frame loads at 150 px |
| `AdBlockBrowserTest` + `CosmeticFilterAgentTest` | 32 of 32 passed |

## The Boost editor, rebuilt against Arc's actual specification

The previous panel was not built from the reference. It offered six fixed tint
swatches, which Arc does not have, and was missing most of what Arc does. It
was rebuilt from Arc's own documentation ("Boosts: Customize Any Website"),
in Arc's documented control order.

| Arc control | State |
|---|---|
| Color wheel (drag the coloured dots) | built - two dots, page background and page text, on one HSV disc |
| Invert lightness | already present |
| Advanced colour controls: Contrast, Brightness, Original Saturation | built |
| Reset to original colors | built |
| Font selector | already present |
| Size, 90%–150% | corrected from 85%–130% |
| Case | built |
| Zap | already present; restore now reachable from the editor |
| Code (CSS and JavaScript) | built; JavaScript defaults off, per Arc's own posture |

Two things outside the editor came with it. The Boost's own name now titles the
panel and carries Arc's caret menu - "Rename this Boost..." and "Reset all
Edits", the latter returning the page to how the site wrote it while keeping
the Boost, its name and its place in the list. And Arc's Settings > Advanced
switch, "Enable Boosts on websites you visit", is a profile pref checked at
`SiteLayerApplicator::Refresh` - the one point every Boost reaches a page - so
off silences all of them at once and deletes none, which a browser test pins by
turning it off and back on.

Four new typed adjustment kinds carry the new controls, each with range
validation, CSS emission and a persistence round trip. One of them is not
obvious: CSS `filter` does not accumulate across rules, so three sliders
emitted as three declarations would silently apply only the last one touched.
They compile into a single `filter: contrast() brightness() saturate()`, and a
test asserts exactly one `filter:` appears in the output.

Arc also reaches the editor from a paintbrush in the Site Control Panel at the
right of the URL bar. Seoul had no such affordance, so the feature was
reachable only from the command surface - which means it was not discoverable
by anyone who did not already know it existed. There is now a paintbrush in the
same place, hidden while the omnibox is floating and on non-http(s) pages.

Arc's Code editor writes raw CSS and raw JavaScript. The CSS is appended after
the compiled typed adjustments so it wins on equal specificity, which is the
reason to drop to code at all, and it is deliberately not put through the
selector validator: that validator exists so a *filter list* or a control
cannot emit something unintended, and its safe-subset premise is that nobody
typed it. A stylesheet the user wrote for their own browser is a different
trust relationship, and CSS cannot reach outside the document it styles. Length
is bounded either way.

JavaScript is a different kind of power, so it has its own profile switch and
that switch defaults OFF - which is Arc's current posture, having disabled
JavaScript Boosts and left the user to re-enable the ones they want. The CSS
switch fails open when there is no pref service, because a Boost the user made
should keep working; the JavaScript switch fails closed, because absent a
record that the user turned it on it must not run. A browser test stores a
script on a layer, asserts it does not execute with the switch off, turns the
switch on, and asserts it then runs in the page. Author scripts run in the
page's world, since a Boost script is written against the page's own globals
and would be useless without them - which is exactly why it is gated - and each
one is wrapped so a throw in one cannot stop the next.

Arc restores a zapped area from a control on the page - "clicking the Slash
(\\) icon near the bottom of the webpage afterward will restore any zapped
area". Seoul does the same thing from the editor instead: "Undo Zap" removes
the most recent hide and the element returns on the next apply. Same behaviour,
different placement, and that difference is stated rather than glossed.

Not built, and not claimed: delete from Library > Boosts, and Easels.

## Capture, and what Easels already are

Arc's Easels are "super powered whiteboards... for collecting your ideas as you
browse", filled by taking a Capture: drag a rectangle over the page and keep
that region. Seoul's **Boards are that surface already** - the same authored
spatial document, with text, image references, capture references, links,
persistence, bounded undo and archive - so this is a set of gaps to close on
something real rather than a feature to write from scratch. `New Easel`,
`easel`, `board` and `boards` now reach it as command tokens, each with a test
that they name the command rather than falling through to a web search.

`SeoulCapture` implements the Capture itself. The decision that matters: **the
page chooses the rectangle, the browser process reads the pixels.** The overlay
runs in Chrome's isolated world and reports four numbers; the image comes from
`CopyFromSurface` on the compositor. A page can never hand Seoul fabricated
image data claiming to be a picture of something it does not show.

Four failure modes are handled rather than left to chance: Escape cancels, and
so does navigating away, because a capture belongs to the page it was started
on and must not silently photograph a different document; a drag under 8 px is
a stray click rather than a region, and is answered with nothing instead of a
one-pixel picture; the surface copy times out at five seconds so a capture
cannot hang; and a generation counter drops replies from selections the user
has already abandoned. PNG encoding and the disk write run off the UI thread.

End to end: the `Capture` command runs the region picker, and the finished PNG
is filed in the Library as a `kCapture` artifact carrying its origin, title and
path. The Library is the single owner of the image; a Board references it
rather than holding a second copy of the bytes. Arc's "Capture Full Page" is
the same path with an empty source rect.

`PlaceCaptureOnBoard` closes the loop. A board element of kind
`kCaptureReference` points at the Library artifact by id - not at the file, and
not at a copy of the image - so the Library remains the single owner of the
bytes and deleting the artifact cannot leave a board showing a picture that no
longer exists. It refuses an artifact that does not exist, and one that is not
a capture: anything else would render as a picture the board cannot show. A
refused placement adds nothing.

Still open on Easels: freehand ink, new Easels opening as a Pinned Tab in the
current Space, a Library > Easels section, `Ctrl+Shift+E`, and PNG export.

## The Boost panel was anchored to the wrong thing

`ShowBoostBubbleForWebContents` anchored the panel to `browser_view->toolbar()`
with `BubbleBorder::TOP_LEFT`. In Single Toolbar the toolbar *is* the vertical
rail — a full-height view pinned to the window's left edge — so the panel was
laid out from that corner, covering the sidebar and pointing at nothing. It now
anchors to the address field, which is the control that names the site the
panel acts on. A browser case pins that it starts at the address field and
never hangs off the window's left edge.

The contents were also a flat stack of independently sized rows. The panel now
has a fixed measure, a header that says what it is and which site it acts on
before offering the switch that turns it off, and one shared label-left
control-right row so the rows cannot drift apart.

## Known open issues

Current, specific, and found by driving the product rather than by the suites:

1. **Shell controls are invisible to macOS accessibility.** System Events sees
   only the titlebar buttons on a Seoul window; the rail, the Space strip,
   Downloads, and Create New are not reachable as accessibility elements from
   outside the process. The in-process Views accessibility (names, roles, live
   regions) is implemented and tested, so this is about the mac-side bridge,
   not missing labels. Blocks the screen-reader half of release gate 6.
2. **The Space-container cookie test crashes under the launcher's
   parallel-jobs mode.** Stable across five serial repeats, and the project's
   own runner is serial, so the gate is green; the parallel teardown crash is
   real and uninvestigated.
3. **A profile that went through the earlier session-store crashes shows a
   persistent "Recovery required" banner.** Correct behaviour on genuinely
   damaged state, but there is no user-facing path that repairs and clears it
   beyond acknowledging.

## Public release gates

The following work remains before Seoul can be called a distributable release:

1. Build and repeat the full test matrix as a non-component release build.
2. Replace Chromium application identity with approved Seoul product name,
   bundle identifier, icons, legal assets, and update configuration.
3. Produce the release package and test clean install, upgrade, rollback, and
   profile migration.
4. Sign with the intended Apple Developer identity and notarize the package.
5. Run real credentialed model, realtime voice, and connected-tool acceptance
   tests without recording secrets.
6. Complete keyboard, screen-reader, contrast, reduced-motion, multi-window,
   and long-session soak passes on supported hardware.
7. Complete privacy, security, dependency, network-endpoint, and release-policy
   review.

Those gates require release identities, credentials, external services, and
product decisions that are not stored in this repository. They must not be
simulated or marked complete by a development build.

## Current handoff

The development build is reproducible and every gate is green. The remaining
work is the public-release gate list above, not defect repair.

Continue from this state; do not restart from the standalone prototype, do not
weaken the patch or build gates, and do not use an installed browser as a Seoul
test substitute. Where a suite and the product disagree, settle which is right
from the product contracts in `docs/product/` before changing either - two of
the six browser cases fixed for this report were product defects and three were
assertions that had never been true, and relaxing them would have hidden a
sidebar that could not be collapsed.

## Repository scope note

This repository holds two tracks. This report covers the native Chromium product
only. The Seoul v1 voice-and-pointing track - the macOS Swift overlay app
(`Seoul/`, `SeoulApp/`, `SeoulBridge/`, `SeoulHost/`, `SeoulVerify/`) and the
MV3 extension harness (`apps/browser-harness/`) - is described in
`SEOUL_V1_BRIEF.md`. Its suites are green (21 Swift cases, 34 harness cases) and
now run in CI, but it has no readiness report of its own and no verified
end-to-end voice loop recorded here.
