// The one place that answers "where is the pinned Chromium checkout".
//
// Four scripts used to work this out for themselves and only two of them knew
// about git worktrees, so in a worktree the other two resolved to a sibling
// directory that holds nothing and reported the built browser missing on a
// machine that had one. The rule is small but it has three parts that all have
// to agree - the env override, the legacy directory name, and the worktree
// fallback - which is exactly the kind of rule that drifts when it is copied.
// Import it; do not re-derive it. scripts/check-checkout-resolution.mjs fails
// the build if a script hand-rolls it again.
//
// This is the JavaScript half of resolve_root() in native/scripts/common.sh and
// must keep the same answer as the shell half.

import { existsSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const defaultStart = path.resolve(here, '..', '..');

/// A git worktree has its own root, and that root's sibling holds no checkout.
/// Resolve the MAIN working tree so every caller agrees on the answer whether
/// it is run from the repository or from a worktree of it.
export function mainWorktreeRoot(start = defaultStart) {
  try {
    const commonDir = execFileSync(
      'git',
      ['-C', start, 'rev-parse', '--path-format=absolute', '--git-common-dir'],
      { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] },
    ).trim();
    if (commonDir) {
      return path.resolve(commonDir, '..');
    }
  } catch {
    // Not a git checkout, or no git on PATH: the plain sibling is all there is.
  }
  return start;
}

/// The checkout root. SEOUL_CHROMIUM_ROOT overrides; otherwise the sibling of
/// the main working tree named seoul-chromium.noindex, with the legacy
/// seoul-chromium honored when the .noindex one is absent.
export function checkoutRoot({ env = process.env, start = defaultStart } = {}) {
  if ((env.SEOUL_CHROMIUM_ROOT || '').trim()) {
    return path.resolve(env.SEOUL_CHROMIUM_ROOT);
  }
  const repoRoot = mainWorktreeRoot(start);
  const found = ['seoul-chromium.noindex', 'seoul-chromium']
    .map((name) => path.resolve(repoRoot, '..', name))
    .find((candidate) => existsSync(candidate));
  return found ?? path.resolve(repoRoot, '..', 'seoul-chromium.noindex');
}

/// The built product binary inside that checkout. SEOUL_CHROMIUM_BINARY
/// overrides. The bundle is Seoul.app because the branding patches rename it;
/// an out directory can still hold a stale Chromium.app from an older build,
/// and that one is not the product.
export function productBinary({ env = process.env, start = defaultStart } = {}) {
  if ((env.SEOUL_CHROMIUM_BINARY || '').trim()) {
    return path.resolve(env.SEOUL_CHROMIUM_BINARY);
  }
  return path.join(
    checkoutRoot({ env, start }),
    'src',
    'out',
    'SeoulBaseline',
    'Seoul.app',
    'Contents',
    'MacOS',
    'Seoul',
  );
}

/// The materialized Seoul overlay inside the checkout.
export function checkoutSrc(options = {}) {
  return path.join(checkoutRoot(options), 'src');
}
