#!/usr/bin/env bash
# Self-test of packaging/runtime/verify/verify-appimage-centos7.sh inside a STOCK
# centos:7 container (no fuse, no network, nothing installed).
#
# Expected outcome in a container: kernel.is-3.10 FAILS, because a container
# shares the host kernel — that is exactly the item only the real VM can answer.
# Everything else must PASS. Do not present this log as the real-machine result.
#
# Usage (from the repository root):
#   bash evidence/centos7-final-verify/selftest-in-container.sh
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMAGE=${VEDIT_CENTOS7_IMAGE:-docker.m.daocloud.io/library/centos:7}
docker run --rm \
  -v "$ROOT/packaging/runtime/out":/img:ro \
  -v "$ROOT/packaging/runtime/verify":/script:ro \
  "$IMAGE" bash -c '
    set -u
    W=$(mktemp -d); cd "$W"
    cp /img/VEdit-0.1.1-x86_64.AppImage . && cp /img/SHA256SUMS . && cp /script/verify-appimage-centos7.sh .
    chmod +x verify-appimage-centos7.sh
    echo "# container: $(cat /etc/centos-release 2>/dev/null | tr -d "\n") | kernel: $(uname -r) (host kernel, expected)"
    echo "# ldd: $(ldd --version | head -1)"
    bash verify-appimage-centos7.sh; rc=$?
    echo "SCRIPT_EXIT=$rc"
    echo "REPORT_HEAD=$(ls -1 verify-report-*.txt | head -1)"
    rm -rf "$W"
  '
