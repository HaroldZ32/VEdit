#!/usr/bin/env bash
# Regenerate T15 (Verible LSP client) evidence inside the pinned image:
#   docker run --rm -e HOME=/vedit-build/target/home \
#     -e CARGO_HOME=/vedit-build/target/cargo-home -e RUSTUP_HOME=/opt/rust \
#     -e RUSTUP_TOOLCHAIN=1.99.0 -v "$PWD:/vedit-build" -w /vedit-build \
#     vedit-build:local bash /vedit-build/evidence/lsp/run-evidence.sh
set -u
ROOT=${ROOT:-/vedit-build}
OUT="$ROOT/evidence/lsp"
cd "$ROOT" || exit 1

{
  echo "# lsp-probe — workspace WITHOUT a rules file"
  echo "\$ cargo run -q -p vd-lsp --example lsp-probe -- crates/vd-lsp/fixtures/project"
  cargo run -q -p vd-lsp --example lsp-probe -- crates/vd-lsp/fixtures/project
  echo "EXIT=$?"
  echo
  echo "# lsp-probe — workspace WITH .rules.verible_lint (explicit --rules_config)"
  echo "\$ cargo run -q -p vd-lsp --example lsp-probe -- crates/vd-lsp/fixtures/project_with_rules"
  cargo run -q -p vd-lsp --example lsp-probe -- crates/vd-lsp/fixtures/project_with_rules
  echo "EXIT=$?"
} > "$OUT/probe-transcript.txt" 2>&1

{
  echo "# after every probe run above the LSP process must be gone"
  echo "\$ ps -eo pid,ppid,stat,comm | grep -i verible || echo none"
  ps -eo pid,ppid,stat,comm | grep -i verible || echo "none"
  echo
  echo "\$ ps -eo stat,comm | awk '\$1 ~ /^Z/ {print}' | grep -i verible || echo 'no verible zombie'"
  ps -eo stat,comm | awk '$1 ~ /^Z/ {print}' | grep -i verible || echo "no verible zombie"
} > "$OUT/zombie-proof.txt" 2>&1

{
  echo "\$ cargo test -p vd-lsp --test verible_ls request_timeout_and_cancellation_are_enforced -- --exact --nocapture"
  cargo test -p vd-lsp --test verible_ls request_timeout_and_cancellation_are_enforced -- --exact --nocapture
  echo "EXIT=$?"
  echo
  echo "\$ cargo test -p vd-lsp --test verible_ls initialize_timeout_is_enforced_and_the_child_is_reaped -- --exact --nocapture"
  cargo test -p vd-lsp --test verible_ls initialize_timeout_is_enforced_and_the_child_is_reaped -- --exact --nocapture
  echo "EXIT=$?"
  echo
  echo "\$ cargo test -p vd-lsp --test verible_ls lazy_start_crash_restart_and_shutdown_leave_no_zombie -- --exact --nocapture"
  cargo test -p vd-lsp --test verible_ls lazy_start_crash_restart_and_shutdown_leave_no_zombie -- --exact --nocapture
  echo "EXIT=$?"
} > "$OUT/timeouts-cancellation-lifecycle.txt" 2>&1

{
  echo "# scoped gates for crates/vd-lsp (T15), run in the pinned CentOS 7 image"
  echo "# date: $(date -u +%Y-%m-%dT%H:%M:%SZ); rustc: $(rustc --version)"
  echo
  echo "\$ cargo fmt -p vd-lsp -- --check"
  cargo fmt -p vd-lsp -- --check
  echo "EXIT=$?"
  echo
  echo "\$ cargo clippy -p vd-lsp --all-targets -- -D warnings"
  cargo clippy -p vd-lsp --all-targets -- -D warnings
  echo "EXIT=$?"
  echo
  echo "\$ cargo test -p vd-lsp"
  cargo test -p vd-lsp
  echo "EXIT=$?"
} > "$OUT/scoped-gates.log" 2>&1

echo "== evidence files =="
ls -l "$OUT"
echo "== probe summary =="
grep -E 'lazy_start_running|server_argv|## diagnostics|## definition|byte_equal|## shutdown' "$OUT/probe-transcript.txt"
echo "== gates =="
grep -E "EXIT=|test result" "$OUT/scoped-gates.log"
echo "== zombies =="
cat "$OUT/zombie-proof.txt"
