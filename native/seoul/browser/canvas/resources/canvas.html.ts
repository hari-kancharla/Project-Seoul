// Project Seoul Canvas Lit template. Kept separate so Chromium's checked-in
// html.ts tooling can audit the trusted template boundary.

import {html, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import type {SeoulCanvasAppElement} from './canvas.js';

export function getHtml(this: SeoulCanvasAppElement) {
  const activeTasks = this.activeTasks_();
  const voiceActive = [
    'connecting', 'microphone_requesting', 'listening', 'hearing', 'thinking',
    'speaking', 'working',
  ].includes(this.voiceState_);
  return html`<!--_html_template_start_-->
    <main id="canvas-root" aria-live="polite">
      <header class="canvas-header">
        <div><span class="eyebrow">SEOUL CANVAS</span>
          <h1>${this.selectedView_ === 'canvas' ?
              this.surface_?.title || 'Ask, act, understand.' :
          this.selectedView_ === 'chat' ?
              this.thread_.name || 'Project chat' :
          this.selectedView_ === 'boosts' ? 'Make the web fit you.' :
          this.selectedView_ === 'studio' ? 'Shape how Seoul works.' :
              'Your thinking space'}</h1></div>
        <span class="route" data-voice-configured="${this.voiceConfigured_}"
            aria-live="polite"><span aria-hidden="true"></span>${this.routeLabel_}</span>
      </header>

      <nav class="view-switcher" aria-label="Canvas views">
        ${([
          'canvas',
          ...(this.activeThreadId_ ? ['chat'] as const : []),
          'boosts', 'library', 'boards', 'studio',
        ] as const).map(view => html`
          <button type="button" aria-current="${this.selectedView_ === view ? 'page' : 'false'}"
              @click="${() => this.selectView_(view)}">${view[0]!.toUpperCase() + view.slice(1)}</button>`)}
      </nav>

      ${this.selectedView_ === 'canvas' ? this.renderPageContext_() : nothing}

      ${this.selectedView_ === 'canvas' && activeTasks.length ? html`<section class="task-ribbon" aria-label="Live tasks">
        ${activeTasks.map(task => html`<article class="task-row">
          <span class="task-state task-${task.state}">${task.state.replace(/_/g, ' ')}</span>
          <span class="task-goal">${task.goal}</span>
          <span class="task-controls">
            ${task.state === 'executing' ? html`<button @click="${() => this.taskControl_(task, 'pause')}">Pause</button>` : nothing}
            ${task.state === 'paused' ? html`<button @click="${() => this.taskControl_(task, 'resume')}">Resume</button>` : nothing}
            ${task.state === 'awaiting_approval' && !task.pending_user_input ? html`
              <button class="primary" @click="${() => this.taskControl_(task, 'approve')}">Approve</button>
              <button @click="${() => this.taskControl_(task, 'reject')}">Reject</button>` : nothing}
            ${task.state !== 'failed' ? html`<button @click="${() => this.taskControl_(task, 'cancel')}">Cancel</button>` : nothing}
          </span>
          ${task.pending_approval_prompt ? html`<p class="task-prompt">${task.pending_approval_prompt}</p>` : nothing}
          ${task.pending_user_input ? html`<form class="task-input"
              @submit="${(event: Event) => { event.preventDefault(); this.provideTaskInput_(task); }}">
            <input aria-label="Input for ${task.goal}" placeholder="Type the missing detail"
                .value="${this.taskInputs_[task.id] ?? ''}"
                @input="${(event: Event) => this.onTaskInput_(task, event)}">
            <button class="primary" type="submit"
                ?disabled="${!(this.taskInputs_[task.id] ?? '').trim()}">Continue</button>
          </form>` : nothing}
        </article>`)}
      </section>` : nothing}

      <section class="canvas-content" aria-label="Result surface">
        ${this.selectedView_ === 'chat' ? this.renderThread_() :
          this.selectedView_ === 'boosts' ? this.renderBoosts_() :
          this.selectedView_ === 'library' ? this.renderLibrary_() :
          this.selectedView_ === 'boards' ? this.renderBoards_() :
          this.selectedView_ === 'studio' ? this.renderStudio_() :
          this.surface_ ? html`<div class="saui-surface">
            ${this.surface_.components.map(component => this.renderComponent_(component))}
          </div>` : html`<div class="idle">
            <section class="idle-lede">
              <div class="idle-signal" aria-hidden="true">
                <span class="signal-orbit orbit-outer"></span>
                <span class="signal-orbit orbit-inner"></span>
                <span class="signal-core"></span>
              </div>
              <span class="idle-kicker">BROWSER-NATIVE INTELLIGENCE</span>
              <h2>Turn intent into visible work.</h2>
              <p>Seoul reads the browser context you choose, shows its plan, asks before
                impactful actions, and leaves a receipt.</p>
              <div class="prompt-list" aria-label="Starter commands">
                <button type="button"
                    @click="${() => this.usePrompt_('List the open tabs in this window')}">
                  <span class="prompt-index">01</span>
                  <span><strong>Inventory this window</strong>
                    <small>See the live tab set as structured context</small></span>
                  <span class="prompt-arrow" aria-hidden="true">↗</span>
                </button>
                <button type="button"
                    @click="${() => this.usePrompt_('Read the current page')}">
                  <span class="prompt-index">02</span>
                  <span><strong>Understand this page</strong>
                    <small>Observe its semantic structure, not raw pixels</small></span>
                  <span class="prompt-arrow" aria-hidden="true">↗</span>
                </button>
                <button type="button"
                    @click="${() => this.usePrompt_('Open https://example.com in a new tab')}">
                  <span class="prompt-index">03</span>
                  <span><strong>Try a browser action</strong>
                    <small>Preview a typed mutation before it runs</small></span>
                  <span class="prompt-arrow" aria-hidden="true">↗</span>
                </button>
              </div>
            </section>

            <aside class="control-card" aria-label="How Seoul works">
              <header>
                <span>THE CONTROL LOOP</span>
                <span class="control-status"><i></i> Ready</span>
              </header>
              <ol>
                <li>
                  <span class="control-number">01</span>
                  <div><strong>Observe</strong>
                    <p>Bounded page and tab context enters the Canvas.</p></div>
                </li>
                <li>
                  <span class="control-number">02</span>
                  <div><strong>Plan</strong>
                    <p>Typed capabilities make every proposed step inspectable.</p></div>
                </li>
                <li>
                  <span class="control-number">03</span>
                  <div><strong>Approve</strong>
                    <p>Impactful changes stop for a clear decision.</p></div>
                </li>
                <li>
                  <span class="control-number">04</span>
                  <div><strong>Verify</strong>
                    <p>Observed outcomes return as durable receipts.</p></div>
                </li>
              </ol>
              <footer><span aria-hidden="true">◇</span>
                Your browser remains the source of truth.</footer>
            </aside>
          </div>`}
      </section>
    </main>

    ${this.voiceError_ ? html`<div class="voice-error" role="alert">
      <span>${this.voiceError_}</span>
      <button type="button" aria-label="Dismiss voice error"
          @click="${() => this.voiceError_ = ''}">×</button>
    </div>` : nothing}

    <footer class="composer">
      <button class="voice-button" type="button" aria-label="Toggle voice input"
          aria-pressed="${voiceActive}" data-state="${this.voiceState_}"
          data-configured="${this.voiceConfigured_}" @click="${this.toggleVoice_}">
        <span class="voice-dot" aria-hidden="true"></span><span>Voice</span>
      </button>
      <input type="text" aria-label="Message Seoul" placeholder="Ask or act…"
          .value="${this.inputValue_}" @input="${this.onInput_}"
          @keydown="${this.onInputKeydown_}">
      <button class="send-button" type="button" aria-label="Send message"
          ?disabled="${!this.inputValue_.trim()}" @click="${this.submitTurn_}">↑</button>
    </footer>
  <!--_html_template_end_-->`;
}
