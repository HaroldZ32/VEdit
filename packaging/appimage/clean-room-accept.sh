#!/usr/bin/env bash
# Offline acceptance for the VEdit AppImage, inside the clean-room image.
# Mounts: /art = built AppImage (read-only), /evid = evidence (writable),
#         /pack = this packaging directory (read-only).
set -uo pipefail

ART=${ART:-/art}
EVID=${EVID:-/evid}
PACK=${PACK:-/pack}
LOGS="$EVID/clean-room"
SHOTS="$EVID/screenshots"
FIXTURE="$EVID/fixture"
FAIL=0
mkdir -p "$LOGS" "$SHOTS" "$FIXTURE" /tmp/xdg
export XDG_RUNTIME_DIR=/tmp/xdg
export LIBGL_ALWAYS_SOFTWARE=1
export LANG=C.UTF-8

say() { echo "[clean-room] $*"; }

say "host: $(cat /etc/centos-release)"
say "glibc: $(rpm -q glibc)"

# 1) The clean room must not provide the GUI stack.
if rpm -q gtk3 gtksourceview3 gtksourceview4 >/dev/null 2>&1; then
  say "FAIL: test container has a system GTK/gsv package"; FAIL=1
else
  say "PASS: no system gtk3/gsv package installed (negative assertion)"
fi

# AC-16 prerequisite: the payload must bring its own CJK font — the clean room
# must not provide one.
cjk_host=$(fc-list :lang=zh 2>/dev/null | wc -l)
say "host CJK fonts available before the payload runs: $cjk_host"
if [ "$cjk_host" -eq 0 ]; then
  say "PASS: clean room has no CJK font (negative assertion)"
else
  say "FAIL: the test image already provides CJK fonts"; FAIL=1
fi

# Test-side setup: a bare container has no machine-id; a real CentOS 7 desktop
# always does. GApplication/libdbus need it, so the harness supplies one.
if [ "$(tr -d '\n' < /etc/machine-id 2>/dev/null | wc -c)" -ne 32 ]; then
  printf '0123456789abcdef0123456789abcdef\n' > /etc/machine-id
  say "test harness: wrote a 32-hex /etc/machine-id (real CentOS 7 has one)"
fi

# 2) X server.
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
  echo "command: xdpyinfo | grep -i glx"
  xdpyinfo | grep -i glx || echo "GLX extension: absent"
  echo
  echo "command: glxinfo -B (LIBGL_ALWAYS_SOFTWARE=$LIBGL_ALWAYS_SOFTWARE)"
  glxinfo -B 2>&1; echo "EXIT=$?"
} > "$LOGS/glx.txt"

APPIMAGE=$(ls "$ART"/*.AppImage | head -1)
say "appimage=$APPIMAGE"
test -f "$APPIMAGE" || { say "FAIL: no AppImage in /art"; exit 1; }

# A fixture the interactive start can open.
printf 'module fixture_top;\n  logic q;\nendmodule\n' > "$FIXTURE/top.sv"

# 3) The two documented CentOS 7 paths (no fusermount3 / no FUSE).
timeout 240 "$APPIMAGE" --appimage-extract-and-run --selftest all \
  --screenshot "$SHOTS/selftest-all.png" >"$LOGS/selftest-all.log" 2>&1
rc=$?
say "AppImage --appimage-extract-and-run --selftest all exit=$rc"
grep -E '"suite"|"status"|"checks"' "$LOGS/selftest-all.log" | tail -20
[ $rc -eq 0 ] || FAIL=1
[ -s "$SHOTS/selftest-all.png" ] || { say "FAIL: selftest-all.png missing"; FAIL=1; }
{
  echo "suite_headers=$(grep -c '\"suite\"' "$LOGS/selftest-all.log")"
  echo "checks=$(grep -c '\"name\"' "$LOGS/selftest-all.log")"
  echo "failed_checks=$(grep -c '\"ok\": false' "$LOGS/selftest-all.log" || true)"
  grep -E '"suite"|"ok":' "$LOGS/selftest-all.log" | tail -20
} > "$LOGS/selftest-all-summary.txt" 2>&1
failed_checks=$(grep -c '"ok": false' "$LOGS/selftest-all.log" || true)
say "selftest all: $(grep -c '\"name\"' "$LOGS/selftest-all.log") checks, $failed_checks failed"
[ "$failed_checks" -eq 0 ] || FAIL=1

# AC-16 / CJK: the payload must render Chinese with its bundled font. The app's
# own --screenshot is blank under this Xvfb (reported to T21), so the visual
# proof is an X-server capture of the visible UI showing a Chinese fixture.
printf 'module chinese_top;\n  // 中文注释：你好，世界\n  logic 信号;\nendmodule\n' > "$FIXTURE/chinese.sv"
timeout 240 "$APPIMAGE" --appimage-extract-and-run --selftest highlight \
  --screenshot "$SHOTS/app-selftest-highlight.png" >"$LOGS/app-screenshot.log" 2>&1
say "app --screenshot (highlight) exit=$? bytes=$(stat -c%s "$SHOTS/app-selftest-highlight.png" 2>/dev/null || echo 0)"

timeout 120 "$APPIMAGE" --appimage-extract-and-run --file "$FIXTURE/chinese.sv" \
  >"$LOGS/cjk-interactive.log" 2>&1 &
CJK_PID=$!
sleep 6
xwininfo -root -tree >"$LOGS/cjk-windows.txt" 2>&1
best=0
for id in $(grep -i 'vedit' "$LOGS/cjk-windows.txt" | awk '{print $1}'); do
  if xwd -silent -id "$id" -out "/tmp/cjk-$id.xwd" 2>/dev/null; then
    xwdtopnm "/tmp/cjk-$id.xwd" 2>/dev/null | pnmtopng >"$SHOTS/cjk-window-$id.png" 2>/dev/null
    bytes=$(stat -c%s "$SHOTS/cjk-window-$id.png" 2>/dev/null || echo 0)
    echo "window $id png_bytes=$bytes" >>"$LOGS/cjk-windows.txt"
    if [ "$bytes" -gt "$best" ]; then best=$bytes; cp "$SHOTS/cjk-window-$id.png" "$SHOTS/cjk-window.png"; fi
  fi
done
kill "$CJK_PID" 2>/dev/null || true
wait "$CJK_PID" 2>/dev/null || true
say "CJK window capture: largest window PNG bytes=$best"
if [ "$best" -gt 5000 ]; then
  say "PASS: rendered window captured to screenshots/cjk-window.png ($best bytes)"
else
  say "FAIL: no rendered window captured for the CJK fixture"; FAIL=1
fi

APPIMAGE_EXTRACT_AND_RUN=1 timeout 240 "$APPIMAGE" --selftest smoke \
  >"$LOGS/selftest-env-extract.log" 2>&1
rc=$?
say "APPIMAGE_EXTRACT_AND_RUN=1 --selftest smoke exit=$rc"
[ $rc -eq 0 ] || FAIL=1

timeout 120 "$APPIMAGE" --selftest smoke >"$LOGS/appimage-direct.log" 2>&1
direct_rc=$?
say "AppImage direct exec exit=$direct_rc (expected 127 without /dev/fuse)"
tail -2 "$LOGS/appimage-direct.log"

# 3b) Runtime directory contract (same rules as the tar.gz/.run launcher):
#     XDG_RUNTIME_DIR -> TMPDIR -> explicit failure, per-user directory name.
rm -rf "/tmp/vedit-runtime-$(id -u)"
XDG_RUNTIME_DIR=/proc/nonexistent timeout 120 "$APPIMAGE" --appimage-extract-and-run --version \
  >"$LOGS/runtime-fallback.log" 2>&1
rc=$?
fallback_ok=0
if [ $rc -eq 0 ] && grep -q 'cannot use XDG_RUNTIME_DIR=/proc/nonexistent' "$LOGS/runtime-fallback.log" \
   && grep -q 'using /tmp instead' "$LOGS/runtime-fallback.log"; then
  fallback_ok=1
fi
say "runtime fallback (XDG_RUNTIME_DIR=/proc/nonexistent): exit=$rc warning=$fallback_ok"
[ "$fallback_ok" -eq 1 ] || FAIL=1

# The AppImage runtime itself extracts into TMPDIR, so the "both unusable"
# case is asserted on the already-extracted payload (the launcher path).
rm -rf /tmp/rt-app && mkdir -p /tmp/rt-app && cd /tmp/rt-app
"$APPIMAGE" --appimage-extract >/dev/null 2>&1
XDG_RUNTIME_DIR=/proc/nonexistent TMPDIR=/proc/nonexistent \
  timeout 120 /tmp/rt-app/squashfs-root/AppRun --version \
  >"$LOGS/runtime-fatal.log" 2>&1
rc=$?
fatal_ok=0
if [ $rc -eq 1 ] \
   && grep -q "tried '/proc/nonexistent' and '/proc/nonexistent'" "$LOGS/runtime-fatal.log" \
   && grep -q 'set XDG_RUNTIME_DIR (or TMPDIR)' "$LOGS/runtime-fatal.log"; then
  fatal_ok=1
fi
say "runtime fatal (both unusable): exit=$rc both-paths-and-hint=$fatal_ok"
[ "$fatal_ok" -eq 1 ] || FAIL=1

unset XDG_RUNTIME_DIR
timeout 120 "$APPIMAGE" --appimage-extract-and-run --version \
  >"$LOGS/runtime-default.log" 2>&1
rc=$?
default_ok=0
if [ $rc -eq 0 ] && ! grep -q 'cannot use XDG_RUNTIME_DIR' "$LOGS/runtime-default.log"; then
  default_ok=1
fi
say "runtime default (no XDG_RUNTIME_DIR): exit=$rc no-warning=$default_ok"
[ "$default_ok" -eq 1 ] || FAIL=1
if [ -d "/tmp/vedit-runtime-$(id -u)" ]; then
  say "PASS: per-user runtime directory /tmp/vedit-runtime-$(id -u) was used"
else
  say "FAIL: per-user runtime directory was not created"; FAIL=1
fi

# 4) Cold start: exec -> window mapped, measured by polling the X server.
: > "$LOGS/cold-start.log"
: > "$LOGS/cold-start-wall.txt"
: > "$LOGS/cold-start-windows.txt"
for run in 1 2 3; do
  # Let the previous window disappear so the measurement cannot read a stale one.
  for _ in $(seq 1 100); do
    if ! xwininfo -root -tree 2>/dev/null | grep -qi 'vedit'; then break; fi
    sleep 0.02
  done
  start_ns=$(date +%s%N)
  timeout 120 "$APPIMAGE" --appimage-extract-and-run --file "$FIXTURE/top.sv" \
    --probe-hold-ms 4000 >>"$LOGS/cold-start.log" 2>&1 &
  app_pid=$!
  mapped_ms=""
  while kill -0 "$app_pid" 2>/dev/null; do
    if xwininfo -root -tree 2>/dev/null | grep -qi 'vedit'; then
      mapped_ms=$(( ( $(date +%s%N) - start_ns ) / 1000000 ))
      break
    fi
    sleep 0.02
  done
  kill "$app_pid" 2>/dev/null || true
  wait "$app_pid" 2>/dev/null || true
  echo "run=$run exec_to_window_ms=${mapped_ms:-timeout}" | tee -a "$LOGS/cold-start-wall.txt"
  xwininfo -root -tree 2>/dev/null | grep -i vedit | head -1 >> "$LOGS/cold-start-windows.txt"
  [ -n "$mapped_ms" ] || FAIL=1
done
xwininfo -root -tree >"$LOGS/xwininfo.txt" 2>&1 || true

# 5) immodules: files, effective env value, expanded cache, GTK acceptance.
rm -rf /tmp/appimg && mkdir -p /tmp/appimg && cd /tmp/appimg
"$APPIMAGE" --appimage-extract >/dev/null 2>&1
EXTRACTED=/tmp/appimg/squashfs-root
{
  echo "# modules in the payload"
  ls -l "$EXTRACTED"/usr/lib/gtk-3.0/3.0.0/immodules/
  sha256sum "$EXTRACTED"/usr/lib/gtk-3.0/3.0.0/immodules/*.so
  echo
  echo "# effective GTK_IM_MODULE_FILE (from AppRun xtrace)"
  sh -x "$EXTRACTED/AppRun" --version 2>&1 | grep -E 'GTK_IM_MODULE_FILE=' | head -2
} > "$LOGS/immodules-files.txt" 2>&1
IMFILE=$(sh -x "$EXTRACTED/AppRun" --version 2>&1 | sed -n 's/.*GTK_IM_MODULE_FILE=\([^ ]*\).*/\1/p' | head -1)
say "effective GTK_IM_MODULE_FILE=$IMFILE"
if [ -n "$IMFILE" ] && [ -f "$IMFILE" ]; then
  {
    echo "GTK_IM_MODULE_FILE=$IMFILE"
    echo
    echo "# expanded cache entries (paths must be inside the payload)"
    grep -A2 -E 'im-(xim|ibus)' "$IMFILE"
  } > "$LOGS/immodules-cache.txt" 2>&1
  grep -q 'im-xim' "$IMFILE" || { say "FAIL: cache has no im-xim entry"; FAIL=1; }
  grep -q 'im-ibus' "$IMFILE" || { say "FAIL: cache has no im-ibus entry"; FAIL=1; }
  grep -q "$EXTRACTED" "$IMFILE" || { say "FAIL: cache paths are not inside the payload"; FAIL=1; }
  say "PASS: immodules cache has im-xim and im-ibus with payload paths"
else
  say "FAIL: GTK_IM_MODULE_FILE not set or missing"; FAIL=1
fi
GTK_IM_MODULE=xim timeout 240 "$APPIMAGE" --appimage-extract-and-run --selftest smoke \
  >"$LOGS/immodule-xim.log" 2>&1
rc=$?
say "GTK_IM_MODULE=xim selftest smoke exit=$rc"
grep -iE 'Failed to load module|im-xim.*(error|cannot)' "$LOGS/immodule-xim.log" && FAIL=1 || true
[ $rc -eq 0 ] || FAIL=1

# Payload font resolution: fontconfig must see the bundled CJK font and nothing
# from the host. /tmp/vedit-runtime/fonts.conf was written by AppRun above.
{
  echo "# host fontconfig zh fonts:"
  fc-list :lang=zh 2>/dev/null | wc -l
  echo "# payload fontconfig zh fonts (FONTCONFIG_FILE from AppRun):"
  FONTCONFIG_FILE="/tmp/vedit-runtime-$(id -u)/fonts.conf" fc-list :lang=zh | head -5
  echo "payload_cjk_count=$(FONTCONFIG_FILE="/tmp/vedit-runtime-$(id -u)/fonts.conf" fc-list :lang=zh | wc -l)"
} > "$LOGS/fonts.txt" 2>&1
# The font list can be long; `grep -q` would exit at the first match and SIGPIPE
# `fc-list` under this script's `set -uo pipefail`, turning a present font into a
# "not visible" verdict. `grep` without `-q` reads the whole list.
if FONTCONFIG_FILE="/tmp/vedit-runtime-$(id -u)/fonts.conf" fc-list :lang=zh | grep 'wqy-zenhei' > /dev/null; then
  say "PASS: fontconfig resolves the bundled wqy-zenhei CJK font"
else
  say "FAIL: bundled CJK font is not visible to fontconfig"; FAIL=1
fi

# 6) Self-containment: every dynamic dependency resolves inside the payload.
LD_LIBRARY_PATH="$EXTRACTED/usr/lib" ldd "$EXTRACTED/usr/bin/vd-app" >"$LOGS/ldd-extracted.txt" 2>&1
if grep -q 'not found' "$LOGS/ldd-extracted.txt"; then
  say "FAIL: unresolved dynamic dependency"; FAIL=1
else
  say "PASS: every dependency of vd-app resolves inside the payload"
fi
resolved=$(grep -cE "$EXTRACTED" "$LOGS/ldd-extracted.txt")
host=$(grep -cE '/(usr/)?lib64/' "$LOGS/ldd-extracted.txt" || true)
say "deps resolved from payload: $resolved; from host glibc family: $host"

# 7) No residual processes after the selftests (verible/slang are children).
{
  echo "\$ ps -eo pid,ppid,stat,comm | grep -Ei 'verible|slang' || echo none"
  ps -eo pid,ppid,stat,comm | grep -Ei 'verible|slang' || echo "none"
  echo
  echo "\$ ps -eo stat,comm | awk '\$1 ~ /^Z/ {print}' | grep -Ei 'verible|slang|vd-app' || echo 'no zombie'"
  ps -eo stat,comm | awk '$1 ~ /^Z/ {print}' | grep -Ei 'verible|slang|vd-app' || echo "no zombie"
} > "$LOGS/processes.txt" 2>&1
if grep -Eiv '^(#|\$)' "$LOGS/processes.txt" | grep -qiE 'verible|slang'; then
  say "FAIL: verible/slang process left behind"; FAIL=1
else
  say "PASS: no verible/slang process or zombie left behind"
fi

# 8) Sizes (record item for AC-18), with the measurement method spelled out:
#    - canonical payload bytes = sum of regular-file sizes (what a user's
#      installed tree occupies, symlinks/directories excluded);
#    - du -sb also counts every directory inode (4096 B each) and symlink
#      targets, which is why it is larger;
#    - the tar stream adds archive headers, so it is larger again.
file_bytes=$(find "$EXTRACTED" -type f -printf '%s\n' | awk '{s+=$1} END {print s+0}')
link_bytes=$(find "$EXTRACTED" -type l -printf '%s\n' | awk '{s+=$1} END {print s+0}')
link_count=$(find "$EXTRACTED" -type l | wc -l)
dir_count=$(find "$EXTRACTED" -type d | wc -l)
file_count=$(find "$EXTRACTED" -type f | wc -l)
du_sb=$(du -sb "$EXTRACTED" | cut -f1)
du_apparent=$(du -sb --apparent-size "$EXTRACTED" | cut -f1)
tar_bytes=$( (cd "$EXTRACTED" && tar -cf - .) | wc -c )
{
  echo "AppImage_bytes=$(stat -c%s "$APPIMAGE")"
  sha256sum "$APPIMAGE"
  echo "payload_canonical_bytes=${file_bytes}   # method: find -type f -printf '%s' | awk sum (regular files only)"
  echo "payload_regular_file_count=${file_count}"
  echo "payload_symlink_count=${link_count} payload_symlink_target_bytes=${link_bytes}"
  echo "payload_directory_count=${dir_count}"
  echo "payload_du_sb=${du_sb}   # method: du -sb (apparent size incl. 4096 B per directory inode)"
  echo "payload_du_sb_apparent_size=${du_apparent}"
  echo "payload_tar_stream_bytes=${tar_bytes}   # method: tar -cf - . | wc -c (adds 512 B headers)"
  grep -E 'run=' "$LOGS/cold-start-wall.txt"
} | tee "$EVID/sizes.txt"

say "RESULT=$([ $FAIL -eq 0 ] && echo PASS || echo FAIL)"
exit $FAIL
