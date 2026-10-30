#!/usr/bin/env bash
# Shared helpers for scripts/build.sh and scripts/gates.sh (sourced, never run).
# shellcheck shell=bash

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export ROOT
VERSIONS_FILE="$ROOT/scripts/versions.env"
BUILD_IMAGE_TAG="vedit-build:local"

# Export every pinned input, then refuse to continue with a missing one.
load_versions() {
  # shellcheck source=versions.env
  set -a
  # shellcheck disable=SC1090
  . "$VERSIONS_FILE"
  set +a
  : "${VEDIT_BASE_IMAGE:?missing pin}" "${VEDIT_BASE_IMAGE_DIGEST:?missing pin}"
  : "${CENTOS_VAULT_BASE:?missing pin}"
  : "${RUST_TOOLCHAIN:?missing pin}" "${RUSTUP_URL:?missing pin}" "${RUSTUP_SHA256:?missing pin}"
  : "${GSV_URL:?missing pin}" "${GSV_SHA256:?missing pin}"
  : "${MESON_WHEEL_URL:?missing pin}" "${MESON_WHEEL_SHA256:?missing pin}"
  : "${NINJA_WHEEL_URL:?missing pin}" "${NINJA_WHEEL_SHA256:?missing pin}"
}

image_ref() {
  printf '%s@%s\n' "$VEDIT_BASE_IMAGE" "$VEDIT_BASE_IMAGE_DIGEST"
}

# Make the base image available at its pinned digest (never by mutable tag).
ensure_base_image() {
  local ref
  ref=$(image_ref)
  if ! docker image inspect "$ref" >/dev/null 2>&1; then
    echo "[image] pulling pinned base image $ref"
    docker pull "$ref" || {
      echo "FATAL: cannot obtain the pinned base image $ref" >&2
      exit 1
    }
  fi
  docker image inspect "$ref" >/dev/null 2>&1 || {
    echo "FATAL: pinned base image $ref is not present after pull" >&2
    exit 1
  }
}

# Build the pinned CentOS 7 build image. Argument 1 is a log file to mirror to.
build_build_image() {
  local log="${1:-/dev/null}"
  ensure_base_image
  echo "[image] building $BUILD_IMAGE_TAG from the pinned inputs"
  DOCKER_BUILDKIT=0 docker build \
    --build-arg CENTOS_VAULT_BASE="$CENTOS_VAULT_BASE" \
    --build-arg RUSTUP_URL="$RUSTUP_URL" \
    --build-arg RUSTUP_SHA256="$RUSTUP_SHA256" \
    --build-arg RUST_TOOLCHAIN="$RUST_TOOLCHAIN" \
    --build-arg GSV_URL="$GSV_URL" \
    --build-arg GSV_SHA256="$GSV_SHA256" \
    --build-arg MESON_WHEEL_URL="$MESON_WHEEL_URL" \
    --build-arg MESON_WHEEL_SHA256="$MESON_WHEEL_SHA256" \
    --build-arg NINJA_WHEEL_URL="$NINJA_WHEEL_URL" \
    --build-arg NINJA_WHEEL_SHA256="$NINJA_WHEEL_SHA256" \
    -f "$ROOT/scripts/docker/Dockerfile.build" \
    -t "$BUILD_IMAGE_TAG" "$ROOT/scripts/docker" 2>&1 | tee "$log"
}

# Run a command in the build image with the workspace mounted at /vedit-build.
# Runs as the calling user so build outputs are not root-owned on the host.
run_in_build_container() {
  mkdir -p "$ROOT/target/home" "$ROOT/target/cargo-home" "$ROOT/build/evidence"
  # NOTE: no --user. This daemon runs rootless, so container root maps to the
  # calling host user; passing --user 1000 would map to a subuid and make the
  # mounted workspace read-only. Root here produces host-owned files.
  docker run --rm \
    -e HOME=/vedit-build/target/home \
    -e CARGO_HOME=/vedit-build/target/cargo-home \
    -e RUSTUP_HOME=/opt/rust \
    -e RUSTUP_TOOLCHAIN="$RUST_TOOLCHAIN" \
    -e SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-}" \
    -e NO_AT_BRIDGE=1 \
    -v "$ROOT:/vedit-build" \
    -w /vedit-build \
    "$BUILD_IMAGE_TAG" "$@"
}
