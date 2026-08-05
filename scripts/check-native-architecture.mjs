#!/usr/bin/env node
// Project Seoul native architecture gate.
//
// Enforces the product-wide state-ownership rules in the native (C++) source,
// the equivalent of the harness gate. It targets the specific anti-patterns
// this codebase has had to remove:
//   1. a process-global singleton (base::NoDestructor) holding mutable product
//      state - e.g. the former process-global VerticalTabStripRegionView ->
//      host map. The sanctioned KeyedServiceFactory singleton is exempt: it is
//      stateless infrastructure that vends per-profile services.
//   2. a namespace-scope mutable global (a non-const variable defined outside
//      any function/class at file scope).
//
// It is a lint, not a compiler; it scans production .cc/.h (tests and fakes
// excluded). Findings are precise file:line so a real regression is actionable.

import { readdirSync, readFileSync, statSync } from 'node:fs';
import path from 'node:path';
import process from 'node:process';

const ROOT = process.cwd();
const NATIVE = path.join(ROOT, 'native', 'seoul');

const EXCLUDE_FILE = /(_unittest\.cc|_browsertest\.cc|fake_[a-z_]+\.(cc|h))$/;

function walk(dir) {
  const out = [];
  for (const entry of readdirSync(dir)) {
    const abs = path.join(dir, entry);
    if (statSync(abs).isDirectory()) {
      out.push(...walk(abs));
    } else if (/\.(cc|h)$/.test(entry)) {
      out.push(abs);
    }
  }
  return out;
}

const violations = [];
function flag(file, line, message) {
  violations.push(`${path.relative(ROOT, file)}:${line}: ${message}`);
}

for (const file of walk(NATIVE)) {
  const rel = path.relative(ROOT, file);
  if (EXCLUDE_FILE.test(rel)) continue;
  const lines = readFileSync(file, 'utf8').split('\n');

  lines.forEach((line, i) => {
    // Rule 1: base::NoDestructor holding mutable state. Exempt the sanctioned
    // KeyedServiceFactory pattern (NoDestructor<...Factory> in a GetInstance,
    // and the friend declaration). Explicit `NoDestructor<const T>` caches are
    // also safe: their contents cannot hold mutable profile/window state.
    if (/base::NoDestructor\s*<|friend\s+base::NoDestructor/.test(line)) {
      const isFactorySingleton = /NoDestructor<\s*\w*Factory\s*>/.test(line) ||
        /friend\s+base::NoDestructor<\s*\w*Factory\s*>/.test(line);
      const isImmutableCache = /NoDestructor\s*<\s*const\b/.test(line);
      if (!isFactorySingleton && !isImmutableCache) {
        flag(file, i + 1, 'process-global base::NoDestructor holding state; scope it to a profile/window service or a per-window owner instead');
      }
    }

    // Rule 2: namespace-scope mutable global. Heuristic for a definition at
    // file scope (column 0), of a plain type, that is not const/constexpr, not
    // a function/class/struct/namespace/using/typedef, not a macro, and not a
    // function prototype or definition (no '(' before ';' or '{').
    if (/^[A-Za-z_][\w:<>\* ]*\s+[A-Za-z_]\w*\s*(=|;)/.test(line)) {
      const trimmed = line.trim();
      const looksLikeDeclKeyword =
        /^(const|constexpr|static|inline|using|typedef|namespace|class|struct|enum|template|friend|return|extern|#)/.test(trimmed);
      const looksCallableOrType = trimmed.includes('(') || trimmed.includes('{');
      if (!looksLikeDeclKeyword && !looksCallableOrType) {
        flag(file, i + 1, `possible namespace-scope mutable global ('${trimmed}'); use scoped ownership (const, a service member, or a per-window store)`);
      }
    }
  });
}

if (violations.length > 0) {
  console.error('native-architecture: FAILED');
  for (const v of violations) console.error(`  - ${v}`);
  process.exit(1);
}
console.log('native-architecture: OK (no unscoped process-global singletons or namespace-scope mutable globals in production source)');
