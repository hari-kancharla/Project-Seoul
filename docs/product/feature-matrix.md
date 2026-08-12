# Project Seoul Feature Matrix

## Honesty header

This document is the broad product direction, not a per-feature release
checklist. The authoritative current implementation and test evidence lives in
`docs/release/seoul-product-readiness.md`.

Seoul now has a compiled Chromium development build with a default-on vertical
shell, native organization and task services, an ephemeral Preview path, a
first-party Canvas, live per-site Boosts, persistent Boards, editable provider
routes, contextual page observation, and a Realtime voice-to-browser-task
bridge. That does not mean every item in this matrix is complete. Entries below
retain future-tense language where their full end-user behavior is still
roadmap work.

Seoul copies feature CAPABILITIES from other browsers. It does not copy their
source code, assets, branding, or layout. Every reference-browser note below
describes observable behavior, not an intent to reuse their implementation.
Seoul's interface and identity are original.

Upstream-Chromium-support claims are grounded in the local pinned checkout at
the external Chromium checkout (Chromium 149.0.7827.201,
revision 6a7b3dbec3b2ca25877c2553b5473b2f277ef644). Each cited path was verified
with `git grep` / `ls` in that checkout. Where no evidence was found, the field
reads "research required" rather than guessing. Cited paths are relative to that
checkout root unless stated otherwise.

A note on a recurring anchor: the pinned checkout is recent enough that it
already ships an agentic browsing stack under `chrome/browser/actor/` and an
on-device AI stack under `third_party/blink/renderer/modules/ai/` and
`components/optimization_guide/core/model_execution/`. Several AI features below
lean on these. They are upstream scaffolding, not finished Seoul features, and
they are default-gated; Seoul will still own the integration and the UX.

Milestones are a proposed ordering, not a schedule:

- M1: Shell and tab-model foundations (vertical tabs, workspaces, session).
- M2: Tab lifecycle and organization (favorites, pinned, temporary, routing).
- M3: Layout and navigation (split, preview, compact, command navigation).
- M4: Identity and isolation (containers, credential isolation).
- M5: AI understanding (page understanding, command surface for AI).
- M6: AI action (task execution, permissions, audit, pause/resume).

---

## 1. Workspaces / Spaces

- User problem: A single flat tab strip mixes unrelated contexts (work, personal,
  a specific project) into one cluttered surface, so users lose focus and lose
  tabs.
- Reference browser behavior: Arc "Spaces" group tabs per context, each with its
  own color theme; favorites stay constant across Spaces while pinned and
  unpinned tabs change per Space. Zen "Workspaces" group tabs by task/project and
  switch the live tab set instantly.
- What Seoul will implement: Named workspaces that each scope their own set of
  pinned and ordinary tabs, with fast switching that swaps the visible tab set.
  Original Seoul visual identity, not Arc's or Zen's layout.
- What Seoul will improve: Make workspace switching deterministic and
  session-durable (each workspace restores exactly what it held), and expose
  workspaces as first-class objects in command navigation rather than only as a
  sidebar affordance.
- Upstream Chromium support already available: Partial. The checkout has a richer
  tab-collection model than a plain tab strip, including
  `components/tabs/public/tab_collection.h`,
  `components/tabs/public/tab_strip_collection.h`,
  `components/tabs/public/pinned_tab_collection.h`, and an experimental projects
  panel at
  `chrome/browser/ui/tabs/projects/projects_panel_state_controller_interactive_uitest.cc`.
  No "workspace" concept matching the product definition was found; the grouping
  primitives exist but the per-workspace pinned/unpinned scoping does not.
- Expected native integration area: `chrome/browser/ui/tabs/` tab model and
  `components/tabs/public/` collections; sidebar UI under
  `chrome/browser/ui/views/tabs/` (research required for the exact view host).
- Risks and edge cases: Workspace state interacting with session restore;
  per-workspace theming bleeding into wrong contexts; moving a tab between
  workspaces; very large workspace counts; sync semantics across devices.
- Milestone: M1.
- Verification method: Create N workspaces, assign tabs, switch repeatedly, quit
  and relaunch; assert each workspace restores its exact tab set and order.
  Unit-test the workspace-to-collection mapping; interactive UI test for switch.

---

## 2. Global favorites / Essentials

- User problem: A handful of sites (mail, calendar, chat) are used in every
  context and should never disappear or need re-pinning per workspace.
- Reference browser behavior: Arc "Favorites" are available in every Space and
  are protected from auto-archiving; Zen "Essentials" are always-available tabs
  prioritized ahead of pinned and ordinary tabs by keyboard navigation.
- Current implementation: Canvas Studio creates, edits, and deletes
  profile-global Essentials, including a current-page capture path. Destinations
  are strict HTTP(S), duplicate origins are rejected, ordering and identity
  persist, the native Shell exposes them in every workspace, and opening one
  reuses an already-live tab for that site instead of spawning a duplicate.
- What Seoul improves: An Essential is a durable site identity rather than a
  copied tab. Removing the Essential never closes the user's live page.
- Upstream Chromium support already available: Partial. Pinned-tab persistence
  primitives exist (`components/tabs/public/pinned_tab_collection.h`,
  `chrome/browser/ui/tabs/pinned_tab_codec.h` family under
  `chrome/browser/ui/tabs/`). A cross-workspace "global favorites" tier was not
  found and is Seoul-specific.
- Expected native integration area: `chrome/browser/ui/tabs/` plus a new global
  store layered above per-workspace collections (research required for store
  location).
- Risks and edge cases: Favorite pointing at a logged-out or moved site;
  dedup-to-live-tab logic across workspaces; sync conflicts; ordering stability.
- Milestone: M2.
- Verification: Pure organization tests cover mutation, origin uniqueness,
  unsafe-URL refusal, persistence, and ordering. A headless in-process browser
  test creates and edits an Essential through the real Canvas, rejects a second
  definition for the same origin, observes the persisted profile record, opens
  the destination through the native Shell, proves a second open reuses the same
  live tab, and deletes the Essential without closing that tab.

---

## 3. Workspace-specific pinned tabs

- User problem: Some tabs matter only inside one context and should persist there
  without polluting other contexts or the global favorites tier.
- Reference browser behavior: Arc pinned tabs are saved per Space, never
  auto-archive, and differ from Favorites by being Space-dependent.
- What Seoul will implement: Pinned tabs scoped to a single workspace, persistent
  across restarts, exempt from temporary-tab cleanup.
- What Seoul will improve: Clear, visible tiering (favorites vs workspace-pinned
  vs temporary) so users always know which cleanup rules apply to a given tab.
- Upstream Chromium support already available: Yes for the pinning primitive:
  `components/tabs/public/pinned_tab_collection.h` and the pinned-tab
  serialization under `chrome/browser/ui/tabs/` (pinned tab codec). The
  per-workspace scoping is Seoul-specific and not present.
- Expected native integration area: `chrome/browser/ui/tabs/` (pinned collection
  + workspace association); persistence alongside workspace store (research
  required for store location).
- Risks and edge cases: A pinned tab whose workspace is deleted; moving a pinned
  tab to another workspace; ordering relative to favorites; restore ordering.
- Milestone: M2.
- Verification method: Pin tabs in workspace A only; confirm they appear in A and
  not B; confirm exemption from temporary-tab cleanup; relaunch and assert
  per-workspace restoration.

---

## 4. Temporary tabs and lifecycle

- User problem: Most opened tabs are read-once and never closed, so the tab set
  grows without bound and degrades focus and memory.
- Reference browser behavior: Arc auto-archives idle unpinned tabs (default 12
  hours, timer resets on view, cannot be fully disabled, interval adjustable);
  pinned tabs and favorites are exempt.
- Current implementation: An active Scene can enable a configurable idle
  archive interval for temporary tabs and restore eligible archives when the
  Scene is activated again. The production sweep protects the active tab,
  retained/Essential tabs, media, loading or unsaved pages, permission prompts,
  DevTools, splits, active tasks, and in-progress downloads. Archive commits
  only after exact Chromium removal; restore is background-only and leaves a
  durable archive intact when insertion cannot be verified.
- What Seoul will improve: Make the policy explicitly configurable (including a
  user-chosen long interval) and pair archiving with memory discard so the
  feature also reclaims resources, not just visual clutter.
- Upstream Chromium support already available: Yes for the resource side: tab
  lifecycle and discard machinery exists at
  `chrome/browser/resource_coordinator/tab_lifecycle_unit.cc`,
  `chrome/browser/resource_coordinator/tab_lifecycle_unit_source.cc`, and
  `chrome/browser/resource_coordinator/tab_lifecycle_unit_external.cc`. The
  time-based auto-archive-to-recoverable-list UX is Seoul-specific.
- Expected native integration area: `chrome/browser/resource_coordinator/` for
  discard, plus tab-model bookkeeping in `chrome/browser/ui/tabs/` and an archive
  store (research required for archive store location).
- Risks and edge cases: Losing unsaved form state on archive; timer correctness
  across sleep/wake; exemption correctness for pinned/favorites; recovery UX;
  interaction with session restore.
- Milestone: M2.
- Verification: Pure organization tests cover eligibility and exemptions. A
  headless in-process browser test ages a real background temporary tab,
  archives it transactionally, verifies recoverable metadata, reactivates its
  Scene, and confirms background restoration without disturbing the active tab.
  Resource discard is not claimed by that archive path.

---

## 5. Link routing

- User problem: Opening a link from an external app or from one context drops it
  into the wrong place, disrupting the current workspace.
- Reference browser behavior: Arc opens external links in "Little Arc", a
  temporary lightweight window, so a quick link does not disturb your Spaces and
  tabs. Installed PWAs capture in-scope links into the app window.
- Current implementation: Studio authors priority-ordered exact-origin,
  URL-prefix, URL-glob, and fallback rules. The real open capability can route
  into the current tab, a new temporary or retained tab, a specific workspace,
  an ephemeral Preview, a native split pane, an external application, or an
  explicit approval path.
- What Seoul will improve: Make routing rule-based and inspectable (route by
  source, by URL pattern, by workspace) instead of a single fixed behavior.
- Upstream Chromium support already available: Partial. Web-app link capturing
  exists at `chrome/browser/ui/web_applications/web_app_link_capturing_browsertest.cc`
  and `chrome/browser/ui/web_applications/web_app_launch_utils.cc`, with
  `apps::test::LinkCapturingFeatureVersion` exercised in
  `chrome/browser/web_applications/chromeos_web_app_experiments_browsertest.cc`.
  This covers PWA scope capture, not general workspace-aware routing, which is
  Seoul-specific.
- Expected native integration area: navigation/launch path in
  `chrome/browser/ui/web_applications/` and the browser navigation entry points
  (research required for the external-link handoff entry point on macOS).
- Risks and edge cases: Rule precedence and conflicts; external-app handoff on
  macOS; loops between routing and capture; routing into an archived/closed
  workspace.
- Milestone: M2.
- Verification: Organization tests cover precedence, matching, update, and
  dispositions. Headless browser coverage proves specific-workspace,
  native-split, current-tab, and Preview destinations through the real
  capability and command layers. OS-level external-app handoff remains a
  platform acceptance test.

---

## 6. Split views

- User problem: Comparing or referencing two pages requires manual window
  tiling, which is fiddly and not persisted.
- Reference browser behavior: Zen Split View shows up to 4 tabs in a grid via
  drag-to-side. Chromium itself supports a two-pane split.
- Current implementation: The typed split capability and routing path create a
  real Chromium two-pane split from exact live tab identities and commit Seoul
  organization state only after the native mutation is observed.
- What Seoul will improve: Persist split layout across restart and tie it to a
  workspace, and integrate split with command navigation (create/dissolve split
  by command). Whether Seoul exceeds two panes depends on extending the upstream
  two-pane model and is a research item, not a promise.
- Upstream Chromium support already available: Yes, two-pane split exists.
  `chrome/browser/ui/views/frame/multi_contents_view.h` and `.cc` host the split
  (max two panes); the model lives in `components/tabs/public/split_tab_data.h`
  and `components/tabs/public/split_tab_collection.h`; the command id
  `IDC_NEW_SPLIT_TAB` (value 34055) is defined at
  `chrome/app/chrome_command_ids.h`.
- Expected native integration area:
  `chrome/browser/ui/views/frame/multi_contents_view.{h,cc}` and the split tab
  model under `components/tabs/public/`; session glue in
  `chrome/browser/sessions/`.
- Risks and edge cases: Going beyond two panes is a substantial change to the
  upstream contents-view model; focus and keyboard target ambiguity; restoring
  split state; per-pane navigation history.
- Milestone: M3.
- Verification: Pure model/lifecycle tests cover exact split membership and
  reconciliation; headless browser tests cover both an explicit two-tab split
  capability and a routed open into a split. Relaunch restoration and
  drag-to-create remain open.

---

## 7. Peek / Glance-style preview

- User problem: Following a link to skim it forces a full tab switch and a return
  trip, breaking flow.
- Reference browser behavior: Zen "Glance" (Alt+click) opens a floating preview
  overlay on top of the current tab without switching; equivalent in spirit to
  Arc's Little Arc.
- Current implementation: Seoul owns an ephemeral non-tab-strip WebContents
  host that opens over an exact source tab, keeps navigation bounded, dismisses
  cleanly, and can be selected by routing without creating a retained tab.
  Explicit promotion re-evaluates routing, transfers the same live WebContents,
  creates retained membership in the resolved Workspace, or creates a real
  Chromium split. Approval/external routes fail before transfer and leave the
  Preview ready.
- What Seoul will improve: Add a faster modifier/hover gesture alongside the
  shipped context-menu command without weakening the exact source-tab binding.
- Upstream Chromium support used: browser Views bubble hosting, independent
  WebContents ownership, TabStripModel insertion, and native split-tab APIs.
- Native integration area: `native/seoul/browser/preview/` plus the localized,
  reversible Chromium context-menu bridge in patch 0001.
- Risks and edge cases: Input focus and dismissal correctness; preventing the
  preview from polluting underlying tab history; nested previews; media/autoplay
  in preview; promote-to-tab edge cases.
- Milestone: M3.
- Verification: Headless browser coverage proves the Preview stays outside the
  tab strip, survives routing rejection unchanged, promotes into a routed
  Workspace as retained membership, and can be routed into a native split.
  Modifier gesture wiring remains open.

---

## 8. Compact mode

- User problem: Browser chrome consumes vertical and horizontal space that users
  want for content.
- Reference browser behavior: Zen "Compact Mode" hides toolbars for a wider
  content view, toggled by shortcut or toolbar right-click; revealed on hover.
- Current implementation: Standalone compact mode drives the real native
  vertical-tab collapse and reveal-on-hover controls. It is available from the
  fuzzy command launcher, `Command+Shift+C`, and the typed
  `browser.compact.set` capability used by Canvas, tasks, and voice. The
  preference is durable per Workspace and reapplied on Workspace switches and
  browser relaunch. Scene activation temporarily owns the same controls;
  manual changes are disabled with an explicit reason, and leaving the Scene
  restores the exact prior collapse and hover settings.
- What Seoul improves: Compact state follows the durable Workspace rather than
  being a one-off window animation, while command navigation and its keyboard
  shortcut remain available when the chrome is collapsed.
- Upstream Chromium support already available: The pinned revision provides
  `tabs::VerticalTabStripStateController` collapse, reveal-on-hover, state
  notifications, and session hooks. Seoul composes those native controls with
  its Workspace/Scene persistence instead of introducing a parallel toolbar.
- Expected native integration area: Seoul's profile runtime, shell controller,
  and command launcher over Chromium's vertical-tab state controller.
- Risks and edge cases: Reveal-on-hover thrash; losing access to controls; and
  interaction with OS fullscreen on macOS. Seoul uses a pinned window-local
  hover setter so different browser windows do not mutate Chromium's
  profile-wide stock preference or fight over presentation.
- Milestone: M3.
- Verification: Browser coverage enters compact mode through the native command
  path, switches between Workspaces with different preferences, exits through
  the typed AI capability only after observing the native postcondition, and
  proves Scene ownership fails closed. Separate two-process relaunch coverage
  restores standalone compact state. The Studio test also verifies compact
  Scene activation and exact baseline restoration, including across relaunch.

---

## 9. Command navigation

- User problem: Reaching tabs, workspaces, and actions by mouse is slow;
  keyboard-first users want a single fuzzy entry point.
- Reference browser behavior: SupaSidebar/Arc-style Command+E fuzzy search jumps
  to any tab; Dia exposes "Skills" (prewritten prompts) from a command-like
  surface.
- Current implementation: The native command palette indexes live tabs across
  profile windows, non-archived workspaces, Essentials, and typed browser
  utilities. It uses deterministic fuzzy/subsequence ranking, bounds catalog
  and result sizes, revalidates live targets at execution, and focuses another
  window only after exact tab activation succeeds.
- What Seoul will improve: Unify navigation targets (tabs, workspaces,
  favorites, splits, AI actions) in one ranked palette rather than separate
  surfaces, and keep it functional in compact mode.
- Upstream Chromium support already available: Partial. No dedicated commander
  directory (`chrome/browser/ui/commander/`) exists in this checkout. The omnibox
  action framework exists (`components/omnibox/browser/actions/`, e.g.
  `contextual_search_action.h`) and can host some actions, but a tab/workspace
  command palette is Seoul-specific.
- Native integration: `native/seoul/browser/shell/command_launcher_catalog.*`,
  `shell_controller.*`, and `views/seoul_command_launcher_view.cc`, hosted in
  the default-on native vertical Shell.
- Risks and edge cases: Ranking quality; large tab/workspace counts; keyboard
  focus capture; collision with omnibox; localization of command names.
- Milestone: M3.
- Verification: Pure catalog tests cover fuzzy ranking, complete target kinds,
  invalid/archived exclusions, and deterministic bounds. Headless in-process
  browser tests prove exact live-tab activation and rejection when a tab closes
  after the search result was produced.

---

## 10. Session restore

- User problem: A crash or quit must not lose the user's carefully arranged
  workspaces, splits, pinned tabs, and favorites.
- Reference browser behavior: Most Chromium browsers restore the last session;
  Zen and Arc restore their workspace/Space structure and pinned tabs.
- What Seoul will implement: Full restoration of Seoul's structured state
  (workspaces, per-workspace pinned tabs, global favorites, splits, compact
  state) on launch and after crash.
- What Seoul will improve: Extend restore beyond Chromium's tab/window model to
  cover Seoul's workspace and favorites tiers, with deterministic ordering.
- Upstream Chromium support already available: Yes for the base mechanism:
  `chrome/browser/sessions/session_restore.cc` and
  `chrome/browser/sessions/session_restore.h`. Seoul's structured state (workspaces,
  favorites, splits-as-workspace-state) must be serialized in addition and is not
  present.
- Expected native integration area: `chrome/browser/sessions/` for the restore
  pipeline, extended to carry workspace/favorites/split state (research required
  for the serialization schema location).
- Risks and edge cases: Schema migration across Seoul versions; partial restore
  after corruption; restore ordering vs auto-archive; restoring split layouts;
  crash-loop on a bad restore record.
- Milestone: M1.
- Verification method: Build a rich session, force-quit and crash, relaunch;
  assert workspaces, pinned tabs, favorites, splits, and compact state restore
  exactly; test a corrupted record degrades gracefully.

---

## 11. Account / container isolation

- User problem: Logging into the same site with two identities (work vs personal)
  collides cookies and sessions in one profile.
- Reference browser behavior: Zen (Firefox-based) "Container Tabs" isolate
  cookies/storage per container so multiple identities coexist.
- What Seoul will implement: Container-style isolation that scopes
  cookies/storage so a tab can carry a distinct identity, associable with a
  workspace.
- What Seoul will improve: Tie containers to workspaces so identity follows
  context automatically, instead of requiring manual container selection per tab.
- Upstream Chromium support already available: Partial. Chromium isolates state
  by `StoragePartition` (`content/public/browser/storage_partition.h`) and by
  profile (`chrome/browser/profiles/profile.h`). These are the building blocks;
  a per-tab, workspace-associated container abstraction layered on storage
  partitions is Seoul-specific.
- Expected native integration area: storage partitioning in `content/` consumed
  via `chrome/browser/profiles/` and the tab model in `chrome/browser/ui/tabs/`
  (research required for the per-tab partition assignment point).
- Risks and edge cases: Leakage between containers; extension and download
  scoping; service-worker and cache partitioning; sync of container definitions;
  performance of many partitions.
- Milestone: M4.
- Verification method: Log into one site in two containers, assert independent
  sessions/cookies; assert workspace association applies the right container;
  assert no cross-container leakage in storage and cache.

---

## 11b. Content blocking

- User problem: Advertising and cross-site measurement are the dominant cost of
  the modern web in bandwidth, battery, latency and privacy, and a browser that
  leaves them to an extension leaves them on by default for most people.
- Reference browser behavior: Brave blocks ads and trackers natively with its
  own Rust engine and shipped lists; uBlock Origin does the same as an
  extension; Safari exposes a content-blocker API rather than blocking itself.
- What Seoul implements today: a native browser-process blocker with a Rust
  engine, network interception, frame-associated WebSocket interception, CSS
  cosmetic filtering, a CSP response filter, per-site modes, temporary disable,
  two engine groups, and last-known-good storage. All of it is compiled and
  covered by three unit binaries plus browser and renderer cases.
- What it runs: a Seoul-authored bundled baseline as the floor, plus EasyList
  and EasyPrivacy fetched over HTTPS at startup and refreshed daily, installed
  into the default engine on top of the baseline. Measured on 2026-08-12: 13 of
  15 ad requests blocked on theverge.com, 25 of 33 on reuters.com, 5 of 5 on
  bbc.com/news.
- What is NOT true yet: the signed component channel does not exist, so list
  integrity rests on the list hosts' TLS rather than a Seoul signature. That is
  the stronger delivery path and it needs a release signing identity, not code.
  The optional catalogued lists are not enabled by default, and no cosmetic
  filtering comparison against Brave or uBlock has been run.
- Upstream Chromium support already available: no native blocker; Chromium
  supplies the URLLoader interception seam, content settings, and the component
  updater that the signed list channel is registered against.
- Native integration area: `seoul/browser/adblock/`, `seoul/renderer/`, and
  patches 0020-0024.
- Honest status: engine complete and tested; blocking materially better than
  nothing and materially worse than Brave. See
  `docs/release/seoul-product-readiness.md` for the measurement and
  `docs/research/native-adblock-implementation.md` for the unfinished parity
  list.

## 12. Native AI page understanding

- User problem: Reading, summarizing, and asking questions about a page (or
  across several tabs) requires copy-pasting into an external tool.
- Reference browser behavior: Dia's assistant reads page content, summarizes,
  answers questions, and references other tabs via @tab-name for cross-tab
  comparison; Arc Max and Comet offer similar page-context answering.
- What Seoul will implement: An in-browser assistant that can read the current
  page (and explicitly referenced tabs) to summarize and answer questions, with
  an original Seoul UI.
- What Seoul will improve: Make tab context explicit and user-controlled (which
  tabs are in scope is visible and revocable), and route each request to the
  best available model for the job.
- Upstream Chromium support already available: Yes, substantial scaffolding. The
  Blink AI module exists at `third_party/blink/renderer/modules/ai/language_model.cc`
  (and `language_model_prompt_builder.cc`), and the on-device model execution
  stack lives under `components/optimization_guide/core/model_execution/`
  (`on_device_execution.cc`, `on_device_context.cc`, `on_device_capability.cc`,
  `on_device_asset_manager.cc`). Page content extraction can use the accessibility
  tree snapshot exposed via `content/public/browser/web_contents.h`. These are
  default-gated upstream primitives, not a finished assistant.
- Expected native integration area: model execution via
  `components/optimization_guide/core/model_execution/`, page capture via the AX
  snapshot API in `content/public/browser/web_contents.h`, and a new assistant UI
  (research required for the UI host).
- Risks and edge cases: Privacy of page content sent to any model; large-page
  truncation; on-device model availability and download size; hallucinated
  summaries; cross-tab scope creep.
- Milestone: M5.
- Verification method: Summarize and Q&A on fixture pages with known content;
  assert answers cite in-scope tabs only; assert on-device path is used when the
  model asset is present; assert page content is not sent out of scope.

---

## 13. Task execution

- User problem: Multi-step web chores (filling a form, navigating a flow,
  gathering data across pages) are tedious and repetitive.
- Reference browser behavior: Perplexity Comet's assistant monitors open tabs,
  carries context across pages/sessions, and executes multi-step workflows with
  minimal clicks (click, navigate, fill forms), pausing for important steps.
- What Seoul will implement: A constrained agent that performs multi-step
  in-browser actions (navigate, click, fill) on the user's behalf, scoped to
  approved sites, with original Seoul UX.
- What Seoul will improve: Make the action set, site scope, and per-step
  approvals explicit and conservative by default (see features 14-18), prioritizing
  safety over autonomy.
- Upstream Chromium support already available: Yes, an actor framework exists.
  `chrome/browser/actor/` contains `actor_task.cc`, `execution_engine.cc`,
  `tool_request_variant.cc`, a `tools/` directory, `tab_observation_controller.cc`,
  and `actor_navigation_throttle.cc`; the glic UI/actor bridge is at
  `chrome/browser/glic/actor/`. The README at `chrome/browser/actor/README.md`
  documents the ActorTask / ExecutionEngine / ToolController / Tool ownership
  chain. This is upstream scaffolding, default-gated, not a finished Seoul agent.
- Expected native integration area: `chrome/browser/actor/` (execution engine and
  tools) bridged to a Seoul-owned assistant UI; observation via
  `chrome/browser/actor/tab_observation_controller.cc`.
- Risks and edge cases: Prompt injection from page content driving the agent;
  unintended destructive actions; auth flows and purchases; runaway loops;
  cross-origin navigation during a task.
- Milestone: M6.
- Verification method: Run scripted multi-step tasks against local fixtures;
  assert each tool action matches intent; assert site scope is enforced by
  `chrome/browser/actor/site_policy.cc`; assert navigation gating via the actor
  navigation throttle.

---

## 14. Permissions and approvals

- User problem: An agent acting in the browser must not silently take consequential
  actions; the user needs to grant and withhold capability.
- Reference browser behavior: Comet pauses and asks permission for important
  actions (login, purchase); admins can allow/deny the assistant's ability to
  click, navigate, and fill on a case-by-case basis.
- What Seoul will implement: A capability/approval layer where the agent's tools
  are gated by per-site and per-action policy, with explicit prompts for
  consequential steps and conservative defaults.
- What Seoul will improve: Default to deny, require explicit per-site enablement,
  and surface a clear "what is about to happen" prompt before each consequential
  action rather than a blanket session grant.
- Upstream Chromium support already available: Yes for building blocks. The actor
  framework has site/enterprise gating at `chrome/browser/actor/site_policy.cc`
  and `chrome/browser/actor/enterprise_policy_checker.h`, and the general
  permissions framework lives at `components/permissions/permission_manager.cc`
  and `components/permissions/permission_request.cc`. The user-facing per-action
  approval UX is Seoul-specific.
- Expected native integration area: `chrome/browser/actor/site_policy.cc` for
  scope gating, `components/permissions/` for the request model, and a Seoul
  approval UI (research required for the UI host).
- Risks and edge cases: Prompt fatigue leading to blind approval; scope of a
  grant (one action vs session vs site); revocation mid-task; default safety vs
  usability; enterprise policy overrides.
- Milestone: M6.
- Verification method: Attempt gated actions on non-approved sites and assert
  refusal via site policy; assert consequential actions prompt; assert deny
  blocks the action and the task halts cleanly.

---

## 15. Credential isolation

- User problem: An agent must not be able to read or exfiltrate the user's saved
  passwords, cookies, or active login sessions.
- Reference browser behavior: Comet documents permission controls around what the
  assistant can access and pauses before logins; agentic browsers are a known
  prompt-injection target (Brave documented Comet indirect prompt injection), so
  credential boundaries matter.
- What Seoul will implement: A boundary that prevents the AI/agent layer from
  reading raw credential stores and from harvesting cookies/session tokens; the
  agent operates on rendered pages, not on the secret store.
- What Seoul will improve: Make credential access an explicit, separately gated
  capability that is off by default and never implied by general page-action
  approval.
- Upstream Chromium support already available: Partial / research required. State
  isolation primitives exist (`content/public/browser/storage_partition.h`,
  `chrome/browser/profiles/profile.h`), and actor site gating exists
  (`chrome/browser/actor/site_policy.cc`), but a verified rule that the actor tool
  layer cannot read the password store was not confirmed in this checkout and must
  be designed and verified by Seoul.
- Expected native integration area: boundary between `chrome/browser/actor/tools`
  and the credential store under `chrome/browser/` password management (research
  required for the exact password-store access boundary).
- Risks and edge cases: Autofill exposing secrets to the page DOM the agent can
  read; cookie/session-token readout via DOM or devtools-like tools; injection
  tricking the agent into self-exfiltration; clipboard side channels.
- Milestone: M4 for the isolation boundary, enforced before M6 agent actions ship.
- Verification method: Attempt to make the agent read saved passwords and
  session cookies; assert all such attempts fail; injection red-team test on
  fixtures attempting credential exfiltration.

---

## 16. Action audit trail

- User problem: After an agent acts, the user needs a trustworthy record of
  exactly what it did, to review and to recover from mistakes.
- Reference browser behavior: Agentic browsers (Comet) describe taking actions on
  the user's behalf; a reviewable log of those actions is the accountability
  mechanism.
- What Seoul will implement: A durable, user-viewable journal of every agent
  action (tool, target, parameters, result, timestamp) per task.
- What Seoul will improve: Make the journal user-facing and per-task reviewable,
  not just a metrics/telemetry stream, and tie each entry to the approval that
  authorized it.
- Upstream Chromium support already available: Yes, an aggregated journal exists
  in the actor framework: `chrome/browser/actor/aggregated_journal.cc`,
  `chrome/browser/actor/aggregated_journal_file_serializer.cc`, and
  `chrome/browser/actor/aggregated_journal_in_memory_serializer.cc`; per-action
  metrics tracking at `chrome/browser/actor/action_tracker_for_metrics.cc` and
  `chrome/browser/actor/actor_metrics.cc`. The user-facing review UI is
  Seoul-specific.
- Expected native integration area: `chrome/browser/actor/aggregated_journal.cc`
  for capture and a Seoul review UI (research required for the UI host).
- Risks and edge cases: Journal growth and retention; redaction of sensitive
  parameters in the log; tamper-resistance; mapping entries to approvals;
  performance overhead of journaling.
- Milestone: M6.
- Verification method: Run a multi-step task and assert every executed tool
  appears in the journal with target/params/result/timestamp; assert sensitive
  fields are redacted; assert the log persists for review after task completion.

---

## 17. Pause / resume and no-replay behavior

- User problem: A user must be able to stop a running agent mid-task, inspect it,
  and resume without the agent re-executing steps it already completed.
- Reference browser behavior: Comet pauses for important steps and resumes;
  responsible agent design requires that resuming does not duplicate completed
  side-effecting actions (e.g. a purchase must not fire twice).
- What Seoul will implement: Explicit pause and resume controls on a running
  task, with resume continuing from the next pending step and never replaying
  already-committed actions.
- What Seoul will improve: Tie no-replay to the audit journal (feature 16): a
  step recorded as committed is never re-run on resume, and idempotency is
  enforced for side-effecting tools.
- Upstream Chromium support already available: Partial / research required. The
  actor task model exists (`chrome/browser/actor/actor_task.cc`,
  `chrome/browser/actor/execution_engine.cc`) and the README documents the task
  lifecycle, but explicit user-facing pause/resume with no-replay guarantees was
  not confirmed in the checkout and must be designed and verified by Seoul.
- Expected native integration area: `chrome/browser/actor/actor_task.cc` and
  `chrome/browser/actor/execution_engine.cc` for task state, with the journal as
  the source of truth for completed steps (research required for the pause hook).
- Risks and edge cases: A step that committed externally but was not yet journaled
  (crash window); resuming after a long pause when page state changed; double-fire
  on side-effecting actions; resume after browser restart.
- Milestone: M6.
- Verification method: Start a task with a side-effecting step, pause after it
  commits, resume; assert the committed step does not re-run; crash mid-task and
  resume; assert no duplicate side effects using a fixture that counts actions.
