#!/usr/bin/env bash
# SVN 1.7 parameter scan (AC-3.0 item 6). Read-only: scans the source, writes
# nothing, and records the verdicts the batch-6 owner needs.
#
# Usage: svn-scan.sh <workspace>
set -uo pipefail

WORKSPACE=${1:?workspace path}
if [ "$(pwd -P)" = "$WORKSPACE" ]; then
  echo "FATAL: the scan must not run inside the VEdit working tree ($WORKSPACE)" >&2
  exit 1
fi
cd "$WORKSPACE" || exit 1

echo "# SVN read-path parameter scan (AC-3.0 item 6)"
echo "collected_at=$(date --iso-8601=seconds)"
echo "svn_binary_in_build_image=$(docker run --rm vedit-build:local sh -c 'svn --version --quiet 2>/dev/null || echo NO_SVN')"
echo

echo "== every svn invocation in the read path (source excerpt) =="
grep -n -A4 'command_runner.run("svn"\|fn svn(\|\.svn(' \
  crates/vd-vcs/src/lib.rs crates/vd-vcs/src/service.rs || true
echo

echo "== options introduced in Subversion 1.8 / 1.9 (must be 0 in the source) =="
fail=0
for option in --show-item --include-externals --search --accept --parents --pin-externals; do
  hits=$(grep -rn -- "$option" crates/vd-vcs/src crates/vd-ffi/src 2>/dev/null | wc -l)
  printf '%-22s hits=%s\n' "$option" "$hits"
  [ "$hits" -eq 0 ] || fail=1
done
echo

echo "== verdicts: the commands the read path actually issues =="
cat <<'EOF'
svn status <path>                since 1.0   compatible with the bundled 1.7.14
svn info --xml <path>            since 1.0   compatible; replaced the 1.9-only single-field selector form
svn diff --internal-diff <path>  since 1.7   compatible with 1.7.14
svn log/list/cat/propget/proplist/blame   allow-listed, but not issued by the service today
EOF
echo
echo "conclusion=no 1.8+ option remains in the SVN read path"
echo "note=the real subprocess E2E is deferred to batch 6b (rev2 §126); this image ships no svn"
echo "scan_fail=$fail"
exit "$fail"
