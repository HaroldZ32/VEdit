#!/usr/bin/env bash
# Build and run the folding-form probe inside the pinned Qt6 image.
#
#   bash evidence/docs-amendments/probe/run.sh
#
# The image already carries Qt6Core + KF6SyntaxHighlighting + g++; the probe is
# compiled to /tmp inside the container (nothing is written outside evidence/).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)

echo "collected_at: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "git_head: $(git -C "$ROOT" rev-parse HEAD)"
echo "image: vedit-qt6-build:local"
echo "cmd: g++ -std=c++20 -O0 -fPIC folding_form_probe.cpp \$(pkg-config --cflags --libs Qt6Core) -lKF6SyntaxHighlighting"
echo "--- output ---"

# Match scripts/lib-qt.sh: a rootless daemon already maps container root to the
# calling user; forcing --user breaks reads of 0600 files in the bind mount.
user_args=()
if ! docker info --format '{{json .SecurityOptions}}' 2>/dev/null | grep -q '"name=rootless"'; then
  user_args=(--user "$(id -u):$(id -g)")
fi

docker run --rm "${user_args[@]}" \
  -v "$ROOT":/vedit-build -w /vedit-build \
  -e HOME=/tmp -e LANG=C.UTF-8 \
  vedit-qt6-build:local bash -c '
    set -euo pipefail
    g++ -std=c++20 -O0 -fPIC -o /tmp/folding-form-probe \
      evidence/docs-amendments/probe/folding_form_probe.cpp \
      $(pkg-config --cflags Qt6Core) \
      -I/usr/include/KF6/KSyntaxHighlighting \
      $(pkg-config --libs Qt6Core) \
      -lKF6SyntaxHighlighting
    /tmp/folding-form-probe \
      /vedit-build/resources/syntax/vedit-verilog.xml \
      /vedit-build/resources/syntax/vedit-systemverilog.xml
  '
