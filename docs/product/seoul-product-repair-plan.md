# Seoul repair results and release gates

Started September 7, 2026 from `687c18d`.
This records the first repair pass, which the user rejected visually.
The [September 7 visual and blocking revision](seoul-visual-and-blocking-revision.md)
supersedes its Boost, palette, graph, ad-blocking and verification sections.
This is not a declaration of release readiness.

## Product direction

The website is the primary workspace. Seoul assistance is optional, compact,
contextual and dismissible. Site appearance and phone presentation belong in
site controls. Saved work and settings have their own destinations. Preserve
the existing native browser, permission boundary and reversible patch series.
Avoid adding features whose visible controls cannot complete a useful journey.

## Research informing the changes

- [Dia](https://www.diabrowser.com/getting-started): help is invoked beside the
  current page. Use a clear entry point and selected context.
- [Arc Boosts](https://resources.arc.net/hc/en-us/articles/26681118599191-How-do-you-disable-Boosts): site customization belongs with the site's controls.
- [Zen Compact Mode](https://docs.zen-browser.app/user-manual/compact-mode):
  browser controls can yield space while staying recoverable by pointer and keyboard.
- [HeyClicky at YC](https://www.ycombinator.com/companies/heyclicky): immediate
  voice access while work stays visible. This is a product pattern, not proof
  that Seoul matches its abilities.
- [Apple sidebars](https://developer.apple.com/design/human-interface-guidelines/sidebars):
  keep navigation shallow, concise, hideable and appropriate to available space.
- [W3C motion guidance](https://www.w3.org/WAI/WCAG22/Understanding/animation-from-interactions.html):
  nonessential interaction animation must respect reduced motion.
- [Obsidian graph](https://obsidian.md/help/plugins/graph): graph navigation
  should expose actual connections and support focus, rather than decorative nodes.
- [VS Code layout](https://code.visualstudio.com/docs/configure/custom-layout):
  supporting tools occupy a separate, hideable region. A terminal requires a
  real process/session and a defined workspace trust boundary.
- [Cloudflare challenges](https://developers.cloudflare.com/cloudflare-challenges/troubleshooting/challenge-solve-issues/):
  blocking or modifying challenge dependencies can break verification. Reduce
  compatibility problems; do not promise that the browser can remove site requirements.
- [Claude API](https://platform.claude.com/docs/en/api/overview) and
  [OpenAI Realtime WebRTC](https://developers.openai.com/api/docs/guides/realtime-webrtc):
  use actual endpoints, version headers, scoped credentials and current handshake contracts.

## Implemented repairs

- Provider configuration now uses a valid Claude endpoint/version and a normalized
  local API base. Health checks require the selected model, reject malformed or
  oversized responses, and cannot let a stale request overwrite a newer setting.
  Generation handles concurrent requests, cancellation, late callbacks, streaming
  errors, truncated responses and malformed plans. Local-first routing is explicit.
  Connection forms identify which provider receives page context or microphone audio.
- Realtime voice uses the current WebRTC call endpoint. Connection/reconnection
  timeouts, session generation guards, aborts and microphone cleanup prevent old
  sessions from taking over a new one. Closing/hiding the assistant stops microphone
  capture. Provider setup errors are shown without exposing raw transport output.
- The assistant is now an optional panel beside the page, with one visible sidebar
  button, one command-palette entry, a working close button, shallow tools navigation,
  a reachable composer and consistent Seoul identity. The original native opening
  route had never signalled readiness; exposing it revealed a separate toolbar
  pinning crash. Both defects are repaired and covered by a real-panel browser test.
- Boosts open the existing native site editor without also redirecting the assistant.
  Site controls use neutral action fills, clearer selected outlines and larger click
  targets. The Boost panel is wide enough for its action labels. The actual Serif
  selection was checked against both the saved state and the visible webpage.
- The phone picker shows four common devices, preserves a current nonfeatured choice,
  and groups the remaining catalogue into menus of at most ten devices. Rotation
  preserves custom dimensions. The menu holds a weak reference to its tab.
- Task history retains up to 100 recent snapshots within a 2 MiB persistence budget.
  Completed receipts survive relaunch; interrupted tasks are labelled and never
  replayed automatically. A fresh request does not inherit a persisted approval.
  Planning can be cancelled or time out, and finished tasks no longer exhaust the
  500-task live capacity permanently.
- The context graph now consumes actual native tab, Space, window, project-context,
  task and Board metadata. It supports search, focus, pan, zoom, keyboard navigation
  and validated opening of existing tabs. A closed tab's old graph node cannot
  activate an unrelated tab. Relationships are derived from saved/browser data.
  It bounds overview/detail rendering and reports truncation.
- New profiles enter resumable onboarding automatically. Completion and skipping
  return to browsing; sidebar choices use the same persisted workspace setting as
  the browser controls. The selected option no longer depends on an animation's
  intermediate frame. Setup errors are recoverable and entrance motion is shorter.
- The syntax checker uses a private temporary directory. The separate design lab
  also had a deferred scroll-position regression: browser anchoring and smooth
  programmatic scrolling could compete with explicit restoration. Its updated test
  checks focus, selection, DOM identity and scroll position after subsequent frames.

## Historical verification

The first pass rebuilt the native product on macOS arm64 and passed 856 unit
tests across 34 binaries and 89 repository tests. Its patch verification covered
42 patches. Those figures predate the subsequently rejected Boost layout and the
new page-world blocking pipeline, and must not be presented as validation of the
current tree. The [revision record](seoul-visual-and-blocking-revision.md) contains
the current checks and evidence. Logs from both passes remain under
`/tmp/seoul-repair-20260907/`.

## Work still required before public release

| Priority | Work | Completion evidence |
| --- | --- | --- |
| P0 | Update the Chromium 149 baseline to a supported current engine, or establish verified applicable security backports | Isolated engine migration, complete patch roundtrip, dependency/build provenance and full integration/compatibility pass |
| P0 | Build a release distribution and secure updater | Developer ID signing, notarization, clean-machine install, verified signed update and profile-safe recovery from an interrupted update |
| P0 | Validate real-account AI and voice | Actual supported provider keys, microphone permission grant/deny, audible responses, interruption, network loss, cancellation and representative tasks; controlled mocks do not prove these journeys |
| P0 | Finish spending enforcement before promising cost limits | Account for planning, replanning, execution and voice usage; mark unknown prices as unknown; enforce a real budget across routes |
| P1 | Complete daily browser acceptance | Import, first-use/relaunch, downloads, permissions, extensions, session restore, recovery and multiwindow use on supported hardware; no known blockers in advertised journeys |
| P1 | Validate broad website compatibility | Representative login/CAPTCHA, media, banking, shopping and productivity workflows with Shields defaults and per-site recovery; blocking challenge scripts is not a CAPTCHA solution |
| P1 | Validate useful real-model output | The later revision renders page outlines without internal interaction handles. Validate useful summaries and actions through actual model tasks; rendering alone does not prove task success |
| P1 | Finish the visible organization model | Native folder/custom-title create, rename, move, collapse, delete and undo paths must agree with persisted state and graph output |
| P1 | Decide and deliver a real terminal/IDE scope | A trusted-workspace boundary, real process sessions, terminal rendering/input, lifecycle cleanup, file editing/saving and recovery; no decorative terminal or fake execution |
| P1 | Decide whether real phone handoff is a launch promise | Paired-device discovery, authenticated transfer, offline/error handling and delivery verification; the repaired picker currently changes presentation only |
| P2 | Expand graph usefulness with observed user needs | Durable navigation state, richer authoring/filtering, tested large-graph behavior and relationships that users can inspect; current graph is not Obsidian parity |
| P2 | Complete a product-wide visual/interaction pass | Consistent terminology, spacing, focus, motion, loading, empty and failure states across every advertised journey; light/dark, small windows, keyboard and assistive technology |

The engine update is a concrete release blocker: the official September 3 release
notice identifies stable Chromium 152, while this build is still 149.
[Official desktop release notice](https://chromereleases.googleblog.com/2026/09/stable-channel-update-for-desktop_01882797386.html).

## Recommended launch sequence

1. Stabilize this repaired foundation, then perform the engine migration and build
   the signed install/update path. Do not market a development build as ready for
   everyone simply because its own unit suite passes.
2. Make a complete daily-browsing loop dependable: install/import, organize pages,
   read/act with explicit approvals, customize a site, recover saved work and relaunch.
3. Prove real voice and agent task completion on a held-out set, including failure
   explanation, latency and cost. Ship only capabilities with working permissions,
   cancellation and recovery. Define terminal/IDE and true handoff explicitly.
4. Run a small external pilot with people outside the development setup. Record
   task completion, where help was needed, crashes, repeat use and failed actions.
   Use those observations to choose the first audience and the next fixes.

The ambition can remain broad. The first public promise should be specific enough
that a new user can finish it reliably. A YC demonstration should show the working
product and its measured usefulness, not claim superiority or universal acceptance
without evidence.
