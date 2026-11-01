#!/usr/bin/env bash
# T105 machine check: every log carries the backfilled C9 header, and the REPORT's
# collected_at is not later than its own mtime.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
EVID="$ROOT/evidence/slice3-fim"
fail=0
total=0
for f in "$EVID"/*.log "$EVID"/repro/*.log; do
  total=$((total+1))
  missing=""
  for key in 'collected_at:' 'collected_at_utc:' 'git_head' 'git_status_porcelain' 'backfill: 补记（T105）'; do
    grep -q "$key" "$f" || missing="$missing $key"
  done
  if [ -n "$missing" ]; then
    echo "FAIL $f missing:$missing"
    fail=$((fail+1))
  fi
done
echo "LOGS checked=$total missing_fields=$fail"
report="$EVID/REPORT.md"
collected=$(sed -n 's/^collected_at: \([0-9T:+-]*\).*/\1/p' "$report" | head -1)
mtime=$(date -r "$report" --iso-8601=seconds)
echo "REPORT collected_at=$collected mtime=$mtime"
[ -n "$collected" ] || { echo "FAIL report has no collected_at"; fail=$((fail+1)); }
python3 - "$collected" "$mtime" <<'PY' || fail=$((fail+1))
import sys, datetime
c = datetime.datetime.fromisoformat(sys.argv[1])
m = datetime.datetime.fromisoformat(sys.argv[2])
ok = c <= m
print(f"REPORT_TIME_CHECK {'PASS' if ok else 'FAIL'} collected_at={c.isoformat()} mtime={m.isoformat()}")
sys.exit(0 if ok else 1)
PY
python3 - "$EVID" <<'PY' || fail=$((fail+1))
import pathlib, hashlib, sys
root = pathlib.Path(sys.argv[1])
bad = 0
files = sorted(list(root.glob('*.log')) + list(root.glob('repro/*.log')))
for path in files:
    lines = path.read_text().splitlines()
    claimed = None
    block = []
    state = False
    for line in lines:
        if line.startswith('git_status_porcelain_sha256:'):
            claimed = line.split(':', 1)[1].split('#')[0].strip()
        if line.startswith('git_status_porcelain:'):
            state = True
            continue
        if state:
            if line.startswith('---'):
                break
            block.append(line[1:] if line.startswith(' ') else line)
    actual = hashlib.sha256(('\n'.join(block) + '\n').encode()).hexdigest()
    if claimed != actual:
        bad += 1
        print(f'PORCELAIN-SHA FAIL {path} claimed={claimed} actual={actual}')
print(f'LOG_PORCELAIN_SHA logs={len(files)} mismatches={bad}')
sys.exit(0 if bad == 0 else 1)
PY
echo "VERIFY-EXIT=$fail"
exit "$fail"
