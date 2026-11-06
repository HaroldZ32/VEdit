#!/usr/bin/env bash
# Run the AC-8.1 probe in the pinned image and record its log + exit code.
#
#   bash evidence/g6-diag/probe/run.sh <label> [available|missing]
#
# <label> is the evidence file stem (e.g. 00-red-before-fix / 30-green-after-fix);
# the log lands at evidence/g6-diag/<label>.log and the exit code at
# evidence/g6-diag/<label>-exit.txt.
#
# `available` runs the probe from the build tree (target/g6-diag-probe), where
# the product's documented local-checkout toolchain discovery finds
# .mpd/recon/verible/bin by walking up to scripts/fetch-verible.sh. No
# VEDIT_* override is passed: the gate must reflect the product's OWN probe.
#
# `missing` copies the same binary OUTSIDE the checkout (/tmp/g6-missing) and
# runs it with a PATH that has no verible, so the toolchain is genuinely absent -
# the negative control for "greyed only when really missing".
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
LABEL=${1:?usage: run.sh <label> [available|missing]}
MODE=${2:-available}
LOG="evidence/g6-diag/${LABEL}.log"
EXIT_FILE="evidence/g6-diag/${LABEL}-exit.txt"

cd "$ROOT"
mkdir -p evidence/g6-diag

USER_ARGS=()
if ! docker info --format '{{json .SecurityOptions}}' 2>/dev/null | grep -q '"name=rootless"'; then
  USER_ARGS=(--user "$(id -u):$(id -g)")
fi

in_container() {
  docker run --rm "${USER_ARGS[@]}" \
    -v "$ROOT":/vedit-build \
    -w /vedit-build \
    -e HOME=/tmp \
    -e XDG_RUNTIME_DIR=/tmp/xdg-runtime \
    -e LANG=C.UTF-8 \
    -e QT_QPA_PLATFORM=offscreen \
    vedit-qt6-build:local \
    bash -lc "mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime && $1"
}

echo "=== [1/2] build probe (current tree) ==="
in_container "bash evidence/g6-diag/probe/build.sh" 2>&1 | tee "evidence/g6-diag/${LABEL}-build.log"

echo "=== [2/2] run probe mode=${MODE} ==="
set +e
if [ "$MODE" = "missing" ]; then
  in_container "mkdir -p /tmp/g6-missing && cp target/g6-diag-probe/g6-diag-probe /tmp/g6-missing/ && cd /tmp/g6-missing && env -u VEDIT_VERIBLE_LS -u VEDIT_VERIBLE_LS_DIR -u VEDIT_VERIBLE_FORMAT -u VEDIT_VERIBLE_LINT -u VEDIT_SLANG PATH=/usr/bin:/bin ./g6-diag-probe --expect=missing --evidence-dir=/vedit-build/evidence/g6-diag/missing" 2>&1 | tee "$LOG"
else
  in_container "./target/g6-diag-probe/g6-diag-probe --expect=available --evidence-dir=/vedit-build/evidence/g6-diag" 2>&1 | tee "$LOG"
fi
status=${PIPESTATUS[0]}
set -e
echo "$status" > "$EXIT_FILE"
echo "PROBE-RUN label=${LABEL} mode=${MODE} exit=${status} log=${LOG}"
exit "$status"
