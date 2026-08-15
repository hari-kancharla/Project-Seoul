// Project Seoul - identity and step artwork.
//
// Everything here is inline SVG rather than an image file. The welcome surface
// has a `default-src 'self'` CSP with no network of any kind, and an inlined
// vector also stays sharp on every display and inherits `currentColor` so the
// light and dark treatments cannot drift apart.
//
// The mark is the product in one glyph: a window, a rail down its leading edge,
// and an aperture. It is built on an 8-unit grid inside a 48-unit box so it
// stays true at 16px in a tab strip and at 200px here.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

// The Seoul mark. `scale` drives everything so one definition serves the hero,
// the corner lockup and a favicon-sized rendering.
export function seoulMark(size: number = 48) {
  return html`
    <svg class="mark" width="${size}" height="${size}" viewBox="0 0 48 48"
        fill="none" aria-hidden="true">
      <rect x="3" y="3" width="42" height="42" rx="13"
          stroke="currentColor" stroke-width="2.5" opacity="0.9"></rect>
      <!-- The rail: the product's defining edge. -->
      <rect x="11" y="12" width="5" height="24" rx="2.5"
          fill="currentColor"></rect>
      <!-- The aperture: the browser that looks at the page for you. -->
      <circle cx="30" cy="24" r="7.5" stroke="currentColor"
          stroke-width="2.5"></circle>
      <circle cx="30" cy="24" r="2.5" fill="currentColor"></circle>
    </svg>`;
}

// Step 1 artwork: the mark at hero scale with concentric rings that settle
// outwards, so the first frame of the product has motion in it rather than
// arriving as a static block of text.
export function heroArt() {
  return html`
    <div class="art art-hero" aria-hidden="true">
      <span class="ring ring-1"></span>
      <span class="ring ring-2"></span>
      <span class="ring ring-3"></span>
      <span class="glow"></span>
      <span class="hero-mark">${seoulMark(96)}</span>
    </div>`;
}

// Step 2 artwork: a live schematic of the window. The rail column is driven by
// a CSS custom property so choosing a layout animates the diagram with the same
// easing the real rail uses, instead of swapping a static picture.
export function layoutArt(collapsed: boolean) {
  return html`
    <div class="art art-layout ${collapsed ? 'is-collapsed' : ''}"
        aria-hidden="true">
      <div class="win">
        <div class="win-rail">
          <span class="rail-dot"></span>
          <span class="rail-line"></span>
          <span class="rail-line short"></span>
          <span class="rail-line"></span>
        </div>
        <div class="win-page">
          <span class="page-line w70"></span>
          <span class="page-line w90"></span>
          <span class="page-line w50"></span>
        </div>
      </div>
    </div>`;
}

// Step 3 artwork: requests arriving from the right, the blocked ones stopping
// dead at the shield. Deliberately shows both outcomes - a shield that stops
// everything would misdescribe what a blocker does.
export function blockingArt() {
  return html`
    <div class="art art-blocking" aria-hidden="true">
      <div class="shield">
        <svg viewBox="0 0 32 32" fill="none" width="34" height="34">
          <path d="M16 3 5 7v9c0 7 4.7 11.6 11 13 6.3-1.4 11-6 11-13V7L16 3Z"
              stroke="currentColor" stroke-width="2.2"
              stroke-linejoin="round"></path>
        </svg>
      </div>
      <span class="tracer t1"></span>
      <span class="tracer t2"></span>
      <span class="tracer t3"></span>
      <span class="tracer t4 pass"></span>
    </div>`;
}
