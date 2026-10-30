#!/usr/bin/env bash
# Offline smoke test for the built slang binary.
# Runs INSIDE a STOCK docker.m.daocloud.io/library/centos:7 container started
# with `--network none`: nothing is installed, nothing is downloaded. The
# artifact must work with only the glibc 2.17 that CentOS 7 ships.
#
# Mounts: /out (artifact, ro), /fixtures (ro), /evidence (rw), /scripts (ro).
# Exit code: 0 only if every check passes.
set -uo pipefail

EV=/evidence
LOG=$EV/09-offline-smoke.txt
# Stream everything to the console AND to the evidence file.
exec > >(tee "$LOG") 2>&1

fail=0
say() { printf '%s\n' "$*"; }
check() { # check <rc> <description>
  if [ "$1" -eq 0 ]; then say "PASS: $2"; else say "FAIL: $2"; fail=1; fi
}

say "# offline smoke test ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
say "## container"; cat /etc/centos-release
say "## glibc"; ldd --version | sed -n '1p'
say "## routing table (no default route expected under --network none):"
cat /proc/net/route

say
say "## 1) slang --version"
version_out=$(/out/slang --version 2>&1); version_rc=$?
say "$version_out"
say "version_exit=$version_rc"
check "$version_rc" "--version exits 0 on a stock CentOS 7 (glibc 2.17)"
case "$version_out" in
  *"slang version ${SLANG_VERSION:-}"*) check 0 "version string names slang $SLANG_VERSION" ;;
  *) check 1 "version string names slang ${SLANG_VERSION:-<unset>}" ;;
esac

say
say "## 2) lint a clean fixture (expect exit 0)"
/out/slang --lint-only /fixtures/clean.sv >"$EV/10-clean.out" 2>&1
clean_rc=$?
say "clean_lint_exit=$clean_rc"
sed -n '1,10p' "$EV/10-clean.out"
check "$clean_rc" "clean fixture lints with exit 0"

say
say "## 3) lint a fixture with an undeclared identifier (expect non-zero + JSON array)"
/out/slang --lint-only -Weverything --diag-json "$EV/11-diag.json" \
  /fixtures/undeclared.sv >"$EV/11-diag-text.out" 2>&1
lint_rc=$?
say "lint_exit=$lint_rc (expected non-zero: the fixture contains an error)"
sed -n '1,20p' "$EV/11-diag-text.out"
if [ "$lint_rc" -ne 0 ]; then check 0 "lint reports failure exit for a broken fixture"
else check 1 "lint exit code is 0 for a broken fixture"; fi

say "## raw --diag-json output (first 800 bytes):"
head -c 800 "$EV/11-diag.json"; echo
python - "$EV/11-diag.json" <<'PY'
import json, sys
with open(sys.argv[1]) as fh:
    data = json.load(fh)
assert isinstance(data, list), "expected a JSON array, got %r" % type(data)
assert data, "expected at least one diagnostic"
print("json_array_len=%d" % len(data))
print("first_diagnostic_keys=%s" % sorted(data[0].keys()))
PY
check $? "--diag-json output parses as a non-empty JSON array"

say
say "## 4) runtime dependency check (libc only, no libstdc++/libgcc_s)"
ldd /out/slang || check 1 "ldd failed"
if ldd /out/slang 2>/dev/null | grep -qE 'libstdc\+\+|libgcc_s'; then
  check 1 "binary still needs libstdc++/libgcc_s"
else
  check 0 "binary needs neither libstdc++ nor libgcc_s"
fi

say
if [ "$fail" -eq 0 ]; then
  say "OFFLINE SMOKE: PASS"
else
  say "OFFLINE SMOKE: FAIL"
fi
exit "$fail"
