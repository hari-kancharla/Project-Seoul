# Seoul adblock Rust dependency overlay

This directory is the checked-in, build-time-only Rust dependency closure for
Seoul's native blocker. It is materialized at
`//third_party/rust/seoul_adblock` so Chromium's generated third-party Rust
visibility rules continue to apply without changing Chromium's global Cargo
workspace.

The graph was generated with Chromium 149's `gnrt` from `adblock =0.12.0`,
with default features disabled and these features enabled:

- `full-regex-handling`
- `debug-info`
- `css-validation`
- `single-thread`

`single-thread` is intentional. Seoul owns each matcher on one dedicated
sequenced task runner; the browser UI thread never calls the Rust engine
directly.

`provenance.json` pins the Chromium/Brave reference, the adblock crate archive
checksum, its VCS revision, and every separately materialized package version.
Every crate also retains Cargo's normalized manifest, `.cargo_vcs_info.json`
when published, and its generated `README.chromium`.

The 39-package closure deliberately carries local copies of `bitflags`,
`regex`, and `regex-automata`. The blocker needs serde support in `bitflags`
and the complete regex feature set, while Chromium's global targets are built
with narrower features. The local `bitflags` target has distinct rustc metadata
so both graphs can coexist without a stable-crate-ID collision.

`native/scripts/check-adblock-rust-vendor.mjs` checks the revision pins,
package graph, local source and dependency paths, required features, license
paths, compatibility pins, and build-script inputs without network access.

The browser build and runtime perform no Cargo or network download.
