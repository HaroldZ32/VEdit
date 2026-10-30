#!/usr/bin/env bash
# Runs INSIDE the pinned CentOS 7 build image (workspace mounted at /vedit-build).
# Driven by scripts/build.sh; not meant to run on the host.
#
# Produces: target/release/vd-app, build/artifacts/vd-app, and the evidence files
# 04..11 in build/evidence/ (environment, RPM baseline check, build log, artifact,
# Xvfb + --selftest smoke).
set -euo pipefail

ROOT=/vedit-build
# shellcheck source=versions.env
source "$ROOT/scripts/versions.env"
EVID="$ROOT/build/evidence"
ARTIFACTS="$ROOT/build/artifacts"
mkdir -p "$EVID" "$ARTIFACTS"

# Reproducible-build inputs: SDE must arrive from the host (there is no .git in
# the container); an empty or non-numeric value is a hard failure, never a
# silent fallback. Incremental compilation is pinned off for the release build.
: "${SOURCE_DATE_EPOCH:?SOURCE_DATE_EPOCH must be passed in from the host build}"
case "$SOURCE_DATE_EPOCH" in
  ''|*[!0-9]*)
    echo "FATAL: SOURCE_DATE_EPOCH is not a numeric epoch: '$SOURCE_DATE_EPOCH'" >&2
    exit 1
    ;;
esac
export CARGO_INCREMENTAL=0
{
  echo "source_date_epoch=$SOURCE_DATE_EPOCH"
  echo "cargo_incremental=$CARGO_INCREMENTAL"
  echo "remap_flags=$RELEASE_RUSTFLAGS"
} >> "$EVID/14-determinism-env.txt"

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
# The workspace is mounted at /vedit-build in the pinned image; remap it to a
# neutral /build prefix instead of a no-op.
export RUSTFLAGS="${RUSTFLAGS:-} ${RELEASE_RUSTFLAGS:?RELEASE_RUSTFLAGS is defined in scripts/versions.env}"
cargo build --release --workspace 2>&1 | tee "$EVID/06-cargo-build.log"

BIN="$ROOT/target/release/vd-app"
if [ ! -x "$BIN" ]; then
  echo "FATAL: $BIN was not produced" >&2
  exit 1
fi
strip --strip-all "$BIN"
cp "$BIN" "$ARTIFACTS/vd-app"
{
  echo "# Embedded build-machine paths (panic locations must not leak /vedit-build)"
  echo "\$ strings vd-app | grep -c /vedit-build"
  PATH_LEAKS=$(strings "$BIN" | grep -c '/vedit-build' || true)
  echo "leaks=$PATH_LEAKS"
  echo "remap_flags=$RUSTFLAGS"
} > "$EVID/13-remap-check.txt" 2>&1
PATH_LEAKS=$(strings "$BIN" | grep -c '/vedit-build' || true)
if [ "$PATH_LEAKS" -ne 0 ]; then
  echo "FATAL: $PATH_LEAKS embedded /vedit-build path(s) remain in $BIN" >&2
  exit 1
fi
{
  echo "artifact=build/artifacts/vd-app"
  sha256sum "$BIN"
  stat -c 'size_bytes=%s' "$BIN"
  file "$BIN"
  echo "## ldd:"
  ldd "$BIN"
} | tee "$EVID/07-artifact-container.txt"

echo "== Xvfb main-window smoke test =="
export VEDIT_GSV_DATADIR="${VEDIT_GSV_DATADIR:-/opt/gsv4/share/gtksourceview-4}"
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

raw = open(sys.argv[1], encoding="utf-8", errors="replace").read()
# GTK diagnostics may precede the JSON report on stdout; the report is the object
# from the first "{" to the last "}".
report = json.loads(raw[raw.find("{"):raw.rfind("}") + 1])
assert report.get("ok") is True, f"selftest ok: {report.get('ok')!r}"
checks = [check for suite in report["reports"] for check in suite["checks"]]
failed = [check["name"] for check in checks if not check["ok"]]
assert not failed, f"failed checks: {failed}"
print(
    "selftest JSON valid: suite=%s ok=%s checks=%d"
    % (report["suite"], report["ok"], len(checks))
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

# A machine id helps GApplication in a bare container; the minimal CentOS 7
# image ships it empty (0 bytes). Writing it is best effort: with
# VEDIT_CONTAINER_USER=1 the container runs as the invoking non-root user and
# this path is not writable — the app starts fine with the empty file (verified
# with --selftest smoke as uid 1000), so a failed write must not abort the build.
if [ ! -s /etc/machine-id ]; then
  if ! printf "0123456789abcdef0123456789abcdef\n" > /etc/machine-id 2>/dev/null; then
    echo "[build] /etc/machine-id is not writable as uid $(id -u 2>/dev/null || echo '?'); continuing with the empty file"
  fi
fi

echo "== Xvfb selftest suites (smoke/highlight/config/fim, plus lsp/slang when the binaries are provided) =="
# GtkSourceView data dir of the pinned image; the app warns (and the highlight
# suite fails) when it is wrong, which is exactly the AC-05 guard.
if [ -z "${VEDIT_SLANG:-}" ] && [ -x "$ROOT/build/slang/out/slang" ]; then
  export VEDIT_SLANG="$ROOT/build/slang/out/slang"
fi
if [ -z "${VEDIT_VERIBLE_LS:-}" ]; then
  VERIBLE_DIR=$(compgen -G "$ROOT/target/verible-pin/extract/verible-*/bin" | head -1 || true)
  if [ -n "$VERIBLE_DIR" ]; then
    export VEDIT_VERIBLE_LS="$VERIBLE_DIR/verible-verilog-ls"
    export VEDIT_VERIBLE_FORMAT="$VERIBLE_DIR/verible-verilog-format"
    export VEDIT_VERIBLE_LINT="$VERIBLE_DIR/verible-verilog-lint"
  fi
fi
# The CLI accepts exactly ONE suite name (or `all`/`list`), never a list, so a
# clean environment runs the suites one argv at a time. With both external
# binaries present a single `all` run covers everything and keeps the historical
# 12-selftest-suites.json evidence name.
if [ -n "${VEDIT_VERIBLE_LS:-}" ] && [ -n "${VEDIT_SLANG:-}" ]; then
  SELFTEST_SUITES=(all)
else
  SELFTEST_SUITES=(smoke highlight config fim)
fi
{
  echo "# Xvfb selftest suites ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  echo "VEDIT_SLANG=${VEDIT_SLANG:-<unset>}"
  echo "VEDIT_VERIBLE_LS=${VEDIT_VERIBLE_LS:-<unset>}"
  echo "VEDIT_GSV_DATADIR=${VEDIT_GSV_DATADIR:-<unset>}"
} > "$EVID/12-selftest-suites.txt"
for suite in "${SELFTEST_SUITES[@]}"; do
  SUITE_REPORT="$EVID/12-selftest-suites.json"
  SUITE_STDERR="$EVID/12-selftest-suites.err"
  if [ "$suite" != "all" ]; then
    SUITE_REPORT="$EVID/12-selftest-$suite.json"
    SUITE_STDERR="$EVID/12-selftest-$suite.err"
  fi
  set +e
  "$BIN" --selftest "$suite" > "$SUITE_REPORT" 2> "$SUITE_STDERR"
  SUITE_RC=$?
  set -e
  {
    echo "suite=$suite"
    echo "exit=$SUITE_RC"
    echo "report=$SUITE_REPORT"
    echo "stderr=$SUITE_STDERR"
  } >> "$EVID/12-selftest-suites.txt"
  if [ "$SUITE_RC" -ne 0 ]; then
    echo "FATAL: --selftest $suite exited $SUITE_RC (the JSON report is in $SUITE_REPORT)" >&2
    sed -n '1,60p' "$SUITE_STDERR" >&2
    exit 1
  fi
done
cat "$EVID/12-selftest-suites.txt"

echo "BUILD + XVFB SMOKE OK"
