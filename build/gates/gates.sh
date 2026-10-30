#!/usr/bin/env bash
# Gate `gates`: one-command build of the artifact of record, then the CentOS 7
# (glibc 2.17) ELF assertions:
#   * the highest versioned GLIBC symbol required is <= GLIBC_2.17 (readelf -V);
#   * no NEEDED entry pulls in libstdc++ / libgcc_s (readelf -d).
# The build command itself is build/slang/build.sh: a fresh centos:7 container
# produces build/slang/out/slang. Re-run with GATES_REBUILD=1 to force it.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

BIN="$ROOT/build/slang/out/slang"
if [ ! -x "$BIN" ] || [ "${GATES_REBUILD:-0}" = "1" ]; then
  echo "[gates] artifact missing or GATES_REBUILD=1 -> one-command build: build/slang/build.sh"
  bash "$ROOT/build/slang/build.sh"
else
  echo "[gates] artifact present, re-checking it (GATES_REBUILD=1 rebuilds): $BIN"
fi
[ -x "$BIN" ] || no_gate "[gates] FAIL: $BIN missing or not executable"

command -v readelf >/dev/null 2>&1 || no_gate "[gates] FAIL: readelf is not available on this host"

echo "[gates] readelf -d NEEDED entries:"
readelf -d "$BIN" | sed -n '/NEEDED/p'
if readelf -d "$BIN" | sed -n '/NEEDED/p' | grep -qE 'libstdc\+\+|libgcc_s'; then
  no_gate "[gates] FAIL: binary still depends on libstdc++/libgcc_s"
fi

max_glibc=$(readelf -V "$BIN" | grep -o 'GLIBC_[0-9.]*' | sort -V -u | tail -1)
echo "[gates] highest GLIBC symbol version required: ${max_glibc:-<none>}"
[ -n "$max_glibc" ] || no_gate "[gates] FAIL: no GLIBC versioned symbols found (unexpected for a glibc ELF)"
if [ "$(printf '%s\n%s\n' "$max_glibc" "GLIBC_2.17" | sort -V | tail -1)" != "GLIBC_2.17" ]; then
  no_gate "[gates] FAIL: $max_glibc is newer than the CentOS 7 baseline GLIBC_2.17"
fi
echo "[gates] sha256: $(sha256sum "$BIN" | cut -d' ' -f1)"

# The MPD gate table reaches `gates` but NOT `tests`: its fixed argv for `tests`
# is `bun test packages`, which a Rust product can never satisfy. `gates` is the
# only reachable id that may run a heavy command, so the Rust test suite is
# folded in here (its result is what the ledger sees).
if [ -f "$ROOT/Cargo.toml" ]; then
  echo "[gates] Cargo workspace present -> cargo test --workspace is a HARD requirement"
  ( cd "$ROOT" && cargo test --workspace ) \
    || no_gate "[gates] FAIL: cargo test --workspace failed"
else
  echo "[gates] N/A: no Cargo workspace yet (T7 owns the Rust skeleton); the test step has nothing to run."
  echo "[gates] this N/A is only valid before T7: once Cargo.toml exists, a test failure fails this gate."
fi
echo "[gates] PASS"
