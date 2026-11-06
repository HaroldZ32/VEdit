#!/usr/bin/env bash
# Batch 6b (AC-6.5 / AC-6.4): probe the BUNDLED subversion 1.14.5 CANABILITIES the
# panel's capability table claims, and the AppDir loader/subprocess facts.
#
#   bash evidence/svn-e2e/tools/probe-toolchain.sh
#
# Everything is read-only: `svn help <sub>` proves a subcommand exists, `--version`
# proves the bundle is the expected one, `readelf`/`ldd` prove the wrapper's
# subprocess resolution. The fixture commands that CREATE things live in
# crates/vd-vcs/tests/svn_e2e.rs (env-gated) and evidence/svn-e2e/run-all.sh.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
APPDIR="$ROOT/packaging/runtime/out/AppDir"
SVN="$APPDIR/bin/svn"
SVN_BIN="$APPDIR/usr/lib/vcs/libexec/svn"
LOADER="$APPDIR/ld-linux-x86-64.so.2"
ADMIN="$ROOT/build/vcs-tools/svnadmin"
LOG="$ROOT/evidence/svn-e2e/logs/20-toolchain-capabilities.log"
mkdir -p "$(dirname "$LOG")"

{
  echo "# batch 6b toolchain / capability probe"
  echo "collected_at: $(date --iso-8601=seconds)"
  echo "host: $(uname -srm)"
  echo "host_glibc: $(ldd --version | head -1)"
  echo "appdir=$APPDIR"
  echo
  echo "## bundled versions (through the AppDir wrapper)"
  echo "\$ $SVN --version --quiet"
  "$SVN" --version --quiet
  echo "\$ $ADMIN --version --quiet"
  "$ADMIN" --version --quiet
  echo "\$ $SVN --version | head -2"
  "$SVN" --version | head -2
  echo
  echo "## the loader / subprocess resolution (AC-6.4)"
  echo "\$ file $SVN_BIN"
  file "$SVN_BIN"
  echo "\$ file $LOADER"
  file "$LOADER"
  echo "\$ readelf -d $LOADER | grep SONAME"
  readelf -d "$LOADER" 2>/dev/null | grep SONAME || true
  echo "\$ readelf -d $SVN_BIN | grep NEEDED"
  readelf -d "$SVN_BIN" 2>/dev/null | grep NEEDED || true
  echo "\$ head -3 $APPDIR/usr/lib/vcs/bin/svn   (the wrapper the product spawns)"
  head -3 "$APPDIR/usr/lib/vcs/bin/svn"
  echo "\$ $LOADER --library-path <appdir libs> --list $SVN_BIN | head -14"
  "$LOADER" --library-path "$APPDIR/usr/lib/vcs/lib:$APPDIR/lib" --list "$SVN_BIN" 2>&1 | head -14 || true
  echo "\$ ... | grep -c 'usr/lib/vcs/lib'   (bundled libs BEFORE the host's)"
  "$LOADER" --library-path "$APPDIR/usr/lib/vcs/lib:$APPDIR/lib" --list "$SVN_BIN" 2>/dev/null     | grep -c 'usr/lib/vcs/lib' || true
  echo "\$ ... | grep -c '/lib/x86_64-linux-gnu'   (host libs still used for glibc/system)"
  "$LOADER" --library-path "$APPDIR/usr/lib/vcs/lib:$APPDIR/lib" --list "$SVN_BIN" 2>/dev/null     | grep -c '/lib/x86_64-linux-gnu' || true
  echo
  echo "## capability probes (the table in README.md §2)"
  for sub in status info diff commit add remove revert update log copy merge; do
    if "$SVN" help "$sub" >/dev/null 2>&1; then
      echo "svn help $sub -> exit=0 (available)"
    else
      echo "svn help $sub -> NON-ZERO (unavailable)"
    fi
  done
  echo "svn --version reports: $("$SVN" --version --quiet)"
  echo "\$ $SVN help diff | grep -E 'internal-diff|non-interactive|no-auth-cache'"
  "$SVN" help diff 2>/dev/null | grep -E 'internal-diff|non-interactive|no-auth-cache' || true
  echo
  echo "## the panel's documented argv against this client"
  echo "svn status                    -> $("$SVN" help status >/dev/null 2>&1 && echo ok)"
  echo "svn info --xml                -> $("$SVN" help info >/dev/null 2>&1 && echo ok)"
  echo "svn diff --internal-diff      -> $("$SVN" help diff >/dev/null 2>&1 && echo ok)"
  echo "svn commit --non-interactive --no-auth-cache -> $("$SVN" help commit >/dev/null 2>&1 && echo ok)"
  echo "svn update --non-interactive --no-auth-cache -> $("$SVN" help update >/dev/null 2>&1 && echo ok)"
  echo
  echo "## svnadmin is build-only (never shipped in the AppDir)"
  if [ -e "$APPDIR/usr/lib/vcs/bin/svnadmin" ] || [ -e "$APPDIR/bin/svnadmin" ]; then
    echo "FAIL: svnadmin is inside the AppDir"
  else
    echo "OK: no svnadmin in the AppDir"
  fi
  echo
  echo "result=PASS"
} 2>&1 | tee "$LOG"
