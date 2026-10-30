#!/usr/bin/env bash
# Build the slang SystemVerilog compiler/linter so that it runs on CentOS 7
# (glibc 2.17) without any extra runtime libraries.
#
# One command, from a clean tree on any host with docker:
#     ./build.sh
#
# What it does:
#   1. runs scripts/build-in-container.sh inside a FRESH
#      docker.m.daocloud.io/library/centos:7 container (no host toolchain used);
#   2. asserts the produced ELF needs no symbol newer than GLIBC_2.17 and does
#      not depend on libstdc++/libgcc_s;
#   3. re-runs the artifact in a second fresh centos:7 container WITH NETWORK
#      DISABLED: `--version`, plus `--lint-only -Weverything --diag-json` on a
#      fixture with an undeclared identifier, asserting a JSON array.
#
# Outputs:
#   out/slang              the statically-libstdc++ binary (build result)
#   evidence/*.log|txt     raw command output of the build and the checks
#   REPORT.md              human summary (written separately, cites evidence/)
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=versions.env
set -a
source "$HERE/versions.env"
set +a

IMAGE=${SLANG_BUILD_IMAGE:-docker.m.daocloud.io/library/centos:7}
OBJDUMP=${OBJDUMP:-objdump}

mkdir -p "$HERE/out" "$HERE/evidence" "$HERE/cache"

image_present() { docker image inspect "$IMAGE" >/dev/null 2>&1; }
if ! image_present; then
  echo "== pulling $IMAGE =="
  docker pull "$IMAGE"
fi

echo "== stage 1: build inside a fresh $IMAGE container =="
set +e
docker run --rm \
  --label vedit-build=slang \
  -e SLANG_VERSION -e SLANG_TAG -e SLANG_REPO -e SLANG_COMMIT \
  -e CMAKE_VERSION -e CMAKE_URL -e CMAKE_SHA256 \
  -e PYTHON_VERSION -e PYTHON_URL -e PYTHON_SHA256 \
  -e CENTOS_VAULT_BASE \
  -v "$HERE:/work" \
  -w /work \
  "$IMAGE" /bin/bash /work/scripts/build-in-container.sh \
  2>&1 | tee "$HERE/evidence/00-build-console.log"
build_rc=${PIPESTATUS[0]}
set -e
if [ "$build_rc" -ne 0 ]; then
  echo "FAIL: in-container build exited $build_rc (see evidence/00-build-console.log)" >&2
  exit "$build_rc"
fi

BIN="$HERE/out/slang"
if [ ! -x "$BIN" ]; then
  echo "FAIL: $BIN missing or not executable" >&2
  exit 1
fi

echo "== stage 2: ELF checks on the host =="
elf_report="$HERE/evidence/05-elf-check.txt"
{
  echo "# generated $(date -u +%Y-%m-%dT%H:%M:%SZ) by build.sh stage 2"
  "$OBJDUMP" -p "$BIN" | sed -n '1,20p'
  echo "## highest versioned GLIBC symbol required:"
  "$OBJDUMP" -T "$BIN" | grep -o 'GLIBC_[0-9.]*' | sort -V -u | tail -1
  echo "## all GLIBC versions referenced:"
  "$OBJDUMP" -T "$BIN" | grep -o 'GLIBC_[0-9.]*' | sort -V -u | tr '\n' ' '
  echo
  echo "## non-GLIBC versioned symbols (expected: none):"
  "$OBJDUMP" -T "$BIN" | grep -o 'GLIBCXX_[0-9.]*\|CXXABI_[0-9.]*\|GCC_[0-9.]*' | sort -u || echo "(none)"
  echo "## NEEDED entries:"
  "$OBJDUMP" -p "$BIN" | sed -n '/NEEDED/p'
  echo "## sha256 / size:"
  sha256sum "$BIN"
  stat -c 'size_bytes=%s' "$BIN"
} > "$elf_report" 2>&1
cat "$elf_report"

max_glibc=$("$OBJDUMP" -T "$BIN" | grep -o 'GLIBC_[0-9.]*' | sort -V -u | tail -1)
if [ "$(printf '%s\n%s\n' "$max_glibc" "GLIBC_2.17" | sort -V | tail -1)" != "GLIBC_2.17" ]; then
  echo "FAIL: artifact requires $max_glibc, newer than the CentOS 7 baseline GLIBC_2.17" >&2
  exit 1
fi
if "$OBJDUMP" -p "$BIN" | sed -n '/NEEDED/p' | grep -qE 'libstdc\+\+|libgcc_s'; then
  echo "FAIL: artifact still depends on libstdc++/libgcc_s" >&2
  exit 1
fi
echo "OK: highest GLIBC symbol is $max_glibc (<= 2.17); no libstdc++/libgcc_s dependency"

echo "== stage 3: offline smoke test in a second fresh $IMAGE container =="
set +e
docker run --rm \
  --network none \
  -e SLANG_VERSION \
  -v "$HERE/out:/out:ro" \
  -v "$HERE/fixtures:/fixtures:ro" \
  -v "$HERE/evidence:/evidence" \
  -v "$HERE/scripts:/scripts:ro" \
  "$IMAGE" /bin/bash /scripts/verify-offline.sh \
  2>&1 | tee "$HERE/evidence/06-offline-console.log"
smoke_rc=${PIPESTATUS[0]}
set -e
if [ "$smoke_rc" -ne 0 ]; then
  echo "FAIL: offline smoke test exited $smoke_rc (see evidence/06-offline-console.log)" >&2
  exit "$smoke_rc"
fi

echo
echo "BUILD OK: $(sha256sum "$BIN")"
echo "artifact: $BIN"
echo "evidence: $HERE/evidence/"
