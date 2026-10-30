#!/usr/bin/env bash
# Make the slang build image available locally, resiliently:
#   * the image is already present  -> nothing is pulled (no extra time);
#   * otherwise the primary registry is retried a bounded number of times
#     (3 attempts, 2 s / 4 s backoff, every attempt logged);
#   * if it still cannot deliver the image, the SAME tag is pulled from the
#     fallback registry (Docker Hub by default) and tagged back as the expected
#     ref, so stage 1/3 of build/slang/build.sh and the local cache keep using
#     the same reference;
#   * both failing is fatal (`FATAL:` + exit 1) — never a silent continue.
#
#   bash build/slang/fetch-image.sh
#
# Environment (defaults in build/slang/versions.env):
#   SLANG_BUILD_IMAGE           build image ref (default: daocloud centos:7)
#   SLANG_BUILD_IMAGE_FALLBACK  fallback registry host (default: docker.io)
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=versions.env
set -a
# shellcheck disable=SC1091
. "$HERE/versions.env"
set +a
: "${SLANG_BUILD_IMAGE:?missing SLANG_BUILD_IMAGE}" \
  "${SLANG_BUILD_IMAGE_FALLBACK:?missing SLANG_BUILD_IMAGE_FALLBACK}"

IMAGE="$SLANG_BUILD_IMAGE"
FALLBACK_REGISTRY="$SLANG_BUILD_IMAGE_FALLBACK"
ATTEMPTS=3
BACKOFF_SECONDS=2

# The repository path of an image reference, without its registry host and tag
# ("docker.m.daocloud.io/library/centos:7" -> "library/centos").
image_repository() {
  local image="$1" first="${1%%/*}" rest
  case "$first" in
    *.* | *:* | localhost) rest="${image#*/}" ;;
    *) rest="$image" ;;
  esac
  # A tag can only appear after the last '/', so strip it there.
  case "${rest##*/}" in
    *:*) rest="${rest%:*}" ;;
  esac
  printf '%s\n' "$rest"
}

# The tag of an image reference ("…/centos:7" -> "7", no tag -> "latest").
image_tag() {
  local last="${1##*/}"
  case "$last" in
    *:*) printf '%s\n' "${last##*:}" ;;
    *) printf 'latest\n' ;;
  esac
}

# The same tag served by the fallback registry.
image_fallback_ref() {
  printf '%s/%s:%s\n' "$FALLBACK_REGISTRY" \
    "$(image_repository "$IMAGE")" "$(image_tag "$IMAGE")"
}

# `docker pull` with the bounded retry; every attempt is logged.
pull_with_retry() {
  local ref="$1" attempt=1 delay="$BACKOFF_SECONDS"
  while [ "$attempt" -le "$ATTEMPTS" ]; do
    echo "== pulling $ref (attempt $attempt/$ATTEMPTS) =="
    if docker pull "$ref"; then
      return 0
    fi
    if [ "$attempt" -lt "$ATTEMPTS" ]; then
      echo "== pull attempt $attempt/$ATTEMPTS failed; retrying in ${delay}s ==" >&2
      sleep "$delay"
      delay=$((delay * 2))
    fi
    attempt=$((attempt + 1))
  done
  return 1
}

if docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "== image $IMAGE is already present; no pull needed =="
  exit 0
fi

if pull_with_retry "$IMAGE"; then
  docker image inspect "$IMAGE" >/dev/null 2>&1 || {
    echo "FATAL: $IMAGE is not present after pulling it" >&2
    exit 1
  }
  exit 0
fi

FALLBACK=$(image_fallback_ref)
if [ "$FALLBACK" = "$IMAGE" ]; then
  echo "FATAL: cannot obtain the slang build image $IMAGE (no distinct fallback registry)" >&2
  exit 1
fi
echo "== primary registry failed after $ATTEMPTS attempts; falling back to $FALLBACK_REGISTRY: $FALLBACK =="
if pull_with_retry "$FALLBACK"; then
  docker image inspect "$FALLBACK" >/dev/null 2>&1 || {
    echo "FATAL: fallback image $FALLBACK is not present after pulling it" >&2
    exit 1
  }
  echo "== tagging $FALLBACK as $IMAGE (same tag, so later stages are unchanged) =="
  docker tag "$FALLBACK" "$IMAGE" || {
    echo "FATAL: could not tag $FALLBACK as $IMAGE" >&2
    exit 1
  }
  exit 0
fi

echo "FATAL: cannot obtain the slang build image $IMAGE (primary registry and the $FALLBACK_REGISTRY fallback both failed)" >&2
exit 1
