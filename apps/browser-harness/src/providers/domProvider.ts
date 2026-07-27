/**
 * The universal fallback provider: any page that is real DOM.
 *
 * Deliberately holds NO DOM references between calls. Everything is resolved
 * from a TextQuoteAnchor at the moment of use (design rule 1), because the
 * pages this has to survive — virtualized feeds, React re-renders, editors that
 * rebuild their subtree on every keystroke — invalidate node handles constantly.
 *
 * atomicMutate is FALSE and that is not a temporary limitation. A multi-op edit
 * against raw DOM is a sequence of independent writes; if op 3 of 5 fails, ops
 * 1 and 2 have already landed. `undo` exists to make that recoverable, but
 * recoverable is not the same as atomic, and the assistant must be told the
 * difference before it decides whether to batch.
 */

import type {
  Capabilities,
  Coverage,
  DocumentProvider,
  EditOp,
  EditResult,
  LocateResult,
  Passage,
  TextQuoteAnchor,
  ViewportRect,
} from './types.ts';
import { coverageOf } from './types.ts';

// Node type constants, spelled numerically on purpose: the global `Node` does
// not exist under Node.js, and these tests run without a browser.
const ELEMENT_NODE = 1;
const TEXT_NODE = 3;

const SKIPPED_TAGS = new Set(['SCRIPT', 'STYLE', 'NOSCRIPT', 'TEMPLATE']);

interface TextSlice {
  node: Text;
  /** Offset of this node's first character within the flattened document text. */
  start: number;
  end: number;
}

interface ResolvedSpan {
  slices: TextSlice[];
  startNode: Text;
  startOffset: number;
  endNode: Text;
  endOffset: number;
}

/** Length of the longest common suffix of `a` and `b`. */
function commonSuffixLength(a: string, b: string): number {
  let n = 0;
  while (n < a.length && n < b.length && a[a.length - 1 - n] === b[b.length - 1 - n]) n += 1;
  return n;
}

/** Length of the longest common prefix of `a` and `b`. */
function commonPrefixLength(a: string, b: string): number {
  let n = 0;
  while (n < a.length && n < b.length && a[n] === b[n]) n += 1;
  return n;
}

export class DOMProvider implements DocumentProvider {
  readonly id = 'dom';

  readonly capabilities: Capabilities = {
    index: true,
    locate: true,
    mutate: true,
    // See the file header: raw DOM cannot promise all-or-nothing.
    atomicMutate: false,
  };

  private readonly injectedDoc: Document | undefined;

  constructor(doc?: Document) {
    this.injectedDoc = doc;
  }

  private get doc(): Document {
    const resolved = this.injectedDoc ?? (globalThis as { document?: Document }).document;
    if (!resolved) throw new Error('DOMProvider: no Document available');
    return resolved;
  }

  /** The universal fallback matches everything; the registry ranks it last. */
  matches(_url: string, _doc: Document): boolean {
    return true;
  }

  /**
   * Placeholder. The real harvest is M3a.
   *
   * Coverage is reported as INCOMPLETE rather than as an empty-but-complete
   * document — an empty passage list with `complete: true` would license the
   * assistant to say "that text is not on this page", which is precisely the
   * wrong answer while the harvester does not exist.
   */
  async index(): Promise<{ passages: Passage[]; coverage: Coverage }> {
    const coverage: Coverage = coverageOf(false, [], []);
    return { passages: [], coverage };
  }

  async locate(anchor: TextQuoteAnchor): Promise<LocateResult> {
    const span = this.resolve(anchor);
    if (!span) return { rects: [], reason: 'not-found', navigated: false };

    const doc = this.doc;
    if (typeof doc.createRange !== 'function') {
      // A surface with text but no Range support. Found, but unmeasurable.
      return { rects: [], reason: 'no-geometry', navigated: false };
    }

    const range = doc.createRange();
    range.setStart(span.startNode, span.startOffset);
    range.setEnd(span.endNode, span.endOffset);

    let rects = this.measure(range);
    let navigated = false;

    const view = doc.defaultView;
    if (rects.length > 0 && view && !this.anyRectInViewport(rects, view)) {
      // Off-screen but real: move the user to it, then re-measure. Scrolling is
      // the whole point of locate() on a long document.
      const host = span.startNode.parentElement;
      if (host && typeof host.scrollIntoView === 'function') {
        host.scrollIntoView({ block: 'center', inline: 'nearest' });
        navigated = true;
        rects = this.measure(range);
      }
    }

    if (rects.length === 0) {
      // Found in the text, but it paints to nothing measurable: display:none,
      // a zero-size container, or a surface without layout. Design rule 3 —
      // this is a success, not a miss.
      return { rects: [], reason: 'no-geometry', navigated };
    }

    if (view && !this.anyRectInViewport(rects, view)) {
      return { rects, reason: 'off-screen', navigated };
    }
    return { rects, reason: 'ok', navigated };
  }

  async mutate(ops: EditOp[]): Promise<EditResult> {
    // Snapshot every text node this batch will touch, before touching any of
    // them, so `undo` can put the document back even on a partial application.
    const snapshot = new Map<Text, string>();
    const remember = (node: Text) => {
      if (!snapshot.has(node)) snapshot.set(node, node.nodeValue ?? '');
    };
    const undo = async () => {
      for (const [node, value] of snapshot) node.nodeValue = value;
    };

    let appliedAny = false;

    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      const span = this.resolve(op.anchor);
      if (!span) {
        return {
          applied: appliedAny,
          atomic: false,
          reason: `op ${i} (${op.kind}): anchor not found`,
          undo: appliedAny ? undo : undefined,
        };
      }
      if (!this.isEditable(span.startNode)) {
        return {
          applied: appliedAny,
          atomic: false,
          reason: `op ${i} (${op.kind}): target is not editable`,
          undo: appliedAny ? undo : undefined,
        };
      }

      for (const slice of span.slices) remember(slice.node);

      if (op.kind === 'insertAfter') {
        const node = span.endNode;
        const value = node.nodeValue ?? '';
        node.nodeValue =
          value.slice(0, span.endOffset) + op.text + value.slice(span.endOffset);
      } else {
        const replacement = op.kind === 'replace' ? op.text : '';
        this.writeSpan(span, replacement);
      }
      appliedAny = true;
    }

    return {
      applied: appliedAny,
      // Never true for this provider, whatever happened. See the file header.
      atomic: false,
      undo: appliedAny ? undo : undefined,
    };
  }

  // MARK: - Internals

  /** Replaces everything between the span's endpoints with `replacement`. */
  private writeSpan(span: ResolvedSpan, replacement: string): void {
    if (span.startNode === span.endNode) {
      const value = span.startNode.nodeValue ?? '';
      span.startNode.nodeValue =
        value.slice(0, span.startOffset) + replacement + value.slice(span.endOffset);
      return;
    }
    const head = span.startNode.nodeValue ?? '';
    span.startNode.nodeValue = head.slice(0, span.startOffset) + replacement;
    for (const slice of span.slices) {
      if (slice.node === span.startNode || slice.node === span.endNode) continue;
      slice.node.nodeValue = '';
    }
    const tail = span.endNode.nodeValue ?? '';
    span.endNode.nodeValue = tail.slice(span.endOffset);
  }

  private measure(range: Range): ViewportRect[] {
    if (typeof range.getClientRects !== 'function') return [];
    const raw = Array.from(range.getClientRects());
    return raw
      .filter((r) => r.width > 0 && r.height > 0)
      .map((r) => ({ x: r.x, y: r.y, width: r.width, height: r.height }));
  }

  private anyRectInViewport(rects: ViewportRect[], view: Window): boolean {
    const w = view.innerWidth;
    const h = view.innerHeight;
    if (!w || !h) return true; // No viewport to judge against; do not claim off-screen.
    return rects.some(
      (r) => r.x < w && r.y < h && r.x + r.width > 0 && r.y + r.height > 0,
    );
  }

  /** Nearest ancestor that accepts text editing. */
  private isEditable(node: Node): boolean {
    let current: Node | null = node;
    while (current) {
      if (current.nodeType === ELEMENT_NODE) {
        const el = current as HTMLElement;
        if (el.isContentEditable === true) return true;
        const attr = typeof el.getAttribute === 'function' ? el.getAttribute('contenteditable') : null;
        if (attr === '' || attr === 'true') return true;
      }
      current = current.parentNode;
    }
    return this.doc.designMode === 'on';
  }

  private collect(root: Node): TextSlice[] {
    const slices: TextSlice[] = [];
    let cursor = 0;
    const visit = (node: Node): void => {
      if (node.nodeType === TEXT_NODE) {
        const text = node as Text;
        const value = text.nodeValue ?? '';
        if (value.length > 0) {
          slices.push({ node: text, start: cursor, end: cursor + value.length });
          cursor += value.length;
        }
        return;
      }
      if (node.nodeType === ELEMENT_NODE) {
        const tag = (node as Element).tagName;
        if (tag && SKIPPED_TAGS.has(tag.toUpperCase())) return;
      }
      const children = node.childNodes;
      if (!children) return;
      for (let i = 0; i < children.length; i += 1) visit(children[i]);
    };
    visit(root);
    return slices;
  }

  /**
   * Resolves an anchor to a concrete span.
   *
   * When `exact` occurs more than once, prefix and suffix agreement break the
   * tie; an exact tie falls back to document order so the same anchor always
   * resolves to the same occurrence.
   */
  private resolve(anchor: TextQuoteAnchor): ResolvedSpan | null {
    const doc = this.doc;
    const root: Node = doc.body ?? doc.documentElement ?? doc;
    if (!root) return null;
    if (!anchor.exact) return null;

    const slices = this.collect(root);
    if (slices.length === 0) return null;
    const flat = slices.map((s) => s.node.nodeValue ?? '').join('');

    let best = -1;
    let bestScore = -1;
    let from = 0;
    for (;;) {
      const at = flat.indexOf(anchor.exact, from);
      if (at === -1) break;
      const score =
        commonSuffixLength(flat.slice(0, at), anchor.prefix ?? '') +
        commonPrefixLength(flat.slice(at + anchor.exact.length), anchor.suffix ?? '');
      if (score > bestScore) {
        bestScore = score;
        best = at;
      }
      from = at + 1;
    }
    if (best === -1) return null;

    const globalStart = best;
    const globalEnd = best + anchor.exact.length;
    const touched = slices.filter((s) => s.end > globalStart && s.start < globalEnd);
    if (touched.length === 0) return null;

    const first = touched[0];
    const last = touched[touched.length - 1];
    return {
      slices: touched,
      startNode: first.node,
      startOffset: globalStart - first.start,
      endNode: last.node,
      endOffset: globalEnd - last.start,
    };
  }
}

/** The shared fallback instance the registry defaults to. */
export const domProvider = new DOMProvider();
