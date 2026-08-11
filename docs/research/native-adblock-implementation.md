# Native ad blocker: M149 audit and staged implementation

Status: Stages 1–6 implemented and verified; Stage 7 code now includes
last-known-good storage, hash-pinned subscriptions, and Chromium component
registration. Release key/list/feed configuration and Stage 8 remain
unfinished.

This is the implementation ledger for Seoul's native blocker. It is deliberately
separate from the Zen-style browser UI work: the blocker will use Chromium's
browser, network, renderer, profile, preference, content-setting, component
update, and test infrastructure. It will not be an extension, a
`declarativeNetRequest` ruleset, or an Extension WebRequest implementation.

## Revision-locked baseline

Seoul is pinned by `native/chromium.lock.json` to:

- Chromium `149.0.7827.201`
- milestone `149`
- revision `6a7b3dbec3b2ca25877c2553b5473b2f277ef644`
- official stable macOS release

The exact Brave reference with the same Chromium version is:

- Brave Core tag `v1.91.180`
- Brave Core commit `5360f32b80aaae415d2b6c28bf84ef494248d36a`
- `package.json` Chromium tag `149.0.7827.201`

The reference implementation is audited from `brave/brave-core`, not only the
small `brave/brave-browser` release repository. Later Brave master code targets
Chromium 151 and is not an integration reference for this fork.

The M149-matched Brave tag depends on the MPL-2.0 `adblock` Rust crate `0.12.0`
through a CXX wrapper. Seoul pins crate archive SHA-256
`c05e128b668b937fc733b04f8832dde112dbef39a23a5fe2d6653045eff301a6`
and `.cargo_vcs_info.json` revision
`29cf12d01b1a840eb860867c7c16b55de58a1eb8`. It does not substitute Brave
master's newer 0.13 API.

Primary references:

- <https://github.com/brave/brave-core/tree/v1.91.180>
- <https://github.com/brave/brave-core/tree/v1.91.180/components/brave_shields>
- <https://github.com/brave/brave-core/tree/v1.91.180/browser/net>
- <https://github.com/brave/adblock-rust>

## Repository audit

### 1. Browser-process request interception

Seoul does not currently contain an ad blocker or any Seoul-owned request
interceptor.

Chromium M149 exposes three relevant browser embedder seams:

1. `ChromeContentBrowserClient::WillCreateURLLoaderFactory()` in
   `chrome/browser/chrome_content_browser_client.cc`. It receives a
   `network::URLLoaderFactoryBuilder` on the UI thread. Chromium already appends
   proxy factories for Extension WebRequest, sign-in, macOS enterprise auth,
   guest view, and bound-network routing.
2. `ChromeContentBrowserClient::CreateURLLoaderThrottles()` in the same file.
   This is used for browser-created loaders and currently adds Safe Browsing,
   captive portal, request-header integrity, no-state-prefetch, Google,
   protocol-handler, plug-in, and sign-in throttles.
3. `ChromeContentBrowserClient::CreateThrottlesForNavigation()`, which delegates
   to Chromium's `NavigationThrottleRegistry`.

`CreateURLLoaderThrottles()` is not sufficient for ordinary renderer
subresources. `ContentBrowserClient` explicitly routes renderer subresource
throttles through `ContentRendererClient::CreateURLLoaderThrottleProvider()`.
Putting the native engine there would either miss browser/worker paths or
require renderer-side engine ownership, both of which violate the requested
browser-process architecture.

### 2. Selected interception design

The primary Seoul hook will be a browser-process URLLoaderFactory proxy appended
from `ChromeContentBrowserClient::WillCreateURLLoaderFactory()` before calling
the existing Chromium implementation. This is the same class of interception
used by the M149-matched Brave code:

- `browser/net/brave_proxying_url_loader_factory.*`
- `browser/net/resource_context_data.*`
- `browser/net/brave_request_handler.*`
- `browser/net/brave_ad_block_tp_network_delegate_helper.*`

The Seoul implementation will be smaller and adblock-specific rather than
copying Brave's general multi-callback request handler. It will preserve the
factory chain, redirect restart behavior, URLLoader/URLLoaderClient lifetime,
`ERR_IO_PENDING` ordering, and `ERR_BLOCKED_BY_CLIENT` completion semantics.

Factory types in this Chromium revision are:

- navigation
- download
- document subresource
- worker main resource
- worker subresource
- service-worker script
- service-worker subresource
- prefetch
- DevTools
- early hints

Initial policy:

- Intercept document subresources, worker resources, service-worker resources,
  and WebSockets.
- Do not filter DevTools, component update, browser update, extension update,
  Safe Browsing, or Seoul filter-update traffic.
- Do not filter downloads, prefetch, or early hints until dedicated behavior
  and tests exist.
- Skip `chrome`, `chrome-untrusted`, `devtools`, `file`, `data`, `blob`,
  extension, and other non-network schemes.
- Evaluate only HTTP, HTTPS, WS, and WSS.
- Skip ordinary main-frame requests in the URLLoaderFactory proxy so the
  navigation path is not evaluated twice.

Top-level HTTP(S) navigation decisions will use a separate asynchronous
`AdBlockNavigationThrottle` registered from
`ChromeContentBrowserClient::CreateThrottlesForNavigation()`. It will defer,
query the profile service, then resume or cancel with
`net::ERR_BLOCKED_BY_CLIENT`, preserving data for a future interstitial and
explicit proceed override.

WebSocket interception needs its own `ContentBrowserClient::CreateWebSocket()`
proxy because WebSocket creation does not pass through a normal
`network::mojom::URLLoaderFactory`.

### 3. Rust and CXX pattern

Chromium M149 already supports Rust in production targets:

- `build/rust/rust_static_library.gni`
- `build/rust/cargo_crate.gni`
- `third_party/rust/cxx/chromium_integration/rust_cxx.gni`
- `//build/rust:cxx_cppdeps`

Existing examples include:

- `third_party/rust/jxl/v0_4/wrapper/BUILD.gn`
- `third_party/rust/jxl/v0_4/wrapper/lib.rs`
- `components/user_data_importer/utility/parsing_ffi/lib.rs`

The correct Seoul pattern is a `rust_static_library` with
`cxx_bindings = [ "src/lib.rs" ]`, a narrow `#[cxx::bridge]` interface, and
generated C++ headers included from the GN output. Imported Cargo dependencies
must use Chromium's `gnrt`-generated `cargo_crate` targets. Missing transitive
crates must be vendored under the Seoul overlay with their license metadata;
they must not be downloaded at browser build or runtime.

The engine object is single-sequence:

- `AdBlockEngineHost` owns the active matcher on a dedicated sequenced task
  runner.
- Requests are posted asynchronously and replies are posted to the caller's
  sequence.
- A separate build sequence parses lists and constructs replacement engines.
- A replacement is moved to the matcher sequence and atomically swapped only
  after complete construction and sanity checks.
- The old engine remains active after any parse, validation, construction, or
  cache-deserialization failure.
- No UI-thread method directly calls Rust.

### 4. Profile-keyed service pattern

Seoul already has the exact required factory pattern:

- `native/seoul/browser/organization/seoul_organization_service_factory.*`
- `ProfileKeyedServiceFactory`
- `ProfileSelections::Builder()`
- `RegisterProfilePrefs()`
- `ServiceIsCreatedWithBrowserContext()`
- deterministic `Shutdown()`

`AdBlockServiceFactory` will follow this pattern. Unlike Brave's M149
browser-global engine coordinator, Seoul's `AdBlockService` will be
profile-aware. Regular profiles receive independent preferences, custom lists,
cached engines, stats, and update state. The initial policy excludes guest,
system, and off-the-record profiles. Incognito inheritance can be added only
after its data-separation policy is specified and tested.

`AdBlockService` owns:

- `AdBlockEngineHost`
- `FilterListManager`
- live `AdBlockRequestInterceptor` proxy instances for its BrowserContext
- per-profile mode/settings access
- `AdBlockStatsService`
- minimal UI-facing methods

Shutdown invalidates weak pointers, stops accepting new checks, lets pending
callbacks fail open, destroys proxy instances, then destroys engine/build
sequences.

### 5. Mojo renderer-agent pattern

Chromium creates renderer agents in
`ChromeContentRendererClient::RenderFrameCreated()`. Existing render-frame
agents use `content::RenderFrameObserver`, register renderer-associated
interfaces through `GetAssociatedInterfaceRegistry()`, and acquire browser
interfaces through `RenderFrame::GetBrowserInterfaceBroker()`.

Browser-side frame binders are registered through:

- `ChromeContentBrowserClient::RegisterBrowserInterfaceBindersForFrame()`
- `chrome/browser/chrome_content_browser_client_receiver_bindings.cc`
- `chrome::internal::PopulateChromeFrameBinders()`
- `chrome/browser/chrome_browser_interface_binders.cc`

Stage 5 will define an asynchronous Seoul Mojo interface and create one
`CosmeticFilterAgent` for each eligible render frame. The design is informed by
Brave's `components/cosmetic_filters`, but Seoul will not copy Brave M149's
synchronous `UrlCosmeticResources` call.

The agent will:

- operate only on eligible HTTP(S) documents;
- use a fixed Seoul-owned Blink isolated-world ID and security origin;
- request typed URL cosmetic resources asynchronously;
- add user-origin stylesheets rather than page-authored styles;
- batch and deduplicate dynamic class/ID queries;
- cap pending identifiers, selectors, stylesheet bytes, and update frequency;
- invalidate callbacks on document replacement and renderer teardown;
- handle subframes, cross-origin frames, BFCache restore, and prerender
  activation explicitly;
- never execute arbitrary JavaScript supplied by a remote list.

### 6. Preferences and per-site settings

Profile preferences are registered by the profile-keyed factory, following
`SeoulOrganizationServiceFactory::RegisterProfilePrefs()`. Stage 4 implements
the default mode preference. Later stages still need preferences for:

- custom filters;
- enabled optional list identifiers;
- subscription metadata;
- last-known-good engine/cache version;
- update timestamps and diagnostic error codes;
- filter-settings and update-status UI routing.

Per-site state uses `HostContentSettingsMap`, not an unscoped hostname map.
Stage 4 adds two dedicated top-origin-only website-setting types:

- `SEOUL_AD_BLOCK_MODE` stores a dictionary-backed persistent mode override,
  is profile-local/unsyncable, and is not inherited into incognito;
- `SEOUL_AD_BLOCK_TEMPORARY_ALLOW` stores a separate dictionary-backed
  expiring disable marker, is unsyncable, and is not inherited into incognito.

Keeping expiration in a separate setting prevents it from deleting the
persistent site mode. The primary pattern is the top-level origin; matching and
first-party calculation use Chromium's registry-controlled-domain APIs with
`INCLUDE_PRIVATE_REGISTRIES`.

### 7. Component updates and tests

Chromium component installers live in:

- `chrome/browser/component_updater/`
- `components/component_updater/`
- registration entry point
  `chrome/browser/component_updater/registration.cc`

Stage 7 uses `ComponentInstallerPolicy` for browser-vetted signed production
packages. Registration is fail-closed until a Seoul release public-key hash is
supplied at build time; the external private key, licensed production lists,
and compatible update feed still require project-owner release configuration.
Direct custom subscriptions use a profile-owned HTTPS downloader with explicit
redirect, status, MIME, decoded-size, timeout, and pinned-hash policy.

Browser tests are already integrated through Seoul source sets into
`//chrome/test:browser_tests`; examples are:

- `native/seoul/browser/shell/shell_browsertest.cc`
- `native/seoul/browser/organization/seoul_organization_service_browsertest.cc`
- `native/seoul/browser/product/browser/seoul_runtime_browsertest.cc`

Adblock test placement:

- Rust wrapper tests: `native/seoul/browser/adblock/rs/`
- C++ engine/service tests: `native/seoul/browser/adblock/*_unittest.cc`
- deterministic browser tests:
  `native/seoul/browser/adblock/ad_block_browsertest.cc`
- local fixtures:
  `native/seoul/browser/adblock/test/data/`

No test may depend on a live ad domain or remote filter service.

## Logical components and source layout

Planned repository-owned source:

| Component | Planned path | Responsibility |
| --- | --- | --- |
| `AdBlockService` | `native/seoul/browser/adblock/ad_block_service.*` | Profile coordinator and minimal UI API |
| `AdBlockServiceFactory` | `native/seoul/browser/adblock/ad_block_service_factory.*` | Profile selection, prefs, lifetime |
| `AdBlockEngineHost` | `native/seoul/browser/adblock/ad_block_engine_host.*` | Async single-sequence Rust ownership and replacement |
| `FilterListManager` | `native/seoul/browser/adblock/filter_list_manager.*` | Bundled/custom/subscription sources and LKG state |
| `AdBlockRequestInterceptor` | `native/seoul/browser/adblock/ad_block_request_interceptor.*` | URLLoaderFactory proxy and request lifetime |
| `AdBlockWebSocketInterceptor` | `native/seoul/browser/adblock/ad_block_websocket_interceptor.*` | Native WS/WSS checks |
| `AdBlockNavigationThrottle` | `native/seoul/browser/adblock/ad_block_navigation_throttle.*` | Deferred top-level navigation decision |
| `CosmeticFilterHost` | `native/seoul/browser/adblock/cosmetic_filter_host.*` | Browser-side async Mojo |
| `CosmeticFilterAgent` | `native/seoul/browser/adblock/renderer/cosmetic_filter_agent.*` | Isolated-world CSS and dynamic selectors |
| `AdBlockStatsService` | `native/seoul/browser/adblock/ad_block_stats_service.*` | Per-tab counts and bounded debug events |
| Rust bridge | `native/seoul/browser/adblock/rs/` | CXX-safe adblock-rust API |
| Vendored crates | `native/third_party/rust/seoul_adblock/` | Pinned, licensed offline Rust inputs |

The unavoidable Chromium integration patch will be limited to:

- browser/renderer target dependency edges;
- profile factory registration;
- `WillCreateURLLoaderFactory()`;
- `CreateWebSocket()`;
- `CreateThrottlesForNavigation()`;
- frame Mojo binder registration;
- renderer-agent creation;
- component registration;
- content-setting enum/registry/UMA additions;
- browser-test source-set dependency.

## Request and decision model

`AdBlockRequest` carries:

- request URL;
- initiator origin and URL when available;
- outermost top-frame origin/URL from trusted isolation information or frame
  state;
- Blink resource type;
- mapped adblock-rust request type;
- HTTP method;
- computed first-party/third-party state;
- effective per-site mode;
- URLLoaderFactory type;
- render-frame token when present;
- DevTools request identifier when present;
- service-worker origin flag.

First-party calculation uses
`net::registry_controlled_domains::SameDomainOrHost()` or
`GetDomainAndRegistry()` with `INCLUDE_PRIVATE_REGISTRIES`. It never uses suffix
matching.

Required resource mapping:

| Blink resource | Engine type |
| --- | --- |
| main frame | `main_frame` |
| subframe | `sub_frame` |
| script | `script` |
| stylesheet | `stylesheet` |
| favicon/image | `image` |
| font | `font` |
| media | `media` |
| XHR/fetch | `xmlhttprequest`/engine-compatible `xhr` |
| WebSocket | `websocket` |
| ping/beacon | `ping` |
| object/embed | `object` |
| worker/service-worker/prefetch/CSP/plugin/unknown | explicit reviewed mapping, otherwise `other` |

The C++ result is a structured `AdBlockDecision`:

- action: allow, block, redirect, or rewrite;
- normal match;
- exception match;
- important match;
- deciding engine: none, default, or additional;
- rule category;
- optional vetted resource name;
- optional rewritten URL;
- bounded debug metadata.

Merge policy:

1. Off returns allow without engine work.
2. Evaluate the default engine.
3. In Standard mode, retain default exceptions and important matches but ignore
   ordinary default first-party blocks.
4. In Aggressive mode, apply ordinary default matches to first and third party.
5. Evaluate the additional engine with aggressive semantics, carrying prior
   normal/exception/important state.
6. Important wins over a normal exception; otherwise a match blocks only when
   no applicable exception exists.
7. Default-engine remove-parameter rewrites remain disabled until Stage 6.
8. Rewrites are valid only for GET, HEAD, or OPTIONS and only to a valid
   HTTP(S) URL after a second policy check.
9. Redirects resolve only through the bundled vetted-resource catalog.

## Runtime request flow

1. Chromium asks `ChromeContentBrowserClient` to create a network
   URLLoaderFactory.
2. The Seoul integration hook gets the eligible profile's `AdBlockService`.
3. `AdBlockRequestInterceptor` appends a proxy to the existing factory builder.
4. On `CreateLoaderAndStart`, the proxy builds a bounded `AdBlockRequest`,
   rejects excluded schemes/factory types, and pauses the request.
5. `AdBlockService` posts the decision to `AdBlockEngineHost`.
6. The engine host evaluates default and additional engines on its dedicated
   sequence.
7. The reply returns to the proxy's owning sequence through a weak/lifetime-safe
   callback.
8. Allow forwards to the next factory; block completes with
   `ERR_BLOCKED_BY_CLIENT`; later stages may issue a vetted redirect or safe URL
   rewrite.
9. A live frame/tab decision is reported to `AdBlockStatsService`; a closed tab
   simply drops the bounded UI event without affecting request completion.

Main-frame HTTP(S) flow starts at `AdBlockNavigationThrottle` instead of step 3.
WebSocket flow starts at `AdBlockWebSocketInterceptor`.

## Filter-list and last-known-good policy

Stages 2–6 use only small deterministic Seoul-owned test lists. Production list
licensing and selection remain a project-owner decision.

Stage 7 lifecycle:

1. Start with the bundled engine or a version-compatible validated cache.
2. Never wait for network access during browser startup.
3. Download through HTTPS with bounded redirects and byte/time limits.
4. Verify status, manifest schema, version, file hashes, and component
   signature/pinned trust.
5. Decompress with a hard output cap.
6. Parse all applicable sources into new default/additional `FilterSet`s on the
   build sequence.
7. Construct replacement engines and run deterministic sanity probes.
8. Post a completed replacement to the matcher sequence and swap atomically.
9. Persist cache and status only after activation.
10. Preserve the old engine and record a bounded diagnostic on every failure.

The blocker's own component and subscription downloader requests are marked with
an explicit trusted bypass, not a domain-name allowlist.

## Staged delivery and verification

### Stage 1 — audit and design

Completed:

- locked Chromium and matching Brave revisions;
- selected M149 browser-process interception seams;
- selected CXX/Rust, profile-service, Mojo, preference, component, and test
  patterns;
- documented request flow, exclusions, threading, modes, and stage gates.

Binary build: not applicable; Stage 1 changes documentation only.

Changed file:

- `docs/research/native-adblock-implementation.md`

### Stage 2 — Rust engine

Completed:

- pinned and vendored `adblock` `0.12.0` plus its complete 39-package offline
  dependency closure, generated with Chromium M149's `gnrt`;
- recorded the exact Brave/Chromium revisions, crate archive checksums, feature
  set, licenses, and the Brave-compatible `rmp` `0.8.11` compatibility pin;
- kept Chromium's global Rust workspace unchanged by materializing the closure
  under `//third_party/rust/seoul_adblock`;
- isolated Seoul-only `bitflags` serde and full `regex-automata` features from
  Chromium's narrower global crate targets;
- added a narrow CXX boundary that builds one optimized engine from one complete
  `FilterSet`, evaluates preparsed request metadata, exposes block, exception,
  important, matched-rule, exception-rule, redirect, and rewrite results, and
  serializes/deserializes the engine cache;
- rejected invalid UTF-8 filter input and safely allowed invalid UTF-8 request
  metadata instead of panicking across FFI;
- added a sequence-checked C++ owner and deterministic standalone tests for
  blocking, resource/domain constraints, third-party policy input, exceptions,
  important rules, `badfilter`, serialization, malformed rules, and invalid
  UTF-8.

Verification:

- built `//seoul/browser/adblock:seoul_adblock_engine_unittests` against the
  pinned Chromium M149 checkout;
- all 10 standalone native tests passed;
- restored `out/SeoulBaseline/args.gn` to its normal arguments after the
  isolated target build, with no `root_extra_deps`;
- did not launch, focus, navigate, or otherwise interact with the user's open
  browser.

Stage 2 request flow:

1. C++ copies the complete UTF-8 rules bytes across the CXX boundary.
2. Rust adds the list to a debug-enabled `FilterSet` and constructs one
   optimized `adblock::Engine`.
3. C++ supplies already-parsed request URL, request/source hostnames, resource
   type, and third-party state on the engine's owning sequence.
4. Rust returns a structured match result, including rule metadata and
   redirect/rewrite candidates; Stage 2 does not act on those candidates.
5. C++ returns the decision to the caller or serializes the engine for a future
   versioned cache.

Stage 2 project-owned files:

- `native/seoul/browser/adblock/BUILD.gn`
- `native/seoul/browser/adblock/ad_block_engine.h`
- `native/seoul/browser/adblock/ad_block_engine.cc`
- `native/seoul/browser/adblock/ad_block_engine_unittest.cc`
- `native/seoul/browser/adblock/rs/BUILD.gn`
- `native/seoul/browser/adblock/rs/src/lib.rs`
- `native/third_party/rust/seoul_adblock/README.md`
- `native/third_party/rust/seoul_adblock/provenance.json`
- `native/third_party/rust/seoul_adblock/crates/` (39 generated GN crate
  targets and license records)
- `native/third_party/rust/seoul_adblock/vendor/` (39 exact Cargo packages;
  1,181 overlay files in total at verification time)
- `native/scripts/check-adblock-rust-vendor.mjs`
- `native/scripts/common.sh`
- `native/scripts/materialize.sh`
- `native/seoul/browser/BUILD.gn`
- `native/README.md`
- `native/seoul/README.md`

Cosmetic-resource bindings remain deliberately deferred to Stage 5, where
their bounded asynchronous Mojo contract and renderer lifetime behavior can be
implemented and tested together.

### Stage 3 — subresource blocking

Completed:

- added a regular-profile-only `AdBlockServiceFactory`, eagerly registered with
  Chromium's profile dependency manager; guest, system, Ash-internal, and
  off-the-record profiles do not receive a service;
- added an asynchronous, sequence-owned `AdBlockEngineHost`; valid replacement
  engines swap atomically and a failed replacement retains the last good
  engine;
- added a browser-trusted request model with complete reviewed Blink
  destination mapping, registry-controlled-domain party calculation including
  private registries, factory provenance, frame token, service-worker marker,
  initiator/top-frame context, method, and DevTools request identifier;
- added a browser-process `URLLoaderFactory` proxy for document, worker, and
  service-worker resource factories, including request cloning, priority
  forwarding, client/loader teardown, fail-open service shutdown, and
  `ERR_BLOCKED_BY_CLIENT` completion;
- re-evaluated both server redirects and caller-supplied `FollowRedirect` URL
  overrides before allowing the redirected request to continue;
- added a self-owned asynchronous WebSocket check that either returns the
  original handshake client to Chromium's existing extension/network path or
  fails the handshake with `ERR_BLOCKED_BY_CLIENT`;
- added a primary-main-frame navigation throttle that checks both initial and
  redirected HTTP(S) destinations, handles synchronous and deferred engine
  replies, and retains one bounded blocked-navigation URL/decision for a future
  interstitial or explicit proceed flow;
- added bounded aggregate/per-frame blocked counts;
- registered the profile factory and the URLLoader, WebSocket, and navigation
  hooks through patch
  `native/patches/chromium/0020-seoul-native-adblock-network-hook.patch`;
- added deterministic engine, service, URLLoader, WebSocket, navigation, and
  embedded-server browser coverage with no dependency on a public website or
  remote filter service.

Verification:

- built `//seoul/browser/adblock:seoul_adblock_engine_unittests`,
  `//seoul/browser/adblock:seoul_adblock_core_unittests`,
  `//seoul/browser/adblock:seoul_adblock_interceptor_unittests`,
  `//seoul/browser/adblock:adblock_browser_tests`,
  `//seoul/browser/product/browser:seoul_browser_tests`, and
  `//chrome/browser:browser` against the pinned Chromium M149 checkout;
- all 10 Rust-engine tests, 10 core/service tests, and 14 request-path tests
  passed;
- compiled and linked the focused browser-test binary containing local
  embedded-server tests for blocked script, WebSocket, and top-level navigation
  traffic, but did not execute that binary because doing so would launch a
  browser and violate the user's no-interruption directive;
- verified the Seoul source mirror, the 39-package Rust vendor closure, and the
  20-entry Chromium patch manifest;
- did not launch, focus, navigate, capture, or otherwise interact with the
  user's open browser.

Stage 3 request behavior:

1. An eligible network, WebSocket, or primary-main-frame navigation request is
   converted into bounded browser-trusted metadata.
2. The profile service evaluates the active engine off the UI sequence.
3. Allow continues through Chromium's existing factory/extension/navigation
   chain without replacing it.
4. Block terminates the request or handshake with
   `net::ERR_BLOCKED_BY_CLIENT`; navigation cancellation records only the most
   recent blocked URL and decision.
5. Redirect targets are checked again before continuation.
6. Service teardown and closed clients invalidate callbacks safely and fail
   open without dereferencing destroyed state.

Stage 3 project-owned files:

- `native/seoul/browser/adblock/ad_block_decision.*`
- `native/seoul/browser/adblock/ad_block_engine_host.*`
- `native/seoul/browser/adblock/ad_block_request.*`
- `native/seoul/browser/adblock/ad_block_request_interceptor.*`
- `native/seoul/browser/adblock/ad_block_websocket_interceptor.*`
- `native/seoul/browser/adblock/ad_block_navigation_throttle.*`
- `native/seoul/browser/adblock/ad_block_service.*`
- `native/seoul/browser/adblock/ad_block_service_factory.*`
- `native/seoul/browser/adblock/ad_block_stats_service.*`
- `native/seoul/browser/adblock/ad_block_core_unittest.cc`
- `native/seoul/browser/adblock/ad_block_request_interceptor_unittest.cc`
- `native/seoul/browser/adblock/ad_block_websocket_interceptor_unittest.cc`
- `native/seoul/browser/adblock/ad_block_navigation_throttle_unittest.cc`
- `native/seoul/browser/adblock/ad_block_browsertest.cc`
- `native/patches/chromium/0020-seoul-native-adblock-network-hook.patch`

Known Stage 3 boundary:

- no production filter source is installed yet, so the production engine starts
  empty; deterministic rules are injected only by tests until Stage 7 selects
  licensed lists and implements vetted updates;
- mode values exist in the request schema, but Standard/Aggressive semantics,
  per-site settings, temporary disable, and the second engine belong to Stage
  4;
- frame-associated WebSockets are covered by Chromium's M149 embedder hook;
  frame-less worker WebSockets do not expose a safe profile context at that
  seam and remain an explicit compatibility item rather than being attached to
  an incorrect global profile;
- Stage 3 intentionally does not act on redirect or rewrite candidates returned
  by the Rust engine; those require Stage 6's vetted resource and method policy.

### Stage 4 — modes and two engines

Completed:

- added profile-default Off, Standard, and Aggressive modes, with Standard as
  the fail-safe default for invalid or absent preference values;
- added independent persistent per-site mode and expiring temporary-disable
  storage through Chromium's `HostContentSettingsMap`;
- rejected non-HTTP(S) site overrides and bounded temporary disables to 30
  days;
- made the public service fail open for unsupported/internal request schemes
  even if a future caller bypasses the existing interceptor-level guards, and
  report Off as the effective UI mode on those pages;
- added two dedicated top-origin-only Chromium website-setting types, stable
  registry names, exhaustive settings-WebUI entries, and stable UMA values;
- kept persistent site modes and temporary disables profile-local and
  unsyncable, and prevented both types from inheriting into incognito;
- extended the Rust bridge to use adblock-rust's subset evaluation API so the
  additional engine receives prior normal-match and exception state;
- added independent default and additional engines with atomic per-group
  replacement and last-known-good retention after invalid input;
- implemented the M149-matched Brave merge order: Off short-circuits, Standard
  suppresses ordinary default first-party matches while retaining default
  exceptions and important matches, Aggressive applies default first-party
  matches, and the additional engine always evaluates with aggressive
  semantics;
- made the final service decision exception-aware across both engines: an
  important rule blocks, otherwise a normal match blocks only when no
  applicable exception remains;
- exposed backend methods for current effective site mode, changing the
  default or site mode, temporarily disabling blocking, clearing the temporary
  disable, and reading the per-tab blocked count already owned by the stats
  service;
- added profile-backed browser coverage for Standard first-party allow,
  Aggressive first-party block, temporary disable, and restoration after clear;
- kept default-engine remove-parameter rewrites disabled pending Stage 6's
  method and rewritten-URL policy.

Verification:

- built `//seoul/browser/adblock:seoul_adblock_engine_unittests`,
  `//seoul/browser/adblock:seoul_adblock_core_unittests`,
  `//seoul/browser/adblock:seoul_adblock_interceptor_unittests`,
  `//seoul/browser/product/browser:seoul_browser_tests`, and
  `//chrome/browser:browser` against the pinned Chromium M149 checkout;
- all 10 engine tests, 20 request/service/settings tests, and 14 interception
  tests passed;
- compiled and linked the focused browser-test binary with the new
  profile-settings integration case, but did not execute it because that would
  launch a browser;
- passed `npm run check`, 8 protocol tests, 31 non-GUI Canvas tests, and 4
  Boost-editor tests;
- verified the normal release-component GN arguments were restored and no
  `root_extra_deps` remained;
- validated the 21-entry patch manifest and checksum;
- reversed patch `0021` in an isolated five-file repository, byte-compared all
  files to their pre-change sources, reapplied it, and byte-compared all files
  to the built checkout;
- did not launch, focus, navigate, capture, or otherwise interact with the
  user's open browser.

Stage 4 request behavior:

1. The profile service derives the site key from the outermost top-frame URL,
   then the initiator, then the request URL.
2. A live temporary-disable marker returns Off without posting engine work.
3. Otherwise a persistent site override wins over the profile-default mode.
4. The default engine evaluates first. Standard discards only its ordinary
   first-party blocking result; exceptions and important results survive.
5. Unless an important default result already decides the request, the
   additional engine evaluates with prior match/exception state.
6. The merged decision blocks for important or for match-without-exception,
   then records bounded per-frame/profile statistics through the existing
   Stage 3 path.

Stage 4 project-owned files:

- `native/seoul/browser/adblock/BUILD.gn`
- `native/seoul/browser/adblock/ad_block_engine.*`
- `native/seoul/browser/adblock/ad_block_engine_host.*`
- `native/seoul/browser/adblock/ad_block_service.*`
- `native/seoul/browser/adblock/ad_block_service_factory.*`
- `native/seoul/browser/adblock/ad_block_settings.*`
- `native/seoul/browser/adblock/ad_block_settings_unittest.cc`
- `native/seoul/browser/adblock/ad_block_core_unittest.cc`
- `native/seoul/browser/adblock/ad_block_browsertest.cc`
- `native/seoul/browser/adblock/rs/src/lib.rs`
- `native/patches/chromium/0021-seoul-adblock-site-settings.patch`
- `native/patches/manifest.json`
- `native/patches/README.md`
- `native/patches/chromium/README.md`
- `docs/research/native-adblock-implementation.md`

Known Stage 4 boundary:

- the production engines still start empty because licensed production list
  selection, custom-list persistence, component updates, subscriptions, cache
  versioning, and update diagnostics belong to Stage 7;
- current-site mode, mutation, temporary-disable, and count backends exist, but
  filter-settings routing and last-update status require the Stage 7 manager;
- cosmetic resources, vetted redirects, safe remove-parameter rewrites,
  scriptlets, procedural filtering, CSP, and CNAME handling remain gated behind
  Stages 5–8.

### Stage 5 — cosmetic selectors

Implemented, built, and exercised in isolated headless browser processes.

Completed:

- extended the pinned Rust bridge with typed URL-specific cosmetic resources
  and generic class/id selector lookup while continuing to discard injected
  scripts and procedural/action filters;
- supplied adblock-rust's required external domain resolver from Chromium's
  registry-controlled-domain implementation, including private registries,
  instead of enabling a second public-suffix implementation;
- kept default and additional cosmetic outputs separate so additional-list
  selectors remain force-hide rules, stripped default `:has()` selectors in
  Standard mode, and retained them for Aggressive/default and additional rules;
- matched Brave M149's combined cosmetic exception semantics: exceptions from
  either engine suppress generic discovery in both engines, and a
  `$generichide` result from either engine disables all generic class/id
  discovery for that document;
- added a document-scoped asynchronous Mojo host that derives the committed
  document URL and profile from its bound `RenderFrameHost`; the renderer
  cannot choose a URL, mode, profile, exception list, or engine;
- used `WeakDocumentPtr` and weak callbacks so navigation, teardown, or service
  shutdown cannot deliver old selector results into a replacement document;
- added browser-side identifier and selector validation with 256 identifiers
  per kind and hard selector count, length, aggregate-byte, NUL, at-rule, and
  declaration-injection limits;
- allocated a synchronized Seoul isolated-world id in Chromium's C++ and
  Android registries, with a fixed `chrome://seoul-cosmetic-filters` security
  origin and an empty isolated-world CSP;
- created one renderer agent for every `RenderFrame`, including cross-origin
  subframes, and started the async resource request at new-document creation
  with a document-element fallback to reduce first-paint delay;
- applied only browser-returned selectors as Blink user-origin stylesheets;
  scriptlets, remote JavaScript, procedural actions, arbitrary declarations,
  and page-world execution are absent;
- installed a fixed Seoul-authored isolated-world `MutationObserver`, scanned
  the existing document with a bounded tree walker, drained at most 256
  classes and 256 ids per 250 ms batch, and limited each renderer stylesheet
  to 4,096 selectors and 512 KiB;
- deferred all browser IPC during prerender, invalidated late callbacks by
  document generation, suspended timers in BFCache, and revalidated resources
  after BFCache restore without clearing the retained sheet before the new
  response arrives;
- added deterministic engine/service tests for URL-specific selectors, generic
  lookup, exceptions, `$generichide`, Standard/Aggressive, Off, and two-engine
  behavior;
- added renderer-harness coverage for initial selectors, dynamic class/id
  discovery, isolated-world invisibility, declaration-injection rejection, and
  Off mode;
- added compile-verified browser cases for domain-specific and dynamic
  cosmetics, cross-origin subframes, and history/BFCache restore.

Verification:

- all engine, request/service/settings/engine-host, renderer, and focused
  browser tests passed;
- executed the renderer lifecycle and cosmetic browser cases in isolated
  headless processes with `--use-mock-keychain`;
- built `//chrome/browser:browser` with the browser host, Mojo bindings,
  renderer agent, fixed isolated-world id, and Rust bridge linked;
- passed `npm run check`, including the 39-package vendored Rust closure,
  repository boundaries, architecture gates, Canvas TypeScript, syntax checks,
  and protocol consistency;
- validated the 22-entry manifest and patch checksum;
- reversed and reapplied patch `0022` in an isolated six-file repository and
  byte-compared the result to the built checkout;
- reversed the complete 22-patch Chromium series, completed a clean cumulative
  apply/reverse verification, and reapplied the series;
- verified the materialized Seoul and pinned Rust trees match their canonical
  repository sources;
- kept runtime verification isolated from the user's normal browser profile.

Stage 5 project-owned files:

- `native/seoul/browser/adblock/ad_block_domain_resolver.*`
- `native/seoul/browser/adblock/ad_block_engine.*`
- `native/seoul/browser/adblock/ad_block_engine_host.*`
- `native/seoul/browser/adblock/ad_block_service.*`
- `native/seoul/browser/adblock/cosmetic_filter.mojom`
- `native/seoul/browser/adblock/cosmetic_filter_host.*`
- `native/seoul/browser/adblock/rs/src/lib.rs`
- `native/seoul/browser/adblock/ad_block_engine_unittest.cc`
- `native/seoul/browser/adblock/ad_block_core_unittest.cc`
- `native/seoul/browser/adblock/ad_block_browsertest.cc`
- `native/seoul/renderer/BUILD.gn`
- `native/seoul/renderer/cosmetic_filter_agent.*`
- `native/seoul/renderer/cosmetic_filter_agent_unittest.cc`
- `native/patches/chromium/0022-seoul-adblock-cosmetic-filtering.patch`
- `native/patches/manifest.json`
- `native/patches/README.md`
- `native/patches/chromium/README.md`
- `docs/research/native-adblock-implementation.md`

Known Stage 5 boundary:

- production lists still require an owner-approved license/source and signed
  release channel;
- scriptlets, procedural actions, CSP rules, and CNAME uncloaking remain
  separately gated for Stage 8.

### Stage 6 — redirect resources and safe rewrites

Implemented and verified:

- browser-owned replacement catalog with stable names/aliases, explicit MIME
  types, embedded implementations, versions, SHA-256 verification, and
  fail-closed argument validation;
- redirects only when adblock-rust resolves a catalog entry and the browser can
  byte-match the exact vetted data URL;
- synthetic URLLoader responses that never reach the network for redirects;
- remove-parameter rewriting limited to HTTP(S) GET/HEAD/OPTIONS, preserving
  scheme, credentials, host, effective port, path, fragment, and existing query
  pairs except removals;
- redirect destinations and rewritten targets are re-evaluated, and top-level
  safe rewrites restart through the navigation throttle;
- deterministic unit and browser coverage for accepted/rejected redirects,
  subresource rewrites, redirect-target rechecks, and main-frame restarts.

### Stage 7 — updates and LKG

Implemented and verified:

- startup loads the newest valid profile cache without waiting for network and
  falls back to a small Seoul-owned safety baseline;
- two complete engine groups are constructed before either live engine pointer
  moves, preventing half-applied updates;
- component-ready packages require a strict, bounded manifest, exact signed
  component version match, SHA-256 for both payloads, UTF-8 validation, and
  successful Rust engine construction;
- two-slot last-known-good storage writes payloads and manifest atomically,
  commits only through an atomic active-slot marker, and falls back to the
  previous slot when the active slot is damaged;
- browser-process subscription downloads require HTTPS, omit credentials and
  cookies, bypass cache, reject redirects, enforce a 30-second timeout and
  SimpleURLLoader's 5 MiB decoded-body bound, require HTTP 200 plus a supported
  MIME type, and accept content only when it matches a trusted pinned SHA-256;
- update state exposes source, version, attempt/success times, and bounded error
  text through the profile service API and registered preferences;
- added a Chromium `ComponentInstallerPolicy` that requires encrypted update
  transport, a configured 32-byte CRX signing-key hash, compatible manifest
  schema, valid component version, both required rule files, and strict
  per-file/combined size limits before `ComponentReady`;
- registered the component at Chromium's canonical component-update startup
  seam and persisted its ready path/version as one Local State record, so
  already-loaded profiles and profiles created later both receive the same
  signature-verified package through their own `AdBlockService`;
- left the build-time public-key hash empty by default, causing registration to
  be skipped instead of trusting a development placeholder or third-party key;
- update, cache, hash, UTF-8, engine-construction, and network failures retain
  the previously active engines;
- current verification: 18 engine tests, 40 core/LKG/component tests, 28
  interceptor/downloader tests, and 13 focused browser/renderer tests all pass.

Still required before Stage 7 is called fully complete:

- an owner-selected and legally reviewed production list set;
- a Seoul release component public-key hash, external private signing key, and
  compatible production update feed; the registration code is present but
  intentionally remains disabled without that release identity;
- a signed catalog for multiple optional subscriptions and custom-filter
  persistence. Chromium supplies the release-component scheduler; the current
  pinned downloader activates one complete additional rule set supplied by
  trusted catalog metadata.

### Stage 8 — advanced parity

- Add isolated-world scriptlets first, then threat-reviewed page-world support,
  procedural filtering, CSP, and CNAME uncloaking.
- Each capability remains separately gated and tested.

## Unfinished parity

Stages 1–7 currently provide tested browser-process network, frame-associated
WebSocket, primary-main-frame navigation, mode, per-site, temporary-disable,
two-engine, CSS-only cosmetic, vetted redirect/rewrite, last-known-good,
hash-pinned subscription, and fail-closed signed-component infrastructure. This
is not full Brave compatibility. Specifically absent:

- production filter lists and the release-signed component channel;
- a delivery path for the catalogued runtime lists: EasyList and EasyPrivacy are
  catalogued with `enabled_by_default=true` and
  `AdBlockListDelivery::kRuntimeDownload`, but nothing reads that field, so they
  are never fetched. `DownloadPinnedAdditionalRuleSet` has no production caller,
  and it requires a pinned SHA-256, which a continuously updated upstream list
  cannot supply - so the wiring is not a small omission but a delivery-policy
  decision that has not been made. Until it is, a fresh profile blocks with the
  five-rule baseline alone; measured numbers are in
  `docs/release/seoul-product-readiness.md`;
- frame-less worker WebSocket interception;
- multiple optional/custom subscription catalog management;
- scriptlets, procedural filtering, CSP, and CNAME;
- full Brave compatibility coverage.

Brave parity must not be claimed until all of those paths and their compatibility
tests are complete.
