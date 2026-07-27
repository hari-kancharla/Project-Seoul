/**
 * Seoul Document Provider protocol.
 *
 * Runs INSIDE THE EXTENSION, never in Swift. Every provider needs either the
 * DOM, an OAuth token, or an Office.js context, none of which exist on the
 * native side. The Swift side only ever sends a query and receives rects.
 *
 * ---------------------------------------------------------------------------
 * FOUR DESIGN RULES
 * ---------------------------------------------------------------------------
 *
 * 1. TextQuoteAnchor is the universal currency.
 *    No provider ever exposes DOM nodes, character indices, or element IDs
 *    across this boundary. Anchors are the only thing that survives a
 *    re-render, a virtualized list recycling its rows, and a move to a
 *    different surface entirely. A node handle is stale the moment React
 *    reconciles; a quote is still findable.
 *
 * 2. Capabilities are DECLARED, not discovered.
 *    The assistant must be able to say "I can find it and change it, but I
 *    cannot draw on it here" BEFORE attempting anything. Probing by calling a
 *    method and catching the failure is not equivalent: it burns a round trip,
 *    and on a mutating call it may half-succeed before it throws.
 *
 * 3. locate() returning zero rects with reason 'no-geometry' is a SUCCESS.
 *    It means the passage was found and the user was moved to it, on a surface
 *    that cannot hand back screen geometry. Canvas-rendered editors are the
 *    motivating case: Google Docs paints text to a canvas, so there is no box
 *    to measure, but "scroll the user to the sentence" still worked. Treating
 *    an empty rect list as failure would report a successful navigation as a
 *    miss.
 *
 * 4. Coverage is never optional.
 *    A provider that could only read part of the document must say so, so the
 *    assistant says "I have only indexed through chapter 4" instead of claiming
 *    a passage does not exist. Silence about a gap is indistinguishable from an
 *    exhaustive search, and that is the one confusion that makes the assistant
 *    confidently wrong.
 */

/**
 * A passage identified by what it says, not by where it lives.
 * `prefix` and `suffix` disambiguate when `exact` occurs more than once.
 */
export interface TextQuoteAnchor {
  prefix: string;
  exact: string;
  suffix: string;
}

export interface Passage {
  id: string;
  text: string;
  anchor: TextQuoteAnchor;
  /** Human-readable, e.g. "Chapter 4" or "main". */
  region: string;
  /** Document order, for stable tie-breaking. */
  order: number;
}

export interface RegionNote {
  region: string;
  reason: string;
  estimatedSize?: number;
}

export interface Coverage {
  complete: boolean;
  /** Virtualized lists, unfetched infinite scroll. */
  partial: RegionNote[];
  /** Canvas text, cross-origin iframes. */
  unreachable: RegionNote[];
}

export interface ViewportRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export type LocateReason = 'ok' | 'no-geometry' | 'not-found' | 'off-screen';

export interface LocateResult {
  /** MAY be empty; empty is not necessarily failure. See design rule 3. */
  rects: ViewportRect[];
  reason: LocateReason;
  /** True if we scrolled or expanded to reach it. */
  navigated: boolean;
}

export type EditOp =
  | { kind: 'replace'; anchor: TextQuoteAnchor; text: string }
  | { kind: 'insertAfter'; anchor: TextQuoteAnchor; text: string }
  | { kind: 'delete'; anchor: TextQuoteAnchor };

export interface EditResult {
  applied: boolean;
  /** Did the surface guarantee all-or-nothing. */
  atomic: boolean;
  reason?: string;
  undo?: () => Promise<void>;
}

export interface Capabilities {
  index: boolean;
  locate: boolean;
  mutate: boolean;
  atomicMutate: boolean;
}

export interface DocumentProvider {
  readonly id: string;
  readonly capabilities: Capabilities;
  matches(url: string, doc: Document): boolean;
  index(): Promise<{ passages: Passage[]; coverage: Coverage }>;
  locate(anchor: TextQuoteAnchor): Promise<LocateResult>;
  mutate(ops: EditOp[]): Promise<EditResult>;
}

/**
 * Thrown by a provider stub whose real implementation has not landed yet.
 * Distinct from a runtime failure: this means "this surface will support the
 * operation, the code is not written", whereas `capabilities` declaring false
 * means "this surface cannot do it at all".
 */
export class NotImplementedError extends Error {
  readonly providerId: string;
  readonly operation: string;

  constructor(providerId: string, operation: string) {
    super(`${providerId}.${operation}() is not implemented yet`);
    this.name = 'NotImplementedError';
    this.providerId = providerId;
    this.operation = operation;
  }
}

/**
 * Builds a Coverage, enforcing design rule 4 structurally: a provider can never
 * claim completeness while also listing regions it could not read. Making this
 * unrepresentable is worth more than documenting it, because the failure mode —
 * `complete: true` next to a non-empty `unreachable` — reads as authoritative
 * and is exactly what makes the assistant deny a passage that does exist.
 */
export function coverageOf(
  complete: boolean,
  partial: RegionNote[] = [],
  unreachable: RegionNote[] = [],
): Coverage {
  return {
    complete: complete && partial.length === 0 && unreachable.length === 0,
    partial,
    unreachable,
  };
}
