#!/usr/bin/env bash
# F2 acceptance (T64): prove the AppDir/AppImage cannot see host or user syntax
# definitions, and prove the test can actually fail (reverse + shadow controls).
#
# Positive: AppRun narrows XDG_DATA_DIRS *and* XDG_DATA_HOME to the AppDir, so a
#           simulated KDE host (/usr/share/... 402 defs + SystemVerilog + a unique
#           marker) and a user dir (~/.local/share, another marker) are invisible:
#           the visible set must equal our whitelist AND every loaded definition
#           must WIN from the AppDir's own syntax directory.
# Reverse:  run the probe DIRECTLY with XDG_DATA_DIRS="$APPDIR/usr/share:/usr/share"
#           (what a broken AppRun would do) and require the leak to reappear.
# User:     run it with XDG_DATA_HOME="$HOME/.local/share" (the user-override path
#           a broken AppRun would leave visible) and require UserLeakProbe to
#           reappear.
# Shadow:   expose a SAME-NAME, HIGHER-VERSION definition (/shadow/Tcl.xml,
#           version 999) and require (i) that it wins over our copy and (ii) that
#           the winner-path assertion reports it. KSyntaxHighlighting keeps one
#           definition per name, so this is the ONLY observable form of a shadow:
#           the name list and the count do not change.
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
APPDIR=$(cd "$APPDIR" && pwd -P)
if [ -n "$APPIMAGE" ]; then APPIMAGE=$(cd "$(dirname "$APPIMAGE")" && pwd -P)/$(basename "$APPIMAGE"); fi
WORK="$EVID/.f2-work"; rm -rf "$WORK"; mkdir -p "$WORK"
LOG="$WORK/f2-isolation.log"
ok=0; bad=0
pass() { ok=$((ok+1)); echo "CHECK PASS  $*" | tee -a "$LOG"; }
fail() { bad=$((bad+1)); echo "CHECK FAIL  $*" | tee -a "$LOG"; }

# --- simulate a KDE host, a user definition dir and a shadowing definition ---
HOST="$WORK/host/org.kde.syntax-highlighting/syntax"
USER="$WORK/home/.local/share/org.kde.syntax-highlighting/syntax"
SHADOW="$WORK/shadow/org.kde.syntax-highlighting/syntax"
mkdir -p "$HOST" "$USER" "$SHADOW"
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

# Same-name, HIGHER-VERSION negative control (R2). If this file is visible it MUST
# win over our Tcl definition; the winner-path assertion below has to report it.
# A name-only/duplicate assertion cannot: the repository keeps one definition per
# name, so the visible name set and count stay identical.
python3 - "$SHADOW" <<'PY'
import sys, os
tmpl = ('<?xml version="1.0" encoding="UTF-8"?>\n<language name="%s" version="%s" '
        'kateversion="5.0" section="Other" extensions="*.hx" mimetype="text/x-hx">'
        '<highlighting><contexts><context name="N" attribute="Normal Text" '
        'lineEndContext="#stay"/></contexts><itemDatas><itemData name="Normal Text" '
        'defStyleNum="dsNormal"/></itemDatas></highlighting></language>\n')
open(os.path.join(sys.argv[1], "Tcl.xml"), "w").write(tmpl % ("Tcl", "999"))
PY

WHITELIST=$(cat "$APPDIR/usr/share/vedit/ksh-whitelist.txt")
# The expected count is DERIVED from the shipped whitelist, never hard-coded: A3
# grew the definition set from 2 (Verilog, SystemVerilog) to 5 (VHDL/Markdown/
# Tcl added), and a hard-coded 2 would have failed on a correct package.
WANT=$(printf '%s' "$WHITELIST" | tr ',' '\n' | grep -c .)
# Canonical order for the name comparison: BOTH sides go through `LC_ALL=C sort`,
# because the whitelist file is written by python's sorted() (code-point order)
# while `sort` uses the caller's locale (zh_CN/en_US order Verilog before VHDL).
WANT_NAMES=$(printf '%s' "$WHITELIST" | tr ',' '\n' | LC_ALL=C sort | paste -sd, -)
# Inside the container the AppDir is mounted at /app; every definition that won
# in a correct package must carry this prefix.
APPDIR_SYNTAX="/app/usr/share/org.kde.syntax-highlighting/syntax"
echo "whitelist: $WHITELIST" | tee -a "$LOG"
echo "whitelist_entries: $WANT" | tee -a "$LOG"
echo "fixtures: host_xml=$(ls "$HOST" | wc -l) user_xml=$(ls "$USER" | wc -l) shadow_xml=$(ls "$SHADOW" | wc -l)" | tee -a "$LOG"

run_probe() { # <root> <xdg_data_dirs> <xdg_data_home> -> prints probe output
  local root=$1 xdg=$2 xdg_home=$3
  # Mount the PARENT of `USER` (…/org.kde.syntax-highlighting), not `$USER`
  # itself: `$USER` already ends in `syntax`, so mounting it at the
  # `org.kde.syntax-highlighting` level would place the XML one directory too
  # high and make the fixture invisible no matter what XDG_DATA_HOME says — the
  # exact reason the old "user leak" control could not fail.
  docker run --rm -v "$root:/app:ro" \
    -v "$WORK/host/org.kde.syntax-highlighting:/usr/share/org.kde.syntax-highlighting:ro" \
    -v "$WORK/home/.local/share/org.kde.syntax-highlighting:/root/.local/share/org.kde.syntax-highlighting:ro" \
    -v "$WORK/shadow:/shadow:ro" \
    -e HOME=/root -e QT_QPA_PLATFORM=offscreen \
    -e QT_PLUGIN_PATH=/app/plugins -e QT_QPA_PLATFORM_PLUGIN_PATH=/app/plugins/platforms \
    -e XDG_DATA_DIRS="$xdg" -e XDG_DATA_HOME="$xdg_home" \
    "$CENTOS" /app/ld-linux-x86-64.so.2 --library-path /app/lib /app/bin/vedit-ksh-env-probe --list 2>&1
}

probe_defs() { sed -n 's/^KSH-ENV defs=\([0-9]*\).*/\1/p' | head -1; }
# [name<TAB>winning-file] for every loaded definition (R2: the probe prints the
# winning Definition::filePath()).
probe_pairs() { sed -n 's/^KSH-ENV def name="\([^"]*\)" file="\([^"]*\)"$/\1\t\2/p'; }
probe_names() { probe_pairs | cut -f1 | LC_ALL=C sort | paste -sd, -; }
probe_file_of() { sed -n "s/^KSH-ENV def name=\"$1\" file=\"\\(.*\\)\"\$/\\1/p"; }

# Every loaded definition whose winner is NOT the AppDir's own syntax file.
winner_offenders() { # <probe output> -> "name=file" lines
  local name file
  while IFS=$'\t' read -r name file; do
    case "$file" in
      "$APPDIR_SYNTAX"/*) ;;
      *) printf '%s=%s\n' "$name" "${file:-<empty>}" ;;
    esac
  done < <(printf '%s\n' "$1" | probe_pairs)
}

check_positive() { # <label> <root>
  local label=$1 root=$2 out defs names loaded offenders missing name file
  out=$(run_probe "$root" "/app/usr/share" "/app/usr/share")
  printf '%s\n' "$out" >> "$LOG"
  defs=$(printf '%s\n' "$out" | probe_defs)
  names=$(printf '%s\n' "$out" | probe_names)
  loaded=$(printf '%s\n' "$out" | probe_pairs | wc -l)
  echo "$label winners: $(printf '%s\n' "$out" | probe_pairs | tr '\t' '=' | paste -sd' ' -)" | tee -a "$LOG"
  [ "$defs" = "$WANT" ] && pass "$label: defs=$WANT (whitelist entries)" \
    || fail "$label: defs=$defs (want $WANT = whitelist entries)"
  [ "$names" = "$WANT_NAMES" ] && pass "$label: names == whitelist [$names]" \
    || fail "$label: names [$names] != [$WANT_NAMES]"
  # (a) WINNER-PATH assertion: names alone cannot detect a shadow (the repository
  # keeps ONE definition per name), so every whitelist name must resolve to the
  # file under <AppDir>/usr/share/org.kde.syntax-highlighting/syntax/.
  missing=""
  for name in $(printf '%s' "$WHITELIST" | tr ',' ' '); do
    file=$(printf '%s\n' "$out" | probe_file_of "$name")
    case "$file" in
      "$APPDIR_SYNTAX"/*) ;;
      *) missing="$missing $name=${file:-<absent>}" ;;
    esac
  done
  if [ -z "$missing" ]; then
    pass "$label: every whitelist name wins from $APPDIR_SYNTAX"
  else
    fail "$label: whitelist definition(s) not won from the AppDir:$missing"
  fi
  offenders=$(winner_offenders "$out")
  if [ "$loaded" -eq "$WANT" ] && [ -z "$offenders" ]; then
    pass "$label: all $loaded loaded definitions win from the AppDir"
  else
    fail "$label: definition(s) outside the AppDir won: ${offenders:-<none>} (loaded=$loaded want=$WANT)"
  fi
  for marker in HostLeakProbe UserLeakProbe; do
    case ",$names," in
      *",$marker,"*) fail "$label: $marker leaked" ;;
      *) pass "$label: $marker invisible" ;;
    esac
  done
}

check_reverse() { # <label> <root>
  local label=$1 root=$2 out defs
  out=$(run_probe "$root" "/app/usr/share:/usr/share" "/app/usr/share")
  defs=$(printf '%s\n' "$out" | probe_defs)
  printf '%s\n' "$out" >> "$LOG"
  if [ -n "$defs" ] && [ "$defs" -gt "$WANT" ]; then
    pass "$label: reverse control reproduces the host leak (defs=$defs > whitelist $WANT)"
  else
    fail "$label: reverse control did NOT leak (defs=${defs:-?}, whitelist $WANT) - the test cannot detect a broken isolation"
  fi
}

check_reverse_user() { # <label> <root>
  local label=$1 root=$2 out defs user_file
  # XDG_DATA_HOME="$HOME/.local/share" is the user-override path: AppRun must pin
  # it into the AppDir, so restoring it MUST make the UserLeakProbe fixture visible
  # (today's script pinned it to /app/usr/share and the fixture was inert).
  out=$(run_probe "$root" "/app/usr/share" "/root/.local/share")
  defs=$(printf '%s\n' "$out" | probe_defs)
  user_file=$(printf '%s\n' "$out" | probe_file_of UserLeakProbe)
  printf '%s\n' "$out" >> "$LOG"
  case "$user_file" in
    /root/.local/share/*)
      if [ -n "$defs" ] && [ "$defs" -gt "$WANT" ]; then
        pass "$label: XDG_DATA_HOME=\$HOME/.local/share makes the user definition visible (defs=$defs > $WANT; UserLeakProbe=$user_file)"
      else
        fail "$label: UserLeakProbe visible but defs=${defs:-?} is not > whitelist $WANT"
      fi
      ;;
    *)
      fail "$label: user-level reverse probe did NOT see UserLeakProbe (defs=${defs:-?}, file='${user_file:-<absent>}') - the user-override path is not exercised"
      ;;
  esac
}

check_shadow_control() { # <label> <root>
  local label=$1 root=$2 out tcl_file offenders
  # The ONLY observable shadow: a same-name definition with a HIGHER version. It
  # replaces our Tcl in the repository's name-keyed map, so `defs` and the name
  # list stay identical while the winning file changes. This is the negative
  # control the previous script lacked.
  out=$(run_probe "$root" "/app/usr/share:/shadow" "/app/usr/share")
  printf '%s\n' "$out" >> "$LOG"
  tcl_file=$(printf '%s\n' "$out" | probe_file_of Tcl)
  if [ "$tcl_file" = "/shadow/org.kde.syntax-highlighting/syntax/Tcl.xml" ]; then
    pass "$label: higher-version same-name host Tcl.xml wins when visible ($tcl_file)"
  else
    fail "$label: shadow fixture did not win (Tcl -> '${tcl_file:-<absent>}') - the probe cannot prove a shadow"
  fi
  offenders=$(winner_offenders "$out")
  case "$offenders" in
    *Tcl=/shadow/*) pass "$label: the winner-path assertion REPORTS the shadowed Tcl (offenders: $(printf '%s' "$offenders" | paste -sd' ' -))" ;;
    *) fail "$label: winner-path assertion missed the shadowed Tcl (offenders='${offenders:-<none>}')" ;;
  esac
  # A name/duplicate check cannot see this: prove the name set is unchanged.
  local names defs
  names=$(printf '%s\n' "$out" | probe_names)
  defs=$(printf '%s\n' "$out" | probe_defs)
  [ "$names" = "$WANT_NAMES" ] && pass "$label: shadow leaves the name set unchanged (defs=$defs) - why names alone are insufficient" \
    || fail "$label: shadow changed the name set (names [$names] != [$WANT_NAMES])"
}

echo "== positive (AppDir) ==" | tee -a "$LOG"
check_positive appdir "$APPDIR"
echo "== reverse control: host leak (AppDir) ==" | tee -a "$LOG"
check_reverse appdir-reverse "$APPDIR"
echo "== reverse control: user-level leak (AppDir) ==" | tee -a "$LOG"
check_reverse_user appdir-reverse-user "$APPDIR"
echo "== negative control: same-name higher-version shadow (AppDir) ==" | tee -a "$LOG"
check_shadow_control appdir-shadow "$APPDIR"

if [ -n "$APPIMAGE" ]; then
  [ -f "$APPIMAGE" ] || { fail "AppImage missing: $APPIMAGE"; }
  if [ -f "$APPIMAGE" ]; then
    mkdir -p "$WORK/img"
    ( cd "$WORK/img" && APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGE" --appimage-extract >/dev/null 2>&1 ) \
      && mv "$WORK/img/squashfs-root" "$WORK/img/root" || fail "AppImage extraction failed"
    echo "== positive (AppImage) ==" | tee -a "$LOG"
    check_positive appimage "$WORK/img/root"
    echo "== reverse control: host leak (AppImage) ==" | tee -a "$LOG"
    check_reverse appimage-reverse "$WORK/img/root"
    echo "== reverse control: user-level leak (AppImage) ==" | tee -a "$LOG"
    check_reverse_user appimage-reverse-user "$WORK/img/root"
    echo "== negative control: same-name higher-version shadow (AppImage) ==" | tee -a "$LOG"
    check_shadow_control appimage-shadow "$WORK/img/root"
  fi
fi

echo "== summary: pass=$ok fail=$bad ==" | tee -a "$LOG"
[ "$KEEP" -eq 1 ] || rm -rf "$WORK/host" "$WORK/home"
if [ "$bad" -ne 0 ]; then exit 1; fi
echo "F2-ISOLATION PASS"
