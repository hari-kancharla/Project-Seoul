#!/usr/bin/env bash
# Host-side C++ syntax gate for Seoul-owned source. Parses every given source
# (or every tracked and untracked Seoul .cc by default) with Apple clang against the REAL
# pinned checkout headers (-fsyntax-only; no build, no gn). Generated buildflag
# headers do not exist without `gn gen`, so they are stubbed on the fly and
# every BUILDFLAG_INTERNAL_* the parse encounters is defined to a conservative
# no-op value, except target facts that must match the compiler itself (for
# example, whether pointers are 64-bit). This is NOT compilation and proves no
# codegen or linking; it catches include, name, type, and template errors in
# Seoul code a plain read cannot, on hosts the build gate refuses.
#
# usage: syntax-check.sh [file.cc ...]
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

need_cmd clang++
if [ ! -d "$CHROMIUM_SRC/base" ]; then
  # CI hosts without the external checkout cannot parse against real headers;
  # skipping is stated loudly, never silent (mirrors check-header-includes).
  log "SKIPPED: no Chromium checkout at $CHROMIUM_SRC; the parse gate runs only where the pinned checkout exists"
  exit 0
fi

STUB_DIR="$(mktemp -d "${TMPDIR:-/tmp}/seoul-syntax-stubs.XXXXXX")"
trap 'rm -rf "$STUB_DIR"' EXIT
FLAGS_FILE="$STUB_DIR/flags.rsp"
mkdir -p "$STUB_DIR"
# Buildflag discovery is deterministic per invocation. Never inherit flags
# learned by an older checkout or a host with a different architecture.
: > "$FLAGS_FILE"

# Seoul headers must resolve to THIS repository (the object under test), not
# to the materialized copy in the checkout.
SHIM_DIR="$STUB_DIR/shim"
mkdir -p "$SHIM_DIR"
ln -sfn "$SEOUL_SRC_DIR" "$SHIM_DIR/seoul"

if [ "$#" -gt 0 ]; then
  FILES=("$@")
else
  FILES=()
  while IFS= read -r f; do FILES+=("$f"); done < <(
    cd "$SEOUL_REPO_ROOT" && \
      git ls-files --cached --others --exclude-standard 'native/seoul/**/*.cc'
  )
fi

MAX_ITERATIONS=400
run_clang() {
  # shellcheck disable=SC2046
  # Real checkout headers take precedence; the stub dir only fills paths that
  # exist solely as gn-generated files.
  clang++ -std=c++23 -fsyntax-only -w \
    -I "$SHIM_DIR" -I "$CHROMIUM_SRC" \
    -I "$CHROMIUM_SRC/base/allocator/partition_allocator/src" \
    -I "$CHROMIUM_SRC/third_party/skia" \
    -I "$STUB_DIR" \
    -I "$CHROMIUM_SRC/third_party/abseil-cpp" \
    -I "$CHROMIUM_SRC/third_party/boringssl/src/include" \
    -I "$CHROMIUM_SRC/third_party/googletest/src/googletest/include" \
    -I "$CHROMIUM_SRC/third_party/googletest/src/googlemock/include" \
    @"$FLAGS_FILE" \
    "$1" 2>&1
}

check_file() {
  local file="$1"
  local iteration=0
  while :; do
    iteration=$((iteration + 1))
    if [ "$iteration" -gt "$MAX_ITERATIONS" ]; then
      die "stub iteration limit reached for $file"
    fi
    local out
    if out="$(run_clang "$file")"; then
      log "OK   $file"
      return 0
    fi
    # 1. Missing header. Decide from evidence, not path lists:
    #    - buildflag-shaped names get the all-zero stub (gn writes them);
    #    - names absent from the whole checkout are gn-generated code
    #      (mojom/grit/protozero/resources) -> the file is classified a
    #      codegen skip;
    #    - a name that DOES exist in the checkout means this gate's include
    #      paths are wrong -> hard fail so the gate itself gets fixed.
    local missing
    missing="$(printf '%s\n' "$out" | sed -n "s/.*fatal error: '\([^']*\)' file not found.*/\1/p" | head -1)"
    if [ -n "$missing" ]; then
      case "$missing" in
        *buildflags*.h | *buildflag*.h)
          mkdir -p "$STUB_DIR/$(dirname "$missing")"
          printf '#ifndef SEOUL_STUB_%s\n#define SEOUL_STUB_%s\n#include "build/buildflag.h"\n#endif\n' \
            "$(printf '%s' "$missing" | tr -c 'A-Za-z0-9' '_')" \
            "$(printf '%s' "$missing" | tr -c 'A-Za-z0-9' '_')" > "$STUB_DIR/$missing"
          continue
          ;;
      esac
      if [ -f "$CHROMIUM_SRC/$missing" ] || \
         [ -f "$CHROMIUM_SRC/base/allocator/partition_allocator/src/$missing" ] || \
         [ -f "$CHROMIUM_SRC/third_party/skia/$missing" ]; then
        printf 'include path gap: %s exists in the checkout but was not found\n' "$missing"
        printf '%s\n' "$out" | head -12
        return 1
      fi
      SKIP_REASON="$missing"
      return 2
    fi
    # 2. Undefined buildflag macro -> define it as a conservative value. Most
    #    optional features are off. Architecture facts must agree with clang's
    #    target or Chromium's headers intentionally reject the configuration.
    local flag
    flag="$(printf '%s\n' "$out" | sed -n "s/.*function-like macro '\([A-Za-z0-9_]*\)' is not defined.*/\1/p" | head -1)"
    if [ -n "$flag" ]; then
      if grep -q -- "-D${flag}()=0" "$FLAGS_FILE"; then
        printf '%s\n' "$out" | head -30
        die "flag ${flag} already stubbed; real error in $file"
      fi
      flag_value=0
      if [ "$flag" = "PA_BUILDFLAG_INTERNAL_HAS_64_BIT_POINTERS" ] &&
         [ "$(getconf LONG_BIT)" = "64" ]; then
        flag_value=1
      fi
      printf -- '-D%s()=%s\n' "$flag" "$flag_value" >> "$FLAGS_FILE"
      continue
    fi
    # 3. A perfetto dependency means the include graph reached gn-generated
    #    tracing protos (base::test::TaskEnvironment and the sequence manager
    #    do); that parses only on the build host. Stated skip, not a pass.
    if printf '%s\n' "$out" | grep -q "perfetto"; then
      SKIP_REASON="gn-generated perfetto tracing protos"
      return 2
    fi
    # 4. Anything else is a real diagnostic in Seoul code (or an environment
    #    limit worth seeing). Print it and fail.
    printf '%s\n' "$out" | head -40
    return 1
  done
}

# Codegen skips are decided by evidence during the parse (a header that exists
# nowhere in the checkout, or generated perfetto protos), never by a path
# list. SKIP_REASON carries what was actually missing.
SKIP_REASON=""

fail=0
checked=0
skipped=0
for file in "${FILES[@]}"; do
  abs="$SEOUL_REPO_ROOT/$file"
  checked=$((checked + 1))
  rc=0
  SKIP_REASON=""
  check_file "$abs" || rc=$?
  case "$rc" in
    0) : ;;
    2)
      log "SKIP $file (needs ${SKIP_REASON:-gn-generated headers}; parses only on the build host)"
      checked=$((checked - 1))
      skipped=$((skipped + 1))
      ;;
    *) fail=1 ;;
  esac
done
if [ "$fail" -ne 0 ]; then
  die "syntax check FAILED (see diagnostics above)"
fi
log "syntax check passed: $checked file(s) parsed clean, $skipped skipped for codegen (parse-only; not compilation)"
