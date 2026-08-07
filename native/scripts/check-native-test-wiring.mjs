#!/usr/bin/env node
// Every native Seoul test target must actually be built and run by test.sh.
//
// Two independent ways a native suite goes quiet, both of which had happened:
//
//   1. A `test("seoul_..._unittests")` target is declared in a BUILD.gn and
//      never added to the runner's list, so `npm run test:native` neither
//      builds nor runs it. The three adblock unit targets - 116 passing cases -
//      sat like this.
//   2. A browser fixture is linked into seoul_browser_tests but falls outside
//      the gtest filter, described below.
//
// This gate reads the tracked tree only; no Chromium checkout is required.
//
// -- the browser-test filter must name every Seoul fixture it links ---------
//
// `seoul_browser_tests` is a dedicated binary, but it links one upstream target
// (//chrome/browser/ui/views/tabs/vertical:browser_tests) whose cases belong to
// Chromium, so test.sh runs an allow-list of fixtures rather than the whole
// binary. An allow-list that nobody re-derives goes stale in the quietest
// possible way: the new fixture is compiled, linked, never selected, and the
// run still prints SUCCESS.
//
// That is not hypothetical. Before this gate existed the filter named five
// fixtures while the binary carried nine, and AdBlockBrowserTest,
// CosmeticFilterAgentTest, SeoulBoostDarkBrowserTest and
// SeoulOrganizationServiceBrowserTest - 31 cases - had never run.
//
// So: resolve the Seoul-owned test source sets out of GN, read the fixture
// names out of the sources they list, and require each one to appear in the
// filter. No checkout needed; this reads the tracked tree only.

import {readFileSync, existsSync, readdirSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

/** Every BUILD.gn beneath `root`. */
function gnFilesUnder(root) {
  const found = [];
  for (const entry of readdirSync(root, {withFileTypes: true})) {
    const full = path.join(root, entry.name);
    if (entry.isDirectory()) found.push(...gnFilesUnder(full));
    else if (entry.name === 'BUILD.gn') found.push(full);
  }
  return found;
}

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, '..', '..');
const seoulRoot = path.join(repoRoot, 'native', 'seoul');

const ENTRY_BUILD = path.join(seoulRoot, 'browser', 'product', 'browser', 'BUILD.gn');
const TEST_SH = path.join(scriptDir, 'test.sh');

let failed = false;
function fail(message) {
  console.error(`native-test-wiring: FAIL - ${message}`);
  failed = true;
}

/** The body of `<kind>("<name>") { ... }`, brace-matched. */
function gnBlock(source, kind, name) {
  const header = new RegExp(`${kind}\\("${name}"\\)\\s*{`);
  const start = source.match(header);
  if (!start) return null;
  let depth = 0;
  const from = start.index + start[0].length - 1;
  for (let i = from; i < source.length; i += 1) {
    if (source[i] === '{') depth += 1;
    else if (source[i] === '}') {
      depth -= 1;
      if (depth === 0) return source.slice(from + 1, i);
    }
  }
  return null;
}

/** String literals inside a `name = [ ... ]` list. */
function gnList(block, name) {
  const match = block.match(new RegExp(`${name}\\s*(?:\\+)?=\\s*\\[([\\s\\S]*?)\\]`));
  if (!match) return [];
  return [...match[1].matchAll(/"([^"]+)"/g)].map((m) => m[1]);
}

// -- Seoul-owned test source sets reachable from seoul_browser_tests ---------

if (!existsSync(ENTRY_BUILD)) {
  fail(`cannot find ${path.relative(repoRoot, ENTRY_BUILD)}`);
  process.exit(1);
}

const entrySource = readFileSync(ENTRY_BUILD, 'utf8');
const entryBlock = gnBlock(entrySource, 'test', 'seoul_browser_tests');
if (!entryBlock) {
  fail('no test("seoul_browser_tests") target in the product browser BUILD.gn');
  process.exit(1);
}

// `:product_browser_tests` is a source set in the same file; `//seoul/...:name`
// points at another Seoul BUILD.gn. Anything else (//chrome/..., //content/...)
// is upstream and its cases are not ours to run.
const deps = gnList(entryBlock, 'deps');
const sourceSets = [];
for (const dep of deps) {
  if (dep.startsWith(':')) {
    sourceSets.push({buildFile: ENTRY_BUILD, name: dep.slice(1)});
  } else if (dep.startsWith('//seoul/')) {
    const [dir, name] = dep.slice('//'.length).split(':');
    sourceSets.push({
      buildFile: path.join(repoRoot, 'native', dir, 'BUILD.gn'),
      name,
    });
  }
}

if (sourceSets.length === 0) {
  fail('resolved no Seoul-owned test source sets - the GN parse is wrong');
}

// -- fixtures declared by those sources -------------------------------------

const FIXTURE_MACRO = /\b(?:IN_PROC_BROWSER_TEST_F|TEST_F|TEST)\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,/g;

const fixtures = new Map(); // fixture -> [source files]
for (const {buildFile, name} of sourceSets) {
  if (!existsSync(buildFile)) {
    fail(`dep resolves to a missing BUILD.gn: ${path.relative(repoRoot, buildFile)}`);
    continue;
  }
  const buildSource = readFileSync(buildFile, 'utf8');
  const block = gnBlock(buildSource, 'source_set', name);
  if (!block) {
    fail(`no source_set("${name}") in ${path.relative(repoRoot, buildFile)}`);
    continue;
  }
  for (const relative of gnList(block, 'sources')) {
    if (!/\.(cc|mm)$/.test(relative)) continue;
    const file = path.join(path.dirname(buildFile), relative);
    if (!existsSync(file)) {
      fail(`source_set("${name}") lists a missing file: ${relative}`);
      continue;
    }
    const source = readFileSync(file, 'utf8');
    for (const match of source.matchAll(FIXTURE_MACRO)) {
      const fixture = match[1];
      if (!fixtures.has(fixture)) fixtures.set(fixture, []);
      const seen = fixtures.get(fixture);
      const rel = path.relative(repoRoot, file);
      if (!seen.includes(rel)) seen.push(rel);
    }
  }
}

if (fixtures.size === 0) {
  fail('found no test fixtures at all - the macro scan is wrong');
}

// -- the filter test.sh actually runs ---------------------------------------

const testShSource = readFileSync(TEST_SH, 'utf8');
const filtered = new Set(
  [...testShSource.matchAll(/([A-Za-z_][A-Za-z0-9_]*)\.\*/g)].map((m) => m[1]),
);

for (const [fixture, files] of [...fixtures].sort()) {
  if (!filtered.has(fixture)) {
    fail(
      `${fixture} is compiled into seoul_browser_tests (${files.join(', ')}) ` +
        'but the filter in native/scripts/test.sh never selects it, so it has never run. ' +
        `Add "${fixture}.*" to the filter.`,
    );
  }
}

// A fixture named in the filter but absent from the sources is the same rot in
// the other direction: it reads as coverage and selects nothing.
for (const fixture of [...filtered].sort()) {
  if (!fixtures.has(fixture)) {
    fail(
      `the filter in native/scripts/test.sh selects ${fixture}.* but no Seoul ` +
        'browser-test source declares that fixture; it matches nothing.',
    );
  }
}

// -- every unit-test target is built and run by test.sh ---------------------
//
// `test("seoul_x_unittests")` produces a binary; test.sh has to name it twice,
// once as a GN target to build and once as a binary to execute. Declaring the
// target and forgetting the runner is invisible - nothing errors, the suite
// just never runs.

const unitTargets = new Map(); // target name -> "dir:name" GN label
for (const buildFile of gnFilesUnder(seoulRoot)) {
  const source = readFileSync(buildFile, 'utf8');
  for (const match of source.matchAll(/\btest\("([A-Za-z0-9_]+)"\)/g)) {
    const name = match[1];
    if (!name.endsWith('_unittests')) continue; // seoul_browser_tests is handled above
    const dir = path
      .relative(path.join(repoRoot, 'native'), path.dirname(buildFile))
      .split(path.sep)
      .join('/');
    unitTargets.set(name, `${dir}:${name}`);
  }
}

if (unitTargets.size === 0) {
  fail('found no seoul_*_unittests targets - the BUILD.gn scan is wrong');
}

for (const [name, label] of [...unitTargets].sort()) {
  // Mac-only targets are guarded by `if (is_mac)` in GN; test.sh runs on macOS,
  // so they are still expected in the list.
  if (!new RegExp(`^\\s*${name}\\s*$`, 'm').test(testShSource)) {
    fail(
      `test("${name}") is declared in the tracked tree but native/scripts/test.sh ` +
        'never runs the binary, so `npm run test:native` skips it entirely. ' +
        `Add ${name} to the binaries list.`,
    );
  }
  if (!testShSource.includes(label)) {
    fail(
      `test("${name}") is declared but native/scripts/test.sh never builds it. ` +
        `Add ${label} to the targets list.`,
    );
  }
}

if (failed) process.exit(1);

console.log(
  `native-test-wiring: OK (${unitTargets.size} unit-test targets built and run; ` +
    `${fixtures.size} browser fixtures across ${sourceSets.length} test source sets, ` +
    'all selected by native/scripts/test.sh)',
);
