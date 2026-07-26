/**
 * Seoul coordinate transform: page rect -> macOS AppKit screen rect.
 *
 * This is the verified reference implementation. The Swift port is a
 * line-for-line translation; the test vectors below are the Swift test spec.
 *
 * KEY CORRECTION vs the naive approach:
 *   backingScaleFactor plays NO part in this transform.
 *   NSWindow/NSRect frames are in POINTS, not device pixels.
 *   window.screenX/screenY are in CSS px which map 1:1 to points on macOS.
 *   Dividing by backingScaleFactor here is a bug that shows up only on Retina.
 *
 * Coordinate spaces involved:
 *   A. page space    - getBoundingClientRect(), CSS px, viewport-relative, y down
 *   B. screen-TL     - macOS global points, origin top-left of primary, y down
 *   C. AppKit        - macOS global points, origin bottom-left of primary, y up
 */

/**
 * @param {{x:number,y:number,width:number,height:number}} rect
 *        getBoundingClientRect() of the target element.
 * @param {object} ctx  Reported by the content script, same frame as the rect.
 *        screenX, screenY      - window.screenX / window.screenY (points, TL origin)
 *        innerWidth, innerHeight
 *        outerWidth, outerHeight
 *        devicePixelRatio      - window.devicePixelRatio
 *        displayScaleFactor    - the NSScreen backingScaleFactor Swift already knows
 * @param {number} primaryScreenHeight  NSScreen.screens[0].frame.height, in points.
 * @returns {{x:number,y:number,width:number,height:number,pageZoom:number}}
 */
export function pageRectToScreenRect(rect, ctx, primaryScreenHeight) {
  // 1. Page zoom. devicePixelRatio folds together display scale and page zoom.
  //    Divide the display scale back out to isolate page zoom.
  //    Retina at 100% zoom: dpr 2 / scale 2 = 1.0
  //    Retina at 125% zoom: dpr 2.5 / scale 2 = 1.25
  const pageZoom = ctx.devicePixelRatio / ctx.displayScaleFactor;

  // 2. Browser chrome offset. On macOS side chrome is 0, so the horizontal
  //    term is zero and the whole vertical delta is the tab strip + toolbar.
  //    Computed defensively so a future Windows port does not silently break.
  //    CRITICAL: innerWidth/innerHeight are page CSS px and SHRINK as the user
  //    zooms in. outerWidth/outerHeight are screen points and do not change.
  //    So inner must be scaled by pageZoom before subtracting, otherwise the
  //    arrow drifts further off target the more the user has zoomed.
  const chromeLeft = Math.max(0, (ctx.outerWidth - ctx.innerWidth * pageZoom) / 2);
  const chromeTop = Math.max(0, ctx.outerHeight - ctx.innerHeight * pageZoom - chromeLeft);

  // 3. Page space -> screen-TL. rect is in page CSS px, so it scales by zoom.
  //    screenX/screenY are already in points and do NOT scale by zoom.
  const tlX = ctx.screenX + chromeLeft + rect.x * pageZoom;
  const tlY = ctx.screenY + chromeTop + rect.y * pageZoom;
  const w = rect.width * pageZoom;
  const h = rect.height * pageZoom;

  // 4. screen-TL -> AppKit. x is unchanged; y flips about the primary screen.
  //    Subtracting h anchors the AppKit rect at its bottom edge.
  //    Works for negative tlY (a display positioned above the primary) and for
  //    negative tlX (a display to the left) with no special casing.
  const appKitY = primaryScreenHeight - tlY - h;

  return { x: tlX, y: appKitY, width: w, height: h, pageZoom };
}

/** Convert an AppKit rect back to screen-TL. Used by tests and by hit-testing. */
export function appKitRectToScreenTopLeft(r, primaryScreenHeight) {
  return {
    x: r.x,
    y: primaryScreenHeight - r.y - r.height,
    width: r.width,
    height: r.height,
  };
}

/**
 * Pick which NSScreen a rect lands on, by largest intersection area.
 * Needed because the arrow must be drawn on the panel covering that display.
 * screens: [{ x, y, width, height }] in AppKit space.
 */
export function screenForRect(rect, screens) {
  let best = null;
  let bestArea = -1;
  for (const s of screens) {
    const ix = Math.max(0, Math.min(rect.x + rect.width, s.x + s.width) - Math.max(rect.x, s.x));
    const iy = Math.max(0, Math.min(rect.y + rect.height, s.y + s.height) - Math.max(rect.y, s.y));
    const area = ix * iy;
    if (area > bestArea) {
      bestArea = area;
      best = s;
    }
  }
  return bestArea > 0 ? best : null;
}
