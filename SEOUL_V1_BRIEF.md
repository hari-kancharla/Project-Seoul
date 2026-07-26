# Project Seoul v1 — Claude Code Build Brief

**Target:** a working voice-and-pointing loop in 7 working days.
**Gate metric:** median time from end-of-user-speech to arrow-on-target, under 600ms.

---

## 0. Read this first (context for the agent)

You are building **Seoul v1**, the first shippable milestone of a voice-first browsing assistant.

The product promise, in one sentence:

> The user talks. The assistant answers in their ear. An arrow lands on the exact element on the page in under 600ms, and the arrow stays glued to that element when the page scrolls.

Nothing else is in v1. See section 7 for what is explicitly excluded.

**Assumption about the existing repo:** this brief targets the MV3 extension harness (`apps/browser-harness/`), which is built, unit-tested, and verified against real Chrome via Puppeteer. It does **not** target `native/seoul/browser/` (the Chromium fork), which per the repo README has never been compiled or runtime-verified. Do not attempt to build Chromium as part of v1. If the Chromium build is later verified, only section 3.2 changes.

---

## 1. Non-negotiable constraints

These are researched, not preferences. Violating any one of them costs days.

1. **Never send the accessibility tree across the process bridge.** Chrome native messaging caps host-to-Chrome messages at 1MB, and CDP's WebSocket silently disconnects above 1MB with no error. A real page's AX tree routinely exceeds this. Queries go in, one small rect comes out.
2. **The coordinate transform is a pure, unit-tested function written before any UI exists.** Every early bug lives here.
3. **`.nonactivatingPanel` must be in the panel's initial `styleMask`.** Setting it after init leaves the WindowServer activation tag in place and produces a window that looks key but never receives key events.
4. **Never request Screen Recording or Accessibility permission.** Microphone only. This is a product advantage, not an oversight. Nothing in v1 may reintroduce those prompts.
5. **Do not link libwebrtc.** The Realtime session runs inside a hidden `WKWebView`.
6. **Do not develop on a beta macOS.** Transparent borderless window click-through broke in macOS Tahoe 26.3 RC, was fixed in the 26.3 public release, and has been reported to regress in 26.4 beta.

---

## 2. Architecture

Three processes.

```
┌─────────────────────────────┐
│ Swift menu bar app          │  LSUIElement, no dock icon
│  ├─ OverlayPanel (NSPanel)  │  draws the arrow, sharingType = .none
│  ├─ Hidden WKWebView        │  OpenAI Realtime over WebRTC
│  ├─ CoordinateTransform     │  pure function, tested
│  └─ NativeHost (stdio)      │  talks to the extension
└─────────────────────────────┘
              ▲ stdio, JSON, small messages only
              ▼
┌─────────────────────────────┐
│ MV3 extension (existing)    │
│  ├─ service worker (router) │
│  └─ content script          │  owns the AX tree, does matching in-page
└─────────────────────────────┘
```

The Swift app never sees page content. It sends a query string and receives one rect.

---

## 3. Milestones

Each milestone has acceptance criteria. Do not start the next until the current one passes.

### 3.1 M1 — CoordinateTransform (Day 1)

Create `Seoul/Core/CoordinateTransform.swift` and `SeoulTests/CoordinateTransformTests.swift`.

Implement one pure function:

```swift
struct PageRect { let x, y, width, height: Double }   // CSS px, viewport-relative
struct ViewportContext {
    let screenX: Double          // window.screenX
    let screenY: Double          // window.screenY
    let innerWidth: Double
    let innerHeight: Double
    let outerHeight: Double
    let pageZoom: Double         // window.devicePixelRatio / screen scale
}

func pageRectToScreenRect(
    _ rect: PageRect,
    context: ViewportContext,
    screen: NSScreen,
    primaryScreenMaxY: Double
) -> NSRect
```

The transform chain, in order:

1. Multiply by `pageZoom`.
2. Add the browser chrome offset, derived as `outerHeight - innerHeight`.
3. Add `screenX` / `screenY` to get global top-left coordinates.
4. Flip the origin: `y_appkit = primaryScreenMaxY - y_topleft - height`.
5. Divide by that screen's `backingScaleFactor` where converting from device pixels.

**Traps to encode in tests:**

- AppKit's origin is bottom-left of the primary screen. CoreGraphics and CGEvent use top-left.
- `backingScaleFactor` is per screen. Write a test with a mixed Retina and non-Retina two-monitor layout.
- Page zoom and macOS display scaling are independent multipliers that compound.
- Derive the chrome offset from `window.screenX/screenY` and `outerHeight - innerHeight`. Do **not** use `chrome.windows.get`; its bounds do not agree with the viewport.

**Acceptance:** all tests green, including negative-coordinate screens (a monitor positioned left of or above the primary), fractional zoom levels, and mixed-DPI layouts. No AppKit UI code in this file beyond `NSScreen` and `NSRect` types.

### 3.2 M2 — OverlayPanel (Day 1 to 2)

Create `Seoul/Overlay/OverlayPanel.swift`.

Exact configuration:

```swift
let panel = OverlayPanel(
    contentRect: screenFrame,
    styleMask: [.borderless, .nonactivatingPanel],   // must be set at init
    backing: .buffered,
    defer: false
)
panel.level = .screenSaver
panel.isOpaque = false
panel.backgroundColor = .clear
panel.hasShadow = false
panel.ignoresMouseEvents = true
panel.sharingType = .none
panel.collectionBehavior = [.canJoinAllSpaces, .stationary, .fullScreenAuxiliary]
```

`Info.plist` must set `LSUIElement` to `true`.

Public API:

```swift
func point(at rect: NSRect, label: String?)
func clear()
```

Render a curly arrow whose tip sits at the rect's leading edge, with the tail curving in from whichever side has room. Animate the tip into place over roughly 180ms with ease-out. Redraw is driven by `point(at:)` calls, not by a timer.

**Acceptance:**
- The arrow is visible on screen.
- `Cmd+Shift+4` capture of that region does **not** contain the arrow.
- Clicking where the arrow is passes the click through to the app underneath.
- Clicking never brings the Seoul app to the front.
- The arrow appears correctly on a secondary monitor.

### 3.3 M3 — Element resolution and re-anchoring (Day 2 to 3)

Extend the existing content script. Add two messages.

**Request (Swift to page):**
```json
{ "op": "find", "query": "shipping address field", "requestId": "..." }
```

**Response (page to Swift):**
```json
{ "requestId": "...", "id": "e42", "rect": {...}, "role": "textbox",
  "name": "Shipping address", "context": {...} }
```

Matching runs **in the page**. Build a candidate list from the accessibility tree using `role` plus accessible `name`, score by string similarity against the query, and return the single best match plus its `getBoundingClientRect()` and the `ViewportContext` fields. If confidence is below threshold, return the top three candidates with names so the assistant can ask which one.

**Re-anchoring:** once an element is tracked, attach a `ResizeObserver` and an `IntersectionObserver` to it and a `scroll` listener throttled to `requestAnimationFrame`. Push an updated rect only when it actually changed. Clear the arrow on navigation. If the element leaves the viewport, either scroll it back into view or fade the arrow out. Never leave a stale arrow on screen.

**Native messaging format:** stdio, JSON, UTF-8, prefixed with a 32-bit length in **native byte order**. Assert on the Swift side that any outbound message is under 1MB and fail loudly rather than silently truncating.

**Acceptance:** ask for an element on a long page, the arrow lands on it, scroll continuously for ten seconds, and the arrow tracks it without visible lag or drift.

### 3.4 M4 — Realtime voice session (Day 4 to 5)

Create `Seoul/Voice/RealtimeSession.swift` plus `Resources/realtime.html`.

- A hidden, zero-size `WKWebView` loads `realtime.html`.
- The page establishes a WebRTC session with the OpenAI Realtime API.
- Swift mints an ephemeral token via `POST /v1/realtime/client_secrets` and passes it in. **Do not** send `OpenAI-Beta: realtime=v1`; that header is beta-only and must be omitted on the GA interface.
- Swift to page via `evaluateJavaScript`. Page to Swift via `WKScriptMessageHandler`.

Session config:
- Model: `gpt-realtime-mini` by default. Escalate to the full model only for requests classified as needing reasoning.
- **Select the voice before any audio is emitted.** Voice set after the first audio output will not apply to the session.
- Leave input transcription **off** unless needed; it is billed on a separate path.
- Tune server VAD toward less aggressive. False speech-ends damage the experience more than added latency does.
- Keep playback buffers minimal.
- **Pre-warm the session at app launch.** First-turn latency runs roughly 500 to 1,200ms while subsequent turns run 300 to 600ms, so a cold first turn will be felt.

Register exactly five tools, no more:

```
find_element(query: string)      -> resolves and returns candidates
point_at(element_id: string)     -> draws the arrow
scroll_to(element_id: string)    -> scrolls the element into view
open_url(url: string)            -> opens in the current tab
switch_tab(index_or_title)       -> switches tabs
```

Tool calls dispatch straight to the native host. No reasoning model in the hot path for any of these.

**Acceptance:** speak a request, get a spoken answer, and see the arrow land. Barge-in works: speaking over the assistant stops its audio immediately.

### 3.5 M5 — Instrument and tune (Day 6 to 7)

Add a timing harness that logs, per turn:

```
t0  end of user speech (VAD speech_stopped event)
t1  tool call received by Swift
t2  rect returned from page
t3  arrow rendered
t4  first audio byte played
```

Run 30 turns across five different sites. Report p50 and p95 for `t3 - t0`.

**Acceptance:** p50 under 600ms. If it is not, spend the remaining time on that number and nothing else. Every other feature is worthless if this one fails.

---

## 4. Known traps, collected

| Trap | Consequence | Mitigation |
|---|---|---|
| Message over 1MB across native messaging or CDP | Silent disconnect, no error | Query in, one rect out. Assert size in Swift. |
| `.nonactivatingPanel` set after init | Window looks key, receives no key events | Set it in the initial `styleMask`. |
| AppKit bottom-left vs CoreGraphics top-left origin | Arrow appears mirrored vertically | Explicit flip step, unit tested. |
| Mixed-DPI multi-monitor | Arrow offset on the secondary display | Per-screen `backingScaleFactor`. |
| Voice set after first audio | Wrong voice for the whole session | Set voice in the initial session config. |
| Aggressive VAD | Assistant cuts the user off mid-sentence | Tune toward less aggressive. |
| Cold first turn | Feels broken on the very first impression | Pre-warm at app launch. |
| Beta macOS | Click-through breaks entirely | Pin to a stable release. |
| Stale arrow after scroll or navigation | Destroys trust in every future arrow | rAF re-anchor, clear on navigation. |

---

## 5. Repo layout

```
Seoul/
  Core/CoordinateTransform.swift
  Overlay/OverlayPanel.swift
  Overlay/ArrowRenderer.swift
  Voice/RealtimeSession.swift
  Voice/ToolDispatcher.swift
  Bridge/NativeHost.swift
  Resources/realtime.html
  Info.plist
SeoulTests/
  CoordinateTransformTests.swift
  NativeHostFramingTests.swift
apps/browser-harness/          (existing, extend only)
  src/content/elementResolver.ts
  src/content/anchorTracker.ts
```

---

## 6. Working rules for the agent

- Write the test before the implementation for `CoordinateTransform` and `NativeHost` framing.
- Do not add a dependency without stating why in one line. No package should be added for something under 50 lines.
- No `print` debugging left in committed code. Use `os_log` with a `seoul` subsystem.
- Every milestone gets its own commit. Do not batch.
- If a milestone's acceptance criteria cannot be met, stop and report why rather than working around it.
- Prefer boring, obvious code. This will be read by other people under time pressure.

---

## 7. Explicitly out of scope for v1

Not cancelled. Sequenced. Do not build any of these:

- The Chromium fork and the vertical tab strip
- The sidebar and insight cards
- Spotify, connectors, and any third-party integration
- Memory and persistence
- Autonomous multi-step agents
- Windows support
- Any of the remaining subsystems in `native/seoul/browser/`

If a request seems to require one of these, it is out of scope for v1. Say so and move on.

---

## 8. The kickoff prompt

Paste this into Claude Code, in the repo root, with this brief saved as `SEOUL_V1_BRIEF.md`:

> Read `SEOUL_V1_BRIEF.md` in full before writing anything.
>
> We are building Seoul v1. Start with Milestone 3.1 only: `Seoul/Core/CoordinateTransform.swift` and `SeoulTests/CoordinateTransformTests.swift`.
>
> Write the tests first. Cover, at minimum: the AppKit-to-CoreGraphics origin flip, a secondary monitor positioned to the left of the primary (negative x), a secondary monitor positioned above the primary (negative y), a mixed-DPI two-monitor layout, fractional page zoom at 1.25 and 0.9, and a rect that is partially offscreen.
>
> Then implement the function until the tests pass.
>
> Constraints: this file is pure. No window creation, no side effects, no I/O, no singletons. It may reference `NSScreen` and `NSRect` types only. Do not start Milestone 3.2. Do not touch the browser extension. When the tests pass, stop and report the results.

---

## 9. Note for the YC application

The thing that gets evaluated is not this architecture. It is a thirty-second screen recording where someone talks and an arrow lands on the right thing faster than the viewer expected, plus one number: p50 latency.

Build M1 through M5. Record the demo. Everything in section 7 can wait until after.
