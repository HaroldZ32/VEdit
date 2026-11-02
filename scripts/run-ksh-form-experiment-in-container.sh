#!/usr/bin/env bash
# T61 experiment (runs INSIDE the pinned M0 image): can host KDE definitions leak
# into F2 (NO_STANDARD_PATHS=OFF + XDG_DATA_DIRS), and what exactly does each
# form see? All scenarios are measured with the same probe binary, driven by
# environment variables only.
#
# Simulated host:
#   /usr/share/org.kde.syntax-highlighting/syntax/*.xml   (450 upstream XMLs,
#       including SystemVerilog) + host-leak-probe.xml    (unique marker)
#   $HOME/.local/share/org.kde.syntax-highlighting/syntax/user-leak-probe.xml
# Our AppDir whitelist:
#   <appdir>/usr/share/org.kde.syntax-highlighting/syntax/verilog.xml
set -euo pipefail

WORK="${VEDIT_WORKSPACE:-/vedit-build}"
EVID="$WORK/evidence/m0-ksh-selfbuild/form-experiment-logs"
PROBE="$WORK/target/qt6-build/cmake/smoke/vedit-ksh-env-probe"
F1ROOT="$WORK/target/ksh-selfbuild/F1"
F2ROOT="$WORK/target/ksh-selfbuild/F2"
F1LIB="$F1ROOT/install/lib/x86_64-linux-gnu"
F2LIB="$F2ROOT/install/lib/x86_64-linux-gnu"
SRC="$F1ROOT/src"

mkdir -p "$EVID"

echo "== [1/6] sanity: both forms built, probe built =="
for path in "$F1LIB/libKF6SyntaxHighlighting.so.6" "$F2LIB/libKF6SyntaxHighlighting.so.6" "$PROBE" "$SRC/data/syntax/verilog.xml"; do
  [ -e "$path" ] || { echo "FATAL: missing $path (run scripts/run-ksh-form-experiment.sh)" >&2; exit 1; }
done
{
  echo "F1 lib: $(readlink -f "$F1LIB/libKF6SyntaxHighlighting.so.6")"
  echo "F2 lib: $(readlink -f "$F2LIB/libKF6SyntaxHighlighting.so.6")"
  sha256sum "$F1LIB"/libKF6SyntaxHighlighting.so.6.* "$F2LIB"/libKF6SyntaxHighlighting.so.6.*
  echo "probe: $PROBE"
} | tee "$EVID/00-inputs.txt"

echo "== [2/6] simulate a host KDE install + user data + our AppDir whitelist =="
HOST_SYNTAX="/usr/share/org.kde.syntax-highlighting/syntax"
APPROOT=/tmp/ksh-probe/appdir
APP_SHARE="$APPROOT/usr/share"
APP_SYNTAX="$APP_SHARE/org.kde.syntax-highlighting/syntax"
FAKE_HOME=/tmp/ksh-probe/home
USER_SYNTAX="$FAKE_HOME/.local/share/org.kde.syntax-highlighting/syntax"
RUNTIME=/tmp/ksh-probe/run

rm -rf "$HOST_SYNTAX" "$APPROOT" "$FAKE_HOME" "$RUNTIME"
mkdir -p "$HOST_SYNTAX" "$APP_SYNTAX" "$USER_SYNTAX" "$FAKE_HOME/.config" "$RUNTIME"
chmod 700 "$RUNTIME"

cp "$SRC"/data/syntax/*.xml "$HOST_SYNTAX"/
cat > "$HOST_SYNTAX/host-leak-probe.xml" <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<language name="HostLeakProbe" version="1" kateversion="5.0" section="Other"
          extensions="*.host-leak" author="T61 probe" license="MIT">
  <highlighting>
    <contexts><context name="Normal" attribute="Normal Text" lineEndContext="#stay"/></contexts>
    <itemDatas><itemData name="Normal Text" defStyleNum="dsNormal"/></itemDatas>
  </highlighting>
</language>
XML
cat > "$USER_SYNTAX/user-leak-probe.xml" <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<language name="UserLeakProbe" version="1" kateversion="5.0" section="Other"
          extensions="*.user-leak" author="T61 probe" license="MIT">
  <highlighting>
    <contexts><context name="Normal" attribute="Normal Text" lineEndContext="#stay"/></contexts>
    <itemDatas><itemData name="Normal Text" defStyleNum="dsNormal"/></itemDatas>
  </highlighting>
</language>
XML
cp "$SRC/data/syntax/verilog.xml" "$APP_SYNTAX/verilog.xml"
{
  echo "host KDE syntax dir : $HOST_SYNTAX ($(ls "$HOST_SYNTAX" | wc -l) xml files, incl. systemverilog.xml)"
  echo "user data syntax dir: $USER_SYNTAX (user-leak-probe.xml)"
  echo "AppDir whitelist    : $APP_SYNTAX (verilog.xml only, $(ls "$APP_SYNTAX" | wc -l) file)"
  ls "$HOST_SYNTAX" | grep -c . 
  ls "$HOST_SYNTAX" | grep -E 'systemverilog|host-leak' || true
} | tee "$EVID/01-fixtures.txt"

echo "== [3/6] baseline: the SAME probe against the DISTRO library (embedded defs) =="
set +e
env -i PATH=/usr/bin:/bin HOME="$FAKE_HOME" QT_QPA_PLATFORM=offscreen \
  XDG_RUNTIME_DIR="$RUNTIME" XDG_CONFIG_HOME="$FAKE_HOME/.config" \
  LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu \
  "$PROBE" --list > "$EVID/10-distro-baseline.log" 2>&1
status=$?
set -e
grep -E "^KSH-ENV (defs|loc|result)" "$EVID/10-distro-baseline.log" | head -5
echo "distro-baseline exit=$status (expect 0; shows the distro library's embedded set)"


: > "$EVID/02-summary.txt"
summarize() {
  local name="$1" status="$2" libdir="$3" log="$4"
  printf 'scenario=%-24s lib=%-55s exit=%s defs=%s hash=%s\n' "$name" "$libdir" "$status" \
    "$(grep -o 'defs=[0-9]*' "$log" | head -1 | cut -d= -f2)" \
    "$(grep -o 'hash=[0-9a-f]*' "$log" | head -1 | cut -d= -f2)" >> "$EVID/02-summary.txt"
}

echo "== [4/6] F1 (NO_STANDARD_PATHS=ON): repository never consults the host =="
set +e
env -i PATH=/usr/bin:/bin HOME="$FAKE_HOME" QT_QPA_PLATFORM=offscreen \
  XDG_RUNTIME_DIR="$RUNTIME" XDG_CONFIG_HOME="$FAKE_HOME/.config" \
  LD_LIBRARY_PATH="$F1LIB" \
  "$PROBE" --list --show-sources --forbid-names HostLeakProbe,UserLeakProbe,SystemVerilog,Verilog \
  > "$EVID/20-f1-default.log" 2>&1
f1_default=$?
set -e
grep -E "^KSH-ENV (defs|std_paths|result|repo forbid)" "$EVID/20-f1-default.log"
echo "F1 default exit=$f1_default (expect 0: 0 valid definitions, host/user invisible)"
summarize "f1-default" "$f1_default" "$F1LIB" "$EVID/20-f1-default.log"

set +e
env -i PATH=/usr/bin:/bin HOME="$FAKE_HOME" QT_QPA_PLATFORM=offscreen \
  XDG_RUNTIME_DIR="$RUNTIME" XDG_CONFIG_HOME="$FAKE_HOME/.config" \
  LD_LIBRARY_PATH="$F1LIB" \
  "$PROBE" --list --editor-repo --inject-editor "$APP_SHARE/org.kde.syntax-highlighting" \
  --forbid-names Verilog,HostLeakProbe,UserLeakProbe,SystemVerilog \
  --expect-editor-names Verilog --forbid-editor-names HostLeakProbe,UserLeakProbe,SystemVerilog \
  > "$EVID/21-f1-editor-inject.log" 2>&1
f1_inject=$?
set -e
grep -E "^KSH-ENV (defs|editor_repo|editor_def|result|editor (expect|forbid))" "$EVID/21-f1-editor-inject.log" | head -8
echo "F1 editor-repo const_cast injection exit=$f1_inject (expect 0)"
summarize "f1-editor-inject" "$f1_inject" "$F1LIB" "$EVID/21-f1-editor-inject.log"

echo "== [5/6] F2 (NO_STANDARD_PATHS=OFF): leakage matrix =="
set +e
env -i PATH=/usr/bin:/bin HOME="$FAKE_HOME" QT_QPA_PLATFORM=offscreen \
  XDG_RUNTIME_DIR="$RUNTIME" XDG_CONFIG_HOME="$FAKE_HOME/.config" \
  LD_LIBRARY_PATH="$F2LIB" \
  "$PROBE" --list --show-sources --expect-names HostLeakProbe,UserLeakProbe,SystemVerilog \
  > "$EVID/30-f2-default.log" 2>&1
f2_default=$?
set -e
grep -E "^KSH-ENV (defs|std_paths|result|repo expect)" "$EVID/30-f2-default.log"
echo "F2 default exit=$f2_default (expect 0: leakage CONFIRMED -- host and user defs visible)"
summarize "f2-default" "$f2_default" "$F2LIB" "$EVID/30-f2-default.log"

set +e
env -i PATH=/usr/bin:/bin HOME="$FAKE_HOME" QT_QPA_PLATFORM=offscreen \
  XDG_RUNTIME_DIR="$RUNTIME" XDG_CONFIG_HOME="$FAKE_HOME/.config" \
  LD_LIBRARY_PATH="$F2LIB" XDG_DATA_DIRS="$APP_SHARE" \
  "$PROBE" --list --show-sources --expect-names UserLeakProbe \
  --forbid-names HostLeakProbe,SystemVerilog \
  > "$EVID/31-f2-xdgdirs-only.log" 2>&1
f2_dirs=$?
set -e
grep -E "^KSH-ENV (defs|std_paths|result|repo expect|repo forbid)" "$EVID/31-f2-xdgdirs-only.log"
echo "F2 XDG_DATA_DIRS only exit=$f2_dirs (expect 0: host gone, USER data still leaks via XDG_DATA_HOME)"
summarize "f2-xdgdirs-only" "$f2_dirs" "$F2LIB" "$EVID/31-f2-xdgdirs-only.log"

set +e
env -i PATH=/usr/bin:/bin HOME="$FAKE_HOME" QT_QPA_PLATFORM=offscreen \
  XDG_RUNTIME_DIR="$RUNTIME" XDG_CONFIG_HOME="$FAKE_HOME/.config" \
  LD_LIBRARY_PATH="$F2LIB" XDG_DATA_DIRS="$APP_SHARE" XDG_DATA_HOME="$APP_SHARE" \
  "$PROBE" --list --show-sources --expect-names Verilog \
  --forbid-names HostLeakProbe,UserLeakProbe,SystemVerilog \
  > "$EVID/32-f2-isolated.log" 2>&1
f2_isolated=$?
set -e
grep -E "^KSH-ENV (defs|std_paths|result|repo expect|repo forbid)" "$EVID/32-f2-isolated.log"
echo "F2 isolated exit=$f2_isolated (expect 0: only the whitelist's Verilog is visible)"
summarize "f2-isolated" "$f2_isolated" "$F2LIB" "$EVID/32-f2-isolated.log"

set +e
env -i PATH=/usr/bin:/bin HOME="$FAKE_HOME" QT_QPA_PLATFORM=offscreen \
  XDG_RUNTIME_DIR="$RUNTIME" XDG_CONFIG_HOME="$FAKE_HOME/.config" \
  LD_LIBRARY_PATH="$F2LIB" XDG_DATA_DIRS="$APP_SHARE" XDG_DATA_HOME="$APP_SHARE" \
  "$PROBE" --list --editor-repo --expect-names Verilog \
  --forbid-names HostLeakProbe,UserLeakProbe,SystemVerilog \
  > "$EVID/33-f2-editor-isolated.log" 2>&1
f2_editor=$?
set -e
grep -E "^KSH-ENV (defs|editor_repo|editor_def|result|editor (expect|forbid))" "$EVID/33-f2-editor-isolated.log" | head -8
echo "F2 editor repo under isolation exit=$f2_editor (expect 0: KTextEditor sees the whitelist, no injection)"
summarize "f2-editor-isolated" "$f2_editor" "$F2LIB" "$EVID/33-f2-editor-isolated.log"

set +e
env -i PATH=/usr/bin:/bin HOME="$FAKE_HOME" QT_QPA_PLATFORM=offscreen \
  XDG_RUNTIME_DIR="$RUNTIME" XDG_CONFIG_HOME="$FAKE_HOME/.config" \
  LD_LIBRARY_PATH="$F2LIB" XDG_DATA_DIRS="$APP_SHARE:/usr/share" XDG_DATA_HOME="$APP_SHARE" \
  "$PROBE" --list --expect-names HostLeakProbe,Verilog \
  > "$EVID/34-f2-override.log" 2>&1
f2_override=$?
set -e
grep -E "^KSH-ENV (defs|std_paths|result|repo expect)" "$EVID/34-f2-override.log"
echo "F2 with host path re-added exit=$f2_override (expect 0: failure mode documented -- host leaks again)"
summarize "f2-override" "$f2_override" "$F2LIB" "$EVID/34-f2-override.log"

echo "== [6/6] determinism: toggle the host install, compare definition hashes =="
{
  echo "## determinism probe"
  echo "hash = sha256 over the sorted visible definition names"
  echo
  run_hash() {
    local name="$1" libdir="$2"
    shift 2
    set +e
    env -i PATH=/usr/bin:/bin HOME="$FAKE_HOME" QT_QPA_PLATFORM=offscreen \
      XDG_RUNTIME_DIR="$RUNTIME" XDG_CONFIG_HOME="$FAKE_HOME/.config" \
      LD_LIBRARY_PATH="$libdir" "$@" \
      "$PROBE" --list > "/tmp/${name}.log" 2>&1
    local status=$?
    set -e
    printf '%-26s exit=%s defs=%s hash=%s\n' "$name" "$status" \
      "$(grep -o 'defs=[0-9]*' "/tmp/${name}.log" | head -1 | cut -d= -f2)" \
      "$(grep -o 'hash=[0-9a-f]*' "/tmp/${name}.log" | head -1 | cut -d= -f2)"
  }

  echo "host install PRESENT:"
  run_hash "f1-host-present" "$F1LIB"
  run_hash "f2-isolated-host-present" "$F2LIB" XDG_DATA_DIRS="$APP_SHARE" XDG_DATA_HOME="$APP_SHARE"
  run_hash "f2-default-host-present" "$F2LIB"

  mv "$HOST_SYNTAX" "${HOST_SYNTAX}.off"
  echo
  echo "host install ABSENT:"
  run_hash "f1-host-absent" "$F1LIB"
  run_hash "f2-isolated-host-absent" "$F2LIB" XDG_DATA_DIRS="$APP_SHARE" XDG_DATA_HOME="$APP_SHARE"
  run_hash "f2-default-host-absent" "$F2LIB"
  mv "${HOST_SYNTAX}.off" "$HOST_SYNTAX"
} | tee "$EVID/40-determinism.txt"

echo
echo "== summary =="
cat "$EVID/02-summary.txt"
echo
echo "[t61] experiment complete (expected pattern: f1 determinism-independent; f2 isolated only WITH XDG_DATA_DIRS+XDG_DATA_HOME)"
