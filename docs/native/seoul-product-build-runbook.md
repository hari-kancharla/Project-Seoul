# Seoul product build runbook

This runbook builds the actual Seoul product: unmodified Chromium at the pinned
revision, plus the Seoul-owned native source materialized into the checkout, the
ordered reversible integration patches applied, the Seoul targets compiled, and
the Seoul unit tests and browser tests run. It supersedes the baseline-only
`remote-build-runbook.md` (which builds unmodified Chromium and applies no
patch).

Current execution evidence is maintained in
`docs/release/seoul-product-readiness.md`; this runbook intentionally contains
no host-specific status snapshot.

## Capable-host requirements

More than RAM and disk are required. A host must have:

- Apple silicon (arm64) for the current target.
- Full Xcode (not just Command Line Tools) and a compatible macOS SDK.
- Python 3.10 or newer. The scripts automatically use depot_tools' pinned
  bootstrap Python when the system interpreter is older; set `SEOUL_PYTHON3`
  only to select another absolute executable path.
- The correct active developer directory (`xcode-select -p`).
- The pinned `depot_tools` at the locked revision (see `native/chromium.lock.json`).
- A fast local filesystem with ample free space for a Chromium build.
- A clean checkout at the locked Chromium revision with no tracked edits.
- An absolute, space-free checkout path outside the Project Seoul repo.
- A controlled macOS test runner for browser tests (a windowing session).
- Local model files, if the local intelligence path is being exercised.
- Official API test credentials, if cloud evaluation is being run - supplied via
  the platform secure store and environment, never committed.
- Signing credentials later, for distribution only.

Never place credentials in the repository. Never claim a build succeeded on a
host where it did not.

## Ordered procedure

Each step maps to a repository script. Absolute paths assume the repo at
`$SEOUL_REPO` and the checkout resolved by `native/scripts/common.sh`
(`SEOUL_CHROMIUM_ROOT`, else the sibling `seoul-chromium.noindex`).

1. Host validation
   `native/scripts/build-host-check.sh`
   Gates RAM, disk, architecture, Xcode, SDK. A failing gate stops the build.

2. Locked checkout verification
   `native/scripts/verify-checkout.sh`
   Confirms the Chromium HEAD and depot_tools HEAD equal the lock, no tracked
   user edits, expected upstream paths present, gclient consistent.

3. Materialization
   `native/scripts/materialize.sh apply`
   Mirrors `native/seoul/` into `src/seoul/` in the checkout. Then
   `native/scripts/materialize.sh verify` confirms the overlay matches source.

4. Patch application
   `native/scripts/patches.sh apply`
   Applies the ordered series from `native/patches/manifest.json` with
   `git apply` in ascending order. `0001-seoul-native-core` wires the Seoul
   services into `//chrome/browser`, registers the
   organization service at profile init, registers Seoul projection/shell against
   the vertical tab strip after `RootTabCollectionNode::Init()`, exposes the
   production `GetSeoulRootNode()` accessor, and forwards collapse-state changes.
   `0002-seoul-product-defaults` enables the vertical workspace shell for fresh
   profiles and updates the upstream default-state unit tests.

5. GN generation
   `native/scripts/gen.sh`
   Writes `args.gn` from `native/gn/macos-arm64-baseline.gn` and runs `gn gen`
   into `out/SeoulBaseline`. Use `gen.sh --verify` on an incapable host to print
   effective args without running GN.

5b. Host-side parse gate (also runs in `npm run ci` wherever the pinned
   checkout exists)
   `native/scripts/syntax-check.sh`
   Parses every Seoul .cc with clang -fsyntax-only against the REAL pinned
   checkout headers, stubbing only gn-generated buildflag headers (all-zero,
   64-bit pointers true). Files that transitively need generated mojom/grit/
   perfetto code are SKIPPED with a stated reason and parse only after
   `gn gen`. This is not compilation, but it catches include, name, type,
   and API-drift errors host-side; it found the M149 base::DictValue/
   base::ListValue migration and a dozen real defects that would otherwise
   have surfaced as build breaks on this host.

6. GN check
   `gn check out/SeoulBaseline //seoul/...`
   Validates the Seoul target graph and include dependencies. The Seoul BUILD
   files import `//testing/test.gni` for the `test()` template; every module
   exposes a `*_core`/`*_chromium` library and a `seoul_*_unittests` target, and
   `//seoul/browser:seoul_unittests` aggregates them.

7. Browser build
   `native/scripts/build.sh`
   Builds the `chrome` target. To include the Seoul libraries explicitly, build
   `//seoul/browser` as well:
   `autoninja -C out/SeoulBaseline chrome seoul/browser`.

8. Unit-test build
   `native/scripts/test.sh unit`
   Builds every Seoul pure-model unit-test target explicitly, then runs every
   produced binary and fails on the first nonzero exit. The semantic,
   SAUI, tools, and product targets include the CROSS-LANGUAGE PROTOCOL
   CONFORMANCE suites (`semantic_wire_unittest.cc`,
   `saui_protocol_fixtures_unittest.cc`, `tool_descriptor_wire_unittest.cc`,
   `task_snapshot_wire_unittest.cc`); they read the shared corpus from
   `src/seoul/protocol/fixtures/`, which `materialize.sh apply` mirrors from
   the repository's `protocol/` directory (the GN targets declare it as test
   `data`). The TypeScript side of the same corpus runs on any host via
   `npm run test:protocol`, and `npm run check:protocol` fails CI when schema
   enums and native wire names drift.

9. Browser-test build
   `native/scripts/test.sh browser`
   The runner builds the focused `seoul_browser_tests` executable and executes
   Seoul's exact filter, one isolated test process at a time. The same source
   sets remain wired into `//chrome/test:browser_tests` through the integration
   patch for upstream-style coverage, but routine Seoul development does not
   compile thousands of unrelated Chromium cases. The cases exercise the
   runtime, command adapter, projection, default shell, Preview, and Canvas
   WebUI against a real browser.

10. Full native execution
    `native/scripts/test.sh all` reproduces both prior stages. Run individual
    `out/SeoulBaseline/seoul_*_unittests` binaries only while diagnosing a
    focused failure.

11. Launch
    `native/scripts/run.sh` launches the built browser for manual validation.

12. Fixture test execution
    Run the end-to-end fixtures (`docs/quality/seoul-end-to-end-tests.md`) as
    browser tests with fake speech, model, and connector providers.

13. Evidence capture
    Record build manifest (revision, args, target list), test logs, timings,
    and performance measurements (`docs/native/` and the readiness report).

14. Cleanup / restoration
    `native/scripts/patches.sh reverse` then `native/scripts/materialize.sh
    reverse`, then `native/scripts/verify-checkout.sh` to confirm the checkout is
    restored to the locked HEAD with no overlay and no applied patch.

## What is proven on the authoring host today

- Materialize apply/verify/reverse round-trip: clean.
- Patch `git apply --check`, apply, and reverse against the pinned checkout:
  clean; zero tracked Chromium edits remain after reverse.
- clang-format (Chromium style) across all native C++: clean.
- Header include resolution against the pinned checkout: all resolve.
- `git diff --check`, JSON, patch-manifest, repo-boundary, shell-syntax: pass.
- Checkout restored to the locked HEAD with no overlay and no applied patch.

## What is NOT proven and requires a capable host

GN generation, `gn check`, C++ compilation, linking, unit-test execution,
browser-test execution, launch, runtime behavior, performance, packaging,
signing, and notarization. None of these have been performed. A capable-host
build is the mandatory next action.
