#!/usr/bin/env bash
# Host-side orchestration for T23:
#   package image (pinned build image + GTK immodules)
#     -> build the AppImage from pinned sources
#     -> clean-room image (no GTK at all)
#     -> offline Xvfb acceptance, evidence under evidence/appimage/.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
PKG="$ROOT/packaging/appimage"
EVID="$ROOT/evidence/appimage"
set -a
# shellcheck source=/dev/null
. "$ROOT/scripts/versions.env"
set +a
mkdir -p "$EVID" "$PKG/out"

echo "== [1/3] package image (vedit-build + gtk3-immodule-xim + ibus-gtk3) =="
DOCKER_BUILDKIT=0 docker build \
  -f "$PKG/Dockerfile.package" \
  --build-arg BUILD_IMAGE=vedit-build:local \
  --build-arg CENTOS_VAULT_BASE="$CENTOS_VAULT_BASE" \
  -t vedit-appimage-package:local "$PKG"

echo "== [2/3] assemble the AppImage from pinned sources =="
docker run --rm -e HOME=/vedit-build/target/home \
  -e CARGO_HOME=/vedit-build/target/cargo-home -e RUSTUP_HOME=/opt/rust \
  -e RUSTUP_TOOLCHAIN="$RUST_TOOLCHAIN" -e NO_AT_BRIDGE=1 \
  -v "$ROOT:/vedit-build" -w /vedit-build \
  vedit-appimage-package:local bash /vedit-build/packaging/appimage/build-appimage.sh \
  2>&1 | tee "$EVID/build.log"

echo "== [3/3] clean-room image + offline acceptance =="
DOCKER_BUILDKIT=0 docker build \
  -f "$PKG/Dockerfile.test" \
  --build-arg CENTOS_VAULT_BASE="$CENTOS_VAULT_BASE" \
  -t vedit-appimage-test:local "$PKG"
docker run --rm --network none \
  -v "$PKG/out:/art:ro" -v "$EVID:/evid" -v "$PKG:/pack:ro" \
  vedit-appimage-test:local /bin/bash /pack/clean-room-accept.sh \
  2>&1 | tee "$EVID/clean-room.log"

echo "== done =="
ls -lh "$PKG/out"
