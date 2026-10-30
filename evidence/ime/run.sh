#!/bin/sh
# AC-16 probes, executed INSIDE the vedit-ime image (no GTK installed there).
#
# Mounts: /art = packaging/tarball/out (read-only product), /work = repo (evidence),
#         /probes = evidence/ime/out/bin (compiled probes).
# Tiers:
#   T1  which GtkIMContext GTK really creates (payload immodules)
#   T2  real IME: ibus + libpinyin converts "nihao" -> 你好 into a GtkTextView
#   T3  X11 Unicode injection into the same GTK text view (no IME conversion)
set -u

ART=${ART:-/art}
WORK=${WORK:-/work}
PROBES=${PROBES:-/probes}
EVID="$WORK/evidence/ime"
LOG="$EVID/logs"
SHOTS="$EVID/shots"
mkdir -p "$LOG" "$SHOTS" /tmp/payload /tmp/xdg

FAIL=0
say() { echo "[ime] $*"; }
check() { # check <name> <rc>
  if [ "$2" -eq 0 ]; then say "PASS $1"; echo "RESULT $1=PASS" >> "$LOG/99-results.txt"
  else say "FAIL $1"; echo "RESULT $1=FAIL" >> "$LOG/99-results.txt"; FAIL=$((FAIL + 1)); fi
}
: > "$LOG/99-results.txt"

export DISPLAY=:99
export XDG_RUNTIME_DIR=/tmp/xdg
export LANG=en_US.UTF-8
export NO_AT_BRIDGE=1
Xvfb :99 -screen 0 1280x900x24 -nolisten tcp >"$LOG/10-xvfb.log" 2>&1 &
XVFB_PID=$!
trap 'kill "$XVFB_PID" 2>/dev/null; [ -n "${IBUS_PID:-}" ] && kill "$IBUS_PID" 2>/dev/null' EXIT
sleep 2
xdpyinfo >/dev/null 2>&1 || { say "Xvfb failed"; exit 1; }
say "Xvfb :99 up"

say "== extract the packaged payload (the GTK that must be used) =="
tar xzf "$ART"/vedit-0.1.0-linux-x86_64.tar.gz -C /tmp/payload
HERE=/tmp/payload/VEdit
RUNTIME=/tmp/vedit-runtime
mkdir -p "$RUNTIME"
sed "s|@APPDIR@|$HERE|g" "$HERE/etc/loaders.cache.in" > "$RUNTIME/loaders.cache"
sed "s|@APPDIR@|$HERE|g" "$HERE/etc/immodules.cache.in" > "$RUNTIME/immodules.cache"
sed "s|@APPDIR@|$HERE|g" "$HERE/etc/fonts.conf.in" > "$RUNTIME/fonts.conf"

export LD_LIBRARY_PATH="$HERE/lib"
export GDK_PIXBUF_MODULE_FILE="$RUNTIME/loaders.cache"
export GTK_IM_MODULE_FILE="$RUNTIME/immodules.cache"
export GSETTINGS_SCHEMA_DIR="$HERE/share/glib-2.0/schemas"
export GTK_DATA_PREFIX="$HERE"
export GTK_EXE_PREFIX="$HERE"
export GTK_PATH="$HERE/lib/gtk-3.0"
export FONTCONFIG_FILE="$RUNTIME/fonts.conf"
export XDG_DATA_DIRS="$HERE/share:/usr/share"
export VEDIT_GSV_DATADIR="$HERE/share/gtksourceview-4"

{
  echo "# which GTK / immodules are actually used"
  echo "payload=$HERE"
  echo "GTK_IM_MODULE_FILE=$GTK_IM_MODULE_FILE"
  echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
  echo
  echo "## immodule files in the payload"
  ls -l "$HERE/lib/gtk-3.0/3.0.0/immodules/"
  echo
  echo "## payload immodule cache"
  cat "$HERE/etc/immodules.cache.in"
  echo
  echo "## ldd of the probe (GTK must resolve inside the payload)"
  ldd "$PROBES/im-probe"
} > "$LOG/11-payload-gtk.txt" 2>&1
grep -q "$HERE/lib/libgtk-3.so.0" "$LOG/11-payload-gtk.txt"
check "payload_gtk_used" $?

# --------------------------------------------------------------------- Tier 1
say "== Tier 1: which GtkIMContext does GTK create? =="
GTK_IM_MODULE=xim "$PROBES/im-probe" >"$LOG/30-tier1-xim.txt" 2>&1
check "tier1_probe_runs_xim" $?
GTK_IM_MODULE=ibus "$PROBES/im-probe" >"$LOG/31-tier1-ibus.txt" 2>&1
check "tier1_probe_runs_ibus" $?
xim_id=$(sed -n 's/^IM_CONTEXT_ID=//p' "$LOG/30-tier1-xim.txt")
ibus_id=$(sed -n 's/^IM_CONTEXT_ID=//p' "$LOG/31-tier1-ibus.txt")
say "Tier 1 context ids: xim='$xim_id' ibus='$ibus_id'"
[ "$xim_id" = "xim" ] && check "tier1_context_is_xim" 0 || check "tier1_context_is_xim" 1
[ "$ibus_id" = "ibus" ] && check "tier1_context_is_ibus" 0 || check "tier1_context_is_ibus" 1

# --------------------------------------------------------------------- Tier 2
say "== Tier 2: real IME (ibus + libpinyin) commits Chinese =="
eval "$(dbus-launch --sh-syntax)"
echo "DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:-<unset>}" > "$LOG/40-ibus-env.txt"
ibus-daemon -drx >"$LOG/41-ibus-daemon.log" 2>&1 &
IBUS_PID=$!
sleep 3
{
  echo "## ibus version"; ibus version 2>&1 || true
  echo "## ibus engines"; ibus list-engine 2>&1 | head -20 || true
  echo "## current engine"; ibus engine 2>&1 || true
} >> "$LOG/40-ibus-env.txt"
# The engine id is the first field of the indented "  <id> - <name>" line.
engine=$(ibus list-engine 2>/dev/null | awk '/pinyin/{print $1; exit}')
[ -n "$engine" ] || engine="libpinyin"
say "selecting ibus engine '$engine'"
ibus engine "$engine" >>"$LOG/40-ibus-env.txt" 2>&1
echo "engine_requested=$engine" >> "$LOG/40-ibus-env.txt"
echo "engine_active=$(ibus engine 2>&1)" >> "$LOG/40-ibus-env.txt"
say "active ibus engine: $(ibus engine 2>&1)"

GTK_IM_MODULE=ibus XMODIFIERS=@im=ibus "$PROBES/input-probe" --seconds 12 \
  >"$LOG/42-tier2-typed.txt" 2>&1 &
PROBE_PID=$!
i=0
while [ "$i" -lt 100 ]; do
  grep -q WINDOW_READY "$LOG/42-tier2-typed.txt" 2>/dev/null && break
  sleep 0.1
  i=$((i + 1))
done
win=$(xdotool search --sync --name "VEdit IME probe" 2>/dev/null | head -1)
say "probe window id='$win'"
if [ -n "$win" ]; then
  xdotool windowfocus --sync "$win" 2>>"$LOG/42-tier2-typed.txt" || true
  sleep 0.5
  xdotool type --delay 150 "nihao" >>"$LOG/42-tier2-typed.txt" 2>&1 || true
  sleep 0.8
  import -window root "$SHOTS/tier2-before-commit.png" 2>>"$LOG/42-tier2-typed.txt" || true
  xdotool key space >>"$LOG/42-tier2-typed.txt" 2>&1 || true
  sleep 1
fi
# Capture while the window is still mapped: the probe exits after --seconds, and a
# screenshot taken after wait would be an empty root window.
import -window root "$SHOTS/tier2-after-commit.png" 2>>"$LOG/42-tier2-typed.txt" || true
sleep 0.3
wait "$PROBE_PID" 2>/dev/null
cat "$LOG/42-tier2-typed.txt" >> "$LOG/42-tier2-full.txt" 2>/dev/null || true
if grep -q "你好" "$LOG/42-tier2-typed.txt"; then
  check "tier2_ime_commits_chinese" 0
else
  check "tier2_ime_commits_chinese" 1
fi

# --------------------------------------------------------------------- Tier 3
say "== Tier 3: X11 Unicode injection (no IME conversion) =="
GTK_IM_MODULE=simple "$PROBES/input-probe" --seconds 10 >"$LOG/50-tier3-typed.txt" 2>&1 &
PROBE3=$!
i=0
while [ "$i" -lt 100 ]; do
  grep -q WINDOW_READY "$LOG/50-tier3-typed.txt" 2>/dev/null && break
  sleep 0.1
  i=$((i + 1))
done
win3=$(xdotool search --sync --name "VEdit IME probe" 2>/dev/null | head -1)
if [ -n "$win3" ]; then
  xdotool windowfocus --sync "$win3" 2>>"$LOG/50-tier3-typed.txt" || true
  sleep 0.3
  xdotool type --clearmodifiers --delay 80 "中文注释：你好，世界" >>"$LOG/50-tier3-typed.txt" 2>&1 || true
  sleep 0.5
  import -window root "$SHOTS/tier3-unicode.png" 2>>"$LOG/50-tier3-typed.txt" || true
fi
wait "$PROBE3" 2>/dev/null
if grep -q "中文注释：你好，世界" "$LOG/50-tier3-typed.txt"; then
  check "tier3_unicode_injection" 0
else
  check "tier3_unicode_injection" 1
fi

{
  echo "# environment actually used"
  echo "GTK_IM_MODULE_FILE=$GTK_IM_MODULE_FILE"
  echo "XMODIFIERS=${XMODIFIERS:-<unset>}"
  echo "DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:-<unset>}"
  echo "engine=${engine:-<none>} current=$(ibus engine 2>&1)"
} > "$LOG/60-env-actual.txt"

say "IME RESULT: failures=$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
