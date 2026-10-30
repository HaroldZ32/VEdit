#!/usr/bin/env bash
# T25 entry point (host): build the payload in the pinned CentOS 7 image, then
# verify both products in a clean, offline CentOS 7 container.
#
#   bash packaging/tarball/build.sh
#
# Products land in packaging/tarball/out/, evidence in packaging/tarball/evidence/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LANE="$ROOT/packaging/tarball"
# shellcheck source=../../scripts/lib.sh
source "$ROOT/scripts/lib.sh"
load_versions

mkdir -p "$LANE/evidence" "$LANE/out"
TEST_IMAGE="${TEST_IMAGE:-vedit-tarball-cleanroom:local}"

echo "== 1/3 pinned build image =="
build_build_image "$LANE/evidence/00-image-build.log"

echo "== 2/3 stage the payload (inside the pinned image) =="
run_in_build_container bash packaging/tarball/stage.sh 2>&1 | tee "$LANE/evidence/00-stage.log"

echo "== 3/3 clean-room acceptance image =="
# The Docker CLI wants a writable config dir; the sandboxed HOME is read-only, so
# keep it inside the workspace. The classic builder needs no buildx state.
mkdir -p "$ROOT/target/docker-config"
DOCKER_BUILDKIT=0 DOCKER_CONFIG="$ROOT/target/docker-config" \
  docker build -t "$TEST_IMAGE" -f "$LANE/docker/Dockerfile.test" "$LANE/docker" \
  > "$LANE/evidence/01-cleanroom-image.log" 2>&1
tail -1 "$LANE/evidence/01-cleanroom-image.log"

echo "== clean-room: full run (network disabled, exec-capable /tmp) =="
DOCKER_CONFIG="$ROOT/target/docker-config" docker run --rm --network none \
  -e CLEANROOM_MODE=full \
  -v "$LANE/out:/art:ro" -v "$LANE/evidence:/evid" -v "$LANE:/scripts:ro" \
  "$TEST_IMAGE" sh /scripts/clean-room.sh 2>&1 | tee "$LANE/evidence/20-cleanroom-full.log"

echo "== clean-room: noexec /tmp (self-extracting fallback) =="
docker run --rm --network none --tmpfs /tmp:noexec \
  -e CLEANROOM_MODE=noexec \
  -v "$LANE/out:/art:ro" -v "$LANE/evidence:/evid" -v "$LANE:/scripts:ro" \
  "$TEST_IMAGE" sh /scripts/clean-room.sh 2>&1 | tee "$LANE/evidence/21-cleanroom-noexec.log"

echo "PACKAGE OK"
