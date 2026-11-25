#!/usr/bin/env bash
# Build the OFFLINE VHDL payload that packaging/runtime/assemble-appdir.sh
# installs into the AppDir (T11 / AC-6.5 + AC-6.8):
#
#   vhdl_ls  — static musl binary (upstream release)
#   GHDL     — mcode variant (upstream ubuntu24.04 release, run through the
#              AppDir's own loader)
#   CPython  — built from source with the SAME glibc the AppDir ships
#   VSG      — pure-Python wheel installed into that CPython (PyYAML alongside)
#
# Runs INSIDE the pinned Qt6 image (its glibc is the one the AppDir bundles):
#
#   docker run --rm -v "$PWD:/vedit-build" -w /vedit-build vedit-qt6-build:local \
#     bash packaging/runtime/build-vhdl-payload.sh
#
# Every input is pinned (scripts/versions-vhdl.env) and its sha256 verified; the
# result is a self-contained tree under packaging/runtime/out/vhdl-payload/ that
# the assembler copies without fetching anything.
set -euo pipefail

ROOT=${ROOT:-/vedit-build}
OUT="$ROOT/packaging/runtime/out/vhdl-payload"
CACHE="$ROOT/packaging/runtime/out/vhdl-cache"
LICENCES="$ROOT/packaging/runtime/assets/licenses"

while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT=${2:?}; shift 2 ;;
    --cache) CACHE=${2:?}; shift 2 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

log() { printf '[vhdl-payload] %s\n' "$*"; }

# The pinned image has no `unzip`; the bundled Python's zipfile module extracts
# a wheel/zip exactly (and is the same tool that reads the wheel metadata later).
unzip_to() { python3 -m zipfile -e "$1" "$2"; }

# shellcheck source=../../scripts/versions-vhdl.env
set -a; . "$ROOT/scripts/versions-vhdl.env"; set +a
: "${VHDL_LS_URL:?}" "${VHDL_LS_SHA256:?}"
: "${GHDL_URL:?}" "${GHDL_SHA256:?}"
: "${VHDL_PYTHON_VERSION:?}" "${VHDL_PYTHON_SHA256:?}"
: "${VSG_WHEEL_URL:?}" "${VSG_WHEEL_SHA256:?}"
: "${PYYAML_WHEEL_URL:?}" "${PYYAML_WHEEL_SHA256:?}"

mkdir -p "$CACHE" "$OUT" "$OUT/licenses"

# Pinned download with a hard sha256 check. A cached copy is reused only when it
# already matches; anything else is re-fetched once and then refused.
fetch() {
  local url=$1 sha=$2 dest=$3
  if [ -f "$dest" ] && [ "$(sha256sum "$dest" | cut -d' ' -f1)" = "$sha" ]; then
    log "cached $(basename "$dest") sha256=$sha"
    return 0
  fi
  rm -f "$dest"
  log "fetch $url"
  curl -sSL --retry 3 --retry-delay 2 -o "$dest" "$url" || {
    echo "ERROR: download failed: $url" >&2; return 1; }
  local actual
  actual=$(sha256sum "$dest" | cut -d' ' -f1)
  [ "$actual" = "$sha" ] || {
    echo "ERROR: sha256 mismatch for $dest: expected $sha got $actual" >&2; return 1; }
  log "sha256 OK $(basename "$dest")"
}

# --- GHDL --------------------------------------------------------------------
fetch "$GHDL_URL" "$GHDL_SHA256" "$CACHE/ghdl.tar.gz"
rm -rf "$OUT/ghdl"
mkdir -p "$OUT/ghdl"
tar xzf "$CACHE/ghdl.tar.gz" -C "$OUT/ghdl" --strip-components=1
[ -x "$OUT/ghdl/bin/ghdl" ] || { echo "ERROR: ghdl binary missing" >&2; exit 1; }
log "GHDL $( "$OUT/ghdl/bin/ghdl" --version 2>/dev/null | head -1 )"

# --- vhdl_ls -----------------------------------------------------------------
# The upstream zip ships `bin/vhdl_ls` NEXT TO its `vhdl_libraries/` directory.
# On initialize the binary resolves the installed standard libraries at
# `../vhdl_libraries` relative to its own executable path
# (vhdl_lang/src/config.rs, load_installed_config) and PANICS when that
# directory is absent — `--version` never reads the libraries, so it cannot
# catch this. Keep the upstream relative layout in the payload:
#   $OUT/vhdl_ls/bin/vhdl_ls
#   $OUT/vhdl_ls/vhdl_libraries/{vhdl_ls.toml,std,ieee2008,synopsys,vital2000}
fetch "$VHDL_LS_URL" "$VHDL_LS_SHA256" "$CACHE/vhdl_ls.zip"
rm -rf "$CACHE/vhdl_ls-extract" "$OUT/vhdl_ls"
mkdir -p "$CACHE/vhdl_ls-extract" "$OUT/vhdl_ls/bin"
unzip_to "$CACHE/vhdl_ls.zip" "$CACHE/vhdl_ls-extract"
ls_bin=$(find "$CACHE/vhdl_ls-extract" -type f -name vhdl_ls | head -1)
[ -n "$ls_bin" ] || { echo "ERROR: vhdl_ls binary missing from the release zip" >&2; exit 1; }
ls_libs=$(find "$CACHE/vhdl_ls-extract" -type f -name vhdl_ls.toml | head -1)
[ -n "$ls_libs" ] || { echo "ERROR: vhdl_libraries/vhdl_ls.toml missing from the release zip" >&2; exit 1; }
[ "$(basename "$(dirname "$ls_bin")")" = "bin" ] || {
  echo "ERROR: release zip layout changed: vhdl_ls is not under bin/ (needed for ../vhdl_libraries)" >&2
  exit 1; }
install -m 0755 "$ls_bin" "$OUT/vhdl_ls/bin/vhdl_ls"
cp -a "$(dirname "$ls_libs")" "$OUT/vhdl_ls/vhdl_libraries"
for required in vhdl_ls.toml std ieee2008 synopsys vital2000; do
  [ -e "$OUT/vhdl_ls/vhdl_libraries/$required" ] || {
    echo "ERROR: vhdl_libraries/$required missing from the release zip" >&2; exit 1; }
done
log "vhdl_ls $( "$OUT/vhdl_ls/bin/vhdl_ls" --version 2>/dev/null | head -1 ) (libraries $(du -sh "$OUT/vhdl_ls/vhdl_libraries" | cut -f1))"

# --- CPython (build-time only inputs are not shipped) ------------------------
PY_PREFIX="$OUT/python"
if [ -x "$PY_PREFIX/bin/python3.12" ]; then
  log "CPython cached at $PY_PREFIX"
else
  python_tar="$CACHE/Python-$VHDL_PYTHON_VERSION.tgz"
  if [ ! -f "$python_tar" ] && [ -f "$ROOT/build/slang/cache/Python-$VHDL_PYTHON_VERSION.tgz" ]; then
    cp "$ROOT/build/slang/cache/Python-$VHDL_PYTHON_VERSION.tgz" "$python_tar"
  fi
  fetch "$VHDL_PYTHON_URL" "$VHDL_PYTHON_SHA256" "$python_tar"
  rm -rf "$CACHE/Python-$VHDL_PYTHON_VERSION"
  tar xzf "$python_tar" -C "$CACHE"
  src="$CACHE/Python-$VHDL_PYTHON_VERSION"
  # gcc 15 defaults to C23; CPython 3.12 is C17. The optional extension modules
  # (ssl/sqlite/bz2) are deliberately absent: nothing at runtime needs them, and
  # the wheels are installed from local files, so no pip/TLS is involved.
  (
    cd "$src"
    CFLAGS="-O2 -std=gnu17" ./configure --prefix="$PY_PREFIX" \
      --without-ensurepip --disable-test-modules >/dev/null
    make -j"$(nproc)" >/dev/null
    make install >/dev/null
  )
  log "CPython $("$PY_PREFIX/bin/python3.12" --version 2>&1)"
fi

# PSF-2.0 text ships with the CPython source tree; copy it whatever the build
# cache did (the install guard above may have skipped the whole block).
licence_src="$CACHE/Python-$VHDL_PYTHON_VERSION/LICENSE"
if [ -f "$licence_src" ]; then
  install -m 0644 "$licence_src" "$OUT/licenses/Python-2.0.txt"
else
  tar xOzf "$CACHE/Python-$VHDL_PYTHON_VERSION.tgz" \
    "Python-$VHDL_PYTHON_VERSION/LICENSE" > "$OUT/licenses/Python-2.0.txt"
fi

# --- VSG + PyYAML into that CPython (offline wheels, no pip) ----------------
fetch "$VSG_WHEEL_URL" "$VSG_WHEEL_SHA256" "$CACHE/vsg.whl"
fetch "$PYYAML_WHEEL_URL" "$PYYAML_WHEEL_SHA256" "$CACHE/pyyaml.whl"
site="$PY_PREFIX/lib/python3.12/site-packages"
mkdir -p "$site"
for wheel in "$CACHE/vsg.whl" "$CACHE/pyyaml.whl"; do
  unzip_to "$wheel" "$site"
done
log "VSG $("$PY_PREFIX/bin/python3.12" -c 'import sys; sys.argv[0]="vsg"; from vsg.__main__ import main; sys.exit(main())' -v 2>&1 | head -1)"

# Ship only what a VHDL user needs: no headers, no build config, no IDLE/pydoc.
rm -rf "$PY_PREFIX/include" "$PY_PREFIX/share" "$PY_PREFIX/lib/pkgconfig"
rm -rf "$PY_PREFIX"/lib/python3.12/config-*
rm -rf "$PY_PREFIX"/lib/python3.12/test "$PY_PREFIX"/lib/python3.12/idlelib
rm -f "$PY_PREFIX"/bin/idle3* "$PY_PREFIX"/bin/pydoc3* "$PY_PREFIX"/bin/2to3*

# --- licences ----------------------------------------------------------------
mkdir -p "$OUT/licenses"
install -m 0644 "$LICENCES/MPL-2.0.txt" "$OUT/licenses/vhdl_ls-MPL-2.0.txt"
install -m 0644 "$LICENCES/GPL-2.0.txt" "$OUT/licenses/ghdl-GPL-2.0.txt"
install -m 0644 "$LICENCES/GPL-3.0.txt" "$OUT/licenses/vsg-GPL-3.0.txt"

# --- manifest for the assembler and the evidence -----------------------------
{
  echo "VHDL_LS_VERSION=$VHDL_LS_VERSION"
  echo "VHDL_LS_SHA256=$VHDL_LS_SHA256"
  echo "GHDL_VERSION=$GHDL_VERSION"
  echo "GHDL_SHA256=$GHDL_SHA256"
  echo "VHDL_PYTHON_VERSION=$VHDL_PYTHON_VERSION"
  echo "VHDL_PYTHON_SHA256=$VHDL_PYTHON_SHA256"
  echo "VSG_VERSION=$VSG_VERSION"
  echo "VSG_WHEEL_SHA256=$VSG_WHEEL_SHA256"
  echo "PYYAML_VERSION=$PYYAML_VERSION"
  echo "PYYAML_WHEEL_SHA256=$PYYAML_WHEEL_SHA256"
  echo "PAYLOAD_BYTES=$(du -sb "$OUT" | cut -f1)"
  echo "PAYLOAD_FILES=$(find "$OUT" -type f | wc -l)"
} > "$OUT/PAYLOAD.env"

log "payload: $OUT bytes=$(du -sb "$OUT" | cut -f1) files=$(find "$OUT" -type f | wc -l)"
cat "$OUT/PAYLOAD.env"
