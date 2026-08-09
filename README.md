# Project Seoul

Project Seoul is the voice-first, visual, programmable personal Chromium-based
browser. This repository holds the tracked Seoul-owned native source, the
reversible Chromium integration patch series over a pinned Chromium revision,
the canonical cross-language protocol, and the runnable design labs. The source
has been compiled into a real Chromium application on macOS arm64, the native
unit suites have run, and the isolated product launch/Canvas paths are exercised.
See `docs/release/seoul-product-readiness.md` for the exact current commands,
counts, remaining release gates, and overall verdict.

The repository contains:

1. The **tracked Seoul-owned native product source** (`native/seoul/browser/`):
   organization, projection, command, and shell layers, plus the product
   subsystems - adaptive visual UI (`saui/`), Library/Boards/Live Collections
   (`library/`), exact-scope agent permissions (`policy/`), voice (`voice/`), the tool
   registry and general operator (`tools/`, `tasks/`), workflows (`workflows/`),
   Scenes (`scenes/`), themes (`themes/`), Site Layers (`site_layers/`), Context
   Threads (`context/`), hybrid intelligence (`intelligence/`), grounded data
   contracts (`data/`), and connected tools (`connectors/`). Every module ships a
   unit-test target.
2. The **reversible Chromium integration patch series** (`native/patches/`) over
   a pinned Chromium revision, which wires the native services into
   `//chrome/browser` and the vertical tab strip. It applies and reverses
   cleanly against the pinned checkout.
3. An **unmodified, pinned native Chromium baseline** (`native/chromium.lock.json`,
   `native/gn/`) plus reproduction/verification scripts. The Chromium source and
   build output live in an external, untracked checkout (default sibling
   `../seoul-chromium.noindex`; the `.noindex` suffix keeps Spotlight from
   indexing it), never in this repo.
4. The **canonical cross-language protocol** (`protocol/`): versioned JSON
   Schemas for the semantic result, adaptive surface, surface patch, component
   event, task snapshot, and capability descriptor wire formats, with generated
   TypeScript types and a shared conformance fixture corpus consumed by both
   the native C++ tests and the TypeScript tests. `npm run check:protocol`
   fails CI if the schemas drift from the native wire names. It is also the
   browser-control protocol reference.
5. The **Seoul Canvas Design Lab** (`apps/canvas-prototype/`): a runnable
   TypeScript design environment for the Canvas over twenty synthetic fixture
   capabilities. It consumes the canonical protocol and implements real
   atomic, incremental surface patches, but it is not the shipping runtime -
   see its README for its honest boundaries.
6. The **Voice Lab** (`apps/voice-lab/`): pinned, hash-verified voice runtime
   and model candidates plus ASR/TTS benchmark scripts that feed the voice
   readiness gates.
7. The **Seoul v1 voice-and-pointing track**: a macOS Swift package (`Seoul/`,
   `SeoulApp/`, `SeoulBridge/`, `SeoulHost/`, `SeoulVerify/`, `SeoulTests/`,
   `Package.swift`) holding the menu-bar overlay app, the pure coordinate and
   display-selection transforms, and the native-messaging bridge; plus the MV3
   extension harness (`apps/browser-harness/`) that owns the in-page
   accessibility tree and element resolution. `SEOUL_V1_BRIEF.md` is its brief.
   This track is separate from the native Chromium product above and has no
   readiness report of its own.

Source-of-truth model: Seoul-owned code is tracked here (`native/seoul/`) and is
materialized into the external checkout; unavoidable upstream edits are minimal,
reversible patches (`native/patches/chromium/`) over a pinned Chromium revision.
The canonical `protocol/` directory is mirrored into the checkout at
`src/seoul/protocol/` by the same materialization step.
The modified checkout is disposable and is never the source of truth.

The checked development build is a component build for fast, low-risk iteration.
Distribution packaging, product branding, signing, notarization, and a
non-component release build remain separate release gates; the readiness report
does not conflate those with a passing functional development build.

See: `docs/product/seoul-product-definition.md` (what Seoul is),
`docs/product/seoul-product-thesis.md` (who it is for and why),
`docs/native/seoul-product-build-runbook.md` (how to build the product on a
capable Mac), `docs/product/seoul-competitive-review.md`, and
`docs/release/seoul-product-readiness.md` (per-feature status and verdict).

## Prerequisites

- macOS on Apple Silicon (`arm64`)
- Node.js >= 23.6 (built-in TypeScript type stripping; `v26.0.0` verified)
- npm (`11.12.1` verified)
- Python >= 3.10 for Chromium generators. If the system `python3` is older, set
  `SEOUL_PYTHON3` to the absolute path of a newer executable.

## Install

```
npm install
```

This installs the development dependencies pinned in `package-lock.json`:
`typescript`, `esbuild`, and `puppeteer-core` (used by the isolated Canvas and
native browser smoke tests; nothing downloads a browser and nothing is installed
at runtime).

## Checks and tests

```
npm run check   # 13 static gates: scripts, json, adblock vendor, patch
                # manifest, boundary, neutrality, native/product architecture,
                # canvas webui, native syntax, protocol drift, test wiring,
                # native test wiring
npm test        # 89 cases: protocol conformance, Canvas Design Lab, Boost
                # editor, extension-harness resolver and providers, and the
                # reference coordinate transform
npm run ci      # both, as CI runs them

# macOS with a Swift toolchain (a separate CI job runs this):
npm run test:swift           # 21 overlay/transform cases

# On a capable macOS host with the materialized/patched checkout:
npm run test:native          # build + run all 30 Seoul unit binaries
npm run test:native:browser  # build + run the Seoul browser-test filter
```

`npm run check` includes two wiring gates, because a suite that exists but never
runs is indistinguishable from one that passes. `check:test-wiring` fails if a
committed test file is named by no npm script, or if a `test:*` script is not
reachable from `npm test`. `check:native-tests` fails if a `test()` target in a
Seoul `BUILD.gn` is missing from the runner, or if a browser fixture compiled
into `seoul_browser_tests` falls outside the gtest filter.

Every gate is currently green, with per-suite counts and the exact commands in
`docs/release/seoul-product-readiness.md`.

## Canvas Design Lab

```
npm run canvas
```

Builds the design lab with esbuild and opens it in the default browser. See
`apps/canvas-prototype/README.md`.

## Building the product

The native product builds against the pinned external Chromium checkout on a
capable Mac (RAM/disk/Xcode gated by `native/scripts/build-host-check.sh`).
Follow `docs/native/seoul-product-build-runbook.md`; the deterministic scripts
live in `native/scripts/` and resolve the checkout at `$SEOUL_CHROMIUM_ROOT`
(default sibling `../seoul-chromium.noindex`).
