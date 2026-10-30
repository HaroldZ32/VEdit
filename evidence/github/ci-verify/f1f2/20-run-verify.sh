#!/usr/bin/env bash
# Reviewer verification of the F1/F2 follow-up changes (T4) against contract v2 AC-B1..AC-B4.
# Runs INSIDE the pinned image vedit-build:local with the repo mounted at /vedit-build.
# The F1/F2 changes are UNCOMMITTED in the working tree at review time; the caller records
# the file hashes before and after this run so the result is bound to a known tree state.
set -u

export HOME=/vedit-build/target/home
export CARGO_HOME=/vedit-build/target/cargo-home
export VEDIT_SLANG=/vedit-build/build/slang/out/slang
export VEDIT_GSV_DATADIR=/opt/gsv4/share/gtksourceview-4
V=/vedit-build/target/verible-pin/extract/verible-v0.0-4298-g085eef05/bin
export VEDIT_VERIBLE_LS=$V/verible-verilog-ls
export VEDIT_VERIBLE_FORMAT=$V/verible-verilog-format
export VEDIT_VERIBLE_LINT=$V/verible-verilog-lint
mkdir -p "$HOME"

if [ ! -s /etc/machine-id ]; then
  printf '%s\n' 0123456789abcdef0123456789abcdef > /etc/machine-id
fi
cd /vedit-build

echo "HEAD=$(git rev-parse HEAD)"
echo "branch=$(git rev-parse --abbrev-ref HEAD)"
echo "date=$(date -Is)"
echo "nproc=$(nproc) loadavg=$(cat /proc/loadavg)"
echo "--- F1/F2 file hashes at start ---"
sha256sum crates/vd-app/src/app.rs crates/vd-app/src/selftest.rs crates/vd-editor/src/document.rs

echo '=== 1/6 cargo fmt --all -- --check ==='
cargo fmt --all -- --check
echo "fmt_exit=$?"

echo '=== 2/6 cargo clippy --workspace --all-targets -- -D warnings ==='
cargo clippy --workspace --all-targets -- -D warnings 2>&1 | tail -3
echo "clippy_exit=${PIPESTATUS[0]}"

echo '=== 3/6 the new AC-B2 document tests (LF + CRLF), headless ==='
cargo test -p vd-editor opening_ 2>&1 | grep -E '^test |running|test result:'
echo "acb2_unit_exit=${PIPESTATUS[0]}"

echo '=== 4/6 cargo test --workspace (VEDIT_* unset: repo gate env) ==='
env -u VEDIT_SLANG -u VEDIT_VERIBLE_LS -u VEDIT_VERIBLE_FORMAT -u VEDIT_VERIBLE_LINT -u VEDIT_VERIBLE_DIR \
  cargo test --workspace 2>&1 | grep -E 'test result:|^error|FAILED|panicked'
echo "test_exit=${PIPESTATUS[0]}"

echo '=== 5/6 cargo build --release --workspace ==='
cargo build --release --workspace 2>&1 | tail -3
echo "build_exit=${PIPESTATUS[0]}"
sha256sum target/release/vd-app

echo '=== 6/6 xvfb-run -a target/release/vd-app --selftest all ==='
xvfb-run -a target/release/vd-app --selftest all > /tmp/st.json 2>&1
echo "selftest_exit=$?"
cat /tmp/st.json

echo '--- F1/F2 file hashes at end ---'
sha256sum crates/vd-app/src/app.rs crates/vd-app/src/selftest.rs crates/vd-editor/src/document.rs
echo 'DONE'
