#!/usr/bin/env bash
# Assert the pinned tree-sitter CLI (T60 deliverable; grammar lane T17 input).
#
#   bash scripts/check-tree-sitter-cli.sh            # verify (download if needed)
#   bash scripts/check-tree-sitter-cli.sh --offline  # fail instead of downloading
#
# It verifies the TWO pinned artifacts (npm launcher package + linux-x64 release
# binary), extracts the binary, and asserts `tree-sitter --version` equals the
# pinned string. The cache lives in target/ts-cli/ (gitignored).
#
# Run it INSIDE the pinned M0 image to make the assertion use the same
# glibc/toolchain as the build:
#   docker run --rm -v "$PWD":/vedit-build -w /vedit-build \
#     vedit-qt6-build:local bash scripts/check-tree-sitter-cli.sh
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# shellcheck source=versions-grammar.env
set -a
. "$ROOT/scripts/versions-grammar.env"
set +a

CACHE="$ROOT/target/ts-cli"
mkdir -p "$CACHE"
OFFLINE=0
[ "${1:-}" = "--offline" ] && OFFLINE=1

NPM_TGZ="$CACHE/tree-sitter-cli-$TREE_SITTER_CLI_VERSION.tgz"
BIN_ZIP="$CACHE/tree-sitter-cli-linux-x64-$TREE_SITTER_CLI_VERSION.zip"

fetch_verified() {
  local url="$1" dest="$2" sha="$3"
  if [ -f "$dest" ] && printf '%s  %s\n' "$sha" "$dest" | sha256sum -c - >/dev/null 2>&1; then
    return 0
  fi
  if [ "$OFFLINE" = "1" ]; then
    echo "FATAL: $dest is missing or does not match the pin, and --offline was given" >&2
    exit 1
  fi
  curl -fsSL -o "$dest" "$url"
  printf '%s  %s\n' "$sha" "$dest" | sha256sum -c -
}

echo "== tree-sitter CLI pin =="
echo "version: $TREE_SITTER_CLI_VERSION"
fetch_verified "$TREE_SITTER_CLI_NPM_URL" "$NPM_TGZ" "$TREE_SITTER_CLI_NPM_SHA256"
fetch_verified "$TREE_SITTER_CLI_BIN_URL" "$BIN_ZIP" "$TREE_SITTER_CLI_BIN_SHA256"

actual_size=$(stat -c %s "$BIN_ZIP")
[ "$actual_size" = "$TREE_SITTER_CLI_BIN_SIZE" ] \
  || { echo "FATAL: binary archive size $actual_size != pinned $TREE_SITTER_CLI_BIN_SIZE" >&2; exit 1; }

rm -rf "$CACHE/extracted"
mkdir -p "$CACHE/extracted"
python3 - "$BIN_ZIP" "$CACHE/extracted" <<'PY'
import sys
import zipfile
with zipfile.ZipFile(sys.argv[1]) as archive:
    archive.extractall(sys.argv[2])
PY
CLI="$CACHE/extracted/tree-sitter"
[ -f "$CLI" ] || { echo "FATAL: $CLI is missing after extraction" >&2; exit 1; }
# Python's zipfile does not restore the executable bit; the archive is verified
# by sha256 before this point, so marking the binary executable is safe.
chmod +x "$CLI"

actual_version=$("$CLI" --version 2>&1 | head -1)
echo "actual:  $actual_version"
[ "$actual_version" = "$TREE_SITTER_CLI_VERSION_OUTPUT" ] \
  || { echo "FATAL: tree-sitter --version is '$actual_version', pinned '$TREE_SITTER_CLI_VERSION_OUTPUT'" >&2; exit 1; }

echo "PASS: tree-sitter CLI $TREE_SITTER_CLI_VERSION (npm package + linux-x64 binary sha256 verified)"
