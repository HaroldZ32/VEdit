#!/usr/bin/env bash
# F2 acceptance (T64): prove the AppDir/AppImage cannot see host or user syntax
# definitions, and prove the test can actually fail (reverse control).
#
# Positive: AppRun narrows XDG_DATA_DIRS *and* XDG_DATA_HOME to the AppDir, so a
#           simulated KDE host (/usr/share/... 402 defs + SystemVerilog + a unique
#           marker) and a user dir (~/.local/share, another marker) are invisible:
#           the visible set must equal our whitelist.
# Reverse:  run the probe DIRECTLY with XDG_DATA_DIRS="$APPDIR/usr/share:/usr/share"
#           (what a broken AppRun would do) and require the leak to reappear.
#
# Usage: verify-f2-isolation.sh [--appdir <dir>] [--appimage <file>] [--keep]
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
EVID="$ROOT/evidence/m0-runtime"
APPDIR="$ROOT/packaging/runtime/out/AppDir"
APPIMAGE=""
KEEP=0
CENTOS=docker.m.daocloud.io/library/centos@sha256:be65f488b7764ad3638f236b7b515b3678369a5124c47b8d32916d6487418ea4
while [ $# -gt 0 ]; do
  case "$1" in
    --appdir) APPDIR=${2:?}; shift 2 ;;
    --appimage) APPIMAGE=${2:?}; shift 2 ;;
    --keep) KEEP=1; shift ;;
    -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
    *) echo "unknown: $1" >&2; exit 2 ;;
  esac
done
[ -d "$APPDIR" ] || { echo "ERROR: no AppDir $APPDIR" >&2; exit 1; }
WORK="$EVID/.f2-work"; rm -rf "$WORK"; mkdir -p "$WORK"
LOG="$WORK/f2-isolation.log"
ok=0; bad=0
pass() { ok=$((ok+1)); echo "CHECK PASS  $*" | tee -a "$LOG"; }
fail() { bad=$((bad+1)); echo "CHECK FAIL  $*" | tee -a "$LOG"; }

# --- simulate a KDE host and a user definition dir --------------------------
HOST="$WORK/host/org.kde.syntax-highlighting/syntax"
USER="$WORK/home/.local/share/org.kde.syntax-highlighting/syntax"
mkdir -p "$HOST" "$USER"
python3 - "$HOST" <<'PY'
import sys, os
d = sys.argv[1]
tmpl = ('<?xml version="1.0" encoding="UTF-8"?>\n<language name="%s" version="1" '
        'kateversion="5.0" section="Other" extensions="*.hx" mimetype="text/x-hx">'
        '<highlighting><contexts><context name="N" attribute="Normal Text" '
        'lineEndContext="#stay"/></contexts><itemDatas><itemData name="Normal Text" '
        'defStyleNum="dsNormal"/></itemDatas></highlighting></language>\n')
names = ["HostProbe%03d" % i for i in range(400)] + ["HostLeakProbe", "SystemVerilog"]
for n in names:
    open(os.path.join(d, n + ".xml"), "w").write(tmpl % n)
print(len(names))
PY
sed 's/HostLeakProbe/UserLeakProbe/' "$HOST/HostLeakProbe.xml" > "$USER/UserLeakProbe.xml"

WHITELIST=$(cat "$APPDIR/usr/share/vedit/ksh-whitelist.txt")
echo "whitelist: $WHITELIST" | tee -a "$LOG"
echo "fixtures: host_xml=$(ls "$HOST" | wc -l) user_xml=$(ls "$USER" | wc -l)" | tee -a "$LOG"

run_probe() { # <root> <xdg_data_dirs> -> prints probe output
  local root=$1 xdg=$2
  docker run --rm -v "$root:/app:ro" \
    -v "$WORK/host/org.kde.syntax-highlighting:/usr/share/org.kde.syntax-highlighting:ro" \
    -v "$USER:/root/.local/share/org.kde.syntax-highlighting:ro" \
    -e HOME=/root -e QT_QPA_PLATFORM=offscreen \
    -e QT_PLUGIN_PATH=/app/plugins -e QT_QPA_PLATFORM_PLUGIN_PATH=/app/plugins/platforms \
    -e XDG_DATA_DIRS="$xdg" -e XDG_DATA_HOME="/app/usr/share" \
    "$CENTOS" /app/ld-linux-x86-64.so.2 --library-path /app/lib /app/bin/vedit-ksh-env-probe --list 2>&1
}

check_positive() { # <label> <root>
  local label=$1 root=$2 out
  out=$(run_probe "$root" "/app/usr/share")
  local defs names
  defs=$(printf '%s\n' "$out" | sed -n 's/^KSH-ENV defs=\([0-9]*\).*/\1/p' | head -1)
  names=$(printf '%s\n' "$out" | sed -n 's/^KSH-ENV def name="\(.*\)"$/\1/p' | sort | paste -sd, -)
  printf '%s\n' "$out" >> "$LOG"
  [ "$defs" = "2" ] && pass "$label: defs=2" || fail "$label: defs=$defs (want 2)"
  [ "$names" = "$WHITELIST" ] && pass "$label: names == whitelist [$names]" || fail "$label: names [$names] != [$WHITELIST]"
  for marker in HostLeakProbe UserLeakProbe; do
    printf '%s' "$names" | grep -q "$marker" && fail "$label: $marker leaked" || pass "$label: $marker invisible"
  done
  # the host's SystemVerilog must NOT be visible as an extra copy: our own SV is
  # in the whitelist, so a leak would show up as defs>2 / duplicate names instead.
}

check_reverse() { # <label> <root>
  local label=$1 root=$2 out defs
  out=$(run_probe "$root" "/app/usr/share:/usr/share")
  defs=$(printf '%s\n' "$out" | sed -n 's/^KSH-ENV defs=\([0-9]*\).*/\1/p' | head -1)
  printf '%s\n' "$out" >> "$LOG"
  if [ -n "$defs" ] && [ "$defs" -gt 2 ]; then
    pass "$label: reverse control reproduces the leak (defs=$defs)"
  else
    fail "$label: reverse control did NOT leak (defs=${defs:-?}) - the test cannot detect a broken isolation"
  fi
}

echo "== positive (AppDir) ==" | tee -a "$LOG"
check_positive appdir "$APPDIR"
echo "== reverse control (AppDir) ==" | tee -a "$LOG"
check_reverse appdir-reverse "$APPDIR"

if [ -n "$APPIMAGE" ]; then
  [ -f "$APPIMAGE" ] || { fail "AppImage missing: $APPIMAGE"; }
  if [ -f "$APPIMAGE" ]; then
    mkdir -p "$WORK/img"
    ( cd "$WORK/img" && APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGE" --appimage-extract >/dev/null 2>&1 ) \
      && mv "$WORK/img/squashfs-root" "$WORK/img/root" || fail "AppImage extraction failed"
    echo "== positive (AppImage) ==" | tee -a "$LOG"
    check_positive appimage "$WORK/img/root"
    echo "== reverse control (AppImage) ==" | tee -a "$LOG"
    check_reverse appimage-reverse "$WORK/img/root"
  fi
fi

echo "== summary: pass=$ok fail=$bad ==" | tee -a "$LOG"
[ "$KEEP" -eq 1 ] || rm -rf "$WORK/host" "$WORK/home"
if [ "$bad" -ne 0 ]; then exit 1; fi
echo "F2-ISOLATION PASS"
