#!/usr/bin/env node
// Validate the Seoul-owned, gnrt-generated adblock Rust dependency overlay.
// This check is checkout-independent and performs no network access.

import {
  existsSync,
  readFileSync,
  readdirSync,
  statSync,
} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const nativeDir = path.resolve(here, '..');
const repoRoot = path.resolve(nativeDir, '..');
const overlay = path.join(nativeDir, 'third_party', 'rust', 'seoul_adblock');
const cratesRoot = path.join(overlay, 'crates');
const vendorRoot = path.join(overlay, 'vendor');
const provenancePath = path.join(overlay, 'provenance.json');
const lockPath = path.join(nativeDir, 'chromium.lock.json');
const problems = [];

function fail(message) {
  problems.push(message);
}

function readJson(file) {
  try {
    return JSON.parse(readFileSync(file, 'utf8'));
  } catch (error) {
    fail(`cannot parse ${path.relative(repoRoot, file)}: ${error.message}`);
    return null;
  }
}

function walk(dir, predicate) {
  const files = [];
  if (!existsSync(dir)) {
    return files;
  }
  for (const entry of readdirSync(dir)) {
    const absolute = path.join(dir, entry);
    if (statSync(absolute).isDirectory()) {
      files.push(...walk(absolute, predicate));
    } else if (predicate(absolute)) {
      files.push(absolute);
    }
  }
  return files;
}

function cargoPackageIdentity(cargoToml) {
  const text = readFileSync(cargoToml, 'utf8');
  const name = text.match(/^name = "([^"]+)"/m)?.[1];
  const version = text.match(/^version = "([^"]+)"/m)?.[1];
  return {name, version};
}

const provenance = readJson(provenancePath);
const lock = readJson(lockPath);
if (provenance && lock) {
  if (provenance.schemaVersion !== 1) {
    fail(`provenance schemaVersion must be 1`);
  }
  if (provenance.reference?.chromiumVersion !== lock.chromium?.version) {
    fail(`provenance Chromium version must match native/chromium.lock.json`);
  }
  if (provenance.reference?.chromiumRevision !== lock.chromium?.revision) {
    fail(`provenance Chromium revision must match native/chromium.lock.json`);
  }
  if (provenance.adblock?.version !== '0.12.0') {
    fail(`adblock must remain pinned to 0.12.0 for the M149 integration`);
  }
  if (
    provenance.adblock?.vcsRevision !==
    '29cf12d01b1a840eb860867c7c16b55de58a1eb8'
  ) {
    fail(`adblock VCS revision does not match Brave v1.91.180`);
  }
  if (!/^[0-9a-f]{64}$/.test(provenance.adblock?.crateArchiveSha256 ?? '')) {
    fail(`adblock crate archive checksum must be a 64-character SHA-256`);
  }
  if (
    provenance.compatibilityPins?.rmp?.version !== '0.8.11' ||
    provenance.compatibilityPins?.rmp?.crateArchiveSha256 !==
      '44519172358fd6d58656c86ab8e7fbc9e1490c3e8f14d35ed78ca0dd07403c9f'
  ) {
    fail(`rmp must remain pinned to Brave-compatible 0.8.11`);
  }

  const expectedPackages = new Map(Object.entries(provenance.packages ?? {}));
  const actualPackages = new Map();
  if (existsSync(vendorRoot)) {
    for (const directory of readdirSync(vendorRoot).sort()) {
      const packageRoot = path.join(vendorRoot, directory);
      if (!statSync(packageRoot).isDirectory()) {
        continue;
      }
      const cargoToml = path.join(packageRoot, 'Cargo.toml');
      const checksum = path.join(packageRoot, '.cargo-checksum.json');
      if (!existsSync(cargoToml)) {
        fail(`vendor/${directory} has no Cargo.toml`);
        continue;
      }
      if (!existsSync(checksum)) {
        fail(`vendor/${directory} has no .cargo-checksum.json`);
      } else {
        readJson(checksum);
      }
      const {name, version} = cargoPackageIdentity(cargoToml);
      if (!name || !version) {
        fail(`cannot read package identity from vendor/${directory}/Cargo.toml`);
        continue;
      }
      if (actualPackages.has(name)) {
        fail(`duplicate vendored package name ${name}`);
      }
      actualPackages.set(name, version);
    }
  }

  for (const [name, version] of expectedPackages) {
    if (actualPackages.get(name) !== version) {
      fail(
        `vendored ${name} version is ${actualPackages.get(name) ?? 'missing'}, ` +
          `expected ${version}`,
      );
    }
  }
  for (const name of actualPackages.keys()) {
    if (!expectedPackages.has(name)) {
      fail(`unrecorded vendored package ${name}`);
    }
  }
}

const adblockVcsPath = path.join(
  vendorRoot,
  'adblock-v0_12',
  '.cargo_vcs_info.json',
);
const adblockVcs = readJson(adblockVcsPath);
if (
  adblockVcs?.git?.sha1 !==
  '29cf12d01b1a840eb860867c7c16b55de58a1eb8'
) {
  fail(`vendored adblock .cargo_vcs_info.json revision is incorrect`);
}

for (const buildFile of walk(cratesRoot, (file) => path.basename(file) === 'BUILD.gn')) {
  const text = readFileSync(buildFile, 'utf8');
  if (text.includes('//third_party/rust/chromium_crates_io/vendor/')) {
    fail(
      `${path.relative(repoRoot, buildFile)} references Chromium's global vendor root`,
    );
  }

  for (const match of text.matchAll(
    /"\/\/third_party\/rust\/seoul_adblock\/vendor\/([^"]+)"/g,
  )) {
    const sourcePath = path.join(vendorRoot, match[1]);
    if (!existsSync(sourcePath)) {
      fail(
        `${path.relative(repoRoot, buildFile)} references missing ` +
          `vendor/${match[1]}`,
      );
    }
  }
  for (const match of text.matchAll(
    /"\/\/third_party\/rust\/seoul_adblock\/crates\/([^":]+(?:\/[^":]+)*):[^"]+"/g,
  )) {
    const dependencyBuild = path.join(cratesRoot, match[1], 'BUILD.gn');
    if (!existsSync(dependencyBuild)) {
      fail(
        `${path.relative(repoRoot, buildFile)} references missing generated ` +
          `crate target ${match[1]}`,
      );
    }
  }
}

for (const readme of walk(
  cratesRoot,
  (file) => path.basename(file) === 'README.chromium',
)) {
  const text = readFileSync(readme, 'utf8');
  const licensePath = text.match(
    /^License File: \/\/third_party\/rust\/seoul_adblock\/(.+)$/m,
  )?.[1];
  if (!licensePath) {
    fail(`${path.relative(repoRoot, readme)} has no Seoul-local License File`);
  } else if (!existsSync(path.join(overlay, licensePath))) {
    fail(
      `${path.relative(repoRoot, readme)} references missing license ${licensePath}`,
    );
  }
}

const adblockBuild = path.join(cratesRoot, 'adblock', 'v0_12', 'BUILD.gn');
if (existsSync(adblockBuild)) {
  const text = readFileSync(adblockBuild, 'utf8');
  for (const feature of [
    'css-validation',
    'debug-info',
    'full-regex-handling',
    'single-thread',
  ]) {
    if (!text.includes(`"${feature}"`)) {
      fail(`adblock BUILD.gn is missing required feature ${feature}`);
    }
  }
  if (text.includes('testonly = true')) {
    fail(`adblock production crate cannot be testonly`);
  }
}

const bitflagsBuild = path.join(cratesRoot, 'bitflags', 'v2', 'BUILD.gn');
if (existsSync(bitflagsBuild)) {
  const text = readFileSync(bitflagsBuild, 'utf8');
  if (!text.includes('"serde"')) {
    fail(`Seoul-local bitflags must enable serde for adblock-rust`);
  }
  if (!text.includes('rustc_metadata = "seoul_adblock_bitflags"')) {
    fail(
      `Seoul-local bitflags must use isolated rustc metadata to avoid ` +
        `colliding with Chromium's global crate`,
    );
  }
}

const regexBuild = path.join(cratesRoot, 'regex', 'v1', 'BUILD.gn');
if (
  !existsSync(regexBuild) ||
  !readFileSync(regexBuild, 'utf8').includes(
    '//third_party/rust/seoul_adblock/crates/regex_automata/v0_4:lib',
  )
) {
  fail(`Seoul-local regex must depend on Seoul-local regex-automata`);
}

const regexAutomataBuild = path.join(
  cratesRoot,
  'regex_automata',
  'v0_4',
  'BUILD.gn',
);
if (existsSync(regexAutomataBuild)) {
  const text = readFileSync(regexAutomataBuild, 'utf8');
  for (const feature of ['alloc', 'meta', 'nfa-thompson', 'syntax']) {
    if (!text.includes(`"${feature}"`)) {
      fail(`Seoul-local regex-automata is missing required feature ${feature}`);
    }
  }
}

const adblockCargoLock = path.join(vendorRoot, 'adblock-v0_12', 'Cargo.lock');
if (existsSync(adblockCargoLock)) {
  const text = readFileSync(adblockCargoLock, 'utf8');
  if (!/\[\[package\]\]\s+name = "rmp"\s+version = "0\.8\.11"/m.test(text)) {
    fail(`adblock-rust 0.12.0 lockfile must pin rmp 0.8.11`);
  }
}

const requiredBuildInputs = new Map([
  [
    path.join(cratesRoot, 'siphasher', 'v1', 'BUILD.gn'),
    '//third_party/rust/seoul_adblock/vendor/siphasher-v1/README.md',
  ],
  [
    path.join(cratesRoot, 'thiserror', 'v1', 'BUILD.gn'),
    '//third_party/rust/seoul_adblock/vendor/thiserror-v1/build/probe.rs',
  ],
]);
for (const [buildFile, requiredInput] of requiredBuildInputs) {
  if (
    !existsSync(buildFile) ||
    !readFileSync(buildFile, 'utf8').includes(`"${requiredInput}"`)
  ) {
    fail(
      `${path.relative(repoRoot, buildFile)} must declare input ${requiredInput}`,
    );
  }
}

const thiserrorBuild = path.join(
  cratesRoot,
  'thiserror',
  'v1',
  'BUILD.gn',
);
if (
  existsSync(thiserrorBuild) &&
  readFileSync(thiserrorBuild, 'utf8').includes(
    'build_script_outputs = [ "private.rs" ]',
  )
) {
  fail(`thiserror 1.0.69 must not declare the v2-only private.rs output`);
}

if (problems.length) {
  console.error('adblock-rust-vendor: FAIL');
  for (const problem of problems) {
    console.error(`  - ${problem}`);
  }
  process.exit(1);
}

const packageCount = Object.keys(provenance?.packages ?? {}).length;
console.log(
  `adblock-rust-vendor: OK (${packageCount} pinned packages, adblock 0.12.0)`,
);
