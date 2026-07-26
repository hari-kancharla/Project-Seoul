# Seoul shell interaction contract

Expected browser behavior for the later UI, defined against the organization engine.
This is a behavior contract, not a pixel design: it does not prescribe Arc's sidebar
or Zen's toolbar; Seoul's layout is its own. Each interaction lists trigger, visible
state change, model mutation, persistence effect, focus, accessibility, reduced
motion, and failure behavior. Mutations name real `OrganizationModel` methods.
Plain ASCII.

## Switch workspace
- Trigger: user selects another workspace (click/keyboard/gesture).
- Visible: the tab area swaps to the target workspace's tabs; active indicator moves.
- Model: `SwitchWorkspaceForWindow(window, target)` - transactional; activates a projected tab before committing active workspace.
- Projection: `WindowProjectionController` publishes updated `WindowProjection`; `VerticalPresentationFilter` refreshes vertical tab visibility.
- Persistence: window active mapping persisted (coalesced write) only after successful switch.
- Focus: focus moves to the target workspace's selected projected tab (pinned → retained → temporary priority).
- Accessibility: announce "Workspace <name> active"; expose active state on the item; hidden tabs excluded from traversal.
- Reduced motion: no slide animation; instant swap.
- Failure: archived target → `ProjectionError::kArchivedWorkspace`; activation failure preserves prior workspace; UI surfaces non-blocking message.
- Empty workspace: explicit empty state from `ProjectionStatus::kEmptyWorkspace`; no fabricated tab.

## Projection consumption (V0 shell contract)

The future shell reads projection state from `ProjectionService` / `WindowProjectionController`:

| Surface | Source |
| --- | --- |
| Workspace selector | `OrganizationModel` workspaces + per-window `active_workspace` |
| Tab list | `WindowProjection.tabs` ordered by projected order |
| Workspace-pinned | `TabRole::kPinned` within projection |
| Temporary / retained | `TabRole` within projection |
| Essentials | profile-global `EssentialRecord` (not per-workspace duplicated tabs) |
| Splits | `WindowProjection.splits` when both panes projected |
| Empty workspace | `projection.empty_workspace` |
| Degraded / fail-open | `ProjectionStatus` (`kDegraded`, `kFailOpen`, `kReconciliationRequired`) |
| Keyboard navigation | projected tabs only; hidden tabs not focusable |
| Focus restoration | last projected active tab per window/workspace |
| Accessibility | announce workspace switch; live region for degraded state |

Vertical-tab mode required for V0 projection visibility. Horizontal strip does not filter in V0.

## View global Essentials
- Trigger: Essentials surface is always visible across workspaces.
- Visible: the same Essentials render in every workspace (one identity).
- Model: read-only (`FindEssential`, snapshot).
- Persistence: none on view.
- Focus: activating an Essential focuses its single shared backing tab.
- Accessibility: Essentials are a labeled group, distinct from workspace tabs.
- Reduced motion: none.
- Failure: a missing backing tab re-opens at `root_url`; no duplicate is created.

## Pinned vs temporary tab (visual distinction)
- Trigger: rendering the tab list.
- Visible: pinned tabs grouped/persistent; temporary tabs in a clearly distinct,
  ephemeral-looking region.
- Model: read `TabMembershipRecord.role`.
- Persistence: none on view.
- Focus: unchanged.
- Accessibility: role conveyed in the accessible name ("pinned"/"temporary").
- Reduced motion: none.
- Failure: n/a (read-only).

## Retain a temporary tab
- Trigger: user keeps a temporary tab (explicit action).
- Visible: the tab moves out of the temporary region; archive countdown removed.
- Model: `RetainTab(id)`.
- Persistence: membership role persisted.
- Focus: stays on the tab.
- Accessibility: announce "Tab retained".
- Reduced motion: instant region change.
- Failure: missing tab -> `kTabMembershipNotFound`; already retained ->
  `kNoOpRejected` (no visible change, no false success).

## Move a tab between workspaces
- Trigger: drag/command a tab to another workspace.
- Visible: tab leaves the source list, appears in the target.
- Model: `MoveTabToWorkspace(id, target)` (removes it from any split first).
- Persistence: membership workspace + any dissolved split persisted.
- Focus: follows the tab if the target becomes active; otherwise stays in source.
- Accessibility: announce "Moved to <workspace>".
- Reduced motion: no fly animation.
- Failure: missing target -> `kWorkspaceNotFound`; same workspace ->
  `kNoOpRejected`.

## Close a tab
- Trigger: close control / shortcut.
- Visible: tab disappears; any split it was in updates or dissolves.
- Model: `RemoveTabMembership(id)`.
- Persistence: membership removed; split changes persisted.
- Focus: moves to the adjacent tab in the same workspace.
- Accessibility: announce "Tab closed"; focus target announced.
- Reduced motion: instant removal.
- Failure: missing -> `kTabMembershipNotFound`.

## Restore an archived tab
- Trigger: user picks a tab from the archive surface.
- Visible: tab reappears in its original workspace (or default), retained.
- Model: `RestoreArchivedTab(original_id, tab_key)`.
- Persistence: archive entry removed, new membership persisted.
- Focus: focus the restored tab.
- Accessibility: announce "Tab restored to <workspace>".
- Reduced motion: instant.
- Failure: missing entry -> `kTabMembershipNotFound`; the archive list is
  recoverable metadata, not a promise of a live renderer.

## Create a split
- Trigger: split command on two tabs in one workspace (e.g., upstream Shift+Alt/Option+N).
- Visible: two panes side by side with a divider.
- Model: `CreateSplitGroup(workspace, {a,b}, ratio, upstream_token)`.
- Persistence: split record persisted.
- Focus: the active pane (index 0) receives focus.
- Accessibility: panes are labeled regions; divider is a focusable separator.
- Reduced motion: no resize animation on creation.
- Failure: cross-workspace -> `kCrossWorkspaceSplit`; wrong arity ->
  `kInvalidSplitArity`; bad ratio -> `kInvalidDividerRatio`.

## Split focus / resize
- Trigger: click a pane or drag the divider.
- Visible: active pane highlight moves; divider ratio changes.
- Model: `UpdateSplitLayout(id, ratio, active_pane_index)`.
- Persistence: ratio + active pane persisted (coalesced).
- Focus: follows the active pane.
- Accessibility: active pane announced; divider exposes min/max ratio.
- Reduced motion: divider moves without easing.
- Failure: bad ratio / pane index -> `kInvalidDividerRatio` / `kInvalidActivePane`.

## Open a preview
- Trigger: preview gesture on a link.
- Visible: an ephemeral overlay shows the destination; underlying tab unchanged.
- Model: `PreviewManager::Open` creates one bounded ephemeral record for the
  exact live window and parent tab; a second open replaces only that window's
  non-promoting Preview.
- Persistence: none (previews are ephemeral, do not persist across restart).
- Focus: focus enters the preview; dismissal returns focus to the parent tab.
- Accessibility: preview is a modal-like region with a clear escape.
- Reduced motion: fade replaced by instant show/hide.
- Failure: a crash in the preview dismisses it without affecting the parent tab.

## Promote a preview
- Trigger: explicit promote action (to tab or to split).
- Visible: the preview becomes a real tab, or joins a split pane.
- Model: `BeginPromotion(kTab|kSplit)` locks the lifecycle; only a successful
  Chromium transfer may call `CommitPromotion`, while failure calls
  `AbortPromotion` and leaves the Preview ready.
- Routing: promotion re-evaluates the current rule set. A specific-Workspace
  result switches first and creates retained membership there; a split result
  creates a native split. Approval/external results reject before transfer.
- Persistence: the new membership/split persists; the preview identity ends.
- Focus: focus the promoted tab/pane.
- Accessibility: announce "Preview opened as tab".
- Reduced motion: instant.
- Failure: a pre-transfer routing, Workspace, parent-tab, split, or lifecycle
  failure restores the original Workspace when necessary and leaves the
  Preview ready. Once native ownership transfers, the Preview identity ends;
  the result is reported failed if its tab/split postcondition is not observed.
  A preview never becomes a retained tab implicitly.

## Compact mode
- Trigger: toggle compact browsing.
- Visible: chrome/sidebar auto-hides to maximize content.
- Model: a user-owned preference keyed by durable Workspace identity, separate
  from organization membership and temporarily overridden by an active Scene.
- Persistence: stored with bounded product presentation state and reapplied on
  Workspace switch and relaunch.
- Focus: unchanged content focus; revealed chrome is reachable by keyboard.
- Accessibility: hidden chrome must remain keyboard- and screen-reader-reachable.
- Reduced motion: no slide; instant hide/show.
- Failure: direct changes are rejected with a clear reason while a Scene owns
  compact presentation; native completion is verified before success.

## Keyboard navigation
- Trigger: keyboard shortcuts for workspace switch, new/close tab, split, command
  surface.
- Visible: matches the equivalent pointer action.
- Model: the same methods as the pointer paths.
- Persistence: same as the equivalent action.
- Focus: every action defines a deterministic next-focus target.
- Accessibility: all organization actions are keyboard-reachable with announced
  state changes.
- Reduced motion: applies to all.
- Failure: same typed errors; surfaced non-blocking.

## Multiple windows
- Trigger: open/use a second window.
- Visible: each window shows its own active workspace; the same workspace can appear
  in two windows sharing one tab model.
- Model: per-window `WindowWorkspaceState`.
- Persistence: per-window active mapping persisted.
- Focus: per window.
- Accessibility: each window announces its active workspace.
- Reduced motion: applies.
- Failure: closing the last window of a workspace leaves the workspace intact;
  conflicts resolve to the model as the single source of truth.

## Startup restoration
- Trigger: browser launch for an eligible regular profile.
- Visible: workspaces, pins, and splits reappear; Chromium restores live tabs.
- Model: `LoadSnapshot` then `EnsureDefaultWorkspace` (idempotent).
- Persistence: read-only on load; one normalizing write after init.
- Focus: the last active workspace/tab per window.
- Accessibility: restored structure is announced on first focus.
- Reduced motion: no startup animation required.
- Failure: corrupt/unsupported stored data is not overwritten; the last known valid
  state is preserved and a default workspace is guaranteed; startup never blocks on
  a network service.

## Empty workspace
- Trigger: a workspace with no tabs is active.
- Visible: an explicit empty state (Seoul's own, not a blank void).
- Model: no memberships for the workspace.
- Persistence: none.
- Focus: focus the new-tab affordance.
- Accessibility: empty state is labeled and actionable.
- Reduced motion: n/a.
- Failure: n/a.

## Error and recovery states
- Trigger: any mutation returns a typed `OrganizationError`.
- Visible: a non-blocking, specific message (never a generic "something went wrong").
- Model: the mutation made no change (atomic failure).
- Persistence: nothing written on failure.
- Focus: stays where it was.
- Accessibility: errors are announced via a polite live region.
- Reduced motion: applies.
- Failure: the engine never reports false success; `kNoOpRejected` means the action
  legitimately changed nothing.
