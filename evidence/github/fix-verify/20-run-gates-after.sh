#!/usr/bin/env bash
# Reviewer's independent AC-8 re-run for commit 7acd487 (T2).
# Runs INSIDE the pinned image vedit-build:local with the repo at /vedit-build.
# Records each gate's exit code separately; the caller tees stdout to evidence.
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
echo "date=$(date -Is)"
echo "nproc=$(nproc) loadavg=$(cat /proc/loadavg)"
echo "--- product-path tree status (must be empty) ---"
git status --porcelain -- crates/
echo "--- end tree status ---"

echo '=== 1/5 cargo build --release --workspace ==='
cargo build --release --workspace 2>&1 | tail -4
echo "build_exit=${PIPESTATUS[0]}"
sha256sum target/release/vd-app

echo '=== 2/5 cargo fmt --all -- --check ==='
cargo fmt --all -- --check
echo "fmt_exit=$?"

echo '=== 3/5 cargo clippy --workspace --all-targets -- -D warnings ==='
cargo clippy --workspace --all-targets -- -D warnings 2>&1 | tail -4
echo "clippy_exit=${PIPESTATUS[0]}"

echo '=== 4/5 cargo test --workspace ==='
cargo test --workspace 2>&1 | grep -E 'test result:|^error|panicked' 
echo "test_exit=${PIPESTATUS[0]}"

echo '=== 4b/5 display-free AC-4 unit test (DISPLAY unset) ==='
env -u DISPLAY cargo test -p vd-app --bin vd-app \
  repeated_lsp_status_refreshes_keep_the_status_text_bounded -- --exact 2>&1 | tail -6
echo "ac4_displayfree_exit=${PIPESTATUS[0]}"

echo '=== 5/5 xvfb-run -a target/release/vd-app --selftest all ==='
xvfb-run -a target/release/vd-app --selftest all > /tmp/selftest-all.json 2> /tmp/selftest-all.err
echo "selftest_exit=$?"
echo '--- stderr (tail) ---'
tail -4 /tmp/selftest-all.err
echo '--- stdout JSON ---'
cat /tmp/selftest-all.json
echo
echo 'DONE'
