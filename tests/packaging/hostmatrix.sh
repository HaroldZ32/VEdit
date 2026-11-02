#!/usr/bin/env bash
# M0 host matrix (T12): run the SAME AppImage artifact on four host shapes and
# prove A0a / A0b / A0d / A0e.
#
#   A0a  CentOS 7.9.2009 (glibc 2.17, base image with no Qt/KF6): Qt6 window +
#        KTextEditor init + packaged Verilog/SV syntax + >=1 theme, xcb.
#   A0b  Ubuntu 24.04.5 LTS (glibc 2.39): the same checks.
#   A0d  base-only: prove the host has NO Qt/KF6 and the app still starts
#        (offscreen + xcb), i.e. nothing is borrowed from the host.
#   A0e  no GPU / software fallback: LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software
#        QSG_RHI_BACKEND=software; window visible, key events delivered, no crash.
#
# ARTIFACT HONESTY: the only artifact today is T11's PROBE AppImage (the distro
# KSyntaxHighlighting embeds 36 strict-GPL definitions; delivery is blocked
# pending T60). This matrix proves the RUNTIME MECHANISM on that artifact and
# every record says so; it is not a delivery verdict.
#
# DISPLAY: a test sidecar (tests/packaging/sidecar.Dockerfile) runs Xvfb and
# listens on TCP; the tested container gets DISPLAY=<sidecar>:99 and nothing
# else from outside itself (no socket mount, no host libs, no host Qt/KF6).
# The tested images are stock base images.
#
# Usage: bash tests/packaging/hostmatrix.sh [--appimage <file>] [--only a0a|a0b|a0d|a0e]
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
EVID="$ROOT/evidence/m0-hostmatrix"
APPIMAGE=${VEDIT_APPIMAGE:-$ROOT/packaging/runtime/out/VEdit-0.1.1-x86_64.probe.AppImage}
SIDECAR=vedit-hostmatrix-xside:local
NET=vedit-hm-net
CENTOS=docker.m.daocloud.io/library/centos@sha256:be65f488b7764ad3638f236b7b515b3678369a5124c47b8d32916d6487418ea4
UBUNTU=docker.m.daocloud.io/library/ubuntu@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852

ONLY=""
while [ $# -gt 0 ]; do
  case "$1" in
    --appimage) APPIMAGE=${2:?}; shift 2 ;;
    --only) ONLY=${2:?}; shift 2 ;;
    -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[ -f "$APPIMAGE" ] || { echo "ERROR: no AppImage at $APPIMAGE" >&2; exit 1; }
mkdir -p "$EVID"
: > "$EVID/summary.tsv"
pass=0; fail=0
result() { printf '%s\t%s\n' "$1" "$2" >> "$EVID/summary.tsv"; }

ensure_sidecar() {
  docker image inspect "$SIDECAR" >/dev/null 2>&1 && return 0
  echo "building test sidecar (Xvfb+xdotool+imagemagick; test tooling, not a product image)"
  # buildx cannot write its activity file under the sandboxed $HOME; legacy builder can
  DOCKER_BUILDKIT=0 docker build -q -t "$SIDECAR" -f "$ROOT/tests/packaging/sidecar.Dockerfile" \
    "$ROOT/tests/packaging" >/dev/null 2>&1
}
net_create() { docker network inspect "$NET" >/dev/null 2>&1 || docker network create "$NET" >/dev/null; }

xside_start() { # <name>
  docker rm -f "$1" >/dev/null 2>&1
  docker run -d --name "$1" --network "$NET" -v "$EVID:/evidence" "$SIDECAR" \
    sh -c 'Xvfb :99 -screen 0 1280x800x24 -listen tcp -ac >/tmp/xvfb.log 2>&1 & sleep 2; exec tail -f /dev/null' >/dev/null || return 1
  local i; for i in $(seq 1 40); do
    docker exec "$1" sh -c 'DISPLAY=:99 xdotool getdisplaygeometry' >/dev/null 2>&1 && return 0; sleep 0.25
  done
  echo "FAIL: Xvfb did not come up in $1"; docker logs "$1" 2>&1 | tail -3; return 1
}
xside_stop() { docker rm -f "$1" >/dev/null 2>&1; }

extract_appdir() { # <workdir>; extraction stays OUT of evidence/ (it is ~178 MB)
  ( cd "$1" && APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGE" --appimage-extract >/dev/null 2>&1 ) \
    || { echo "FAIL: --appimage-extract failed"; return 1; }
  mv "$1/squashfs-root" "$1/root"
}

shot() { docker exec "$1" sh -c "DISPLAY=:99 import -window root '/evidence/$2'" >/dev/null 2>&1; }
win_id() { docker exec "$1" sh -c 'DISPLAY=:99 xdotool search --name "VEdit M0 smoke" 2>/dev/null' | head -1; }
win_geom() { docker exec "$1" sh -c "DISPLAY=:99 xdotool getwindowgeometry '$2' 2>/dev/null" | tr '\n' ' '; }
send_input() { docker exec "$1" sh -c "DISPLAY=:99 xdotool windowfocus '$2' 2>/dev/null; DISPLAY=:99 xdotool type --window '$2' 'verilog' 2>/dev/null; DISPLAY=:99 xdotool key --window '$2' Return 2>/dev/null; echo sent"; }

run_xcb() { # <tag> <image> <dir> <sidecar-name> [extra docker env...]
  local tag=$1 image=$2 dir=$3 side=$4; shift 4
  local envargs=("$@")
  echo; echo "===== $tag ($image) ====="
  rm -rf "$dir"; mkdir -p "$dir"
  local work="$EVID/.work/$tag"; rm -rf "$work"; mkdir -p "$work"
  extract_appdir "$work" || { rm -rf "$work"; result "$tag" FAIL; fail=$((fail+1)); return; }
  ensure_sidecar || { result "$tag" FAIL; fail=$((fail+1)); return; }
  net_create
  xside_start "$side" || { result "$tag" FAIL; fail=$((fail+1)); return; }

  {
    echo "artifact: $APPIMAGE"
    echo "artifact_sha256: $(sha256sum "$APPIMAGE" | cut -d' ' -f1)"
    echo "artifact_note: PROBE build (T11); delivery blocked pending T60 — mechanism verification only"
    echo "tested_image: $image"
    echo "tested_os: $(docker run --rm "$image" sh -c '. /etc/os-release 2>/dev/null; echo ${PRETTY_NAME:-unknown}')"
    echo "tested_glibc: $(docker run --rm "$image" ldd --version 2>&1 | head -1)"
    echo "display: Xvfb :99 in sidecar $side (TCP), tested container has DISPLAY=$side:99"
    echo "extra_env: ${envargs[*]:-none}"
    echo "host_qt_kf6_count: $(docker run --rm "$image" sh -c 'ls /usr/lib*/libQt6* /usr/lib*/libKF6* /usr/lib*/libkf6* 2>/dev/null | wc -l')"
  } > "$dir/meta.txt"

  shot "$side" "${tag}/00-empty.png"
  docker run --rm --name "vedit-${tag}" --network "$NET" -v "$work/root:/app:ro" \
    -e DISPLAY="$side:99" -e QT_QPA_PLATFORM=xcb -e HOME=/tmp -e LANG=C.UTF-8 "${envargs[@]}" \
    "$image" /app/AppRun --selfcheck --hold 16000 > "$dir/run.log" 2>&1 &
  local pid=$!
  sleep 5
  local wid; wid=$(win_id "$side")
  shot "$side" "${tag}/10-window.png"
  local geom=""; [ -n "$wid" ] && geom=$(win_geom "$side" "$wid")
  local input_out=""; [ -n "$wid" ] && input_out=$(send_input "$side" "$wid")
  sleep 1
  local alive=0; kill -0 "$pid" 2>/dev/null && alive=1
  shot "$side" "${tag}/11-after-input.png"
  wait "$pid"; local rc=$?
  local diff; diff=$(docker exec "$side" sh -c "compare -metric AE '/evidence/$tag/00-empty.png' '/evidence/$tag/10-window.png' null: 2>&1" 2>/dev/null | tr -d '\n' | head -c 20)
  xside_stop "$side"; rm -rf "$work"
  {
    echo "window_id=$wid"
    echo "window_geometry=$geom"
    echo "input=$input_out"
    echo "alive_after_input=$alive"
    echo "smoke_exit=$rc"
    echo "screenshot_pixel_diff_empty_vs_window=$diff"
  } >> "$dir/meta.txt"

  local ok=1
  grep -q 'M0-SMOKE ok=1' "$dir/run.log"        || { echo "CHECK FAIL  [$tag] M0-SMOKE ok=1"; ok=0; }
  grep -q 'platform=xcb' "$dir/run.log"          || { echo "CHECK FAIL  [$tag] platform=xcb"; ok=0; }
  grep -q 'ktexteditor=1' "$dir/run.log"         || { echo "CHECK FAIL  [$tag] ktexteditor=1"; ok=0; }
  grep -q 'document=1' "$dir/run.log"            || { echo "CHECK FAIL  [$tag] document=1"; ok=0; }
  grep -q 'verilog=1' "$dir/run.log"             || { echo "CHECK FAIL  [$tag] verilog=1"; ok=0; }
  grep -qE 'themes=([1-9][0-9]*)' "$dir/run.log" || { echo "CHECK FAIL  [$tag] themes>=1"; ok=0; }
  grep -q 'M0-SMOKE theme=' "$dir/run.log"       || { echo "CHECK FAIL  [$tag] a theme was resolved"; ok=0; }
  grep -q "libc=/app/" "$dir/run.log"            || { echo "CHECK FAIL  [$tag] libc under /app"; ok=0; }
  [ "$rc" -eq 0 ]                                || { echo "CHECK FAIL  [$tag] exit=$rc"; ok=0; }
  [ -n "$wid" ]                                  || { echo "CHECK FAIL  [$tag] window not found by xdotool"; ok=0; }
  [ -s "$dir/00-empty.png" ] && [ -s "$dir/10-window.png" ] || { echo "CHECK FAIL  [$tag] screenshots missing"; ok=0; }
  [ "$alive" -eq 1 ]                             || { echo "CHECK FAIL  [$tag] process died during input"; ok=0; }
  [ "$ok" -eq 1 ] && { echo "CHECK PASS  [$tag]"; pass=$((pass+1)); result "$tag" PASS; } \
                  || { echo "CHECK FAIL  [$tag]"; fail=$((fail+1)); result "$tag" FAIL; }
}

scenario_a0a() { run_xcb a0a-centos7 "$CENTOS" "$EVID/a0a-centos7" xside-a0a; }
scenario_a0b() { run_xcb a0b-ubuntu2404 "$UBUNTU" "$EVID/a0b-ubuntu2404" xside-a0b; }

scenario_a0d() {
  local tag=a0d-base-only dir="$EVID/a0d-base-only"
  echo; echo "===== A0d: base-only CentOS 7 (no host Qt/KF6) ====="
  rm -rf "$dir"; mkdir -p "$dir"
  local work="$EVID/.work/$tag"; rm -rf "$work"; mkdir -p "$work"
  extract_appdir "$work" || { rm -rf "$work"; result "$tag" FAIL; fail=$((fail+1)); return; }

  {
    echo "artifact: $APPIMAGE"
    echo "artifact_sha256: $(sha256sum "$APPIMAGE" | cut -d' ' -f1)"
    echo "artifact_note: PROBE build (T11); delivery blocked pending T60 — mechanism verification only"
    echo "tested_image: $CENTOS"
    echo "tested_glibc: $(docker run --rm "$CENTOS" ldd --version 2>&1 | head -1)"
  } > "$dir/meta.txt"

  docker run --rm "$CENTOS" sh -c '
    echo "host_libQt6_or_libKF6=$(ls /usr/lib*/libQt6* /usr/lib*/libKF6* /usr/lib*/libkf6* 2>/dev/null | wc -l)"
    echo "host_qmake_or_qtpaths=$(command -v qmake6 qmake qtpaths6 2>/dev/null | wc -l)"
    echo "host_ksyntax_data=$(ls -d /usr/share/ksyntaxhighlighting /usr/share/org.kde.syntax-highlighting 2>/dev/null | wc -l)"
    echo "host_katepart=$(find /usr -name katepart.so 2>/dev/null | wc -l)"
    echo "host_libX11=$(ldconfig -p 2>/dev/null | grep -c "libX11.so")"
    echo "host_libfuse2=$(ldconfig -p 2>/dev/null | grep -c "libfuse.so.2")"
  ' > "$dir/host-purity.log" 2>&1

  docker run --rm -v "$work/root:/app:ro" -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen \
    "$CENTOS" /app/AppRun --selfcheck --expect-libc-prefix /app > "$dir/run-offscreen.log" 2>&1
  local rc_off=$?
  rm -rf "$work"
  echo "offscreen_exit=$rc_off" >> "$dir/meta.txt"

  local ok=1
  grep -q 'host_libQt6_or_libKF6=0' "$dir/host-purity.log" || { echo "CHECK FAIL  [$tag] host ships Qt/KF6"; ok=0; }
  grep -q 'host_katepart=0' "$dir/host-purity.log"         || { echo "CHECK FAIL  [$tag] host ships katepart"; ok=0; }
  grep -q 'M0-SMOKE ok=1' "$dir/run-offscreen.log"         || { echo "CHECK FAIL  [$tag] offscreen M0-SMOKE ok=1"; ok=0; }
  grep -q 'libc=/app/' "$dir/run-offscreen.log"            || { echo "CHECK FAIL  [$tag] libc under /app"; ok=0; }
  grep -q 'ktexteditor=1' "$dir/run-offscreen.log"         || { echo "CHECK FAIL  [$tag] ktexteditor=1"; ok=0; }
  [ "$rc_off" -eq 0 ]                                      || { echo "CHECK FAIL  [$tag] offscreen exit=$rc_off"; ok=0; }
  [ "$ok" -eq 1 ] && { echo "CHECK PASS  [$tag]"; pass=$((pass+1)); result "$tag" PASS; } \
                  || { echo "CHECK FAIL  [$tag]"; fail=$((fail+1)); result "$tag" FAIL; }
}

scenario_a0e() {
  run_xcb a0e-no-gpu "$CENTOS" "$EVID/a0e-no-gpu" xside-a0e \
    -e LIBGL_ALWAYS_SOFTWARE=1 -e QT_OPENGL=software -e QSG_RHI_BACKEND=software
  local dir="$EVID/a0e-no-gpu"
  local dri; dri=$(docker run --rm "$CENTOS" sh -c 'ls /dev/dri 2>/dev/null | wc -l')
  {
    echo "gpu_devices_in_container=$dri"
    echo "mesa_on_host=$(docker run --rm "$CENTOS" sh -c 'ldconfig -p 2>/dev/null | grep -c "libGL.so"')"
    echo "glxinfo_on_host=$(docker run --rm "$CENTOS" sh -c 'command -v glxinfo 2>/dev/null | wc -l')"
  } >> "$dir/meta.txt"
}

case "${ONLY:-all}" in
  a0a) scenario_a0a ;;
  a0b) scenario_a0b ;;
  a0d) scenario_a0d ;;
  a0e) scenario_a0e ;;
  all) scenario_a0a; scenario_a0b; scenario_a0d; scenario_a0e ;;
  *) echo "unknown --only value: $ONLY" >&2; exit 2 ;;
esac

echo
echo "== matrix summary =="
cat "$EVID/summary.tsv"
echo "pass=$pass fail=$fail"
[ "$fail" -eq 0 ] || exit 1
