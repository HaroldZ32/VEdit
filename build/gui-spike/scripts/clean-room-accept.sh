#!/usr/bin/env bash
# Runs INSIDE the clean, offline centos:7 test container (no network).
# Mounts: /art = out/ (read-only products), /evid = evidence/ (writable logs).
set -uo pipefail

ART="${ART:-/art}"
EVID="${EVID:-/evid}"
FIX="${FIX:-$EVID/top.sv}"
LOGS="$EVID/clean-room"
SHOTS="$EVID/screenshots"
mkdir -p "$LOGS" "$SHOTS" /tmp/xdg
export XDG_RUNTIME_DIR=/tmp/xdg
export LIBGL_ALWAYS_SOFTWARE=1
export LANG=C.UTF-8
FAIL=0

say() { echo "[clean-room] $*"; }

say "host: $(cat /etc/centos-release)"
say "glibc: $(rpm -q glibc)"
say "software rendering: LIBGL_ALWAYS_SOFTWARE=$LIBGL_ALWAYS_SOFTWARE"

if rpm -q gtk3 gtksourceview3 gtksourceview4 >/dev/null 2>&1; then
  say "FAIL: test container has a system GTK/gsv package — smoke is not clean"; FAIL=1
else
  say "PASS: no system gtk3/gsv package installed (negative assertion)"
fi

Xvfb :99 -screen 0 1280x900x24 -nolisten tcp >"$LOGS/xvfb.log" 2>&1 &
XVFB_PID=$!
trap 'kill "$XVFB_PID" 2>/dev/null' EXIT
sleep 2
export DISPLAY=:99
if ! xdpyinfo >/dev/null 2>&1; then
  say "FAIL: Xvfb did not come up"; exit 1
fi
say "PASS: Xvfb :99 up"
{
  echo "command: xdpyinfo | grep -i glx   (does this X server offer GLX at all?)"
  xdpyinfo | grep -i glx || echo "GLX extension: absent"
  echo
  echo "command: glxinfo -B   (LIBGL_ALWAYS_SOFTWARE=$LIBGL_ALWAYS_SOFTWARE)"
  glxinfo -B 2>&1; echo "EXIT=$?"
} > "$LOGS/glxinfo.txt"
grep -Ei 'renderer|OpenGL version' "$LOGS/glxinfo.txt" | sed 's/^/[clean-room] glxinfo: /'

APPIMAGE=$(ls "$ART"/*.AppImage | head -1)
TARBALL=$(ls "$ART"/*.tar.gz | head -1)
say "appimage=$APPIMAGE"
say "tarball=$TARBALL"

# 1) tar.gz (FUSE-free fallback): selftest + screenshot.
rm -rf /tmp/tarroot && mkdir -p /tmp/tarroot
tar xzf "$TARBALL" -C /tmp/tarroot
TAR_DIR=$(find /tmp/tarroot -maxdepth 1 -mindepth 1 -type d | head -1)
timeout 120 "$TAR_DIR/vedit" --file "$FIX" --selftest >"$LOGS/targz-selftest.log" 2>&1
rc=$?; say "tar.gz selftest exit=$rc"; sed 's/^/[clean-room] /' "$LOGS/targz-selftest.log"
[ $rc -eq 0 ] || FAIL=1
timeout 120 "$TAR_DIR/vedit" --file "$FIX" --screenshot "$SHOTS/targz.png" >>"$LOGS/targz-selftest.log" 2>&1
rc=$?; say "tar.gz screenshot exit=$rc path=$SHOTS/targz.png"
[ $rc -eq 0 ] || FAIL=1

# 2) AppImage direct exec (may be impossible without /dev/fuse; record the result).
timeout 120 "$APPIMAGE" --file "$FIX" --selftest >"$LOGS/appimage-direct.log" 2>&1
direct_rc=$?; say "AppImage direct exec exit=$direct_rc (expected 127/1 without /dev/fuse)"
tail -2 "$LOGS/appimage-direct.log" | sed 's/^/[clean-room] /'

# 3) AppImage extract-and-run: the guaranteed CentOS 7 path (no fusermount3).
timeout 180 "$APPIMAGE" --appimage-extract-and-run --file "$FIX" --selftest >"$LOGS/appimage-selftest.log" 2>&1
rc=$?; say "AppImage --appimage-extract-and-run selftest exit=$rc"
sed 's/^/[clean-room] /' "$LOGS/appimage-selftest.log"
[ $rc -eq 0 ] || FAIL=1

# 4) Cold start: 3 measured runs. Two numbers are recorded:
#    - process-wall time of the whole extract-and-run invocation (includes the
#      AppImage extraction the runtime does before exec);
#    - WINDOW_MAPPED_MS printed by the app itself (exec -> window mapped).
: >"$LOGS/cold-start.log"
: >"$LOGS/cold-start-wall.txt"
for i in 1 2 3; do
  t0=$(date +%s%N)
  timeout 180 "$APPIMAGE" --appimage-extract-and-run --file "$FIX" \
    --screenshot "$SHOTS/appimage-$i.png" >>"$LOGS/cold-start.log" 2>&1
  rc=$?
  t1=$(date +%s%N)
  echo "run=$i exit=$rc wall_ms=$(( (t1 - t0) / 1000000 ))" | tee -a "$LOGS/cold-start-wall.txt"
done
grep -E 'WINDOW_MAPPED_MS' "$LOGS/cold-start.log" | tee "$LOGS/window-mapped.txt"

# 5) Self-containment: extract, then every dynamic dependency must resolve inside.
rm -rf /tmp/appimg && mkdir -p /tmp/appimg && cd /tmp/appimg
"$APPIMAGE" --appimage-extract >/dev/null 2>&1
EXTRACTED=/tmp/appimg/squashfs-root
LD_LIBRARY_PATH="$EXTRACTED/usr/lib" ldd "$EXTRACTED/usr/bin/vedit-gui-spike" >"$LOGS/ldd-extracted.txt" 2>&1
if grep -q 'not found' "$LOGS/ldd-extracted.txt"; then
  say "FAIL: unresolved dynamic dependency in extracted payload"; FAIL=1
else
  say "PASS: every dependency of the app resolves inside the extracted payload"
fi
resolved=$(grep -cE "$EXTRACTED" "$LOGS/ldd-extracted.txt")
hostpaths=$(grep -cE '/usr/lib64|/lib64' "$LOGS/ldd-extracted.txt" || true)
say "deps resolved from payload: $resolved; deps still taken from host: $hostpaths"

# 6) Window visibility evidence from the X server itself: launch the app
#    interactively, let it map, ask the X server, then close it.
"$TAR_DIR/vedit" --file "$FIX" --title "VEdit T5 spike" >"$LOGS/window-run.log" 2>&1 &
APP_PID=$!
sleep 3
xwininfo -root -tree >"$LOGS/xwininfo.txt" 2>&1 || true
kill "$APP_PID" 2>/dev/null || true
wait "$APP_PID" 2>/dev/null || true   # deliberate shutdown of the window-visibility probe
grep -E 'VEdit|900x700' "$LOGS/xwininfo.txt" | tee "$LOGS/window-found.txt"

# 7) APPIMAGE_EXTRACT_AND_RUN=1 variant (documented env-var form).
APPIMAGE_EXTRACT_AND_RUN=1 timeout 180 "$APPIMAGE" --file "$FIX" --selftest >"$LOGS/appimage-env-extract.log" 2>&1
rc=$?; say "APPIMAGE_EXTRACT_AND_RUN=1 selftest exit=$rc"
[ $rc -eq 0 ] || FAIL=1

say "RESULT=$([ $FAIL -eq 0 ] && echo PASS || echo FAIL)"
exit "$FAIL"
