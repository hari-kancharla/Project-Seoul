import { test } from 'node:test';
import assert from 'node:assert/strict';

import { ProviderRegistry } from './registry.ts';
import { domProvider } from './domProvider.ts';
import { googleDocsProvider } from './googleDocsProvider.ts';
import { officeProvider } from './officeProvider.ts';
import type { DocumentProvider } from './types.ts';

// Minimal stand-ins. matches() is the only thing under test here, and it takes
// the document only to sniff for an injected Office.js context.
const plainDoc = { defaultView: null } as unknown as Document;
const officeDoc = { defaultView: { Office: {} } } as unknown as Document;

const DOCS_URL = 'https://docs.google.com/document/d/1AbCd/edit';
const OFFICE_URL = 'https://word-edit.officeapps.live.com/we/wordeditorframe.aspx?x=1';
const ARBITRARY_URL = 'https://example.com/blog/some-article';

/** Deterministic PRNG so a shuffle failure reproduces exactly. */
function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function shuffled<T>(items: readonly T[], rand: () => number): T[] {
  const out = items.slice();
  for (let i = out.length - 1; i > 0; i -= 1) {
    const j = Math.floor(rand() * (i + 1));
    const swap = out[i];
    out[i] = out[j];
    out[j] = swap;
  }
  return out;
}

test('resolves docs.google.com to the Google Docs provider', () => {
  const registry = new ProviderRegistry(domProvider);
  registry.register(googleDocsProvider);
  registry.register(officeProvider);
  assert.equal(registry.resolve(DOCS_URL, plainDoc).id, 'google-docs');
});

test('resolves an arbitrary site to the DOM provider', () => {
  const registry = new ProviderRegistry(domProvider);
  registry.register(googleDocsProvider);
  registry.register(officeProvider);
  assert.equal(registry.resolve(ARBITRARY_URL, plainDoc).id, 'dom');
});

test('resolves an Office host carrying an Office.js context', () => {
  const registry = new ProviderRegistry(domProvider);
  registry.register(googleDocsProvider);
  registry.register(officeProvider);
  assert.equal(registry.resolve(OFFICE_URL, officeDoc).id, 'office');
});

test('an Office host WITHOUT Office.js falls back to the DOM provider', () => {
  // SharePoint serves file listings and wiki pages as ordinary DOM. Claiming
  // those for the Office provider would route a plain page into an API that
  // cannot read it.
  const registry = new ProviderRegistry(domProvider);
  registry.register(officeProvider);
  assert.equal(registry.resolve('https://contoso.sharepoint.com/sites/team', plainDoc).id, 'dom');
});

test('resolution is stable across 100 shuffled registration orders', () => {
  // The DOM provider is deliberately shuffled in alongside the others. It
  // matches every URL, so if precedence came from position rather than from
  // identity it would win whenever the shuffle happened to place it first —
  // and this test would fail for roughly a third of the seeds.
  const all: readonly DocumentProvider[] = [googleDocsProvider, officeProvider, domProvider];
  const seen = new Set<string>();

  for (let seed = 1; seed <= 100; seed += 1) {
    const registry = new ProviderRegistry(domProvider);
    const order = shuffled(all, mulberry32(seed));
    for (const provider of order) registry.register(provider);

    const outcome = [
      registry.resolve(DOCS_URL, plainDoc).id,
      registry.resolve(OFFICE_URL, officeDoc).id,
      registry.resolve(ARBITRARY_URL, plainDoc).id,
    ].join(',');
    seen.add(outcome);

    assert.equal(
      outcome,
      'google-docs,office,dom',
      `seed ${seed} resolved differently with order [${order.map((p) => p.id).join(', ')}]`,
    );
  }

  assert.equal(seen.size, 1, `expected one outcome across all orders, saw ${[...seen].join(' | ')}`);
});

test('re-registering an id replaces in place and keeps its precedence', () => {
  const registry = new ProviderRegistry(domProvider);
  registry.register(googleDocsProvider);
  registry.register(officeProvider);

  const replacement: DocumentProvider = {
    ...googleDocsProvider,
    id: 'google-docs',
    matches: () => true,
  } as DocumentProvider;
  registry.register(replacement);

  assert.equal(registry.providers.length, 2);
  assert.equal(registry.providers[0].id, 'google-docs');
  // It now matches everything, and being first it wins — proving it kept
  // position 0 rather than being appended.
  assert.equal(registry.resolve(ARBITRARY_URL, plainDoc).id, 'google-docs');
});

test('a provider whose matcher throws is skipped, not fatal', () => {
  const registry = new ProviderRegistry(domProvider);
  registry.register({
    id: 'broken',
    capabilities: { index: false, locate: false, mutate: false, atomicMutate: false },
    matches: () => {
      throw new Error('boom');
    },
    index: async () => {
      throw new Error('unused');
    },
    locate: async () => {
      throw new Error('unused');
    },
    mutate: async () => {
      throw new Error('unused');
    },
  });
  registry.register(googleDocsProvider);

  assert.equal(registry.resolve(DOCS_URL, plainDoc).id, 'google-docs');
  assert.equal(registry.resolve(ARBITRARY_URL, plainDoc).id, 'dom');
});
