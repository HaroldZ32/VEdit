#!/usr/bin/env bash
# Shared helpers for scripts/build.sh and scripts/gates.sh (sourced, never run).
# shellcheck shell=bash

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export ROOT
VERSIONS_FILE="$ROOT/scripts/versions.env"
# The pinned build image. CI sets VEDIT_BUILD_IMAGE_REF to the registry ref it
# pulled or pushed (e.g. ghcr.io/<owner>/vedit-build:latest); with the variable
# unset the historical local tag is used, so local behaviour is unchanged.
BUILD_IMAGE_TAG="${VEDIT_BUILD_IMAGE_REF:-vedit-build:local}"

# BuildKit keeps its state in the Docker CLI config directory. Keep the normal
# config (it may hold registry credentials) when it is writable; a sandboxed or
# read-only HOME would otherwise make every `docker build` fail, so fall back to
# a workspace directory in that case.
if [ -z "${DOCKER_CONFIG:-}" ]; then
  home_dir="${HOME:-/nonexistent}"
  if ! { mkdir -p "$home_dir/.docker" 2>/dev/null && [ -w "$home_dir/.docker" ]; }; then
    export DOCKER_CONFIG="$ROOT/target/docker-config"
    mkdir -p "$DOCKER_CONFIG"
  fi
fi

# The base-image pin lives in scripts/versions.env. Honour an incoming
# VEDIT_BASE_IMAGE (captured BEFORE the pin is sourced, so versions.env cannot
# silently overwrite it): a test or an alternative mirror can point the primary
# registry elsewhere without editing the pin. With no override the pinned value
# is used unchanged.
BASE_IMAGE_OVERRIDE="${VEDIT_BASE_IMAGE:-}"
# Registry used only when the primary registry cannot deliver the image after
# the bounded retries; the SAME digest is requested there (Docker Hub default).
VEDIT_BASE_IMAGE_FALLBACK="${VEDIT_BASE_IMAGE_FALLBACK:-docker.io}"
# Bounded pull retry: three attempts with a 2 s / 4 s exponential backoff.
BASE_IMAGE_PULL_ATTEMPTS=3
BASE_IMAGE_PULL_BACKOFF_SECONDS=2

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

# The repository path of an image reference, without its registry host
# ("docker.m.daocloud.io/library/centos" -> "library/centos").
base_image_repository() {
  local image="$1"
  local first="${image%%/*}"
  case "$first" in
    *.* | *:* | localhost) printf '%s\n' "${image#*/}" ;;
    *) printf '%s\n' "$image" ;;
  esac
}

# The pinned base image at its digest (primary registry, overridable).
image_ref() {
  printf '%s@%s\n' "${BASE_IMAGE_OVERRIDE:-$VEDIT_BASE_IMAGE}" "$VEDIT_BASE_IMAGE_DIGEST"
}

# The same pinned digest served by the fallback registry (Docker Hub default).
image_fallback_ref() {
  printf '%s/%s@%s\n' "$VEDIT_BASE_IMAGE_FALLBACK" \
    "$(base_image_repository "${BASE_IMAGE_OVERRIDE:-$VEDIT_BASE_IMAGE}")" \
    "$VEDIT_BASE_IMAGE_DIGEST"
}

# `docker pull` a base image with the bounded retry; every attempt is logged.
pull_base_image() {
  local ref="$1"
  local attempt=1
  local delay="$BASE_IMAGE_PULL_BACKOFF_SECONDS"
  while [ "$attempt" -le "$BASE_IMAGE_PULL_ATTEMPTS" ]; do
    echo "[image] pulling $ref (attempt $attempt/$BASE_IMAGE_PULL_ATTEMPTS)"
    if docker pull "$ref"; then
      return 0
    fi
    if [ "$attempt" -lt "$BASE_IMAGE_PULL_ATTEMPTS" ]; then
      echo "[image] pull attempt $attempt/$BASE_IMAGE_PULL_ATTEMPTS failed; retrying in ${delay}s" >&2
      sleep "$delay"
      delay=$((delay * 2))
    fi
    attempt=$((attempt + 1))
  done
  return 1
}

# Make the base image available at its pinned digest (never by mutable tag).
#
# The primary registry is retried a bounded number of times; when it still
# cannot deliver the image, the SAME @sha256 digest is pulled from the fallback
# registry (Docker Hub by default). `docker tag` refuses a digest reference as
# its target ("refusing to create a tag with a digest reference"), so the
# resolved reference is exported in BASE_IMAGE_RESOLVED_REF and handed to the
# build as the BASE_IMAGE build argument; the digest is unchanged, so the
# layers and the cache stay the same.
ensure_base_image() {
  local ref fallback
  ref=$(image_ref)
  BASE_IMAGE_RESOLVED_REF="$ref"
  if docker image inspect "$ref" >/dev/null 2>&1; then
    return 0
  fi
  if pull_base_image "$ref"; then
    docker image inspect "$ref" >/dev/null 2>&1 || {
      echo "FATAL: pinned base image $ref is not present after pull" >&2
      exit 1
    }
    return 0
  fi
  fallback=$(image_fallback_ref)
  if [ "$fallback" = "$ref" ]; then
    echo "FATAL: cannot obtain the pinned base image $ref (no distinct fallback registry)" >&2
    exit 1
  fi
  echo "[image] primary registry failed after $BASE_IMAGE_PULL_ATTEMPTS attempts; falling back to $VEDIT_BASE_IMAGE_FALLBACK: $fallback"
  if pull_base_image "$fallback"; then
    if ! docker image inspect "$fallback" >/dev/null 2>&1; then
      echo "FATAL: fallback base image $fallback is not present after pull" >&2
      exit 1
    fi
    BASE_IMAGE_RESOLVED_REF="$fallback"
    echo "[image] fallback pull succeeded; the build will use $fallback (same pinned @sha256 digest)"
    return 0
  fi
  echo "FATAL: cannot obtain the pinned base image $ref (primary registry and the $VEDIT_BASE_IMAGE_FALLBACK fallback both failed)" >&2
  exit 1
}

# Fetch the build image when a CI run points VEDIT_BUILD_IMAGE_PULL=1 at a
# registry ref. Local runs never pull.
ensure_build_image_pulled() {
  [ "${VEDIT_BUILD_IMAGE_PULL:-0}" = "1" ] || return 0
  echo "[image] pulling $BUILD_IMAGE_TAG (VEDIT_BUILD_IMAGE_PULL=1)"
  docker pull "$BUILD_IMAGE_TAG" || {
    echo "FATAL: cannot pull the build image $BUILD_IMAGE_TAG" >&2
    exit 1
  }
}

# Build the pinned CentOS 7 build image. Argument 1 is a log file to mirror to.
#
# Environment (all optional; unset behaviour is the local default):
#   VEDIT_BUILD_IMAGE_REF   image ref/tag to build or reuse
#                           (default: vedit-build:local)
#   VEDIT_BUILD_IMAGE_PULL  =1: docker pull that ref before anything else
#   VEDIT_SKIP_IMAGE_BUILD  =1: never build; the ref must already exist locally
build_build_image() {
  local log="${1:-/dev/null}"
  ensure_build_image_pulled
  if [ "${VEDIT_SKIP_IMAGE_BUILD:-0}" = "1" ]; then
    if docker image inspect "$BUILD_IMAGE_TAG" >/dev/null 2>&1; then
      echo "[image] VEDIT_SKIP_IMAGE_BUILD=1: reusing the existing image $BUILD_IMAGE_TAG"
      return 0
    fi
    echo "FATAL: VEDIT_SKIP_IMAGE_BUILD=1, but the build image '$BUILD_IMAGE_TAG' does not exist locally." >&2
    echo "       Pull it first (docker pull $BUILD_IMAGE_TAG, or VEDIT_BUILD_IMAGE_PULL=1), or unset VEDIT_SKIP_IMAGE_BUILD to build it." >&2
    exit 1
  fi
  ensure_base_image
  echo "[image] building $BUILD_IMAGE_TAG from the pinned inputs"
  # BASE_IMAGE is the primary pinned ref, or the Docker Hub fallback ref after a
  # mirror outage (same digest); a normal build passes the Dockerfile default.
  local base_image="${BASE_IMAGE_RESOLVED_REF:-$(image_ref)}"
  echo "[image] base image: $base_image"
  local args=(
    --build-arg BASE_IMAGE="$base_image"
    --build-arg CENTOS_VAULT_BASE="$CENTOS_VAULT_BASE"
    --build-arg RUSTUP_URL="$RUSTUP_URL"
    --build-arg RUSTUP_SHA256="$RUSTUP_SHA256"
    --build-arg RUST_TOOLCHAIN="$RUST_TOOLCHAIN"
    --build-arg GSV_URL="$GSV_URL"
    --build-arg GSV_SHA256="$GSV_SHA256"
    --build-arg MESON_WHEEL_URL="$MESON_WHEEL_URL"
    --build-arg MESON_WHEEL_SHA256="$MESON_WHEEL_SHA256"
    --build-arg NINJA_WHEEL_URL="$NINJA_WHEEL_URL"
    --build-arg NINJA_WHEEL_SHA256="$NINJA_WHEEL_SHA256"
    -f "$ROOT/scripts/docker/Dockerfile.build"
    -t "$BUILD_IMAGE_TAG"
    "$ROOT/scripts/docker"
  )
  # The plain `docker build` path has no cache import/export; only use buildx
  # when a CI layer cache is requested, so the local command stays unchanged.
  if [ -n "${VEDIT_BUILD_CACHE_FROM:-}" ] || [ -n "${VEDIT_BUILD_CACHE_TO:-}" ]; then
    build_build_image_with_cache "$log" "${args[@]}"
  else
    docker build "${args[@]}" 2>&1 | tee "$log"
  fi
}

# Fail loudly when a cache was requested but buildx/builder is missing: the
# caller asked for a cache, and silently falling back to `docker build` would
# hide that the cache was never used.
require_buildx() {
  if ! docker buildx version >/dev/null 2>&1; then
    echo "FATAL: VEDIT_BUILD_CACHE_FROM/VEDIT_BUILD_CACHE_TO need the buildx plugin," >&2
    echo "       but 'docker buildx version' failed. Install docker buildx, or unset both variables." >&2
    exit 1
  fi
  if ! docker buildx inspect >/dev/null 2>&1; then
    echo "FATAL: VEDIT_BUILD_CACHE_FROM/VEDIT_BUILD_CACHE_TO need a usable buildx builder." >&2
    echo "       Create one: docker buildx create --driver docker-container --name vedit-builder --use" >&2
    exit 1
  fi
}

# Build through buildx so the CI layer cache can be imported and exported.
# `docker buildx build --load` keeps the image in the local docker image store,
# exactly like `docker build`, so run_in_build_container() is unaffected.
build_build_image_with_cache() {
  local log="$1"
  shift
  require_buildx
  local cache_args=()
  if [ -n "${VEDIT_BUILD_CACHE_FROM:-}" ]; then
    cache_args+=(--cache-from "$VEDIT_BUILD_CACHE_FROM")
  fi
  if [ -n "${VEDIT_BUILD_CACHE_TO:-}" ]; then
    cache_args+=(--cache-to "$VEDIT_BUILD_CACHE_TO")
  fi
  echo "[image] build cache: from=${VEDIT_BUILD_CACHE_FROM:-<none>} to=${VEDIT_BUILD_CACHE_TO:-<none>} (docker buildx --load)"
  local status=0
  docker buildx build --load "${cache_args[@]}" "$@" 2>&1 | tee "$log" || status=${PIPESTATUS[0]}
  if [ "$status" -ne 0 ]; then
    echo "FATAL: the buildx build failed (exit $status); the requested cache was not usable." >&2
    echo "       Cache import/export depends on the buildx builder and the cache type" >&2
    echo "       (type=gha needs the GitHub Actions cache environment). If the error above" >&2
    echo "       is about cache support, create a container-driver builder:" >&2
    echo "         docker buildx create --driver docker-container --name vedit-builder --use" >&2
    echo "       (GitHub Actions does this with docker/setup-buildx-action.)" >&2
    echo "       Or unset VEDIT_BUILD_CACHE_FROM/VEDIT_BUILD_CACHE_TO." >&2
    exit 1
  fi
}

# Run a command in the build image with the workspace mounted at /vedit-build.
# Runs as the calling user so build outputs are not root-owned on the host.
run_in_build_container() {
  mkdir -p "$ROOT/target/home" "$ROOT/target/cargo-home" "$ROOT/build/evidence"
  # Default: NO --user. A rootless docker daemon maps container root to the
  # calling host user, which is what keeps the mounted workspace host-owned;
  # --user 1000 there would map to a subuid and make the workspace read-only.
  # A rootful daemon (GitHub runners) would write root-owned files instead, so
  # CI sets VEDIT_CONTAINER_USER=1 to run the container as the invoking uid.
  local user_args=""
  if [ "${VEDIT_CONTAINER_USER:-0}" = "1" ]; then
    local uid gid
    if ! uid=$(id -u 2>/dev/null) || [ -z "$uid" ]; then
      echo "FATAL: VEDIT_CONTAINER_USER=1, but 'id -u' is unavailable; cannot pick the container user" >&2
      return 1
    fi
    if ! gid=$(id -g 2>/dev/null) || [ -z "$gid" ]; then
      echo "FATAL: VEDIT_CONTAINER_USER=1, but 'id -g' is unavailable; cannot pick the container group" >&2
      return 1
    fi
    user_args="--user ${uid}:${gid}"
    echo "[image] container user: ${uid}:${gid} (VEDIT_CONTAINER_USER=1)"
  fi
  # shellcheck disable=SC2086 # $user_args is either empty or two numeric words
  docker run --rm \
    $user_args \
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
