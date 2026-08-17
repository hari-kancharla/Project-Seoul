#!/usr/bin/env bash
# Installs the Chrome native-messaging host manifest for SeoulHost.
#
# Two things here are easy to get wrong and both fail silently:
#
#   1. "path" must be an ABSOLUTE path to the binary. Chrome does not resolve
#      relative paths, and a wrong one shows up only as a host that never
#      starts.
#   2. "allowed_origins" must carry the extension's REAL id. An unpacked
#      extension without a pinned "key" in its manifest gets an id derived from
#      its directory path, which changes if the folder ever moves — and then
#      every connectNative call is rejected. apps/browser-harness/manifest.json
#      pins "key", so the id below is stable forever.
#
# Usage: scripts/install-native-host.sh [--release]

set -euo pipefail

CONFIG="debug"
if [[ "${1:-}" == "--release" ]]; then CONFIG="release"; fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ext_dir="$repo_root/apps/browser-harness"
ext_manifest="$ext_dir/manifest.json"
host_name="com.seoul.host"
host_dir="$HOME/Library/Application Support/Google/Chrome/NativeMessagingHosts"
host_manifest="$host_dir/$host_name.json"

if [[ ! -f "$ext_manifest" ]]; then
  echo "error: $ext_manifest not found" >&2
  exit 1
fi

echo "==> building SeoulHost ($CONFIG)"
if [[ "$CONFIG" == "release" ]]; then
  ( cd "$repo_root" && swift build -c release --product SeoulHost >/dev/null )
  host_bin="$repo_root/.build/release/SeoulHost"
else
  ( cd "$repo_root" && swift build --product SeoulHost >/dev/null )
  host_bin="$repo_root/.build/debug/SeoulHost"
fi

# Resolve through SwiftPM's symlinked .build/<config> to a real absolute path.
host_bin="$(cd "$(dirname "$host_bin")" && pwd)/$(basename "$host_bin")"
if [[ ! -x "$host_bin" ]]; then
  echo "error: SeoulHost binary not found or not executable at $host_bin" >&2
  exit 1
fi

# THIRD silent failure, and the nastiest: macOS TCC.
#
# ~/Documents, ~/Desktop and ~/Downloads are privacy-protected. A native host
# living under one of them HANGS INSIDE dyld, before main() runs, unless the
# browser has been granted access to that folder. There is no error, no prompt
# and no log line. The process exists and sleeps forever with a single thread,
# the extension's port stays open, and every find request goes unanswered.
#
# Measured with the binary at ~/Documents/.../.build/debug/SeoulHost: `sample`
# on the stuck process showed it parked in dyld's open() on its own executable,
# having never executed one instruction of its own. The identical binary run
# from a shell starts instantly, which is what makes this so hard to place.
#
# So a binary in protected space is COPIED to Application Support, which is not
# TCC-gated, and the manifest points at the copy.
stage_dir="$HOME/Library/Application Support/Seoul/bin"
staged_from=""
case "$host_bin" in
  "$HOME/Documents"/*|"$HOME/Desktop"/*|"$HOME/Downloads"/*|"$HOME/Library/Mobile Documents"/*)
    mkdir -p "$stage_dir"
    cp -f "$host_bin" "$stage_dir/SeoulHost"
    chmod +x "$stage_dir/SeoulHost"
    staged_from="$host_bin"
    host_bin="$stage_dir/SeoulHost"
    ;;
esac

echo "==> bundling the content script"
( cd "$repo_root" && node apps/browser-harness/build.mjs >/dev/null )

# Chrome's extension id: SHA-256 of the DER public key, first 16 bytes, with
# every hex nibble remapped 0-9a-f -> a-p. This is exactly how Chrome derives
# it, so what this prints is what the browser will show.
key="$(node -e 'process.stdout.write(JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")).key || "")' "$ext_manifest")"
if [[ -z "$key" ]]; then
  echo "error: $ext_manifest has no \"key\" field, so the extension id is not stable." >&2
  echo "       Add one, or allowed_origins will break on every reload." >&2
  exit 1
fi

ext_id="$(printf '%s' "$key" \
  | openssl base64 -d -A \
  | openssl dgst -sha256 -binary \
  | xxd -p -c 32 \
  | head -c 32 \
  | tr '0-9a-f' 'a-p')"

if [[ ${#ext_id} -ne 32 ]]; then
  echo "error: derived an implausible extension id: '$ext_id'" >&2
  exit 1
fi

mkdir -p "$host_dir"
cat > "$host_manifest" <<JSON
{
  "name": "$host_name",
  "description": "Seoul native messaging relay",
  "path": "$host_bin",
  "type": "stdio",
  "allowed_origins": [
    "chrome-extension://$ext_id/"
  ]
}
JSON

echo
echo "installed:      $host_manifest"
echo "host binary:    $host_bin"
echo "extension id:   $ext_id"
echo "extension dir:  $ext_dir"
if [[ -n "$staged_from" ]]; then
  echo
  echo "STAGED, because the build sits in a TCC-protected folder:"
  echo "  built at:   $staged_from"
  echo "  copied to:  $host_bin"
  echo "  Left in place, Chrome's copy of the host would hang inside dyld with no"
  echo "  error of any kind. The trade: a rebuild does NOT take effect until you"
  echo "  re-run this script. Move the repo out of ~/Documents to avoid the copy."
fi
echo
echo "Next:"
echo "  1. chrome://extensions -> Developer mode -> Load unpacked -> $ext_dir"
echo "  2. confirm the id shown by Chrome is exactly: $ext_id"
echo "  3. run:  swift run SeoulApp"
echo "  4. open a page with a form and press cmd+shift+space"
echo
echo "Note: this installs for stock Google Chrome. Chromium, Brave and Edge read"
echo "the same file from their own Application Support directories."
echo
echo "Loading the extension must be done through the chrome://extensions UI above."
echo "Branded Google Chrome now refuses the --load-extension switch outright"
echo "(\"--load-extension is not allowed in Google Chrome, ignoring\"), so a"
echo "scripted run needs Chrome for Testing or Chromium instead."
