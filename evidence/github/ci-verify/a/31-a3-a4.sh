#!/usr/bin/env bash
# T5 — AC-A3 (script seams) + AC-A4 (BuildKit) on the committed tree, run on the HOST
# (scripts/gates.sh delegates the compile gates into the pinned image by design).
# Each command's exit code is recorded; failures do not abort the script.
set -u
cd /home/haroldzhao/MyProj/DshProj/VEdit || exit 9

echo "HEAD=$(git rev-parse HEAD)"
echo "branch=$(git rev-parse --abbrev-ref HEAD)"
echo "date=$(date -Is)"
echo "--- git status before (working tree must be clean) ---"
git status --porcelain
echo "--- end status before ---"
echo "--- F1/F2 hashes ---"
sha256sum crates/vd-app/src/app.rs crates/vd-app/src/selftest.rs crates/vd-editor/src/document.rs

echo '=== AC-A3.1 gates.sh, NO env vars (default ref vedit-build:local, build allowed) ==='
bash scripts/gates.sh
echo "gates_novars_exit=$?"

echo '=== AC-A3.2 gates.sh with VEDIT_SKIP_IMAGE_BUILD=1 ==='
VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/gates.sh
echo "gates_skip_exit=$?"

echo '=== AC-A3.3 VEDIT_SKIP_IMAGE_BUILD=1 + missing ref must FATAL, exit 1 ==='
VEDIT_SKIP_IMAGE_BUILD=1 VEDIT_BUILD_IMAGE_REF=vedit-build:definitely-missing bash scripts/build-image.sh
echo "skip_missing_exit=$?"

echo '=== AC-A3.4 VEDIT_BUILD_IMAGE_PULL=1 with an unpublished local tag (pull attempted, must fail loudly) ==='
VEDIT_BUILD_IMAGE_PULL=1 VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-image.sh
echo "pull_local_exit=$?"

echo '=== AC-A4 BuildKit build into a fresh tag (DOCKER_BUILDKIT=1, no legacy flag anywhere) ==='
docker buildx version 2>&1 | head -2
DOCKER_BUILDKIT=1 VEDIT_BUILD_IMAGE_REF=vedit-build:ac4-verify bash scripts/build-image.sh
echo "buildkit_build_exit=$?"
docker image inspect vedit-build:ac4-verify --format 'ac4_image_id={{.Id}}' 2>&1
echo "--- build log: BuildKit markers ---"
grep -cE '^#|=> ' build/evidence/01-image-build.log 2>/dev/null || true
grep -c 'DEPRECATED\|legacy builder\|DOCKER_BUILDKIT=0' build/evidence/01-image-build.log 2>/dev/null || true
echo "--- build log head ---"
head -12 build/evidence/01-image-build.log 2>/dev/null

echo '--- git status after ---'
git status --porcelain
echo '--- end status after ---'
echo 'DONE'
