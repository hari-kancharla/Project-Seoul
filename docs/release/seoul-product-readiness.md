# Seoul product readiness

Last verified: 2026-07-25 on macOS arm64.

This report is the source of truth for the current product build. It separates
working product behavior from public-distribution work. A passing development
build is not described as a signed release.

## Verdict

SEOUL FUNCTIONAL DEVELOPMENT BUILD VERIFIED

The tracked Seoul source was materialized into the pinned Chromium checkout,
the reversible integration patches were applied, Chromium and every Seoul
native test target compiled, the local browser launched, and the shipping
`chrome://seoul-canvas` WebUI ran successfully.

The current build is not yet a public release artifact. It is a component
development build without final Seoul application branding, signing,
notarization, an installer, or production update infrastructure.

## Verified build

| Item | Verified value |
|---|---|
| Chromium version | `149.0.7827.201` |
| Chromium revision | `6a7b3dbec3b2ca25877c2553b5473b2f277ef644` |
| Platform | macOS arm64 |
| Output | `out/SeoulBaseline/Chromium.app` |
| Build mode | release component build, `symbol_level=0` |
| Seoul overlay | `native/seoul/` materialized to `src/seoul/` |
| Integration | 4 ordered, hash-verified patches |
| First-party Canvas | `chrome://seoul-canvas` |

The build host passed the RAM, storage, Xcode, SDK, architecture, and checkout
gates before generation or compilation. The build did not bypass or weaken a
host gate.

## Test evidence

All results below were produced locally from the source and binary described
above.

| Suite | Result |
|---|---|
| Native unit executables | 24 passed |
| Native unit tests | 562 passed |
| Focused Chromium browser tests | 45 passed |
| Protocol conformance | 8 passed |
| Canvas and renderer tests | 31 passed |
| Native syntax audit | 173 parsed, 40 generated-header files deferred to the native compiler |
| TypeScript and JSON checks | passed |
| Patch manifest, apply, and reverse verification | passed |
| GN header dependency check | passed |
| Architecture, boundary, and domain-neutrality gates | passed |

The 40 syntax-audit skips are not uncompiled gaps. They depend on GN-generated
Chromium headers and were compiled through their native build targets. The
focused browser suite also exercised their runtime integration.

### Focused browser coverage

The 45 passing in-process browser cases run with explicit headless flags. Each
fixture releases the macOS key window before exercising native layout, so the
suite does not intercept input from an interactive desktop session. The runner
disables Chromium's unrelated experimental `InitialWebUI` toolbar
in the explicit headless backend because its pre-test paint-metrics callback
can wait forever before a test body starts. Seoul's native vertical Shell and
`chrome://seoul-canvas` remain enabled and are exercised by the suite.

The cases cover:

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

The isolated smoke test launched only the explicit local Seoul binary with a
new disposable profile. It did not discover or launch an installed browser.

Observed on 2026-07-25:

| Check | Result |
|---|---|
| Isolated launch | 5279 ms |
| Local navigation | 898 ms |
| Canvas interactive | 626 ms |
| 25 Canvas view switches | 417 ms |
| Total smoke | 10073 ms |
| JavaScript and DOM | passed |
| Second tab open and activation | passed |
| Canvas product heading | passed |
| Five Canvas views | passed |
| Voice default-off | passed |
| Empty-send refusal | passed |
| Canvas console errors | 0 |
| Browser disconnects | 0 |
| Page crashes | 0 |

These are development-host smoke measurements, not broad performance
benchmarks or service-level guarantees.

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
- zero console errors in both preview capture and product smoke.

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

The headless product smoke also exercises every Canvas view repeatedly. The
current local run reached interactive Canvas in 626 ms and completed 25 rapid
view switches in 417 ms with zero console errors; the smoke enforces generous
5-second ceilings to remain stable on slower supported development hosts.

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
npm run ci
npm run test:native
npm run test:native:browser
npm run preview:native
node native/scripts/smoke.mjs
```

The native commands default to the pinned sibling checkout. Set
`SEOUL_CHROMIUM_ROOT` or `SEOUL_CHROMIUM_BINARY` only when deliberately using
another Seoul checkout or binary. The preview and smoke tools never fall back
to installed Google Chrome.

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

The functional development build is reproducible and green. Continue from this
state; do not restart from the standalone prototype, do not weaken the patch or
build gates, and do not use an installed browser as a Seoul test substitute.
