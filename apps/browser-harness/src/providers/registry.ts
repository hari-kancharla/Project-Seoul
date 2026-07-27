/**
 * Provider registry and capability gate.
 *
 * ORDERING IS A CORRECTNESS PROPERTY, NOT A DETAIL.
 *
 * Providers are held in an ARRAY, in registration order. Nothing here ever
 * iterates an object's keys or a Set built from a literal to decide precedence:
 * key order is an implementation detail of the engine and of how the module
 * graph happened to load, and resolution that depends on it produces a
 * different answer on a different bundler. The DOM fallback is ranked last by
 * IDENTITY rather than by position, which is what makes resolution independent
 * of the order the other providers happened to register in.
 */

import type { DocumentProvider, LocateResult, TextQuoteAnchor } from './types.ts';
import { domProvider } from './domProvider.ts';
import { googleDocsProvider } from './googleDocsProvider.ts';
import { officeProvider } from './officeProvider.ts';

export class ProviderRegistry {
  private readonly ordered: DocumentProvider[] = [];
  private readonly fallbackProvider: DocumentProvider;

  constructor(fallback: DocumentProvider = domProvider) {
    this.fallbackProvider = fallback;
  }

  get fallback(): DocumentProvider {
    return this.fallbackProvider;
  }

  /** Registration order, for inspection and for tests. */
  get providers(): readonly DocumentProvider[] {
    return this.ordered;
  }

  /**
   * Re-registering an existing id REPLACES it in place rather than appending,
   * so a hot-reloaded provider keeps its precedence instead of silently
   * drifting to the back of the queue.
   */
  register(provider: DocumentProvider): void {
    const at = this.ordered.findIndex((p) => p.id === provider.id);
    if (at >= 0) {
      this.ordered[at] = provider;
      return;
    }
    this.ordered.push(provider);
  }

  /**
   * The most specific provider that claims this page, falling back to the DOM
   * provider.
   *
   * "Most specific" is decided by identity, not by scoring: the DOM provider
   * matches every page by construction, so it is excluded from the scan and
   * used only when nothing else claims the document. Among providers that do
   * claim it, registration order breaks the tie.
   */
  resolve(url: string, doc: Document): DocumentProvider {
    for (const provider of this.ordered) {
      if (provider === this.fallbackProvider) continue;
      // A provider whose matcher throws is treated as not matching. One
      // malformed provider must not be able to take down resolution for
      // every page.
      let claimed = false;
      try {
        claimed = provider.matches(url, doc);
      } catch {
        claimed = false;
      }
      if (claimed) return provider;
    }
    return this.fallbackProvider;
  }
}

/**
 * The default registry, pre-populated so `resolve()` works out of the box.
 * Tests that need to vary ordering build their own `ProviderRegistry`.
 */
export const registry = new ProviderRegistry(domProvider);
registry.register(googleDocsProvider);
registry.register(officeProvider);

export function register(provider: DocumentProvider): void {
  registry.register(provider);
}

export function resolve(url: string, doc: Document): DocumentProvider {
  return registry.resolve(url, doc);
}

/**
 * Design rule 2, enforced at the call site: a provider that DECLARED it cannot
 * locate is never asked to.
 *
 * The empty result this returns is not an error — per design rule 3, zero rects
 * with 'no-geometry' means "no screen geometry is available here", which for a
 * canvas-rendered editor is the true and final answer. `navigated` is false
 * because we did not move the user; nothing was called.
 */
export async function locateIfCapable(
  provider: DocumentProvider,
  anchor: TextQuoteAnchor,
): Promise<LocateResult> {
  if (!provider.capabilities.locate) {
    return { rects: [], reason: 'no-geometry', navigated: false };
  }
  return provider.locate(anchor);
}
