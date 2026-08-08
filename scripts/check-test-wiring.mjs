#!/usr/bin/env node
// Every test file in this repository must actually run.
//
// Two distinct ways a suite goes quiet, both of which happened here:
//
//   1. The file is written and committed but no npm script ever names it.
//      `apps/browser-harness/src/content/elementResolver.test.mjs` (14 cases)
//      and `tools/reference/transform.test.mjs` (11 cases) sat like this.
//   2. A script names it, but nothing reaches that script. `test:providers`
//      (20 cases) existed and passed for anyone who typed it by hand, and CI
//      never did.
//
// Both look identical from the outside: green suite, untested code. This gate
// closes both by walking the npm script graph from `test` and comparing what it
// reaches against what is on disk.

import {readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const pkg = JSON.parse(readFileSync(path.join(repoRoot, 'package.json'), 'utf8'));
const scripts = pkg.scripts ?? {};

// Suites that cannot run from a bare checkout. Each needs a prerequisite this
// repository deliberately does not vendor, so `npm test` must not depend on it.
// Anything listed here still has to exist, and has to be reachable some other
// way - the reason is recorded so an unrunnable suite is a decision, not a
// leftover.
const DETACHED_SUITES = new Map([
  ['test:native', 'needs the pinned external Chromium checkout; run on a capable macOS host'],
  ['test:native:browser', 'needs the pinned external Chromium checkout; run on a capable macOS host'],
  ['test:swift', 'needs a macOS Swift toolchain; run by the macos job in .github/workflows/ci.yml'],
]);

const TEST_FILE_PATTERN = /(\.(test|smoke|spec)\.(mjs|js|ts|mts)|^protocol\/tests\/.*\.mjs)$/;

function fail(message) {
  console.error(`test-wiring: FAIL - ${message}`);
  process.exitCode = 1;
}

// -- what the script graph reaches ------------------------------------------

/** Script names reachable from `entry` by following `npm run <name>`. */
function reachableScripts(entry) {
  const seen = new Set();
  const queue = [entry];
  while (queue.length > 0) {
    const name = queue.shift();
    if (seen.has(name) || !(name in scripts)) continue;
    seen.add(name);
    // `npm run <name>`, plus npm's built-in shorthands for the lifecycle
    // scripts - `npm test` is how `ci` invokes the suite, and missing it would
    // make this gate report the wiring it is meant to verify as broken.
    for (const match of scripts[name].matchAll(/npm run ([A-Za-z0-9:_-]+)/g)) {
      queue.push(match[1]);
    }
    for (const match of scripts[name].matchAll(/npm (test|start|stop|restart)\b/g)) {
      queue.push(match[1]);
    }
  }
  return seen;
}

const reachedFromTest = reachableScripts('test');
const reachedFromCi = reachableScripts('ci');

// -- 1. every test file on disk is named by a script ------------------------

const tracked = execFileSync('git', ['ls-files'], {cwd: repoRoot, encoding: 'utf8'})
  .split('\n')
  .filter(Boolean);

const testFiles = tracked.filter(
  (file) =>
    TEST_FILE_PATTERN.test(file) &&
    !file.startsWith('node_modules/') &&
    // Built output, not a source suite.
    !file.includes('/dist/'),
);

if (testFiles.length === 0) {
  fail('found no test files at all - the discovery pattern is wrong');
}

const runnableScriptText = [...reachedFromTest, ...DETACHED_SUITES.keys()]
  .map((name) => scripts[name] ?? '')
  .join('\n');

for (const file of testFiles) {
  if (!runnableScriptText.includes(file)) {
    fail(
      `${file} is committed but no npm script reachable from \`npm test\` runs it. ` +
        'Add it to an existing test script, or give it one and call that from `test`.',
    );
  }
}

// -- 2. every test script is reachable, or declared detached ----------------

for (const name of Object.keys(scripts)) {
  if (!name.startsWith('test')) continue;
  if (reachedFromTest.has(name)) continue;
  if (DETACHED_SUITES.has(name)) continue;
  fail(
    `\`${name}\` is a test script that \`npm test\` never reaches, so CI never runs it. ` +
      'Call it from `test`, or record it in DETACHED_SUITES with the prerequisite it needs.',
  );
}

for (const [name, reason] of DETACHED_SUITES) {
  if (!(name in scripts)) {
    fail(`DETACHED_SUITES names \`${name}\` (${reason}) but package.json has no such script`);
  }
}

// -- 3. `npm run ci` really does run the tests ------------------------------

if (!reachedFromCi.has('test')) {
  fail('`npm run ci` does not reach `test`');
}
if (!reachedFromCi.has('check')) {
  fail('`npm run ci` does not reach `check`');
}

if (process.exitCode !== 1) {
  console.log(
    `test-wiring: OK (${testFiles.length} test files, all reached from \`npm test\`; ` +
      `${DETACHED_SUITES.size} suites declared detached with a recorded prerequisite)`,
  );
}
