#!/usr/bin/env bash
# RM-7b: reproduce the OLD selftest's hang on a style that stalls, then show the
# NEW selftest bounding it. Runs inside the pinned Qt6/KF6 image:
#   bash evidence/ux-batch-2/rm7b-disabled-followups/probe/reproduce-old-hang.sh <old|new|both> [timeout-s]
set -euo pipefail
which_mode="${1:-both}"
limit="${2:-30}"
ROOT=/vedit-build
here="$ROOT/evidence/ux-batch-2/rm7b-disabled-followups"
outdir=/tmp/rm7b-slowstyle
mkdir -p "$outdir/plugin/styles"

# --- build the stalling style plugin -------------------------------------------------
/usr/lib/qt6/libexec/moc "$here/probe/slowstyle.cpp" -o "$outdir/slowstyle.moc"
g++ -std=c++20 -fPIC -shared -O1 -g -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB -DQT_NO_DEBUG \
    -I"$outdir" -I/usr/include/x86_64-linux-gnu/qt6 \
    -I/usr/include/x86_64-linux-gnu/qt6/QtCore -I/usr/include/x86_64-linux-gnu/qt6/QtGui \
    -I/usr/include/x86_64-linux-gnu/qt6/QtWidgets \
    "$here/probe/slowstyle.cpp" -o "$outdir/plugin/styles/libslowstyle.so" \
    -lQt6Widgets -lQt6Gui -lQt6Core
echo "PLUGIN $outdir/plugin/styles/libslowstyle.so"

# --- build the OLD (pre-RM-7b) selftest: the prefix source + the current palette ----------
qt_inc=(-I"$ROOT/src/app" -I/usr/include/x86_64-linux-gnu/qt6
        -I/usr/include/x86_64-linux-gnu/qt6/QtCore -I/usr/include/x86_64-linux-gnu/qt6/QtGui
        -I/usr/include/x86_64-linux-gnu/qt6/QtWidgets
        -I/usr/include/KF6/KSyntaxHighlighting)
qt_lib=(-lQt6Widgets -lQt6Gui -lQt6Core -lKF6SyntaxHighlighting)
g++ -std=c++20 -fPIC -O1 -g -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB -DQT_NO_DEBUG \
    "${qt_inc[@]}" "$here/prefix/disabled_look_selftest_prefix.cpp" \
    "$ROOT/src/app/theme_manager.cpp" -o "$outdir/old-disabled-look-selftest" "${qt_lib[@]}"
echo "OLD-BINARY $outdir/old-disabled-look-selftest"

run() {
  local label="$1" binary="$2" timeout_s="$3"
  local extra="${4:-}"
  echo "=== RUN $label (limit ${timeout_s}s, QT_PLUGIN_PATH=$outdir/plugin ${extra:+$extra}) ==="
  set +e
  env QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-rm7b \
      QT_PLUGIN_PATH="$outdir/plugin" ${extra} timeout "$timeout_s" "$binary"
  local status=$?
  set -e
  echo "=== $label exit=$status ==="
  if [ "$status" -eq 124 ]; then
    echo "=== $label TIMED OUT after ${timeout_s}s (exit 124) ==="
  fi
  return 0
}

mkdir -p /tmp/xdg-rm7b; chmod 700 /tmp/xdg-rm7b
case "$which_mode" in
  old) run OLD "$outdir/old-disabled-look-selftest" "$limit" ;;
  new)
    new_binary="$ROOT/target/qt6-build/src/app/vedit-app-disabled-look-selftest"
    run NEW "$new_binary" "$limit"
    ;;
  new-app-style)
    # The stalling style is the APPLICATION style (the KDE case: the user's
    # desktop style is what every widget paints with). Every bounded probe
    # (style samples AND the dot delegate) must still exit 0.
    new_binary="$ROOT/target/qt6-build/src/app/vedit-app-disabled-look-selftest"
    run NEW-APP-STYLE "$new_binary" "$limit" "QT_STYLE_OVERRIDE=SlowStyle"
    ;;
  both)
    run OLD "$outdir/old-disabled-look-selftest" "$limit"
    new_binary="$ROOT/target/qt6-build/src/app/vedit-app-disabled-look-selftest"
    run NEW "$new_binary" "$limit"
    ;;
  *) echo "usage: $0 <old|new|new-app-style|both> [timeout-s]" >&2; exit 2 ;;
esac
