#!/usr/bin/env bash
# R1 / F1 regression: "switch the interface theme, restart, it is still there".
#
# The defect this locks down (evidence/ui-mosaic/review/VERDICT.md F1): the View
# submenu and the toolbar button applied a product theme WITHOUT writing
# settings.toml, so after the user had once used Preferences (which does write it)
# the next start preferred the OLD settings value over the newer session archive,
# and reopening Preferences replayed that old value.
#
# Every case runs the SHIPPED binary offscreen in the pinned image with
# VEDIT_UI_PROBE=1; the script reads the probe line ("theme=" and "status-theme=").
# The switch itself is replayed through the REAL View-menu QAction
# (VEDIT_UI_THEME_SWITCH=<id> -> action->trigger()), not through a private path.
#
# Usage: bash evidence/ui-mosaic/fix-r1/run-f1-regression.sh
# Exit:  0 = every assertion held, 1 = at least one failed (the log names it).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
BIN_REL="target/qt6-build/src/app/vedit-app"
OUT="$ROOT/evidence/ui-mosaic/fix-r1"
STATE="$OUT/state"
LOG="$OUT/logs"
mkdir -p "$STATE" "$LOG"
rm -rf "$OUT/state"
mkdir -p "$STATE"

ok=0
bad=0
pass() { ok=$((ok + 1)); printf 'PASS  %s\n' "$*"; }
fail() { bad=$((bad + 1)); printf 'FAIL  %s\n' "$*"; }

# One offscreen run in the pinned image. $1 = case state dir, $2 = log file,
# $3 = optional theme id replayed through the View-menu action.
run_app() {
  local state=$1 log=$2 switch=${3:-}
  local envs=(-e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp/xdg-r1
              -e VEDIT_UI_PROBE=1)
  if [ -n "$switch" ]; then
    envs+=(-e "VEDIT_UI_THEME_SWITCH=$switch")
  fi
  docker run --rm -v "$ROOT":/vedit-build -v "$state":/state -w /vedit-build \
    "${envs[@]}" "$IMG" bash -lc \
    "mkdir -p /tmp/xdg-r1 && chmod 700 /tmp/xdg-r1 && \
     ./$BIN_REL --state-dir /state --hold 3500" > "$LOG/$log" 2>&1
  local code=$?
  [ "$code" -eq 0 ] || fail "$log: binary exit=$code (see evidence/ui-mosaic/fix-r1/logs/$log)"
  grep -q '\[ui-probe\] theme=' "$LOG/$log" \
    || fail "$log: no [ui-probe] theme= line (probe never fired)"
}

probe_theme() { grep -o 'theme=[a-z]*' "$LOG/$1" | head -1 | cut -d= -f2; }
probe_status() { grep -o 'status-theme=[^ ]*.*' "$LOG/$1" | head -1; }
json_ui_theme() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("uiTheme",""))' "$1"; }
settings_ui_theme() { grep -E '^[[:space:]]*("?uiTheme"?)[[:space:]]*=' "$1" | head -1; }

seed_session() { # seed_session <dir> <archive-uiTheme>
  mkdir -p "$1"
  cat > "$1/session.json" <<JSON
{
  "version": 2,
  "project": "",
  "theme": "Breeze Dark",
  "uiTheme": "$2",
  "panels": []
}
JSON
}

# --- 1. last writer wins: menu switch overrides BOTH stored sources ----------
# settings says bohe, the archive says shenye; the user then switches to qingkong.
# The switch must write settings.toml (the Preferences key), update the archive,
# and the NEXT start must read qingkong back.
S1="$STATE/case1-switch"
seed_session "$S1" shenye
printf 'uiTheme = "bohe"\n"ui.language" = "zh_CN"\n' > "$S1/settings.toml"
run_app "$S1" case1-switch.log qingkong

[ "$(probe_theme case1-switch.log)" = "qingkong" ] \
  && pass "case1: the live switch applied qingkong ($(probe_status case1-switch.log))" \
  || fail "case1: live theme is '$(probe_theme case1-switch.log)', expected qingkong"
grep -qx 'uiTheme = "qingkong"' "$S1/settings.toml" \
  && pass "case1: settings.toml now carries uiTheme = \"qingkong\" (the Preferences key)" \
  || fail "case1: settings.toml was not rewritten to qingkong: $(settings_ui_theme "$S1/settings.toml")"
[ "$(json_ui_theme "$S1/session.json")" = "qingkong" ] \
  && pass "case1: session archive uiTheme was synced to qingkong" \
  || fail "case1: session archive uiTheme is '$(json_ui_theme "$S1/session.json")', expected qingkong"

# Restart WITHOUT a switch: the persisted value must be read back (settings wins
# over the now-stale archive, but both say qingkong anyway).
run_app "$S1" case1-restart.log
[ "$(probe_theme case1-restart.log)" = "qingkong" ] \
  && pass "case1: restart read the switch back (theme=qingkong)" \
  || fail "case1: restart read '$(probe_theme case1-restart.log)', expected qingkong"
case "$(probe_status case1-restart.log)" in
  *"晴空蓝"*) pass "case1: status-theme names the stored Sky Blue theme (zh_CN UI)" ;;
  *) fail "case1: status-theme did not name 晴空蓝: $(probe_status case1-restart.log)" ;;
esac

# --- 2. precedence is unchanged when nobody switched: settings > archive -----
S2="$STATE/case2-precedence"
seed_session "$S2" shenye
printf 'uiTheme = "bohe"\n' > "$S2/settings.toml"
run_app "$S2" case2-precedence.log
[ "$(probe_theme case2-precedence.log)" = "bohe" ] \
  && pass "case2: with no switch, settings.toml still wins over the archive (bohe)" \
  || fail "case2: expected bohe, got '$(probe_theme case2-precedence.log)'"
run_app "$S2" case2-restart.log
[ "$(probe_theme case2-restart.log)" = "bohe" ] \
  && pass "case2: the second start also reads bohe (settings stays authoritative)" \
  || fail "case2: second start read '$(probe_theme case2-restart.log)', expected bohe"

# --- 3. the archive is still the second source when settings is absent -------
S3="$STATE/case3-archive-only"
seed_session "$S3" shenye
run_app "$S3" case3-archive-only.log
[ "$(probe_theme case3-archive-only.log)" = "shenye" ] \
  && pass "case3: an absent settings.toml falls back to the archived shenye" \
  || fail "case3: expected shenye, got '$(probe_theme case3-archive-only.log)'"

# --- 4. M2: an invalid settings id is UNSET, not the default; archive used ---
S4="$STATE/case4-invalid-settings"
seed_session "$S4" shenye
printf 'uiTheme = "no-such-theme"\n"ui.language" = "zh_CN"\n' > "$S4/settings.toml"
run_app "$S4" case4-invalid-settings.log
[ "$(probe_theme case4-invalid-settings.log)" = "shenye" ] \
  && pass "case4: invalid settings id fell through to the archive (shenye), not yunwu" \
  || fail "case4: expected shenye, got '$(probe_theme case4-invalid-settings.log)'"
# Language-agnostic: the id and the source token stay Latin in the zh_CN message.
grep -Eq '\[ui-probe\] diagnostic=.*no-such-theme.*settings\.toml' \
  "$LOG/case4-invalid-settings.log" \
  && pass "case4: the fallback is a VISIBLE diagnostic (not silent)" \
  || fail "case4: no visible diagnostic for the invalid settings id"

# --- 5. M2: invalid id + no archive -> the contract default, still diagnosed -
S5="$STATE/case5-invalid-no-archive"
mkdir -p "$S5"
printf 'uiTheme = "no-such-theme"\n' > "$S5/settings.toml"
run_app "$S5" case5-invalid-no-archive.log
[ "$(probe_theme case5-invalid-no-archive.log)" = "yunwu" ] \
  && pass "case5: invalid settings id with no archive lands on the contract default (yunwu)" \
  || fail "case5: expected yunwu, got '$(probe_theme case5-invalid-no-archive.log)'"
grep -Eq '\[ui-probe\] diagnostic=.*no-such-theme.*settings\.toml' \
  "$LOG/case5-invalid-no-archive.log" \
  && pass "case5: the default fallback is still diagnosed" \
  || fail "case5: no visible diagnostic for the invalid settings id"

# --- 6. the stored spellings the two entry points write are identical --------
# Preferences writes through SettingsClient (toml crate: `uiTheme = "id"`); the
# menu path installs the SAME client from main.cpp. The file this run produced
# must therefore already be in the store's own spelling — the next Preferences
# save and the next startup read the one key.
S6="$STATE/case6-spelling"
seed_session "$S6" yunwu
printf '"ui.language" = "zh_CN"\n' > "$S6/settings.toml"
run_app "$S6" case6-spelling.log xinghuang
grep -qx 'uiTheme = "xinghuang"' "$S6/settings.toml" \
  && pass "case6: the menu switch wrote the store's exact spelling (uiTheme = \"xinghuang\")" \
  || fail "case6: unexpected settings spelling: $(settings_ui_theme "$S6/settings.toml")"
if python3 - "$S6/settings.toml" <<'PY'
import sys
import tomllib
data = tomllib.load(open(sys.argv[1], "rb"))
assert data.get("uiTheme") == "xinghuang", data
assert data.get("ui.language") == "zh_CN", data
PY
then
  pass "case6: settings.toml is still valid TOML with the OTHER key preserved"
else
  fail "case6: settings.toml is not valid TOML / lost the other key"
fi

printf '\n== summary: pass=%d fail=%d ==\n' "$ok" "$bad"
[ "$bad" -eq 0 ] && echo "F1-REGRESSION PASS" || echo "F1-REGRESSION FAIL"
exit $([ "$bad" -eq 0 ] && echo 0 || echo 1)
