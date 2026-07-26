# Seoul Library, Boards, and Live Collections

Status: the native core, persistent Board editor, and production Lit
Library/Boards surfaces are implemented and runtime-verified. Browser-owned
capture storage, Live Collection executors, and browser-state projections
remain.

## Product contract

Seoul adopts the useful behavior of browser Libraries, visual whiteboards, and
live folders without copying another browser's layout or branding:

- **Library** is one searchable index of things the user created, saved, or is
  actively tracking. It references browser-owned downloads, archived tabs,
  workspaces, captures, surfaces, boards, workflows, and live collections. It
  never becomes a second history database or stores download bytes in prefs.
- **Boards** are spatial, editable documents for authored text, links, images,
  safe web-capture references, and Seoul adaptive surfaces. They are Seoul's
  original visual workspace rather than a visual clone of Arc Easels.
- **Live Collections** are provider-neutral, refreshable lists. Each definition
  references a registered typed capability and a bounded non-secret source
  locator. Initial adapters may cover pull-request feeds, syndication feeds,
  and schedules, but those domains never become enum cases or conditionals in
  Library core.
- **Live Calendar** is a Calendar Live Collection rendered compactly when
  attached to an Essential: next-event countdown, truthful freshness state, and
  a validated HTTP(S) join action when the provider supplies one.

The official behavior research used as acceptance input is documented by Arc's
Library, Easels, Live Calendars, and GitHub Live Folders help pages and Zen's
current Live Folder release notes. Competitor names are research references,
not product identifiers or UI requirements.

## State and security boundaries

`native/seoul/browser/library/` owns metadata only:

- Binary images, screenshots, video, and downloaded files remain in a
  browser-owned file/capture store. `LibraryArtifact.reference` is an opaque
  durable handle.
- Provider credentials remain in the connector or OS credential store.
  `LiveCollectionDefinition` stores only a typed capability id and non-secret
  source locator; the capability registry owns provider identity, permissions,
  input validation, and availability.
- Board elements reference captures and SAUI surfaces by id. They never embed
  arbitrary page HTML or executable script.
- URLs exposed as board links or live-item actions must be valid HTTP(S) URLs.
- Every user-growing collection is capped before allocation or restore.

## Board correctness

Board and element ids are UUIDs. Names, text, references, coordinates, sizes,
and element counts are bounded. Updates validate a complete proposed element
before replacing the existing value, so an invalid resize, URL, or reference
cannot partially corrupt a board. Deleting a board removes only its layout and
references; referenced browser-owned artifacts are not silently deleted.
Archived Boards reject rename and element mutations until restored. Every
accepted mutation advances a monotonic revision, and Canvas ignores late older
snapshots so delayed replies cannot roll visible state backward.

The production editor supports typed text and link creation, editing,
confirmation-gated removal, drag and keyboard movement, pointer and keyboard
resize, bounded undo/redo, archive/restore, reduced-motion styling, and
screen-reader spatial descriptions. Rapid keyboard changes are coalesced into
one history step. Independent layout saves are serialized and optimistic
layouts remain visible across slow native replies.

## Live refresh correctness

Every refresh receives a monotonically increasing generation token. Completion
is accepted only for the current token, preventing a slow older request from
overwriting a newer result. A successful refresh validates the entire proposed
item set, including unique stable keys, URL safety, bounds, and calendar time
ordering, before an atomic replacement.

Changing a collection's capability, source locator, or enabled execution state
while a refresh is active exits the refreshing state. A late callback from the
old definition is therefore rejected as stale even if provider cancellation did
not arrive in time.

Provider failure preserves the last successful items and records an explicit
error state. The UI must show both the retained data and its last-success time;
it must never present failed or old data as freshly updated. A persisted
in-flight refresh restores as idle because no network request survives a browser
restart.

## Persistence

The profile runtime persists Library state in the bounded `seoul.product.v1`
dictionary. Library mutations schedule a coalesced next-turn write through the
existing `PersistenceScheduler`; shutdown performs a final synchronous write
before setting the runtime's shutdown flag. Unknown schemas are rejected and
malformed individual records are skipped rather than guessed at.

## Remaining production slices

The following are required before this feature is runtime-complete:

1. Browser-owned capture/file storage and region/full-page capture adapter.
2. GitHub, RSS, and calendar refresh executors over the connector permission
   system, including cancellation, backoff, auth-expiry, and offline behavior.
3. Archive/download/workspace projection adapters that index existing browser
   state without duplicating it.
4. Credentialed provider acceptance, relaunch recovery for fetched collection
   items, and offline/auth-expiry/network-failure integration tests.

The capable host passed the RAM, storage, Xcode, SDK, architecture, and
checkout gates. Native compilation, unit tests, WebUI checks, isolated browser
tests, and product smoke pass without bypassing a host gate.
