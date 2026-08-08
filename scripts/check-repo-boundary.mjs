#!/usr/bin/env node
// Repository-boundary guard. Fails if Project Seoul tracks or has a non-ignored
// untracked file that must stay out of the repo: the external Chromium checkout or its source, Chromium build
// output, browser profiles, secrets/keys, or generated audit evidence.
// Seoul-owned native source (native/seoul/...) and the design labs (apps/...)
// are repository-owned and allowed.

import { execSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

let repositoryFiles = [];
try {
  repositoryFiles = execSync(
    'git ls-files --cached --others --exclude-standard',
    { cwd: repoRoot, encoding: 'utf8' },
  )
    .split('\n')
    .map((s) => s.trim())
    .filter(Boolean);
} catch (e) {
  console.error('repo-boundary: cannot list repository files: ' + e.message);
  process.exit(2);
}

// Each rule: a regex over the repository-visible path and a human reason.
const DENY = [
  [/^seoul-chromium\//, 'external Chromium checkout leaked into the repo'],
  [/(^|\/)depot_tools\//, 'depot_tools must not be tracked'],
  [/(^|\/)\.gclient(_entries)?$/, 'gclient checkout metadata must not be tracked'],
  [/(^|\/)out\/.+/, 'Chromium build output (out/...) must not be tracked'],
  [/\.app\/Contents\//, 'built application bundle must not be tracked'],
  [/\.(ninja|ninja_log|ninja_deps)$/, 'Ninja build files must not be tracked'],
  [/(^|\/)native\/evidence\//, 'generated audit evidence must not be tracked'],
  // Verification captures land in the working directory and are easy to commit
  // by reflex; eleven of them (19 MB) were tracked here. Product artwork lives
  // under native/seoul/resources/, so a bare image at the root is always scratch.
  [
    /^[^/]+\.(png|jpg|jpeg|gif|webp|bmp|tiff|mov|mp4|zip)$/i,
    'screenshots and captures must not be tracked at the repository root (native/evidence/ is ignored)',
  ],
  [/(^|\/)(Cookies|Login Data|Local State|History|Web Data)$/, 'browser profile data must not be tracked'],
  [/(^|\/)secrets\//, 'secrets directory must not be tracked'],
  [/\.(pem|key|p12|keychain)$/, 'private key/credential material must not be tracked'],
  [/(^|\/)\.env(\.[A-Za-z0-9_]+)?$/, 'environment/secret file must not be tracked'],
];
// Explicit allowances (example env files are fine).
const ALLOW = [/(^|\/)\.env\.example$/];

const violations = [];
for (const f of repositoryFiles) {
  if (ALLOW.some((re) => re.test(f))) continue;
  for (const [re, reason] of DENY) {
    if (re.test(f)) violations.push(`${f}  (${reason})`);
  }
}

if (violations.length) {
  console.error('repo-boundary: FAIL');
  for (const v of violations) console.error('  - ' + v);
  process.exit(1);
}
console.log(
  `repo-boundary: OK (${repositoryFiles.length} tracked/untracked files, ` +
    'no Chromium source/build/profile/secret/evidence)',
);
