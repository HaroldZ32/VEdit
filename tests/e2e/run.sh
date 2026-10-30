#!/usr/bin/env bash
# T27 frozen-artifact acceptance harness. Run in a clean, network-less centos:7.
# Consumes packaging outputs; never builds or modifies product code.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/tests/e2e/out"
MODE=${1:-both}
APPIMAGE="$ROOT/packaging/appimage/out/VEdit-0.1.0-x86_64.AppImage"
TARBALL="$ROOT/packaging/tarball/out/vedit-0.1.0-linux-x86_64.tar.gz"
RUNFILE="$ROOT/packaging/tarball/out/VEdit-0.1.0-x86_64.run"
CANON=$(sha256sum "$ROOT/build/artifacts/vd-app" | awk '{print $1}')
mkdir -p "$OUT"
printf '0123456789abcdef0123456789abcdef\n' > /etc/machine-id 2>/dev/null || true
export NO_AT_BRIDGE=1
FAILS=0
REC=""
record() { REC="$REC$1\t$2\t$3\t$4\n"; if [ "$3" = FAIL ]; then FAILS=$((FAILS+1)); fi; }
Xvfb :99 -screen 0 1280x900x24 >/dev/null 2>&1 & XVFB=$!
sleep 1; export DISPLAY=:99
trap 'kill "$XVFB" 2>/dev/null' EXIT

extract() { # $1=artifact, sets PAYLOAD/APP explicitly per product
  local a=$1
  local p=/tmp/e2e-$a
  rm -rf "$p"; mkdir -p "$p"
  case $a in
    appimage) "$APPIMAGE" --appimage-extract >/dev/null 2>&1 || { APP=""; return 1; }; mv squashfs-root/* "$p"/ 2>/dev/null || true; rm -rf squashfs-root; PAYLOAD=$p; APP="$p/AppRun";;
    tarball) tar xzf "$TARBALL" -C "$p" || { APP=""; return 1; }; PAYLOAD="$p/VEdit"; APP="$PAYLOAD/bin/vedit";;
    run) sh "$RUNFILE" --noexec --target "$p" >/dev/null 2>&1 || { APP=""; return 1; }; PAYLOAD="$p/VEdit"; APP="$PAYLOAD/bin/vedit";;
  esac
  [ -x "$APP" ] || return 1
  VDBIN=$(find "$PAYLOAD" -name vd-app -type f | head -1)
}
is_elf() { [ "$(head -c4 -- "$1" 2>/dev/null | od -An -tx1 | tr -d " \n")" = "7f454c46" ]; }

selftest() {
  "$APP" --selftest all > "$OUT/$artifact/selftest-all.log" 2>&1; local rc=$?
  [ $rc -eq 0 ] && record AC-02 "selftest all" PASS "exit=0" || record AC-02 "selftest all" FAIL "exit=$rc"
  local checks failed oktrue
  checks=$(grep -c '"name":' "$OUT/$artifact/selftest-all.log")
  failed=$(grep -c '"ok": false' "$OUT/$artifact/selftest-all.log")
  oktrue=$(grep -c '"ok": true' "$OUT/$artifact/selftest-all.log")
  if [ "$failed" -eq 0 ] && [ "$checks" -eq 51 ]; then
    record AC-02 "selftest checks" PASS "checks=$checks expected=51 failed=$failed; ok_true=$oktrue (= $checks checks + 6 suites + 1 top level)"
  else
    record AC-02 "selftest checks" FAIL "checks=$checks expected=51 failed=$failed"
  fi
  "$APP" --selftest highlight --screenshot "$OUT/$artifact/screenshot.png" > "$OUT/$artifact/screenshot.log" 2>&1; rc=$?
  if [ $rc -eq 0 ] && [ -s "$OUT/$artifact/screenshot.png" ] && [ "$(head -c8 "$OUT/$artifact/screenshot.png" | od -An -tx1 | tr -d ' \n')" = 89504e470d0a1a0a ]; then
    record AC-02 "screenshot non-blank" PASS "bytes=$(stat -c%s "$OUT/$artifact/screenshot.png") sha256=$(sha256sum "$OUT/$artifact/screenshot.png" | awk '{print $1}')"
  else record AC-02 "screenshot non-blank" FAIL "exit=$rc"; fi
}

window_check() {
  "$APP" --file "$ROOT/crates/vd-app/tests/fixtures/definition/a.sv" >"$OUT/$artifact/window.log" 2>&1 & local pid=$!
  sleep 5
  local n; n=$(xwininfo -root -tree 2>/dev/null | grep -c '"VEdit"')
  kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  [ "$n" = 1 ] && record AC-02 "single toplevel" PASS "1" || record AC-02 "single toplevel" FAIL "count=$n"
}

runtime_dirs() {
  mkdir -p /tmp/rtg /tmp/rtt
  XDG_RUNTIME_DIR=/tmp/rtg TMPDIR=/tmp/rtt "$APP" --selftest list >/dev/null 2>&1 && record AC-02 "runtime_dir valid" PASS "XDG_RUNTIME_DIR" || record AC-02 "runtime_dir valid" FAIL "XDG_RUNTIME_DIR"
  env -u XDG_RUNTIME_DIR TMPDIR=/tmp/rtt "$APP" --selftest list >/dev/null 2>&1 && record AC-02 "runtime_dir TMPDIR fallback" PASS "unset->TMPDIR" || record AC-02 "runtime_dir TMPDIR fallback" FAIL "unset->TMPDIR"
  env XDG_RUNTIME_DIR=/proc/nonexistent TMPDIR=/proc/nonexistent "$APP" --selftest list >/tmp/badrt.log 2>&1; local rc=$?
  [ $rc -ne 0 ] && grep -qiE 'runtime|tmp|tmpdir' /tmp/badrt.log && record AC-02 "runtime_dir loud failure" PASS "rc=$rc" || record AC-02 "runtime_dir loud failure" FAIL "rc=$rc"
}

glibc() {
  local max="none" bad=0 v tool=none
  if command -v readelf >/dev/null 2>&1; then tool=readelf; elif command -v objdump >/dev/null 2>&1; then tool=objdump; fi
  if [ "$tool" = none ]; then record AC-01 "glibc max" FAIL "tool=none (no readelf/objdump in image)"; return; fi
  while IFS= read -r -d '' f; do
    is_elf "$f" || continue
    if [ "$tool" = readelf ]; then v=$(readelf --version-info "$f" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1); else v=$(objdump -T "$f" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1); fi
    if [ -n "$v" ] && [ "$(printf '%s\nGLIBC_2.17\n' "$v" | sort -V | tail -1)" = "$v" ] && [ "$v" != GLIBC_2.17 ]; then bad=1; fi
    [ -n "$v" ] && max=$v
  done < <(find "$PAYLOAD" -type f -print0)
  [ $bad -eq 0 ] && record AC-01 "glibc max" PASS "tool=$tool highest=$max" || record AC-01 "glibc max" FAIL "tool=$tool highest=$max"
}

for artifact in $([ "$MODE" = both ] && echo "appimage tarball" || echo "$MODE"); do
  mkdir -p "$OUT/$artifact"; REC=""
  if ! extract "$artifact"; then record AC-02 "extract" FAIL "$artifact"; printf "$REC" > "$OUT/$artifact/results.tsv"; continue; fi
  glibc
  selftest
  window_check
  runtime_dirs
  local_sha=$(sha256sum "$VDBIN" | awk '{print $1}')
  [ "$local_sha" = "$CANON" ] && record AC-19 "payload vd-app == canonical" PASS "$local_sha" || record AC-19 "payload vd-app == canonical" FAIL "payload=$local_sha canonical=$CANON"
  record AC-19 "vd-app size" PASS "$(stat -c%s "$VDBIN") bytes"
  record AC-19 "payload path" RECORD "$(basename "$(dirname "$(dirname "$VDBIN")")")"
  record AC-18 "artifact size" RECORD "$([ "$artifact" = appimage ] && stat -c%s "$APPIMAGE" || stat -c%s "$TARBALL") bytes"
  pgrep -f 'verible-verilog-ls|vd-app' >/dev/null 2>&1 && record AC-09 "no leftover processes" FAIL "found" || record AC-09 "no leftover processes" PASS "none"
  printf "$REC" > "$OUT/$artifact/results.tsv"
  echo "== $artifact =="; cat "$OUT/$artifact/results.tsv"
done
# .run contract: official launcher runs the suite and cleans its extraction dir
if [ "$MODE" = both ] || [ "$MODE" = run ]; then
  mkdir -p "$OUT/run"
  before=$(find /tmp -maxdepth 1 -name 'vedit.*' 2>/dev/null | wc -l)
  sh "$RUNFILE" --selftest all > "$OUT/run/selftest-all.log" 2>&1
  rc=$?
  after=$(find /tmp -maxdepth 1 -name 'vedit.*' 2>/dev/null | wc -l)
  [ $rc -eq 0 ] && grep -q '"ok": true' "$OUT/run/selftest-all.log" && record AC-02 ".run launcher selftest all" PASS "exit=0"; _=""
  rec="AC-02	.run launcher selftest all	PASS	exit=0
"; [ $rc -ne 0 ] && rec="AC-02	.run launcher selftest all	FAIL	exit=$rc
"
  rec2="AC-02	.run extraction cleanup	PASS	before=$before after=$after
"; [ "$before" != "$after" ] && rec2="AC-02	.run extraction cleanup	FAIL	before=$before after=$after
"
  printf "$rec$rec2" > "$OUT/run/results.tsv"
  echo "== run =="; cat "$OUT/run/results.tsv"
  [ $rc -ne 0 ] && FAILS=$((FAILS+1))
  [ "$before" != "$after" ] && FAILS=$((FAILS+1))
fi
echo "== manifest =="
sha256sum "$APPIMAGE" "$TARBALL" "$RUNFILE" build/artifacts/vd-app 2>/dev/null
[ "$FAILS" -eq 0 ] && { echo "T27E2E PASS"; exit 0; } || { echo "T27E2E FAILURES=$FAILS"; exit 1; }
