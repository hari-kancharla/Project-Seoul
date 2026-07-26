// Launch an isolated Chromium through puppeteer-core.
// On macOS, never pick the user's Google Chrome installation implicitly. Use
// the locally built Seoul Chromium; any system browser must be explicitly
// opted into with SEOUL_CHROME_BINARY.
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer-core';
import { assertBrowserLaunchPermitted } from '../../scripts/browser-launch-safety.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '..', '..');

export function candidateBrowserPaths({
  env = process.env,
  platform = process.platform,
  projectRoot = repoRoot,
} = {}) {
  const checkoutRoot = (env.SEOUL_CHROMIUM_ROOT || '').trim()
    ? path.resolve(env.SEOUL_CHROMIUM_ROOT)
    : path.resolve(projectRoot, '..', 'seoul-chromium.noindex');
  const seoulBinary = (env.SEOUL_CHROMIUM_BINARY || '').trim()
    ? path.resolve(env.SEOUL_CHROMIUM_BINARY)
    : path.join(
        checkoutRoot,
        'src',
        'out',
        'SeoulBaseline',
        'Chromium.app',
        'Contents',
        'MacOS',
        'Chromium',
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

export function resolveChromeBinary({
  candidates = candidateBrowserPaths(),
  pathExists = existsSync,
} = {}) {
  const found = candidates.find((candidate) => pathExists(candidate));
  if (!found) {
    throw new Error(
      'No isolated Chrome/Chromium binary found. Build Seoul Chromium or ' +
        'point SEOUL_CHROME_BINARY at a dedicated test browser executable.',
    );
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
