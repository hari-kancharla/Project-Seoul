# Seoul organization semantics v0

The exact semantics implemented by the native engine in
`native/seoul/browser/organization/`. This is the contract; the code enforces it
and the unit tests check it. Nothing here implements UI, AI, or page automation.
Plain ASCII.

## 1. Profile

The Chromium profile remains the security and data-isolation boundary. Workspaces
never replace or silently cross profiles.

- Regular profile: eligible. Exactly one organization store per regular profile.
- Incognito / off-the-record: NOT organized or persisted. The factory
  (`ProfileSelections` with `WithRegular(kOriginalOnly)`) gives no service to OTR.
- Guest: excluded (`WithGuest(kNone)`). No workspace state written.
- System / Ash-internal: excluded (`WithSystem(kNone)`, `WithAshInternals(kNone)`).
- Multiple regular profiles: each has its own independent store and ids; a tab,
  workspace, or split from one profile is never referenced from another.
- Account/container isolation: a FUTURE capability that will map onto profiles; the
  routing model already reserves a container/account predicate slot. Not in v0.

## 2. Workspace

A persistent area of activity, not a tab group, window, profile, folder, or URL
list. Fields (see `WorkspaceRecord`): stable `WorkspaceId` (UUID, never changes),
user-editable `name` (not an identity), optional `icon` reference, integer `order`,
`created_at`, `last_active_at`, `archived`, `is_default`. Workspace-owned objects
(pinned/temporary/retained tabs, splits) and optional routing rules reference the
workspace by id (normalized, not embedded). Active state is per browser window.
Persistence is versioned.

## 3. Global Essentials

Seoul's improved Favorites/Essentials. An Essential is **one profile-global
persistent destination with a single identity**, not a per-workspace copy.

- Ownership: the profile (global), not a workspace.
- Visibility: shown across all workspaces by being global; it is never duplicated as
  a separate live tab per workspace. At most one shared live backing tab exists.
- Saved destination: `root_url` is the saved destination; live navigation state
  lives in the (Chromium-owned) live tab, not in the record.
- Reset to root: returns the backing tab to `root_url`.
- Memory suspension / close / restore: the backing tab may be suspended and later
  restored to `root_url`; the Essential record persists regardless.
- Opening external links from an Essential follows the routing rules (preview,
  temporary tab, or a rule result); it does not hijack the Essential's own tab.

## 4. Workspace-pinned tabs

Pinned tabs belong to exactly one workspace (`TabMembershipRecord` with
`role = kPinned`).

- Saved root URL vs live URL: `saved_root_url` is the reset target; the live URL is
  the Chromium tab's current navigation.
- Reset: returns the tab to `saved_root_url`.
- Closing vs unpinning: closing removes the membership (`RemoveTabMembership`);
  unpinning keeps the tab and makes it retained (`UnpinTab` -> `kRetained`).
- Memory suspension and session restore: handled by Chromium for the live tab;
  Seoul persists only the membership metadata.
- Moving between workspaces: `MoveTabToWorkspace` (removes it from any split first).
- Folders/collections: deferred past v0 (ordering exists today).
- Distinct from bookmarks: a pin is a live tab membership with a reset target, not a
  passive bookmark entry.

## 5. Temporary tabs

Explicit lifecycle (`role = kTemporary`).

- Creation: any new tab not explicitly retained/pinned (bridge-assigned).
- Retained vs temporary: `RetainTab` promotes to `kRetained`; `PinTab` to `kPinned`.
- Auto-archive eligibility: `EligibleForAutoArchive(activity, now, threshold)`
  returns temporary tabs inactive for >= threshold AND not protected.
- Protected (never auto-archived) when ANY holds: playing media, active download,
  active task, permission prompt, in a split, active DevTools, unsaved form, still
  loading, or recent activity. Pinned and retained tabs are never eligible.
- Auto-archive is NOT a blind timer: it is a gated query the caller runs, then
  `ArchiveTab` is applied only to eligible, unprotected tabs.
- Crash recovery / unsaved state: Seoul never fabricates a live tab; recovery
  restores metadata only, and protection covers unsaved-form and media tabs.

## 6. Tab archive

Recoverable metadata, not a live renderer and not a second copy of history
(`ArchivedTabRecord`).

- Stored: original membership id, workspace id, saved root url, optional last title,
  archived-at time. No page content, history, or form values.
- Restore: `RestoreArchivedTab` recreates a membership (retained role) in the
  original workspace, or the default if it is gone.
- Retention: bounded at `kMaxArchivedTabs` with oldest-first eviction.
- Manual clear / auto expiry: eviction is by capacity; manual clear is a future op.
- Relationship to Chromium history/session restore: independent; Seoul does not
  duplicate Chromium history. A tab is either live or archived, never both.

## 7. Split group

Uses Chromium's M149 split model (`split_tabs::SplitTabId` / `MultiContentsView`).
`SplitGroupRecord` fields: `SplitGroupId`, `workspace_id`, ordered `pane_tab_keys`
(exactly two in v0, stored as a list to allow growth), `divider_ratio`
(0.1..0.9), `active_pane_index`, `upstream_split_token` (the serialized upstream
split id), `created_at`.

- Workspace ownership: a split belongs to exactly one workspace and may reference
  only that workspace's tabs (`kCrossWorkspaceSplit` otherwise).
- One pane closes / tab removed: the tab is dropped from the split; if it falls
  below two panes the split dissolves.
- A participating tab moves workspace: the tab is removed from the split first
  (dissolving it if needed), preserving the cross-workspace invariant.
- Restoration: persisted and validated on load.
- Multi-pane: only `kMaxSplitPanesV0` changes later; no four-pane assumption is
  hardcoded.

## 8. Preview (source-connected and runtime-verified)

Seoul's Peek/Glance equivalent. The organization model reserves the `kPreview`
routing disposition, while `native/seoul/browser/preview/` owns the bounded
ephemeral lifecycle and the source-connected Chromium host. A preview has an
ephemeral identity, exact parent tab and window, destination, loading/error
state, explicit promotion-to-tab and promotion-to-split paths, dismissal, and
defined permission/download/popup/navigation/crash behavior. `PreviewHostService`
owns a separately created non-tab `WebContents` in a native bubble; the typed
`browser.preview.open` capability reaches it through exact window, tab,
main-frame, source-origin, and destination-origin permission scope. Promotion
is two-phase (`BeginPromotion`, same-`WebContents` Chromium transfer, then
`CommitPromotion` or `AbortPromotion`) and the lifecycle bridge classifies the
inserted tab as retained through an exact bounded handshake. Promotion
re-evaluates the active routing policy: it can land in a specific Workspace or
native split, while approval/external routes reject before transfer and leave
the Preview ready. A preview must
NEVER silently become an ordinary retained tab, and previews do NOT persist
across restart. The pinned integration patch adds a localized, DLP-gated
**Preview link** command to Chromium's existing link context menu and passes
the exact source `WebContents` into the same host. A faster modifier/hover
gesture beyond that menu remains outstanding. The host, routing rejection,
Workspace promotion, retained membership, and native-split promotion are
compiled and covered by isolated headless in-process browser tests.

## 9. Window projection

A browser window is a view onto workspace state, not the sole owner
(`WindowWorkspaceState`: opaque `window_key` -> active `WorkspaceId`).

- Each window has at most one active workspace.
- Multiple windows may show the same workspace; they share the one live tab model
  (Seoul does not duplicate tabs per window).
- Window closure: removes the window's active mapping; workspace state persists.
- Last window showing a workspace closes: the workspace remains; its tabs are
  governed by normal lifecycle/session restore.
- Switching / moving tabs across windows: model mutations, not view mirroring.
- Conflict resolution: the model is the single source of truth; window mappings are
  reconciled to it. Archiving/deleting an active workspace reassigns affected
  windows to a deterministic fallback.

## 10. Link routing

Future-capable, fully deterministic (`RoutingRule`, `EvaluateRouting`).

- A rule predicate may consider: destination origin (exact), URL prefix, URL glob,
  source workspace scope, and required user gesture. Reserved for the future:
  profile/container/account context and requested disposition.
- A result disposition: current tab, new temporary tab, new retained tab, specific
  workspace, preview, split pane, external application, or ask the user.
- Deterministic priority: highest `priority` wins, ties broken by rule id.
- Loop prevention: single-hop evaluation (`kMaxRoutingHops = 1`); no chaining.
- Safe fallback: an unmatched request resolves to the current tab with
  `used_fallback = true`. A rule pointing at a missing/archived workspace is
  skipped.
- Validation: rules with empty patterns (for non-anything matches) or dangling
  workspace references are rejected at add/load time.
- No website name, URL, or workflow is hardcoded anywhere.

## Non-negotiable invariants (enforced + tested)

1. Every eligible regular profile has exactly one default workspace.
2. Workspace identifiers never change; names are not identifiers.
3. An archived workspace cannot be active (archiving reassigns active windows).
4. The default workspace cannot be deleted or archived.
5. Removing/archiving the active workspace selects a deterministic fallback.
6. Each browser window has at most one active workspace.
7. A tab (tab_key) belongs to at most one workspace in a profile.
8. A split group belongs to exactly one workspace.
9. A split cannot reference tabs outside its workspace.
10. A tab cannot be both archived and live.
11. A protected temporary tab is never auto-archived.
12. Global Essentials are not duplicated as separate live tabs per workspace.
13. Incognito organization state is not persisted into the regular profile.
14. Guest and system-profile state is never written to the user's workspace store.
15. Unknown/future schema versions are rejected, never downgraded or overwritten.
16. Corrupt persistence does not destroy the last known valid state (atomic load).
17. No mutation silently succeeds when invalid state made it a no-op
    (`kNoOpRejected`).
18. No website names, URLs, or workflows are hardcoded.
19. No AI or network dependency for ordinary organization; startup does not block on
    a network service.

## Persistence schema summary

One bounded, versioned dict preference (`seoul.organization.v1`) per regular
profile. The value carries `schema_version`. Chromium owns tab contents, history,
cookies, passwords, form state, downloads, and session data; Seoul stores only the
organization metadata that relates Chromium objects to workspaces. Strict
deserialization, deterministic serialization, explicit bounds
(`organization_limits.h`), and a serialized-size cap. Details:
`docs/native/chromium-baseline.md` is the build doc; the format lives in
`organization_store.{h,cc}`.

## Performance-sensitive decisions

- Workspace switching updates only in-memory active mappings and never scans
  Chromium history.
- Persistence is a single in-memory `PrefService` write (flushed asynchronously by
  Chromium), so no blocking file I/O is added to the UI thread.
- Routing evaluation is a single bounded pass (`<= kMaxRoutingRules`), no chaining.
- Restoration is idempotent: `EnsureDefaultWorkspace` never creates a second
  default; `LoadSnapshot` replaces state atomically; crash recovery restores
  metadata only and cannot fabricate a live tab.
- No organization-specific benchmark numbers are claimed; measured build and
  smoke evidence is tracked in the readiness report.
