#!/usr/bin/env bash
# End-to-end drill of the "slang artifact missing" branch of build/gates/gates.sh,
# the branch the misleading evidence/gate-infra/21-gates-slang-missing-path.log
# never actually exercised (that run failed later, in scripts/gates.sh).
#
#   bash evidence/build-infra/run-slang-missing-drill.sh
#
# Method (no file edits, no destroyed artifacts):
#   1. sha256 the existing artifact and move it aside INSIDE build/slang/out/;
#   2. run the real gate with docker made unreachable through the environment
#      (DOCKER_HOST), so its documented one-command build cannot succeed;
#   3. assert the gate prints the actionable FAIL block (Missing/Fix/Logs) and
#      exits 1 — it must not skip and must not pass;
#   4. restore the artifact and prove the sha256 is identical again (and the
#      artifact is executable), then run the gate once more so the restored
#      state is the last thing the tree sees.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT="$ROOT/evidence/build-infra"
ART="$ROOT/build/slang/out/slang"
BACKUP="$ROOT/build/slang/out/.slang.t58-backup"
LOG="$OUT/02-slang-missing-branch.log"

mkdir -p "$OUT"
{
  echo "# C9 evidence header"
  echo "collected_at: $(date --iso-8601=seconds)"
  echo "git_head: $(git -C "$ROOT" rev-parse HEAD)"
  echo "artifact: build/slang/out/slang"
  echo
} > "$LOG"

restore() {
  if [ -f "$BACKUP" ]; then
    mv -f "$BACKUP" "$ART"
    echo "RESTORED: build/slang/out/slang"
  fi
}
trap restore EXIT

if [ ! -x "$ART" ]; then
  echo "FATAL: no artifact to drill the missing branch with: $ART" | tee -a "$LOG"
  exit 2
fi
before_sha=$(sha256sum "$ART" | cut -d' ' -f1)
before_size=$(stat -c '%s' "$ART")
echo "before: sha256=$before_sha size=$before_size executable=yes" | tee -a "$LOG"

# 1. hide the artifact (same directory, restored by the trap and by step 4).
mv -f "$ART" "$BACKUP"
echo "moved away: build/slang/out/slang -> .slang.t58-backup" | tee -a "$LOG"

# 2/3. run the real gate with docker unreachable.
set +e
DOCKER_HOST=tcp://127.0.0.1:1 timeout 300 bash "$ROOT/build/gates/gates.sh" 2>&1 | tee -a "$LOG"
gate_status=${PIPESTATUS[0]}
set -e
echo "GATES-EXIT=$gate_status" | tee -a "$LOG"

set +e
grep -q 'slang artifact is missing' "$LOG"
saw_missing=$?
grep -qi 'do not skip and does not pass\|does NOT skip and does NOT pass' "$LOG"
saw_actionable=$?
grep -q 'Missing  : build/slang/out/slang' "$LOG"
saw_missing_path=$?
grep -q 'Fix      : bash build/slang/build.sh' "$LOG"
saw_fix=$?
set -e
{
  echo
  echo "## assertions"
  echo "branch-entered (artifact missing message): $([ "$saw_missing" -eq 0 ] && echo YES || echo NO)"
  echo "actionable error printed:                 $([ "$saw_actionable" -eq 0 ] && echo YES || echo NO)"
  echo "missing path named:                       $([ "$saw_missing_path" -eq 0 ] && echo YES || echo NO)"
  echo "fix command named:                        $([ "$saw_fix" -eq 0 ] && echo YES || echo NO)"
  echo "gate exit code:                           $gate_status (expected 1)"
} | tee -a "$LOG"

# 4. restore and prove the artifact is byte-identical and executable again.
restore
trap - EXIT
after_sha=$(sha256sum "$ART" | cut -d' ' -f1)
after_size=$(stat -c '%s' "$ART")
executable=no
[ -x "$ART" ] && executable=yes
{
  echo
  echo "## restore proof"
  echo "after:  sha256=$after_sha size=$after_size executable=$executable"
  echo "same sha256: $([ "$before_sha" = "$after_sha" ] && echo YES || echo NO)"
  echo "same size:   $([ "$before_size" = "$after_size" ] && echo YES || echo NO)"
} | tee -a "$LOG"

# The restored artifact must satisfy the gate again (artifact step passes).
set +e
bash "$ROOT/build/gates/gates.sh" >/dev/null 2>&1
restored_gate=$?
set -e
echo "gate with restored artifact: exit=$restored_gate (0 expected for the artifact step; later halves may be red for other lanes)" | tee -a "$LOG"

ok=1
[ "$gate_status" -eq 1 ] || ok=0
[ "$saw_missing" -eq 0 ] || ok=0
[ "$saw_actionable" -eq 0 ] || ok=0
[ "$saw_missing_path" -eq 0 ] || ok=0
[ "$saw_fix" -eq 0 ] || ok=0
[ "$before_sha" = "$after_sha" ] || ok=0
[ "$executable" = yes ] || ok=0
echo "DRILL-OK=$ok" | tee -a "$LOG"
[ "$ok" -eq 1 ]
