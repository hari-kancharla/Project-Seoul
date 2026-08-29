# Seoul Handset

Status: core model landed and unit tested; WebContents integration and the
window layer are not yet built. This document is the design and the honest
boundary, not a readiness claim. `docs/release/seoul-product-readiness.md`
remains the source of truth for what is verified.

## What a Handset is

A Handset is one web page presented as a phone, in its own chrome-less window:
exact device metrics, a mobile viewport, coarse-pointer media features, a
coherent mobile User-Agent with matching client hints, and touch instead of a
mouse.

"Open in Handset" on a tab produces a window that is the device. The page has
the whole window; there is no toolbar, no tab strip, and no letterboxed phone
picture sitting inside a desktop frame. It shares the originating tab's storage
partition, so a signed-in session stays signed in - a choice that is not free,
and that the section on process isolation explains the cost of.

## What a Handset is not

It does not run iOS or Android application binaries, and no amount of work in
this repository would change that:

- **iOS.** An iOS app runs on Apple Silicon only when its developer publishes it
  to the Mac App Store. That is per-developer opt-in, and the large social and
  messaging apps have opted out. There is also no macOS API for embedding
  another application's window into your own view hierarchy, so even an
  opted-in app could not be composited into a Seoul window. Apple's own iPhone
  Mirroring drives a physically present iPhone for exactly this reason - it is
  the only lawful way to reach the real binary, not a shortcut Apple took.
- **Android.** An arm64 Android guest does run at near-native speed on Apple
  Silicon, so the emulation itself is tractable. It dead-ends elsewhere: the
  major apps require Google Play services, which cannot be redistributed, and
  they gate sign-in on Play Integrity attestation, which fails on an emulator
  by design. The result would be an Android runtime that cannot sign in to the
  apps it exists to run.

So Handset targets the mobile web and installed web apps, which is where the
experience is actually reachable. For the large social products this is not a
consolation: their mobile sites are complete progressive web apps, and in a
chrome-less window with correct metrics and real touch they are the app for
almost every flow. The gaps that remain are native-only surfaces - camera-first
capture, the OS share sheet, and background push - and they are named here
rather than papered over.

## The four signals that must agree

A site decides which layout to serve from four independent signals. Overriding
some and leaving the rest describing a desktop Mac is the usual way device
emulation fails: the site reads the signal you forgot and serves the desktop
page, or serves the mobile page to a viewport that cannot lay it out.

1. **Viewport and screen.** `blink::DeviceEmulationParams` with
   `screen_type = kMobile`. Blink's `DevToolsEmulator::EnableMobileEmulation`
   flips the whole mobile stack at runtime - `viewport_enabled`,
   `viewport_meta_enabled`, `viewport_style = kMobile`,
   `shrinks_viewport_contents_to_fit`, and the page-scale limits. Those
   preferences have Android/iOS-only compile-time *defaults* on a macOS build,
   but the emulator sets them at runtime, so a desktop build reaches the real
   mobile layout path rather than an approximation of it.
2. **Media features.** `pointer: coarse`, `hover: none`, and the matching
   `any-*` forms, through the pointer and hover fields of `WebPreferences`. A
   site whose menu opens on hover is unusable without this, and no viewport
   width substitutes for it.
3. **User-Agent string.**
4. **User-Agent client hints** - `Sec-CH-UA-Mobile`, `-Platform`,
   `-Platform-Version`, `-Model`, `-Form-Factors`, and their
   `navigator.userAgentData` mirror.

Signals 3 and 4 are set together through
`WebContents::SetUserAgentOverride(blink::UserAgentOverride, ...)`, which
carries both the string and the metadata. `native/seoul/browser/handset/`
decides both from one profile so they cannot drift apart.

An iOS profile deliberately sends **no** client hints. Safari implements none,
so a real iPhone sends none; a Safari string arriving with a full Chromium hint
set is a combination no device produces, and a site that checks can tell.

One caveat on signal 4, because the wording above would otherwise overstate it.
Suppressing the metadata suppresses the `Sec-CH-UA-*` **request headers**. It
does not remove the `navigator.userAgentData` mirror: blink materializes an
absent metadata override into a default-constructed one before it reaches
script, so on an iOS profile the object still exists and reports `mobile:false`
with an empty platform and brand list, where a real iPhone leaves the property
undefined. A site branching on the headers or on the User-Agent string sees a
phone; a site branching on `navigator.userAgentData.mobile` does not. There is
no value of `ua_metadata_override` that closes this - it needs the property
hidden in the renderer.

## Input, and what the latency argument actually is

An earlier draft of this document justified the whole design on scroll latency
and got the mechanism wrong in three ways. The corrected version is narrower
and still sufficient, but it is a different argument and the difference matters
to anyone deciding whether the work is worth it.

**What was wrong.** The draft said Chrome's device mode scrolls badly because
`TouchEmulator::Mode::kEmulatingTouchFromMouse` converts wheel ticks into
touches and loses velocity. It does not: `TouchEmulatorImpl::HandleMouseWheelEvent`
declines the wheel except while an emulated touch sequence is already active,
so device-mode trackpad scrolling already runs the ordinary compositor-threaded
touchpad path with Apple's own momentum. The draft also said Seoul would inject
`GestureScrollBegin/Update/End` and `GestureFlingStart` carrying trackpad
velocity. There is no such entry point - the only injection API is
`InjectTouchEvent(const blink::WebTouchEvent&, ...)`, and gestures and their
velocities are derived internally by `ui::FilteredGestureProvider`. And it said
the fling curve runs on the compositor thread. It does not: an injected
`GestureFlingStart` is consumed by the browser-process `FlingController` and is
never sent to the renderer, ticked by `fling_scheduler_mac.mm` off the
*browser's* compositor animation observer. That avoids the renderer main
thread, which is the property worth having, but it is not what was claimed.

**The latency delta that is real.** The wheel path costs one browser-renderer
round trip per wheel event, because `MouseWheelEventQueue` only synthesizes the
`GestureScrollUpdate` after the wheel event is acked. An injected touch on a
page with no blocking touch handler is acked synchronously inside the browser
and its `GestureScrollUpdate` goes out in the same stack. That is a genuine
saving and it is smaller than the draft implied. **It has not been measured, and
no number should be quoted until it has.**

**The reasons that actually justify the work**, neither of which is latency:

1. **Touch event dispatch.** There is no public `ForwardTouchEvent`. Without
   the touch emulator a site listening for `touchstart`/`touchmove`/`touchend`,
   or feature-detecting `TouchEvent`, receives nothing - so a swipeable carousel
   or a pull-to-refresh does not work at all. `ForwardGestureEvent` *is* already
   public, so the scroll-gesture half needs no patch.

   The emulator runs in `kEmulatingTouchFromMouse`, synthesizing touches from
   the pointer, **not** in `kInjectingTouchEvents`. That is not the ambitious
   choice and it is the correct one until a trackpad gesture layer exists: the
   injecting mode delivers a touch only when something calls
   `InjectTouchEvent`, so turning it on with no injector would be actively
   worse than leaving touch off. A page would feature-detect touch, switch to
   its touch-only path, retire its mouse handlers, and then wait forever for an
   event that never comes. Synthesizing from the pointer makes a drag a real
   swipe today. The injecting mode is where a genuine trackpad gesture layer
   plugs in, once one exists to feed it.
2. **Screen and device pixel ratio.** `blink::DeviceEmulationParams` has no
   public route whatsoever. Without it `screen.width`, `screen.height`, and
   `devicePixelRatio` keep reporting the Mac's display, and any DPR-driven asset
   choice picks the wrong image.

**Unsolved, and named rather than assumed away.** macOS delivers inertia itself
as further `scrollWheel` events with a momentum phase. Forwarding those as
touchmoves makes the gesture provider emit its own fling on top of Apple's -
double inertia. Suppressing them and owning the fling means a non-native
deceleration, and `ShouldUseMobileFlingCurve()` returns false on macOS, so the
curve would be the desktop one rather than the phone feel this product wants.
Neither option is obviously right; this needs a prototype against a real page
before either is committed to.

## Process isolation: what is actually achievable

Blink's mobile emulation installs a **process-global** `ScopedGlobalOverrides`
singleton covering overlay scrollbars, `OrientationEvent`, and the mobile layout
theme. Those three have no `WebPreferences` equivalent, so they cannot be scoped
per-page. If a Handset shares a renderer process with an ordinary tab, that tab
silently gets overlay scrollbars and `window.orientation`.

An earlier draft called a dedicated process a requirement and implied Space
membership would supply it. Both halves need correcting.

**A dedicated process and a shared session are mutually exclusive.** A
`RenderProcessHost` serves exactly one `StoragePartition` - the check is
explicit in `RenderProcessHostImpl::IsSuitableHost`, "a RenderProcessHost can
only support a single StoragePartition" - so a distinct partition does
guarantee a distinct process. But a distinct partition is a distinct cookie
jar, and a Handset signed out of the site it is presenting is useless: the
entire point is seeing the mobile experience of a site you are logged in to.

**There is no per-tab escape.** The only embedder hook that influences process
reuse, `ContentBrowserClient::ShouldTryToUseExistingProcessHost`, takes a
`BrowserContext` and a `GURL` and cannot name a particular WebContents.
`DoesSiteRequireDedicatedProcess` is keyed on a site and would isolate that site
everywhere rather than this tab.

So Handset keeps the originating partition and accepts the residual. Two things
follow:

- **In an isolated Space this resolves itself.** An isolated Space already has
  its own `StoragePartition` (`SiteInstanceForNewTabInActiveSpace`), so its tabs
  already sit in their own process, and the session the user cares about lives
  in that partition anyway. Isolation and session continuity stop competing.
  Only a non-isolated Space carries the residual, and only between tabs of that
  same Space.
- **The residual is reported, not assumed away.** `HandsetRendererIsShared()`
  answers whether the Handset's renderer hosts frames belonging to any other
  page, so the emulation layer can decide - decline, warn, or proceed - instead
  of leaking silently. Chrome's own device mode has the same exposure; the
  difference is that Seoul can see it.

## Ordering: emulation and preferences are one transition

`DevToolsEmulator` snapshots the embedder's viewport settings when it is
constructed, and once mobile emulation is on it swallows later preference
pushes. A cross-process navigation builds a fresh `WebViewImpl` whose snapshot
is the desktop default, so if emulation reaches the new widget before the
preferences do, the snapshot is wrong and disabling emulation later restores
desktop layout.

The transition therefore has a required order: on enable, push `WebPreferences`
**before** enabling device emulation; on disable, disable emulation **before**
recomputing preferences, and recompute unconditionally afterwards.

## Integration surface

Two entry points Seoul needs are `//content`-internal, and the surface is wider
than a first look suggests:

- `RenderWidgetHostImpl::GetAssociatedFrameWidget()` returns a
  `mojo::AssociatedRemote<blink::mojom::FrameWidget>&`. Exposing that through
  `content/public` is not a narrow change - it hands the embedder a raw mojo
  pipe to a Blink interface.
- `GetTouchEmulator()` is on `RenderWidgetHostImpl`, not the public
  `RenderWidgetHost`, and returns the content-internal `TouchEmulatorImpl*`.
  Every method Seoul would call - `Enable`, `Disable`, `InjectTouchEvent` -
  lives on that internal type, so exporting the accessor alone would not
  compile.

So the patch must **not** export these objects. It adds a `content/public`
header of free functions in `namespace content` that take a
`RenderWidgetHost*` and do the work internally, keeping the mojo remote and the
emulator implementation inside `//content` where they belong.

This is also two patches rather than one: the `content/public` facade, and
separately the chrome-layer entry points that call
`seoul::OverrideHandsetWebPreferences` from both
`ChromeContentBrowserClient::OverrideWebPreferences` and
`::OverrideWebPreferencesAfterNavigation`, mirroring how patch 0005 wires the
Boost hook.

Note for whoever authors them: `check:syntax` parses Seoul source against the
checkout and so needs the patches **applied**, while `patches.sh verify` and
`apply` refuse to run on a dirty tree. Running the two in the obvious order
deadlocks. Author against the applied tree, capture the diff, restore, then
update the manifest.

## Module layout

- `native/seoul/browser/handset/` - pure model. The device catalogue, the
  resolved geometry, and the User-Agent and client-hint rules. `//base` only,
  fully unit tested, no browser required.
- `native/seoul/browser/product/browser/` - the WebContents integration:
  applies emulation, the User-Agent override, the touch emulator mode, and the
  web preferences, and reapplies them across navigation and process swap.
- The window layer - a chrome-less Seoul window bound to the Handset - is
  specified but not yet built.

## Snap

`HandsetSnapMode::kSnapToProfile` sizes the window to the chosen device exactly
and holds it there; this is the option surfaced as "snap to device". `kFree`
lets the window resize and the emulated viewport follow it, inheriting the
device pixel ratio and User-Agent family from the nearest catalogue profile so
a window dragged to tablet width stops claiming a phone's pixel ratio.

Screen size always equals view size. A Handset window has no chrome, so the
page occupies the window exactly as a full-bleed app occupies a phone screen,
and a site comparing `screen` to `innerWidth` finds them consistent.
