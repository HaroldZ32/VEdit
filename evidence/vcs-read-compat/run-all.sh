#!/usr/bin/env bash
# AC-3.0 evidence driver: delivery-baseline git compatibility of the VCS read path.
#
# Produces evidence/vcs-read-compat/logs/:
#   00-context.txt        environment + git versions on all three surfaces
#   10-matrix-1831.log    the read command matrix on git 1.8.3.1 (vedit-build image)
#   20-matrix-current.log the same matrix on the current git (host)
#   30-shim.log           the read tests behind a shim that rejects the legacy spellings
#   40-grep.log           grep proof that the source/tests do not use them
#   60-svn-scan.log       SVN 1.7 parameter scan (input for batch 6b)
#
# Every probe asserts at start-up that its cwd is NOT the VEdit working tree, and
# all fixtures are fresh `mktemp -d` directories (AC-0.4).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
EVID="$ROOT/evidence/vcs-read-compat"
mkdir -p "$EVID/logs"
chmod +x "$EVID"/*.sh "$EVID/shim/git"

RUST_ARGS=(
  docker run --rm
  -e HOME=/vedit-build/target/home
  -e CARGO_HOME=/vedit-build/target/cargo-home
  -e RUSTUP_HOME=/opt/rust
  -e RUSTUP_TOOLCHAIN=1.99.0
  -e NO_AT_BRIDGE=1
  -v "$ROOT":/vedit-build
  -w /tmp
  vedit-build:local
)

{
  echo "# batch 3a-fix VCS read compatibility - run context"
  echo "collected_at: $(date --iso-8601=seconds)"
  echo "workspace: $ROOT"
  echo "git_head: $(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo '<no git>')"
  echo "host_git: $(git --version)"
  echo "build_image_git: $(docker run --rm vedit-build:local git --version)"
  echo "qt6_image_git: $(docker run --rm vedit-qt6-build:local git --version)"
  echo "header_sha256: $(sha256sum "$ROOT/crates/vd-ffi/include/vd_ffi.h" | cut -d' ' -f1)"
} | tee "$EVID/logs/00-context.txt"

echo "== matrix on the delivery baseline (git 1.8.3.1, vedit-build:local) =="
"${RUST_ARGS[@]}" bash -lc \
  'bash /vedit-build/evidence/vcs-read-compat/matrix.sh /vedit-build baseline-git-1.8.3.1' \
  2>&1 | tee "$EVID/logs/10-matrix-1831.log"

echo "== matrix on the current git (host) =="
CURRENT_DIR=$(mktemp -d)
( cd "$CURRENT_DIR" && bash "$EVID/matrix.sh" "$ROOT" current-host-git ) \
  2>&1 | tee "$EVID/logs/20-matrix-current.log"

echo "== controlled git shim: rejects BOTH legacy spellings, forwards the rest =="
"${RUST_ARGS[@]}" bash -lc '
  set -euo pipefail
  cd /tmp
  COMPAT_BIN=$(cargo test --manifest-path /vedit-build/Cargo.toml -p vd-vcs --test compat \
      --no-run --message-format=json 2>/dev/null \
      | sed -n "s/.*\"executable\":\"\([^\"]*\)\".*/\1/p" | tail -1)
  ABI_BIN=$(cargo test --manifest-path /vedit-build/Cargo.toml -p vd-ffi --test abi \
      --no-run --message-format=json 2>/dev/null \
      | sed -n "s/.*\"executable\":\"\([^\"]*\)\".*/\1/p" | tail -1)
  echo "compat_bin=$COMPAT_BIN"
  echo "abi_bin=$ABI_BIN"
  test -x "$COMPAT_BIN"
  test -x "$ABI_BIN"

  export PATH="/vedit-build/evidence/vcs-read-compat/shim:$PATH"
  export VEDIT_SHIM_LOG=/tmp/vedit-vcs-shim.log
  export VEDIT_REAL_GIT=/usr/bin/git
  export QT_QPA_PLATFORM=offscreen

  echo "== shim self-test: both legacy spellings must be rejected =="
  set +e
  git -C / status >/dev/null 2>&1
  echo "  legacy-path-option exit=$?"
  git status --porcelain=v1 >/dev/null 2>&1
  echo "  legacy-porcelain-v1 exit=$?"
  set -e
  echo "self_test_log:"
  cat "$VEDIT_SHIM_LOG"
  : > "$VEDIT_SHIM_LOG"

  echo "== product read tests through the shim =="
  "$COMPAT_BIN" --test-threads=1 --nocapture
  "$ABI_BIN" vcs_read --test-threads=1 --nocapture

  echo "== shim log of the read tests =="
  cat "$VEDIT_SHIM_LOG"
  rejected=$(grep -c REJECTED "$VEDIT_SHIM_LOG" || true)
  forwarded=$(grep -vc REJECTED "$VEDIT_SHIM_LOG" || true)
  outside_tmp=$(grep -v REJECTED "$VEDIT_SHIM_LOG" | grep -vc "^cwd=/tmp" || true)
  workspace_hits=$(grep -c "cwd=/vedit-build" "$VEDIT_SHIM_LOG" || true)
  fixture_reads=$(grep -c "^cwd=/tmp/.tmp.*arg=status" "$VEDIT_SHIM_LOG" || true)
  echo "rejected=$rejected"
  echo "forwarded=$forwarded"
  echo "outside_tmp=$outside_tmp"
  echo "workspace_hits=$workspace_hits"
  echo "fixture_reads=$fixture_reads"
  [ "$rejected" = "0" ] || { echo "FAIL: a legacy argument reached the shim"; exit 1; }
  [ "$forwarded" -ge 5 ] || { echo "FAIL: the shim saw too few real calls"; exit 1; }
  [ "$outside_tmp" = "0" ] || { echo "FAIL: a git invocation ran outside /tmp"; exit 1; }
  [ "$workspace_hits" = "0" ] || { echo "FAIL: a git invocation ran in the VEdit working tree"; exit 1; }
  [ "$fixture_reads" -ge 1 ] || { echo "FAIL: no status read happened inside a fixture"; exit 1; }
  echo "shim_probe=PASS"
' 2>&1 | tee "$EVID/logs/30-shim.log"

echo "== grep proof: the legacy spellings are absent from product sources/tests =="
{
  cd "$ROOT"
  echo "# grep proof (cwd=$ROOT)"
  echo "collected_at: $(date --iso-8601=seconds)"
  echo
  echo "## quoted option-string literals in product sources and tests"
  echo '$ grep -rnE -- "\"-C\"|'"'"'-C'"'"'" crates/ src/workspace/vcs/'
  grep -rnE -- '"-C"|\x27-C\x27' crates/ src/workspace/vcs/ || true
  count_c=$(grep -rnE -- '"-C"|\x27-C\x27' crates/ src/workspace/vcs/ | wc -l || true)
  echo "count=$count_c"
  echo
  echo "## the legacy porcelain spelling"
  echo '$ grep -rn -- "--porcelain=v1" crates/ src/workspace/vcs/'
  grep -rn -- '--porcelain=v1' crates/ src/workspace/vcs/ || true
  count_p=$(grep -rn -- '--porcelain=v1' crates/ src/workspace/vcs/ | wc -l || true)
  echo "count=$count_p"
  echo
  echo "## the 1.9-only SVN selector form"
  echo '$ grep -rn -- "--show-item" crates/ src/workspace/vcs/'
  grep -rn -- '--show-item' crates/ src/workspace/vcs/ || true
  count_s=$(grep -rn -- '--show-item' crates/ src/workspace/vcs/ | wc -l || true)
  echo "count=$count_s"
  echo
  echo "## the only occurrences left are the EVIDENCE negative controls (by design):"
  grep -rnE -- '(-C|-C\?)|--porcelain=v1' "$EVID"/matrix.sh "$EVID"/shim/git || true
  echo
  echo "source_hits=$((count_c + count_p + count_s))"
  [ "$count_c" = "0" ] && [ "$count_p" = "0" ] && [ "$count_s" = "0" ] \
    || { echo "FAIL: a legacy spelling remains in the product sources"; exit 1; }
  echo "grep_probe=PASS"
} 2>&1 | tee "$EVID/logs/40-grep.log"

echo "== SVN 1.7 parameter scan =="
( cd "$(mktemp -d)" && bash "$EVID/svn-scan.sh" "$ROOT" ) 2>&1 | tee "$EVID/logs/60-svn-scan.log"

echo "[vcs-read-compat] PASS: matrices + shim + grep + svn scan; logs in $EVID/logs"
