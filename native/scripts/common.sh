#!/usr/bin/env bash
# Shared helpers for the Seoul native-bootstrap scripts.
#
# Source this file; do not execute it directly. It resolves the external Chromium
# checkout root, defines logging/guard helpers, and reads the pinned revision from
# native/chromium.lock.json. It never deletes or resets anything.
set -euo pipefail

SEOUL_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SEOUL_REPO_ROOT="$(cd "$SEOUL_SCRIPT_DIR/../.." && pwd)"
SEOUL_NATIVE_DIR="$SEOUL_REPO_ROOT/native"
LOCK_FILE="$SEOUL_NATIVE_DIR/chromium.lock.json"

log()   { printf '[seoul-native] %s\n' "$*"; }
stage() { printf '\n==> %s\n' "$*"; }
warn()  { printf '[warn] %s\n' "$*" >&2; }
die()   { printf '[error] %s\n' "$*" >&2; exit 1; }

need_cmd() { command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"; }

# Chromium's current Python tooling uses syntax introduced in Python 3.10.
# Resolve one executable once, then put its directory first so Ninja actions
# that invoke the literal `python3` use the same interpreter. Prefer an
# explicit override, then a compatible host interpreter, then depot_tools'
# pinned bootstrap interpreter. SEOUL_PYTHON3 remains the deterministic escape
# hatch for custom checkouts.
python_is_build_compatible() {
  local executable="${1:-}"
  [ -x "$executable" ] &&
    "$executable" -c 'import sys; raise SystemExit(sys.version_info < (3, 10))' \
      >/dev/null 2>&1
}

resolve_build_python() {
  local explicit="${SEOUL_PYTHON3:-}"
  local candidate=""
  if [ -n "$explicit" ]; then
    assert_safe_path "$explicit" "SEOUL_PYTHON3"
    [ -x "$explicit" ] || die "SEOUL_PYTHON3 is not executable: $explicit"
    candidate="$(dirname "$explicit")/python3"
    [ -x "$candidate" ] ||
      die "SEOUL_PYTHON3 must have a python3 executable in the same directory: $explicit"
    python_is_build_compatible "$candidate" ||
      die "SEOUL_PYTHON3 must be Python 3.10 or newer: $explicit"
    printf '%s\n' "$candidate"
    return 0
  fi

  for candidate in \
    "$(command -v python3 2>/dev/null || true)" \
    /opt/homebrew/bin/python3 \
    /usr/local/bin/python3 \
    "$DEPOT_TOOLS_DIR/python-bin/python3"; do
    if python_is_build_compatible "$candidate"; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

# Reject empty, relative, space-containing or shell-unsafe paths.
assert_safe_path() {
  local p="${1:-}" what="${2:-path}"
  [ -n "$p" ] || die "$what is empty"
  case "$p" in
    /*) : ;;
    *)  die "$what must be an absolute path: $p" ;;
  esac
  case "$p" in
    *" "*) die "$what contains a space (the Chromium build does not tolerate this): $p" ;;
  esac
  case "$p" in
    *'$'* | *'`'* | *';'* | *'|'* | *'&'* | *'*'* | *'?'* | *'<'* | *'>'* | *'('* | *')'* | *'!'*)
      die "$what contains unsafe shell characters: $p" ;;
  esac
}

# Resolve the external checkout root. SEOUL_CHROMIUM_ROOT overrides; otherwise a
# sibling of the Project Seoul repo named seoul-chromium.noindex (the .noindex
# suffix keeps Spotlight from indexing the checkout, so its bundled test apps
# stay out of app search). A legacy sibling named seoul-chromium is honored when
# the .noindex one is absent. Never inside the repo.
resolve_root() {
  local root="${SEOUL_CHROMIUM_ROOT:-}"
  if [ -z "$root" ]; then
    local siblings
    siblings="$(cd "$SEOUL_REPO_ROOT/.." && pwd)"
    root="$siblings/seoul-chromium.noindex"
    if [ ! -d "$root" ] && [ -d "$siblings/seoul-chromium" ]; then
      root="$siblings/seoul-chromium"
    fi
  fi
  case "$root" in /*) : ;; *) root="$(pwd)/$root" ;; esac
  assert_safe_path "$root" "SEOUL_CHROMIUM_ROOT"
  case "$root/" in
    "$SEOUL_REPO_ROOT"/*) die "checkout root must be OUTSIDE the Project Seoul repo: $root" ;;
  esac
  printf '%s\n' "$root"
}

SEOUL_CHROMIUM_ROOT_ABS="$(resolve_root)"
DEPOT_TOOLS_DIR="$SEOUL_CHROMIUM_ROOT_ABS/depot_tools"
CHROMIUM_SRC="$SEOUL_CHROMIUM_ROOT_ABS/src"
OUT_DIR_NAME="SeoulBaseline"
OUT_DIR="$CHROMIUM_SRC/out/$OUT_DIR_NAME"
CHROMIUM_BINARY="$OUT_DIR/Seoul.app/Contents/MacOS/Seoul"
EVIDENCE_DIR="$SEOUL_NATIVE_DIR/evidence"

# Repository-owned Seoul native source and the Chromium integration patch series.
SEOUL_SRC_DIR="$SEOUL_NATIVE_DIR/seoul"
# Seoul-owned, pinned Rust dependency closure for the native blocker. Keeping it
# outside Chromium's global Cargo graph prevents unrelated upstream Rust crates
# from being regenerated when the blocker dependency graph changes.
SEOUL_ADBLOCK_RUST_DIR="$SEOUL_NATIVE_DIR/third_party/rust/seoul_adblock"
# The canonical cross-language wire contract (schemas + shared fixtures),
# mirrored into the overlay at src/seoul/protocol by materialize.sh.
SEOUL_PROTOCOL_DIR="$SEOUL_REPO_ROOT/protocol"
PATCHES_DIR="$SEOUL_NATIVE_DIR/patches/chromium"
PATCH_MANIFEST="$SEOUL_NATIVE_DIR/patches/manifest.json"
# Where Seoul-owned source is materialized inside the external checkout.
SEOUL_OVERLAY_DEST="$CHROMIUM_SRC/seoul"
SEOUL_ADBLOCK_RUST_DEST="$CHROMIUM_SRC/third_party/rust/seoul_adblock"

# Build-host minimums (overridable). These gate gen/build only; the checkout
# itself does not require them.
SEOUL_MIN_RAM_GIB="${SEOUL_MIN_RAM_GIB:-16}"
SEOUL_MIN_BUILD_FREE_GIB="${SEOUL_MIN_BUILD_FREE_GIB:-150}"

# Read a dotted field from the lock file (no jq dependency).
lock_field() {
  local path="$1"
  [ -f "$LOCK_FILE" ] || die "lock file not found: $LOCK_FILE"
  python3 - "$LOCK_FILE" "$path" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
cur = data
for part in sys.argv[2].split('.'):
    if isinstance(cur, dict) and part in cur:
        cur = cur[part]
    else:
        cur = None
        break
print('' if cur is None else cur)
PY
}

lock_chromium_revision() { lock_field chromium.revision; }
lock_chromium_version()  { lock_field chromium.version; }
lock_depot_revision()    { lock_field depotTools.revision; }

# Prepend depot_tools to PATH and disable its self-update so the pinned revision
# is honored.
use_depot_tools() {
  [ -d "$DEPOT_TOOLS_DIR" ] || die "depot_tools not present at $DEPOT_TOOLS_DIR (run fetch.sh first)"
  local build_python
  build_python="$(resolve_build_python)" ||
    die "Chromium requires Python 3.10 or newer; install it or set SEOUL_PYTHON3 to its absolute executable path"
  export PATH="$(dirname "$build_python"):$DEPOT_TOOLS_DIR:$PATH"
  export DEPOT_TOOLS_UPDATE=0
}

# Current detached/checked-out SHA of a git tree (empty if not a repo).
git_head_sha() { git -C "$1" rev-parse HEAD 2>/dev/null || true; }

# True if the working tree at $1 has no uncommitted changes.
git_is_clean() { [ -z "$(git -C "$1" status --porcelain 2>/dev/null)" ]; }

# Free space (GiB, integer) on the volume holding the nearest existing ancestor.
free_gib_for() {
  local p="$1"
  while [ ! -e "$p" ] && [ "$p" != "/" ]; do p="$(dirname "$p")"; done
  df -k "$p" | awk 'NR==2 { printf "%d", $4/1024/1024 }'
}

# Physical RAM in whole GiB.
mem_gib() { printf '%d' "$(( $(sysctl -n hw.memsize 2>/dev/null || echo 0) / 1024 / 1024 / 1024 ))"; }

# A gclient checkout is intentionally NOT "git clean": gclient manages
# dependencies as git submodule gitlinks (which show as " M third_party/...") and
# as untracked dependency directories (which show as "?? ..."). Both are expected.
# A real, blocking edit is a modified or deleted TRACKED, non-submodule file. This
# helper returns success only when such a real edit exists.
src_has_user_edits() {
  [ -n "$(git -C "$CHROMIUM_SRC" status --porcelain --ignore-submodules=all --untracked-files=no 2>/dev/null)" ]
}

# Resolve the Ninja job count. SEOUL_NINJA_JOBS overrides (validated positive
# integer). The default is conservative and memory-aware: about one job per 4 GiB
# of RAM, floored at 2. There is no unconditional hard-coded value.
resolve_jobs() {
  local override="${SEOUL_NINJA_JOBS:-}"
  if [ -n "$override" ]; then
    case "$override" in
      '' | *[!0-9]*) die "SEOUL_NINJA_JOBS must be a positive integer, got: '$override'" ;;
    esac
    [ "$override" -ge 1 ] || die "SEOUL_NINJA_JOBS must be >= 1"
    [ "$override" -le 256 ] || die "SEOUL_NINJA_JOBS is unreasonably large: $override"
    printf '%s\n' "$override"
    return 0
  fi
  local jobs=$(( $(mem_gib) / 4 ))
  [ "$jobs" -lt 2 ] && jobs=2
  printf '%s\n' "$jobs"
}
