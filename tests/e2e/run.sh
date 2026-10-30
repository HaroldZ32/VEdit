#!/usr/bin/env bash
# E2E acceptance harness (T27). Run inside a clean, network-less centos:7 image.
# It consumes FROZEN artifacts only; it does not build them.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/tests/e2e/out"
MODE=${1:-both}
APPIMAGE="$ROOT/packaging/appimage/out/VEdit-0.1.0-x86_64.AppImage"
TARBALL="$ROOT/packaging/tarball/out/vedit-0.1.0-linux-x86_64.tar.gz"
RUNFILE="$ROOT/packaging/tarball/out/VEdit-0.1.0-x86_64.run"
mkdir -p "$OUT"
printf '0123456789abcdef0123456789abcdef\n' > /etc/machine-id 2>/dev/null || true

fail=0
record() { printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" >> "$OUT/$artifact/results.tsv"; [ "$3" = FAIL ] && fail=1; }
check() { local ac=$1 name=$2; shift 2; if "$@"; then record "$ac" "$name" PASS "$(echo "$@" )"; else record "$ac" "$name" FAIL "$(echo "$@" )"; fi; }

start_xvfb() { Xvfb :99 -screen 0 1280x900x24 >/dev/null 2>&1 & XVFB=$!; export DISPLAY=:99 NO_AT_BRIDGE=1; sleep 1; }
stop_xvfb() { kill "$XVFB" 2>/dev/null || true; }

prepare() {
  artifact=$1; mkdir -p "$OUT/$artifact"; : > "$OUT/$artifact/results.tsv"
  PAYLOAD="/tmp/e2e-$artifact"; rm -rf "$PAYLOAD"; mkdir -p "$PAYLOAD"
  case $artifact in
    appimage) "$APPIMAGE" --appimage-extract >/dev/null 2>&1 || { record AC-02 extract FAIL appimage; return 1; }; mv squashfs-root/* "$PAYLOAD"/ 2>/dev/null || true; rm -rf squashfs-root;;
    tarball) tar xzf "$TARBALL" -C "$PAYLOAD" || { record AC-02 extract FAIL tarball; return 1; }; PAYLOAD="$PAYLOAD/VEdit";;
  esac
  APP="$PAYLOAD/AppRun"
  [ -x "$APP" ] || APP="$PAYLOAD/usr/bin/vd-app"
  [ -x "$APP" ] || APP="$PAYLOAD/bin/vd-app"
}

runtime_dir_checks() {
  mkdir -p /tmp/rt-good /tmp/rt-tmp
  XDG_RUNTIME_DIR=/tmp/rt-good TMPDIR=/tmp/rt-tmp "$APP" --selftest list >/dev/null 2>&1 && record AC-02 runtime_dir_valid PASS "XDG_RUNTIME_DIR=/tmp/rt-good" || record AC-02 runtime_dir_valid FAIL "valid XDG_RUNTIME_DIR"
  env -u XDG_RUNTIME_DIR TMPDIR=/tmp/rt-tmp "$APP" --selftest list >/dev/null 2>&1 && record AC-02 runtime_dir_tmpdir_fallback PASS "unset -> TMPDIR" || record AC-02 runtime_dir_tmpdir_fallback FAIL "unset -> TMPDIR"
  env XDG_RUNTIME_DIR=/nonexistent/unwritable TMPDIR=/nonexistent/unwritable "$APP" --selftest list >/tmp/rt-bad.log 2>&1
  rc=$?
  [ "$rc" -ne 0 ] && grep -qiE "runtime|tmp" /tmp/rt-bad.log && record AC-02 runtime_dir_loud_failure PASS "rc=$rc" || record AC-02 runtime_dir_loud_failure FAIL "rc=$rc (expected loud error)"
}

glibc_scan() {
  local max bad=0
  while IFS= read -r -d '' elf; do
    file "$elf" | grep -q ELF || continue
    local v
    v=$(readelf --version-info "$elf" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1)
    if [ -n "$v" ] && [ "$(printf '%s\n%s\n' "$v" GLIBC_2.17 | sort -V | tail -1)" = "$v" ] && [ "$v" != GLIBC_2.17 ]; then bad=1; fi
    [ -n "$v" ] && max=$v
  done < <(find "$PAYLOAD" -type f -print0)
  [ "$bad" -eq 0 ] && record AC-01 glibc_max PASS "highest=${max:-none} <= 2.17" || record AC-01 glibc_max FAIL "highest=${max:-?} > 2.17"
}

selftest_all() {
  start_xvfb
  "$APP" --selftest all > "$OUT/$artifact/selftest-all.log" 2>&1; local rc=$?
  [ $rc -eq 0 ] && record AC-02 selftest_all PASS "exit=0" || record AC-02 selftest_all FAIL "exit=$rc"
  grep -q '"ok": false' "$OUT/$artifact/selftest-all.log" && record AC-02 selftest_checks FAIL "json has failed checks" || record AC-02 selftest_checks PASS "no failed checks"
  "$APP" --selftest highlight --screenshot "$OUT/$artifact/screenshot.png" > "$OUT/$artifact/screenshot.log" 2>&1
  local rc2=$?
  if [ $rc2 -eq 0 ] && [ -s "$OUT/$artifact/screenshot.png" ] && [ "$(head -c8 "$OUT/$artifact/screenshot.png" | od -An -tx1 | tr -d ' \n')" = 89504e470d0a1a0a ]; then
    record AC-02 screenshot PASS "bytes=$(stat -c%s "$OUT/$artifact/screenshot.png") sha256=$(sha256sum "$OUT/$artifact/screenshot.png" | awk '{print $1}')"
  else record AC-02 screenshot FAIL "exit=$rc2"; fi
  xwininfo -root -tree 2>/dev/null | grep -c '"VEdit"' | grep -qx 1 && record AC-02 single_toplevel PASS "1" || record AC-02 single_toplevel FAIL "not 1"
  pgrep -f 'verible-verilog-ls|vd-app' >/dev/null 2>&1 && record AC-09 no_processes FAIL "leftover" || record AC-09 no_processes PASS "none"
  stop_xvfb
}

consistency() {
  local app_sha; app_sha=$(sha256sum "$(find "$PAYLOAD" -name vd-app -type f | head -1)" 2>/dev/null | awk '{print $1}')
  local built; built=$(sha256sum "$ROOT/build/artifacts/vd-app" | awk '{print $1}')
  [ "$app_sha" = "$built" ] && record AC-19 same_binary PASS "$app_sha" || record AC-19 same_binary FAIL "payload=${app_sha:-?} built=$built"
  record AC-18 size RECORD "$(stat -c%s "$1" 2>/dev/null) bytes"
}

for goal in $MODE; do
  case $goal in appimage) targets="appimage";; tarball) targets="tarball";; both) targets="appimage tarball";; *) echo "usage: $0 [appimage|tarball|both]" >&2; exit 2;; esac
  for artifact in $targets; do
    (cd /tmp && prepare "$artifact") || continue
    runtime_dir_checks
    glibc_scan
    selftest_all
    consistency
    record AC-19 frozen_artifact RECORD "$(sha256sum "$APPIMAGE" "$TARBALL" "$RUNFILE" 2>/dev/null | awk '{print $1" "$2}' | tr '\n' ';')"
  done
done
for artifact in appimage tarball; do
  [ -d "$OUT/$artifact" ] && echo "-- $artifact --" && column -t -s$'\t' "$OUT/$artifact/results.tsv" 2>/dev/null || cat "$OUT/$artifact/results.tsv" 2>/dev/null
done
[ "$fail" -eq 0 ] && { echo "E2E: all machine checks PASS"; exit 0; } || { echo "E2E: FAILURES (see results.tsv)"; exit 1; }
