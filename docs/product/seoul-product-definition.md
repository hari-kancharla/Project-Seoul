# Seoul product definition

Status: This is the product contract. Current compile, test, runtime, and
release evidence is maintained in `docs/release/seoul-product-readiness.md`.

## What Seoul is

Project Seoul is the voice-first, visual, programmable personal browser. It is a
real native Chromium browser that combines Arc- and Zen-inspired organization
and customization, a VoiceOS-style voice-to-action interface, a persistent
generative visual workspace, general-purpose browser task execution, hybrid
local and cloud intelligence, editable visual workflows, browser-native
customization, verified action outcomes, and user-controlled context.

Seoul is useful as an ordinary browser with no model configured, and becomes
substantially more capable when intelligence is enabled.

Seoul is not an extension, an Electron app, a wrapper controlling Chrome, a
static chat sidebar, a clone of Arc/Zen/VoiceOS/Dia/Comet, a fixed workflow
service, a hardcoded demo, or a text generator that pretends work occurred.

## Open-ended by design

A user can speak or type an outcome and Seoul composes registered tools
dynamically to pursue it: organize the browser, research a question, compare
sources, fill a form, prepare but not submit an application, create a workflow,
monitor a page, customize a website, create a theme, inspect stocks or weather,
compare prices, plan travel, manage connected tools, or produce a report. These
are examples and evaluation fixtures, not hardcoded task categories. There is no
table of phrases mapped to canned behaviors; goals enter one general operator
(`native/seoul/browser/tasks/`, `tools/`) that plans over currently available
permitted tools.

## The five persistent surfaces

- Seoul Shell: the native Chromium Views browser interface (context header,
  Scene and workspace navigation, Essentials, projected tab views, role
  indicators, utility controls, Task Deck status, expanded/collapsed/Focus
  modes, recovery status). Implemented as native source under
  `native/seoul/browser/shell/` and the existing organization/projection layers.
- Seoul Canvas: the persistent voice, conversational, and visual side panel that
  renders validated adaptive surfaces. Protocol and validation in
  `native/seoul/browser/saui/`; see `seoul-canvas-spec.md`.
- Task Deck: the always-visible list and detail of active, paused, monitoring,
  completed, and failed tasks and workflows, with receipts. Model in
  `native/seoul/browser/tasks/task_deck_model.*`.
- Library: the searchable home for browser-owned archives/downloads plus Seoul
  Boards, captures, adaptive surfaces, workflows, and refreshable Live
  Collections. Its durable provider-neutral core is in
  `native/seoul/browser/library/`; see
  `seoul-library-boards-live-collections-spec.md`.
- Studio: the product area for Themes, Site Layers, Scenes, routing, workflow
  editing, local model management, providers, and connected tools. Backed by
  `themes/`, `site_layers/`, `scenes/`, `workflows/`, `intelligence/`,
  `connectors/`.

## Subsystems and where they live

| Subsystem | Source root | Spec |
| --- | --- | --- |
| Adaptive visual UI (SAUI) | `native/seoul/browser/saui/` | seoul-adaptive-ui-spec.md |
| Canvas | `saui/` + `voice/` | seoul-canvas-spec.md |
| Library, Boards, Live Collections | `native/seoul/browser/library/` | seoul-library-boards-live-collections-spec.md |
| Studio | `canvas/` + profile runtime registries | seoul-studio-spec.md |
| Preview lifecycle | `native/seoul/browser/preview/` | seoul-organization-v0.md + seoul-shell-interaction-contract.md |
| Voice operating layer | `native/seoul/browser/voice/` | seoul-voice-spec.md |
| Tool registry + operator | `tools/`, `tasks/` | seoul-task-deck-spec.md |
| Workflows | `native/seoul/browser/workflows/` | seoul-workflow-spec.md |
| Scenes | `native/seoul/browser/scenes/` | seoul-scenes-spec.md |
| Themes | `native/seoul/browser/themes/` | seoul-theme-system-spec.md |
| Site Layers | `native/seoul/browser/site_layers/` | seoul-site-layers-spec.md |
| Context Threads | `native/seoul/browser/context/` | seoul-context-threads-spec.md |
| Hybrid intelligence | `native/seoul/browser/intelligence/` | seoul-hybrid-intelligence-spec.md |
| Grounded data | `native/seoul/browser/data/` | (covered in adaptive-ui + provenance) |
| Connected tools | `native/seoul/browser/connectors/` | seoul-connected-tools-spec.md |
| Organization/projection/command/shell | `organization/`, `projection/`, `commands/`, `shell/` | native-architecture.md, seoul-organization-v0.md, workspace-projection-v0-spec.md |

## Non-negotiable product rules (enforced in source)

- No fabricated data. Every data-backed visual must originate from real
  structured tool output carrying provenance; a single unverified number never
  becomes a chart; delayed market data never renders as live; unavailable
  weather is reported as unavailable, never invented (`data/data_validation.*`,
  `saui/saui_validator.*`).
- No arbitrary code from a model. The adaptive UI is a validated declarative
  protocol; Site Layers compile to a safe scoped-CSS subset with no JavaScript;
  the planner may only call registered typed tools (`saui/`, `site_layers/`,
  `tools/`, `tasks/plan_validator.*`).
- Verified outcomes. Actions are observed and verified; an unknown-outcome
  mutation is never auto-retried; each step yields a receipt
  (`tasks/task_execution.*`).
- User-owned, minimized context. Forbidden content classes (passwords, cookies,
  tokens, raw audio, full history) are unrepresentable in a Context Thread, and
  cloud scope is minimized before sending (`context/context_thread.*`). Native
  page observations omit all current form-control values; protected-password
  state and standards-defined credential/payment autofill tokens also block
  model value mutations before they reach the renderer
  (`product/page_field_safety.*`, `product/browser/page_agent.*`).
- No hidden automation. Every background workflow and monitor is visible in the
  Task Deck (`tasks/task_deck_model.*`, `workflows/workflow_types.h`).
- Voice is not always-on; there is no raw-audio persistence by construction
  (`voice/voice_types.h` has no audio buffer type).

## What exists today vs what remains

The implemented native subsystems, organization/projection/command/shell
layers, and first-party Lit Canvas are compiled and covered by the native test
suites recorded in the readiness report. Studio edits local/cloud provider
routes, global Essentials, Themes, Scenes, routing rules, and workflows; Boosts edits and
immediately applies validated Site Layers. Scene activation owns real workspace,
Theme, compact, lifecycle, routing, and workflow behavior with exact baseline
restoration, including Workspace-scoped Scene/Theme and durable tab-membership
recovery across relaunch. Standalone compact mode is also implemented through
the native command launcher, `Command+Shift+C`, and the typed AI/voice
capability; its preference follows the Workspace and survives relaunch.
Remaining product and release work includes
real-device configured-provider evaluation, multi-window same-Workspace
presentation policy, distribution branding, a non-component release build,
signing, and notarization. The ordered Chromium patch series applies and
reverses cleanly against the pinned checkout. See the readiness report for
exact status.
