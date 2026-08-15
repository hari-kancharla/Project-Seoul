// Project Seoul first run - Lit template.
//
// One idea per screen, full-bleed. `--i` on each arriving element orders the
// entrance stagger; the artwork carries the motion so the copy can stay still
// and be read.

import {html, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import type {SeoulWelcomeAppElement} from './welcome.js';
import {blockingArt, heroArt, layoutArt, seoulMark} from './welcome_marks.js';

export function getHtml(this: SeoulWelcomeAppElement) {
  // Nothing paints until the browser has answered. A first-run screen that
  // renders defaults and then corrects itself is the first thing a new user
  // sees flicker.
  if (!this.state_) {
    return html`<div class="stage"></div>`;
  }

  const lockup = html`
    <div class="lockup">
      ${seoulMark(26)}<span class="wordmark">Seoul</span>
    </div>`;

  if (this.finished_()) {
    return html`
      <div class="stage">
        ${lockup}
        <div class="centre">
          <div class="panel">
            <div class="art art-hero">
              <span class="ring ring-1"></span>
              <span class="ring ring-2"></span>
              <span class="glow"></span>
              <span class="hero-mark">${seoulMark(76)}</span>
            </div>
            <h1 class="rise" style="--i:1">Ready.</h1>
            <p class="lede rise" style="--i:2">
              Everything here stays changeable from Studio.
            </p>
          </div>
        </div>
        <div class="controls"></div>
      </div>`;
  }

  const dots = this.state_.allSteps.map((id, i) => {
    const cls = i === this.stepIndex_() ? 'on' :
        this.state_!.completedSteps.includes(id) ? 'done' : '';
    return html`<span class="dot ${cls}"></span>`;
  });

  const last = this.stepIndex_() === this.stepCount_() - 1;

  return html`
    <div class="stage">
      ${lockup}
      <div class="centre">
        <div class="panel">

          ${this.isStep_('welcome') ? html`
            ${heroArt()}
            <h1 class="rise" style="--i:1">A browser that pays attention.</h1>
            <p class="lede rise" style="--i:2">
              Seoul keeps your work in <strong>workspaces</strong>, blocks the
              advertising industry <strong>by default</strong>, and takes
              instructions in plain language.
            </p>
            <div class="claims">
              <div class="claim rise" style="--i:3">
                <b>Workspaces</b>
                <span>A vertical rail that holds context, not forty tabs.</span>
              </div>
              <div class="claim rise" style="--i:4">
                <b>Blocking built in</b>
                <span>Native engine in the browser, not an extension.</span>
              </div>
              <div class="claim rise" style="--i:5">
                <b>Ask, don't hunt</b>
                <span>A command surface for what you actually want done.</span>
              </div>
            </div>` : nothing}

          ${this.isStep_('appearance') ? html`
            ${layoutArt(this.state_.railCollapsed)}
            <h1 class="rise" style="--i:1">Choose your rail.</h1>
            <p class="lede rise" style="--i:2">
              This changes the window behind you as you pick, so you can see
              which one you want.
            </p>
            <div class="choices">
              <button class="choice rise ${!this.state_.railCollapsed ? 'on' : ''}"
                  style="--i:3" data-rail="expanded"
                  @click="${this.onRailChoice_}">
                <b>Expanded</b>
                <span>Titles always visible.</span>
              </button>
              <button class="choice rise ${this.state_.railCollapsed ? 'on' : ''}"
                  style="--i:4" data-rail="collapsed"
                  @click="${this.onRailChoice_}">
                <b>Compact</b>
                <span>Slides open when you reach for it.</span>
              </button>
            </div>` : nothing}

          ${this.isStep_('browsing') ? html`
            ${blockingArt()}
            <h1 class="rise" style="--i:1">Already blocking.</h1>
            <p class="lede rise" style="--i:2">
              Nothing to install and nothing to switch on. This is the state of
              the blocker in this session, right now.
            </p>
            <div class="status rise ${this.blockingIsFull_() ? '' : 'weak'}"
                style="--i:3">
              <span class="pip"></span>${this.blockingSummary_()}
            </div>
            ${this.state_.isDefaultBrowser ? html`
              <p class="settled rise" style="--i:4">
                Seoul is already your default browser.
              </p>` :
              this.state_.canSetDefaultBrowser ? html`
              <div class="offer">
                <button class="secondary rise" style="--i:4"
                    ?disabled="${this.busy_}" @click="${this.onMakeDefault_}">
                  Make Seoul my default browser
                </button>
              </div>
              ${this.defaultBrowserAnswer_ ? html`
                <p class="settled">${this.defaultBrowserAnswer_}</p>` :
                nothing}` : nothing}` : nothing}

        </div>
      </div>

      <div class="controls">
        <div class="dots">${dots}</div>
        <div class="buttons">
          <button class="ghost" @click="${this.onSkip_}">Skip</button>
          <button class="primary" ?disabled="${this.busy_}"
              @click="${this.onNext_}">
            ${last ? 'Start browsing' : 'Continue'}
          </button>
        </div>
      </div>
    </div>`;
}
