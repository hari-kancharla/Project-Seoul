# Seoul browser foundation

This pass covers everyday browsing: tabs, splits, grouping, containers, Settings,
Downloads, and the shape of browser controls. It is not a release certification.

**Design review update, September 8:** The primary Settings presentation and
command/creation controls were rejected in visual review. Their direction below
is superseded by [Settings research and replacement direction](seoul-settings-research-and-direction-2026-09-08.md).
Existing behavioral test results do not constitute acceptance of that design.

## Research and design decisions

- [Arc profiles](https://resources.arc.net/hc/en-us/articles/19227964556183-Profiles-Separate-Work-Personal-Browsing)
  distinguish account data from the Spaces that organize tabs. Seoul must make
  its isolation boundary equally explicit; moving a label cannot change a live
  document's account session.
- [Zen workspaces and containers](https://docs.zen-browser.app/user-manual/workspaces)
  expose separate cookie sessions and explain that history and extensions remain
  shared. Seoul's StoragePartition containers have the same important distinction
  from full browser profiles. They must preserve their partition through restore.
- [Arc splits](https://resources.arc.net/hc/en-us/articles/19335393146775-Split-View-View-Multiple-Tabs-at-Once)
  and [Zen splits](https://docs.zen-browser.app/user-manual/split-view) keep split
  operations attached to the affected tabs. Seoul should preserve tab identity,
  focus, and groups when creating, resizing, separating, and closing panes.
- [Arc Library](https://resources.arc.net/hc/en-us/articles/19230634389911-Library-A-home-for-your-downloads-archived-tabs-easels-and-more)
  makes recent files accessible from the sidebar. Seoul Downloads should clearly
  distinguish active transfers, finished files, and failed transfers, with progress,
  cancel/retry, reveal, search, and removal available in context.
- [Zen preferences source](https://github.com/zen-browser/desktop/blob/dev/src/browser/components/preferences/zenLooksAndFeel.inc.xhtml)
  separates browser appearance and tab behavior into meaningful groups. Seoul
  Settings should use a compact category rail, readable preference groups,
  search, and stable back navigation. Existing preference and download security
  handlers should remain authoritative behind the new presentation.

These are reference interaction patterns, not evidence that Seoul matches either
browser. Zen's manual also warns that parts may lag its implementation.

Shape direction requested by the owner: sky-blue accents, neutral surfaces,
rounded rectangles for address/search fields, small-radius rectangular buttons,
square icon tiles. No oval address field in any layout. Keyboard focus, reduced
motion, dark mode, text zoom, and narrow windows are part of verification.

## Findings at the start of this pass

1. Space isolation exists in the new-tab path but is not exposed by the shell's
   create menu. Browser restore currently creates the default SiteInstance.
2. Moving a membership to another Space does not recreate its WebContents, so
   crossing an isolation boundary would mislabel an unchanged session.
3. Isolation can currently be toggled on a populated Space without recreating its
   tabs. The UI must not present such a change as applied isolation.
4. Normal location bars still request Chromium's maximum corner radius. The
   floating command surface has a separate fixed radius, explaining the mismatch.
5. Settings and Downloads buttons route directly to the stock Chromium pages.
6. The three previously pending Boost settings browser tests now pass (5 seconds,
   no retries), including execution, panel notifications, and the shipping WebUI.

The implementation and verification record below distinguish completed checks
from remaining release work.

## Changes

- The location-bar drawing path now caps corners at 10 px in every layout. The
  current Space tile uses a 7 px corner, and internal search fields use 8 px.
- Settings starts with Appearance and exposes the actual sidebar/top-toolbar/
  compact-sidebar preferences with diagrams, plus sidebar address display and
  download animation. Its category rail and content cards use Seoul surfaces.
- Paused downloads retain a readable filename instead of the crossed-out style
  used for cancelled items.
- Downloads has a Seoul heading, a separate search/action row, rectangular file
  cards, dark colors, and a compact empty state. Chromium still owns transfer
  progress, warnings, cancellation, removal, and undo.
- New Container Space is available from the footer + Create New menu and the
  shell command launcher. Creation
  explains the shared history/extensions boundary; its Space indicator identifies
  it as a container. A live tab cannot be relabelled across a container boundary.
- Restore uses the saved partition and its session-storage namespace. New code
  also preserves the isolation boundary after tabs close and can recover a deleted
  container's identity when reopening a tab. The browser tests exercise actual
  cookies, local storage, and session-storage drafts across that restore.
- Name dialogs now validate before accepting instead of silently trimming a long
  name or closing on an empty name.

- Container New Tab creation now resolves virtual browser URLs before selecting
  the isolated SiteInstance, matching Chromium's ordinary new-tab path.
- A container with open tab memberships cannot be deleted and silently relabel
  those renderers as shared-session tabs.
- macOS keyboard redispatch now rejects synchronous re-entry into the same
  window. Two initial test crashes had native event recursion in their crash
  stacks. A bounded reproduction failed against the previous code, then passed
  for key-down, key-up, and modifier events after the guard; subsequent keys
  still dispatch.
- Native visual review caught New Tab underneath macOS window controls in Top
  toolbar mode. The sidebar now reserves the toolbar overlap height. Integrated
  and already-offset compact layouts do not receive an extra inset.
- Settings card widths adapt to narrow pages; the layout diagrams align beside
  their labels; selected navigation icons remain readable in dark mode. Search
  field and toolbar transitions respect reduced-motion preferences.

## Verification record

Repository CI passed, including 91 JavaScript tests. The final CSS-only download
adjustment was then rebuilt and checked with the transfer probe, smoke test, and
final patch-manifest, overlap, and round-trip verification.

The completed full native run passed **864 cases in all 34 unit-test binaries**.
The browser launcher selected 218 cases and finished in 374 seconds with retries
disabled: **217 passed and one opt-in screenshot fixture was skipped** because
`SEOUL_CAPTURE_DIR` was not set. The separate process-relaunch pair then passed
both cases. This is 219 browser cases passed across those runs, not a claim that
one combined 220-case suite was run.

The final production adjustments after that full run only change responsive
Settings CSS and the paused-download filename styling. The rebuilt browser passed the actual-window Settings regression
again. The Settings build completed in 13.28 seconds (21 incremental steps); the
final Downloads CSS build completed in 6.89 seconds (21 incremental steps),
followed by a passing transfer check.
Build and test runs were sequential; no source materialization or library rebuild
ran underneath browser tests.

| Area | Observed result |
| --- | --- |
| Tabs, groups, splits | Grouped real pages retained their identities and group while splitting, switching all three layouts, separating panes, and closing one pane. |
| Container separation | Same-origin cookies, local storage, and session storage stay separate from the shared Space. Closing/reopening a tab restores its original partition, including recovery of a deleted container ID. |
| Container relaunch | A PRE/post pair uses separate browser processes: the cookie, local storage, and session draft survive restart; a shared Space cannot read them. |
| Container model | Used boundaries cannot be toggled; live tabs cannot cross them by metadata reassignment; an occupied container cannot be deleted. |
| Settings | Layout preferences change the actual native window, reflect external preference changes, and preserve 10 px address-field corners. Nine narrow Settings routes and search pass. |
| Downloads | A real file survives search, removal from history, and undo. A separate controlled 4 MiB transfer passes progress, pause, resume, cancel, retry, and completion, with the correct saved size. |
| Keyboard | A bounded native redispatch reproduction fails on the old code and passes with the guard for key-down, key-up, and modifiers; later events still dispatch. |
| Visual pages | 16 settled captures cover Settings and Downloads in light/dark mode at 1280, 760, and 480 px, plus 480 px at 150% CSS scaling. No detected horizontal control overflow or page JavaScript errors. |
| Patch integrity | All 50 patches apply to the pinned base; 240 affected files match the active checkout byte for byte; reversing restores all 216 original files exactly. |

At narrow effective widths, Settings hides the decorative layout previews to
leave room for the labels. The 150% check uses CSS scaling, not operating-system
text scaling or native browser zoom.

The original Downloads regression failure was a fixture mistake: an explicitly
forced file path marks the item temporary, which Chromium deliberately excludes
from history. The corrected test uses the normal profile download directory and
checks that the item is non-temporary. The transfer probe also needed timed
polling because animation-frame polling stopped in its background tab; diagnostic
inspection showed the completed item was already present. These failed attempts
were not counted as passes.

Three launcher unit expectations needed the two new Space commands; the updated
checks verify their executable actions and searchability. The full native run
then passed.

## Visual evidence and remaining native review

Seoul Settings at desktop width (`native/evidence/2026-09-08/browser-foundation/screenshots/settings-light-1280.png`)

Seoul Downloads in dark mode (`native/evidence/2026-09-08/browser-foundation/screenshots/downloads-dark-1280.png`)

The evidence folder (`native/evidence/2026-09-08/browser-foundation/README.md`) contains
build/test logs, patch verification, source hashes, capture scripts, and the
narrow, scaled, and active-transfer screenshots.

Native app review before the caption-clearance fix exercised all three layouts
through their actual Settings controls and found the overlap that was corrected.
The post-fix geometry assertion passes. **The final native visual review and
manual Container Space creation remain pending:** the computer-use tool reported
that the Mac was locked and could not unlock it. No post-fix native screenshot
or completed manual creation flow is claimed here. The disposable verification
app was closed before the final rebuild.

## Release scope

Container Spaces isolate cookies and site storage. History, bookmarks, saved
passwords, and extensions remain profile-wide; use a separate browser profile
when those also need separation.

This is a verified browser-foundation pass on macOS, not certification that every
Seoul feature or every website is flawless. Windows/Linux interaction review,
localized strings for the new controls, assistive-technology review, long-session
memory/performance measurements, and signed distribution/update testing remain
release work. Boosts, the context graph, voice, handset handoff, and live-site ad
blocking need their own feature reviews, as requested.
