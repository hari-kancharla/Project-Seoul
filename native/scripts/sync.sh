#!/usr/bin/env bash
# Pin the existing checkout to the locked Chromium revision and sync dependencies.
# Verifies the lock BEFORE and AFTER. Never resets a dirty checkout.
#
#   sync.sh                 pin + gclient sync to the locked revision
#   sync.sh --verify-only   only verify the current checkout matches the lock
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

# --verify-only delegates to the read-only checkout verifier (lock schema, src and
# depot_tools HEADs, .gclient structure, expected upstream paths, tracked-edit
# guard, and gclient validate). The precise definition of an expected vs blocked
# "dirty" state (gclient submodule gitlinks and untracked dependency dirs are
# expected; tracked non-submodule edits are blocked) lives in common.sh's
# src_has_user_edits.
if [ "${1:-}" = "--verify-only" ]; then
  exec "$SEOUL_SCRIPT_DIR/verify-checkout.sh"
fi

REV="$(lock_chromium_revision)"; [ -n "$REV" ] || die "no chromium.revision in lock file"
log "locked chromium revision: $REV"

is_git_checkout "$CHROMIUM_SRC" || die "no Chromium checkout at $CHROMIUM_SRC (run fetch.sh first)"

current="$(git_head_sha "$CHROMIUM_SRC")"
log "checkout HEAD: ${current:-<unknown>}"

if src_has_user_edits; then
  die "checkout has uncommitted edits to tracked files; refusing to sync (will not reset your work)"
fi

use_depot_tools
stage "pin to locked revision"
if ! git -C "$CHROMIUM_SRC" cat-file -e "${REV}^{commit}" 2>/dev/null; then
  log "fetching locked revision object (shallow)"
  git -C "$CHROMIUM_SRC" fetch --depth 1 origin "$REV"
fi
git -C "$CHROMIUM_SRC" checkout --detach "$REV"

stage "gclient sync (deps for the locked revision; shallow, no patches)"
CAFF=""; command -v caffeinate >/dev/null 2>&1 && CAFF="caffeinate -dimsu"
( cd "$CHROMIUM_SRC" && $CAFF gclient sync --no-history --with_branch_heads --with_tags --revision "src@$REV" -D )

stage "gclient runhooks"
( cd "$CHROMIUM_SRC" && $CAFF gclient runhooks )

stage "verify after sync"
after="$(git_head_sha "$CHROMIUM_SRC")"
[ "$after" = "$REV" ] || die "post-sync HEAD ($after) does not match locked revision ($REV)"
! src_has_user_edits || warn "checkout has local edits to tracked files after sync"
( cd "$CHROMIUM_SRC" && gclient validate )
log "OK: checkout pinned and synced at $REV"
