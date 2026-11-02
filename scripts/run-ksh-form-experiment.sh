#!/usr/bin/env bash
# T61: F1 vs F2 experiment driver (host side).
#
# Builds BOTH forms of the self-built KSyntaxHighlighting (identical except
# NO_STANDARD_PATHS) and runs the isolation/determinism experiment inside the
# pinned M0 image:
#   * a SIMULATED host KDE install under /usr/share (the 450 plain XMLs +
#     SystemVerilog + a unique marker),
#   * a simulated user data directory (~/.local/share) with a unique marker,
#   * our AppDir whitelist (verilog.xml only),
#   * every scenario's visible definitions, their on-disk sources and exit codes.
#
#   bash scripts/run-ksh-form-experiment.sh
#
# Evidence: evidence/m0-ksh-selfbuild/form-experiment-logs/
set -euo pipefail

# shellcheck source=lib-qt.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib-qt.sh"
# shellcheck source=versions-ksh.env
set -a
. "$ROOT/scripts/versions-ksh.env"
set +a
load_qt_versions

echo "[t61] building F1 (NO_STANDARD_PATHS=ON)"
bash "$ROOT/scripts/build-ksh-selfbuild.sh" --form F1 >/dev/null

echo "[t61] building F2 (NO_STANDARD_PATHS=OFF)"
bash "$ROOT/scripts/build-ksh-selfbuild.sh" --form F2 >/dev/null

echo "[t61] running the isolation experiment inside ${QT_BUILD_IMAGE_TAG}"
run_in_qt6_container bash scripts/run-ksh-form-experiment-in-container.sh

echo "[t61] evidence: evidence/m0-ksh-selfbuild/form-experiment-logs/"
