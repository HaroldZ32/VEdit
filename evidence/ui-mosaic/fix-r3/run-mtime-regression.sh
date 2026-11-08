#!/usr/bin/env bash
# R3 / A1 regression: "last writer wins" across the TWO storage sources of the
# interface theme (settings.toml `uiTheme` vs session archive `uiTheme`).
#
# The rule under test (captain's R3 verdict):
#   * both files carry a VALID and DIFFERENT id -> the file with the NEWER mtime wins;
#   * equal mtimes -> settings wins (the persistent preference);
#   * missing file / unreadable mtime / invalid value -> fixed precedence
#     valid settings > valid archive > default yunwu, and an invalid value still
#     produces a visible diagnostic (M2 unchanged).
#
# Isolation: every case gets its OWN state directory under
# evidence/ui-mosaic/fix-r3/state/, its OWN docker container name (r3-probe-<case>)
# and its OWN XDG_RUNTIME_DIR. The probes run with QT_QPA_PLATFORM=offscreen, so no
# X11 display/socket is used at all — this script never touches :97/:98/:99 or
# /tmp/.X11-unix, and never reads/writes evidence/ui-mosaic/review-r1/**.
#
# Usage: bash evidence/ui-mosaic/fix-r3/run-mtime-regression.sh
# Exit:  0 = every case held, 1 = at least one failed.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
BIN_REL="target/qt6-build/src/app/vedit-app"
OUT="$ROOT/evidence/ui-mosaic/fix-r3"
STATE="$OUT/state"
LOG="$OUT/logs"
mkdir -p "$STATE" "$LOG"
rm -rf "$STATE"
mkdir -p "$STATE"

ok=0
bad=0
pass() { ok=$((ok + 1)); printf 'PASS  %s\n' "$*"; }
fail() { bad=$((bad + 1)); printf 'FAIL  %s\n' "$*"; }

write_settings() { # <dir> <uiTheme>
  printf 'uiTheme = "%s"\n' "$2" > "$1/settings.toml"
}
write_archive() { # <dir> <uiTheme> <syntaxTheme>
  cat > "$1/session.json" <<JSON
{
  "version": 2,
  "project": "",
  "theme": "$3",
  "uiTheme": "$2",
  "panels": []
}
JSON
}
stamp() { # <file> <touch -d argument>
  touch -d "$2" "$1"
  printf '%s' "$(stat -c '%y' "$1")"
}
epoch() { stat -c '%Y' "$1"; }

run_case() { # <case> <dir>
  local case_id=$1 dir=$2
  docker run --rm --name "r3-probe-$case_id" \
    -v "$ROOT":/vedit-build -v "$dir":/state -w /vedit-build \
    -e QT_QPA_PLATFORM=offscreen -e "XDG_RUNTIME_DIR=/tmp/xdg-r3-$case_id" \
    -e VEDIT_UI_PROBE=1 \
    "$IMG" bash -lc \
    "mkdir -p /tmp/xdg-r3-$case_id && chmod 700 /tmp/xdg-r3-$case_id && \
     ./$BIN_REL --state-dir /state --hold 3500" > "$LOG/case-$case_id.log" 2>&1
  local code=$?
  [ "$code" -eq 0 ] || fail "case-$case_id: binary exit=$code (see logs/case-$case_id.log)"
  grep -q '\[ui-probe\] theme=' "$LOG/case-$case_id.log" \
    || fail "case-$case_id: no [ui-probe] theme= line"
}

probe_theme() { grep -o 'theme=[a-z]*' "$LOG/case-$1.log" | head -1 | cut -d= -f2; }
probe_status() { grep -o 'status-theme=[^ ]*.*' "$LOG/case-$1.log" | head -1; }
show_mtimes() { # <case> <settings mtime> <archive mtime>
  printf '        settings.toml mtime = %s\n' "$2"
  printf '        session.json  mtime = %s\n' "$3"
}

echo "== R3/A1: mtime arbitration between settings.toml and session.json =="
echo "   container names: r3-probe-<case>; QPA=offscreen (no X display is used)"

# --- a) settings OLD + archive NEW -> the archive's newer value wins ----------
A="$STATE/case-a"; mkdir -p "$A"
write_settings "$A" bohe
sm_a=$(stamp "$A/settings.toml" '2026-11-08 21:30:00')
write_archive "$A" shenye "Breeze Dark"
am_a=$(stamp "$A/session.json" '2026-11-08 22:30:00')
[ "$(epoch "$A/settings.toml")" -lt "$(epoch "$A/session.json")" ] \
  && pass "case-a: setup — settings ($(epoch "$A/settings.toml")) is older than archive ($(epoch "$A/session.json"))" \
  || fail "case-a: setup did not make settings older than the archive"
run_case a "$A"
show_mtimes a "$sm_a" "$am_a"
printf '        observed: theme=%s  %s\n' "$(probe_theme a)" "$(probe_status a)"
[ "$(probe_theme a)" = "shenye" ] \
  && pass "case-a: startup took the NEWER archive value (shenye), not the older settings (bohe)" \
  || fail "case-a: startup took '$(probe_theme a)', expected shenye"

# --- b) settings NEW + archive OLD -> the settings value wins -----------------
B="$STATE/case-b"; mkdir -p "$B"
write_archive "$B" shenye "Breeze Dark"
am_b=$(stamp "$B/session.json" '2026-11-08 21:30:00')
write_settings "$B" qingkong
sm_b=$(stamp "$B/settings.toml" '2026-11-08 22:30:00')
[ "$(epoch "$B/settings.toml")" -gt "$(epoch "$B/session.json")" ] \
  && pass "case-b: setup — settings ($(epoch "$B/settings.toml")) is newer than archive ($(epoch "$B/session.json"))" \
  || fail "case-b: setup did not make settings newer than the archive"
run_case b "$B"
show_mtimes b "$sm_b" "$am_b"
printf '        observed: theme=%s  %s\n' "$(probe_theme b)" "$(probe_status b)"
[ "$(probe_theme b)" = "qingkong" ] \
  && pass "case-b: startup took the NEWER settings value (qingkong), not the older archive (shenye)" \
  || fail "case-b: startup took '$(probe_theme b)', expected qingkong"

# --- c) identical mtimes -> settings wins (the persistent preference) --------
C="$STATE/case-c"; mkdir -p "$C"
write_settings "$C" bohe
write_archive "$C" shenye "Breeze Dark"
same='2026-11-08 22:00:00'
sm_c=$(stamp "$C/settings.toml" "$same")
am_c=$(stamp "$C/session.json" "$same")
[ "$(epoch "$C/settings.toml")" -eq "$(epoch "$C/session.json")" ] \
  && pass "case-c: setup — both files carry the same mtime ($(epoch "$C/settings.toml"))" \
  || fail "case-c: setup did not make the mtimes equal"
run_case c "$C"
show_mtimes c "$sm_c" "$am_c"
printf '        observed: theme=%s  %s\n' "$(probe_theme c)" "$(probe_status c)"
[ "$(probe_theme c)" = "bohe" ] \
  && pass "case-c: equal mtimes fall back to settings (bohe), not the archive (shenye)" \
  || fail "case-c: startup took '$(probe_theme c)', expected bohe"

# --- d1) no archive -> settings ---------------------------------------------
D1="$STATE/case-d1"; mkdir -p "$D1"
write_settings "$D1" qingkong
sm_d1=$(stamp "$D1/settings.toml" '2026-11-08 22:10:00')
run_case d1 "$D1"
printf '        no session.json; settings mtime = %s\n' "$sm_d1"
printf '        observed: theme=%s  %s\n' "$(probe_theme d1)" "$(probe_status d1)"
[ "$(probe_theme d1)" = "qingkong" ] \
  && pass "case-d1: with no archive, settings supplies the theme (qingkong)" \
  || fail "case-d1: startup took '$(probe_theme d1)', expected qingkong"

# --- d2) no settings -> archive ---------------------------------------------
D2="$STATE/case-d2"; mkdir -p "$D2"
write_archive "$D2" shenye "Breeze Dark"
am_d2=$(stamp "$D2/session.json" '2026-11-08 22:10:00')
run_case d2 "$D2"
printf '        no settings.toml; archive mtime = %s\n' "$am_d2"
printf '        observed: theme=%s  %s\n' "$(probe_theme d2)" "$(probe_status d2)"
[ "$(probe_theme d2)" = "shenye" ] \
  && pass "case-d2: with no settings, the archive supplies the theme (shenye)" \
  || fail "case-d2: startup took '$(probe_theme d2)', expected shenye"

# --- d3) BOTH invalid -> contract default yunwu + visible diagnostics -------
D3="$STATE/case-d3"; mkdir -p "$D3"
write_settings "$D3" no-such-theme
sm_d3=$(stamp "$D3/settings.toml" '2026-11-08 22:05:00')
write_archive "$D3" also-bogus "Breeze Dark"
am_d3=$(stamp "$D3/session.json" '2026-11-08 22:06:00')
run_case d3 "$D3"
show_mtimes d3 "$sm_d3" "$am_d3"
printf '        observed: theme=%s  %s\n' "$(probe_theme d3)" "$(probe_status d3)"
grep -o '\[ui-probe\] diagnostic=.*' "$LOG/case-d3.log" | sed 's/^/        /'
[ "$(probe_theme d3)" = "yunwu" ] \
  && pass "case-d3: two invalid ids fall back to the contract default (yunwu)" \
  || fail "case-d3: startup took '$(probe_theme d3)', expected yunwu"
grep -Eq '\[ui-probe\] diagnostic=.*no-such-theme.*settings\.toml' "$LOG/case-d3.log" \
  && pass "case-d3: the invalid settings id is reported (M2 unchanged)" \
  || fail "case-d3: no visible diagnostic for the invalid settings id"
grep -Eq '\[ui-probe\] diagnostic=.*also-bogus.*session archive' "$LOG/case-d3.log" \
  && pass "case-d3: the invalid archive id is reported too" \
  || fail "case-d3: no visible diagnostic for the invalid archive id"

printf '\n== summary: pass=%d fail=%d ==\n' "$ok" "$bad"
[ "$bad" -eq 0 ] && echo "R3-MTIME PASS" || echo "R3-MTIME FAIL"
exit $([ "$bad" -eq 0 ] && echo 0 || echo 1)
