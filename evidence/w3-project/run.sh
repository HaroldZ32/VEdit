#!/usr/bin/env bash
# Reproduce the T6 (project-model) evidence without touching the repository's
# build configuration.
#
#   bash evidence/w3-project/run.sh
#
# Every command runs inside the pinned images:
#   * vedit-build:local      — Rust toolchain of record (cargo test / clippy / fmt)
#   * vedit-qt6-build:local  — Qt6/KF6 build of the product + the lane's ctest
#
# The two-process restart proof shares one state directory between two separate
# processes: `--seed` writes project-model.json, `--restore` starts from that
# directory only.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
EV="$ROOT/evidence/w3-project"
LOGS="$EV/logs"
mkdir -p "$LOGS" "$ROOT/target/tmp/w3-state"
rm -rf "$ROOT/target/tmp/w3-state"
mkdir -p "$ROOT/target/tmp/w3-state"

# shellcheck source=../../scripts/lib.sh
source "$ROOT/scripts/lib.sh"
load_versions

echo "[w3-project] Rust: project-model tests (bad .f cases + backend argv dump)"
run_in_build_container bash -lc '
  cd /vedit-build
  cargo test -p vd-ffi project::tests -- --nocapture 2>&1
' | tee "$LOGS/30-rust-project-tests.log"

echo "[w3-project] Rust: clippy + fmt for the touched crates (workspace gate)"
run_in_build_container bash -lc '
  cd /vedit-build
  cargo clippy --workspace --all-targets -- -D warnings 2>&1 | tail -3
  echo "CLIPPY-EXIT=${PIPESTATUS[0]}"
  cargo fmt --all -- --check
  echo "FMT-EXIT=$?"
' > "$LOGS/40-clippy-fmt.log" 2>&1

echo "[w3-project] Qt: build the lane's selftest, run it twice (seed + restore)"
docker run --rm -v "$ROOT":/vedit-build vedit-qt6-build:local bash -lc '
  set -euo pipefail
  cd /vedit-build
  cmake --build target/qt6-build --target vedit-workspace-project-selftest
  mkdir -p /tmp/xdg && chmod 700 /tmp/xdg
  export QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg
  BIN=target/qt6-build/src/app/workspace-lane-project/vedit-workspace-project-selftest
  $BIN --selfcheck tests/fixtures/verilog-project
  echo "== seed (process 1) =="
  $BIN --seed tests/fixtures/verilog-project /vedit-build/target/tmp/w3-state
  cp target/tmp/w3-state/project-model.json /vedit-build/evidence/w3-project/logs/20-project-model-before-restart.json
  echo "== restore (process 2, separate process, state dir only) =="
  $BIN --restore /vedit-build/target/tmp/w3-state 11
  cp target/tmp/w3-state/project-model.json /vedit-build/evidence/w3-project/logs/21-project-model-after-restart.json
  echo "== project-model.json before vs after (empty diff = identical) =="
  diff -u evidence/w3-project/logs/20-project-model-before-restart.json \
          evidence/w3-project/logs/21-project-model-after-restart.json \
    && echo STATE-IDENTICAL
' | tee "$LOGS/10-selfcheck-and-restart.log"

echo "[w3-project] A-9: filelist variables (project table > environment, cycle, missing)"
docker run --rm -v "$ROOT":/vedit-build -e PRECEDENCE=environment -e HOME=/tmp/a9home-evidence \
  vedit-qt6-build:local bash -lc '
  cd /vedit-build
  mkdir -p /tmp/a9home-evidence/a9-home /tmp/xdg && chmod 700 /tmp/xdg
  printf "module d; endmodule\n" > /tmp/a9home-evidence/a9-home/d.sv
  export QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg
  BIN=target/qt6-build/src/app/workspace-lane-project/vedit-workspace-project-selftest
  echo "=== project table + environment (table wins) ==="
  $BIN --expansion evidence/w3-project/a9-fixture RTL=rtl WIDTH_VALUE=8 PRECEDENCE=project
  echo "=== control: environment only ==="
  $BIN --expansion evidence/w3-project/a9-fixture
' | tee "$LOGS/60-a9-expansion.log"

echo "[w3-project] A-17: literal \`\$' paths, the \$\$ escape, and kept+reported missing paths"
docker run --rm -v "$ROOT":/vedit-build -e HOME=/tmp/a17home vedit-qt6-build:local bash -lc '
  cd /vedit-build
  mkdir -p /tmp/xdg && chmod 700 /tmp/xdg
  export QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg
  BIN=target/qt6-build/src/app/workspace-lane-project/vedit-workspace-project-selftest
  echo "--- a17-fixture: literal-dollar paths (1 file before A-17, 3 after)"
  $BIN --expansion evidence/w3-project/a17-fixture
  echo "--- a17-escape: \$\$ control (always 1 file)"
  $BIN --expansion evidence/w3-project/a17-escape
  echo "--- a17-missing: expanded + home paths that do not exist (kept + reported)"
  $BIN --expansion evidence/w3-project/a17-missing A17_RTL=rtl
' | tee "$LOGS/75-a17-after.log"

docker run --rm -v "$ROOT":/vedit-build vedit-qt6-build:local bash -lc '
  cd /vedit-build
  mkdir -p /tmp/xdg-a17 && chmod 700 /tmp/xdg-a17
  export QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-a17
  BIN=target/qt6-build/src/app/workspace-lane-project/vedit-workspace-project-selftest
  $BIN --selfcheck tests/fixtures/verilog-project | grep -E "a17\.|PROJECT-MODEL-SELFTEST"
  echo "SELFCHECK-EXIT=${PIPESTATUS[0]}"
' | tee "$LOGS/76-a17-selfcheck.log"

echo "[w3-project] A-17: vd-config tests, clippy, fmt (crate + workspace), docs gate"
run_in_build_container bash -lc '
  cd /vedit-build
  cargo test -p vd-config > evidence/w3-project/logs/71-a17-vdconfig-test.log 2>&1
  echo "TEST-EXIT=$?"
  cargo clippy -p vd-config --all-targets -- -D warnings > evidence/w3-project/logs/72-a17-clippy-crate.log 2>&1
  echo "CLIPPY-CRATE-EXIT=$?"
  cargo clippy --workspace --all-targets -- -D warnings > evidence/w3-project/logs/74-a17-clippy-workspace.log 2>&1
  echo "CLIPPY-WORKSPACE-EXIT=$?"
  cargo fmt -p vd-config -- --check > evidence/w3-project/logs/73a-a17-fmt-vdconfig.log 2>&1
  echo "FMT-VDCONFIG-EXIT=$?"
  cargo fmt --all -- --check > evidence/w3-project/logs/73b-a17-fmt-workspace.log 2>&1
  echo "FMT-WORKSPACE-EXIT=$?"
  cargo test --workspace > evidence/w3-project/logs/78-a17-workspace-test.log 2>&1
  echo "WORKSPACE-TEST-EXIT=$?"
' | tee "$LOGS/71-a17-gate-summary.log"
docs_status=0
bash "$ROOT/build/gates/docs.sh" > "$LOGS/77-a17-docs-gate.log" 2>&1 || docs_status=$?
echo "DOCS-GATE-EXIT=$docs_status" | tee -a "$LOGS/77-a17-docs-gate.log"

echo "[w3-project] Qt: the lane's own ctest entry"
docker run --rm -v "$ROOT":/vedit-build vedit-qt6-build:local bash -lc '
  cd /vedit-build
  ctest --test-dir target/qt6-build -R vedit-workspace-project-selftest --output-on-failure
' | tee "$LOGS/11-ctest.log"

echo "[w3-project] done; logs in $LOGS"
