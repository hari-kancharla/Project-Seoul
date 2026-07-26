# Browser Command Layer Research Record

Status: historical pre-implementation research. The outbound command executor,
typed validation, live target resolver, observation confirmation, Shell
dispatch, and browser tests are now implemented under
`native/seoul/browser/commands/`, `projection/`, and `shell/`. This document
preserves the API research and original command-by-command reasoning; it is not
the current readiness authority. See
`docs/release/seoul-product-readiness.md` for compiled behavior and evidence.

Historical status at authoring: this began as an inbound-only design record.
The prose below is intentionally preserved as the original contract, including
then-open research notes. Do not read it as a current implementation inventory.

Pinned Chromium revision: 6a7b3dbec3b2ca25877c2553b5473b2f277ef644

All Chromium API citations in this document were confirmed by reading header
files in the external Chromium checkout at the pinned revision. Where an API
could not be confirmed, the section reads RESEARCH REQUIRED. No method
signatures are invented.

---

## 1. Data-flow diagram

### 1.1 Current milestone: inbound only (IMPLEMENTED elsewhere, shown for context)

```
  +------------------+      observer       +---------------------+      +----------------------+
  | Chromium tab /   |  ---- event ----->  | Seoul lifecycle     | ---> | Organization Engine  |
  | window / strip   |  (user-originated)  | bridge (inbound)    |      | (workspaces, splits, |
  |                  |                     | normalizes + tags   |      |  previews, archive)  |
  +------------------+                     +---------------------+      +----------------------+
```

### 1.2 Original outbound command-layer design

```
  +----------------------+    command     +----------------------+   validated    +------------------+
  | Organization Engine  | ------------>  | Command layer        | ------------>  | Chromium tab /   |
  | (Seoul intent)       |   request      | (validate, tag,      |   mutation     | window / strip   |
  +----------------------+                |  reconcile)          |                +------------------+
            ^                             +----------------------+                         |
            |                                       ^                                      | normal Chromium
            |                                       |                                      | observer event
            |                                       |                                      | (command-originated)
            |                                       |                                      v
            |                              +---------------------+      reconciliation +------------------+
            +------------------------------| Seoul lifecycle     | <----------------- | observer arrives |
              authoritative state update   | bridge (inbound)    |   tag inspection   | at inbound bridge|
                                           +---------------------+                    +------------------+
```

### 1.3 The single most important rule: avoid the feedback loop

Outbound mutations go through the real Chromium model. The real Chromium model
then emits its NORMAL observer events. Those events flow back into the SAME
inbound bridge that handles user-originated events. Without discipline this is
an infinite or double-apply loop: Seoul tells Chromium to do X, Chromium fires
an event saying X happened, the inbound bridge applies X again.

The contract avoids this by TAGGING every mutation at the source:

- Observer-originated mutation: a Chromium event with NO matching outstanding
  command tag. The inbound bridge applies it to the Organization Engine.
- Command-originated mutation: a Chromium event that carries (or correlates to)
  a command tag minted by the command layer. The inbound bridge MUST NOT
  re-apply the organization mutation; it only RECONCILES (confirms the command
  landed, clears the pending tag, resolves the command promise).

Original correlation design: the command layer mints a
monotonic command id and records an expectation record (target tab/window
handle, expected change Type, expected resulting index/state). When the matching
observer event arrives the inbound bridge matches it against the outstanding
expectation set BEFORE deciding to apply vs reconcile. Matching is on stable
handles (tab handle / SessionID), never on raw index, because indices shift.

Tagging is mandatory for EVERY command below. A command with no expectation
record registered before the Chromium call is a defect, because the resulting
observer event would be misclassified as user-originated and double-applied.

---

## 2. Confirmed Chromium API surface (pinned revision)

These are the real, confirmed entry points referenced by the commands. All
paths are relative to the external Chromium checkout root.

- Insert a tab:
  `TabStripModel::InsertWebContentsAt(int index, std::unique_ptr<content::WebContents> contents, int add_types, std::optional<tab_groups::TabGroupId> group)`
  chrome/browser/ui/tabs/tab_strip_model.h:278
- Insert a detached tab (move between strips):
  `TabStripModel::InsertDetachedTabAt(int index, std::unique_ptr<tabs::TabModel> tab, int add_types, std::optional<tab_groups::TabGroupId> group)`
  chrome/browser/ui/tabs/tab_strip_model.h:286
- Activate (switch to) a tab:
  `TabStripModel::ActivateTabAt(int index, TabStripUserGestureDetails gesture_detail)`
  chrome/browser/ui/tabs/tab_strip_model.h:367
- Move a tab within a strip:
  `TabStripModel::MoveWebContentsAt(int index, int to_position, bool select_after_move)`
  chrome/browser/ui/tabs/tab_strip_model.h:380
  and the group-aware overload at chrome/browser/ui/tabs/tab_strip_model.h:386
- Detach a tab/collection for insertion elsewhere:
  `TabStripModel::DetachTabsAndCollectionsForInsertion(const std::vector<int>& tab_indices)`
  chrome/browser/ui/tabs/tab_strip_model.h (declared just above ActivateTabAt; see line ~360 region)
- Pin / unpin a tab:
  `TabStripModel::SetTabPinned(int index, bool pinned)`
  chrome/browser/ui/tabs/tab_strip_model.h:492
  Query: `TabStripModel::IsTabPinned(int index) const`
  chrome/browser/ui/tabs/tab_strip_model.h (just below SetTabPinned)
- Close a tab:
  `TabStripModel::CloseWebContentsAt(int index, uint32_t close_types)`
  chrome/browser/ui/tabs/tab_strip_model.h:326
- Create a split:
  `TabStripModel::AddToNewSplit(std::vector<int> indices, split_tabs::SplitTabVisualData visual_data, split_tabs::SplitTabCreatedSource source)`
  chrome/browser/ui/tabs/tab_strip_model.h:650 (returns split_tabs::SplitTabId)
- Restore a split (used by restore paths):
  `TabStripModel::RestoreSplit(split_tabs::SplitTabId split_id, const std::vector<int>& indices, split_tabs::SplitTabVisualData visual_data)`
  chrome/browser/ui/tabs/tab_strip_model.h (just below AddToNewSplit)
- Dissolve a split:
  `TabStripModel::RemoveSplit(split_tabs::SplitTabId split_id)`
  chrome/browser/ui/tabs/tab_strip_model.h:693
- Create a window (Browser):
  `static Browser* Browser::Create(const CreateParams& params)`
  chrome/browser/ui/browser.h:351
  with `Browser::CreateParams` at chrome/browser/ui/browser.h:182
- Observer interface the inbound bridge listens on:
  `TabStripModelObserver::OnTabStripModelChanged(TabStripModel*, const TabStripModelChange&, const TabStripSelectionChange&)`
  chrome/browser/ui/tabs/tab_strip_model_observer.h:515
  Change discriminator: `TabStripModelChange::Type { kSelectionOnly, kInserted, kRemoved, kMoved, kReplaced }`
  chrome/browser/ui/tabs/tab_strip_model_observer.h:57
  Group changes: `TabStripModelObserver::OnTabGroupChanged(const TabGroupChange&)`
  chrome/browser/ui/tabs/tab_strip_model_observer.h:561
- Closed-tab / closed-window history and restore:
  `TabRestoreService::CreateHistoricalTab(LiveTab* live_tab, ...)`
  components/sessions/core/tab_restore_service.h:65
  `TabRestoreService::RestoreEntryById(LiveTabContext* context, SessionID id, WindowOpenDisposition disposition)`
  components/sessions/core/tab_restore_service.h:120
  `TabRestoreService::RestoreMostRecentEntry(LiveTabContext* context)`
  components/sessions/core/tab_restore_service.h:105
  `TabRestoreService::RemoveEntryById(SessionID id)`
  components/sessions/core/tab_restore_service.h (just below RestoreMostRecentEntry)

Concepts with NO confirmed native Chromium API at this revision (these are
Seoul-domain concepts, not Chromium concepts):

- Workspace: no native equivalent. A workspace is a Seoul Organization Engine
  construct that maps onto one or more Browser windows and/or tab groups. There
  is no `Workspace` class in chrome/browser/ui. Outbound workspace operations
  are composed from Browser::Create plus tab insert/move/activate calls.
- Temporary vs retained tab: no native distinction. Seoul-domain only.
- Archive / restore-from-archive: there is NO native "archive" feature in
  chrome/browser/ui/tabs at this revision (confirmed: grep for archive /
  declutter / ArchivedTab returned nothing in that directory). Archive is a
  Seoul concept. The closest native primitive for the "close but recover later"
  shape is TabRestoreService (closed-tab history), cited above. Whether Seoul
  archive is backed by TabRestoreService or by a Seoul-owned store is an open
  decision: see the archive command, marked RESEARCH REQUIRED for the chosen
  backing store.
- Preview / promote-preview: Chromium has no native "preview tab" class. The
  selected mechanism is a Seoul-owned, non-strip `WebContents` embedded
  non-owningly in `views::WebView`; `WebView::SetWebContents` explicitly does
  not take ownership. Promotion detaches the view and moves the same
  `unique_ptr<WebContents>` through `chrome::AddWebContents`, then uses
  `TabStripModel::AddToNewSplit` for split promotion. This mechanism is source-
  confirmed at the pinned revision but not yet connected, compiled, or run.

---

## 3. Original command definitions

Every command below specifies the same eight fields:
preconditions; Chromium API candidate; organization mutation timing; rollback;
observer feedback handling; failure behavior; accessibility implications;
deferred compile/runtime requirement.

Convention used throughout for observer feedback handling: "register
expectation before the Chromium call; reconcile-not-apply on the matching
event" is the loop-avoidance rule from section 1.3.

### 3.1 Switch workspace

- Preconditions: target workspace exists in the Organization Engine and is not
  already the active workspace; at least one window/tab is bound to it; no
  blocking modal (for example a beforeunload dialog) is outstanding on the
  current foreground tab.
- Chromium API candidate: activation of the workspace's designated tab via
  `TabStripModel::ActivateTabAt` (chrome/browser/ui/tabs/tab_strip_model.h:367)
  and, if the workspace maps to a different Browser window, bringing that
  window forward. Window-focus API: RESEARCH REQUIRED (Browser window
  activation entry point not confirmed in this pass).
- Organization mutation timing: AFTER the Chromium event. Seoul marks the
  workspace active only once the activation observer event is reconciled, so
  authoritative "active workspace" never leads the real UI.
- Rollback: if activation fails, the previously active workspace remains active;
  no organization state changed because the mutation was deferred to AFTER the
  event. Nothing to undo.
- Observer feedback handling: register an activation expectation keyed by the
  target tab handle. The resulting kSelectionOnly change
  (tab_strip_model_observer.h:57) is reconciled, not applied as a fresh user
  switch. Without the expectation the bridge would treat the activation as a
  user-initiated focus change and could re-emit a redundant switch.
- Failure behavior: if the target tab/window is gone, fail the command with a
  "stale workspace target" error; trigger inbound resync so the Organization
  Engine drops the dangling binding. Never auto-create a replacement.
- Accessibility implications: a workspace switch is a large context change.
  Focus must land on a deterministic element (the activated tab's web contents
  root) and a screen-reader announcement of the new workspace name should be
  emitted. Do not leave focus on a now-hidden element.
- Deferred compile/runtime requirement: requires the window-focus API to be
  identified (RESEARCH REQUIRED) before this can compile against real Chromium.

### 3.2 Create workspace and project into a window

- Preconditions: workspace definition is valid (unique id, name); a target
  Profile is available; the Seoul-to-window mapping policy is decided (one
  window per workspace vs grouping).
- Chromium API candidate: `Browser::Create(const CreateParams&)`
  (chrome/browser/ui/browser.h:351; CreateParams at browser.h:182), then seed
  the new window's strip via `TabStripModel::InsertWebContentsAt`
  (tab_strip_model.h:278) for each projected tab.
- Organization mutation timing: SPLIT. The workspace record is created in the
  Organization Engine BEFORE the Chromium call (so the command layer has an id
  to tag the expectations with), but the binding workspace -> window handle is
  finalized AFTER the window's first observer events are reconciled.
- Rollback: if Browser::Create succeeds but tab seeding fails partway, close the
  partially built window (CloseWebContentsAt per tab, then window teardown) and
  mark the workspace record as failed/unbound. If Browser::Create itself fails,
  delete the pre-created workspace record.
- Observer feedback handling: register insert expectations for each seeded tab
  keyed by tab handle before each InsertWebContentsAt. Each kInserted change
  (tab_strip_model_observer.h:57) is reconciled into the existing workspace
  record, NOT applied as a brand-new user-opened tab.
- Failure behavior: surface a creation error; leave no orphan window. Partial
  windows are always torn down, never handed to the user half-built.
- Accessibility implications: a newly created window takes focus; announce the
  workspace name and the count of projected tabs. Ensure the new window is
  reachable in the window cycle order.
- Deferred compile/runtime requirement: requires a Profile handle plumbed to
  the command layer and a decided window-mapping policy; until both exist this
  command is design-only.

### 3.3 Open temporary tab

- Preconditions: a target workspace/window is active and accepting tabs; the URL
  or about:blank target is known; tab is flagged temporary in Seoul intent.
- Chromium API candidate: `TabStripModel::InsertWebContentsAt`
  (tab_strip_model.h:278) with appropriate add_types. Activation, if desired,
  via `ActivateTabAt` (tab_strip_model.h:367).
- Organization mutation timing: AFTER the Chromium event. The temporary tab is
  recorded with its temporary flag only once the kInserted event is reconciled,
  so the engine never tracks a tab that the strip rejected.
- Rollback: if insertion fails, no organization record was created (deferred to
  AFTER), so there is nothing to roll back. If a follow-on activate fails, the
  tab remains inserted but un-activated; report partial success.
- Observer feedback handling: register an insert expectation keyed by tab handle
  carrying the temporary flag. On the kInserted event, reconcile and stamp the
  tab as temporary instead of applying it as an ordinary user-opened
  (implicitly retained) tab. This is the key place where misclassification would
  silently promote a temporary tab to retained.
- Failure behavior: insertion failure returns an error; the strip is unchanged.
  No retry storm; one attempt, then surface failure.
- Accessibility implications: announce the new tab and whether it is temporary,
  because temporary tabs may auto-dismiss; a user relying on a screen reader
  needs to know the tab is ephemeral.
- Deferred compile/runtime requirement: the "temporary" lifecycle (auto-dismiss
  policy) is Seoul-side and must be defined before this command is meaningful.

### 3.4 Retain tab (promote a temporary tab to retained)

- Preconditions: the tab exists and is currently flagged temporary in the
  Organization Engine; the tab handle is still live.
- Chromium API candidate: NONE required at the Chromium layer in the common
  case. Retention is a Seoul-side state transition; the tab already exists in
  the strip. If retention also implies pinning or moving, those use SetTabPinned
  (tab_strip_model.h:492) or MoveWebContentsAt (tab_strip_model.h:380), but plain
  retention needs no Chromium mutation.
- Organization mutation timing: BEFORE any optional Chromium follow-up, and
  immediately, because the core transition is internal to Seoul. If retention is
  pure state, there is no Chromium event at all.
- Rollback: revert the flag from retained back to temporary; trivial because the
  state is Seoul-owned and no Chromium mutation occurred.
- Observer feedback handling: in the pure case there is NO Chromium event, so
  there is nothing for the inbound bridge to classify. If retention triggered an
  optional pin/move, those follow their own commands' expectation rules. Take
  care not to expect an event when none will be emitted.
- Failure behavior: if the tab handle is stale, fail with "tab gone"; do not
  recreate the tab. If an optional pin/move fails, the retain flag still stands;
  report the secondary failure separately.
- Accessibility implications: announce that the tab is now retained (no longer
  ephemeral), so assistive-tech users know it will persist.
- Deferred compile/runtime requirement: depends on the temporary/retained model
  from 3.3 being defined.

### 3.5 Pin and unpin

- Preconditions: the tab exists and its handle is live; for pin, the tab is
  currently unpinned; for unpin, currently pinned (query with IsTabPinned).
- Chromium API candidate: `TabStripModel::SetTabPinned(int index, bool pinned)`
  (tab_strip_model.h:492). Note the documented side effect: SetTabPinned returns
  the new index because pinning reorders the tab to maintain the
  pinned-before-unpinned invariant.
- Organization mutation timing: AFTER the Chromium event. Because SetTabPinned
  also MOVES the tab, the engine must learn the new index from the observer
  event, not predict it. Apply the pinned state and the new position together on
  reconciliation.
- Rollback: call SetTabPinned with the inverse value; expect another move. Only
  attempt rollback if the original command's observer event never arrives within
  the reconciliation window and a resync confirms an inconsistent state.
- Observer feedback handling: register an expectation that anticipates BOTH a
  pinned-state change AND a kMoved change (tab_strip_model_observer.h:57) for the
  same tab handle, since SetTabPinned can emit a move. The bridge must reconcile
  the move as command-originated and NOT treat the reorder as a user drag.
  Failing to expect the move is the classic double-apply trap here.
- Failure behavior: if the tab is gone, fail and resync. If the state is already
  the requested state (idempotent no-op), treat as success without calling
  Chromium.
- Accessibility implications: pinned state changes the tab's affordances and
  reading order; announce pin/unpin and the resulting position so the user's
  mental model of tab order stays correct.
- Deferred compile/runtime requirement: must reconcile pin-induced reorder; the
  inbound bridge's index reconciliation must be index-stable (handle-keyed)
  before this is safe.

### 3.6 Move tab to workspace

- Preconditions: source tab exists and is live; destination workspace exists and
  maps to a concrete window/strip; cross-window move policy is decided.
- Chromium API candidate: within one strip,
  `TabStripModel::MoveWebContentsAt(int index, int to_position, bool select_after_move)`
  (tab_strip_model.h:380). Across windows (the common workspace case), detach
  then re-insert: `TabStripModel::DetachTabsAndCollectionsForInsertion`
  (tab_strip_model.h, ~line 360 region) followed by
  `TabStripModel::InsertDetachedTabAt` (tab_strip_model.h:286) on the
  destination strip.
- Organization mutation timing: AFTER the Chromium events. A cross-window move
  emits a remove on the source strip and an insert on the destination strip; the
  workspace rebinding is applied only after BOTH are reconciled, to avoid a
  window where the tab belongs to neither or both workspaces.
- Rollback: if the destination insert fails after a successful source detach,
  re-insert the detached tab into the source strip at its prior index (best
  effort) and keep the original workspace binding. The detached tab object must
  not be dropped, or the tab is destroyed.
- Observer feedback handling: register a paired expectation: a kRemoved on the
  source strip and a kInserted on the destination strip, correlated by the same
  tab handle. The inbound bridge MUST treat the source kRemoved as a move, not a
  close (do not archive or push to TabRestoreService), and MUST treat the
  destination kInserted as the same tab re-homing, not a new tab.
- Failure behavior: a detach with no successful insert is the dangerous case;
  the rollback above is mandatory. Surface the failure and resync both strips.
- Accessibility implications: the tab leaves one window and appears in another;
  announce departure and arrival, and ensure focus is handled deliberately
  (typically focus stays on the source window unless the move was a switch).
- Deferred compile/runtime requirement: requires confirmed detach API
  signature (the DetachTabsAndCollectionsForInsertion exact signature should be
  re-confirmed before implementation) and a decided cross-window policy.

### 3.7 Close tab

- Preconditions: tab exists and is live; the close is intentional (not a
  Seoul-side archive, which is a different command, see 3.8).
- Chromium API candidate: `TabStripModel::CloseWebContentsAt(int index, uint32_t close_types)`
  (tab_strip_model.h:326). Note the in-code TODO at tab_strip_model.h:321
  regarding call sites; the method is the current entry point.
- Organization mutation timing: AFTER the Chromium event. The tab record is
  removed from the engine only when the kRemoved event is reconciled, because a
  close can be blocked (beforeunload) and may not actually happen.
- Rollback: a confirmed close is not generally reversible via this command; if
  recovery is desired the user/engine uses restore (3.10) backed by
  TabRestoreService. Do not attempt to re-create the WebContents inline.
- Observer feedback handling: register a remove expectation keyed by the tab
  handle with reason "intentional close". On the kRemoved change
  (tab_strip_model_observer.h:57) the bridge reconciles and removes the record;
  it MUST distinguish this from a move-induced remove (3.6) and from an archive
  (3.8) by the registered reason, so it does not, for example, archive a tab the
  user meant to truly close.
- Failure behavior: if the close is blocked (for example beforeunload), no
  kRemoved arrives; the expectation times out and is cleared; the engine keeps
  the tab. Report "close blocked".
- Accessibility implications: announce the close and move focus deterministically
  to the neighbor tab that Chromium activates; do not leave focus on a destroyed
  contents.
- Deferred compile/runtime requirement: the close_types flag set Seoul should
  pass must be decided; behavior under beforeunload must be specced.

### 3.8 Archive tab

- Preconditions: tab exists and is live; archive is a Seoul concept (no native
  archive feature exists at this revision).
- Chromium API candidate: RESEARCH REQUIRED for the backing store. There is no
  native archive in chrome/browser/ui/tabs. Two candidate shapes:
  (a) close the tab via CloseWebContentsAt (tab_strip_model.h:326) and rely on
  TabRestoreService::CreateHistoricalTab (components/sessions/core/
  tab_restore_service.h:65) plus a Seoul-side archive index keyed by SessionID;
  or (b) a fully Seoul-owned archive store that snapshots tab state before
  closing. The choice between (a) and (b) is RESEARCH REQUIRED.
- Organization mutation timing: BEFORE the Chromium close, capture the archive
  snapshot (URL, title, workspace, position, pin state, split membership) into
  the archive store; then issue the close; then AFTER the kRemoved event,
  finalize the tab's transition from live to archived. Snapshot-before-close is
  required because once the WebContents is gone the live state is unrecoverable.
- Rollback: if the close fails after snapshotting, discard the just-written
  archive snapshot so it does not shadow a still-live tab.
- Observer feedback handling: register a remove expectation with reason
  "archive". The inbound bridge MUST NOT treat the kRemoved as an ordinary close
  (3.7) nor as a move (3.6); it reconciles by flipping the tab record to
  archived rather than deleting it.
- Failure behavior: blocked close leaves the tab live; discard the snapshot and
  report. Never leave a tab marked archived while it is still open.
- Accessibility implications: announce that the tab was archived (recoverable),
  distinct from closed (gone), and move focus to a neighbor.
- Deferred compile/runtime requirement: the backing store decision (RESEARCH
  REQUIRED above) blocks implementation; the snapshot schema must be defined.

### 3.9 (reserved) Archive interplay note

Archive (3.8) and Restore archived tab (3.10) are a pair and share the archive
store schema. They must be implemented together so a snapshot written by 3.8 is
exactly what 3.10 consumes.

### 3.10 Restore archived tab

- Preconditions: an archive entry exists and is well-formed; a destination
  workspace/window is resolvable (the entry's original workspace if it still
  exists, otherwise a fallback target).
- Chromium API candidate: if archive is backed by TabRestoreService (3.8 option
  a), use `TabRestoreService::RestoreEntryById(LiveTabContext*, SessionID, WindowOpenDisposition)`
  (components/sessions/core/tab_restore_service.h:120) or
  `RestoreMostRecentEntry` (tab_restore_service.h:105). If archive is a
  Seoul-owned store (3.8 option b), recreate the tab with
  `TabStripModel::InsertWebContentsAt` (tab_strip_model.h:278) from the snapshot.
  Exact path is contingent on the 3.8 decision: RESEARCH REQUIRED until decided.
- Organization mutation timing: AFTER the Chromium event. The tab record flips
  from archived back to live only when the restoring kInserted event is
  reconciled, so the engine never shows a tab as live before it exists.
- Rollback: if restore fails, leave the archive entry intact (do not consume it)
  so the user can retry. Only remove the archive entry AFTER a confirmed
  successful restore.
- Observer feedback handling: register an insert expectation keyed by the new
  tab handle with reason "restore". On the kInserted event, reconcile by
  reviving the existing archived record (preserving its history) rather than
  applying it as a brand-new user-opened tab.
- Failure behavior: if the destination is gone, retarget to the fallback window
  or report "no destination". If RestoreEntryById finds no matching SessionID,
  report "archive entry stale" and mark the entry invalid.
- Accessibility implications: announce the restored tab and where it landed,
  especially if it landed in a fallback window rather than its original
  workspace.
- Deferred compile/runtime requirement: blocked on the same 3.8 backing-store
  decision; restore and archive ship together.

### 3.11 Create split

- Preconditions: at least two target tabs exist in the same strip and are live;
  none of the target tabs is already in a conflicting split; indices are known
  and will be passed sorted ascending (the API requires sorted indices).
- Chromium API candidate: `TabStripModel::AddToNewSplit(std::vector<int> indices, split_tabs::SplitTabVisualData visual_data, split_tabs::SplitTabCreatedSource source)`
  (tab_strip_model.h:650), which returns a `split_tabs::SplitTabId`.
- Organization mutation timing: AFTER the Chromium event. The Seoul split record
  is created keyed by the returned SplitTabId once the corresponding observer
  signal is reconciled. Note AddToNewSplit reorders tabs to be contiguous, so
  the engine must also absorb the resulting moves.
- Rollback: if split creation fails, the strip is unchanged and no Seoul split
  record exists; nothing to undo. If a follow-on Seoul annotation fails after
  the split was created, dissolve via RemoveSplit (3.12) to return to a clean
  state.
- Observer feedback handling: register expectations for the split creation AND
  for any kMoved changes caused by the contiguity reorder, all keyed by the
  involved tab handles. The inbound bridge MUST treat those moves as
  command-originated (split layout), not as user drags.
- Failure behavior: invalid or unsorted indices, or a tab disappearing mid-call,
  fails the command; resync the strip. Do not partially split.
- Accessibility implications: a split changes spatial layout significantly;
  announce that a split was created and how many panes; ensure each pane's
  contents is individually reachable and labeled.
- Deferred compile/runtime requirement: the exact observer signal emitted for
  split creation (whether it surfaces via OnTabStripModelChanged moves plus a
  dedicated split notification) should be confirmed before implementation:
  RESEARCH REQUIRED for the precise split-change observer callback.

### 3.12 Dissolve split

- Preconditions: a split with a known SplitTabId exists in the Organization
  Engine and is still present in the strip.
- Chromium API candidate: `TabStripModel::RemoveSplit(split_tabs::SplitTabId split_id)`
  (tab_strip_model.h:693). Per the header, the tabs keep their group and pin
  properties after the split is removed.
- Organization mutation timing: AFTER the Chromium event. The Seoul split record
  is deleted only when the dissolution is reconciled, so the engine never shows
  a split as gone while it still renders.
- Rollback: dissolution is effectively the inverse of create; to undo, re-create
  via AddToNewSplit / RestoreSplit (tab_strip_model.h:650 / RestoreSplit just
  below it). Only do so on confirmed inconsistency.
- Observer feedback handling: register a dissolution expectation keyed by the
  SplitTabId and member tab handles. The inbound bridge reconciles by removing
  the Seoul split record; it MUST NOT interpret any layout change as user-driven.
- Failure behavior: if the SplitTabId is unknown to Chromium (already gone),
  treat as idempotent success and clean up the stale Seoul record.
- Accessibility implications: announce that the split was dissolved and that the
  panes are now independent tabs; restore a sensible single-focus target.
- Deferred compile/runtime requirement: same split-observer-callback RESEARCH
  REQUIRED as 3.11; create and dissolve share that reconciliation path.

### 3.13 Open preview

- Preconditions: a source intent to preview a URL/target without fully
  committing it as a normal tab; preview policy (lifetime, visibility) defined.
- Chromium API candidate: the Preview host owns a non-strip
  `content::WebContents`, displays it via non-owning
  `views::WebView::SetWebContents`, and keeps it out of `TabStripModel` until an
  explicit promotion. This preserves the source tab and prevents inbound tab
  observation from misclassifying a Preview as retained.
- Organization mutation timing: AFTER the Chromium event (if any). A preview is
  recorded as a preview-typed entry only once its creation is reconciled. If
  preview is a non-strip surface, timing is Seoul-internal.
- Rollback: previews are by design disposable; closing/discarding the preview is
  the rollback. Discard must not be mistaken for an archive or a true close.
- Observer feedback handling: if the preview is a real strip tab, register an
  insert expectation tagged "preview" so the inbound bridge does NOT classify it
  as a normal user-opened tab (which would make it retained). If the preview is
  a non-strip surface, no OnTabStripModelChanged event is expected; do not
  register a strip expectation.
- Failure behavior: preview creation failure is non-fatal; report and leave the
  source context unchanged. Previews never block the user.
- Accessibility implications: a preview must be clearly announced as a preview
  (transient, not yet committed) and must not steal focus from the user's
  current tab unless the user explicitly drove the preview.
- Deferred compile/runtime requirement: the mechanism is chosen; the host,
  policy delegate, command entry point, and browser tests remain to be authored,
  compiled, and run.

### 3.14 Promote preview

- Preconditions: a live preview entry exists; the user/engine intends to commit
  it to a normal (retained or temporary) tab in a target workspace.
- Chromium API candidate: detach the `WebView`, move its separately owned
  `WebContents` through `chrome::AddWebContents` as a foreground tab, then for
  split promotion call `TabStripModel::AddToNewSplit` with the parent and newly
  inserted tab indices. Commit the Preview lifecycle only after both operations
  succeed; otherwise abort promotion back to ready.
- Organization mutation timing: AFTER the Chromium event. The preview entry
  flips to a normal tab entry only when the promotion (activation or insertion)
  is reconciled, so the engine never shows a committed tab that does not exist.
- Rollback: if promotion fails, leave the preview as a preview (do not destroy
  it); the user can retry. Do not half-promote.
- Observer feedback handling: `LifecycleCoordinator::ExpectTabInsertion`
  records one exact window+tab retained role before insertion. A matching event
  creates that retained membership; a mismatched window consumes the expectation
  and uses the safe temporary default. Expectations are bounded, cancellable,
  and cleared on reconciliation, queue overflow, shutdown, or window removal.
- Failure behavior: if the preview is gone, fail with "preview expired". If the
  destination workspace is gone, retarget or report.
- Accessibility implications: announce that the preview is now a committed tab
  and, if it was activated, that focus moved into it.
- Deferred compile/runtime requirement: the mechanism and lifecycle handshake
  are chosen; the bubble/transfer service and browser tests remain. Preview and
  promote still ship together.

---

## 4. Requirements that were deferred when this record was written

- Expectation/correlation store: a handle-keyed outstanding-command registry
  shared between the command layer and the inbound bridge. Required by every
  command. Index-keyed correlation is forbidden because indices shift on pin,
  split, and move.
- Reconciliation timeout: every expectation needs a timeout after which it is
  cleared and a resync is triggered, so a blocked or dropped event cannot leak a
  permanently pending command.
- Resync path: a full read of the real Chromium model into the Organization
  Engine, used whenever an expectation times out or a stale handle is detected.
- Window-focus API: not confirmed in this pass; required by 3.1 and 3.2.
  RESEARCH REQUIRED.
- Split-change observer callback: the precise observer notification for split
  create/dissolve must be confirmed; required by 3.11 and 3.12. RESEARCH
  REQUIRED.
- Archive backing store: native TabRestoreService vs Seoul-owned store; required
  by 3.8 and 3.10. RESEARCH REQUIRED.
- Preview mechanism: source-confirmed; Chromium host/policy/runtime tests remain.
- Profile plumbing: a Profile handle must reach the command layer for 3.2.

End of historical contract. The pre-implementation notes above are retained to
explain the design constraints; current implementation and runtime evidence are
tracked in `docs/release/seoul-product-readiness.md`.
