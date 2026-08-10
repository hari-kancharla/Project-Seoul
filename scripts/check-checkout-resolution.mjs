#!/usr/bin/env node
// Guard: only one module may work out where the pinned Chromium checkout is.
//
// Four scripts used to derive it independently and only two of them handled git
// worktrees, so `node native/scripts/smoke.mjs` in a worktree reported the
// built browser missing on a machine that had one. Nothing failed loudly - the
// scripts just resolved to a directory that does not exist. Copies of a rule
// with three interacting parts (env override, legacy directory name, worktree
// fallback) drift silently, and a silent drift here reads as "the product is
// not built".
//
// So: native/scripts/checkout-root.mjs owns the JavaScript answer, resolve_root()
// in native/scripts/common.sh owns the shell answer, and this fails the build if
// anything else spells the checkout directory name for itself.

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const repoRoot = path.resolve(import.meta.dirname, '..');

// The two owners, plus this guard, are allowed to name the directory.
const OWNERS = new Set([
  'native/scripts/checkout-root.mjs',
  'native/scripts/common.sh',
  'scripts/check-checkout-resolution.mjs',
]);

const tracked = execFileSync('git', ['-C', repoRoot, 'ls-files', '*.mjs', '*.js', '*.sh'], {
  encoding: 'utf8',
})
  .split('\n')
  .filter(Boolean);

const offenders = [];
for (const rel of tracked) {
  // A test may name the directory in a fixture path: that is an expected
  // input to the resolver, not a second copy of it.
  if (OWNERS.has(rel) || /\.test\.mjs$|\/tests?\//.test(rel)) {
    continue;
  }
  const text = fs.readFileSync(path.join(repoRoot, rel), 'utf8');
  const lines = text.split('\n');
  lines.forEach((line, index) => {
    // A comment may mention the directory; only executable code is a problem.
    const code = line.replace(/^\s*(\/\/|#).*$/, '');
    if (/seoul-chromium(\.noindex)?['"\/\s]/.test(code) && !/^\s*(\/\/|#)/.test(line)) {
      offenders.push(`${rel}:${index + 1}: ${line.trim()}`);
    }
  });
}

if (offenders.length > 0) {
  console.error(
    'checkout-resolution: these re-derive the checkout location instead of importing it:',
  );
  for (const offender of offenders) {
    console.error(`  ${offender}`);
  }
  console.error(
    '\nImport { checkoutRoot, productBinary, checkoutSrc } from ' +
      'native/scripts/checkout-root.mjs, or source native/scripts/common.sh in shell.',
  );
  process.exit(1);
}

console.log(
  `checkout-resolution: OK (${tracked.length} scripts checked, ` +
    `${OWNERS.size - 1} owners of the rule)`,
);
