# native/seoul/browser

Seoul-owned C++ that runs in the Chromium browser process and integrates with
upstream subsystems (tabs, sessions, profiles, views/WebUI, prefs).

Contract:

- Files here are materialized to `$SEOUL_CHROMIUM_ROOT/src/seoul/browser/`.
- Code here is Seoul-authored. It is wired into the build by GN snippets under
  `../config/` and by minimal integration patches in `../../patches/chromium/`
  (for example, adding a dependency edge or a call site in an upstream file).
- Prefer extending existing upstream subsystems over forking them. The upstream
  vertical-tab and split-view implementations must be extended, not reimplemented.
- No upstream Chromium file is edited here. Upstream edits are patches.

This directory is the active native product implementation. It contains the
profile runtime, organization/projection/command layers, vertical Shell,
Canvas, Preview, Tasks, voice, Boards/Library, Boosts/Site Layers, provider
routing, Scenes, Themes, workflows, policy, and their test targets. Do not add
placeholder source, demo-only state, or a second owner for Chromium tab state.
