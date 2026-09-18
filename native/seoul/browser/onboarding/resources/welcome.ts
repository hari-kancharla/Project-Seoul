// Project Seoul first run - trusted Chromium Lit WebUI.
//
// Three screens, each of which changes something real. The appearance step
// moves the actual rail in the window behind this surface as the choice is
// made, and the browsing step reports what the blocker has actually loaded
// rather than what the product would like to claim.

import {CrLitElement} from '//resources/lit/v3_0/lit.rollup.js';

import {PageHandlerFactory, PageHandlerRemote} from './welcome.mojom-webui.js';
import type {WelcomeState} from './welcome.mojom-webui.js';
import {getCss} from './welcome.css.js';
import {getHtml} from './welcome.html.js';

export class SeoulWelcomeAppElement extends CrLitElement {
  static get is() {
    return 'seoul-welcome-app';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      state_: {type: Object},
      busy_: {type: Boolean},
      error_: {type: String},
      // Set once the platform has answered a default-browser request, so the
      // step can report the real outcome instead of implying success.
      defaultBrowserAnswer_: {type: String},
    };
  }

  protected accessor state_: WelcomeState|null = null;
  protected accessor busy_: boolean = false;
  protected accessor error_: string = '';
  protected accessor defaultBrowserAnswer_: string = '';

  private handler_: PageHandlerRemote = new PageHandlerRemote();

  override connectedCallback() {
    super.connectedCallback();
    PageHandlerFactory.getRemote().createPageHandler(
        this.handler_.$.bindNewPipeAndPassReceiver());
    this.refresh_();
  }

  protected async refresh_() {
    try {
      const {state} = await this.handler_.getState();
      this.state_ = state;
      this.error_ = '';
    } catch {
      this.error_ = 'Setup could not be loaded. Please try again.';
    }
  }

  // The index of the current step, for the progress dots. Derived from the
  // browser's own ordering rather than a list duplicated here, so adding a step
  // in C++ cannot leave this rendering the wrong count.
  protected stepIndex_(): number {
    if (!this.state_) {
      return 0;
    }
    const index = this.state_.allSteps.indexOf(this.state_.currentStep);
    return index < 0 ? this.state_.allSteps.length : index;
  }

  protected stepCount_(): number {
    return this.state_ ? this.state_.allSteps.length : 0;
  }

  protected isStep_(id: string): boolean {
    return !!this.state_ && this.state_.currentStep === id;
  }

  protected finished_(): boolean {
    return !!this.state_ && this.state_.currentStep === '';
  }

  // True only when the upstream lists are live. The baseline alone is a
  // materially weaker promise and the pip says so rather than showing green
  // for both.
  protected blockingIsFull_(): boolean {
    return !!this.state_ && this.state_.blockingEnabled &&
        (this.state_.filterSource === 'catalogue' ||
         this.state_.filterSource === 'verified-component');
  }

  protected blockingSummary_(): string {
    if (!this.state_ || !this.state_.blockingEnabled) {
      return 'Blocking is not active in this session.';
    }
    if (this.state_.filterSource === 'catalogue') {
      return 'Blocking ads and trackers with EasyList and EasyPrivacy.';
    }
    if (this.state_.filterSource === 'verified-component') {
      return 'Blocking ads and trackers with the verified filter component.';
    }
    return 'Baseline protection active. The full lists download in the ' +
        'background the first time you are online.';
  }

  protected async onNext_() {
    if (!this.state_ || this.busy_) {
      return;
    }
    this.busy_ = true;
    try {
      const {state} = await this.handler_.completeStep(this.state_.currentStep);
      this.state_ = state;
      if (this.finished_()) this.onStartBrowsing_();
    } catch {
      this.error_ = 'Your choice could not be saved. Please try again.';
    } finally {
      this.busy_ = false;
    }
  }

  protected async onSkip_() {
    if (this.busy_) return;
    this.busy_ = true;
    try {
      await this.handler_.skip();
      this.onStartBrowsing_();
    } catch {
      this.error_ = 'Setup could not be skipped. Please try again.';
    } finally {
      this.busy_ = false;
    }
  }

  protected onStartBrowsing_() {
    window.location.replace('about:blank');
  }

  protected async onRailChoice_(e: Event) {
    if (this.busy_) return;
    const collapsed = (e.currentTarget as HTMLElement).dataset['rail'] ===
        'collapsed';
    this.busy_ = true;
    try {
      const {accepted, state} = await this.handler_.setRailCollapsed(collapsed);
      this.state_ = state;
      this.error_ = accepted ? '' :
          'The sidebar could not be changed. Try again after leaving the active scene.';
    } catch {
      this.error_ = 'Your sidebar choice could not be saved. Please try again.';
    } finally {
      this.busy_ = false;
    }
  }

  protected async onMakeDefault_() {
    if (this.busy_) {
      return;
    }
    this.busy_ = true;
    try {
      const {isDefaultNow} = await this.handler_.requestDefaultBrowser();
      this.defaultBrowserAnswer_ = isDefaultNow ?
          'Seoul is now your default browser.' :
          'Left unchanged. You can set this later in System Settings.';
      await this.refresh_();
    } catch {
      this.error_ = 'The default browser request did not finish. Please try again.';
    } finally {
      this.busy_ = false;
    }
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'seoul-welcome-app': SeoulWelcomeAppElement;
  }
}

customElements.define(SeoulWelcomeAppElement.is, SeoulWelcomeAppElement);
