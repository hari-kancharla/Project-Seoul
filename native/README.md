# Seoul native Chromium baseline

This directory holds Project Seoul's native browser foundation on macOS arm64: the
pinned-revision lock, baseline GN args, repository-owned Seoul source, the Chromium
integration patch series, and deterministic scripts. It contains **no Chromium
source and no build output**.

## Architecture boundary

- **Chromium source and build output live OUTSIDE this repository.** They are
  checked out into an external root (`$SEOUL_CHROMIUM_ROOT`, default sibling
  `../seoul-chromium.noindex`; the `.noindex` suffix keeps Spotlight from
  indexing the checkout). Nothing under that root is tracked by Project Seoul
  git, and it is never the source of truth.
- **`chromium.lock.json` pins the exact upstream source** - product version, full
  Chromium commit SHA, and the depot_tools commit SHA. Reproduced from this pin,
  never from a floating `main`.
- **Seoul-owned native code is tracked here**, under `seoul/` (C++, WebUI,
  resources, config). `scripts/materialize.sh` mirrors it into the checkout at
  `src/seoul/`. It is the source of truth; the checkout copy is disposable.
- **Unavoidable upstream Chromium edits are minimal, ordered, reversible patches**
  under `patches/chromium/`, described by `patches/manifest.json` and applied by
  `scripts/patches.sh`.
- **Upstream vertical tabs and split view must be EXTENDED before any replacement
  is considered.** The pinned revision already ships both (see
  `../docs/native/chromium-baseline.md`); Seoul builds on them.
- **The canonical `protocol/` directory is the browser-control protocol
  reference.** The retired Manifest V3 extension harness has been removed.

## Scripts

- `doctor.sh` - checkout-readiness preflight (does not gate building).
- `fetch.sh` - clone+pin depot_tools and perform the initial checkout (external).
- `sync.sh` - pin to the lock, sync deps, run hooks. `sync.sh --verify-only`
  delegates to `verify-checkout.sh`.
- `verify-checkout.sh` - read-only verification of the checkout against the lock.
- `materialize.sh` - apply/verify/reverse the Seoul source overlay.
- `patches.sh` - list/verify/apply/reverse the Chromium integration patch series.
- `check-patch-manifest.mjs` - validate `patches/manifest.json`.
- `build-host-check.sh` - **build-readiness gate** (hard RAM/disk/Xcode/checkout
  requirements). `gen.sh` and `build.sh` refuse to run unless it passes.
- `gen.sh` - GN generation (gated by `build-host-check.sh`). `gen.sh --verify`
  prints the effective args without a checkout.
- `build.sh` - build the `chrome` target (gated). Job count via `SEOUL_NINJA_JOBS`.
- `test.sh` - build and run every Seoul native unit executable or the focused
  browser integration suite. Browser cases use disposable profiles, explicit
  headless flags, and never select an installed browser.
- `run.sh` / `smoke.mjs` - launch with a disposable profile / launch smoke test.

## Configuration

- `SEOUL_CHROMIUM_ROOT` - absolute path to the external checkout root (default
  sibling `seoul-chromium.noindex`, with a legacy `seoul-chromium` sibling still
  honored). Paths with spaces, or that resolve inside this repo, are rejected.
- `SEOUL_NINJA_JOBS` - explicit Ninja job count (validated positive integer). When
  unset, a conservative memory-aware default is used (about one job per 4 GiB of
  RAM, floored at 2). There is no unconditional hard-coded value.
- `SEOUL_PYTHON3` - optional absolute path to Python 3.10 or newer. The scripts
  automatically use depot_tools' pinned bootstrap Python when the host's
  `python3` is older, and put the selected interpreter first for Ninja actions.
- `SEOUL_MIN_RAM_GIB` (default 16) and `SEOUL_MIN_BUILD_FREE_GIB` (default 150) -
  the build-host minimums enforced by `build-host-check.sh`.

## Status

The checkout is pinned and complete, the materialized overlay and ordered patch
series round-trip cleanly, and the development component build is exercised on
the current capable host. Exact build, test, runtime, and remaining release
evidence lives in `../docs/release/seoul-product-readiness.md`; this README does
not duplicate a dated status snapshot. The component-build args in
`gn/macos-arm64-baseline.gn` are for development and are not a distributable
shipping configuration.

## Reproduce (on a capable host)

```
native/scripts/doctor.sh            # checkout readiness
native/scripts/fetch.sh             # clone depot_tools + initial checkout (external)
native/scripts/sync.sh              # pin to the lock + sync deps + hooks
native/scripts/verify-checkout.sh   # read-only verification against the lock
native/scripts/materialize.sh apply # overlay the Seoul product source
native/scripts/patches.sh verify    # validate + round-trip the ordered patch series
native/scripts/build-host-check.sh  # BUILD gate (RAM/disk/Xcode/checkout)
native/scripts/gen.sh               # gn gen out/SeoulBaseline
native/scripts/build.sh             # build chrome (SEOUL_NINJA_JOBS configurable)
node native/scripts/smoke.mjs       # launch smoke test
```

See `../docs/native/remote-build-runbook.md` for the full remote build runbook.
