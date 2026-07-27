/**
 * STUB. Correct matches() and correct capabilities; no behaviour yet.
 *
 * It exists now so the registry logic is exercised against a real second
 * provider, and so the capability shape is fixed before the real work starts.
 *
 * The capability row is the interesting part, and it is why design rules 2 and
 * 3 exist. Google Docs paints its text to a <canvas>, so:
 *
 *   locate: FALSE        — there is no element to measure, so no rects, ever.
 *                          The assistant must know this in advance and say
 *                          "I can change it, but I cannot point at it here"
 *                          rather than calling locate() and reading the empty
 *                          result as "the passage is not in the document".
 *   atomicMutate: TRUE   — edits go through the Docs API as a single batched
 *                          request that the server applies all-or-nothing,
 *                          which is a stronger guarantee than the DOM provider
 *                          can offer on the same document.
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
} from './types.ts';
import { NotImplementedError } from './types.ts';

const ID = 'google-docs';

function hostOf(url: string): string {
  try {
    return new URL(url).hostname.toLowerCase();
  } catch {
    return '';
  }
}

export class GoogleDocsProvider implements DocumentProvider {
  readonly id = ID;

  readonly capabilities: Capabilities = {
    index: true,
    locate: false,
    mutate: true,
    atomicMutate: true,
  };

  matches(url: string, _doc: Document): boolean {
    return hostOf(url) === 'docs.google.com';
  }

  async index(): Promise<{ passages: Passage[]; coverage: Coverage }> {
    throw new NotImplementedError(ID, 'index');
  }

  async locate(_anchor: TextQuoteAnchor): Promise<LocateResult> {
    throw new NotImplementedError(ID, 'locate');
  }

  async mutate(_ops: EditOp[]): Promise<EditResult> {
    throw new NotImplementedError(ID, 'mutate');
  }
}

export const googleDocsProvider = new GoogleDocsProvider();
