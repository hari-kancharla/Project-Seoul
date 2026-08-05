# native/patches - Chromium integration patches

Seoul integrates with Chromium through two tracked mechanisms:

1. **Repository-owned source** under `../seoul/`, materialized into the checkout at
   `src/seoul/` by `../scripts/materialize.sh`. This is where Seoul's own code
   lives. It never edits upstream files.
2. **A minimal, ordered patch series** here, applied over the pinned base revision
   by `../scripts/patches.sh`, for the unavoidable cases where an upstream Chromium
   file must be touched (a dependency edge, a registration call site).

Files:
- `manifest.json` - the machine-readable, ordered series and its per-entry schema.
  `baseRevision` must equal the pinned revision in `../chromium.lock.json`.
- `chromium/` - the actual `.patch` files referenced by the manifest.

The current series contains 23 patches. Patches `0001` through `0010` establish
the native integration, session model, integrated vertical shell, product
surface, identity, and iconography. Patches `0011` through `0016` refine the
address prompt, Compact timing, command surface, layout migration, native
materials, state colors, and platform-specific radii. Patch `0017` carries the
current source-matched Zen interaction checkpoint, including Compact geometry,
startup-placeholder lifecycle, native vectors, macOS traffic-light handling,
shutdown ordering, and development keychain policy. Patch `0018` connects
current Zen's curved download-origin animation to Chromium's native
download-start lifecycle. Patch `0019` uses Chromium's safe display/edit URL
split to show a host-only resting URL in Single Toolbar and reveal the complete
editing URL on focus.
Patch `0020` registers Seoul's profile-keyed blocker and places its tested
browser-process URLLoaderFactory proxy ahead of Chromium's downstream request
factory chain, with a separate asynchronous WS/WSS handshake gate that
preserves Chromium's extension proxy and direct-network continuation paths,
plus a deferred primary-main-frame NavigationThrottle for blocked documents
and redirect targets.
Patch `0021` adds separate Chromium website-setting schemas for a persistent
per-site blocker mode and an expiring temporary disable, including exhaustive
settings/UMA registration.
Patch `0022` registers Seoul's document-scoped asynchronous cosmetic-filter
host and bounded CSS-only renderer agent, including fixed isolated-world,
prerender, BFCache, cross-origin-frame, and renderer-test integration.
Patch `0023` registers the fail-closed signed filter-list component at
Chromium's update and Local State seams; an owner-supplied release key hash is
required before the component is registered.

Every Chromium modification must remain:

- **Minimal** - the smallest change that achieves the goal; never a broad refactor.
- **Documented** - the manifest entry records description, rationale, and the
  upstream alternative that was rejected.
- **Ordered** - a unique ascending `order`; applied and reversed deterministically.
- **Checksummed** - a `sha256` in the manifest that matches the file on disk.
- **Verifiable** - applies with `git apply --check` and reverses with
  `git apply -R --check`.
- **Independently reviewable** - each patch stands on its own, against the exact
  locked revision, without depending on later patches.

Validate the manifest with `node native/scripts/check-patch-manifest.mjs` and the
apply/reverse behavior with `native/scripts/patches.sh verify`. Patches are an
overlay over a pinned upstream tree; they are never a substitute for extending the
existing upstream vertical-tab and split-view implementations, which must be
exhausted first. See `../README.md` for the architecture boundary.
