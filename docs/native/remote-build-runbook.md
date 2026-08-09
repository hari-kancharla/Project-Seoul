# Remote build runbook: first unmodified Chromium baseline

This runbook describes how to perform the **first unmodified baseline Chromium
build** for Project Seoul on a stronger Apple-silicon Mac than the one used to
prepare the checkout. It is vendor-neutral: it names no cloud provider, no rental
service, and no machine-specific account. "Remote" here means only "a different,
stronger host you control" - it could be a colleague's Mac, a beefier desktop, or
any Apple-silicon machine that meets the requirements below.

The goal of this build is a single deliverable: a launchable, unmodified Chromium
binary at the pinned revision, plus a build manifest that proves what was built and
how. Nothing in this runbook applies any Project Seoul patch (there are none yet),
changes branding, or produces a shippable product.

## Scope and honest status

What this runbook does:

- Clean checkout of the Project Seoul repo.
- Locked Chromium fetch + dependency sync at the pinned revision.
- Read-only checkout verification.
- Build-host readiness gate (RAM, disk, architecture, Xcode, SDK).
- GN generation for a **component development build** in `out/SeoulBaseline`.
- Unmodified `chrome` build.
- Launch smoke test.
- Upstream **vertical-tabs** and **split-view** runtime checks (capabilities that
  already ship at the pinned revision; this only confirms they work in our build).
- Artifact + log capture and a build manifest.

What this runbook explicitly does NOT cover (out of scope here):

- **Packaging, signing, and notarization.** The component dev build is not a
  shippable artifact. See "Not the shipping configuration" near the end.
- **Any Chromium source modification or Seoul-owned overlay code.** Zero patches
  are applied; `native/patches/chromium/` is empty at this milestone.
- **Provider credentials or machine secrets.** Do not place any API key, token,
  signing identity, keychain item, or account credential anywhere in the repo, the
  checkout, the manifest, or the logs.

As of this writing the build, GN generation, launch, smoke, and runtime feature
checks have NOT yet been performed on any host. This runbook is the procedure for
doing them the first time; treat every "expected" result below as a thing to
observe and record, not a thing already proven.

## Pinned upstream (do not float)

| Field | Value |
|---|---|
| Chromium product version | 149.0.7827.201 |
| Chromium revision (full SHA) | `6a7b3dbec3b2ca25877c2553b5473b2f277ef644` |
| depot_tools revision (full SHA) | `1e85b18d8037bca45f9a9891ae9325729f3ebcd4` |

The machine-readable source of truth is `native/chromium.lock.json`. Every script
below reads the pin from that file. Never replace the pin with a floating branch
just to make a fetch easier.

## Target host requirements

The build host must satisfy all of these before you start. The repository's
readiness gate (`native/scripts/doctor.sh`, described in stage 4) enforces the hard
ones and warns on the soft ones.

| Requirement | Hard minimum | Preferred | Why |
|---|---|---|---|
| Architecture | Apple silicon (arm64) | Apple silicon | The baseline targets `target_cpu = "arm64"`; the gate fails on non-arm64. |
| RAM | 16 GiB | 32 GiB or more | The `chrome` link is memory-hungry; below 16 GiB it thrashes or OOMs even at low job counts. |
| Free fast storage | 120 GiB (gate floor) | >= 250 GiB free APFS | A no-history checkout (~48 GiB) plus a component build (~50-100 GiB) needs real headroom; the gate warns below 180 GiB. |
| Filesystem | APFS | Internal NVMe / Thunderbolt NVMe | Chromium's ~450k small files stall on slow external (USB) volumes for both checkout and build. |
| macOS | Compatible release | Current supported release | Required for a compatible SDK and toolchain. |
| Toolchain | **Full Xcode** + macOS SDK | Latest compatible Xcode | Command Line Tools alone are insufficient; the gate fails if the active developer dir is CommandLineTools. |
| Checkout path | No spaces, absolute, outside the repo | A short path like `/Users/<you>/seoul-chromium.noindex` | The Chromium build does not tolerate spaces in paths; `common.sh` rejects unsafe paths outright. |

A USB SSD is acceptable for nothing on the critical path here - neither the checkout
nor the build. Use internal NVMe or a Thunderbolt NVMe volume.

### The one environment variable that matters

`SEOUL_CHROMIUM_ROOT` is the absolute path to the **external** Chromium checkout
root (depot_tools + `src` live under it). It must be outside the Project Seoul repo
and contain no spaces. If unset, the scripts default to a sibling directory named
`seoul-chromium.noindex` next to the repo. Set it explicitly on a new host so you know
exactly where tens of GiB will land:

```
export SEOUL_CHROMIUM_ROOT="/Users/<you>/seoul-chromium.noindex"
```

Do not export any credential, token, or signing identity. None is needed for an
unmodified component build.

## Scripts you will use (and what actually exists)

All scripts live in `native/scripts/`. To keep this runbook honest about the repo:

- The repository provides: `common.sh` (sourced helper, not run directly),
  `doctor.sh`, `fetch.sh`, `sync.sh`, `gen.sh`, `build.sh`, `run.sh`, `smoke.mjs`.
- There is **no** standalone `build-host-check.sh` and **no** standalone
  `verify-checkout.sh` in the repo. The build-host readiness gate is `doctor.sh`.
  Read-only checkout verification is `sync.sh --verify-only`. This runbook uses the
  scripts that exist; if you see those other names elsewhere, treat them as aliases
  for `doctor.sh` and `sync.sh --verify-only` respectively.

Run every script from the repo root, by its path, so it resolves `common.sh`
correctly. The scripts are non-destructive: they never reset or delete a checkout,
and `fetch.sh`/`sync.sh` refuse to touch a checkout that has user edits to tracked
files.

---

## Stage 1: Clean Project Seoul checkout

On the build host, clone the Project Seoul repo fresh. Do not copy a working tree
from another machine (it may carry untracked build evidence or local edits).

```
git clone <project-seoul-remote-url> <project-seoul-checkout>
cd <project-seoul-checkout>
git checkout native/chromium-baseline   # or the branch carrying this runbook
git rev-parse HEAD                       # record this; it goes in the manifest
git status                               # expect a clean tree
```

Confirm the pin and scripts are present:

```
cat native/chromium.lock.json
ls native/scripts
```

The repo contains **no** Chromium source and **no** build output by design. It only
carries the scripts, the lock, the baseline GN args, and docs.

## Stage 2: Locked Chromium fetch + sync (large and slow)

This is the long stage. It downloads tens of GiB and runs dependency hooks. Expect
it to take a long time even on fast storage and a fast link.

```
export SEOUL_CHROMIUM_ROOT="/Users/<you>/seoul-chromium.noindex"
native/scripts/fetch.sh
native/scripts/sync.sh
```

What each does:

- `fetch.sh` runs `doctor.sh` first as a preflight, then clones `depot_tools`,
  pins it to the locked SHA, and performs the initial Chromium checkout with
  `fetch --no-history --nohooks`. It wraps the network work in `caffeinate` so the
  host does not sleep mid-download. It refuses to overwrite a non-git path and skips
  re-fetching a valid clean checkout.
- `sync.sh` pins `src` to the locked Chromium revision (`git checkout --detach`),
  runs `gclient sync --no-history --with_branch_heads --with_tags
  --revision src@<rev> -D`, then `gclient runhooks`, and finally `gclient validate`.
  It verifies the lock both before and after, and refuses to sync a checkout with
  uncommitted edits to tracked files.

**Rate-limit awareness.** The upstream git server can return HTTP 429 (too many
requests) during the dependency sync. This is expected under load. Do not switch
mirrors or unpin to work around it. If a sync is rate-limited:

- Wait and re-run `native/scripts/sync.sh`. It is idempotent and resumes; it will
  not reset your tree.
- Avoid running multiple parallel syncs from the same network.
- Throttle your own concurrency rather than raising it.

The checkout is large (~48 GiB on disk for the source tree alone, before the build
output directory). Make sure the volume has the headroom from the requirements
table before you start.

## Stage 3: Read-only checkout verification

Before building, prove the checkout is exactly the pinned revision and carries no
accidental edits. This stage changes nothing.

```
native/scripts/sync.sh --verify-only
```

Expected: it prints the locked revision, prints the checkout HEAD, and reports
`OK: checkout is at the locked revision`. It exits nonzero if HEAD does not match
the lock. A gclient checkout normally shows modified submodule pointers and
untracked dependency directories - that is expected and is not treated as a user
edit. If it warns about "local edits to tracked files," stop and investigate; do not
build a tree you did not intend to build.

Record for the manifest:

```
git -C "$SEOUL_CHROMIUM_ROOT/src" rev-parse HEAD          # must equal the pin
git -C "$SEOUL_CHROMIUM_ROOT/depot_tools" rev-parse HEAD  # must equal the pin
```

## Stage 4: Build-host readiness gate (must pass)

`doctor.sh` is the readiness gate. It is read-only, never installs anything, and
never uses sudo. It **fails** (nonzero exit) on any hard prerequisite and **warns**
on soft ones.

```
native/scripts/doctor.sh
```

Hard checks (must be PASS, or the gate exits nonzero):

- Architecture is arm64.
- macOS is detectable (`sw_vers`).
- Checkout path has no spaces and is outside the Project Seoul repo.
- Full Xcode is active (not Command Line Tools) and `xcodebuild` is usable.
- A macOS SDK is reachable via `xcrun --show-sdk-path`.
- `git`, `python3`, and `curl` are present.
- Free space on the checkout volume is at least 120 GiB.

Soft checks (WARN only, do not block, but you should heed them):

- Checkout volume is APFS.
- RAM is at least 16 GiB (below that, the link may thrash or OOM).
- Free space is at least 180 GiB of headroom (between 120 and 180 GiB is "tight").

Do not proceed past a FAIL. The whole point of moving to a stronger host is to clear
the RAM and disk findings that gated the original machine. On a 32 GiB host with
>= 250 GiB free fast APFS storage and full Xcode, every line should be PASS with no
meaningful WARN.

## Stage 5: GN generation (component dev build, not shipping)

Generate the build directory from the baseline GN args. The args come from
`native/gn/macos-arm64-baseline.gn`; `gen.sh` appends `target_cpu = "arm64"` only
after confirming the host is Apple silicon, and writes the composed result to
`out/SeoulBaseline/args.gn` verbatim.

```
native/scripts/gen.sh --verify   # optional: print effective args, run no gn
native/scripts/gen.sh            # write args.gn and run `gn gen out/SeoulBaseline`
```

The effective args for this baseline are:

```
is_debug = false
is_component_build = true
symbol_level = 0
use_lld = false
target_cpu = "arm64"
```

These are **development-build** settings, deliberately chosen to keep peak link
memory and disk use low:

- `is_component_build = true` links many small shared libraries instead of one giant
  static binary. It is faster to link and far lighter on RAM, but it is **not** a
  shippable layout.
- `symbol_level = 0` drops debug symbols to save disk and link memory.
- `use_lld = false` uses the platform linker; kept explicit per the baseline spec.

This is **not** the shipping configuration. A release/shippable build would use a
static (non-component) build, official-build settings, and a different linker
strategy, and would then require packaging/signing/notarization - all out of scope
here. Do not edit `macos-arm64-baseline.gn` to add codec flags, security-disabling
flags, branding, or API keys. The baseline is unmodified Chromium.

Record the contents of `out/SeoulBaseline/args.gn` for the manifest.

## Stage 6: Unmodified chrome build

Build only the `chrome` target. Do not build `all`, unit tests, or browser tests.

```
native/scripts/build.sh
```

`build.sh` runs `autoninja -C out/SeoulBaseline -j <jobs> chrome` under
`caffeinate`, records start/end timestamps and elapsed time, and on success reports
the binary path and the output directory size. On failure it stops and instructs you
to capture the exact error and permit at most two narrow corrections - it explicitly
forbids broadening GN flags or disabling security to force a build.

### Configuring parallelism

The repo's `build.sh` currently hard-codes `JOBS=2`, which is a cautious setting for
a memory-starved 8 GiB machine, not an upstream requirement. On a stronger host you
will want more jobs. There is **no `SEOUL_NINJA_JOBS` environment variable in the
script today**; to make job count configurable, apply this one-line change so the
script honors `SEOUL_NINJA_JOBS` while defaulting to the safe value:

```
# in native/scripts/build.sh, replace:
JOBS=2
# with:
JOBS="${SEOUL_NINJA_JOBS:-2}"
```

Then choose jobs based on RAM headroom, not core count. A rough, conservative guide:

```
export SEOUL_NINJA_JOBS=4    # ~16 GiB RAM
export SEOUL_NINJA_JOBS=8    # ~32 GiB RAM
export SEOUL_NINJA_JOBS=12   # >= 64 GiB RAM, fast NVMe
native/scripts/build.sh
```

The link step is the memory bottleneck. If you see swap thrash or an OOM during
linking, lower `SEOUL_NINJA_JOBS` and re-run; ninja resumes from where it stopped.

On success the binary is:

```
$SEOUL_CHROMIUM_ROOT/src/out/SeoulBaseline/Seoul.app/Contents/MacOS/Seoul
```

Record the elapsed build time, the final output directory size, and the chosen job
count for the manifest.

## Stage 7: Smoke test

Run the launch smoke test. It uses the repo's pinned Puppeteer with an explicit
`executablePath` to the built binary, needs no internet (it loads a `data:` URL),
and modifies no Chromium source.

```
node native/scripts/smoke.mjs
```

It launches with a fresh temporary profile, then asserts:

- the browser reports a Chromium version string,
- JavaScript executes in the page (`1 + 2 + 3 === 6`),
- the DOM renders and is queryable,
- a second tab opens, activates, and reports its title,
- the browser stays connected with no disconnect or page crash,
- it closes cleanly and removes the temp profile.

Expected output ends with `SMOKE PASS`. If the binary is missing it prints
`SMOKE FAIL` with the path it looked for. Record the printed `browser version`
string and the PASS/FAIL result for the manifest. Set `SEOUL_HEADFUL=1` if you want
to watch it run with a visible window.

## Stage 8: Vertical-tabs runtime test

This confirms an upstream capability that already ships at the pinned revision. It
is **disabled by default**, so you must enable it. Verified source anchors at the
pinned revision (cite these in the manifest test notes):

- Feature: `BASE_FEATURE(kVerticalTabs, base::FEATURE_DISABLED_BY_DEFAULT)` at
  `chrome/browser/ui/tabs/features.cc:18`.
- chrome://flags entry: `{"vertical-tabs", ...}` at
  `chrome/browser/about_flags.cc:7001` -> `chrome://flags/#vertical-tabs`.
- Pref: `kVerticalTabsEnabled = "vertical_tabs.enabled"` at
  `chrome/common/pref_names.h:1371`.

Launch the built browser with a disposable profile (the helper passes extra args
straight through to Chromium):

```
native/scripts/run.sh --enable-features=VerticalTabs
```

Or enable it interactively: launch `native/scripts/run.sh`, open
`chrome://flags/#vertical-tabs`, set it to Enabled, and relaunch.

What to observe and record:

- A vertical tab strip appears along the side instead of (or alongside) the
  horizontal strip.
- Switching tabs, opening a new tab, and closing a tab all work from the vertical
  strip.
- Pinning/unpinning and tab groups render correctly in vertical orientation.
- The strip can resize/collapse as expected.
- Horizontal mode remains available when the feature is turned off again.

Record PASS/FAIL plus a one-line note (for example, "vertical strip rendered;
new/switch/close OK; groups OK").

## Stage 9: Split-view runtime test

Split view (two side-by-side contents panes) is **built in / on by default** at the
pinned revision - there is no single master feature flag for it. Verified source
anchors at the pinned revision:

- `MultiContentsView` is created unconditionally by `BrowserView`
  (`chrome/browser/ui/views/frame/multi_contents_view.{h,cc}`).
- Entry command `IDC_NEW_SPLIT_TAB` with accelerator Shift+Alt+N at
  `chrome/browser/ui/accelerator_table.cc:321`. On macOS, Alt is the Option key, so
  the keystroke is Shift+Option+N; the tab context menu is the most reliable
  trigger if the accelerator does not fire in your window.
- Two-pane cap documented at `chrome/browser/ui/views/frame/multi_contents_view.h:101`
  ("we currently only support two contents").

Launch the built browser:

```
native/scripts/run.sh
```

Then create a split via the tab context menu (right-click a tab -> the split entry)
or via `IDC_NEW_SPLIT_TAB` (Shift+Option+N on macOS).

What to observe and record:

- A second contents pane appears side by side with the first.
- You can load a different page in each pane.
- Switching the active pane works.
- The divider between panes can be dragged to resize.
- Closing one side leaves the other intact.
- A third split is not offered: the cap is two panes.

Record PASS/FAIL plus a one-line note (for example, "split created via tab menu;
two panes; resize OK; 2-pane cap confirmed").

## Stage 10: Artifact + log capture

Capture evidence without committing any Chromium source, build output, profile, or
secret to the Project Seoul repo.

Logs and operation evidence (untracked, outside the repo) live under the checkout's
own log directory, and temporary screenshots/raw build evidence may go under the
gitignored `native/evidence/`:

```
mkdir -p "$SEOUL_CHROMIUM_ROOT/.seoul-logs"
```

Suggested capture per build (tee command output into the log dir as you go):

- Full output of `doctor.sh`, `gen.sh`, `build.sh`, and `smoke.mjs`.
- The exact `out/SeoulBaseline/args.gn`.
- Toolchain versions: `xcodebuild -version`, `xcrun --show-sdk-version`,
  `xcrun --show-sdk-path`, `clang --version`, `sw_vers`.
- Host specs: `uname -m`, total RAM, free disk on the checkout volume.
- The built binary path and its `sha256`:

```
shasum -a 256 "$SEOUL_CHROMIUM_ROOT/src/out/SeoulBaseline/Seoul.app/Contents/MacOS/Seoul"
```

Do not commit any of this to the repo. Only written findings (like an updated
`docs/native/chromium-baseline.md`) and the build manifest below are appropriate to
share, and the manifest must contain no secrets.

## Build manifest

Produce one machine-readable manifest per successful build and store it alongside the
logs (outside the repo, or attached to wherever you track build provenance). It must
record exactly what was built, on what, and with what result - and must contain **no
credentials or machine secrets** (no tokens, no signing identities, no account ids,
no keychain references).

### Manifest schema (informal)

| Field | Type | Notes |
|---|---|---|
| `schemaVersion` | integer | Manifest format version (start at 1). |
| `buildId` | string | Free-form unique id for this build run (for example a timestamp slug). |
| `sourceRepository.repoCommit` | string | `git rev-parse HEAD` of the repository checkout. |
| `sourceRepository.branch` | string | Branch built from. |
| `chromium.version` | string | Product version from the lock. |
| `chromium.revision` | string | Full Chromium SHA; must equal the lock and the checked-out HEAD. |
| `depotTools.revision` | string | Full depot_tools SHA; must equal the lock. |
| `gnArgs` | object | The effective `args.gn` as key/value pairs. |
| `toolchain.xcode` | string | `xcodebuild -version` first line. |
| `toolchain.clang` | string | `clang --version` first line. |
| `toolchain.macosSdk` | string | SDK version + path from `xcrun`. |
| `toolchain.macos` | string | `sw_vers` product version + build. |
| `host.arch` | string | `uname -m` (expect `arm64`). |
| `host.ramGiB` | integer | Total RAM. |
| `host.freeDiskGiBAtStart` | integer | Free space on the checkout volume before building. |
| `host.filesystem` | string | Checkout volume filesystem (expect APFS). |
| `build.ninjaJobs` | integer | Job count used (`SEOUL_NINJA_JOBS` or default). |
| `build.startedAt` | string | ISO-8601 timestamp. |
| `build.finishedAt` | string | ISO-8601 timestamp. |
| `build.elapsedSeconds` | integer | Wall-clock build time. |
| `build.outputDirSize` | string | Human-readable size of `out/SeoulBaseline`. |
| `tests.smoke` | object | Result + captured `browser version`. |
| `tests.verticalTabs` | object | Result + one-line observation. |
| `tests.splitView` | object | Result + one-line observation. |
| `artifacts[]` | array | Each: logical name, absolute path, `sha256`. |
| `notes` | string | Anything noteworthy (rate-limit retries, corrections applied). |

### Manifest example

```json
{
  "schemaVersion": 1,
  "buildId": "seoul-baseline-2026-07-01T09-15-00Z",
  "sourceRepository": {
    "repoCommit": "<git rev-parse HEAD of repository checkout>",
    "branch": "native/chromium-baseline"
  },
  "chromium": {
    "version": "149.0.7827.201",
    "revision": "6a7b3dbec3b2ca25877c2553b5473b2f277ef644"
  },
  "depotTools": {
    "revision": "1e85b18d8037bca45f9a9891ae9325729f3ebcd4"
  },
  "gnArgs": {
    "is_debug": false,
    "is_component_build": true,
    "symbol_level": 0,
    "use_lld": false,
    "target_cpu": "arm64"
  },
  "toolchain": {
    "xcode": "<xcodebuild -version first line>",
    "clang": "<clang --version first line>",
    "macosSdk": "<sdk version> (<xcrun --show-sdk-path>)",
    "macos": "<sw_vers productVersion> (<sw_vers buildVersion>)"
  },
  "host": {
    "arch": "arm64",
    "ramGiB": 32,
    "freeDiskGiBAtStart": 260,
    "filesystem": "APFS"
  },
  "build": {
    "ninjaJobs": 8,
    "startedAt": "2026-07-01T09:15:00Z",
    "finishedAt": "2026-07-01T11:42:00Z",
    "elapsedSeconds": 8820,
    "outputDirSize": "78G"
  },
  "tests": {
    "smoke": {
      "result": "PASS",
      "browserVersion": "<value printed by smoke.mjs>"
    },
    "verticalTabs": {
      "result": "PASS",
      "observed": "vertical strip rendered; new/switch/close OK; groups OK",
      "sourceAnchor": "chrome/browser/ui/tabs/features.cc:18"
    },
    "splitView": {
      "result": "PASS",
      "observed": "split created via tab menu; two panes; resize OK; 2-pane cap confirmed",
      "sourceAnchor": "chrome/browser/ui/views/frame/multi_contents_view.h:101"
    }
  },
  "artifacts": [
    {
      "name": "chromium-binary",
      "path": "/Users/<you>/seoul-chromium.noindex/src/out/SeoulBaseline/Seoul.app/Contents/MacOS/Seoul",
      "sha256": "<shasum -a 256 of the binary>"
    },
    {
      "name": "args-gn",
      "path": "/Users/<you>/seoul-chromium.noindex/src/out/SeoulBaseline/args.gn",
      "sha256": "<shasum -a 256 of args.gn>"
    },
    {
      "name": "build-log",
      "path": "/Users/<you>/seoul-chromium.noindex/.seoul-logs/build.log",
      "sha256": "<shasum -a 256 of the log>"
    }
  ],
  "notes": "Dependency sync hit one HTTP 429 and completed on retry. No corrections applied to the build."
}
```

Fill every `<...>` placeholder with the real captured value. Leave out any field you
genuinely cannot determine rather than guessing, and never add a credential field.

## Not the shipping configuration

The `out/SeoulBaseline` build produced here is a **component development build of
unmodified Chromium**. It exists to prove the pinned source builds and runs and that
the upstream vertical-tabs and split-view capabilities work in our build. It is not a
release artifact:

- `is_component_build = true` and `symbol_level = 0` are dev choices, not the
  shippable layout.
- No official-build settings, no branding, no proprietary codecs, no
  security-disabling flags are applied (and must not be).
- **Packaging, code signing, and notarization are out of scope here.** A
  distributable Project Seoul build is a separate, later effort with its own GN
  configuration and signing pipeline, and it will be documented separately when it
  exists.

Keep this build honest: if any stage fails, record the failure in the manifest
`notes` and stop. Do not loosen GN flags or disable protections to force a green
result.
