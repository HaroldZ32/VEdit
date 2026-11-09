#!/usr/bin/env bash
# R2/A1: startup arbitration between settings.toml `uiTheme` and the session
# archive `uiTheme`. REQUIREMENTS A1 (R2 edit): "settings.toml 与会话存档冲突时
# 最后写入者生效，不得出现旧值回弹".
#
# Four cases, each in its own state dir with EXPLICIT mtimes:
#   case0-archive-only      settings absent, archive bohe      -> bohe must win
#   caseA-settings-old      settings xinghuang (old), archive bohe (new)
#                           -> A1 says the LAST writer (archive, bohe) wins
#   caseB-settings-new      settings bohe (new), archive xinghuang (old)
#                           -> settings bohe wins (same answer, both readings)
#   caseC-settings-invalid  settings "bogus" (old), archive bohe (new)
#                           -> R1/M2: invalid treated as unset, bohe wins
#
# The decision is read from the binary's own startup probe
# (`[ui-probe] theme=`), never from a state file the writer produced.
set -euo pipefail
ROOT="/home/haroldzhao/MyProj/DshProj/VEdit"
LOGS="$ROOT/evidence/ui-mosaic/review-r1/logs"
BASE="$LOGS/f1-conflict"
BIN="/vedit-build/evidence/ui-mosaic/review-r1/logs/r1-binary"
TPL="$ROOT/evidence/ui-mosaic/fix-r1/state-m1/en/session.json"
IMAGE=vedit-qt6-build:local

run_case() {
  local name=$1 settings_theme=$2 archive_theme=$3 settings_age=$4 archive_age=$5
  local case_dir="$BASE/$name"
  local state="$case_dir/state"
  rm -rf "$case_dir"
  mkdir -p "$state/xdg"

  if [ -n "$settings_theme" ]; then
    printf 'uiTheme = "%s"\n' "$settings_theme" > "$state/settings.toml"
  fi
  if [ -n "$archive_theme" ]; then
    python3 - "$TPL" "$state/session.json" "$archive_theme" <<'PY'
import json, sys
tpl, out, theme = sys.argv[1:4]
with open(tpl, encoding="utf-8") as fh:
    data = json.load(fh)
data["uiTheme"] = theme
with open(out, "w", encoding="utf-8") as fh:
    json.dump(data, fh, ensure_ascii=False)
PY
  fi
  # Explicit, far-apart mtimes so "last writer" is unambiguous.
  [ -f "$state/settings.toml" ] && touch -d "$settings_age" "$state/settings.toml"
  [ -f "$state/session.json" ] && touch -d "$archive_age" "$state/session.json"
  stat -c '%n %y' "$state"/* 2>/dev/null | tee "$case_dir/mtimes.txt"

  docker run --rm -v "$ROOT:/vedit-build" -v "$state:/state" -w /vedit-build \
    -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/state/xdg -e VEDIT_UI_PROBE=1 \
    "$IMAGE" bash -lc "chmod 700 /state/xdg 2>/dev/null || true; $BIN --state-dir /state --hold 2500" \
    > "$case_dir/run.log" 2>&1 || true

  echo "probe:  $(grep -o '\[ui-probe\] theme=[^ ]*' "$case_dir/run.log" | head -1)"
  echo "settings@exit: $(grep -o 'uiTheme.*' "$state/settings.toml" 2>/dev/null || echo '<absent>')"
  echo "archive@exit:  $(python3 -c "import json;print(json.load(open('$state/session.json')).get('uiTheme'))" 2>/dev/null || echo '<absent>')"
}

echo "=== A1 startup arbitration, r1 binary $(sha256sum "$LOGS/r1-binary" | cut -d' ' -f1) ==="
run_case case0-archive-only     ""          bohe       ""                    "2026-11-08 22:00:00"
echo
run_case caseA-settings-old     xinghuang   bohe       "2026-11-08 21:30:00" "2026-11-08 22:30:00"
echo
run_case caseB-settings-new     bohe        xinghuang  "2026-11-08 22:30:00" "2026-11-08 21:30:00"
echo
run_case caseC-settings-invalid bogus       bohe       "2026-11-08 21:30:00" "2026-11-08 22:30:00"
