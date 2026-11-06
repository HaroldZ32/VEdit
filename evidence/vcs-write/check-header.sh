#!/usr/bin/env bash
# cbindgen byte-identity probe (AC-3.2). Runs INSIDE the pinned Rust image.
#
# `crates/vd-ffi/build.rs` regenerates include/vd_ffi.h on every build. This
# probe snapshots the consumed header, forces the build script to run again
# (mtime-only touch, the same technique the typecheck gate documents), and then
# compares the regenerated file byte-for-byte.
set -euo pipefail

WORKSPACE=/vedit-build
if [ "$(pwd -P)" = "$WORKSPACE" ]; then
  echo "FATAL: the probe must not run inside the VEdit working tree ($WORKSPACE)" >&2
  exit 1
fi

HEADER="$WORKSPACE/crates/vd-ffi/include/vd_ffi.h"
SNAPSHOT=$(mktemp /tmp/vedit-vd-ffi-header-XXXXXX.h)
cp "$HEADER" "$SNAPSHOT"
echo "# cbindgen header probe"
echo "cwd=$(pwd -P)"
echo "header=$HEADER"
echo "snapshot=$SNAPSHOT"
echo "tracked_bytes=$(stat -c '%s' "$HEADER")"
echo "tracked_sha256=$(sha256sum "$HEADER" | cut -d' ' -f1)"

echo "== force the build script (cbindgen) to run again =="
touch "$WORKSPACE/crates/vd-ffi/build.rs"
cargo build --manifest-path "$WORKSPACE/Cargo.toml" -p vd-ffi

echo "regenerated_bytes=$(stat -c '%s' "$HEADER")"
echo "regenerated_sha256=$(sha256sum "$HEADER" | cut -d' ' -f1)"
if cmp -s "$SNAPSHOT" "$HEADER"; then
  echo "header_byte_identical=yes"
else
  echo "header_byte_identical=no" >&2
  diff -u "$SNAPSHOT" "$HEADER" >&2 || true
  exit 1
fi

BUILT_COPY="$WORKSPACE/evidence/ffi-abi/logs/26-vd_ffi.h"
if [ -f "$BUILT_COPY" ]; then
  if cmp -s "$HEADER" "$BUILT_COPY"; then
    echo "build_qt_copy_identical=yes ($BUILT_COPY)"
  else
    echo "build_qt_copy_identical=no ($BUILT_COPY)" >&2
    diff -u "$HEADER" "$BUILT_COPY" >&2 || true
    exit 1
  fi
else
  echo "build_qt_copy_identical=skipped (no $BUILT_COPY yet)"
fi
