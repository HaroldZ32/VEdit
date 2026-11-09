#!/usr/bin/env bash
# Reproduce both halves of the AC-1.5 / AC-3.0 spike in one command.
#
#   1. build `slang-server` (hudson-trading/slang-server v0.3.1) for CentOS 7 /
#      glibc 2.17 (source build; the upstream prebuilt needs GLIBC_2.27);
#   2. run it in a CLEAN centos:7 container: `--version`;
#   3. a real `initialize` handshake — the server runs INSIDE the container, the
#      LSP client stays on the host; the fixture is mounted at /fixture and the
#      client announces exactly that `rootUri` (both are carried by this script);
#   4. elaboration on the fixed fixture (top instantiates mid twice, the signal is
#      driven in mid and loaded in top): instance hierarchy + signal driver/load,
#      with wall time and peak RSS; 4b captures the same information through
#      slang-server's own LSP commands.
#
# Every step's exit code is printed as `stepN_exit=…`, and the script exits
# non-zero if any step failed — so "it ran" is verifiable from the log alone.
#
#   bash probes/slang-spike/run-spike.sh
#   EV=<dir> WORK=<dir> bash probes/slang-spike/run-spike.sh   # override outputs
#
# Needs docker + network. A clean checkout builds (≈440 s on 16 cores); once
# $WORK/out/slang-server exists the build step is reused.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
PROBE_DIR=$ROOT/probes/slang-spike
WORK=${WORK:-$ROOT/target/tmp/spike-work}
IMAGE=${SPIKE_IMAGE:-docker.m.daocloud.io/library/centos:7}
EV=${EV:-$ROOT/evidence/w2-spike}
FIXTURE_MOUNT=/fixture
mkdir -p "$WORK/cache" "$WORK/out" "$WORK/buildfiles" "$EV"
# Build-file scratch is recreated per run so no stale .f can be mistaken for input.
rm -f "$WORK"/buildfiles/*.f
export SPIKE_BUILD_FILE_DIR="$WORK/buildfiles"

step() { printf '\n== %s\n' "$*"; }

step "1/4 build slang-server for CentOS 7 (source)"
cp "$PROBE_DIR/build-sluserver-centos7.sh" "$PROBE_DIR/gcc11_path_hash.h" "$WORK/"
if [ -x "$WORK/out/slang-server" ]; then
  echo "reusing $WORK/out/slang-server ($(sha256sum "$WORK/out/slang-server" | cut -d' ' -f1))"
else
  for archive in cmake-3.31.6-linux-x86_64.tar.gz Python-3.12.8.tgz; do
    [ -f "$WORK/cache/$archive" ] || cp "$ROOT/build/slang/cache/$archive" "$WORK/cache/"
  done
  docker run --rm \
    -e SLANG_SERVER_TAG=v0.3.1 \
    -e SLANG_SERVER_REPO=https://github.com/hudson-trading/slang-server.git \
    -e SLANG_SERVER_COMMIT=2ad996ffb7388e4f231736609e9d91f87d9efedb \
    -e CMAKE_VERSION=3.31.6 -e PYTHON_VERSION=3.12.8 \
    -e CENTOS_VAULT_BASE=https://mirrors.tuna.tsinghua.edu.cn/centos-vault/7.9.2009 \
    -v "$WORK:/work" -w /work "$IMAGE" bash /work/build-sluserver-centos7.sh
fi

# From here on the steps are measured, not fatal-on-first-error: each exit code is
# printed and the script reports a single verdict at the end.
set +e

step "2/4 --version in a clean CentOS 7 (glibc 2.17)"
docker run --rm -v "$WORK/out:/w" -w /w "$IMAGE" /w/slang-server --version \
  2>&1 | tee "$EV/03-built-centos7-version.txt"
rc2=${PIPESTATUS[0]}
echo "exit=$rc2" >> "$EV/03-built-centos7-version.txt"
echo "step2_exit=$rc2"

step "3/4 real initialize handshake (server inside the container, client on the host)"
echo "mounts: $WORK/out:/w (binary) and $PROBE_DIR/fixture:$FIXTURE_MOUNT (workspace)"
python3 "$PROBE_DIR/lsp_initialize.py" \
  --server /w/slang-server \
  --wrapper "docker run -i --rm -v $WORK/out:/w -v $PROBE_DIR/fixture:$FIXTURE_MOUNT -w /w $IMAGE" \
  --root-uri "file://$FIXTURE_MOUNT" \
  --root "$PROBE_DIR/fixture" \
  2>&1 | tee "$EV/04-built-initialize-handshake.txt"
rc3=${PIPESTATUS[0]}
echo "exit=$rc3" >> "$EV/04-built-initialize-handshake.txt"
echo "step3_exit=$rc3"

step "4/4 hierarchical elaboration on the fixture (CLI: hierarchy + driver/load)"
python3 "$PROBE_DIR/elab_probe.py" \
  --slang "$ROOT/build/slang/out/slang" \
  --fixture "$PROBE_DIR/fixture" \
  --out "$EV/05-elaboration.json" \
  2>&1 | tee "$EV/05-elaboration-console.txt"
rc4=${PIPESTATUS[0]}
echo "exit=$rc4" >> "$EV/05-elaboration-console.txt"
echo "step4_exit=$rc4"

step "4b/4 hierarchy + driver/load through slang-server's own LSP commands"
python3 "$PROBE_DIR/lsp_hierarchy_probe.py" \
  --server "$WORK/out/slang-server" \
  --root "$PROBE_DIR/fixture" \
  2>&1 | tee "$EV/06-hierarchy-lsp-shape.txt"
rc4b=${PIPESTATUS[0]}
echo "exit=$rc4b" >> "$EV/06-hierarchy-lsp-shape.txt"
echo "step4b_exit=$rc4b"

step "4c/4 measured build-file rule (file location vs entry style vs server cwd)"
python3 "$PROBE_DIR/buildfile_rule_probe.py" \
  --server "$WORK/out/slang-server" \
  --root "$PROBE_DIR/fixture" \
  --out "$EV/07-buildfile-rule.json" \
  2>&1 | tee "$EV/07-buildfile-rule.txt"
rc4c=${PIPESTATUS[0]}
echo "step4c_exit=$rc4c"

echo
echo "verdict: step2=$rc2 step3=$rc3 step4=$rc4 step4b=$rc4b step4c=$rc4c"
if [ "$rc2" -ne 0 ] || [ "$rc3" -ne 0 ] || [ "$rc4" -ne 0 ] || [ "$rc4b" -ne 0 ] || [ "$rc4c" -ne 0 ]; then
  echo "SPIKE REPRODUCTION FAILED"
  exit 1
fi
echo "SPIKE REPRODUCTION PASSED — evidence under $EV"
