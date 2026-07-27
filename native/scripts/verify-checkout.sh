#!/usr/bin/env bash
# Read-only verification of the external Chromium checkout against the lock.
# Downloads nothing and modifies nothing. Classifies each finding as PASS, WARN, or
# FAIL, and explicitly states which runtime/build evidence is still unavailable.
#
# Exit nonzero if any FAIL. HEAD equality alone is NOT treated as proof of a
# complete checkout; dependency/consistency is checked via gclient validate.
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

fail=0
pass() { printf 'PASS  %s\n' "$*"; }
bad()  { printf 'FAIL  %s\n' "$*"; fail=1; }
note() { printf 'WARN  %s\n' "$*"; }

stage "Seoul checkout verification (read-only)"
log "checkout root: $SEOUL_CHROMIUM_ROOT_ABS"

# --- Lock file schema + required fields ---
LOCK_REV=""; LOCK_DEPOT=""; LOCK_VER=""
if [ ! -f "$LOCK_FILE" ]; then
  bad "lock file missing: $LOCK_FILE"
else
  lockcheck="$(python3 - "$LOCK_FILE" <<'PY'
import json, re, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception as e:
    print("ERR cannot parse lock: %s" % e); sys.exit(0)
errs = []
if d.get("schemaVersion") != 1: errs.append("schemaVersion != 1")
rev = (d.get("chromium") or {}).get("revision", "")
ver = (d.get("chromium") or {}).get("version", "")
dep = (d.get("depotTools") or {}).get("revision", "")
if not re.fullmatch(r"[0-9a-f]{40}", rev or ""): errs.append("chromium.revision not a 40-hex SHA")
if not ver: errs.append("chromium.version missing")
if not re.fullmatch(r"[0-9a-f]{40}", dep or ""): errs.append("depotTools.revision not a 40-hex SHA")
if errs:
    print("ERR " + "; ".join(errs))
else:
    print("OK %s %s %s" % (rev, dep, ver))
PY
)"
  if [ "${lockcheck%% *}" = "OK" ]; then
    read -r _ LOCK_REV LOCK_DEPOT LOCK_VER <<< "$lockcheck"
    pass "lock schema and required fields ($LOCK_VER)"
  else
    bad "lock file invalid: ${lockcheck#ERR }"
  fi
fi

# --- Checkout path outside the repo ---
case "$SEOUL_CHROMIUM_ROOT_ABS/" in
  "$SEOUL_REPO_ROOT"/*) bad "checkout root is inside the Project Seoul repo";;
  *) pass "checkout root is outside the Project Seoul repo";;
esac

# --- Required tools ---
for c in git python3; do command -v "$c" >/dev/null 2>&1 && pass "tool present: $c" || bad "tool missing: $c"; done
[ -x "$DEPOT_TOOLS_DIR/gclient" ] && pass "depot_tools/gclient present" || note "depot_tools/gclient not present (gclient validate will be skipped)"

# --- src checkout ---
if [ ! -d "$CHROMIUM_SRC/.git" ]; then
  bad "no Chromium checkout at $CHROMIUM_SRC"
else
  head="$(git_head_sha "$CHROMIUM_SRC")"
  if [ -n "$LOCK_REV" ] && [ "$head" = "$LOCK_REV" ]; then pass "src HEAD equals locked revision ($head)"
  else bad "src HEAD ($head) does not equal locked revision (${LOCK_REV:-?})"; fi

  if src_has_user_edits; then
    # Tracked edits are legitimate in exactly one case: they are precisely the
    # applied Seoul patch series (the runbook applies patches before gen/build).
    # Simulate the complete reverse operation in a temporary index. A single
    # multi-patch `git apply --reverse --check` does not carry each patch's
    # result into the next check, so dependent patches can be reported as false
    # failures even when the checkout is the exact cumulative series.
    series_matches=1
    index_file="$(mktemp "${TMPDIR:-/tmp}/seoul-verify-index.XXXXXX")"
    delta_file="$(mktemp "${TMPDIR:-/tmp}/seoul-verify-delta.XXXXXX")"
    rm -f "$index_file"
    patches=("$PATCHES_DIR"/*.patch)
    if [ ! -f "${patches[0]:-}" ]; then
      series_matches=0
    elif ! git -C "$CHROMIUM_SRC" diff --binary --ignore-submodules=all HEAD >"$delta_file"; then
      series_matches=0
    elif ! GIT_INDEX_FILE="$index_file" git -C "$CHROMIUM_SRC" read-tree HEAD; then
      series_matches=0
    elif ! GIT_INDEX_FILE="$index_file" git -C "$CHROMIUM_SRC" apply --cached "$delta_file"; then
      series_matches=0
    else
      for ((i=${#patches[@]}-1; i>=0; i--)); do
        if ! GIT_INDEX_FILE="$index_file" git -C "$CHROMIUM_SRC" \
            apply --cached --reverse "${patches[$i]}" 2>/dev/null; then
          series_matches=0
          break
        fi
      done
      if [ "$series_matches" = 1 ] &&
          ! GIT_INDEX_FILE="$index_file" git -C "$CHROMIUM_SRC" \
              diff --cached --quiet HEAD; then
        series_matches=0
      fi
    fi
    rm -f "$index_file" "$delta_file"
    if [ "$series_matches" = 1 ]; then
      pass "src tracked edits are exactly the applied Seoul patch series (temporary-index reverse proof clean)"
    else
      bad "src has edits to tracked, non-submodule files beyond the Seoul patch series:"
      git -C "$CHROMIUM_SRC" status --porcelain --ignore-submodules=all --untracked-files=no | sed 's/^/        /'
    fi
  else
    pass "src has no tracked user edits (gclient submodule gitlinks and untracked dependency dirs are expected and ignored)"
  fi

  # Expected upstream feature paths present (extended, not reimplemented, by Seoul).
  [ -d "$CHROMIUM_SRC/chrome/browser/ui/views/tabs/vertical" ] \
    && pass "upstream vertical-tabs path present" || bad "missing upstream vertical-tabs path"
  [ -f "$CHROMIUM_SRC/chrome/browser/ui/views/frame/multi_contents_view.h" ] \
    && pass "upstream split-view (multi_contents_view) path present" || bad "missing upstream split-view path"
fi

# --- depot_tools revision ---
if [ -d "$DEPOT_TOOLS_DIR/.git" ]; then
  dh="$(git_head_sha "$DEPOT_TOOLS_DIR")"
  if [ -n "$LOCK_DEPOT" ] && [ "$dh" = "$LOCK_DEPOT" ]; then pass "depot_tools HEAD equals locked revision ($dh)"
  else bad "depot_tools HEAD ($dh) does not equal locked revision (${LOCK_DEPOT:-?})"; fi
else
  bad "no depot_tools checkout at $DEPOT_TOOLS_DIR"
fi

# --- .gclient structure ---
GC="$SEOUL_CHROMIUM_ROOT_ABS/.gclient"
if [ ! -f "$GC" ]; then bad ".gclient missing at $GC"
elif grep -q '"name": *"src"' "$GC" && grep -Eq 'chromium/src(\.git)?' "$GC"; then
  pass ".gclient has the expected src solution"
else
  bad ".gclient does not have the expected src solution"
fi

# --- gclient validate (read-only consistency; safe, no network) ---
if [ -x "$DEPOT_TOOLS_DIR/gclient" ] && [ -d "$CHROMIUM_SRC/.git" ]; then
  use_depot_tools
  if ( cd "$CHROMIUM_SRC" && gclient validate >/dev/null 2>&1 ); then
    pass "gclient validate succeeded (dependency/config consistency)"
  else
    bad "gclient validate failed"
  fi
else
  note "gclient validate skipped (depot_tools or checkout absent)"
fi

stage "runtime/build evidence"
note "NOT verified by this command: GN generation, compilation, launch, smoke,"
note "vertical-tabs runtime, split-view runtime, packaging, signing, notarization."
note "HEAD equality plus gclient validate indicate a consistent checkout, not a"
note "proven buildable or runnable browser."

stage "result"
if [ "$fail" -ne 0 ]; then die "checkout verification FAILED"; fi
log "checkout verification PASSED (read-only; runtime/build evidence still unavailable)"
