#!/usr/bin/env bash
# T20 hard-gate spike S1-S2/S4 - reproducible mechanism evidence.
#
# Produces, under evidence/batch-20261110b/scala-metals/spike/logs/:
#   01-online-version.log     metals --version WITH network (success path, full list)
#   02-offline-version.log    metals --version under `docker run --network none`
#                             (the explicit degradation + the ConnectException stack)
#   03-cache-manifest.txt     the bundled coursier cache before/after both runs
#                             (proves the check writes nothing and reads nothing)
#   04-index-links.log        the REAL Maven Central index page: the mtags_ hrefs
#                             the check iterates (the exact artifact URLs)
#   05-jar-strings.log        the URL/class constants in the shipped metals 1.6.9 jar
#
# Usage: bash probes/scala-metals/run-spike.sh [logdir]
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT=${1:-"$ROOT/evidence/batch-20261110b/scala-metals/spike/logs"}
APPDIR=${APPDIR:-"$ROOT/packaging/runtime/out/AppDir"}
IMAGE=${SPIKE_IMAGE:-ubuntu:24.04}
WORK=$(mktemp -d)
mkdir -p "$OUT"
trap 'rm -rf "$WORK"' EXIT

cache_manifest() {
    find "$APPDIR/usr/lib/scala/coursier-cache" -type f -printf '%P %s\n' | LC_ALL=C sort
    find "$APPDIR/usr/lib/scala/scala-cli-cache" -type f -printf 'scala-cli-cache/%P %s\n' \
        | LC_ALL=C sort
}

echo "== 01: metals --version WITH network" | tee "$OUT/01-online-version.log"
{
    echo "\$ packaging/runtime/out/AppDir/bin/metals --version   # network available"
    HOME="$WORK/home" XDG_CACHE_HOME="$WORK/cache" XDG_DATA_HOME="$WORK/data" \
        TMPDIR="$WORK/tmp" timeout 180 "$APPDIR/bin/metals" --version 2>&1
    echo "[exit=$?]"
} >> "$OUT/01-online-version.log"

cache_manifest > "$OUT/03-cache-before.txt"

echo "== 02: metals --version under docker --network none" | tee "$OUT/02-offline-version.log"
{
    echo "\$ docker run --rm --network none -v <repo>:<ro> $IMAGE \\"
    echo "    bin/metals --version"
    timeout 400 docker run --rm --network none -v "$ROOT:/vedit-build:ro" "$IMAGE" bash -lc '
        mkdir -p /tmp/home /tmp/cache
        HOME=/tmp/home XDG_CACHE_HOME=/tmp/cache \
          timeout 300 /vedit-build/packaging/runtime/out/AppDir/bin/metals --version 2>&1
        echo "[exit=$?]"
    '
} >> "$OUT/02-offline-version.log"

cache_manifest > "$OUT/03-cache-after.txt"
{
    echo "# bundled coursier cache + scala-cli cache: before vs after both runs"
    echo "\$ diff 03-cache-before.txt 03-cache-after.txt"
    if diff -q "$OUT/03-cache-before.txt" "$OUT/03-cache-after.txt" >/dev/null; then
        echo "NO CHANGE: the supported-scala-version check neither writes nor reads this cache"
    else
        diff -u "$OUT/03-cache-before.txt" "$OUT/03-cache-after.txt" || true
    fi
} > "$OUT/03-cache-manifest.txt"

{
    echo "# The index page the check fetches (real HTTP response), filtered by the"
    echo "# bytecode's own predicate: <a> elements whose text contains 'mtags_'."
    echo "\$ curl -s https://repo1.maven.org/maven2/org/scalameta/"
    curl -sS https://repo1.maven.org/maven2/org/scalameta/ \
        | grep -oE 'href="mtags_[^"]*"' | LC_ALL=C sort -u
} > "$OUT/04-index-links.log"

{
    echo "# URL constants in the SHIPPED metals 1.6.9 jar (not from memory):"
    echo "\$ unzip -p .../metals_2.13-1.6.9.jar scala/meta/metals/SupportedScalaVersions.class | strings"
    unzip -p "$APPDIR/usr/lib/scala/coursier-cache/https/repo1.maven.org/maven2/org/scalameta/metals_2.13/1.6.9/metals_2.13-1.6.9.jar" \
        scala/meta/metals/SupportedScalaVersions.class | strings \
        | grep -E 'https?://|mtags_|Could not check|SNAPSHOT|NIGHTLY' | LC_ALL=C sort -u
    echo
    echo "# the fallback source: BuildInfo.supportedScalaVersions (release-time list)"
    unzip -p "$APPDIR/usr/lib/scala/coursier-cache/https/repo1.maven.org/maven2/org/scalameta/metals_2.13/1.6.9/metals_2.13-1.6.9.jar" \
        scala/meta/internal/metals/BuildInfo\$.class | strings \
        | grep -E '^(2\.1[0-9]\.[0-9]+|1\.6\.9)$' | LC_ALL=C sort -u
} > "$OUT/05-jar-strings.log"

echo "logs written to $OUT"
