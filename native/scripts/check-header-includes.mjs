#!/usr/bin/env node
// Header-include audit for the Seoul native source.
//
// Two passes:
//   1. Direct-include rules: specific Seoul headers must pull specific deps.
//   2. Chromium include resolution: every #include of a chrome/, components/,
//      content/, or ui/ path across native/seoul must resolve to a real file in
//      the pinned checkout. This catches mis-pathed upstream includes that the
//      rule pass and a non-compiling host would otherwise miss. The checkout is
//      resolved like native/scripts/common.sh (SEOUL_CHROMIUM_ROOT, else the
//      sibling seoul-chromium); if it is absent (e.g. CI without a checkout) the
//      resolution pass is skipped, not failed.
// This is NOT equivalent to compilation; it catches obvious missing/wrong includes.

import fs from 'node:fs';
import path from 'node:path';

const repoRoot = path.resolve(import.meta.dirname, '../..');
const seoulRoot = path.join(repoRoot, 'native/seoul/browser');
const seoulTree = path.join(repoRoot, 'native/seoul');

const rules = [
  {
    file: 'organization/organization_types.h',
    mustInclude: ['organization_limits.h'],
    mustReference: ['kOrganizationSchemaVersion'],
  },
  {
    file: 'organization/organization_store.h',
    mustInclude: ['organization_errors.h', 'organization_types.h'],
  },
  {
    file: 'organization/organization_model.h',
    mustInclude: ['organization_errors.h', 'organization_types.h'],
  },
  {
    file: 'lifecycle/lifecycle_events.h',
    mustInclude: ['lifecycle_identity.h', 'organization_types.h'],
  },
];

let failed = false;

// Pass 1: direct-include rules.
for (const rule of rules) {
  const full = path.join(seoulRoot, rule.file);
  const text = fs.readFileSync(full, 'utf8');
  for (const inc of rule.mustInclude) {
    if (!text.includes(inc)) {
      console.error(`${rule.file}: missing direct include of ${inc}`);
      failed = true;
    }
  }
  for (const ref of rule.mustReference ?? []) {
    if (!text.includes(ref)) {
      console.error(`${rule.file}: expected reference to ${ref}`);
      failed = true;
    }
  }
}

// organization_types.h must not use kOrganizationSchemaVersion without limits.
const typesPath = path.join(seoulRoot, 'organization/organization_types.h');
const typesText = fs.readFileSync(typesPath, 'utf8');
if (
  typesText.includes('kOrganizationSchemaVersion') &&
  !typesText.includes('organization_limits.h')
) {
  console.error(
    'organization_types.h uses kOrganizationSchemaVersion without organization_limits.h',
  );
  failed = true;
}

// Pass 2: resolve Chromium includes against the pinned checkout.
function resolveChromiumSrc() {
  return checkoutSrc();
}

function walk(dir) {
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      out.push(...walk(full));
    } else if (entry.isFile() && /\.(cc|h)$/.test(entry.name)) {
      out.push(full);
    }
  }
  return out;
}

const chromiumSrc = resolveChromiumSrc();
const UPSTREAM = /^#include\s+"((?:chrome|components|content|ui)\/[^"]+)"/;

if (!fs.existsSync(chromiumSrc)) {
  console.log(
    `note: pinned checkout not found at ${chromiumSrc}; skipping Chromium ` +
      'include-resolution pass (set SEOUL_CHROMIUM_ROOT to enable).',
  );
} else {
  let checked = 0;
  for (const file of walk(seoulTree)) {
    const lines = fs.readFileSync(file, 'utf8').split('\n');
    lines.forEach((line, i) => {
      const m = line.match(UPSTREAM);
      if (!m) {
        return;
      }
      // Generated Mojo bindings headers (*.mojom.h / *.mojom-*.h) are build
      // outputs, not source files, so they never exist in the checkout tree.
      // Their generating .mojom is depended on via the BUILD graph instead.
      if (/\.mojom(-[a-z]+)?\.h$/.test(m[1])) {
        return;
      }
      checked++;
      if (!fs.existsSync(path.join(chromiumSrc, m[1]))) {
        const rel = path.relative(repoRoot, file);
        console.error(
          `${rel}:${i + 1}: include does not resolve in the checkout: ${m[1]}`,
        );
        failed = true;
      }
    });
  }
  console.log(`resolved ${checked} Chromium includes against the checkout`);
}

if (failed) {
  process.exit(1);
}
console.log('OK: header direct-include audit passed');
