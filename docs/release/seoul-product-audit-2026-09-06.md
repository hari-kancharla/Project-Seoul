# Project Seoul: product and release audit

Audited September 6, 2026, America/New_York. Source snapshot: `687c18d2fafb0e05293fe1cf96312fcd21aa2bf1`. Chromium baseline: `149.0.7827.201`, revision `6a7b3dbec3b2ca25877c2553b5473b2f277ef644`. This audit covers the native Chromium product. The separate Swift overlay and extension harness are not evidence that the native product's voice experience works.

**Verdict: Seoul is a substantial, functioning development browser. It is not ready to distribute as a finished, premium, general-purpose browser.** The remaining work includes broken provider integration, incomplete product journeys, engine maintenance and distribution infrastructure, as well as visual and interaction polish. A redesign alone will not resolve this.

The strongest foundation is the native browser organization, typed operations, permission checks, result validation, per-site controls, and reversible integration. The biggest problem is the distance between those foundations and what an ordinary person can discover, configure, complete, recover, and trust.

No audit can establish that a browser has zero bugs. A credible release standard is zero known critical security or data-loss defects, reliable supported journeys, honest failure states, accessible controls, and a working mechanism for delivering fixes. Those are concrete requirements, not a reason to accept avoidable flaws.

## Evidence and its limits

The audit combined source tracing, current tests, native application inspection, and official external sources. Existing product code was not edited. The worktree initially contained 12 modified files above `4ba82f5`; those changes were committed by other work during the audit, ending at `687c18d`. The final change set from the starting HEAD matches those 12 files. This audit did not create those commits.

| Check performed | Fresh result | What it establishes |
| --- | --- | --- |
| `npm test` | 89 passed, zero failed or skipped | Repository protocol, design-lab, Boost-editor, extension-harness and transform tests pass. These are not 89 native product journeys. |
| Existing native unit executables | 34 binaries, 844 tests passed, zero failures or disabled tests | The already-built native unit suites pass. |
| Existing focused native browser suite | Runner succeeded; 171 invocations, including three `PRE_` setup invocations; 170 passed and one optional visual-capture case skipped | Native integration coverage passes with serial execution, headless mode, mock Keychain, GPU disabled and `InitialWebUI` disabled. The launcher summary labels the skipped capture case `SUCCESS`; its result parts explicitly record the skip. |
| Product smoke | Passed; launch 3,816 ms, local navigation 551 ms, Canvas ready 496 ms, 25 view switches 18 ms | The local build launches, renders Canvas and navigates under the test conditions. These are single-run smoke timings, not comparative performance benchmarks. |
| Product churn | Passed: 15 Canvas remounts, 12 concurrent Canvas tabs, 60 activations and eight heavy-document navigation cycles | Short-duration churn caused no observed defects. The measured JavaScript heap went from 3.1 MB to 2.7 MB; this is not total browser memory. |
| Source materialization | Passed, including a later recheck | The Seoul source and protocol mirrored into the external checkout match this repository. |
| Patch integrity | All 41 patches applied and reversed in order in a separate scratch tree; 190 baseline files restored byte-for-byte; all 212 resulting patched files matched the active checkout | The current patch sequence is reproducible at the file level. No active checkout patches were reversed. This does not prove a clean full build or correctness after upgrading Chromium. |
| Native build dry run | Four pending actions, including app-entry compilation and relinking/bundle refresh | A freshly rebuilt, exact-source release artifact was not established by this audit. The tests above used existing binaries. |
| Application signature inspection | Ad-hoc signature, no TeamIdentifier, no sealed resources | This is not a Developer ID-signed distribution package. |
| Static checks | All 16 gates passed with a fresh, private `TMPDIR`; syntax parsing covered 205 files and skipped 91 requiring generated dependencies | The default-temporary-directory run failed first; see the addendum. This is parse-only validation, not compilation or linking. |

Native UI inspection covered the shell, the new-tab/address surface, welcome URL, Canvas, Studio and cloud configuration, Library, Boards, a real local tab-inventory task, ordinary navigation to `example.com`, Shields, Boosts and the Handset menu. A dedicated inspection window was closed afterwards. No user credentials were entered; microphone access, paid models, account connections, purchases and form submissions were not exercised.

The first-run page was opened directly. A fresh-install automatic onboarding flow was not demonstrated. The native UI observations were on one Mac and one ordinary window size, approximately 1128 × 768. They do not constitute a full VoiceOver, keyboard, localization, multi-monitor or hardware matrix.

Raw execution evidence is in `/tmp/seoul-product-audit-20260907/`, including the smoke and stress logs, per-binary unit results, browser launcher summary and patch hashes. That is a temporary evidence location, not a durable release archive. Future release runs should archive evidence against the source and artifact hashes.

## Required fixes, in priority order

### 1. Maintain a current engine and provide a secure update path

**Release blocker; confirmed version gap.** The repository lock and running browser both identify Chromium 149. Google published stable `152.0.7977.82/.83` for Mac and Windows on September 3, with security fixes. Exact applicability of individual vulnerabilities to Seoul was not audited; the version gap and absence of a demonstrated backport process are enough to block a broad release. [Google's release notice](https://chromereleases.googleblog.com/2026/09/stable-channel-update-for-desktop_01882797386.html).

Update the baseline and adapt the Seoul patch series on an isolated branch. Establish an owner and a response target for upstream security updates. Preserve a supported baseline, patch hashes, build provenance, and a documented emergency release procedure. A fork cannot treat the engine as a dependency pinned indefinitely while product features accumulate.

**Done when:** the release candidate uses a supported current baseline or documented applicable security backports; the full patch sequence and native integration tests pass on it; and an installed test client receives and verifies a subsequent signed update. A rollback strategy must preserve profile compatibility and avoid silently downgrading users to a vulnerable engine.

Evidence: [Chromium lock](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/chromium.lock.json#L1), observed browser version, and the package/update gaps already listed in [release gates](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/docs/release/seoul-product-readiness.md#public-release-gates).

### 2. Repair both model connection paths before describing AI as ready

**Core product blocker; determined by source tracing.** `ProviderRegistry::ConfigureCloud()` creates a `CloudModelConfig`, assigns its model and credential account, and constructs the provider. It never assigns `endpoint_url`. That configuration defaults to an empty string; `CloudModelProvider::Generate()` copies it into the HTTP request; the production transport rejects it as `invalid request URL`. A key plus an enabled switch can therefore make the registry report availability even though the request cannot be sent.

The local path has a separate contract mismatch. Studio suggests `http://127.0.0.1:11434/v1`. Health checking appends `/models`, but generation posts to the saved URL unchanged. With that setting, generation targets `/v1`, not `/v1/chat/completions`. Supplying the full generation endpoint instead makes the health check append `/models` to the wrong path. This is incompatible with the standard endpoints documented by [Ollama](https://docs.ollama.com/api/openai-compatibility).

Source: [cloud registry configuration](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/product/provider_registry.cc#L118), [cloud request URL](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/intelligence/cloud_model_provider.cc#L60), [transport rejection](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/product/browser/network_http_transport.cc#L135), [local request URL](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/intelligence/local_model_provider.cc#L56), and [Studio's example endpoint](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/canvas/resources/canvas.ts#L2419).

Define explicit provider adapters with a consistent base-URL contract, actual endpoints, authentication, response formats and supported models. Display the provider name, where to obtain credentials, the destination of user data, connection status and actionable errors. The present cloud form asks for a model ID and two keys without identifying the services an ordinary user should configure.

Model failure currently falls back to lexical deterministic planning. A simple tab command may consequently succeed while the model integration is broken. Make degraded operation visible and preserve the underlying provider error. A successful fallback is not proof of working AI.

**Done when:** setup from the visible UI produces a real local response and a real supported cloud response; changing a key or model is verified; offline, invalid key, timeout, rate limit, cancellation, stream interruption and malformed responses have understandable outcomes. Add transport-level request-contract tests that would fail for these exact URL mistakes. Fake responses alone cannot establish interoperability.

### 3. Align routing, privacy and spending claims with the runtime

**Product contract mismatch; source-confirmed.** The thesis describes deterministic-first, then local, then cloud reasoning. The product instead sets `use_model` whenever a provider is available, uses `prefer_local = !allow_cloud_models`, and ordinarily selects the cloud provider first when enabled. The standalone `RouteReasoning()` implementation has no production caller in the audited Seoul source. Model planning precedes deterministic fallback.

The cloud configuration also leaves pricing metadata at zero, and the plan callback discards generation usage when returning the plan. Execution budget machinery exists, but that does not demonstrate a real ceiling on the cost of planning, replanning and voice sessions.

Source: [StartGoal](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/product/browser/seoul_runtime_service.cc#L1755), [provider selection](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/product/provider_registry.cc#L234), [model-first planning](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/product/planner.cc#L287), and [model metadata defaults](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/intelligence/model_provider.h#L39).

Wire the intended policy through every production path or change the claims. Distinguish permission to use the cloud from a preference to use it. Preserve and account for provider usage; unknown prices must appear as unknown, not free. Include planning and replanning in budget decisions. Review realtime voice separately.

**Done when:** an end-to-end test records no network call for a supported deterministic task, no cloud call under local-only policy, correct fallback under failure, and a visible, enforced budget for actual paid calls.

### 4. Finish the first-use journey

**Partial implementation.** The welcome page exists, is built and renders at `chrome://seoul-welcome`. It should not be described as wholly unbuilt. However, the reviewed patch registers and packages that URL; it does not add a startup decision that opens it. Searches found no production caller of the onboarding decision outside the page handler's state calculation.

The welcome flow consists of welcome, rail appearance and blocking/default-browser choices. It does not take a user through migration, choosing a useful initial setup, AI service selection or a first successful task. Those omissions matter more than another welcome illustration.

Source: [first-run integration patch](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/patches/chromium/0027-seoul-first-run-webui.patch#L1), [onboarding state](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/onboarding/onboarding_state.cc#L41), and [welcome template](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/onboarding/resources/welcome.html.ts#L61).

Create a real first-launch entry with resumable and skippable setup. Offer browser import using Chromium's existing capabilities where appropriate; do not claim import itself is absent merely because it is missing from onboarding. Explain what transfers and what does not. Ensure regular browsing is immediately usable without AI credentials.

**Done when:** a person on a clean Mac profile can install, launch, understand the browser, bring over essential data, open a useful page and complete one advertised task without developer instructions. Relaunches and upgrades must not replay or lose onboarding progress.

### 5. Fix the Canvas layout and turn engineering output into product output

**Observed usability defect and confirmed copy problems.** At the inspected window size, Canvas's default screen did not show its message field. Activating the first starter command focused the field and scrolled the top of the interface away to reveal it. The primary action should not require that discovery.

There is a concrete layout conflict to investigate: the document stylesheet sets `seoul-canvas-app` to `display: block`, while its shadow host styling expects `display: grid` with a scrolling main area and a separate composer. The large minimum height of the idle screen further stresses smaller windows. This audit did not capture computed styles, so that CSS interaction is a source-supported diagnosis to verify, not a completed fix.

Source: [outer host style](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/canvas/resources/canvas.html#L29), [host layout](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/canvas/resources/canvas.css#L24), [idle minimum height](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/canvas/resources/canvas.css#L2375), and [composer](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/canvas/resources/canvas.html.ts#L148).

The local tab-inventory task worked and returned real data. Its visible result exposed an internal tab ID, raw column names, ordinal values and `true`/`false`. Studio similarly exposes language such as profile runtime, registry, typed graph and capability. These are useful concepts for implementation, but they add effort for everyday users.

**Done when:** the composer stays visible at every supported size; keyboard focus and scrolling remain predictable; task results use readable titles, selected states, clear actions and optional detail disclosure; empty states lead directly to a useful next step. Include narrow side panels and 200% zoom in validation. Test visibility and usability, not just whether a DOM element exists.

### 6. Choose one coherent interaction and design system

**Premium-quality requirement.** Several screens have a deliberate visual direction, particularly the warm Canvas and welcome surfaces. The product still switches between large editorial pages, engineering-oriented forms, compact native control panels and residual stock Chromium UI.

Observed residual strings included an infobar offering to open Chrome from the Dock and a restore prompt saying Chromium did not shut down correctly. The deliberate Brave Search attribution is a search-provider label and should not be treated as accidental Chrome branding.

Names also vary across surfaces: the native rail says Spaces while onboarding says workspaces; customization appears as both Boosts and Site Layers; Boards and Library overlap with other creation paths. Multiple entry points can be useful, but they must open the same state and explain the same concept.

Set shared rules for typography, contrast, spacing, control sizes, focus indicators, loading states, empty states, errors, confirmation and undo. Use one user-facing name per concept. Keep advanced controls in an advanced area. Replace repeated implementation explanations with direct task-oriented language.

**Done when:** every supported command produces consistent state, labels and feedback from its menu, shortcut, Canvas and native entry points; small windows and long names work; keyboard and screen-reader users can complete the same core journeys. Passing accessibility-tree name tests is only one part of this work.

### 7. Make tasks and receipts durable and recoverable

**Missing behavior relative to the product promise.** The production `TaskService` explicitly states that its deck is rebuilt each session and tasks are dropped on shutdown. The runtime persists surfaces, threads, workflows, providers, Library, themes, layers, scenes and presentation state, but not the task deck. A checkpoint API in the underlying execution library does not complete product-level restart recovery.

Source: [task lifetime contract](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/product/task_service.h#L7), [runtime persistence](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/product/browser/seoul_runtime_service.cc#L2739), and the still-unexecuted restart scenario in [the end-to-end plan](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/docs/quality/seoul-end-to-end-tests.md).

Persist supported task state and receipts, or explicitly restrict the first release's promise. Show users what completed, what failed, what requires input and what was interrupted. Never automatically replay a mutation whose result is unknown. Ensure completed results can be found again without remembering their originating tab.

**Done when:** pause, quit, crash, relaunch and profile migration preserve the promised task history; interrupted side effects surface as unknown until reconciled; users can resume safe work or discard it knowingly. Verify this through the browser UI and across process restarts.

### 8. Complete or clearly defer connected tools and local speech

**Foundations exist; complete consumer paths are missing.** Connector source provides a registry, schemas and capability importers. No production connection flow or concrete connected-account invocation path was found, and Studio has no connected-services management section. The runtime marks descriptors without executors unavailable. Accordingly, a catalog entry for web search or another service is not proof that the product can perform it.

Likewise, the native runtime passes `speech_to_text_` and `text_to_speech_` pointers that are never initialized in the audited production source. Canvas has a separate realtime WebRTC voice path, so it would be wrong to say there is no voice implementation. However, production voice success, local speech support and real connected-account tasks were not established.

Source: [connector build boundary](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/connectors/BUILD.gn#L1), [executor registration](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/product/browser/seoul_runtime_service.cc#L1458), [voice runtime composition](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/product/browser/seoul_runtime_service.cc#L241), and [Canvas voice entry](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/canvas/resources/canvas.ts#L936).

Pick the minimum complete service and voice experiences needed for the launch promise. Build account connection, scope display, disconnect, expiry, network failure and revocation. If those are outside the first release, remove promises and entry points that suggest they are ready. Local inference should have a supported setup, not require users to understand a loopback server.

**Done when:** one real account can be connected, used within its granted scope, disconnected and verified inaccessible afterwards; voice setup, permission refusal, noisy input, interruption and reconnect work with actual hardware. Record these separately from fake-provider test results.

### 9. Finish daily organization and honest recovery

**Incomplete visible organization; recovery needs a complete user path.** Tab custom titles and folder membership exist in the organization model and persistence. The audited projection/shell source and Chromium patches do not render that folder structure or those custom titles. If those are part of the launch experience, finish create, rename, move, collapse, delete, undo and restore consistently.

The recovery implementation preserves an original damaged snapshot, which is good. But “Acknowledge Recovery” clears recovery state and writes the current model; it is not a repair wizard or a restoration of the saved damaged snapshot. Users need to know what was recovered, what was not and how to retrieve or export remaining data.

Source: [organization records](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/organization/organization_types.h#L112), [load and recovery behavior](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/organization/seoul_organization_service.cc#L174), and [acknowledgment](https://github.com/hari-kancharla/Project-Seoul/blob/687c18d2fafb0e05293fe1cf96312fcd21aa2bf1/native/seoul/browser/organization/seoul_organization_service.cc#L241).

The older readiness report also describes a session-storage assertion whose root cause was not resolved and a parallel container-test crash. This audit's serial suite passed. Neither historical crash was reproduced or cleared here; keep them in an investigation queue with specific repro conditions, rather than calling them confirmed current failures or silently considering them fixed.

**Done when:** saved organization survives crash/restart and supported upgrades; recovery offers an understandable, recoverable decision; multi-window identity is tested; browser history, downloads, find, PDF, permissions, extension compatibility, account sign-in and password-manager behavior pass a daily-use matrix.

### 10. Validate privacy as a complete browser behavior

**Release gate and claim discipline.** Shields, blocking, canvas protection and related native tests are real work. They do not establish comprehensive fingerprint resistance or superiority to Brave. The source/report documents gaps involving shared/service workers, floating-point WebGL readback, pixel-pack buffers, audio, fonts, screen metrics and user-agent surfaces.

The existing competitive description of Brave's Strict fingerprinting mode is outdated: Brave announced its removal in 2024 because of compatibility and maintenance costs. That is a concrete warning against judging browser quality by the number of aggressive switches. [Brave's explanation](https://brave.com/privacy-updates/28-sunsetting-strict-fingerprinting-mode/).

Verify the default protection mode against a representative site set and privacy probes. Measure both tracking resistance and broken functionality. Show current protection and a straightforward per-site recovery path. Finish a usable profile-default settings surface. Review filter-list freshness, failed updates, fallback coverage and licensing decisions; the empty signing-key configuration for the component delivery path must be a documented delivery decision, not a hidden assumption.

Audit startup and background network endpoints, not only assistant requests. The isolated browser startup log contained Google registration errors, establishing that some inherited service behavior remains; it does not establish what private data, if any, was sent. Classify each endpoint and configure intentionally.

For agents, test hostile page content, malicious tool descriptions, cross-frame and cross-tab boundaries, sensitive forms, revoked permissions, stale targets and unknown outcomes. Typed schemas and approvals help but are not a full prompt-injection defense. Require HTTPS for remote provider/connector destinations, with loopback handling explicit and separate. Publish accurate data-use, retention, deletion and incident-reporting information before broad distribution.

**Done when:** claims match measured coverage; protected browsing does not routinely break supported sites; the data flow and security boundary have been reviewed independently; critical findings are closed and regressions retained.

### 11. Build and test the actual distributed artifact

**Release blocker.** The present app is a component development build with an ad-hoc signature. Source branding assets exist, so the task is completion and consistency, not starting branding from nothing.

Produce a non-component release build, final app identity and legal assets, Developer ID signing, notarization, package delivery and an update channel. Test the downloaded/quarantined artifact on another machine: initial install, offline launch where supported, upgrade, interrupted upgrade, incompatible profile handling, uninstall and recovery. Apple specifically documents distribution signing/notarization and testing away from the development machine. [Apple distribution guidance](https://developer.apple.com/documentation/xcode/packaging-mac-software-for-distribution).

**Done when:** a new user can install and launch the distributed build without terminal commands or protection bypasses, then update safely. Supported macOS versions and hardware are explicit. Windows, Linux, Intel Macs and mobile must not be implied as supported without corresponding artifacts and validation. Decide and verify codecs, DRM, sync and other inherited-browser dependencies rather than assuming Chrome's service arrangements transfer to a fork.

### 12. Replace narrative readiness claims with release evidence

**Confirmed process defect.** The current readiness document simultaneously mixes old verification dates, 145 versus 171 browser cases, 33 versus 34 unit binaries, an old 722-test total, statements that smoke stops at the first check, and later evidence that it passes. It says fingerprinting v2 is built in one place and unbuilt in another. It still describes all shell controls as invisible to macOS accessibility; this audit could read the current controls through the native accessibility tree.

The end-to-end and operator-evaluation documents still say no capable build host is available, despite a working build on this machine. The defined twelve scenarios and measured generalization, recovery, task cost and task-completion evaluations have not become a current release evidence set.

The checkout-free CI explicitly omits native builds and browser tests. Passing that workflow cannot certify native changes. Source-pattern architecture gates are useful checks, but cannot establish correct endpoint construction or usable layout.

**Done when:** one release manifest records exact source commit, dirty-state policy, Chromium revision, patch hashes, build args, artifact hash, test commands, passes/failures/skips, hardware and unresolved blockers. Add a native release gate on a capable runner. Use exact-snapshot reports, and move historical narratives into dated audit notes.

## What to preserve

Keep the native Chromium foundation and the owned-source/reversible-patch boundary. Keep the typed tool registry, schema validation, explicit permissions, observed mutation results and protection against replaying unknown side effects. Keep the genuine working organization and per-site controls. Preserve the existing visual direction where it helps readability and clarity.

The goal is to finish the integration and simplify the experience. Restarting the product from a prototype would discard substantial functioning work.

## A practical route to a credible launch

| Stage | Work | Exit condition |
| --- | --- | --- |
| A. Establish a reliable baseline | Isolate syntax-check temporary state; finish an exact-source build; create a current evidence manifest; start Chromium upgrade and update infrastructure | A reproducible candidate with known tests and blockers, no misleading “all green” claim |
| B. Complete the promised experience | Fix cloud/local endpoints and routing; wire onboarding; fix Canvas layout; implement durable task outcomes; choose which voice/service integrations are included | Three complete user journeys work from the visible UI, including failure and restart |
| C. Make daily use coherent | Unify names and controls; finish required organization; improve setup, empty states and recovery; test keyboard, VoiceOver, zoom, dark/light mode and real websites | New users can accomplish the supported work without coaching or losing data |
| D. Prove distribution and maintenance | Release build, signing, notarization, installer, upgrade and security-response verification; long-session/hardware testing | A downloaded artifact installs and updates on clean machines with no known critical blockers |
| E. Pilot and measure | Recruit an initial cohort, watch real usage, fix repeated friction and measure returning use and verified task completion | Evidence that people choose Seoul for recurring work and continue using it |

The three recommended demonstration and acceptance journeys are: ordinary browsing → organize → quit/reopen intact; selected sources → useful answer/comparison → cited saved result → reopen it; and a typed or spoken request → inspectable action → interruption/approval → verified outcome. These are proposed scope choices, not claims that all three are currently complete.

For broad launch, require zero known critical security/data-loss defects and zero known blockers in the supported journeys. Make safety-boundary tests deterministic and exhaustive over the supported actions. For model-driven work, maintain a held-out task set with measured completion, failure explanation, latency and cost; never convert a smoke pass into an “error-free” claim. Run extended browsing sessions and compare resource use with an unmodified current browser on the same machine and sites.

There is not enough evidence to responsibly promise a fixed number of days to public release. Engine rebase, signing, real-account acceptance and recovery findings determine that schedule. Estimate those workstreams after the first exact-source build and provider repairs, rather than attaching a deadline to the number of unchecked boxes.

## Positioning and YC

“For everyone” can remain the long-term ambition. The first release still needs a specific supported platform, a comprehensible promise and a group whose daily problem Seoul solves unusually well. That focus does not justify excluding accessibility or compromising browser safety.

Official competitor material already describes workspaces and containers in Zen, page assistance and context in Dia, and scoped assistant controls in Comet. These are vendor descriptions, not comparative benchmarks from this audit. They establish that a long feature list is insufficient differentiation. [Zen workspaces](https://docs.zen-browser.app/user-manual/workspaces), [Dia onboarding](https://www.diabrowser.com/getting-started), [Comet privacy controls](https://www.perplexity.ai/help-center/comet/en/articles/12867415-comet-assistant-privacy-data-use).

A credible Seoul promise to validate is: a calm everyday browser that turns a request into useful, inspectable work and keeps the result and recovery path available. Demonstrate that with real tasks and returning users. Useful pilot measures are first successful task, weekly returning users, repeated use of the core workflow, verified completion, recovery success and support burden. Do not claim premium positioning or market demand from the design alone.

YC's own advice emphasizes talking to users, focusing, launching and finding an initial group who love the product. It does not require beating every browser on every feature before learning from users. That supports a focused, safe pilot; it does not excuse distributing a stale engine or broken advertised functionality. [YC's Essential Startup Advice](https://www.ycombinator.com/blog/ycs-essential-startup-advice/).

The next implementation work should begin with the provider contracts and Canvas layout, while the engine/release work proceeds as a separate essential workstream. Follow those repairs with complete onboarding and durable task recovery. Additional feature expansion should follow measured demand from the pilot.

## Static-check addendum

The initial `npm run check` passed the first eleven gates and failed in `check:syntax` with `PA_BUILDFLAG_INTERNAL_MOVE_METADATA_OUT_OF_GIGACAGE_FOR_64_BITS_POINTERS already stubbed`. The four later checks were run separately and passed. A second full check with a fresh, audit-specific `TMPDIR` exited successfully: all 16 gates passed, including syntax parsing of 205 files, with 91 files skipped for generated dependencies.

The syntax script shares temporary stub and response files between invocations. The successful isolated rerun points to temporary-state interference rather than a reproduced source syntax defect, but does not establish the exact cause of the first failure. Give each invocation its own temporary directory and clean it up reliably. Preserve both runs in the audit evidence; do not describe parse-only checks with skipped files as a successful full build.
