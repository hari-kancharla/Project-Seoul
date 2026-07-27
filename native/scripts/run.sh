#!/usr/bin/env bash
# Launch the locally built Seoul browser with a DISPOSABLE temporary profile.
# Any extra arguments are passed through to the browser.
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

[ -x "$CHROMIUM_BINARY" ] || die "built Seoul browser not found at $CHROMIUM_BINARY (run build.sh first)"

PROFILE="$(mktemp -d "${TMPDIR:-/tmp}/seoul-baseline-profile.XXXXXX")"
cleanup() { [ -n "${PROFILE:-}" ] && rm -rf "$PROFILE"; }
trap cleanup EXIT

# Local development-only convenience flags. These are NOT production defaults:
#   --use-mock-keychain                 avoid macOS keychain prompts in a throwaway run
#   --disable-features=...              keep Chromium experimental WebUI toolbar /
#                                       omnibox AI chrome from fighting Seoul's rail
#   --no-first-run / --no-default-browser-check keep every disposable-profile
#        run focused on Seoul instead of Chromium onboarding.
DEV_FLAGS=(
  --use-mock-keychain
  --disable-features=DialMediaRouteProvider,InitialWebUI,AiModeOmniboxEntryPoint
  --no-first-run
  --no-default-browser-check
)

# Chromium shows a startup infobar when official Google API keys are absent.
# Local Seoul runs are not Google Chrome builds; suppress that banner noise.
export GOOGLE_API_KEY="${GOOGLE_API_KEY:-seoul-local-dev}"
export GOOGLE_DEFAULT_CLIENT_ID="${GOOGLE_DEFAULT_CLIENT_ID:-seoul-local-dev}"
export GOOGLE_DEFAULT_CLIENT_SECRET="${GOOGLE_DEFAULT_CLIENT_SECRET:-seoul-local-dev}"

stage "launch Seoul"
log "binary:  $CHROMIUM_BINARY"
log "profile: $PROFILE (removed on exit)"
log "dev flags (local only): ${DEV_FLAGS[*]}"
"$CHROMIUM_BINARY" --user-data-dir="$PROFILE" "${DEV_FLAGS[@]}" "$@"
