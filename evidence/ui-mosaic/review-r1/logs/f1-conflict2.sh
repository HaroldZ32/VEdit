#!/usr/bin/env bash
# R2/A1 + R3: startup arbitration between settings.toml `uiTheme` and the
# session archive `uiTheme`, with EXPLICIT mtimes (epoch, no TZ ambiguity).
#
# REQUIREMENTS A1 (R2 edit + captain ruling): when both stores hold a VALID and
# DIFFERENT value, the file with the NEWER mtime wins; equal mtime -> settings
# (the persistent preference). Invalid/missing values fall back to
# valid settings > valid archive > default yunwu, with a visible diagnostic.
#
#   caseA  settings xinghuang @now-3600, archive bohe @now-60   -> bohe
#   caseB  settings bohe @now-60,      archive xinghuang @now-3600 -> bohe
#   caseC  both valid+different, SAME mtime                      -> settings
#   caseD1 settings bogus @now-60,     archive bohe @now-3600     -> bohe
#   caseD2 settings bohe @now-60,      archive bogus @now-3600    -> bohe
#   caseD3 settings bogus, archive bogus                          -> yunwu
#   caseD4 neither file present                                   -> yunwu
#
# The decision is read from the binary's own startup probe (`[ui-probe] theme=`),
# never from a writer-produced log; the two files' mtimes and original values are
# recorded next to each run.
set -euo pipefail
ROOT="/home/haroldzhao/MyProj/DshProj/VEdit"
LOGS="$ROOT/evidence/ui-mosaic/review-r1/logs"
BASE="$LOGS/f1-conflict-v2"
BIN=${BIN:-/vedit-build/evidence/ui-mosaic/review-r1/logs/final-binary}
TPL="$ROOT/evidence/ui-mosaic/fix-r1/state-m1/en/session.json"
IMAGE=vedit-qt6-build:local
NOW=$(date +%s)

run_case() {
  local name=$1 settings_theme=$2 archive_theme=$3 settings_mtime=$4 archive_mtime=$5
  local case_dir="$BASE/$name"
  local state="$case_dir/state"
  rm -rf "$case_dir"; mkdir -p "$state/xdg"

  [ -n "$settings_theme" ] && printf 'uiTheme = "%s"\n' "$settings_theme" > "$state/settings.toml"
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
  [ -n "$settings_mtime" ] && [ -f "$state/settings.toml" ] && touch -d "@$settings_mtime" "$state/settings.toml"
  [ -n "$archive_mtime" ] && [ -f "$state/session.json" ] && touch -d "@$archive_mtime" "$state/session.json"

  { echo "--- mtimes (epoch + human) ---"; stat -c '%n mtime=%Y (%y)' "$state"/*.toml "$state"/session.json 2>/dev/null || true
    echo "--- original values ---"
    echo "settings: $(grep -o 'uiTheme.*' "$state/settings.toml" 2>/dev/null || echo '<absent>')"
    echo "archive : $(python3 -c "import json;print(json.load(open('$state/session.json')).get('uiTheme'))" 2>/dev/null || echo '<absent>')"
  } | tee "$case_dir/input.txt"

  docker run --rm -v "$ROOT:/vedit-build" -v "$state:/state" -w /vedit-build \
    -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/state/xdg -e VEDIT_UI_PROBE=1 \
    "$IMAGE" bash -lc "chmod 700 /state/xdg 2>/dev/null || true; $BIN --state-dir /state --hold 2500" \
    > "$case_dir/run.log" 2>&1 || true

  echo "probe:         $(grep -o '\[ui-probe\] theme=[^ ]*' "$case_dir/run.log" | head -1)"
  echo "diagnostic:    $(grep -o '\[ui-probe\] diagnostic=.*' "$case_dir/run.log" | head -1)"
  echo "settings@exit: $(grep -o 'uiTheme.*' "$state/settings.toml" 2>/dev/null || echo '<absent>')"
  echo "archive@exit:  $(python3 -c "import json;print(json.load(open('$state/session.json')).get('uiTheme'))" 2>/dev/null || echo '<absent>')"
}

echo "=== A1/R3 mtime arbitration, binary $(sha256sum "$LOGS/final-binary" 2>/dev/null | cut -d' ' -f1) ==="
run_case caseA-settings-old     xinghuang bohe      $((NOW-3600)) $((NOW-60));   echo
run_case caseB-settings-new     bohe      xinghuang $((NOW-60))   $((NOW-3600)); echo
run_case caseC-equal-mtime      bohe      xinghuang $((NOW-600))  $((NOW-600));  echo
run_case caseD1-invalid-settings bogus    bohe      $((NOW-60))   $((NOW-3600)); echo
run_case caseD2-invalid-archive  bohe     bogus     $((NOW-60))   $((NOW-3600)); echo
run_case caseD3-both-invalid     bogus    bogus     $((NOW-60))   $((NOW-3600)); echo
run_case caseD4-neither          ""       ""        ""            ""
