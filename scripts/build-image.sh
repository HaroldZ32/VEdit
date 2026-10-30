#!/usr/bin/env bash
# Build (or fetch) ONLY the pinned CentOS 7 build image.
#
#   bash scripts/build-image.sh
#
# This is the `image` workflow's entry point: publish the pinned image to a
# registry so `ci`/`release` can pull it instead of paying a cold build. It does
# not compile the workspace and does not run the gates — use scripts/build.sh or
# scripts/gates.sh for that.
#
# Environment (all optional; unset behaviour is the local default):
#   VEDIT_BUILD_IMAGE_REF=ghcr.io/<owner>/vedit-build:latest
#                            image ref to build/tag (default: vedit-build:local)
#   VEDIT_BUILD_IMAGE_PULL=1 docker pull that ref first (CI / reuse)
#   VEDIT_SKIP_IMAGE_BUILD=1 require the ref and never build it
#
# Exit status: 0 when the image exists (built, reused or pulled); non-zero with
# a FATAL line when the pinned inputs cannot be fetched, the build fails, or
# VEDIT_SKIP_IMAGE_BUILD=1 was set for a ref that does not exist locally.
set -euo pipefail

# shellcheck source=lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
load_versions

EVID="$ROOT/build/evidence"
mkdir -p "$EVID"
LOG="$EVID/01-image-build.log"

build_build_image "$LOG"

{
  echo "[image] ref=$BUILD_IMAGE_TAG"
  docker image inspect "$BUILD_IMAGE_TAG" --format 'image_id={{.Id}}'
} | tee -a "$LOG"
