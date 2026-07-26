import test from 'node:test';
import assert from 'node:assert/strict';

import {
  candidateBrowserPaths,
  resolveChromeBinary,
} from '../launch-browser.mjs';
import { assertBrowserLaunchPermitted } from '../../../scripts/browser-launch-safety.mjs';

test('macOS browser tests never select Google Chrome implicitly', () => {
  const candidates = candidateBrowserPaths({
    env: {},
    platform: 'darwin',
    projectRoot: '/workspace/ProjectSeoul',
  });

  assert.deepEqual(candidates, [
    '/workspace/seoul-chromium.noindex/src/out/SeoulBaseline/Chromium.app/Contents/MacOS/Chromium',
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

test('restricted macOS Codex runs refuse before starting Chromium', () => {
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
