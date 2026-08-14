#!/usr/bin/env bash
# Build only the `chrome` target. Refuses to run unless the build-host gate passes,
# so a build cannot start on an underpowered host. Job count is configurable
# (SEOUL_NINJA_JOBS) with a conservative memory-aware default. Records timing
# and output size. Does not build `all`, unit tests, or browser tests.
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

stage "build-host gate"
"$SEOUL_SCRIPT_DIR/build-host-check.sh" || die "build-host gate failed; refusing to build (this machine cannot build Chromium)"

is_git_checkout "$CHROMIUM_SRC" || die "no Chromium checkout at $CHROMIUM_SRC (run fetch.sh + sync.sh first)"
[ -f "$OUT_DIR/args.gn" ]   || die "build dir not generated; run gen.sh first"

REV="$(lock_chromium_revision)"
use_depot_tools

JOBS="$(resolve_jobs)"
CAFF=""; command -v caffeinate >/dev/null 2>&1 && CAFF="caffeinate -dimsu"

stage "build chrome (revision $REV, -j $JOBS)"
start_epoch="$(date +%s)"
log "start: $(date -Iseconds)"

set +e
( cd "$CHROMIUM_SRC" && $CAFF autoninja -C "out/$OUT_DIR_NAME" -j "$JOBS" chrome )
rc=$?
set -e

end_epoch="$(date +%s)"
elapsed=$(( end_epoch - start_epoch ))
log "end:   $(date -Iseconds)"
log "elapsed: $(( elapsed / 3600 ))h $(( (elapsed % 3600) / 60 ))m $(( elapsed % 60 ))s"

if [ "$rc" -ne 0 ]; then
  die "build failed (rc=$rc). Capture the exact error; permit at most two narrow corrections. Do not broaden GN flags or disable security."
fi

stage "build artifacts"
[ -x "$CHROMIUM_BINARY" ] || die "build reported success but binary not found at $CHROMIUM_BINARY"
log "binary: $CHROMIUM_BINARY"
log "output size: $(du -sh "$OUT_DIR" | awk '{print $1}')"
log "OK: chrome built at revision $REV"
