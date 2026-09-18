// Seoul Canvas Design Lab - application composition and the patch reconciler.
// Wires the fixture loop: a goal enters the command bar, the lexical planner
// selects a registered fixture capability, the fixture runs and its payload
// is validated against the canonical contract, the Lab compiler emits a
// canonical surface, and the artifact renders. Every subsequent change - a
// representation switch, a synthetic stream batch - flows through the surface
// store as a canonical patch, and the reconciler re-renders ONLY the affected
// components: the artifact element, its margin receipt, and unaffected
// siblings keep their DOM identity, and focus, scroll, and selection survive.
//
// Layout: a worklog document, not a dashboard. Each run appends a numbered
// entry whose receipt is typeset as marginalia in a left margin column - the
// Tufte sidenote convention.

import type { AdaptiveSurface, ComponentType, SeriesPoint } from './protocol.js';
import { compileInterface } from './compiler.js';
import { convertToEntries } from './compiler.js';
import { capabilities, mergeStreamingRows, nextStreamRows, runTask } from './runtime.js';
import { renderCatalogIndex } from './catalog.js';
import { elt, renderComponent } from './renderers.js';
import { buildRepresentationPatch, renderRepresentationRow } from './representation.js';
import { provenanceMarginNotes, resultNotices, marginNote } from './provenance-ui.js';
import { receiptMarginNotes } from './receipts.js';
import { summarizeResult } from './voice-summary.js';
import { createAppState, entryBySurfaceId, type AppState, type WorklogEntry } from './state.js';
import { findComponentById, parentOfComponent } from './surface-store.js';

interface FocusCapture {
  activeId: string | null;
  selectionStart: number | null;
  selectionEnd: number | null;
  windowX: number;
  windowY: number;
  stackScroll: number;
}

export function mountCanvas(root: HTMLElement): void {
  const state: AppState = createAppState();

  root.innerHTML = '';
  const app = elt('div', 'app');

  // Masthead.
  const mast = elt('header', 'masthead');
  const brand = elt('div', 'brand');
  brand.appendChild(elt('span', 'brand-title', 'Seoul'));
  brand.appendChild(elt('span', 'brand-sub', 'canvas design lab'));
  mast.appendChild(brand);
  const mastRight = elt('div', 'mast-right');
  mastRight.appendChild(elt('span', 'mast-note', `${capabilities().length} fixture capabilities · synthetic demo data`));
  // Voice replies: explicit toggle, default OFF. The Lab speaks through the
  // OS speech engine (preferring a female en voice) - clearly NOT the
  // measured Seoul Voice Pack; it exists so the ask -> see -> hear loop is
  // felt end to end. The spoken string is exactly the rendered insight line.
  let voiceOn = false;
  const pickVoice = (): SpeechSynthesisVoice | undefined => {
    const voices = window.speechSynthesis?.getVoices() ?? [];
    return (
      voices.find((v) => /samantha|ava|karen|female/i.test(v.name) && v.lang.startsWith('en')) ??
      voices.find((v) => v.lang.startsWith('en'))
    );
  };
  const speak = (text: string) => {
    if (!voiceOn || !('speechSynthesis' in window) || !text) return;
    window.speechSynthesis.cancel(); // a new answer interrupts the old one
    const utterance = new SpeechSynthesisUtterance(text);
    const voice = pickVoice();
    if (voice) utterance.voice = voice;
    utterance.rate = 1.02;
    window.speechSynthesis.speak(utterance);
  };
  const voiceBtn = elt('button', 'ghost-btn', 'Voice off') as HTMLButtonElement;
  voiceBtn.setAttribute('aria-pressed', 'false');
  voiceBtn.title = 'Speak each answer aloud (Lab OS voice, not the Seoul Voice Pack)';
  voiceBtn.addEventListener('click', () => {
    voiceOn = !voiceOn;
    voiceBtn.textContent = voiceOn ? 'Voice on' : 'Voice off';
    voiceBtn.setAttribute('aria-pressed', String(voiceOn));
    if (!voiceOn) window.speechSynthesis?.cancel();
  });
  mastRight.appendChild(voiceBtn);
  const themeBtn = elt('button', 'ghost-btn', 'Light') as HTMLButtonElement;
  themeBtn.title = 'Toggle theme';
  themeBtn.addEventListener('click', () => {
    const dark = document.documentElement.dataset.theme !== 'light';
    document.documentElement.dataset.theme = dark ? 'light' : 'dark';
    themeBtn.textContent = dark ? 'Dark' : 'Light';
  });
  mastRight.appendChild(themeBtn);
  mast.appendChild(mastRight);
  app.appendChild(mast);

  // The worklog document.
  const stack = elt('main', 'stack');
  const lede = elt('div', 'doc-lede');
  lede.appendChild(elt('p', 'lede-line', 'A goal goes in. The interface compiles itself from the shape of what comes back.'));
  stack.appendChild(lede);
  stack.appendChild(renderCatalogIndex((goal) => { input.value = goal; run(); }));
  app.appendChild(stack);

  // Bottom command bar - the single input to the fixture loop.
  const bar = elt('footer', 'command-bar');
  bar.appendChild(elt('span', 'prompt-mark', '›'));
  const input = elt('input', 'composer-input') as HTMLInputElement;
  input.type = 'text';
  input.placeholder = 'State a goal - "show the pipeline latency timeline"';
  input.setAttribute('aria-label', 'Goal');
  bar.appendChild(input);
  const runBtn = elt('button', 'primary-btn', 'Run') as HTMLButtonElement;
  bar.appendChild(runBtn);
  app.appendChild(bar);

  root.appendChild(app);

  // --- Focus / scroll / selection preservation -------------------------------

  function captureFocus(): FocusCapture {
    const active = document.activeElement as HTMLElement | null;
    let selectionStart: number | null = null;
    let selectionEnd: number | null = null;
    if (active instanceof HTMLInputElement || active instanceof HTMLTextAreaElement) {
      selectionStart = active.selectionStart;
      selectionEnd = active.selectionEnd;
    }
    return {
      activeId: active?.dataset?.['seoulFocus'] ?? null,
      selectionStart,
      selectionEnd,
      windowX: window.scrollX,
      windowY: window.scrollY,
      stackScroll: stack.scrollTop,
    };
  }

  function restoreFocus(capture: FocusCapture): void {
    // The renderer owns restoration; neither CSS smooth scrolling nor browser
    // anchoring may continue moving this position on a later frame.
    window.scrollTo({ left: capture.windowX, top: capture.windowY, behavior: 'instant' });
    stack.scrollTo({ top: capture.stackScroll, behavior: 'instant' });
    if (capture.activeId !== null) {
      const el = root.querySelector<HTMLElement>(`[data-seoul-focus="${capture.activeId}"]`);
      if (el && document.activeElement !== el) el.focus({ preventScroll: true });
      const el2 = document.activeElement;
      if (
        (el2 instanceof HTMLInputElement || el2 instanceof HTMLTextAreaElement) &&
        capture.selectionStart !== null
      ) {
        el2.setSelectionRange(capture.selectionStart, capture.selectionEnd);
      }
    }
  }

  // --- The patch reconciler ---------------------------------------------------
  // Applies a canonical patch through the store, then updates only the DOM the
  // change summary names. Rejected patches change nothing and are reported.

  function applyPatchToEntry(entry: WorklogEntry, patchDoc: unknown): string | null {
    const capture = captureFocus();
    const outcome = state.store.applyPatch(patchDoc);
    if (!outcome.ok) return outcome.error;
    const surface = outcome.surface;

    const body = entry.el.querySelector('.artifact-body');
    if (!body) return 'entry body missing';

    const rerendered = new Set<string>();
    const rerender = (componentId: string): void => {
      if (rerendered.has(componentId)) return;
      const node = findComponentById(surface, componentId);
      const existing = body.querySelector(`[data-seoul-component="${componentId}"]`);
      if (!node) {
        existing?.remove();
        rerendered.add(componentId);
        return;
      }
      if (existing) {
        existing.replaceWith(renderComponent(node, surface));
        rerendered.add(componentId);
        return;
      }
      // Newly appended: render under its parent's element (or the body for a
      // top-level component).
      const parent = parentOfComponent(surface, componentId);
      const parentEl = parent ? body.querySelector(`[data-seoul-component="${parent.id}"]`) : body;
      if (parent && parentEl) {
        // Re-render the parent once; that includes the new child.
        rerender(parent.id);
        return;
      }
      parentEl?.appendChild(renderComponent(node, surface));
      rerendered.add(componentId);
    };

    for (const id of outcome.applied.changed_component_ids) rerender(id);

    if (outcome.applied.changed_entry_names.length > 0) {
      // Re-render every component bound to a changed entry (and not already
      // re-rendered above).
      const changed = new Set(outcome.applied.changed_entry_names);
      const visit = (nodes: AdaptiveSurface['components']): void => {
        for (const node of nodes) {
          if (Object.values(node.bindings ?? {}).some((e) => changed.has(e))) rerender(node.id);
          visit(node.children ?? []);
        }
      };
      visit(surface.components);
    }

    if (outcome.applied.title_changed) {
      const title = entry.el.querySelector('.artifact-title');
      if (title) title.textContent = surface.title ?? '';
    }

    restoreFocus(capture);
    return null;
  }

  // --- Entry construction -----------------------------------------------------

  function makeEntry(): { entry: HTMLElement; margin: HTMLElement; content: HTMLElement } {
    const entry = elt('article', 'artifact');
    const margin = elt('div', 'artifact-margin');
    const content = elt('div', 'artifact-content');
    entry.appendChild(margin);
    entry.appendChild(content);
    return { entry, margin, content };
  }

  function switchRepresentation(entry: WorklogEntry, rep: ComponentType): void {
    const { patch, reasons } = buildRepresentationPatch(
      entry.result,
      entry.surfaceId,
      rep,
      state.store.get(entry.surfaceId)?.title ?? '',
    );
    const error = applyPatchToEntry(entry, patch);
    if (error) return; // the store left the surface untouched
    // Update the switch row's active state and the margin reason note only.
    const surface = state.store.get(entry.surfaceId)!;
    const row = entry.el.querySelector('.rep-row');
    const replacement = renderRepresentationRow(entry.result, surface.components[0].type, (r) => switchRepresentation(entry, r));
    if (row && replacement) row.replaceWith(replacement);
    const reason = entry.el.querySelector('.m-reason');
    if (reason && reasons[0]) reason.textContent = reasons[0].replace(/_/g, ' ');
  }

  function appendStreamControls(entry: WorklogEntry, content: HTMLElement): void {
    if (!entry.capability.streamAppendRows?.length) return;
    const controls = elt('div', 'stream-controls');
    const btn = elt('button', 'rep-btn', 'Receive next synthetic batch') as HTMLButtonElement;
    btn.dataset['seoulFocus'] = `stream-${entry.surfaceId}`;
    btn.addEventListener('click', () => {
      // One source of truth: the same shifted rows feed the rows-table upsert
      // AND the per-measure series appends, so chart and table always agree.
      const appendedRows = nextStreamRows(entry.capability, entry.streamBatches);
      if (appendedRows.length === 0) return;
      const merged = mergeStreamingRows(entry.result, appendedRows);
      if (!merged) return;
      entry.result = merged;
      const entries = convertToEntries(merged);
      const schema = entry.capability.result.schema;
      const temporal = (schema.fields ?? []).find((f) => f.role === 'timestamp');
      const seriesOps = (schema.fields ?? [])
        .filter((f) => f.id !== temporal?.id && (f.primitive === 'number' || f.primitive === 'integer'))
        .map((f) => ({
          op: 'append_series_points',
          entry: `series_${f.id}`,
          points: appendedRows
            .filter((row) => typeof row[temporal!.id] === 'number' && typeof row[f.id] === 'number')
            .map((row) => ({ t_ms: row[temporal!.id] as number, y: row[f.id] as number }) satisfies SeriesPoint),
        }))
        .filter((op) => op.points.length > 0);
      const ops: Record<string, unknown>[] = [
        { op: 'upsert_data_entry', entry: 'rows', value: entries['rows'] },
        ...seriesOps,
      ];
      const error = applyPatchToEntry(entry, { surface_id: entry.surfaceId, ops });
      if (!error) entry.streamBatches++;
    });
    controls.appendChild(btn);
    content.appendChild(controls);
  }

  function renderEntry(entryNo: number, run: ReturnType<typeof runTask>): HTMLElement | null {
    if (!run.result || !run.capability) return null;
    const compiled = compileInterface(run.result, { title: titleFor(run) });
    state.store.put(compiled.surface);

    const { entry, margin, content } = makeEntry();
    const record: WorklogEntry = {
      entryNo,
      surfaceId: compiled.surface.id!,
      snapshot: run.snapshot,
      result: run.result,
      capability: run.capability,
      streamBatches: 0,
      el: entry,
    };
    state.entries.unshift(record);

    receiptMarginNotes(entryNo, run.snapshot).forEach((n) => margin.appendChild(n));
    provenanceMarginNotes(run.result).forEach((n) => margin.appendChild(n));
    if (compiled.reasons[0]) margin.appendChild(marginNote('m-reason', compiled.reasons[0].replace(/_/g, ' ')));

    content.appendChild(elt('h2', 'artifact-title', compiled.surface.title ?? ''));
    const insight = summarizeResult(run.result);
    const insightEl = elt('p', 'artifact-insight', insight);
    insightEl.setAttribute('aria-live', 'polite');
    content.appendChild(insightEl);
    speak(insight);
    const repRow = renderRepresentationRow(run.result, compiled.representation, (rep) => switchRepresentation(record, rep));
    if (repRow) content.appendChild(repRow);
    resultNotices(run.result).forEach((n) => content.appendChild(n));

    const body = elt('div', 'artifact-body');
    for (const component of compiled.surface.components) {
      body.appendChild(renderComponent(component, compiled.surface));
    }
    content.appendChild(body);
    appendStreamControls(record, content);
    return entry;
  }

  function titleFor(run: ReturnType<typeof runTask>): string {
    const name = run.capability?.descriptor.name;
    if (name) return name.replace(/\b\w/g, (m) => m.toUpperCase());
    return run.goal;
  }

  function run(): void {
    const goal = input.value.trim();
    if (!goal) return;
    state.taskCount++;
    const task = runTask(goal);
    if (!task.result || !task.capability) {
      // Honest failure entry, not a crash.
      const { entry, margin, content } = makeEntry();
      entry.classList.add('artifact-fail');
      margin.appendChild(marginNote('m-num', String(state.taskCount).padStart(3, '0')));
      margin.appendChild(marginNote('m-bad', 'no match'));
      content.appendChild(elt('h2', 'artifact-title', 'No capability matched'));
      content.appendChild(elt('div', 'artifact-body fail-body', task.failure || 'Planning failed.'));
      stack.appendChild(entry);
      finishRun();
      return;
    }
    const entry = renderEntry(state.taskCount, task);
    if (entry) stack.appendChild(entry);
    finishRun();
  }

  function finishRun(): void {
    input.value = '';
    stack.scrollTo({ top: stack.scrollHeight, behavior: 'instant' });
    input.focus({ preventScroll: true });
  }

  input.dataset['seoulFocus'] = 'composer';
  runBtn.addEventListener('click', run);
  input.addEventListener('keydown', (ev) => {
    if (ev.key === 'Enter') { ev.preventDefault(); run(); }
  });

  // A first entry so the document is never blank on open.
  input.value = 'show the pipeline latency timeline';
  run();

  // Test hook: lets the smoke test drive patches directly and inspect
  // revisions without reaching into module internals.
  (root as HTMLElement & { seoulDesignLab?: object }).seoulDesignLab = {
    applyPatch: (surfaceId: string, patch: unknown) => {
      const entry = entryBySurfaceId(state, surfaceId);
      return entry ? applyPatchToEntry(entry, patch) : 'unknown surface';
    },
    surfaces: () => state.entries.map((e) => ({ surfaceId: e.surfaceId, revision: state.store.revision(e.surfaceId) })),
  };
}
