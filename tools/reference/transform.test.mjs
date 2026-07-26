import { test } from 'node:test';
import assert from 'node:assert/strict';
import { pageRectToScreenRect, appKitRectToScreenTopLeft, screenForRect } from './transform.mjs';

const close = (a, b, msg) => assert.ok(Math.abs(a - b) < 0.001, `${msg}: got ${a}, want ${b}`);
const eqRect = (got, want, label) => {
  close(got.x, want.x, `${label}.x`);
  close(got.y, want.y, `${label}.y`);
  close(got.width, want.width, `${label}.w`);
  close(got.height, want.height, `${label}.h`);
};

// Non-Retina 1920x1080 primary, Chrome maximized, 100% zoom.
const NON_RETINA = {
  screenX: 0, screenY: 25,
  innerWidth: 1920, innerHeight: 980,
  outerWidth: 1920, outerHeight: 1055,
  devicePixelRatio: 1, displayScaleFactor: 1,
};

// MacBook Pro 14" default scaling: 1512x982 points, backingScaleFactor 2.
const retina = (zoom) => ({
  screenX: 0, screenY: 38,
  innerWidth: 1512 / zoom, innerHeight: 880 / zoom,
  outerWidth: 1512, outerHeight: 944,
  devicePixelRatio: 2 * zoom, displayScaleFactor: 2,
});

test('non-Retina, 100% zoom', () => {
  const r = pageRectToScreenRect({ x: 100, y: 200, width: 300, height: 40 }, NON_RETINA, 1080);
  eqRect(r, { x: 100, y: 740, width: 300, height: 40 }, 'nonretina');
  close(r.pageZoom, 1, 'zoom');
});

test('Retina, 100% zoom, backingScaleFactor must NOT appear in the result', () => {
  const r = pageRectToScreenRect({ x: 50, y: 100, width: 200, height: 30 }, retina(1), 982);
  eqRect(r, { x: 50, y: 750, width: 200, height: 30 }, 'retina100');
  close(r.pageZoom, 1, 'zoom');
});

test('Retina, 125% page zoom', () => {
  const r = pageRectToScreenRect({ x: 50, y: 100, width: 200, height: 30 }, retina(1.25), 982);
  close(r.pageZoom, 1.25, 'zoom');
  eqRect(r, { x: 62.5, y: 717.5, width: 250, height: 37.5 }, 'retina125');
});

test('Retina, 90% page zoom (zoomed out)', () => {
  const r = pageRectToScreenRect({ x: 100, y: 100, width: 100, height: 20 }, retina(0.9), 982);
  close(r.pageZoom, 0.9, 'zoom');
  eqRect(r, { x: 90, y: 772, width: 90, height: 18 }, 'retina90');
});

test('chrome offset stays constant across zoom levels (regression guard)', () => {
  // If innerHeight is not scaled by pageZoom, this test fails at 125% and 200%.
  const base = pageRectToScreenRect({ x: 0, y: 0, width: 1, height: 1 }, retina(1), 982);
  for (const z of [1.1, 1.25, 1.5, 2, 0.8, 0.67]) {
    const r = pageRectToScreenRect({ x: 0, y: 0, width: 1, height: 1 }, retina(z), 982);
    close(r.x, base.x, `origin x @${z}`);
    close(r.y + r.height, base.y + base.height, `origin top y @${z}`);
  }
});

test('secondary display to the LEFT of primary (negative screenX)', () => {
  const ctx = {
    screenX: -1920, screenY: 0,
    innerWidth: 1920, innerHeight: 1000,
    outerWidth: 1920, outerHeight: 1064,
    devicePixelRatio: 1, displayScaleFactor: 1,
  };
  const r = pageRectToScreenRect({ x: 10, y: 10, width: 100, height: 20 }, ctx, 982);
  eqRect(r, { x: -1910, y: 888, width: 100, height: 20 }, 'left');
});

test('secondary display ABOVE primary (negative screenY)', () => {
  const ctx = {
    screenX: 0, screenY: -1080,
    innerWidth: 1920, innerHeight: 1000,
    outerWidth: 1920, outerHeight: 1064,
    devicePixelRatio: 1, displayScaleFactor: 1,
  };
  const r = pageRectToScreenRect({ x: 0, y: 0, width: 50, height: 50 }, ctx, 982);
  eqRect(r, { x: 0, y: 1948, width: 50, height: 50 }, 'above');
});

test('mixed DPI: Retina primary, window on non-Retina secondary', () => {
  const ctx = {
    screenX: 1512, screenY: 0,
    innerWidth: 1920, innerHeight: 1000,
    outerWidth: 1920, outerHeight: 1064,
    devicePixelRatio: 1, displayScaleFactor: 1, // this display is 1x
  };
  const r = pageRectToScreenRect({ x: 10, y: 10, width: 100, height: 20 }, ctx, 982);
  eqRect(r, { x: 1522, y: 888, width: 100, height: 20 }, 'mixed');
});

test('element scrolled partially above the viewport (negative rect.y)', () => {
  const r = pageRectToScreenRect({ x: 0, y: -20, width: 100, height: 40 }, retina(1), 982);
  eqRect(r, { x: 0, y: 860, width: 100, height: 40 }, 'clipped');
});

test('round trip AppKit -> top-left -> AppKit is lossless', () => {
  const a = pageRectToScreenRect({ x: 33, y: 77, width: 120, height: 44 }, retina(1.25), 982);
  const tl = appKitRectToScreenTopLeft(a, 982);
  const back = appKitRectToScreenTopLeft(tl, 982);
  eqRect(back, a, 'roundtrip');
});

test('screenForRect picks the display with the largest intersection', () => {
  const screens = [
    { name: 'primary', x: 0, y: 0, width: 1512, height: 982 },
    { name: 'secondary', x: 1512, y: 0, width: 1920, height: 1080 },
  ];
  assert.equal(screenForRect({ x: 100, y: 100, width: 50, height: 50 }, screens).name, 'primary');
  assert.equal(screenForRect({ x: 2000, y: 100, width: 50, height: 50 }, screens).name, 'secondary');
  // Straddling the seam: 40pt on primary, 10pt on secondary.
  assert.equal(screenForRect({ x: 1472, y: 100, width: 50, height: 50 }, screens).name, 'primary');
  // Entirely offscreen.
  assert.equal(screenForRect({ x: 9000, y: 9000, width: 10, height: 10 }, screens), null);
});
