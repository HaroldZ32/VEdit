#!/usr/bin/env bash
# T12 — re-verify the reworked seams (commit 000ecee) on the HOST.
# AC-A3: default behaviour unchanged, skip/pull semantics, new cache/container vars.
set -u
cd /home/haroldzhao/MyProj/DshProj/VEdit || exit 9

echo "HEAD=$(git rev-parse HEAD)"
echo "date=$(date -Is)"
echo "nproc=$(nproc) loadavg=$(cat /proc/loadavg)"
echo "--- git status before ---"; git status --porcelain; echo "--- end ---"

echo '=== A3.1 build-image.sh, NO env vars (local default must be unchanged) ==='
bash scripts/build-image.sh
echo "novars_exit=$?"

echo '=== A3.2 gates.sh with VEDIT_SKIP_IMAGE_BUILD=1 (reuse, no --user) ==='
VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/gates.sh
echo "gates_skip_exit=$?"

echo '=== A3.3 skip + missing ref must FATAL, exit 1 ==='
VEDIT_SKIP_IMAGE_BUILD=1 VEDIT_BUILD_IMAGE_REF=vedit-build:definitely-missing bash scripts/build-image.sh
echo "skip_missing_exit=$?"

echo '=== A3.4 VEDIT_BUILD_IMAGE_PULL=1 + unpublished local tag (pull attempted, must fail loudly) ==='
VEDIT_BUILD_IMAGE_PULL=1 VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-image.sh
echo "pull_local_exit=$?"

echo '=== A3.5 VEDIT_CONTAINER_USER=1 on the local ROOTLESS daemon (light probe, no full gates) ==='
VEDIT_CONTAINER_USER=1 bash -c 'source scripts/lib.sh; load_versions; run_in_build_container id' 2>&1 | tail -4
echo "container_user_exit=${PIPESTATUS[0]}"

echo '=== A3.6 cache vars with the default docker driver (expected: loud failure, not silent fallback) ==='
VEDIT_BUILD_IMAGE_REF=vedit-build:cachetest \
VEDIT_BUILD_CACHE_FROM=type=local,src=/tmp/bx-none \
VEDIT_BUILD_CACHE_TO=type=local,dest=/tmp/bx-out \
  bash scripts/build-image.sh 2>&1 | tail -12
echo "cache_vars_exit=${PIPESTATUS[0]}"

echo '--- git status after ---'; git status --porcelain; echo '--- end ---'
echo DONE
