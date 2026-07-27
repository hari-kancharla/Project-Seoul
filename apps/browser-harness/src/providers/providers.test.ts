import { test } from 'node:test';
import assert from 'node:assert/strict';

import { locateIfCapable } from './registry.ts';
import { DOMProvider, domProvider } from './domProvider.ts';
import { googleDocsProvider } from './googleDocsProvider.ts';
import { officeProvider } from './officeProvider.ts';
import { coverageOf, NotImplementedError } from './types.ts';
import type { DocumentProvider, EditOp, TextQuoteAnchor } from './types.ts';

// ---------------------------------------------------------------------------
// A fake DOM, hand-built. Just the surface DOMProvider actually touches:
// nodeType, nodeValue, childNodes, parentNode, tagName, getAttribute.
// ---------------------------------------------------------------------------

interface FakeNode {
  nodeType: number;
  nodeValue: string | null;
  parentNode: FakeNode | null;
  childNodes: FakeNode[];
  tagName?: string;
  getAttribute?: (name: string) => string | null;
}

function text(value: string): FakeNode {
  return { nodeType: 3, nodeValue: value, parentNode: null, childNodes: [] };
}

function element(tagName: string, children: FakeNode[], attrs: Record<string, string> = {}): FakeNode {
  const node: FakeNode = {
    nodeType: 1,
    nodeValue: null,
    parentNode: null,
    childNodes: children,
    tagName,
    getAttribute: (name: string) => attrs[name] ?? null,
  };
  for (const child of children) child.parentNode = node;
  return node;
}

function documentWith(body: FakeNode): Document {
  return { body, designMode: 'off', defaultView: null } as unknown as Document;
}

/** Flattened text of a fake tree, for asserting what a mutation produced. */
function flatten(node: FakeNode): string {
  if (node.nodeType === 3) return node.nodeValue ?? '';
  return node.childNodes.map(flatten).join('');
}

function editableFixture() {
  const first = text('The quick brown fox jumps over the lazy dog.');
  const second = text('Second paragraph here.');
  const body = element('BODY', [
    element('DIV', [element('P', [first]), element('P', [second])], { contenteditable: 'true' }),
  ]);
  return { body, doc: documentWith(body) };
}

function anchor(prefix: string, exact: string, suffix: string): TextQuoteAnchor {
  return { prefix, exact, suffix };
}

// ---------------------------------------------------------------------------
// Design rule 2 — capabilities are declared, not discovered.
// ---------------------------------------------------------------------------

test('a provider declaring locate false is never asked to locate', async () => {
  // The real stub throws NotImplementedError from locate(). If the gate called
  // it, this test would reject rather than return — so a normal return is
  // itself proof the method was never entered.
  assert.equal(googleDocsProvider.capabilities.locate, false);

  const result = await locateIfCapable(googleDocsProvider, anchor('', 'anything', ''));

  assert.deepEqual(result, { rects: [], reason: 'no-geometry', navigated: false });
});

test('the gate does not enter locate() even when it would succeed', async () => {
  let entered = 0;
  const probe: DocumentProvider = {
    id: 'probe',
    capabilities: { index: true, locate: false, mutate: true, atomicMutate: true },
    matches: () => true,
    index: async () => ({ passages: [], coverage: coverageOf(true) }),
    locate: async () => {
      entered += 1;
      return { rects: [{ x: 1, y: 2, width: 3, height: 4 }], reason: 'ok', navigated: false };
    },
    mutate: async () => ({ applied: true, atomic: true }),
  };

  const result = await locateIfCapable(probe, anchor('', 'x', ''));

  assert.equal(entered, 0, 'locate() was called on a provider that declared locate: false');
  assert.equal(result.reason, 'no-geometry');
  assert.equal(result.rects.length, 0);
  assert.equal(result.navigated, false);
});

test('a provider declaring locate true IS asked to locate', async () => {
  // The mirror image, so the gate cannot pass by simply never calling anything.
  assert.equal(officeProvider.capabilities.locate, true);
  await assert.rejects(
    () => locateIfCapable(officeProvider, anchor('', 'x', '')),
    (error: unknown) => error instanceof NotImplementedError && error.operation === 'locate',
  );
});

// ---------------------------------------------------------------------------
// Non-atomic mutation.
// ---------------------------------------------------------------------------

test('EditOp arrays against a non-atomic provider report atomic false', async () => {
  const { body, doc } = editableFixture();
  const provider = new DOMProvider(doc);
  assert.equal(provider.capabilities.atomicMutate, false);

  const ops: EditOp[] = [
    { kind: 'replace', anchor: anchor('The ', 'quick brown', ' fox'), text: 'slow red' },
    { kind: 'replace', anchor: anchor('', 'Second', ' paragraph'), text: 'Third' },
  ];
  const result = await provider.mutate(ops);

  assert.equal(result.applied, true);
  assert.equal(result.atomic, false, 'a DOM batch must never claim atomicity');
  assert.match(flatten(body), /The slow red fox jumps/);
  assert.match(flatten(body), /Third paragraph here\./);
});

test('a partially applied batch still reports atomic false, and undoes', async () => {
  const { body, doc } = editableFixture();
  const before = flatten(body);
  const provider = new DOMProvider(doc);

  const ops: EditOp[] = [
    { kind: 'replace', anchor: anchor('The ', 'quick brown', ' fox'), text: 'slow red' },
    { kind: 'replace', anchor: anchor('', 'no such passage', ''), text: 'never' },
  ];
  const result = await provider.mutate(ops);

  // This is the exact hazard `atomic: false` warns about: op 0 landed, op 1
  // could not, and the document is now in a state neither op intended.
  assert.equal(result.applied, true);
  assert.equal(result.atomic, false);
  assert.match(result.reason ?? '', /op 1/);
  assert.match(flatten(body), /slow red/);

  assert.ok(result.undo, 'a partial application must offer an undo');
  await result.undo!();
  assert.equal(flatten(body), before, 'undo must restore the document exactly');
});

test('mutating a non-editable document applies nothing', async () => {
  const paragraph = text('Read-only prose.');
  const body = element('BODY', [element('P', [paragraph])]);
  const provider = new DOMProvider(documentWith(body));

  const result = await provider.mutate([
    { kind: 'replace', anchor: anchor('', 'Read-only', ' prose'), text: 'Editable' },
  ]);

  assert.equal(result.applied, false);
  assert.equal(result.atomic, false);
  assert.match(result.reason ?? '', /not editable/);
  assert.equal(flatten(body), 'Read-only prose.');
});

test('prefix and suffix disambiguate a repeated exact quote', async () => {
  const a = text('Address line 1 for shipping. ');
  const b = text('Address line 1 for billing.');
  const body = element('BODY', [element('DIV', [a, b], { contenteditable: 'true' })]);
  const provider = new DOMProvider(documentWith(body));

  const result = await provider.mutate([
    { kind: 'replace', anchor: anchor('shipping. ', 'Address line 1', ' for billing'), text: 'STREET' },
  ]);

  assert.equal(result.applied, true);
  assert.equal(flatten(body), 'Address line 1 for shipping. STREET for billing.');
});

// ---------------------------------------------------------------------------
// Design rule 4 — coverage is never optional.
// ---------------------------------------------------------------------------

test('coverage with a non-empty unreachable list marks complete false', () => {
  const coverage = coverageOf(true, [], [
    { region: 'editor canvas', reason: 'text is painted to a canvas' },
  ]);
  assert.equal(coverage.complete, false, 'cannot claim completeness while listing unreachable regions');
  assert.equal(coverage.unreachable.length, 1);
});

test('coverage with a non-empty partial list marks complete false', () => {
  const coverage = coverageOf(true, [
    { region: 'comments', reason: 'virtualized list, 40 of ~900 rows realised', estimatedSize: 900 },
  ]);
  assert.equal(coverage.complete, false);
});

test('coverage with no gaps may be complete', () => {
  assert.equal(coverageOf(true).complete, true);
  assert.equal(coverageOf(false).complete, false);
});

test('the DOM provider reports an unharvested document as incomplete', async () => {
  // The placeholder must not read as "this document is empty", which would
  // license the assistant to deny a passage that is plainly on the page.
  const { passages, coverage } = await domProvider.index();
  assert.deepEqual(passages, []);
  assert.equal(coverage.complete, false);
  assert.deepEqual(coverage.partial, []);
  assert.deepEqual(coverage.unreachable, []);
});

// ---------------------------------------------------------------------------
// Capability shape is fixed before the real work.
// ---------------------------------------------------------------------------

test('declared capability rows match the milestone contract', () => {
  assert.deepEqual(domProvider.capabilities, {
    index: true, locate: true, mutate: true, atomicMutate: false,
  });
  assert.deepEqual(googleDocsProvider.capabilities, {
    index: true, locate: false, mutate: true, atomicMutate: true,
  });
  assert.deepEqual(officeProvider.capabilities, {
    index: true, locate: true, mutate: true, atomicMutate: true,
  });
});

test('stubs throw NotImplemented rather than returning a plausible empty answer', async () => {
  for (const provider of [googleDocsProvider, officeProvider]) {
    await assert.rejects(() => provider.index(), NotImplementedError);
    await assert.rejects(() => provider.mutate([]), NotImplementedError);
  }
  await assert.rejects(() => googleDocsProvider.locate(anchor('', 'x', '')), NotImplementedError);
});
