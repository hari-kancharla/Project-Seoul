#!/usr/bin/env bash
# Generate the out/SeoulBaseline build directory from the baseline GN args.
#
#   gen.sh            write args.gn and run `gn gen` (requires a synced checkout)
#   gen.sh --verify   print the effective args without a checkout (no gn run)
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

GN_FILE="$SEOUL_NATIVE_DIR/gn/macos-arm64-baseline.gn"
[ -f "$GN_FILE" ] || die "baseline GN file not found: $GN_FILE"

# Compose the effective args: the baseline file plus target_cpu, gated on arm64.
compose_args() {
  grep -vE '^\s*#' "$GN_FILE" | grep -vE '^\s*$'
  if [ "$(uname -m)" = "arm64" ]; then
    echo 'target_cpu = "arm64"'
  else
    die "host is $(uname -m); target_cpu=arm64 is only added on Apple Silicon"
  fi
  # The baseline turns Chromium's development assertions off, because they are
  # live in a non-official build and abort the browser in a user's hands. They
  # are still what catches invariant violations, so put them back on demand:
  # this is the configuration to run the native suites under, and the one to
  # reach for when reproducing a suspected invariant bug. It is last, so it
  # overrides the baseline's value.
  if [ -n "${SEOUL_DCHECKS:-}" ]; then
    echo 'dcheck_always_on = true'
  fi
}

VERIFY=0
[ "${1:-}" = "--verify" ] && VERIFY=1

stage "effective GN args (out/$OUT_DIR_NAME)"
EFFECTIVE="$(compose_args)"
printf '%s\n' "$EFFECTIVE"

if [ "$VERIFY" -eq 1 ]; then
  log "verify mode: GN file is present and well-formed; not running gn gen"
  exit 0
fi

stage "build-host gate"
"$SEOUL_SCRIPT_DIR/build-host-check.sh" || die "build-host gate failed; refusing to run gn gen on this host"

[ -d "$CHROMIUM_SRC/.git" ] || die "no Chromium checkout at $CHROMIUM_SRC (run fetch.sh + sync.sh first)"
use_depot_tools

stage "writing args.gn"
mkdir -p "$OUT_DIR"
printf '# Produced by native/scripts/gen.sh from native/gn/macos-arm64-baseline.gn\n%s\n' "$EFFECTIVE" > "$OUT_DIR/args.gn"
log "wrote $OUT_DIR/args.gn"

stage "gn gen"
( cd "$CHROMIUM_SRC" && gn gen "out/$OUT_DIR_NAME" )
log "OK: build directory generated at $OUT_DIR"
log "record these args in docs/native/chromium-baseline.md"
