#!/usr/bin/env node
// Project Seoul patch-overlap gate.
//
// Catches the one failure mode that silently rots a reversible patch series:
// a patch regenerated from a checkout that already had a *later* patch applied,
// so it carries that later patch's additions as well as its own.
//
// When two patches add the same lines to the same file the series stops being
// a series. Forward, the later patch cannot apply because its additions are
// already present. Backward, reversing the later one removes them and the
// earlier one can no longer find what it expects. The tree still builds,
// because someone applied the patches incrementally while authoring them, so
// nothing visibly breaks until you try to rebase onto a newer Chromium - at
// which point the series cannot be replayed at all.
//
// This runs without a Chromium checkout, which is the whole point: `patches.sh
// verify` proves the property directly but needs a clean checkout, and no CI
// job has one. A static gate that runs everywhere beats a thorough gate that
// never runs.

import fs from 'node:fs';
import path from 'node:path';

const repoRoot = path.resolve(import.meta.dirname, '..');
const manifestPath = path.join(repoRoot, 'native/patches/manifest.json');
const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
const patchDir = path.join(repoRoot, manifest.patchDir);

// The signature of the defect is not "these two patches share a line" - a
// later patch legitimately rewrites a region an earlier one created, so its
// added lines can repeat the earlier patch's. The signature is that a later
// patch's contribution to a file is *already almost entirely present* in an
// earlier patch, because the earlier patch was regenerated on top of it. So
// the metric is the fraction of the later patch's additions that the earlier
// one already makes, not raw shared lines.
const OVERLAP_FRACTION = 0.8;
const MIN_SHARED_LINES = 3;

// Lines that recur legitimately across unrelated patches and carry no identity
// of their own. Excluded so boilerplate cannot carry a comparison on its own.
function isTrivial(line) {
  const t = line.trim();
  if (t.length < 8) return true;
  return /^(#endif|#else|#if\s|\}|\{|\)|\);|return;|break;|namespace\s|\/\/)/.test(t);
}

// Added lines per target file. `+++ b/path` sets the current file; a line
// starting with a single '+' is an addition.
function addedLinesByFile(patchText) {
  const byFile = new Map();
  let file = null;
  for (const line of patchText.split('\n')) {
    if (line.startsWith('+++ ')) {
      const p = line.slice(4).trim();
      file = p === '/dev/null' ? null : p.replace(/^b\//, '');
      if (file && !byFile.has(file)) byFile.set(file, []);
      continue;
    }
    if (line.startsWith('+++') || line.startsWith('---')) continue;
    if (file && line.startsWith('+')) {
      byFile.get(file).push(line.slice(1).replace(/\s+$/, ''));
    }
  }
  return byFile;
}

// Keeps the line exactly as added, minus trailing whitespace. Leading
// indentation is not noise here: the same statement at a different nesting
// level is different code in a different place, and collapsing the two reports
// a duplicate where a patch merely moved something.
function significant(lines) {
  return lines.filter((l) => !isTrivial(l));
}

const patches = [...manifest.patches].sort((x, y) => x.order - y.order);
const parsed = patches.map((entry) => {
  const file = path.join(patchDir, entry.file);
  if (!fs.existsSync(file)) {
    console.error(`patch-overlap: missing patch file ${entry.file}`);
    process.exit(1);
  }
  return { entry, byFile: addedLinesByFile(fs.readFileSync(file, 'utf8')) };
});

const findings = [];
for (let i = 0; i < parsed.length; i++) {
  for (let j = i + 1; j < parsed.length; j++) {
    const earlier = parsed[i];
    const later = parsed[j];
    for (const [target, laterRaw] of later.byFile) {
      const earlierRaw = earlier.byFile.get(target);
      if (!earlierRaw) continue;
      const laterLines = significant(laterRaw);
      if (laterLines.length < MIN_SHARED_LINES) continue;
      const earlierSet = new Set(significant(earlierRaw));
      const shared = laterLines.filter((l) => earlierSet.has(l));
      const fraction = shared.length / laterLines.length;
      if (shared.length >= MIN_SHARED_LINES && fraction >= OVERLAP_FRACTION) {
        findings.push({
          earlier: earlier.entry,
          later: later.entry,
          target,
          shared,
          total: laterLines.length,
          fraction,
        });
      }
    }
  }
}

if (findings.length > 0) {
  for (const f of findings) {
    console.error(
      `patch-overlap: ${f.later.file} adds ${f.shared.length} of its ` +
        `${f.total} lines to ${f.target}, and ${f.earlier.file} already ` +
        `adds them (${Math.round(f.fraction * 100)}%)`,
    );
    for (const line of f.shared.slice(0, 3)) {
      console.error(`    + ${line.trim().slice(0, 96)}`);
    }
    if (f.shared.length > 3) {
      console.error(`    ... ${f.shared.length - 3} more`);
    }
    console.error(
      `  Patch order ${f.earlier.order} was almost certainly regenerated from ` +
        `a checkout that already had order ${f.later.order} applied, so it ` +
        `carries that patch's change as well as its own. Regenerate it to ` +
        `contain only its own change: while both add the same lines, order ` +
        `${f.later.order} cannot apply to a pristine checkout and order ` +
        `${f.earlier.order} cannot be reversed.`,
    );
  }
  console.error(`patch-overlap: FAILED (${findings.length} overlap(s))`);
  process.exit(1);
}

console.log(
  `patch-overlap: OK (${patches.length} patches, no patch duplicates another's additions)`,
);
