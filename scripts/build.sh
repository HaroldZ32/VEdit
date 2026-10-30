#!/usr/bin/env bash
# One command: build VEdit for CentOS 7 (glibc 2.17) and prove it.
#
#   bash scripts/build.sh
#
# Stages:
#   1. build the pinned CentOS 7 build image (digest-pinned base, sha256-pinned
#      downloads, GtkSourceView 4.6.1 from source, Rust 1.99.0);
#   2. cargo build --release --workspace inside that image, then run the Xvfb
#      main-window smoke test (`--selftest smoke`);
#   3. assert on the host that the produced ELF needs no symbol newer than
#      GLIBC_2.17, and record its sha256;
#   4. record the external inputs the packaging lanes consume (the T3 slang
#      binary; never rebuilt here).
#
# Evidence lands in build/evidence/ (path contains "evidence/" on purpose, so a
# verifier can read it without reading implementation).
set -euo pipefail

# shellcheck source=lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
load_versions

EVID="$ROOT/build/evidence"
ARTIFACTS="$ROOT/build/artifacts"
mkdir -p "$EVID" "$ARTIFACTS"

echo "== pinned inputs ==" | tee "$EVID/00-pins.txt"
grep -E '^[A-Z][A-Z0-9_]*=' "$VERSIONS_FILE" | sed 's/^/  /' | tee -a "$EVID/00-pins.txt"

echo "== stage 1/4: pinned build image =="
build_build_image "$EVID/01-image-build.log"
IMAGE_ID=$(docker image inspect "$BUILD_IMAGE_TAG" --format '{{.Id}}')
{
  echo "image_tag=$BUILD_IMAGE_TAG"
  echo "image_id=$IMAGE_ID"
  echo "base_ref=$(image_ref)"
} | tee "$EVID/02-image-id.txt"

# Reproducibility: SOURCE_DATE_EPOCH is taken from the commit on the HOST (the
# container receives no .git) and must be non-empty and numeric. It changes the
# canonical bytes, so it must be set before a freeze, never after.
if [ -z "${SOURCE_DATE_EPOCH:-}" ]; then
  SOURCE_DATE_EPOCH=$(git -C "$ROOT" show -s --format=%ct HEAD 2>/dev/null || true)
  export SOURCE_DATE_EPOCH
fi
case "${SOURCE_DATE_EPOCH:-}" in
  ''|*[!0-9]*)
    echo "FATAL: SOURCE_DATE_EPOCH must be a non-empty numeric epoch (got '${SOURCE_DATE_EPOCH:-}')" >&2
    exit 1
    ;;
esac
{
  echo "source_date_epoch=$SOURCE_DATE_EPOCH"
  echo "source_date_epoch_source=${SOURCE_DATE_EPOCH_SOURCE:-host:git show -s --format=%ct HEAD}"
  echo "cargo_incremental=0 (set inside the container)"
  echo "remap_flags=$(grep '^RELEASE_RUSTFLAGS=' "$VERSIONS_FILE")"
} | tee "$EVID/14-determinism-env.txt"

echo "== stage 2/4: release build + Xvfb smoke in the image =="
run_in_build_container bash scripts/build-in-container.sh 2>&1 | tee "$EVID/03-build-console.log"

BIN="$ROOT/target/release/vd-app"
if [ ! -x "$BIN" ]; then
  echo "FAIL: $BIN was not produced" >&2
  exit 1
fi

echo "== stage 3/4: ELF assertions (host tools) =="
ELF_REPORT="$EVID/04-elf-check.txt"
{
  echo "# generated $(date -u +%Y-%m-%dT%H:%M:%SZ) by scripts/build.sh"
  echo '$ file target/release/vd-app'
  file "$BIN"; echo "exit=$?"
  echo
  echo '$ readelf -h | Class/Machine/Type'
  readelf -h "$BIN" | sed -n '/Class:\|Machine:\|Type:/p'
  echo
  echo '$ readelf -d | NEEDED'
  readelf -d "$BIN" | sed -n '/NEEDED/p'
  echo
  echo '$ readelf -V | grep -o GLIBC_[0-9.]* | sort -V -u'
  readelf -V "$BIN" | grep -o 'GLIBC_[0-9.]*' | sort -V -u
  echo
  echo '$ readelf -V | grep -o GLIBC_[0-9.]* | sort -V -u | tail -1'
  readelf -V "$BIN" | grep -o 'GLIBC_[0-9.]*' | sort -V -u | tail -1
  echo
  echo '$ objdump -T | grep -o GLIBC_[0-9.]* | sort -V -u | tail -1'
  objdump -T "$BIN" | grep -o 'GLIBC_[0-9.]*' | sort -V -u | tail -1
  echo
  echo '$ sha256sum target/release/vd-app'
  sha256sum "$BIN"
  stat -c 'size_bytes=%s' "$BIN"
} > "$ELF_REPORT" 2>&1
cat "$ELF_REPORT"

MAX_GLIBC=$(readelf -V "$BIN" | grep -o 'GLIBC_[0-9.]*' | sort -V -u | tail -1)
if [ -z "$MAX_GLIBC" ]; then
  echo "FAIL: no GLIBC versioned symbols found (unexpected)" >&2
  exit 1
fi
if [ "$(printf '%s\n%s\n' "$MAX_GLIBC" "GLIBC_2.17" | sort -V | tail -1)" != "GLIBC_2.17" ]; then
  echo "FAIL: artifact requires $MAX_GLIBC, newer than the CentOS 7 baseline GLIBC_2.17" >&2
  exit 1
fi
if readelf -d "$BIN" | sed -n '/NEEDED/p' | grep -qE 'libstdc\+\+'; then
  echo "FAIL: artifact depends on libstdc++" >&2
  exit 1
fi
echo "OK: highest GLIBC symbol is $MAX_GLIBC (<= 2.17); no libstdc++ dependency"

cp "$BIN" "$ARTIFACTS/vd-app"
chmod 755 "$ARTIFACTS/vd-app"
{
  echo "artifact=build/artifacts/vd-app"
  sha256sum "$ARTIFACTS/vd-app"
  stat -c 'size_bytes=%s' "$ARTIFACTS/vd-app"
} | tee "$EVID/05-artifact-copy.txt"

echo "== stage 4/4: external inputs (not rebuilt here) =="
{
  echo "# External artifacts consumed by this project"
  echo "slang_expected_sha256=${SLANG_BIN_SHA256:-<unset>}"
  if [ -x "$ROOT/build/slang/out/slang" ]; then
    echo "slang_path=build/slang/out/slang"
    sha256sum "$ROOT/build/slang/out/slang"
    "$ROOT/build/slang/out/slang" --version || true
  else
    echo "slang_path=MISSING (run: bash build/slang/build.sh)"
  fi
} | tee "$EVID/06-external-inputs.txt"

echo
echo "BUILD OK: $(sha256sum "$BIN" | cut -d' ' -f1)"
echo "binary:   $BIN (copy: $ARTIFACTS/vd-app)"
echo "evidence: $EVID/"
