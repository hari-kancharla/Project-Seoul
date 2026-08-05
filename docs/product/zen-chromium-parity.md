# Zen-on-Chromium parity contract

Last researched: 2026-07-26.

This document is the implementation contract for replacing Project Seoul's
current browser shell with a Chromium-native port of Zen Browser. "Parity"
means matching Zen's visible hierarchy, interaction behavior, persistence,
preferences, accessibility, and failure behavior. It does not mean merely
using vertical tabs or applying a similar color palette.

## Reference baseline

The canonical references, in priority order, are:

1. `zen-browser/desktop` `dev` source as retrieved on 2026-07-26;
2. Zen `1.21.9b`, released 2026-07-23, for stable behavior;
3. Zen `1.22t` for the current Space sync preview and changes already present
   in `dev`;
4. Zen's official user manual and release notes;
5. captured Zen windows at standard macOS sizes and both color schemes.

The source baseline identifies itself as Zen Browser `1.21.9b` / Twilight
`1.22t` on Firefox `153.0`. Project Seoul remains on Chromium
`149.0.7827.201`; Firefox/XUL implementation details are translated onto
Chromium systems rather than emulated.

Zen is MPL-2.0 licensed. Any source or asset copied or adapted from Zen must
retain its file-level notice and remain available under MPL-2.0. Independently
written Chromium adapter code remains under Project Seoul's chosen license.

## Non-negotiable visual structure

The default expanded, single-toolbar window must contain:

1. a tinted or system-material window background;
2. macOS traffic lights integrated into the sidebar's top controls;
3. compact navigation controls and the native Chromium omnibox in the sidebar;
4. a centered floating omnibox/results surface while editing;
5. Essentials as favicon tiles in a responsive grid;
6. space-local pinned tabs;
7. the active Space row, including its icon, name, collapse affordance, and
   hover actions;
8. regular tabs and nested folders;
9. the new-tab action;
10. Space icons and sidebar utility buttons at the bottom;
11. page content inset by the configured element separation and clipped to the
    matching inner corner radius.

The default macOS measurements derived from Zen source are:

| Token | Zen value |
|---|---:|
| Content separation | 8 px |
| Expanded sidebar inner padding | 6 px |
| Expanded regular tab height | 44 px |
| Collapsed sidebar width | 60 px (48 px tab + 6 px each side) |
| Workspace/Space row height | 44 px expanded, 38 px collapsed |
| Sidebar toolbar height | 38 px on macOS |
| Default content corner radius | 5 px (`max(5, 10 - 8/2)`) |
| Tab block margin | 2 px |
| Expanded tab inline padding | 8 px |
| Essentials grid gap | 4 px |
| Default maximum Essentials | 12 |

These are baseline tokens, not scattered literals. User theme settings can
change separation, accent, color scheme, texture, and radius while preserving
the hierarchy and layout invariants.

## Feature parity matrix

Status values:

- `base`: a usable Chromium primitive already exists;
- `partial`: Seoul has some model or UI support, but not Zen behavior;
- `missing`: no end-to-end implementation exists;
- `replace`: the current Seoul surface conflicts with the Zen contract.

| Zen capability | Current state | Chromium port requirement |
|---|---|---|
| Single-toolbar vertical shell | partial | Replace the current generic rail styling and two-row toolbar layout with the exact Zen hierarchy, tokens, states, and rounded content frame. |
| Multiple-toolbar layout | base | Preserve Chromium's full top toolbar as a user-selectable Zen layout and apply Zen content framing. |
| Collapsed toolbar layout | partial | Match Zen's 60 px icon rail, visibility rules, tooltips, drag behavior, and resize snapping. |
| Right-side tabs | base | Expose and persist placement; mirror content insets, resize edge, floating UI, and traffic-light rules. |
| Floating URL bar | partial | Keep Chromium's security model and autocomplete, but present the editing/results surface centered over content with Zen sizing, rows, actions, and focus behavior. |
| Domain-only resting URL | partial | Single Toolbar now uses Chromium's native display/edit URL split to show the formatted host at Zen's exact 70% resting text strength and reveal the full-strength editing URL on focus without changing canonical copy/navigation/security state; controlled runtime visual comparison remains required. |
| Essentials | partial | Limit to 12 by default, render favicon tiles, support global or per-Space modes, live/unloaded states, drag promotion/demotion, reset/close behavior, container isolation, and automatic Glance for external links. |
| Pinned tabs | partial | Match Space-local persistence, reset-to-pinned-URL semantics, unload/close shortcut behavior, labels/sublabels, drag rules, and collapsed section behavior. |
| Spaces | partial | Rename the visible concept, match icon strip, swipe/wheel switching, wrap-around, animations, reorder/create/edit/delete UI, default container, last-active behavior, and keyboard shortcuts. |
| Space Routing | partial | Connect existing typed routing rules to link, omnibox, external-launch, and new-tab entry points; add Zen-compatible rule editing and routing UI. |
| Space sync | missing | Add sync serialization boundaries and merge behavior. Do not claim cross-device support until a real Chromium sync datatype and conflict tests exist. |
| Tab folders | base | Restyle Chromium tab groups as Zen folders; add nested folders up to depth 5, ownership rules, search popup, rename/reset/close actions, drag thresholds, and persistence. |
| Live folders | partial | Bind provider-backed GitHub issue/PR and RSS collections directly to sidebar folders, including refresh, loading, error, empty, auth, and last-good states. |
| Split View | partial | Raise the Seoul schema from two to four panes; implement horizontal, vertical, and grid layouts, drag/drop creation and rearrangement, per-pane overlay controls, resizing, Glance promotion, shortcuts, and restore. |
| Glance | partial | Restyle and complete Preview as an overlay WebContents with outside-click close, expand-to-tab, split promotion, modifier trigger, Essentials/pinned automatic trigger, history, permissions, downloads, focus, and crash recovery. |
| Compact Mode | partial | Hide sidebar, toolbar, or both according to layout; edge reveal with Zen delays; persistent keyboard reveal; background-tab toast; animation and reduced-motion behavior. |
| Window Sync | partial | Mirror the same live tab graph across local windows, show inactive previews, support temporary unsynced windows, reconcile activation, and create recoverable backups. |
| Session recovery | partial | Persist and restore Spaces, folders, Essentials, pinned state, split geometry, selected tabs, custom labels, routes, and compact/layout state with rolling backups and corruption recovery. |
| Media controls | missing | Add sidebar media card/state, play/pause, mute, PiP, metadata/artwork, multiple-media selection, unload protection, and accessibility. |
| Download animation | partial | The native current-Zen arc, scale, lower-edge fallback target, left/right placement, bounded duration preference, and Chromium reduced-motion/download-lifecycle gates are implemented and compiled; controlled runtime visual comparison remains required. |
| Command/URL actions | partial | Port Zen global and Space-scoped quick actions into Chromium omnibox results, with ranking/learning, shortcut labels, stale-target rejection, and full keyboard operation. |
| Editable shortcuts | partial | Expose all Zen actions, detect conflicts, persist overrides, restore defaults, and test platform modifiers. |
| Themes | partial | Bind Seoul theme tokens to the whole native browser chrome, add gradients, system/light/dark schemes, texture, transparency/material, accent, separation, radius, inactive-window treatment, and live preview. |
| Mods registry | missing | Implement safe downloadable browser-chrome theme packages, update lifecycle, enable/disable, provenance, rollback, and compatibility checks without granting arbitrary native execution. |
| Boosts | partial | Preserve Seoul's existing CSP-safe per-site CSS engine, then add Zen's selector, editor, color/layout controls, zap/dissolve interaction, enabled state, persistence, size overrides, and UI entry points. |
| Site controls | base | Add Boost and Zen-equivalent site actions without weakening Chromium identity, permission, certificate, or privacy indicators. |
| Welcome/onboarding | replace | Replace Canvas-first onboarding/new-tab ownership with Zen's browser-layout, theme, import, default-browser, Essentials, and Space setup flow. Product-specific Seoul features can be layered later. |
| Settings | replace | Add native/WebUI settings for every parity preference and remove claims for unavailable behavior. |
| Share | base | Integrate platform share UI and tab/link context actions. |
| Drag and drop | partial | Port Zen thresholds, insertion indicators, Space hover switching, Essentials promotion, folder nesting, split targets, cross-window behavior, haptics, cancellation, and rollback. |
| Accessibility | partial | Match semantic roles/names/states, focus order, keyboard reachability, zoom/text scaling, contrast, reduced motion, screen-reader announcements, and hit targets for every new surface. |

## Architecture rules

1. Chromium remains the sole owner of `WebContents`, navigation, tab
   activation, history, permissions, downloads, identity UI, profiles, and
   renderer isolation.
2. Chromium's vertical tab strip, tab groups, split model, omnibox, side-panel
   hosting, session service, and sync infrastructure are extended in place.
3. Seoul owns the Zen-compatible organization metadata and presentation
   policy, but never keeps a second live tab model.
4. Views code renders browser chrome. WebUI is reserved for settings, welcome,
   editors, and other document-like surfaces.
5. Every visual token comes from one theme object and every behavior preference
   comes from one typed preference registry.
6. Every model mutation has a postcondition observation and a failure path.
7. No feature is marked complete based on a static mock, design lab, or
   unit-only implementation.

## Verification contract

Each feature requires all applicable evidence:

- model/unit tests;
- Chromium in-process browser tests;
- two-process relaunch tests for persistent state;
- keyboard-only and accessibility-tree checks;
- light, dark, system, compact, collapsed, right-side, private, inactive-window,
  and reduced-motion layout coverage;
- screenshots at 800×600, 1280×800, 1440×900, and a wide display;
- comparisons against captured Zen reference states with explicit tolerances;
- interactive testing in the actual compiled Chromium app;
- no crashes, DCHECK failures, console errors, clipped controls, phantom
  gutters, focus traps, or first-paint stock-Chromium flashes.

The product is not complete while any matrix row remains `partial`, `missing`,
or `replace`, or while the release build, packaging, signing, notarization,
update, migration, privacy, and security gates remain open.
