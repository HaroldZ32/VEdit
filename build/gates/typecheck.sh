#!/usr/bin/env bash
# Gate `typecheck`: cargo clippy --workspace --all-targets -- -D warnings, plus
# the C++/Qt6 lane's static check (configure + build with warnings-as-errors
# inside the pinned image). Declared fallback: if clippy is not installed, cargo
# check --workspace runs and the script says so on the same line (no silent
# downgrade). The Rust workspace is created by T7, the Qt6 build base by T10;
# until one exists that half is honestly RED.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

# --- C++/Qt6 lane (M0 build base, T10) ---------------------------------------
# The Qt6 lane's own code is compiled with -Wall -Wextra -Wpedantic -Werror in
# cmake/smoke/CMakeLists.txt, so `--no-test` is the C++ static check: a warning
# in our code fails this gate. (The pinned image carries no clang-tidy by
# design; adding a second analyser is a later decision, not a silent downgrade.)
if [ -f "$ROOT/CMakeLists.txt" ] && [ -f "$ROOT/scripts/versions-qt.env" ]; then
  echo "[typecheck] C++/Qt6 lane: configure + build with warnings-as-errors (pinned image)"
  bash "$ROOT/build/gates/qt6-cxx.sh" --no-test \
    || no_gate "[typecheck] FAIL: the C++/Qt6 lane failed to configure/build with -Werror"
else
  echo "[typecheck] N/A: no Qt6 build base yet (T10); the C++ lane has nothing to check."
fi

# --- Rust workspace compile gate -------------------------------------------
# WHERE it runs: the same rule as scripts/gates.sh. The pinned toolchain
# (rust-toolchain.toml: 1.99.0) is the toolchain of record, and on this host that
# rustup toolchain has no working `cargo` binary at all:
#     error: the 'cargo' binary ... is not applicable to the '1.99.0-...' toolchain
# The host is therefore used only when its clippy really runs; otherwise the gate
# DELEGATES TO THE PINNED CENTOS 7 IMAGE and runs the identical command there.
#
# This is an execution-site change, not a weakened check: the command, the
# `--all-targets` scope and `-D warnings` are unchanged, there is no `|| true`,
# no `cargo check` fallback and no pass-on-failure path. If clippy fails, the
# gate is RED wherever it ran.
if [ ! -f "$ROOT/Cargo.toml" ]; then
  no_gate "[typecheck] 本项目无此门禁：尚无 Cargo workspace（Cargo.toml 不存在），Rust 骨架由 T7 交付；此门禁保持 RED，不跳过。"
fi

cd "$ROOT"
if cargo clippy --version >/dev/null 2>&1; then
  echo "[typecheck] Rust workspace: host toolchain works -> cargo clippy --workspace --all-targets -- -D warnings"
  cargo clippy --workspace --all-targets -- -D warnings
  echo "[typecheck] PASS (host toolchain)"
  exit 0
fi

echo "[typecheck] host clippy unusable (pinned 1.99.0 toolchain has no cargo binary)"
echo "[typecheck] delegating to the pinned image, same command (scripts/gates.sh pattern)"
echo "[typecheck]   cargo clippy --workspace --all-targets -- -D warnings"
# shellcheck source=../../scripts/lib.sh
source "$ROOT/scripts/lib.sh"
load_versions
# The gate's own log directory. A repeatable gate run must NOT write into a task's
# evidence directory: `evidence/gate-infra/` belongs to the task that produced it,
# and every later `typecheck` used to overwrite its 10/11 files — that is evidence
# rot (a reviewer's record changes under them because someone re-ran a gate).
# `build/evidence/` is the repository's existing home for gate logs (scripts/gates.sh).
EVID="$ROOT/build/evidence/typecheck"
mkdir -p "$EVID"
# The log directory is operational (regenerated on every run), so it records the
# run context the way a task evidence file does: collection time, git HEAD and the
# dirty paths. It is deliberately NOT a task-evidence artifact: nobody cites it.
{
  echo "# gate log: typecheck (operational; rewritten by every run)"
  echo "collected_at: $(date --iso-8601=seconds)"
  echo "git_head: $(git -C "$ROOT" rev-parse HEAD)"
  echo "git_status_porcelain:"
  git -C "$ROOT" status --porcelain | sed 's/^/  /'
  echo
} > "$EVID/00-run.txt"
build_build_image "$EVID/10-typecheck-image-build.log"
docker image inspect "$BUILD_IMAGE_TAG" --format 'image_id={{.Id}}' | tee "$EVID/11-typecheck-image-id.txt"
# The banner makes the delegation visible in the raw log: the Rust check below ran
# inside the pinned image (container facts printed from inside the container).
run_in_build_container bash -lc '
  set -euo pipefail
  echo "[typecheck] container facts: $(head -1 /etc/centos-release) | $(rustc --version) | $(cargo --version)"
  cd /vedit-build
  echo "[typecheck] cargo clippy --workspace --all-targets -- -D warnings"
  cargo clippy --workspace --all-targets -- -D warnings
'
echo "[typecheck] PASS (pinned image ${BUILD_IMAGE_TAG})"
