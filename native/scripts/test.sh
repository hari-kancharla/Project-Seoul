#!/usr/bin/env bash
# Build and run Project Seoul's native test suites from the pinned Chromium
# checkout.
#
#   test.sh unit      build and run every Seoul unit-test binary (default)
#   test.sh browser   build browser_tests and run only Seoul browser cases
#   test.sh all       run both suites
#
# Browser tests use Chromium's isolated test profiles. This script never
# selects or launches an installed Google Chrome application.
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

MODE="${1:-unit}"
case "$MODE" in
  unit | browser | all) ;;
  *) die "usage: test.sh [unit|browser|all]" ;;
esac

[ -d "$CHROMIUM_SRC/.git" ] ||
  die "no Chromium checkout at $CHROMIUM_SRC (run fetch.sh + sync.sh first)"
[ -f "$OUT_DIR/args.gn" ] ||
  die "build dir not generated; run gen.sh first"
"$SEOUL_SCRIPT_DIR/materialize.sh" verify ||
  die "Seoul overlay is stale or absent; run materialize.sh apply"
stage "build-host gate"
"$SEOUL_SCRIPT_DIR/build-host-check.sh" ||
  die "build-host gate failed; refusing to build native tests"
use_depot_tools

JOBS="$(resolve_jobs)"

run_unit_tests() {
  local binaries=(
    seoul_adblock_core_unittests
    seoul_adblock_engine_unittests
    seoul_adblock_interceptor_unittests
    seoul_command_core_unittests
    seoul_development_keychain_policy_unittests
    seoul_connectors_unittests
    seoul_context_unittests
    seoul_data_unittests
    seoul_intelligence_unittests
    seoul_library_unittests
    seoul_lifecycle_core_unittests
    seoul_lifecycle_state_unittests
    seoul_organization_unittests
    seoul_policy_unittests
    seoul_preview_unittests
    seoul_product_unittests
    seoul_keychain_credential_store_unittests
    seoul_projection_core_unittests
    seoul_runtime_unittests
    seoul_saui_unittests
    seoul_scenes_unittests
    seoul_semantic_unittests
    seoul_shell_core_unittests
    seoul_shell_view_unittests
    seoul_site_layers_unittests
    seoul_tasks_unittests
    seoul_themes_unittests
    seoul_tools_unittests
    seoul_voice_unittests
    seoul_workflows_unittests
  )
  local targets=(
    seoul/browser/adblock:seoul_adblock_core_unittests
    seoul/browser/adblock:seoul_adblock_engine_unittests
    seoul/browser/adblock:seoul_adblock_interceptor_unittests
    seoul/browser/commands:seoul_command_core_unittests
    chrome/app:seoul_development_keychain_policy_unittests
    seoul/browser/connectors:seoul_connectors_unittests
    seoul/browser/context:seoul_context_unittests
    seoul/browser/data:seoul_data_unittests
    seoul/browser/intelligence:seoul_intelligence_unittests
    seoul/browser/library:seoul_library_unittests
    seoul/browser/lifecycle:seoul_lifecycle_core_unittests
    seoul/browser/lifecycle:seoul_lifecycle_state_unittests
    seoul/browser/organization:seoul_organization_unittests
    seoul/browser/policy:seoul_policy_unittests
    seoul/browser/preview:seoul_preview_unittests
    seoul/browser/product:seoul_product_unittests
    seoul/browser/product/browser:seoul_keychain_credential_store_unittests
    seoul/browser/projection:seoul_projection_core_unittests
    seoul/browser/runtime:seoul_runtime_unittests
    seoul/browser/saui:seoul_saui_unittests
    seoul/browser/scenes:seoul_scenes_unittests
    seoul/browser/semantic:seoul_semantic_unittests
    seoul/browser/shell:seoul_shell_core_unittests
    seoul/browser/shell:seoul_shell_view_unittests
    seoul/browser/site_layers:seoul_site_layers_unittests
    seoul/browser/tasks:seoul_tasks_unittests
    seoul/browser/themes:seoul_themes_unittests
    seoul/browser/tools:seoul_tools_unittests
    seoul/browser/voice:seoul_voice_unittests
    seoul/browser/workflows:seoul_workflows_unittests
  )
  [ "${#binaries[@]}" -eq "${#targets[@]}" ] ||
    die "internal test runner mismatch: binary and target counts differ"

  stage "build all Seoul unit tests (-j $JOBS)"
  (cd "$CHROMIUM_SRC" && autoninja -C "out/$OUT_DIR_NAME" -j "$JOBS" \
    "${targets[@]}")

  stage "run all Seoul unit tests"
  local binary
  for binary in "${binaries[@]}"; do
    [ -x "$OUT_DIR/$binary" ] ||
      die "unit-test binary missing after build: $OUT_DIR/$binary"
    log "run $binary"
    "$OUT_DIR/$binary" --single-process-tests --gtest_brief=1
  done
  log "OK: all ${#binaries[@]} Seoul unit-test binaries passed"
}

run_browser_tests() {
  # Every gtest suite Seoul owns inside seoul_browser_tests. The binary also
  # links upstream's //chrome/browser/ui/views/tabs/vertical:browser_tests,
  # whose cases are Chromium's and are deliberately not run here - which is why
  # this is an allow-list rather than an unfiltered run.
  #
  # An allow-list rots silently: a new fixture simply never runs, and the suite
  # still says SUCCESS. check-browser-test-filter.mjs (npm run
  # check:browser-tests) reads the fixtures back out of the Seoul sources and
  # fails if any of them is missing from this list.
  local filter
  filter="SeoulRuntimeSessionRestoreBrowserTest.*"
  filter="${filter}:SeoulRuntimeBrowserTest.*"
  filter="${filter}:SeoulBoostDarkBrowserTest.*"
  filter="${filter}:ChromiumMutationAdapterBrowserTest.*"
  filter="${filter}:VerticalPresentationBrowserTest.*"
  filter="${filter}:SeoulShellBrowserTest.*"
  filter="${filter}:SeoulOrganizationServiceBrowserTest.*"
  filter="${filter}:AdBlockBrowserTest.*"
  filter="${filter}:CosmeticFilterAgentTest.*"

  stage "build focused Seoul browser tests (-j $JOBS)"
  (cd "$CHROMIUM_SRC" &&
    autoninja -C "out/$OUT_DIR_NAME" -j "$JOBS" \
      seoul/browser/product/browser:seoul_browser_tests)

  [ -x "$OUT_DIR/seoul_browser_tests" ] ||
    die "browser-test binary missing after build: $OUT_DIR/seoul_browser_tests"
  stage "run Seoul browser tests"
  # Chromium's unrelated experimental InitialWebUI toolbar waits for a
  # compositor paint callback that is not reliable with the explicit headless
  # backend. Seoul's native vertical Shell and chrome://seoul-canvas remain
  # enabled and are exercised below; disabling only InitialWebUI prevents the
  # upstream pre-test metrics wait from timing out before a Seoul test starts.
  "$OUT_DIR/seoul_browser_tests" \
    "--gtest_filter=$filter" \
    --test-launcher-bot-mode \
    --test-launcher-jobs=1 \
    --test-launcher-batch-limit=1 \
    --headless=new \
    --disable-gpu \
    --use-mock-keychain \
    --disable-features=InitialWebUI
  log "OK: Seoul browser-test filter passed"
}

if [ "$MODE" = "unit" ] || [ "$MODE" = "all" ]; then
  run_unit_tests
fi
if [ "$MODE" = "browser" ] || [ "$MODE" = "all" ]; then
  run_browser_tests
fi
