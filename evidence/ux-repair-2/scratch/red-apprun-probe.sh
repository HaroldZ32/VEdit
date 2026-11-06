#!/bin/sh
# RED control for F1: rebuild the pre-repair AppRun (unconditional slang export)
# in a temp dir and run the SAME probe. It must fail; this script succeeds only
# when the probe detects the regression.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT
cp "$ROOT/packaging/runtime/probe-apprun-env.sh" "$TMP/"
python3 - "$ROOT/packaging/runtime/AppRun" "$TMP/AppRun" <<'PY'
import sys
src = open(sys.argv[1], encoding='utf-8').read()
old = '''if [ -z "${VEDIT_SLANG:-}" ] && [ -x "$APPDIR/bin/slang" ]; then
    export VEDIT_SLANG="$APPDIR/bin/slang"
fi'''
new = 'export VEDIT_SLANG="$APPDIR/bin/slang"'
if old not in src:
    raise SystemExit('RED-control setup failed: guarded slang block not found')
open(sys.argv[2], 'w', encoding='utf-8').write(src.replace(old, new, 1))
PY
chmod +x "$TMP/AppRun"
sh "$TMP/probe-apprun-env.sh"
rc=$?
echo "RED-CONTROL probe exit=$rc (non-zero expected)"
[ "$rc" -ne 0 ]
