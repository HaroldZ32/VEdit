#!/usr/bin/env bash
# Shared helpers for the Qt6/KF6 C++ lane (M0 build base, task T10).
# Sourced by scripts/build-qt.sh and build/gates/qt6-cxx.sh; never executed.
# shellcheck shell=bash

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export ROOT
QT_VERSIONS_FILE="$ROOT/scripts/versions-qt.env"
# BuildKit keeps state in the Docker CLI config directory. Keep the normal
# config (it may hold registry credentials) when it is writable; a sandboxed or
# read-only HOME would otherwise make every `docker build` fail, so fall back to
# a workspace directory (same rule as scripts/lib.sh).
if [ -z "${DOCKER_CONFIG:-}" ]; then
  home_dir="${HOME:-/nonexistent}"
  if ! { mkdir -p "$home_dir/.docker" 2>/dev/null && [ -w "$home_dir/.docker" ]; }; then
    export DOCKER_CONFIG="$ROOT/target/docker-config"
    mkdir -p "$DOCKER_CONFIG"
  fi
fi
# The pinned Qt6 image. CI may set VEDIT_QT6_BUILD_IMAGE_REF to the registry ref
# it pushed/pulled; with the variable unset the local tag is used, so local
# behaviour is predictable.
QT_BUILD_IMAGE_TAG="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"
QT_DOCKERFILE="$ROOT/scripts/docker/Dockerfile.build"
QT_BUILD_CONTEXT="$ROOT/scripts/docker"
# Stage of that Dockerfile (the LAST stage stays the historical CentOS 7 image).
QT_BUILD_STAGE="qt6-build"
# Build directory and evidence directory of this lane. Both are chosen so the
# lane writes ONLY inside its declared scope and inside git-ignored paths:
#   * target/              -> gitignored build output (CMake build tree),
#   * evidence/m0-build/   -> the task's evidence directory (committed).
QT_BUILD_DIR="$ROOT/target/qt6-build"
QT_EVIDENCE_DIR="$ROOT/evidence/m0-build/build-logs"

# Export every pinned input of the Qt6 lane, then refuse to continue when one is
# missing: a half-pinned image must never be built silently.
load_qt_versions() {
  # shellcheck source=versions-qt.env
  set -a
  . "$QT_VERSIONS_FILE"
  set +a
  : "${VEDIT_QT6_BASE_IMAGE:?missing pin}" \
    "${VEDIT_QT6_BASE_IMAGE_DIGEST:?missing pin}" \
    "${VEDIT_QT6_VERSION:?missing pin}" \
    "${VEDIT_KF6_VERSION:?missing pin}" \
    "${VEDIT_KTEXTEDITOR_VERSION:?missing pin}" \
    "${VEDIT_KSYNTAXHIGHLIGHTING_VERSION:?missing pin}" \
    "${VEDIT_TREE_SITTER_VERSION:?missing pin}"
}

# The pinned base image at its digest (never by mutable tag).
qt6_base_image_ref() {
  printf '%s@%s\n' "$VEDIT_QT6_BASE_IMAGE" "$VEDIT_QT6_BASE_IMAGE_DIGEST"
}

# Make the pinned base image available. The same digest is requested from
# Docker Hub when the mirror cannot deliver it; a digest tag cannot be created
# locally, so the resolved reference is passed straight to the build.
ensure_qt6_base_image() {
  local ref
  ref=$(qt6_base_image_ref)
  QT6_BASE_IMAGE_RESOLVED_REF="$ref"
  if docker image inspect "$ref" >/dev/null 2>&1; then
    return 0
  fi
  if docker pull "$ref"; then
    QT6_BASE_IMAGE_RESOLVED_REF="$ref"
    return 0
  fi
  local fallback="docker.io/library/ubuntu@${VEDIT_QT6_BASE_IMAGE_DIGEST}"
  echo "[qt6-image] primary registry failed; retrying the SAME digest from $fallback" >&2
  if docker pull "$fallback"; then
    QT6_BASE_IMAGE_RESOLVED_REF="$fallback"
    return 0
  fi
  echo "FATAL: cannot obtain the pinned Qt6 base image $ref (mirror and Docker Hub fallback both failed)" >&2
  exit 1
}

# Build (or reuse) the pinned Qt6/KF6 build image. Argument 1 is a log to mirror
# the docker build output into.
build_qt6_image() {
  local log="${1:-/dev/null}"
  if [ "${VEDIT_SKIP_IMAGE_BUILD:-0}" = "1" ]; then
    if docker image inspect "$QT_BUILD_IMAGE_TAG" >/dev/null 2>&1; then
      echo "[qt6-image] VEDIT_SKIP_IMAGE_BUILD=1: reusing the existing image $QT_BUILD_IMAGE_TAG"
      return 0
    fi
    echo "FATAL: VEDIT_SKIP_IMAGE_BUILD=1, but the Qt6 image '$QT_BUILD_IMAGE_TAG' does not exist locally." >&2
    echo "       Pull it first (docker pull $QT_BUILD_IMAGE_TAG, or VEDIT_QT6_BUILD_IMAGE_PULL=1), or unset VEDIT_SKIP_IMAGE_BUILD." >&2
    exit 1
  fi
  if [ "${VEDIT_QT6_BUILD_IMAGE_PULL:-0}" = "1" ]; then
    echo "[qt6-image] pulling $QT_BUILD_IMAGE_TAG (VEDIT_QT6_BUILD_IMAGE_PULL=1)"
    docker pull "$QT_BUILD_IMAGE_TAG" || {
      echo "FATAL: cannot pull the Qt6 image $QT_BUILD_IMAGE_TAG" >&2
      exit 1
    }
    return 0
  fi
  ensure_qt6_base_image
  echo "[qt6-image] building $QT_BUILD_IMAGE_TAG (stage $QT_BUILD_STAGE, base ${QT6_BASE_IMAGE_RESOLVED_REF})"
  docker build \
    --target "$QT_BUILD_STAGE" \
    --build-arg VEDIT_QT6_BASE_IMAGE="${QT6_BASE_IMAGE_RESOLVED_REF}" \
    -f "$QT_DOCKERFILE" \
    -t "$QT_BUILD_IMAGE_TAG" \
    "$QT_BUILD_CONTEXT" 2>&1 | tee "$log"
}

# Run a command inside the pinned Qt6 image with the workspace mounted at
# /vedit-build. A rootless daemon maps container root to the calling user, which
# keeps build output host-owned; a rootful daemon needs --user for the same
# result (scripts/lib.sh uses the same rule).
run_in_qt6_container() {
  mkdir -p "$QT_EVIDENCE_DIR" "$QT_BUILD_DIR"
  local user_args=()
  if ! docker info --format '{{json .SecurityOptions}}' 2>/dev/null | grep -q '"name=rootless"'; then
    user_args=(--user "$(id -u):$(id -g)")
  fi
  docker run --rm "${user_args[@]}" \
    -v "$ROOT":/vedit-build \
    -w /vedit-build \
    -e HOME=/tmp \
    -e XDG_RUNTIME_DIR=/tmp/xdg-runtime \
    -e LANG=C.UTF-8 \
    "$QT_BUILD_IMAGE_TAG" "$@"
}
