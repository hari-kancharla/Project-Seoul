# Seoul Context Map

Status: specification, implementation in progress. Written against repository
SHA `9c50e1200ae483afe7ef5f3a371633797eb1d7f3` plus the uncommitted work in the
tree at the time of writing.

Context Map answers one question a browser has never been able to answer: *what
am I actually working on right now?* It draws the live browser context - Spaces,
windows, tabs, folders, splits - together with the durable things a person has
authored around it: Projects, notes, tasks, Boards. It lets them navigate back
to any of it, and lets useful temporary context graduate into something durable.

It is deliberately not Obsidian. Obsidian graphs a vault the user wrote. Seoul
graphs a browser session the user is living in, where most nodes are alive and
changing and a few are authored. That difference drives every decision below.

## 1. The two layers, and why they must not merge

**The Live Context Map** is derived, ephemeral, and authoritative for nothing.
It is a rendering of state other services own. A tab that closes leaves the map.
That is correct behaviour, not data loss.

**A Board** is durable and authored. A person explicitly pins things onto it,
arranges them, connects them and labels the connections. A Board survives every
tab in it being closed; references whose backing object is gone render as
unavailable rather than disappearing.

Conflating the two is the central design risk. The rule that keeps them apart:
moving a node in the Live Map moves nothing in the browser, and closing a tab
changes no Board.

## 2. Ownership: Context Map owns no state

Context Map is a projection. It never creates a second tab or window model, and
never persists a copy of live state. Every node is drawn from the service that
already owns it. The audit that established this is summarised here because an
implementer who guesses at these will build the very duplication this forbids.

| Domain | Owner | Profile accessor | Change signal |
|---|---|---|---|
| Live windows and tabs | `LiveWindowStateProvider` (`lifecycle/live_window_state.h`) | `SeoulRuntimeService::live_window_state_provider()` | `LiveWindowStateObserver` |
| Workspaces, folders, memberships, splits | `OrganizationModel` (`organization/organization_model.h`) | `SeoulOrganizationServiceFactory::GetForProfile` | `OrganizationModelObserver::OnOrganizationChanged` |
| Projects and their items | `ThreadService` (`product/thread_service.h`) | via `SeoulRuntimeService` | **none today - see 2.1** |
| Tasks | `TaskService` (`product/task_service.h`) | via `SeoulRuntimeService` | `TaskServiceObserver` |
| Boards, artifacts, collections | `LibraryService` (`library/library_service.h`) | via `SeoulRuntimeService` | `LibraryServiceObserver::OnLibraryChanged(revision)` |

`LiveWindowStateProvider` is reached through `SeoulOrganizationService` and dies
with it, so it is held by `base::ScopedObservation` and re-fetched, never cached
across shutdown.

### 2.1 The one missing seam

`ThreadService` has no observer interface. It has a single
`base::RepeatingClosure changed_`, and production already binds it in
`seoul_runtime_service.cc` to drive persistence. Repurposing that closure would
silently stop Projects being saved.

Context Map therefore **adds** a `ThreadServiceObserver` alongside the existing
closure rather than replacing it. The closure keeps persisting; observers are
notified after the same successful mutations. This is the only new notification
plumbing the feature needs, and it is additive.

## 3. Node identity

A node's identity must survive everything that does not change what the node
*is*. The rule: **key on the durable identifier where the product has one, and
never on anything derived from presentation.**

- A tab node is keyed on `TabMembershipId`, the persisted organization id that
  is round-tripped through Chromium's session restore. It is **not** keyed on
  `LiveTabKey`, which is a session id that a relaunch may not preserve.
- Workspaces, folders, Threads, Tasks, Boards and artifacts use their own
  durable ids.
- Windows and the session are intentionally ephemeral.

Identity is never derived from a URL, a title, tab-strip order, an array index,
or a node coordinate. Renaming a page or dragging a tab to another Space must
not produce a new node, and a relaunch-restored membership maps back to the same
logical node.

Ids cross the Mojo boundary as opaque strings. No renderer pointer, no raw
native handle.

## 4. Scopes

- **Focus** - centred on the active tab, depth 1 to 3. Never the whole profile.
- **Space** - the current Workspace with its windows, folders, tabs, splits and
  associated Project context.
- **Project** - one Context Thread at the centre with its notes, references,
  task outputs and any live objects matching them. The strongest answer to
  "what am I working on".
- **All Windows** - every eligible regular-profile window. This is not history:
  browsing history never becomes nodes.

Off-the-record profiles are excluded entirely.

## 5. Edges are evidence, never inference

Every edge in V1 is deterministic and traceable to state Seoul already holds:
`contains`, `belongs_to`, `active_in`, `grouped_in`, `split_with`,
`attached_to`, `references`, `bound_to`, `produced_by`, `user_link`.

No edge is created because two titles look similar. No model proposes edges. The
protocol reserves a `suggested` provenance for a future local-only engine, but
V1 emits only `system` and `user`. Seoul does not currently record explicit
tab-opening lineage, so `opened_from` is **not** inferred; it waits for real
evidence rather than a plausible guess.

## 6. The graph is not model context

Non-negotiable. Drawing a tab must never send that tab to a model, and opening
the map must never attach anything to a Project. Visualisation and AI context
are separate concepts that happen to show the same objects.

"Use as context" is an explicit user action that routes through the existing
typed Context Thread attachment path and inherits every sensitive-content and
cloud-scope restriction already enforced there. No model receives the graph.

## 7. Data minimisation

Nodes carry a bounded title, an optional bounded subtitle, a kind, a state and
an opaque id. They never carry cookies, credentials, tokens, form values, page
HTML, accessibility trees, audio, history, or unbounded page text. Query strings
and fragments are not added to make cards look richer.

Note bodies stay inside the existing Context Thread bounds
(`kMaxContextNoteLength`), which the graph does not widen.

## 8. Bounds, and telling the truth about them

Bounds are chosen against the limits the owning services already enforce
(`kMaxWorkspaces = 100`, `kMaxContextItems = 200`, `kMaxBoardElements = 500`,
`kMaxTasksInDeck = 500`).

When a source graph exceeds the render bound, nodes are kept in priority order:
the scope root, then live and active items, then direct relationships, then
user-authored Project context, then the rest. The document then carries explicit
`truncated` metadata with the true totals. The map never silently drops nodes
while presenting itself as complete.

## 9. Board edges and migration

Boards gain first-class persistent edges: a stable id, source and target
element, optional endpoint sides, direction, a bounded label and an optional
theme colour token, under an explicit maximum count. Validation is atomic: both
endpoints must exist, labels are bounded, and dangling edges are impossible by
construction.

The Board schema version moves from 1 to 2. Existing stored Boards migrate by
gaining an empty edge list; a Board written by the older schema must load
unchanged in every other respect, and a corrupt edge list is rejected rather
than partially applied. Deleting an element removes its dependent edges, and
that contract is tested rather than assumed.

## 10. Verification

The graph builder is a pure function over snapshots and is unit tested without a
browser: stable ids, deterministic ordering, no duplicate nodes or edges,
correct containment and split relationships, stale references, truncation, and
the invariant that no sensitive field can reach a node.

Browser tests drive the real product: two windows, several Workspaces, folders,
active-tab change, tab close, tab move between Spaces, rename, splits, a Project
with a note and a tab reference, a live task, a stale durable reference,
activating a tab from a node, and rejection of an action carrying a stale
revision. Canvas tests cover the view itself, including keyboard navigation and
an older snapshot arriving after a newer one.

This document describes intended behaviour. Sections are moved into the
readiness report only as they are built and actually exercised.
