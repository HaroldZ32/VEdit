#!/usr/bin/env bash
# Build the gtk-rs 0.18 spike app inside the CentOS 7 build image (T5).
# Layout: out/   = build products (binary);  evidence/ = every text/log artifact.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT/app"
OUT="$ROOT/out"
EVID="$ROOT/evidence"
mkdir -p "$OUT" "$EVID"

echo "== environment facts =="
{ echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "centos_release=$(cat /etc/centos-release)"
  echo "glibc=$(rpm -q glibc)"
  echo "gcc=$(gcc --version | head -1)"
  echo "meson=$(meson --version)"
  echo "ninja=$(ninja --version)"
  echo "python3=$(python3 --version 2>&1)"
  echo "gtk3_pkg=$(pkg-config --modversion gtk+-3.0)"
  echo "glib_pkg=$(pkg-config --modversion glib-2.0)"
  echo "gsv4_pkg=$(pkg-config --modversion gtksourceview-4)"
  echo "gsv4_libdir=$(pkg-config --variable=libdir gtksourceview-4)"
  echo "gtk3_rpm=$(rpm -q gtk3)"
  echo "language_specs=$(ls /opt/gsv4/share/gtksourceview-4/language-specs/*.lang | wc -l)"
} | tee "$EVID/env-facts.txt"

echo "== gsv4 artifact + its own GLIBC floor =="
GSV_LIB="$(pkg-config --variable=libdir gtksourceview-4)/libgtksourceview-4.so.0"
{ echo "command: ls -l $GSV_LIB && readelf --version-info $GSV_LIB"
  ls -l "$GSV_LIB"
  echo "max GLIBC symbol version required by libgtksourceview-4.so.0:"
  readelf --version-info "$GSV_LIB" | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1
} | tee "$EVID/gsv4-lib.txt"

echo "== toolchain =="
{ rustc --version; cargo --version; gcc --version | head -1; } | tee "$EVID/rustc-version.txt"
{ echo "gtk+-3.0 $(pkg-config --modversion gtk+-3.0)"
  echo "gtksourceview-4 $(pkg-config --modversion gtksourceview-4)"
} | tee "$EVID/pkg-versions.txt"

echo "== cargo fmt / clippy / build (gtk-rs 0.18 + sourceview4 0.5) =="
cd "$APP_DIR"
# Keep build-machine paths out of the shipped ELF (Rust embeds panic locations).
export RUSTFLAGS="${RUSTFLAGS:-} --remap-path-prefix=/root/.cargo=/cargo --remap-path-prefix=/work=/vedit-build"
cargo fmt -- --check > "$EVID/cargo-fmt.log" 2>&1 || { echo "cargo fmt --check FAILED (see evidence/cargo-fmt.log)"; cat "$EVID/cargo-fmt.log"; exit 1; }
echo "cargo fmt --check: exit 0" | tee "$EVID/cargo-fmt.log"
cargo clippy --release -- -D warnings 2>&1 | tee "$EVID/cargo-clippy.log"
cargo build --release 2>&1 | tee "$EVID/cargo-build.log"
BIN="$APP_DIR/target/release/vedit-gui-spike"
test -x "$BIN"
echo "BIN=$BIN"
ldd "$BIN" | tee "$EVID/bin-ldd.txt"
{ echo "command: readelf --version-info $BIN"
  echo "max GLIBC symbol version required by the spike binary:"
  readelf --version-info "$BIN" | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1
} | tee "$EVID/bin-glibc-floor.txt"
cp "$BIN" "$EVID/vedit-gui-spike.bin"
cp Cargo.lock "$EVID/Cargo.lock"
cargo tree -e normal --depth 1 | tee "$EVID/cargo-tree-depth1.txt"
grep -E '^name = "(gtk|gtk-sys|glib|gdk|gdk-pixbuf|cairo-rs|sourceview4|sourceview4-sys)"' -A1 Cargo.lock | tee "$EVID/cargo-lock-versions.txt"
cp "$BIN" "$OUT/vedit-gui-spike"
sha256sum "$OUT/vedit-gui-spike" | tee "$EVID/bin-sha256.txt"
echo "BUILD APP OK"
