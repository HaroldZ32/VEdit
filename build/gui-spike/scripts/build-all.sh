#!/usr/bin/env bash
# Host-side orchestration for T5: build env image, build app, package both
# deliverables, then run the clean-room offline acceptance container.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD_IMAGE="${BUILD_IMAGE:-vedit-gui-spike-build:local}"
TEST_IMAGE="${TEST_IMAGE:-vedit-gui-spike-test:local}"
GSV_SHA256=63f487a7ab59612ffa9e7be1883dbd274ab1016afcede3239b93b0fc384df874

mkdir -p out evidence screenshots
echo "== [1/4] build environment image (centos:7 + gtk3-devel 3.22.30 + gsv4 4.6.1 + rust) =="
if ! docker image inspect "$BUILD_IMAGE" >/dev/null 2>&1; then
  DOCKER_BUILDKIT=0 docker build \
    --build-arg GSV_SHA256="$GSV_SHA256" \
    -f docker/Dockerfile.build -t "$BUILD_IMAGE" docker/
fi

echo "== [2/4] build the gtk-rs spike app inside centos:7 =="
docker run --rm -v "$ROOT:/work" -w /work "$BUILD_IMAGE" \
  bash -lc 'bash scripts/build-app.sh' 2>&1 | tee evidence/build-app-run.log

echo "== [3/4] assemble AppDir + AppImage + tar.gz (ELF GLIBC gate) =="
docker run --rm -v "$ROOT:/work" -w /work -v "$ROOT/../../.mpd/recon:/recon:ro" \
  "$BUILD_IMAGE" bash -lc 'bash scripts/package.sh' 2>&1 | tee evidence/package-run.log

echo "== [4/4] clean-room offline acceptance (centos:7, no network, Xvfb) =="
if ! docker image inspect "$TEST_IMAGE" >/dev/null 2>&1; then
  DOCKER_BUILDKIT=0 docker build -f docker/Dockerfile.test -t "$TEST_IMAGE" docker/
fi
cp fixture/top.sv evidence/top.sv
docker run --rm --network none \
  -v "$ROOT/out:/art:ro" -v "$ROOT/evidence:/evid" \
  -v "$ROOT/scripts:/scripts:ro" \
  "$TEST_IMAGE" /bin/bash /scripts/clean-room-accept.sh 2>&1 | tee evidence/clean-room.log

echo "== done =="
ls -lh out/
