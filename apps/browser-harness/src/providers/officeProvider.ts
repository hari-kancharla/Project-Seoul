/**
 * STUB. Correct matches() and correct capabilities; no behaviour yet.
 *
 * Word and Excel on the web, driven through an Office.js context rather than
 * the DOM. Unlike Google Docs this surface does render real elements, so it can
 * report geometry — it is the case that proves `locate: false` is a property of
 * a particular surface and not of "hosted editors" as a category.
 *
 * All four capabilities are true, which makes this the richest provider and the
 * one the registry must never shadow with the DOM fallback.
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

const ID = 'office';

/** Hosts that serve the Office web apps. */
const OFFICE_HOSTS = [
  'officeapps.live.com',
  'officeapps-df.live.com',
  'office.live.com',
  'onedrive.live.com',
];

function hostOf(url: string): string {
  try {
    return new URL(url).hostname.toLowerCase();
  } catch {
    return '';
  }
}

export class OfficeProvider implements DocumentProvider {
  readonly id = ID;

  readonly capabilities: Capabilities = {
    index: true,
    locate: true,
    mutate: true,
    atomicMutate: true,
  };

  matches(url: string, doc: Document): boolean {
    const host = hostOf(url);
    if (!host) return false;
    const hosted =
      OFFICE_HOSTS.some((h) => host === h || host.endsWith(`.${h}`)) ||
      host === 'sharepoint.com' ||
      host.endsWith('.sharepoint.com');
    if (!hosted) return false;

    // SharePoint serves plenty of pages that are not documents. The Office.js
    // context is what distinguishes an editor from a file listing; when the
    // page has not injected it, this is an ordinary DOM page.
    const view = doc?.defaultView as { Office?: unknown } | null | undefined;
    return Boolean(view && view.Office);
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

export const officeProvider = new OfficeProvider();
