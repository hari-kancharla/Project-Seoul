#!/usr/bin/env bash
# Materialize repository-owned Seoul source and its isolated blocker Rust closure
# into the external Chromium checkout. Deterministic and reversible. Only ever
# writes under the two dedicated Seoul-owned destination directories.
#
#   materialize.sh apply     mirror native/seoul/ -> src/seoul/, protocol/ ->
#                            src/seoul/protocol/, and the pinned blocker Rust
#                            closure -> src/third_party/rust/seoul_adblock/ (default)
#   materialize.sh verify    read-only: report whether all mirrors match
#   materialize.sh reverse   remove both dedicated materialized directories
#
# protocol/ (schemas + shared conformance fixtures) is mirrored INTO the
# overlay so native conformance tests read the identical corpus the
# TypeScript tests read; the main mirror excludes /protocol so the two rsyncs
# never fight over it.
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

CMD="${1:-apply}"
need_cmd rsync

# Mirror by CONTENT, and stamp what changes with the time it changed.
#
# `rsync -a` preserves the source's modification times, which quietly breaks
# incremental builds: an edit made before the last compile arrives in the
# checkout wearing its original timestamp, the build system sees an object file
# that is newer, decides the source is unchanged, and links a stale object
# against new sources. That failure is silent and it produces a binary that does
# not match the tree it claims to be built from.
#
# So: decide what to copy from the file's contents (--checksum), never from its
# timestamp, and touch whatever was actually copied so it is unambiguously newer
# than anything built before. Files that did not change are not touched, so this
# does not force needless rebuilds.
#
# `verify` correspondingly compares contents and ignores times, because after a
# touch the mirror's times legitimately differ from the repository's while its
# contents are identical. Dropping -t (-rlpgoD is -a without -t) is not enough
# on its own: it stops rsync SETTING the time, but rsync still ITEMIZES the
# difference as `.f..T....` - a leading `.` meaning "no transfer needed, only a
# timestamp". Reporting those as staleness made verify fail on exactly the files
# apply had just written correctly, and that blocked the test runner outright.
# So the itemized output is filtered to real content differences.
SEOUL_MIRROR_OPTS=(-a --checksum --omit-dir-times --delete --exclude='.DS_Store')
SEOUL_VERIFY_OPTS=(-rlpgoD --checksum --omit-dir-times --delete --dry-run
                   --itemize-changes --exclude='.DS_Store')

# Keep only itemized lines that mean the mirror's CONTENTS are wrong: a transfer
# (> or <), a creation (c), or a deletion (*deleting). Anything starting with
# `.` is rsync noting an attribute it would touch on a file whose contents
# already match - which, after apply's stamping, is the normal state.
real_changes() {
  grep -E '^(>|<|c|\*)' || true
}

# rsync's itemized output names every file it wrote, relative to the
# destination; touch exactly those.
mirror_and_stamp() {
  local source="$1" dest="$2"
  shift 2
  local changed
  changed="$(rsync "${SEOUL_MIRROR_OPTS[@]}" "$@" --itemize-changes \
    "$source" "$dest")"
  local line path stamped=0
  while IFS= read -r line; do
    # Itemized lines are "<flags> <path>"; only real transfers start with > or c.
    case "$line" in
      \>*|c*) path="${line#* }" ;;
      *) continue ;;
    esac
    case "$path" in */) continue ;; esac
    if [ -f "$dest$path" ]; then
      touch "$dest$path"
      stamped=$((stamped + 1))
    fi
  done <<< "$changed"
  log "mirrored $dest ($stamped file(s) changed and stamped)"
}

[ -d "$SEOUL_SRC_DIR" ] || die "Seoul source dir not found: $SEOUL_SRC_DIR"
[ -d "$SEOUL_PROTOCOL_DIR" ] || die "Seoul protocol dir not found: $SEOUL_PROTOCOL_DIR"
[ -d "$SEOUL_ADBLOCK_RUST_DIR" ] ||
  die "Seoul adblock Rust source dir not found: $SEOUL_ADBLOCK_RUST_DIR"
is_git_checkout "$CHROMIUM_SRC" || die "no Chromium checkout at $CHROMIUM_SRC (run fetch.sh + sync.sh first)"

# Safety: both destinations must be the exact dedicated Seoul directories inside
# the checkout, never a broader upstream Chromium path.
case "$SEOUL_OVERLAY_DEST" in
  "$CHROMIUM_SRC"/seoul) : ;;
  *) die "overlay destination is not the Seoul overlay dir: $SEOUL_OVERLAY_DEST" ;;
esac
case "$SEOUL_ADBLOCK_RUST_DEST" in
  "$CHROMIUM_SRC"/third_party/rust/seoul_adblock) : ;;
  *) die "adblock Rust destination is not the dedicated Seoul dir: $SEOUL_ADBLOCK_RUST_DEST" ;;
esac

case "$CMD" in
  apply)
    stage "materialize native/seoul/ -> $SEOUL_OVERLAY_DEST"
    mkdir -p "$SEOUL_OVERLAY_DEST"
    mirror_and_stamp "$SEOUL_SRC_DIR"/ "$SEOUL_OVERLAY_DEST"/ --exclude='/protocol'
    stage "materialize protocol/ -> $SEOUL_OVERLAY_DEST/protocol"
    rsync -a --omit-dir-times --delete --exclude='.DS_Store' "$SEOUL_PROTOCOL_DIR"/ "$SEOUL_OVERLAY_DEST"/protocol/
    stage "materialize pinned blocker Rust closure -> $SEOUL_ADBLOCK_RUST_DEST"
    mkdir -p "$SEOUL_ADBLOCK_RUST_DEST"
    rsync -a --omit-dir-times --delete --exclude='.DS_Store' "$SEOUL_ADBLOCK_RUST_DIR"/ "$SEOUL_ADBLOCK_RUST_DEST"/
    log "OK: Seoul source, canonical protocol, and blocker Rust closure materialized"
    ;;
  verify)
    stage "verify overlay matches native/seoul/ (read-only)"
    if [ ! -d "$SEOUL_OVERLAY_DEST" ]; then
      log "overlay not present at $SEOUL_OVERLAY_DEST (run: materialize.sh apply)"
      exit 1
    fi
    diff_out="$(rsync -a --omit-dir-times --delete --dry-run --itemize-changes --exclude='.DS_Store' --exclude='/protocol' "$SEOUL_SRC_DIR"/ "$SEOUL_OVERLAY_DEST"/)"
    if [ -n "$diff_out" ]; then
      warn "overlay differs from native/seoul/:"
      printf '%s\n' "$diff_out"
      exit 1
    fi
    proto_diff="$(rsync -a --omit-dir-times --delete --dry-run --itemize-changes --exclude='.DS_Store' "$SEOUL_PROTOCOL_DIR"/ "$SEOUL_OVERLAY_DEST"/protocol/)"
    if [ -n "$proto_diff" ]; then
      warn "overlay protocol/ differs from repository protocol/:"
      printf '%s\n' "$proto_diff"
      exit 1
    fi
    if [ ! -d "$SEOUL_ADBLOCK_RUST_DEST" ]; then
      log "blocker Rust closure not present at $SEOUL_ADBLOCK_RUST_DEST (run: materialize.sh apply)"
      exit 1
    fi
    rust_diff="$(rsync -a --omit-dir-times --delete --dry-run --itemize-changes --exclude='.DS_Store' "$SEOUL_ADBLOCK_RUST_DIR"/ "$SEOUL_ADBLOCK_RUST_DEST"/)"
    if [ -n "$rust_diff" ]; then
      warn "materialized blocker Rust closure differs from repository source:"
      printf '%s\n' "$rust_diff"
      exit 1
    fi
    log "OK: all Seoul-owned source mirrors match"
    ;;
  reverse)
    stage "remove materialized overlay $SEOUL_OVERLAY_DEST"
    if [ -d "$SEOUL_OVERLAY_DEST" ]; then
      rm -rf "$SEOUL_OVERLAY_DEST"
      log "OK: overlay removed"
    else
      log "overlay not present; nothing to remove"
    fi
    stage "remove materialized blocker Rust closure $SEOUL_ADBLOCK_RUST_DEST"
    if [ -d "$SEOUL_ADBLOCK_RUST_DEST" ]; then
      rm -rf "$SEOUL_ADBLOCK_RUST_DEST"
      log "OK: blocker Rust closure removed"
    else
      log "blocker Rust closure not present; nothing to remove"
    fi
    ;;
  *)
    die "usage: materialize.sh [apply|verify|reverse]"
    ;;
esac
