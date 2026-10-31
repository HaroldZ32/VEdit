#!/usr/bin/env bash
# One-command build of the Qt6/KF6 C++ lane (M0 build base, task T10):
#
#   pinned image (verify/build) -> cmake configure -> build -> ctest -> ELF facts
#   -> xcb + software-rendering window smoke
#
# Everything after the image build runs INSIDE the pinned image
# (scripts/build-qt-in-container.sh), so the host toolchain is never part of the
# result. The lane is documented in docs/BUILD.md §12; the pins live in
# scripts/versions-qt.env and scripts/docker/qt6-locked-packages.txt.
#
#   bash scripts/build-qt.sh              # configure + build + ctest + xcb smoke
#   bash scripts/build-qt.sh --no-test    # configure + build only (static check)
#
# Environment (all optional):
#   VEDIT_QT6_BUILD_IMAGE_REF=ghcr.io/<owner>/vedit-qt6-build:latest   reuse/push ref
#   VEDIT_QT6_BUILD_IMAGE_PULL=1      docker pull that ref instead of building
#   VEDIT_SKIP_IMAGE_BUILD=1          never build; the ref must already exist
set -euo pipefail

# shellcheck source=lib-qt.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib-qt.sh"
load_qt_versions

run_tests=1
for arg in "$@"; do
  case "$arg" in
    --no-test) run_tests=0 ;;
    -h | --help) sed -n '2,19p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *)
      echo "FATAL: unknown argument: $arg (supported: --no-test)" >&2
      exit 2
      ;;
  esac
done

EVID="$QT_EVIDENCE_DIR"
mkdir -p "$EVID"

echo "[qt6] pinned matrix: Qt ${VEDIT_QT6_VERSION} / KF6 ${VEDIT_KF6_VERSION} / KTextEditor ${VEDIT_KTEXTEDITOR_VERSION} / KSyntaxHighlighting ${VEDIT_KSYNTAXHIGHLIGHTING_VERSION} / tree-sitter ${VEDIT_TREE_SITTER_VERSION}"
echo "[qt6] build image: ${QT_BUILD_IMAGE_TAG} (stage ${QT_BUILD_STAGE} of scripts/docker/Dockerfile.build, base $(qt6_base_image_ref))"

build_qt6_image "$EVID/20-qt6-image-build.log"
docker image inspect "$QT_BUILD_IMAGE_TAG" --format 'image_id={{.Id}}' | tee "$EVID/21-qt6-image-id.txt"

if [ "$run_tests" = "1" ]; then
  run_in_qt6_container bash scripts/build-qt-in-container.sh --tests
  echo "[qt6] PASS: configure + build + ctest + xcb/software-rendering smoke inside ${QT_BUILD_IMAGE_TAG}"
else
  run_in_qt6_container bash scripts/build-qt-in-container.sh --no-tests
  echo "[qt6] PASS: configure + build (warnings-as-errors) inside ${QT_BUILD_IMAGE_TAG}; tests skipped (--no-test)"
fi
echo "[qt6] evidence: evidence/m0-build/build-logs/2x-qt6-* and 3x-qt6-*"
