/**
 * Content script: query in, rects out.
 *
 * The harvest and the candidate list never leave this file. What crosses the
 * bridge is the winning element's rects plus the viewport numbers needed to
 * turn them into screen coordinates — nothing else.
 */

import { resolve } from './elementResolver.mjs';
import { harvest } from './harvest.mjs';

/**
 * Latest real pointer event, kept purely as a coordinate reference.
 *
 * A mouse event carries the SAME point in two coordinate systems at once:
 * screenX/screenY on the display, clientX/clientY in the viewport. Their
 * difference is the viewport's origin on screen — exactly, and regardless of
 * how the browser lays out its chrome.
 *
 * That matters because outer/inner arithmetic cannot recover it. With vertical
 * tabs enabled the chrome is 224pt on the left and 16pt on the right; the only
 * thing `outerWidth - innerWidth` knows is that the two sum to 240. Measured
 * against a screenshot, this calibration predicted the element's true screen
 * position to the point on both axes, where the arithmetic was 120pt out.
 *
 * Nothing is sent until a pointer has been over the page at least once.
 */
let calibration = null;

/** The element currently annotated, so a scroll can keep the sketch on it. */
let tracked = null;
let listenersInstalled = false;
let updateScheduled = false;

/**
 * Reads geometry AND viewport context in one synchronous block.
 *
 * THE ORDER AND THE ADJACENCY BOTH MATTER. If ctx is sampled in a different
 * frame from the rects — an await between them, a rAF between them, a DOM write
 * that forces a reflow between them — then during a scroll the rects describe
 * frame N and screenY describes frame N+1. The sketch lands one frame behind
 * the element and appears to lag, and nothing in the native side can recover
 * it, because by then the mismatch is baked into the numbers.
 *
 * Nothing in here writes to the DOM and nothing yields.
 */
function measure(el) {
  const rects = [];
  const list = el.getClientRects();
  for (const r of list) {
    if (r.width > 0 && r.height > 0) {
      rects.push({ x: r.x, y: r.y, width: r.width, height: r.height });
    }
  }
  if (rects.length === 0) {
    const r = el.getBoundingClientRect();
    rects.push({ x: r.x, y: r.y, width: r.width, height: r.height });
  }
  const ctx = {
    screenX: window.screenX,
    screenY: window.screenY,
    innerWidth: window.innerWidth,
    innerHeight: window.innerHeight,
    outerWidth: window.outerWidth,
    outerHeight: window.outerHeight,
    devicePixelRatio: window.devicePixelRatio,
    calibScreenX: calibration ? calibration.screenX : null,
    calibScreenY: calibration ? calibration.screenY : null,
    calibClientX: calibration ? calibration.clientX : null,
    calibClientY: calibration ? calibration.clientY : null,
  };
  return { rects, ctx };
}

function emptyResult(requestId, status, error) {
  return { requestId, status, rects: null, ctx: null, label: null, elementId: null, error: error ?? null };
}

function handleFind(query, requestId) {
  let candidates;
  try {
    candidates = harvest(document);
  } catch (e) {
    return emptyResult(requestId, 'error', `harvest failed: ${e && e.message ? e.message : e}`);
  }
  if (candidates.length === 0) return emptyResult(requestId, 'none', 'nothing nameable on this page');

  const result = resolve(query, candidates);
  if (result.status !== 'confident' || !result.best || !result.best.el) {
    return emptyResult(requestId, result.status, null);
  }

  const el = result.best.el;
  const { rects, ctx } = measure(el);

  tracked = { el, elementId: result.best.id, label: result.best.name };
  installViewportListeners();

  return {
    requestId,
    status: 'confident',
    rects,
    ctx,
    label: result.best.name,
    elementId: result.best.id,
    error: null,
  };
}

function installViewportListeners() {
  if (listenersInstalled) return;
  // capture:true so scrolls inside a scrollable container are seen too, not
  // just scrolls of the document.
  window.addEventListener('scroll', onViewportChange, { passive: true, capture: true });
  window.addEventListener('resize', onViewportChange, { passive: true });
  listenersInstalled = true;
}

/**
 * Coalesced to one measurement per frame. Scroll fires far more often than the
 * compositor paints, and sending every event would flood the bridge with
 * updates the overlay cannot use.
 */
function onViewportChange() {
  if (!tracked || updateScheduled) return;
  updateScheduled = true;
  requestAnimationFrame(() => {
    updateScheduled = false;
    if (!tracked) return;
    if (!tracked.el.isConnected) {
      tracked = null;
      return;
    }
    const { rects, ctx } = measure(tracked.el);
    try {
      chrome.runtime.sendMessage({
        type: 'seoul-update',
        payload: {
          // Keyed by elementId, not by the original requestId: the native side
          // recognises it as a re-anchor and moves the existing sketch rather
          // than replaying the draw-on animation on every scroll frame.
          requestId: tracked.elementId,
          status: 'confident',
          rects,
          ctx,
          label: null,
          elementId: tracked.elementId,
          error: null,
        },
      });
    } catch {
      // The service worker was recycled mid-scroll. The next find reconnects.
    }
  });
}

// Passive and capture-phase so it never interferes with the page, and refreshed
// on every move: the offset changes whenever the window is dragged, the chrome
// changes, or a side panel opens.
addEventListener('pointermove', (event) => {
  calibration = {
    screenX: event.screenX, screenY: event.screenY,
    clientX: event.clientX, clientY: event.clientY,
  };
}, { passive: true, capture: true });

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (!message || message.type !== 'seoul-find') return false;
  // One rAF so the read lands on a settled frame; everything inside it is
  // synchronous, which is what keeps rects and ctx in the same frame.
  requestAnimationFrame(() => {
    let reply;
    try {
      reply = handleFind(message.query, message.requestId);
    } catch (e) {
      reply = emptyResult(message.requestId, 'error', String(e && e.message ? e.message : e));
    }
    sendResponse(reply);
  });
  return true; // response is asynchronous
});
