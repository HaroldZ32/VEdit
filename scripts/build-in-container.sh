#!/usr/bin/env bash
# Runs INSIDE the pinned CentOS 7 build image (workspace mounted at /vedit-build).
# Driven by scripts/build.sh; not meant to run on the host.
#
# Produces: target/release/vd-app, build/artifacts/vd-app, and the evidence files
# 04..11 in build/evidence/ (environment, RPM baseline check, build log, artifact,
# Xvfb + --selftest smoke).
set -euo pipefail

ROOT=/vedit-build
EVID="$ROOT/build/evidence"
ARTIFACTS="$ROOT/build/artifacts"
mkdir -p "$EVID" "$ARTIFACTS"

echo "== container environment =="
{
  echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "container=$(cat /etc/centos-release)"
  echo "glibc=$(rpm -q glibc)"
  echo "gtk3=$(rpm -q gtk3)"
  echo "gtk3_pkg=$(pkg-config --modversion gtk+-3.0)"
  echo "glib_pkg=$(pkg-config --modversion glib-2.0)"
  echo "gsv4_pkg=$(pkg-config --modversion gtksourceview-4)"
  echo "rustc=$(rustc --version)"
  echo "cargo=$(cargo --version)"
  echo "python3=$(python3 --version 2>&1)"
  echo "meson=$(meson --version)"
  echo "ninja=$(ninja --version)"
  echo "cargo_home=${CARGO_HOME:-<unset>}"
  echo "home=${HOME:-<unset>}"
} | tee "$EVID/04-container-env.txt"

echo "== installed RPM set vs scripts/docker/rpm-baseline.txt =="
SNAPSHOT="$EVID/05-rpm-snapshot.txt"
rpm -qa --qf '%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}\n' | sort > "$SNAPSHOT"
BASELINE="$ROOT/scripts/docker/rpm-baseline.txt"
if [ "${VEDIT_WRITE_RPM_BASELINE:-0}" = "1" ]; then
  cp "$SNAPSHOT" "$BASELINE"
  echo "baseline written: scripts/docker/rpm-baseline.txt ($(wc -l < "$BASELINE") packages)"
else
  if [ ! -f "$BASELINE" ]; then
    echo "FATAL: $BASELINE is missing; run once with VEDIT_WRITE_RPM_BASELINE=1" >&2
    exit 1
  fi
  if ! diff -u "$BASELINE" "$SNAPSHOT" > "$EVID/05-rpm-diff.txt"; then
    echo "FATAL: installed RPM set differs from the committed baseline:" >&2
    sed -n '1,40p' "$EVID/05-rpm-diff.txt" >&2
    exit 1
  fi
  echo "rpm set matches the baseline ($(wc -l < "$BASELINE") packages)"
fi

echo "== cargo build --release --workspace =="
# Keep build-machine paths out of the shipped ELF (Rust embeds panic locations).
export RUSTFLAGS="${RUSTFLAGS:-} --remap-path-prefix=/vedit-build=/vedit-build --remap-path-prefix=/opt/cargo=/cargo"
cargo build --release --workspace 2>&1 | tee "$EVID/06-cargo-build.log"

BIN="$ROOT/target/release/vd-app"
if [ ! -x "$BIN" ]; then
  echo "FATAL: $BIN was not produced" >&2
  exit 1
fi
strip --strip-all "$BIN"
cp "$BIN" "$ARTIFACTS/vd-app"
{
  echo "artifact=build/artifacts/vd-app"
  sha256sum "$BIN"
  stat -c 'size_bytes=%s' "$BIN"
  file "$BIN"
  echo "## ldd:"
  ldd "$BIN"
} | tee "$EVID/07-artifact-container.txt"

echo "== Xvfb main-window smoke test =="
export GDK_BACKEND=x11
export NO_AT_BRIDGE=1
Xvfb :99 -screen 0 1280x1024x24 > "$EVID/08-xvfb.log" 2>&1 &
XVFB_PID=$!
cleanup() { kill "$XVFB_PID" 2>/dev/null || true; }
trap cleanup EXIT
sleep 1
export DISPLAY=:99

"$BIN" --selftest smoke --probe-hold-ms 2500 \
  > "$EVID/09-selftest-smoke.json" 2> "$EVID/09-selftest-smoke.err" &
APP_PID=$!
sleep 1
xwininfo -root -tree > "$EVID/10-xwininfo-tree.txt" 2>&1 || true
set +e
wait "$APP_PID"
APP_RC=$?
set -e

{
  echo "# Xvfb main-window smoke ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  echo "selftest_exit=$APP_RC"
  echo "## window entry from the X server (xwininfo -root -tree):"
  grep -E 'VEdit' "$EVID/10-xwininfo-tree.txt" || echo "(no VEdit window found)"
} | tee "$EVID/11-xvfb-smoke.txt"

python3 - "$EVID/09-selftest-smoke.json" <<'PY'
import json
import sys

report = json.load(open(sys.argv[1], encoding="utf-8"))
assert report["result"] == "pass", f"selftest result: {report['result']!r}"
failed = [check["name"] for check in report["checks"] if not check["ok"]]
assert not failed, f"failed checks: {failed}"
print(
    "selftest JSON valid: suite=%s result=%s checks=%d"
    % (report["suite"], report["result"], len(report["checks"]))
)
PY

if ! grep -q 'VEdit' "$EVID/10-xwininfo-tree.txt"; then
  echo "FATAL: the X server does not list a VEdit window" >&2
  sed -n '1,40p' "$EVID/10-xwininfo-tree.txt" >&2
  exit 1
fi
if [ "$APP_RC" -ne 0 ]; then
  echo "FATAL: --selftest smoke exited $APP_RC" >&2
  sed -n '1,40p' "$EVID/09-selftest-smoke.err" >&2
  exit 1
fi

echo "BUILD + XVFB SMOKE OK"
