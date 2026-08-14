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
    rsync -a --omit-dir-times --delete --exclude='.DS_Store' --exclude='/protocol' "$SEOUL_SRC_DIR"/ "$SEOUL_OVERLAY_DEST"/
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
