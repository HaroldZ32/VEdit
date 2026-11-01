#!/usr/bin/env bash
# T117: the SHIPPED binary must write a crash report. Runs `vedit-app
# --crash-test=segv` and inspects the report the signal handler wrote.
set -u
app="$1"
work="$2"
dir="${work}/crash-report-check"
rm -rf "${dir}"
mkdir -p "${dir}/state"
export XDG_STATE_HOME="${dir}/state"
export QT_QPA_PLATFORM=offscreen
export XDG_RUNTIME_DIR="${dir}/runtime"
mkdir -p "${XDG_RUNTIME_DIR}"
# The report is what we verify; keep the build dir free of 1 MB cores.
ulimit -c 0 2>/dev/null || true

"${app}" --crash-test=segv >"${dir}/run.log" 2>&1
status=$?
report="$(find "${dir}/state/vedit" -name 'crash-*.log' 2>/dev/null | head -1)"
if [ -z "${report}" ]; then
  echo "FAIL: no crash report under ${dir}/state/vedit (exit=${status})"
  echo "--- process output ---"
  cat "${dir}/run.log" || true
  exit 1
fi

fail=0
require() {
  if ! grep -q "$1" "${report}"; then
    echo "FAIL: report lacks '$1'"
    fail=1
  fi
}
require "signal=11 (SIGSEGV)"
require "si_code="
require "fault_address=0x"
require "build="
require "backtrace:"
require "last_events:"
require "crash-test"

if [ "${fail}" -ne 0 ]; then
  echo "--- report (${report}) ---"
  cat "${report}"
  exit 1
fi

echo "PASS: vedit-app wrote a crash report at ${report} (process exit=${status})"
echo "--- report head ---"
head -20 "${report}"
exit 0
