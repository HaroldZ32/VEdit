#!/usr/bin/env bash
# One-command build of the Qt6/KF6 C++ product (M0 build base, task T10):
#
#   Rust service layer -> pinned image (verify/build) -> cmake configure -> build
#   -> ctest -> ELF facts -> xcb + software-rendering window smoke
#
# Everything after the Rust stage runs INSIDE its pinned image
# (scripts/build-qt-in-container.sh), so the host toolchain is never part of the
# result. The lane is documented in docs/BUILD.md §12; the pins live in
# scripts/versions-qt.env and scripts/docker/qt6-locked-packages.txt.
#
# The Rust service layer (crates/vd-ffi, Slice 0 / task T4) is built FIRST in the
# pinned Rust/CentOS 7 image (`vedit-build:local`, the toolchain of record for
# the Rust workspace, AGENT.md §4), because the Qt6 image carries no Rust. There:
#   * `cargo test -p vd-ffi -p vd-search -p vd-settings -p vd-vcs` runs the unit
#     tests (AC-0.2);
#   * `cargo tree -p vd-ffi` must contain no GTK crate (AC-0.3/AC-S0.5);
#   * cbindgen writes crates/vd-ffi/include/vd_ffi.h on every build, and a Rust
#     type outside a doc comment is a hard failure (C2);
#   * the resulting target/release/libvd_ffi.a is handed to cmake, which links it
#     into vedit-app and fails configure when it is absent.
#
#   bash scripts/build-qt.sh              # Rust layer + configure + build + ctest + xcb smoke
#   bash scripts/build-qt.sh --no-test    # Rust layer + configure + build only
#
# Environment (all optional):
#   VEDIT_QT6_BUILD_IMAGE_REF=ghcr.io/<owner>/vedit-qt6-build:latest   reuse/push ref
#   VEDIT_QT6_BUILD_IMAGE_PULL=1      docker pull that ref instead of building
#   VEDIT_SKIP_IMAGE_BUILD=1          never build; the ref must already exist
#   VEDIT_BUILD_IMAGE_REF / VEDIT_BUILD_IMAGE_PULL   same switches for the Rust image
set -euo pipefail

# shellcheck source=lib-qt.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib-qt.sh"
# shellcheck source=lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
load_qt_versions
load_versions

run_tests=1
for arg in "$@"; do
  case "$arg" in
    --no-test) run_tests=0 ;;
    -h | --help) sed -n '2,33p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *)
      echo "FATAL: unknown argument: $arg (supported: --no-test)" >&2
      exit 2
      ;;
  esac
done

EVID="$QT_EVIDENCE_DIR"
mkdir -p "$EVID"
RUST_EVID="$ROOT/evidence/ffi-abi/logs"
mkdir -p "$RUST_EVID"

# --- Rust service layer (crates/vd-ffi, Slice 0 / task T4) -------------------
# Same rule as the Qt6 image: build it when it is absent, reuse the local tag
# otherwise, and record which image id produced the library.
echo "[rust-ffi] pinned Rust image: ${BUILD_IMAGE_TAG} (toolchain ${RUST_TOOLCHAIN})"
if docker image inspect "$BUILD_IMAGE_TAG" >/dev/null 2>&1; then
  echo "[rust-ffi] reusing the existing image ${BUILD_IMAGE_TAG}" | tee "$RUST_EVID/10-rust-image.txt"
else
  build_build_image "$RUST_EVID/10-rust-image-build.log"
  echo "image_tag=${BUILD_IMAGE_TAG} built=yes" | tee "$RUST_EVID/10-rust-image.txt"
fi
docker image inspect "$BUILD_IMAGE_TAG" --format 'image_id={{.Id}}' | tee -a "$RUST_EVID/10-rust-image.txt"

# Reproducible-build inputs: the Rust static library is part of the product, so
# the same SOURCE_DATE_EPOCH rule as scripts/build.sh applies here too.
if [ -z "${SOURCE_DATE_EPOCH:-}" ]; then
  SOURCE_DATE_EPOCH=$(git -C "$ROOT" show -s --format=%ct HEAD 2>/dev/null || true)
  export SOURCE_DATE_EPOCH
fi

echo "[rust-ffi] build + unit tests + dependency-tree + header checks in ${BUILD_IMAGE_TAG}"
run_in_build_container bash -lc '
  set -euo pipefail
  source scripts/versions.env
  export RUSTFLAGS="${RUSTFLAGS:-} ${RELEASE_RUSTFLAGS:?RELEASE_RUSTFLAGS is defined in scripts/versions.env}"
  export CARGO_INCREMENTAL=0
  EVID=evidence/ffi-abi/logs
  mkdir -p "$EVID"

  echo "== cargo build -p vd-ffi --release (verbose: the target list is in the log) =="
  cargo build -p vd-ffi --release -v 2>&1 | tee "$EVID/20-cargo-build-release.log"
  test -f target/release/libvd_ffi.a
  ls -l target/release/libvd_ffi.a | tee "$EVID/21-staticlib.txt"

  echo "== compiled Rust targets (rustc --crate-name lines, or Fresh lines when cached) =="
  {
    sed -n "s/.*--crate-name \([A-Za-z0-9_]*\).*/\1/p" "$EVID/20-cargo-build-release.log"
    sed -n "s/^ *Fresh \([A-Za-z0-9_-]*\) .*/\1/p" "$EVID/20-cargo-build-release.log"
  } | sort -u | tee "$EVID/22-rust-targets.txt"
  test -s "$EVID/22-rust-targets.txt"

  echo "== cargo test (the service unit tests, AC-0.2) =="
  cargo test -p vd-ffi -p vd-search -p vd-settings -p vd-vcs 2>&1 | tee "$EVID/23-cargo-test.log"

  echo "== cargo tree (GTK isolation, AC-0.3 / AC-S0.5) =="
  cargo tree -p vd-ffi > "$EVID/24-cargo-tree-vd-ffi.txt"
  if grep -Eq "gtk|sourceview4|vd-editor|vd-syntax" "$EVID/24-cargo-tree-vd-ffi.txt"; then
    echo "FATAL: the vd-ffi dependency tree contains GTK or an editor crate (AC-0.3)" >&2
    exit 1
  fi
  for reused in vd-config vd-lsp vd-deeplint vd-fim; do
    grep -q "$reused" "$EVID/24-cargo-tree-vd-ffi.txt" || {
      echo "FATAL: $reused is not in the vd-ffi dependency tree (AC-S0.5)" >&2; exit 1; }
  done
  grep -E "vd-config |vd-lsp |vd-deeplint |vd-fim " "$EVID/24-cargo-tree-vd-ffi.txt" \
    | sort -u | tee "$EVID/25-reused-crates.txt"

  echo "== cbindgen header =="
  test -f crates/vd-ffi/include/vd_ffi.h
  cp crates/vd-ffi/include/vd_ffi.h "$EVID/26-vd_ffi.h"
  # No Rust type may appear OUTSIDE a documentation comment (C2). The doc blocks
  # are stripped first, so a doc sentence that mentions a Rust type is fine.
  awk "
    BEGIN { in_comment = 0 }
    {
      line = \$0
      if (in_comment) { if (line ~ /\\*\\//) { in_comment = 0 } ; next }
      if (line ~ /\\/\\*/) { if (line ~ /\\*\\//) { next } ; in_comment = 1 ; next }
      print
    }" "$EVID/26-vd_ffi.h" > "$EVID/27-vd_ffi-comment-free.txt"
  if grep -En "\b(String|Vec|Option|Box|Arc|Result)\b|::|&str" "$EVID/27-vd_ffi-comment-free.txt"; then
    echo "FATAL: the generated header contains a Rust type (C2)" >&2
    exit 1
  fi
  echo "header check: no Rust type outside comments"
' 2>&1 | tee "$RUST_EVID/19-rust-stage-console.log"

echo "[qt6] pinned matrix: Qt ${VEDIT_QT6_VERSION} / KF6 ${VEDIT_KF6_VERSION} / KTextEditor ${VEDIT_KTEXTEDITOR_VERSION} / KSyntaxHighlighting ${VEDIT_KSYNTAXHIGHLIGHTING_VERSION} / tree-sitter ${VEDIT_TREE_SITTER_VERSION}"
echo "[qt6] build image: ${QT_BUILD_IMAGE_TAG} (stage ${QT_BUILD_STAGE} of scripts/docker/Dockerfile.build, base $(qt6_base_image_ref))"

build_qt6_image "$EVID/20-qt6-image-build.log"
docker image inspect "$QT_BUILD_IMAGE_TAG" --format 'image_id={{.Id}}' | tee "$EVID/21-qt6-image-id.txt"

if [ "$run_tests" = "1" ]; then
  run_in_qt6_container bash scripts/build-qt-in-container.sh --tests
  echo "[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke inside ${QT_BUILD_IMAGE_TAG}"
else
  run_in_qt6_container bash scripts/build-qt-in-container.sh --no-tests
  echo "[qt6] PASS: Rust service layer + configure + build (warnings-as-errors) inside ${QT_BUILD_IMAGE_TAG}; tests skipped (--no-test)"
fi
echo "[qt6] evidence: evidence/ffi-abi/logs/ (Rust layer) and evidence/m0-build/build-logs/2x-qt6-*, 3x-qt6-*"
