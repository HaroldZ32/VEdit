#!/usr/bin/env bash
# Evidence-only runner for the AC-14 real-endpoint test.
# Reads the DeepSeek key from the harness credentials at runtime, passes it to
# the proxy process through its environment only, and never prints or stores it.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
DIR="$ROOT/evidence/fim/real-endpoint"
RUN="$DIR/run"
rm -rf "$RUN"
mkdir -p "$RUN"

KEY=$(python3 - "$HOME/.dsh/.credentials.yaml" <<'PY'
import sys, yaml
doc = yaml.safe_load(open(sys.argv[1]))
print((doc.get("refs") or {}).get("DEEPSEEK_API_KEY", ""))
PY
)
if [ -z "$KEY" ]; then
  printf 'key_present=false\n' > "$RUN/key.txt"
  echo "key_present=false" >&2
  exit 1
fi
printf 'key_present=true\nkey_length=%s\nauth_header=Bearer <redacted>\n' "${#KEY}" > "$RUN/key.txt"

echo "== build evidence harness (pinned image) =="
docker run --rm -e HOME=/vedit-build/target/home \
  -e CARGO_HOME=/vedit-build/target/cargo-home -e RUSTUP_HOME=/opt/rust \
  -e RUSTUP_TOOLCHAIN=1.99.0 \
  -v "$ROOT:/vedit-build" -w /vedit-build \
  vedit-build:local bash -lc \
  'cargo build --release --manifest-path /vedit-build/evidence/fim/real-endpoint/harness/Cargo.toml --target-dir /vedit-build/evidence/fim/real-endpoint/harness/target' \
  > "$RUN/build.log" 2>&1
BIN="$DIR/harness/target/release/fim-real-endpoint-harness"
test -x "$BIN"

start_proxy() {
  local log="$1"
  rm -f "$RUN/port"
  env DEEPSEEK_API_KEY="$KEY" PORT_FILE="$RUN/port" PROXY_DELAY_FIRST=3 \
    python3 "$DIR/proxy.py" > "$log" 2>&1 &
  PROXY_PID=$!
  for _ in $(seq 1 200); do
    if [ -s "$RUN/port" ]; then break; fi
    sleep 0.05
  done
  test -s "$RUN/port" || { echo "proxy did not start" >&2; exit 1; }
  PORT=$(cat "$RUN/port")
  export FIM_BASE="http://127.0.0.1:$PORT"
}

run_case() {
  local mode="$1"
  set +e
  "$BIN" "$mode" > "$RUN/$mode.log" 2>&1
  local rc=$?
  set -e
  echo "$mode exit=$rc"
  cat "$RUN/$mode.log"
}

echo "== positive (real network, proxy rewrites /v1/completions -> /beta/completions) =="
start_proxy "$RUN/proxy-positive.log"
export FIM_MODEL="${FIM_MODEL:-deepseek-flash}"
run_case positive
kill "$PROXY_PID" 2>/dev/null || true
wait "$PROXY_PID" 2>/dev/null || true

echo "== cancel (first upstream response delayed by the proxy) =="
start_proxy "$RUN/proxy-cancel.log"
export FIM_MODEL="${FIM_MODEL:-deepseek-flash}"
run_case cancel
kill "$PROXY_PID" 2>/dev/null || true
wait "$PROXY_PID" 2>/dev/null || true

echo "== degraded (closed port, no proxy) =="
unset FIM_BASE
FIM_BASE="http://127.0.0.1:1" "$BIN" degraded > "$RUN/degraded.log" 2>&1
rc=$?
echo "degraded exit=$rc"
cat "$RUN/degraded.log"

echo "== key leak scan (count only, no literal printed) =="
leak_files=$(grep -rlF --exclude-dir=target --exclude-dir=run --exclude-dir=.cache "$KEY" "$DIR" 2>/dev/null || true)
key_occurrences=$(printf '%s' "$leak_files" | grep -c . || true)
proxy_key=$(cat "$RUN"/proxy-*.log 2>/dev/null | grep -cF "$KEY" || true)
cmdline_key=0
if [ -n "${PROXY_PID:-}" ] && [ -r "/proc/$PROXY_PID/cmdline" ]; then
  if tr '\0' '\n' < "/proc/$PROXY_PID/cmdline" 2>/dev/null | grep -qF "$KEY"; then cmdline_key=1; fi
fi
{
  echo "key_present=true"
  echo "key_length=${#KEY}"
  echo "key_occurrences_in_evidence_tree=$key_occurrences"
  echo "key_occurrences_in_proxy_logs=$proxy_key"
  echo "key_in_proxy_cmdline=$cmdline_key"
  echo "auth_header=Bearer <redacted>"
} > "$RUN/key-scan.txt"
cat "$RUN/key-scan.txt"
if [ "$key_occurrences" -ne 0 ] || [ "$proxy_key" -ne 0 ] || [ "$cmdline_key" -ne 0 ]; then
  echo "KEY LEAK DETECTED" >&2
  exit 1
fi
