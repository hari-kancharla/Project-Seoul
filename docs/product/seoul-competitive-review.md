# Seoul competitive review

Research date: 2026-07-11 (Arc, Zen, and VoiceOS primary sources rechecked).
Seoul's own column re-measured 2026-08-11 against the running build; see
section 4b, which supersedes the "unbuilt" status in the section 3 table.
This document separates four things and never blends
them: (a) documented competitor capability, (b) capability the reviewer could
personally confirm from an official source, (c) uncertainty, and (d) Seoul's
own implemented behavior with its evidence. It makes no claim that Seoul is
better than any product on any axis, because no comparative measurement has
been run. It records where Seoul's integration is structurally different and
what would have to be measured to turn "different" into "better".

Source tags used below:
- OFFICIAL: a vendor page was retrieved and read.
- OFFICIAL-URL: an official vendor URL was identified; the claim comes from its
  search snippet, not a full page fetch.
- THIRD-PARTY: non-vendor reporting.
- UNCERTAIN: could not confirm from any source.

## 1. Method and honesty constraints

The reviewer used web search and page fetches against official sources where
possible. Several vendor sites block automated retrieval (including some
assistant-browser, Zen, and Arc help-center pages), so some competitor claims
rest on official-URL snippets plus third-party reporting; those are tagged
accordingly. Vendor benchmark claims (for example accuracy percentages and
"first on benchmark X" statements) are recorded as vendor claims and are not
taken as verified.

Seoul rows describe source that exists in this repository at the milestone.
"Source complete" means the logic is authored and unit-tested at the source
level; it does NOT mean compiled or runtime-verified. "State contract only"
means no reachable UI exists yet and must not be treated as an implemented
feature (see
`docs/release/seoul-product-readiness.md` for the exact per-feature status and
the overall verdict). No Seoul row below should be read as a shipped,
measured, or user-visible capability yet.

## 2. Per-product capability notes

### Arc (The Browser Company, now Atlassian)
- Maintenance mode since the May 2025 members letter: new feature development
  stopped; security and Chromium updates continue (v1.153.0, 2026-06-25, on
  Chromium 149). [OFFICIAL: browsercompany.substack.com letter; resources.arc.net
  release notes]
- Spaces with per-Space pinned section, theme, and icon; "Air Traffic Control"
  routes matching URLs to Spaces. Split View. Command Bar. Peek link preview.
  Boosts for per-site customization (2026 scope of custom CSS/JS UNCERTAIN).
  AI bundle for previews, tab cleanup, and command-bar assistance. [OFFICIAL /
  OFFICIAL-URL: resources.arc.net]
- Voice input, local models, BYOK, MCP, action receipts: no official evidence
  [UNCERTAIN, likely none].

### Dia (The Browser Company / Atlassian)
- AI-first browser, macOS 14+ on Apple silicon only. Chat over full context
  across connected tools (GSuite, Slack, tabs); Morning Brief; Reports; Decks
  (generative slide artifacts); Skills (community-shared repeatable workflows);
  Splits; auto-organized tabs; profiles; end-to-end-encrypted sync. [OFFICIAL:
  diabrowser.com] Dia Pro at 20 USD/month [THIRD-PARTY].
- Voice, MCP, BYOK, agentic page execution: undocumented officially
  [UNCERTAIN].

### Perplexity Comet
- Free worldwide since October 2025; iOS app with voice mode listed in the App
  Store description; assistant summarizes, shops, schedules, researches in the
  browser; Max-tier Background Assistant with a mission-control dashboard.
  [OFFICIAL-URL hub blog + OFFICIAL App Store listing; THIRD-PARTY for
  Background Assistant] Official page blocks retrieval, so step-transparency,
  confirmation, and receipts are UNCERTAIN.
- MCP: Perplexity ships an official MCP server and connectors for paid tiers.
  [OFFICIAL-URL]

### Zen Browser
- Open-source, Firefox-based (1.21.4b, 2026-06-25). Workspaces with container
  defaults; Split View up to four tabs; Glance link preview; Compact Mode;
  Mods registry and CSS live editing. [OFFICIAL: github releases + docs] No AI,
  voice, or agentic features documented.
- The 2026-07-11 docs recheck confirms the interaction details Seoul should
  test rather than imitate visually: Compact Mode reveals hidden chrome at the
  relevant edge; Glance can close, promote to a tab, or become a split; Split
  View supports horizontal, vertical, and grid arrangements; and Workspaces can
  carry default container identities. [OFFICIAL: docs.zen-browser.app]

### Vivaldi
- Workspaces, tab stacking, tab tiling (split), Quick Commands palette, Command
  Chains (workflow automation), downloadable themes, web panels, built-in
  mail/calendar/feeds. [OFFICIAL: vivaldi.com/features] Explicit stance against
  adding an LLM chatbot, summarizer, or form-filler "until more rigorous ways
  to do those things are available." [OFFICIAL: vivaldi.com/blog]

### Opera (Opera One and Opera Neon)
- Opera One R3 (January 2026): Aria assistant rebuilt on an agentic engine
  derived from Neon; page/tab-island context; named tab islands; workspaces;
  split screen up to four; animated themes. Browser Operator (2025) described
  by Opera as the first major-browser AI agentic browsing. [OFFICIAL: blogs.opera.com]
- Opera Neon (2025-09-30, subscription): Tasks (agent workspaces), Cards
  (reusable prompts with a community store), Neon Do (acts inside the logged-in
  session). MCP + CLI tool connections; a 2026-03-31 MCP Connector lets external
  clients drive Neon (navigate, extract, screenshot, fill forms). [OFFICIAL:
  press.opera.com, operaneon.com, prnewswire release]
- Voice input: UNCERTAIN.

### Microsoft Edge
- The earlier browser-assistant mode retired 2025-05-13 with features folded
  into Edge. The agentic browsing feature for M365 Premium in the US selects,
  types, navigates, and scrolls multi-step; shows real-time cursor visibility;
  allows interrupt/take-control at any time; runs locally in the browser; asks
  for supervision on buying, booking, sending, or deleting; and asks before
  unfamiliar sites. Journeys (persistent topic views). Multi-tab reasoning.
  Voice and vision assistance, hands-free, on desktop and mobile. Vertical tabs.
  Workspaces migrating to Edge Sync with sharing removed. Collections retired.
  [OFFICIAL: blogs.windows.com, support.microsoft.com, learn.microsoft.com
  release notes]
- Local models / BYOK / MCP for consumer Edge: UNCERTAIN.

### Google Chrome
- Gemini in Chrome (multi-tab compare/summarize, AI Mode omnibox, password
  agent). Auto Browse (2026, Gemini 3, preview for AI Pro/Ultra): multi-step
  tasks with explicit confirmation pauses for purchases/posting and permission
  before password-manager sign-in; runs in a side panel. Skills in Chrome
  (save/reuse prompts and multi-tab workflows). On-device Gemini Nano via the
  Prompt API (Chrome 148) with multimodal input and structured output. Voice
  typing into websites. Tab groups plus AI tab organizer. [OFFICIAL: blog.google,
  developer.chrome.com]
- End-user BYOK: none found [UNCERTAIN, likely none].

### VoiceOS (WakoAI)
- OS-level voice assistant app for macOS (and Windows; mobile waitlist), NOT a
  browser. Dictation with per-app formatting; Agent Mode does multi-step
  voice-to-action across named connected services; draft-action confirmation
  cards expose the destination and content before execution, with explicit
  send/edit/cancel choices and click or voice confirmation; free tier plus paid
  tiers; "audio never stored unless you choose to share." [OFFICIAL:
  voiceos.com and voiceos.com/blog/send-emails-and-slack-messages-by-voice]
  Browser-native execution,
  plan-step transparency beyond the confirm card, local/cloud split, BYOK, MCP:
  UNCERTAIN. Distinct from the unrelated open-source Open Voice OS project.
- VoiceOS publishes 300-350 ms transcription latency and 97-98%+ recognition
  figures. These are vendor marketing claims, not independently reproduced
  measurements, and therefore are acceptance-input hypotheses only. [OFFICIAL
  VENDOR CLAIM: voiceos.com/blog]

### Other 2025-2026 agentic browsers
- A macOS assistant browser launched in 2025: in-page sidebar; agent mode
  (preview) that opens tabs and clicks; safety limits (no code execution, no
  file downloads, no other-app access; pauses on sensitive sites); optional
  browser memories. Windows unreleased as of 2026-07-01 [THIRD-PARTY].
- A browser-agent extension from a major model vendor: clicks and fills forms;
  site-level permissions; asks before high-risk actions; injection-attack
  success reduced in vendor testing. [OFFICIAL vendor blog]
- Brave Leo: browser-native assistant; summarize/translate/create; model choice
  including local models and third-party API keys (BYOK); conversations not used
  for training; iOS voice-to-text input. Agentic execution not claimed. [OFFICIAL:
  brave.com/leo]
- Aside (macOS, 2026): Chromium agentic browser; works across logged-in sites;
  local-first positioning; Agent Password Manager with a scoped-access audit log;
  BYO subscription or BYOK; vendor benchmark claims unverified. [OFFICIAL vendor
  claims: aside.com]

## 3. Facet comparison

Each cell is a status, not a score. "documented" means an official source
describes it; "not documented" means no official source was found (proving a
negative is impossible, so this is not the same as "absent"); "source complete
(unbuilt)" was Seoul's honest state on 2026-07-11.

The Seoul column below is therefore out of date: the product has since been
built and driven. It is left as written rather than quietly rewritten, because
the competitor cells were researched on that date too and re-dating one column
without re-verifying the others would be worse. Section 4b carries the measured
2026-08-11 state and is the one to read for where Seoul actually is.

| Facet | Arc | Dia | Comet | Zen | Vivaldi | Opera Neon | Edge | Chrome | Seoul |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Voice-to-action in the browser | no | not doc | voice mode | no | rejected | not doc | voice assistance | voice typing | source complete (unbuilt) |
| Visible plan / step transparency | no | not doc | not doc | n/a | n/a | Tasks | cursor + interrupt | confirm pauses | source complete (unbuilt) |
| Confirmation before risky actions | n/a | n/a | not doc | n/a | n/a | documented | documented | documented | source complete (unbuilt) |
| Generative / adaptive visual UI | no | Decks | not doc | no | no | structured output | no | no | source complete (unbuilt) |
| Persistent task view | no | not doc | Background Assistant | no | no | Tasks | Journeys | side panel | source complete (unbuilt) |
| Editable workflows from tasks | no | Skills | not doc | no | Command Chains | Cards | no | Skills | source complete (unbuilt) |
| Browser-native typed execution | n/a | n/a | agentic | n/a | n/a | Neon Do | agentic browsing | Auto Browse | source complete (existing command layer + planner) |
| Workspaces / spaces | Spaces | profiles | n/a | Workspaces | Workspaces | workspaces | Workspaces v2 | tab groups | source complete (existing) |
| Themes | per-Space | profiles | n/a | Mods | themes | themes | no | no | source complete (unbuilt) |
| Per-site customization (CSS) | Boosts | not doc | not doc | Mods CSS | UI theming | not doc | no | no | source complete (Site Layers, unbuilt) |
| Link preview | Peek | not doc | n/a | Glance | n/a | n/a | n/a | n/a | lifecycle source only (no WebContents host/UI) |
| Split view | yes | yes | n/a | up to 4 | tiling | up to 4 | n/a | n/a | source complete (existing, 2-pane) |
| Command palette / launcher | Command Bar | not doc | n/a | not doc | Quick Cmds | n/a | n/a | omnibox | source connected (searchable launcher, unbuilt) |
| Local models | no | not doc | no | no | no | not doc | not doc | Gemini Nano | source complete (interface; runtime unbuilt) |
| Cloud model choice / BYOK | no | not doc | tiers | no | no | subscription | not doc | no | source complete (interface; unbuilt) |
| Cost visibility per task | no | no | no | no | no | no | no | no | source complete (receipts, unbuilt) |
| Connected tools / MCP | no | not doc | MCP | no | no | MCP | not doc | not doc | source complete (connector seam; MCP adapter future) |
| Action verification / receipts | no | no | not doc | no | no | no | confirm flow | confirm flow | source complete (observe-verify + receipts, unbuilt) |
| User context control | no | toggles | not doc | n/a | n/a | not doc | memories | memories | source complete (Context Threads, unbuilt) |
| Recoverability of a task | no | not doc | not doc | n/a | n/a | not doc | not doc | not doc | source complete (checkpoint/restore, unbuilt) |

## 4. Where Seoul's integration is structurally different

These are architectural differences visible in the source, not measured
advantages. Each is a hypothesis to test once Seoul compiles and runs.

1. Voice, typed text, and Canvas interaction share one session state machine
   (`voice/voice_session.*`), rather than voice being a dictation button bolted
   onto a chat box. Barge-in stops speech immediately and preserves the running
   task by construction.
2. Adaptive visual output is a Seoul-owned, validated declarative protocol
   (`saui/`) with a fixed trusted component catalog. A model emits data; a
   trusted renderer builds the UI. No competitor here exposes a model-independent
   adaptive-UI protocol with a chart-honesty gate and an accessibility fallback.
3. Every data-backed visual must carry provenance and pass a chart-eligibility
   gate (`data/data_validation.*`, `saui/saui_validator.*`): a single unverified
   number cannot become a chart, and delayed data cannot render as live.
4. Task execution is observe-verify-decide with typed receipts and a hard rule
   that an unknown-outcome mutation is never auto-retried
  (`tasks/task_execution.*`). Edge, Chrome, and assistant browsers document confirmation
   flows; none document a per-step receipt with an explicit verification record.
5. Workflows are versioned typed graphs that compile onto the same bounded task
   system (`workflows/`), re-resolving semantic targets each run instead of
   replaying recorded coordinates.
6. Reasoning routes deterministic-first, then local, then cloud, with sensitive
   work forbidden from leaving the device and a per-task budget ceiling
   (`intelligence/reasoning_router.*`). Brave Leo and Aside offer BYOK/local
   choice; the explicit per-step deterministic/local/cloud router with a privacy
   floor is Seoul-specific in this set.
7. Context Threads make the forbidden content classes (passwords, cookies,
   tokens, raw audio, full history) structurally unrepresentable, and cloud
   scope is minimized before any send (`context/context_thread.*`).

## 4b. Where Seoul actually stands, measured 2026-08-11

Section 3 was written on 2026-07-11, when nothing was built; every Seoul cell
there says "source complete (unbuilt)". The product now compiles, runs, and has
been driven against live sites, so this section says what that produced. It is
deliberately blunt, and where Seoul is behind it says so.

### Behind

| Axis | Reference | Seoul, measured | Gap |
|---|---|---|---|
| Content blocking | Brave, uBlock Origin | EasyList + EasyPrivacy fetched at runtime onto a native Rust engine: 13 of 15 ad requests blocked on theverge.com, 25 of 33 on reuters.com, 5 of 5 on bbc.com/news | Largely closed. Seoul now runs the same two lists that are uBlock Origin's default core and a large part of Brave's. What is still behind: the signed component channel does not exist, so list integrity rests on the maintainers' TLS rather than a Seoul signature; the optional lists (uBlock filters, Brave unbreak) are catalogued but not enabled; and no cosmetic-filtering comparison has been run. |
| First run | Arc, Zen | A window with a rail, a command surface, and an empty content area | Arc and Zen both onboard. Seoul's startup tab is a deliberately inert placeholder, so the first thing a new user sees is nothing. The Welcome/onboarding row of the parity contract is marked `replace` and is not built. |
| Stability | Chrome, Brave, Arc | Two distinct startup/exit aborts found and fixed on 2026-08-11 alone | Both were configuration rather than logic - a code-sign clone incompatible with a component build, and Chromium's development assertions left live in a non-official build - but they were found by launching the app, not by the suite, which is the point. A session-storage invariant that fired during shutdown remains unreproduced and open. |
| Extensions | Chrome, Brave, Vivaldi, Zen | Not addressed in this repository | No claim is made here either way; it is simply not built. |

### Comparable in presence, unproven in polish

Vertical tab rail with workspaces, compact mode with hover reveal, split view,
per-site CSS (Boosts), themes, command palette. Zen and Arc have shipped and
iterated these with real users for a long time; Seoul has them implemented and
covered by 129 browser-level cases. Feature presence is not the same as feel,
and no comparative interaction measurement has been run - reaction to a real
user's hand is exactly what has not been tested.

### Structurally different, runtime unproven

Voice-first operation, the adaptive Canvas, typed task execution with receipts
and verification, Context Threads, Live Collections. No mainstream browser
ships this combination, which is the actual thesis of the product. The code
paths are compiled and deterministically tested; a credentialed session with a
real model and a real microphone has not been recorded. Until it has, this is
an architectural difference, not a demonstrated advantage.

### The honest one-line answer

Seoul is not currently better than Arc, Zen or Brave at the things those
browsers are good at. Blocking is now roughly comparable on the measurements
taken - it runs the same core lists on a native engine - but it onboards worse
than Arc and Zen, has no extension story, and has far less shipping maturity
than any of them. What it has that they do
not is a browser-native voice, task and adaptive-surface layer wired into
Chromium rather than bolted on, and that layer's advantage is unmeasured. The
gap list above is what has to close before any superiority claim is made, and
the constraint in section 5 stands unchanged.

## 5. What must be measured before any superiority claim

Do not state that Seoul is first or best on any of the above until these exist:
- A running build with its current verification recorded in the readiness
  report.
- Operator evaluation numbers on held-out goals (valid-plan rate, correct tool
  selection, verified-completion rate, unnecessary-cloud-call rate, latency,
   memory, cost) per `docs/quality/seoul-operator-evaluation.md`.
- Adaptive-UI conformance and malicious-payload-rejection results per
  `docs/quality/seoul-adaptive-ui-tests.md`.
- End-to-end fixture outcomes per `docs/quality/seoul-end-to-end-tests.md`.
- Direct, current re-verification of each competitor claim at comparison time,
  since these products change monthly.

## 6. Interaction decisions from the 2026-07-11 recheck

These are Seoul product decisions derived from the verified behavior, not a
request to copy competitor styling:

1. **Preserve context.** A preview must close back to the exact prior context,
   promote to a durable tab, or become a split without navigation reconstruction.
   Arc Peek and Zen Glance both make those three exits understandable.
2. **Reveal chrome by intent.** Focus mode hides chrome, but the relevant edge,
   keyboard command, and current task state keep it recoverable. Hidden controls
   may never become hover-only for keyboard or assistive-technology users.
3. **Make project context durable.** A workspace owns its visual identity,
   pinned/essential set, routing, and optional account isolation. Switching it
   must feel like returning to a room, not filtering one global tab list.
4. **Voice is an action loop, not a microphone icon.** The minimum loop is
   press/listen -> understood intent -> visible draft/plan -> confirm/edit/cancel
   -> execution -> verified receipt, with interruption available throughout.
5. **Latency claims need percentile budgets.** Seoul will not adopt a vendor's
   single marketing number. It needs measured p50/p95 first-partial, final
   transcript, plan-visible, first-surface, and action-confirmed latency on the
   floor device and the target build host.
6. **No domain router in UI or storage.** New information sources register typed
   capabilities and semantic result roles. Library, Canvas, and voice never gain
   enum cases or keyword branches for a business domain.

Primary pages rechecked:

- https://resources.arc.net/hc/en-us/articles/19228064149143-Spaces-Distinct-Browsing-Areas
- https://resources.arc.net/hc/en-us/articles/19335302900887-Peek-Preview-Sites-From-Pinned-Tabs
- https://resources.arc.net/hc/en-us/articles/19335393146775-Split-View-View-Multiple-Tabs-at-Once
- https://docs.zen-browser.app/user-manual/compact-mode
- https://docs.zen-browser.app/user-manual/glance
- https://docs.zen-browser.app/user-manual/split-view
- https://docs.zen-browser.app/user-manual/workspaces
- https://www.voiceos.com/
- https://www.voiceos.com/blog/send-emails-and-slack-messages-by-voice
