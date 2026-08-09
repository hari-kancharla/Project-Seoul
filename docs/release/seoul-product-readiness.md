# Seoul product readiness

Last verified: 2026-08-09 on macOS arm64.

This report is the source of truth for the current product build. It separates
working product behavior from public-distribution work. A passing development
build is not described as a signed release.

## Verdict

SEOUL DEVELOPMENT BUILD FUNCTIONAL, ONE GATE RED

The tracked Seoul source is materialized into the pinned Chromium checkout, the
reversible integration patches apply, Chromium and every Seoul native test
target compile, the local browser launches, and the shipping
`chrome://seoul-canvas` WebUI runs.

Every unit gate is green. The focused browser suite is **not**: 128 of 134
Seoul browser cases pass and 6 `SeoulShellBrowserTest` cases fail. Those six are
listed under "Known-red gate" below and must be resolved before this report can
return to an unqualified verdict.

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
| Integration | 25 ordered, hash-verified patches |
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
| Native unit executables | 30 of 30 passed |
| Native unit tests | 722 passed, 0 failed |
| Focused Chromium browser tests | **128 passed, 6 failed** (see Known-red gate) |
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

### Known-red gate

Six `SeoulShellBrowserTest` cases fail against the current build:

- `AppearanceLayoutModesAreReversibleAtNarrowWidth`
- `CompactCollapsedMultipleStartsFromRealSixtyDipEndpoint`
- `CompactHoverRevealAndReturnCollapseClipOnlyAtEndpoints`
- `CompactMultipleRoundTripKeepsFiveDipEndpointAndPresentation`
- `ProgrammaticNewTabKeepsChromiumContract`
- `SingleToolbarUsesZenLeadingSearchTreatment`

Two distinct symptoms. The compact cases assert the vertical rail reaches its
collapsed endpoint and observe it still at its expanded width
(`kCompactCollapsedWidth` is 5, `region->width()` is 230), so the collapse
animation does not land on its endpoint. The leading-search case observes a
fresh window reporting neither the editing-or-empty state nor the floating
search icon.

All six were added on 2026-07-27 and 2026-08-01, after the previously recorded
verification on 2026-07-25. They have never been part of a green recorded run,
so this is unfinished work rather than a regression against a known-good state.
`patch 0025 (seoul-shell-reentrancy-guards)` addresses one contributing cause -
an animation subscriber starting a replacement motion from its own `kEnded`
notification - and is necessary but not sufficient: the cases still fail with it
applied.

### Focused browser coverage

The 134 in-process browser cases run with explicit headless flags. Each fixture
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

**`node native/scripts/smoke.mjs` currently fails.** Run on 2026-08-09 against
the build described above:

| Check | Result |
|---|---|
| Isolated launch | passed (1079 ms) |
| Browser version | `Chrome/149.0.7827.201` |
| `chrome://newtab` renders Seoul Canvas | **failed** (30 s timeout) |
| Remaining checks | not reached |

The smoke test navigates to `chrome://newtab/` and waits for the
`seoul-canvas-app` element. Direct observation of the same build:

- `chrome://newtab/` redirects to `chrome://new-tab-page/` and renders
  Chromium's stock `<ntp-app>`. `seoul-canvas-app` is not defined on that
  document.
- `chrome://seoul-canvas/` renders correctly: `seoul-canvas-app` is defined, the
  shadow root is attached, the heading reads "Ask, act, understand.", and there
  are zero console errors.

So the Canvas WebUI itself is healthy; what changed is where a new tab goes.
Patch `0008 (seoul-zen-product-surface)` deliberately removes the normal-profile
NTP rewrite that patch 0006 installed in `chrome/browser/search/search.cc`, and
patch `0017` states the replacement intent directly: Seoul's synthetic startup
tab stays on `about:blank` and "user-created NTPs still take the unmodified
Chromium rewrite path".

That leaves a contradiction a product owner has to settle, not a bug with an
obvious fix:

- the smoke test asserts the pre-0008 behavior (new tab is Seoul Canvas);
- patches 0008 and 0017 assert the post-0008 behavior (new tab is Chromium's);
- and `SeoulShellBrowserTest.ProgrammaticNewTabKeepsChromiumContract`, which
  asserts the post-0008 behavior, also fails - so the intended contract is not
  holding either.

Either the rewrite belongs back in the series and the smoke test is right, or
the smoke test is stale and must be updated to assert the Chromium contract.
Nothing here should be marked green until that is decided. The measurements from
the previous 2026-07-25 run were removed rather than carried forward, because
they describe a behavior this build no longer has.

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
npm run test:native:browser  # 134 browser cases; 6 currently fail
npm run preview:native
node native/scripts/smoke.mjs  # currently fails at its first check
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

The development build is reproducible. Every unit gate is green; two gates are
not, and they are the next work:

1. Six `SeoulShellBrowserTest` cases fail (compact-rail collapse endpoint and
   the Zen leading-search treatment). See "Known-red gate".
2. `node native/scripts/smoke.mjs` fails at its first check, and the underlying
   new-tab behavior needs a product decision. See "Product smoke".

Continue from this state; do not restart from the standalone prototype, do not
weaken the patch or build gates, and do not use an installed browser as a Seoul
test substitute. Do not mark either gate green by relaxing its assertion without
first deciding what the behavior should be.

## Repository scope note

This repository holds two tracks. This report covers the native Chromium product
only. The Seoul v1 voice-and-pointing track - the macOS Swift overlay app
(`Seoul/`, `SeoulApp/`, `SeoulBridge/`, `SeoulHost/`, `SeoulVerify/`) and the
MV3 extension harness (`apps/browser-harness/`) - is described in
`SEOUL_V1_BRIEF.md`. Its suites are green (21 Swift cases, 34 harness cases) and
now run in CI, but it has no readiness report of its own and no verified
end-to-end voice loop recorded here.
