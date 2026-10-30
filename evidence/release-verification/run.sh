#!/usr/bin/env bash
# Host entry for the post-release regression.
#
#   bash evidence/release-verification/run.sh
#
# It downloads the assets of GitHub Release v0.1.0 into a scratch directory,
# verifies them against the published SHA256SUMS, and then runs each of the three
# products in a clean offline CentOS 7 container. Nothing from the repository's
# packaging/*/out is used: the mount is the download directory only.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EVID="$ROOT/evidence/release-verification"
DL="$ROOT/target/release-verify-download"
export DOCKER_CONFIG="$ROOT/target/docker-config"

mkdir -p "$EVID" "$ROOT/target/docker-config"
rm -rf "$DL"; mkdir -p "$DL"

{
  echo "# Downloading the public release assets (v0.1.0)"
  echo "\$ gh release download v0.1.0 -D $DL"
  gh release download v0.1.0 -D "$DL" 2>&1
  echo
  echo "\$ ls -l"
  ls -l "$DL"
  echo
  echo "\$ sha256sum -c SHA256SUMS"
  ( cd "$DL" && sha256sum -c SHA256SUMS ) 2>&1
  echo "EXIT=$?"
} > "$EVID/01-download.txt" 2>&1
cat "$EVID/01-download.txt"
grep -q "VEdit-0.1.0-x86_64.AppImage: OK" "$EVID/01-download.txt"

echo "== clean offline container (no gtk3, --network none, Xvfb) =="
docker run --rm --network none \
  -v "$DL:/art:ro" -v "$EVID:/evid" \
  vedit-tarball-cleanroom:local sh /evid/verify.sh 2>&1 | tee "$EVID/90-run.log"

echo "== results =="
cat "$EVID/99-results.txt"
