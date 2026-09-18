// Project Seoul Canvas Lit template. Kept separate so Chromium's checked-in
// html.ts tooling can audit the trusted template boundary.

import {html, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import type {SeoulCanvasAppElement} from './canvas.js';

const toolGlyphs: Record<string, string> = {
  canvas: 'M5 5h14v10H9l-4 4V5z',
  library: 'M4 5h4v14H4z M10 5h4v14h-4z M17 5l3 13',
  graph: 'M7 7l10 3M7 7l3 11M17 10l-7 8 M5 5h4v4H5z M15 8h4v4h-4z M8 16h4v4H8z',
  boards: 'M4 4h7v10H4z M14 4h6v6h-6z M14 13h6v7h-6z M4 17h7v3H4z',
  boosts: 'M10 3l2.5 6.5L19 12l-6.5 2.5L10 21l-2.5-6.5L1 12l6.5-2.5L10 3z M19 2v5 M16.5 4.5h5',
  studio: 'M4 6h16M4 12h16M4 18h16 M8 3v6M16 9v6M10 15v6',
  chat: 'M4 4h16v12H9l-5 4V4z',
};

export function getHtml(this: SeoulCanvasAppElement) {
  const activeTasks = this.activeTasks_();
  const voiceActive = [
    'connecting', 'microphone_requesting', 'listening', 'hearing', 'thinking',
    'speaking', 'working',
  ].includes(this.voiceState_);
  return html`<!--_html_template_start_-->
    <main id="canvas-root">
      <header class="canvas-header">
        <div class="assistant-identity">
          <svg class="seoul-mark" width="28" height="28" viewBox="0 0 48 48"
              fill="none" aria-hidden="true">
            <rect x="3" y="3" width="42" height="42" rx="13"
                stroke="currentColor" stroke-width="2.5"></rect>
            <rect x="11" y="12" width="5" height="24" rx="2.5" fill="currentColor"></rect>
            <circle cx="30" cy="24" r="7.5" stroke="currentColor" stroke-width="2.5"></circle>
            <circle cx="30" cy="24" r="2.5" fill="currentColor"></circle>
          </svg>
          <h1 tabindex="-1">${this.selectedView_ === 'canvas' ? 'Seoul' :
              this.selectedView_ === 'chat' ? this.thread_.name || 'Chat' :
              this.selectedView_ === 'studio' ? 'Settings' :
              this.selectedView_ === 'graph' ? 'Context graph' :
              this.selectedView_ === 'boosts' ? 'Saved Boosts' :
              this.selectedView_ === 'boards' ? 'Boards' : 'Library'}</h1>
        </div>
        <nav class="assistant-navigation" aria-label="Seoul tools">
          ${this.selectedView_ !== 'canvas' ? html`
            <button type="button" class="quiet-button" aria-label="Back to assistant"
                @click="${() => this.selectView_('canvas')}">← Back</button>` : nothing}
          <details class="tools-menu" @keydown="${this.onToolsMenuKeydown_}">
            <summary aria-label="Open Seoul tools" title="Seoul tools"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" aria-hidden="true"><rect x="4" y="4" width="6" height="6" rx="1.5"></rect><rect x="14" y="4" width="6" height="6" rx="1.5"></rect><rect x="4" y="14" width="6" height="6" rx="1.5"></rect><rect x="14" y="14" width="6" height="6" rx="1.5"></rect></svg></summary>
            <div class="tools-menu-items">
              ${([
                ['canvas', 'Assistant'], ['library', 'Library'],
                ['graph', 'Context graph'],
                ['boards', 'Boards'], ['boosts', 'Saved Boosts'], ['studio', 'Settings'],
                ...(this.activeThreadId_ ? [['chat', 'Project chat']] : []),
              ] as const).map(([view, label]) => html`<button type="button"
                  data-view="${view}"
                  aria-current="${this.selectedView_ === view ? 'page' : 'false'}"
                  @click="${() => this.selectView_(view as typeof this.selectedView_)}"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="${toolGlyphs[view]}"></path></svg><span>${label}</span></button>`)}
            </div>
          </details>
          ${this.embedded_ ? html`<button type="button" class="quiet-button" aria-label="Close assistant"
              title="Close assistant" @click="${this.closeAssistant_}">×</button>` : nothing}
        </nav>
      </header>

      ${this.selectedView_ === 'canvas' ? this.renderPageContext_() : nothing}
      ${this.selectedView_ === 'canvas' && this.providerError_ ? html`
        <div class="saui-error" role="status"><p>${this.providerError_}</p>
          <button type="button" @click="${() => this.selectView_('studio')}">Check connections</button>
        </div>` : nothing}

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
            ${task.state !== 'failed' ? html`<button @click="${() => this.taskControl_(task, 'cancel')}">Cancel</button>` :
              html`<button @click="${() => this.taskControl_(task, 'dismiss')}">Dismiss</button>`}
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
        ${this.selectedView_ === 'graph' ? this.renderContextGraph_() :
          this.selectedView_ === 'chat' ? this.renderThread_() :
          this.selectedView_ === 'boosts' ? this.renderBoosts_() :
          this.selectedView_ === 'library' ? this.renderLibrary_() :
          this.selectedView_ === 'boards' ? this.renderBoards_() :
          this.selectedView_ === 'studio' ? this.renderStudio_() :
          this.surface_ ? html`<div class="saui-surface">
            ${this.surface_.components.map(component => this.renderComponent_(component))}
          </div>` : html`<div class="idle">
            <section class="idle-lede">
              <h2>How can I help?</h2>
              <p>Ask about your tabs or give Seoul a task.</p>
              <div class="prompt-list" aria-label="Starter commands">
                <button type="button"
                    @click="${() => this.usePrompt_('List the open tabs in this window')}">
                  <span><strong>Find my open tabs</strong>
                    <small>See what is open in this window</small></span>
                  <span class="prompt-arrow" aria-hidden="true">↗</span>
                </button>
                <button type="button"
                    @click="${() => this.usePrompt_(
                      'List the actions and editable fields available on the active page')}">
                  <span><strong>Inspect this page</strong>
                    <small>See headings and available controls</small></span>
                  <span class="prompt-arrow" aria-hidden="true">↗</span>
                </button>
                <button type="button" @click="${() => this.selectView_('library')}">
                  <span><strong>Pick up saved work</strong>
                    <small>Your notes, boards and results</small></span>
                  <span class="prompt-arrow" aria-hidden="true">↗</span>
                </button>
              </div>
            </section>
          </div>`}
      </section>
    </main>

    ${this.voiceError_ ? html`<div class="voice-error" role="alert">
      <span>${this.voiceError_}</span>
      <button type="button" aria-label="Dismiss voice error"
          @click="${() => this.voiceError_ = ''}">×</button>
    </div>` : nothing}

    <footer class="composer">
      ${voiceActive || this.microphoneLive_ ? html`<div class="composer-status" role="status">
        <span>${this.routeLabel_}</span>
        ${this.microphoneLive_ ? html`<span class="voice-privacy">Microphone on</span>` : nothing}
      </div>` : nothing}
      <button class="voice-button" type="button" aria-label="Toggle voice input"
          aria-pressed="${voiceActive}" data-state="${this.voiceState_}"
          data-configured="${this.voiceConfigured_}" @click="${this.toggleVoice_}">
        <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.6" aria-hidden="true"><rect x="9" y="3" width="6" height="12" rx="3"/><path d="M6 11v1a6 6 0 0 0 12 0v-1M12 18v3m-3 0h6"/></svg><span>${voiceActive ? 'Stop' : 'Voice'}</span>
      </button>
      <input type="text" aria-label="Message Seoul" placeholder="Ask Seoul…"
          .value="${this.inputValue_}" @input="${this.onInput_}"
          @keydown="${this.onInputKeydown_}">
      <button class="send-button" type="button" aria-label="Send message"
          ?disabled="${!this.inputValue_.trim()}" @click="${this.submitTurn_}">↑</button>
    </footer>
  <!--_html_template_end_-->`;
}
