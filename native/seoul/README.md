# native/seoul - repository-owned Seoul native source

This directory is the single source of truth for Seoul-owned native code,
resources, and configuration. Everything here is tracked in the Project Seoul git
repository. The external Chromium checkout (`$SEOUL_CHROMIUM_ROOT`, default sibling
`seoul-chromium.noindex`) is never the source of truth and is never tracked.

## The model

Seoul = unmodified pinned Chromium (external checkout) + this tracked Seoul source
overlaid into it + a minimal, ordered, reversible patch series
(`../patches/chromium/`) that wires the overlay into the Chromium build.

```
native/seoul/        (tracked here)   -> materialized into  $SEOUL_CHROMIUM_ROOT/src/seoul/
native/third_party/rust/seoul_adblock/
                     (tracked here)   -> materialized into  $SEOUL_CHROMIUM_ROOT/src/third_party/rust/seoul_adblock/
native/patches/chromium/ (tracked)    -> applied onto       $SEOUL_CHROMIUM_ROOT/src/...
```

- `native/scripts/materialize.sh` copies this tree and the isolated, pinned
  blocker Rust closure into their dedicated checkout directories. It can verify
  or cleanly remove both. It never overwrites an upstream-owned file; only the
  patch series may modify upstream files.
- `native/scripts/patches.sh` applies / verifies / reverses the integration
  patches, in the order given by `../patches/manifest.json`.

If you only have the modified checkout, you have nothing authoritative: re-clone
Project Seoul, re-materialize, and re-apply. The checkout is disposable.

## Layout

The product implementation lives primarily under `browser/`: native services,
Views shell, Canvas WebUI, Mojo boundary, protocol codecs, provider adapters,
task execution, and their unit/browser tests. The top-level `webui/`,
`resources/`, and `config/` directories are reserved for cross-feature assets;
feature-owned resources stay beside their implementation (for example,
`browser/canvas/resources/`). Empty reserved directories must remain empty
rather than accumulating placeholder files.

- `browser/`   Seoul-owned C++ that integrates with the Chromium browser process.
- `webui/`     Seoul-owned WebUI (HTML/TS/CSS) surfaces served from the browser.
- `resources/` Static resources (icons, strings, GRD/GRDP inputs) owned by Seoul.
- `config/`    Build/config inputs owned by Seoul (GN snippets, feature configs).

## Rules

- No proprietary source, assets, branding, or layout copied from any other
  browser. Seoul copies feature capabilities, not source.
- Seoul-owned files live here and are materialized into the checkout; they are not
  hand-edited inside the checkout.
- Any change that must touch an unmodified Chromium file is a patch in
  `../patches/chromium/` with a manifest entry, never a direct edit committed only
  inside the checkout.
- Plain ASCII; no AI-assistant attribution anywhere.
