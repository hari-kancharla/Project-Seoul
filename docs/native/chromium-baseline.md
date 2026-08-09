# Native Chromium baseline

Current as of 2026-08-09. The live product and release verdict is maintained in
`../release/seoul-product-readiness.md`.

Project Seoul is pinned to a reproducible Chromium baseline on macOS arm64. The
checkout, generation, component build, native unit tests, browser tests,
product launch, and runtime smoke all complete and pass.

## Status

| Stage | Status |
|---|---|
| Harness and lock | completed |
| Chromium checkout and dependency sync | completed |
| Checkout verification | completed |
| Seoul materialization and patch application | completed |
| GN generation and dependency check | completed |
| `chrome` component build | completed |
| 30 native unit executables | completed, 722 tests passed |
| Focused browser integration suite | completed, 129 tests passed |
| Launch and product smoke | completed |
| Canvas WebUI runtime | completed at `chrome://seoul-canvas/`, zero console errors |
| Non-component release build | not yet run |
| Signing and notarization | release gate |

## Pinned upstream

| Field | Value |
|---|---|
| Product version | `149.0.7827.201` |
| Milestone | `149` |
| Chromium revision | `6a7b3dbec3b2ca25877c2553b5473b2f277ef644` |
| depot_tools revision | `1e85b18d8037bca45f9a9891ae9325729f3ebcd4` |
| Platform | macOS arm64 |

The machine-readable lock is `native/chromium.lock.json`.

## Checkout model

The Chromium checkout is external to this repository:

```text
ProjectSeoul/
seoul-chromium.noindex/
  depot_tools/
  src/
    out/SeoulBaseline/
```

Tracked Seoul source remains under `native/seoul/` and is mirrored into
`src/seoul/`. The canonical `protocol/` tree is mirrored into
`src/seoul/protocol/`. Unavoidable upstream edits are carried only in the
ordered, hash-verified patch series under `native/patches/chromium/`.

The external checkout and build products are disposable. They are not the
source of truth.

## Verified host

The build-host gate passed with:

- macOS arm64;
- 24 GiB RAM;
- 248 GiB free storage;
- Python 3.11.8 from depot_tools' pinned bootstrap;
- Xcode 26.6;
- macOS SDK 26.5;
- verified pinned checkout;
- 6 planned Ninja jobs.

The exact Python runtime was resolved from depot_tools' pinned bootstrap; no
gate was disabled or weakened.

## Development build configuration

`native/gn/macos-arm64-baseline.gn` defines the checked development build:

```gn
is_debug = false
is_component_build = true
symbol_level = 0
use_lld = false
target_cpu = "arm64"
```

This configuration is optimized for safe iteration and test turnaround. A
non-component build must pass before a public release.

Output:

```text
../seoul-chromium.noindex/src/out/SeoulBaseline/Seoul.app
```

## Vertical and split foundations

The pinned Chromium revision contains the upstream vertical-tab Views
implementation and built-in multi-content split infrastructure. Seoul does not
fork tab ownership.

The integration patch sets the product default for vertical presentation while
preserving Chromium's `TabStripModel` and tab collection as authoritative.
Passing browser tests prove:

- the Seoul vertical shell is on by default;
- the real window is projected into the Seoul presentation service;
- an unknown window is not projected;
- Chromium remains the owner of the tab strip;
- real open, activate, pin, and move operations resolve through the typed
  mutation adapter.

Split and preview behavior remain distinct: an ephemeral Seoul Preview is not a
retained tab and dismisses without changing tab count.

## Reproduction

From the Project Seoul repository:

```sh
native/scripts/build-host-check.sh
native/scripts/verify-checkout.sh
native/scripts/patches.sh verify
native/scripts/patches.sh apply
native/scripts/materialize.sh apply
native/scripts/gen.sh
native/scripts/build.sh
npm run test:native
npm run test:native:browser
node native/scripts/smoke.mjs
```

If the default interpreter is older, the scripts automatically use
depot_tools' pinned Python 3.10+ bootstrap interpreter. Set `SEOUL_PYTHON3`
only when deliberately selecting another compatible executable. The full build
and test procedure is in `seoul-product-build-runbook.md`; current observed
results and remaining release gates are in the readiness report.
