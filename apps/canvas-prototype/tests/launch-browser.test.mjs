import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {
  candidateBrowserPaths,
  resolveChromeBinary,
} from '../launch-browser.mjs';
import { assertBrowserLaunchPermitted } from '../../../scripts/browser-launch-safety.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '..', '..', '..');

test('macOS browser tests never select Google Chrome implicitly', () => {
  const candidates = candidateBrowserPaths({
    env: {},
    platform: 'darwin',
    projectRoot: '/workspace/ProjectSeoul',
  });

  assert.deepEqual(candidates, [
    '/workspace/seoul-chromium.noindex/src/out/SeoulBaseline/Seoul.app/Contents/MacOS/Seoul',
  ]);
  assert.equal(
    candidates.some((candidate) => candidate.includes('Google Chrome.app')),
    false,
  );
});

test('an explicitly dedicated browser remains the highest-priority choice', () => {
  const candidates = candidateBrowserPaths({
    env: {SEOUL_CHROME_BINARY: '/dedicated/test-browser'},
    platform: 'darwin',
    projectRoot: '/workspace/ProjectSeoul',
  });

  assert.equal(
    resolveChromeBinary({
      candidates,
      pathExists: (candidate) =>
        candidate === '/dedicated/test-browser' ||
        candidate.includes('SeoulBaseline'),
    }),
    '/dedicated/test-browser',
  );
});

test('restricted macOS Codex runs refuse before starting the browser', () => {
  assert.throws(
    () => assertBrowserLaunchPermitted({
      env: {CODEX_SANDBOX: 'seatbelt'},
      platform: 'darwin',
    }),
    /Refusing to launch Chromium/,
  );
  assert.doesNotThrow(() => assertBrowserLaunchPermitted({
    env: {},
    platform: 'darwin',
  }));
});

test('every repository-owned development browser launch uses a mock keychain', () => {
  const launchFiles = [
    'native/scripts/run.sh',
    'native/scripts/preview.mjs',
    'native/scripts/smoke.mjs',
    'native/scripts/test.sh',
    'apps/canvas-prototype/launch-browser.mjs',
  ];

  for (const relativePath of launchFiles) {
    const source = readFileSync(path.join(repoRoot, relativePath), 'utf8');
    assert.match(
      source,
      /--use-mock-keychain/,
      `${relativePath} must never launch a development browser against the user's Keychain`,
    );
  }
});
