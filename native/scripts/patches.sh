#!/usr/bin/env bash
# Apply / verify / reverse the ordered Chromium integration patch series described
# by native/patches/manifest.json. Apply/reverse operate on every patch in the
# manifest in order, and verify runs a cumulative apply-then-reverse round trip.
#
#   patches.sh list      print the ordered series
#   patches.sh verify    validate the manifest; for each patch, git apply --check
#   patches.sh apply     git apply each patch in ascending order
#   patches.sh reverse   git apply -R each patch in descending order
#
# Run materialize.sh before apply when the series depends on Seoul-owned source.
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

CMD="${1:-verify}"
need_cmd python3

# Emit "order<TAB>relpath" lines, ascending by order.
manifest_entries() {
  python3 - "$PATCH_MANIFEST" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
for p in sorted(m.get("patches", []), key=lambda e: e.get("order", 0)):
    print(f"{p.get('order')}\t{p.get('file')}")
PY
}

case "$CMD" in
  list)
    stage "patch series (native/patches/manifest.json)"
    entries="$(manifest_entries)"
    if [ -z "$entries" ]; then log "series is empty (0 patches)"; else printf '%s\n' "$entries"; fi
    ;;

  verify)
    stage "validate patch manifest"
    node "$SEOUL_SCRIPT_DIR/check-patch-manifest.mjs"
    entries="$(manifest_entries)"
    if [ -z "$entries" ]; then
      log "no patches to apply-check (empty series)"
      exit 0
    fi
    is_git_checkout "$CHROMIUM_SRC" || die "no checkout at $CHROMIUM_SRC; cannot apply-check patches"
    if src_has_user_edits; then die "checkout has edits to tracked files; refusing to verify patches"; fi
    # Cumulative round trip: apply the whole series in ascending order, then
    # reverse it in descending order. This verifies an ordered series where a
    # later patch may depend on an earlier one (a per-patch --check against the
    # clean tree cannot), and it leaves the checkout byte-identical. On any
    # failure, reverse whatever was applied so the tree is restored.
    applied=()
    restore_applied() {
      local i
      for ((i=${#applied[@]}-1; i>=0; i--)); do
        git -C "$CHROMIUM_SRC" apply -R "$PATCHES_DIR/${applied[$i]}" 2>/dev/null || true
      done
    }
    trap restore_applied ERR
    stage "apply-check series (ascending, cumulative)"
    while IFS=$'\t' read -r order file; do
      [ -n "$file" ] || continue
      log "git apply ($order) $file"
      git -C "$CHROMIUM_SRC" apply "$PATCHES_DIR/$file"
      applied+=("$file")
    done <<< "$entries"
    stage "reverse-check series (descending)"
    rev="$(printf '%s\n' "$entries" | sort -rn)"
    while IFS=$'\t' read -r order file; do
      [ -n "$file" ] || continue
      log "git apply -R ($order) $file"
      git -C "$CHROMIUM_SRC" apply -R "$PATCHES_DIR/$file"
    done <<< "$rev"
    trap - ERR
    if src_has_user_edits; then die "checkout not clean after verify round trip"; fi
    log "OK: series applies and reverses cleanly; checkout restored"
    ;;

  apply)
    is_git_checkout "$CHROMIUM_SRC" || die "no checkout at $CHROMIUM_SRC (run fetch.sh + sync.sh first)"
    if src_has_user_edits; then die "checkout has edits to tracked files; refusing to apply patches"; fi
    node "$SEOUL_SCRIPT_DIR/check-patch-manifest.mjs"
    entries="$(manifest_entries)"
    if [ -z "$entries" ]; then log "empty series; nothing to apply"; exit 0; fi
    stage "apply patches (ascending order)"
    while IFS=$'\t' read -r order file; do
      [ -n "$file" ] || continue
      log "git apply ($order) $file"
      git -C "$CHROMIUM_SRC" apply "$PATCHES_DIR/$file"
    done <<< "$entries"
    log "OK: patch series applied"
    ;;

  reverse)
    is_git_checkout "$CHROMIUM_SRC" || die "no checkout at $CHROMIUM_SRC"
    entries="$(manifest_entries | sort -rn)"
    if [ -z "$entries" ]; then log "empty series; nothing to reverse"; exit 0; fi
    stage "reverse patches (descending order)"
    while IFS=$'\t' read -r order file; do
      [ -n "$file" ] || continue
      log "git apply -R ($order) $file"
      git -C "$CHROMIUM_SRC" apply -R "$PATCHES_DIR/$file"
    done <<< "$entries"
    log "OK: patch series reversed"
    ;;

  *)
    die "usage: patches.sh [list|verify|apply|reverse]"
    ;;
esac
