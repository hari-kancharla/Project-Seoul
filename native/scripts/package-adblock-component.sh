#!/usr/bin/env bash
# Packages and signs Seoul's ad-block filter component as a CRX3.
#
# The signing key is the real production identity and never lives in this
# repository. It is read from SEOUL_ADBLOCK_SIGNING_KEY (default
# ~/.seoul/keys/adblock-component/seoul_adblock_component.pem) and is converted
# to the DER PKCS#8 form Chromium's packager expects in a private temporary
# file that is removed on exit.
#
# Signing is done by Chromium's own first-party packager (crx3_build_action) so
# the CRX3 container and its RSA-SHA256 proof are produced by the same code that
# the browser verifies against. Nothing here re-implements the format.
#
# usage: package-adblock-component.sh <version> [output-dir]
#   version     component version, e.g. 1.0.0
#   output-dir  defaults to $SEOUL_DIST_DIR or ~/.seoul/dist/adblock-component
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
  die "usage: package-adblock-component.sh <version> [output-dir]"
fi
if ! printf '%s' "$VERSION" | grep -Eq '^[0-9]+(\.[0-9]+){1,3}$'; then
  die "version must be a dotted numeric version, got: $VERSION"
fi

SIGNING_KEY="${SEOUL_ADBLOCK_SIGNING_KEY:-$HOME/.seoul/keys/adblock-component/seoul_adblock_component.pem}"
OUT_DIR="${2:-${SEOUL_DIST_DIR:-$HOME/.seoul/dist/adblock-component}}"
PACKAGER="${SEOUL_CRX_PACKAGER:-$CHROMIUM_SRC/out/SeoulBaseline/crx3_build_action}"
BASELINE="$SEOUL_REPO_ROOT/native/seoul/browser/adblock/filters/seoul-baseline.txt"

[ -f "$SIGNING_KEY" ] || die "signing key not found: $SIGNING_KEY (provision it outside git first)"
[ -x "$PACKAGER" ] || die "crx3_build_action not built: $PACKAGER"
[ -f "$BASELINE" ] || die "bundled baseline rules missing: $BASELINE"

# Refuse to run against a key that has been copied into a git worktree: a
# production signing key inside a repository is one `git add -A` from published.
if git -C "$(dirname "$SIGNING_KEY")" rev-parse --show-toplevel >/dev/null 2>&1; then
  die "signing key lives inside a git repository; move it outside before signing"
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
umask 077

STAGE="$WORK/payload"
mkdir -p "$STAGE"

# The inner manifest repeats the version the component updater advertises;
# AdBlockFilterComponentInstallerPolicy::VerifyInstallation rejects the package
# when the two disagree, so a mismatched or truncated upload cannot activate.
cat > "$STAGE/manifest.json" <<JSON
{
  "format": "seoul-adblock-filter-set",
  "schema_version": 1,
  "version": "$VERSION",
  "name": "Seoul Ad Block Filter Lists"
}
JSON

cp "$BASELINE" "$STAGE/default.txt"
# Additional rules ship empty: optional lists are fetched per-profile at runtime
# rather than baked into the signed default component. The file must exist -
# VerifyInstallation requires both, and requires default.txt to be non-empty.
: > "$STAGE/additional.txt"

( cd "$STAGE" && zip -q -X -r "$WORK/payload.zip" . )

# Chromium's packager wants DER PKCS#8; keep the private form out of $OUT_DIR.
openssl pkcs8 -topk8 -nocrypt -inform PEM -outform DER \
  -in "$SIGNING_KEY" -out "$WORK/signing_key.der"

mkdir -p "$OUT_DIR"
CRX="$OUT_DIR/seoul-adblock-filters-$VERSION.crx"
"$PACKAGER" "$CRX" "$WORK/payload.zip" "$WORK/signing_key.der"

# Record the identity this artifact was signed under, so a release can be
# audited without access to the private key.
PUBDER="$WORK/pub.der"
openssl rsa -in "$SIGNING_KEY" -pubout -outform DER -out "$PUBDER" 2>/dev/null
KEYHASH="$(openssl dgst -sha256 -hex "$PUBDER" | awk '{print $NF}')"
CRXHASH="$(openssl dgst -sha256 -hex "$CRX" | awk '{print $NF}')"
CRXSIZE="$(wc -c < "$CRX" | tr -d ' ')"

cat > "$OUT_DIR/seoul-adblock-filters-$VERSION.metadata.json" <<JSON
{
  "component_name": "Seoul Ad Block Filter Lists",
  "version": "$VERSION",
  "crx_sha256": "$CRXHASH",
  "crx_size_bytes": $CRXSIZE,
  "public_key_sha256": "$KEYHASH"
}
JSON

log "signed component: $CRX"
log "  version           $VERSION"
log "  crx sha256        $CRXHASH"
log "  size              $CRXSIZE bytes"
log "  public key sha256 $KEYHASH"
log "build Seoul with: gn args --args='seoul_adblock_component_public_key_hash=\"$KEYHASH\"'"
