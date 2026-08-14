#!/usr/bin/env bash
# Clone depot_tools and perform the initial Chromium checkout into the EXTERNAL
# checkout root. Never fetches again when a valid checkout already exists, never
# deletes or resets anything. Revision pinning + dependency sync is done by sync.sh.
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

stage "preflight (doctor)"
"$SEOUL_SCRIPT_DIR/doctor.sh" || die "doctor reported a hard prerequisite failure; not fetching"

REV="$(lock_chromium_revision)"; [ -n "$REV" ] || die "no chromium.revision in lock file"
log "checkout root: $SEOUL_CHROMIUM_ROOT_ABS"
mkdir -p "$SEOUL_CHROMIUM_ROOT_ABS"

stage "depot_tools"
DEPOT_REV="$(lock_depot_revision)"; [ -n "$DEPOT_REV" ] || die "no depotTools.revision in lock file"
if is_git_checkout "$DEPOT_TOOLS_DIR"; then
  origin="$(git -C "$DEPOT_TOOLS_DIR" remote get-url origin 2>/dev/null || true)"
  case "$origin" in
    *depot_tools*) log "reusing existing depot_tools ($origin @ $(git_head_sha "$DEPOT_TOOLS_DIR"))" ;;
    *) die "unexpected repo at $DEPOT_TOOLS_DIR (origin=$origin); refusing to touch it" ;;
  esac
else
  [ -e "$DEPOT_TOOLS_DIR" ] && die "non-git path already exists at $DEPOT_TOOLS_DIR; refusing to overwrite"
  log "cloning depot_tools"
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS_DIR"
fi
# Pin depot_tools to the locked SHA (verifies the SHA exists). Self-update is
# disabled via use_depot_tools so the pin is honored.
if [ "$(git_head_sha "$DEPOT_TOOLS_DIR")" != "$DEPOT_REV" ]; then
  git_is_clean "$DEPOT_TOOLS_DIR" || die "depot_tools has local changes; refusing to repin"
  git -C "$DEPOT_TOOLS_DIR" cat-file -e "${DEPOT_REV}^{commit}" 2>/dev/null || git -C "$DEPOT_TOOLS_DIR" fetch origin
  git -C "$DEPOT_TOOLS_DIR" cat-file -e "${DEPOT_REV}^{commit}" 2>/dev/null || die "locked depot_tools revision $DEPOT_REV not found in the repo"
  git -C "$DEPOT_TOOLS_DIR" checkout --detach "$DEPOT_REV"
fi
log "depot_tools pinned at $(git_head_sha "$DEPOT_TOOLS_DIR")"
use_depot_tools

stage "chromium checkout"
if is_git_checkout "$CHROMIUM_SRC"; then
  log "existing checkout detected at $CHROMIUM_SRC (HEAD $(git_head_sha "$CHROMIUM_SRC"))"
  if ! git_is_clean "$CHROMIUM_SRC"; then
    die "existing checkout has uncommitted changes; resolve manually (this script will not reset it)"
  fi
  log "valid clean checkout present; skipping fetch (use sync.sh to pin to the locked revision)"
  exit 0
fi
[ -e "$CHROMIUM_SRC" ] && die "non-git path already exists at $CHROMIUM_SRC; refusing to overwrite"

CAFF=""; command -v caffeinate >/dev/null 2>&1 && CAFF="caffeinate -dimsu"
log "fetching chromium (--no-history) into $SEOUL_CHROMIUM_ROOT_ABS; this downloads tens of GiB"
( cd "$SEOUL_CHROMIUM_ROOT_ABS" && $CAFF fetch --no-history --nohooks chromium )

is_git_checkout "$CHROMIUM_SRC" || die "fetch did not produce a checkout at $CHROMIUM_SRC"
log "initial checkout complete at HEAD $(git_head_sha "$CHROMIUM_SRC")"
log "next: native/scripts/sync.sh  (pins to $REV and syncs dependencies + hooks)"
