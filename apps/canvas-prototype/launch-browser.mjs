// Launch an isolated Seoul browser through puppeteer-core.
// On macOS, never pick the user's Google Chrome installation implicitly. Use
// the locally built Seoul product; any system browser must be explicitly
// opted into with SEOUL_CHROME_BINARY.
import { existsSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer-core';
import { assertBrowserLaunchPermitted } from '../../scripts/browser-launch-safety.mjs';
import { mainWorktreeRoot, checkoutRoot } from '../../native/scripts/checkout-root.mjs';

const repoRoot = mainWorktreeRoot();

export function candidateBrowserPaths({
  env = process.env,
  platform = process.platform,
  projectRoot = repoRoot,
} = {}) {
  const resolvedCheckout = checkoutRoot({env, start: projectRoot});
  const seoulBinary = (env.SEOUL_CHROMIUM_BINARY || '').trim()
    ? path.resolve(env.SEOUL_CHROMIUM_BINARY)
    : path.join(
        resolvedCheckout,
        'src',
        'out',
        'SeoulBaseline',
        'Seoul.app',
        'Contents',
        'MacOS',
        'Seoul',
      );
  const candidates = [
    env.SEOUL_CHROME_BINARY,
    seoulBinary,
  ];
  if (platform !== 'darwin') {
    candidates.push(
      '/usr/bin/google-chrome',
      '/usr/bin/google-chrome-stable',
      '/usr/bin/chromium-browser',
      '/usr/bin/chromium',
    );
  }
  return candidates.filter(Boolean);
}

export const NO_BROWSER_MESSAGE =
  'No isolated Seoul browser binary found. Build Seoul or ' +
  'point SEOUL_CHROME_BINARY at a dedicated test browser executable.';

/// The same audited resolution as `resolveChromeBinary`, but reporting absence
/// instead of throwing. A suite that needs a real browser uses this to decide
/// whether it can run at all; the safety rule is unchanged, because the
/// candidate list is the only thing either function ever consults.
export function findBrowserBinary({
  candidates = candidateBrowserPaths(),
  pathExists = existsSync,
} = {}) {
  return candidates.find((candidate) => pathExists(candidate)) ?? null;
}

export function resolveChromeBinary({
  candidates = candidateBrowserPaths(),
  pathExists = existsSync,
} = {}) {
  const found = findBrowserBinary({candidates, pathExists});
  if (!found) {
    throw new Error(NO_BROWSER_MESSAGE);
  }
  return found;
}

export function launchBrowser(options = {}) {
  assertBrowserLaunchPermitted();
  const {args = [], ...launchOptions} = options;
  return puppeteer.launch({
    headless: true,
    ...launchOptions,
    // Keep the audited resolver authoritative even if an incidental caller
    // forwards a Puppeteer options object containing executablePath.
    executablePath: resolveChromeBinary(),
    args: [
      '--no-first-run',
      '--no-default-browser-check',
      '--use-mock-keychain',
      ...args,
    ],
  });
}
