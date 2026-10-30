#!/bin/sh
# VEdit launcher — relocatable, no absolute host paths.
#
# Everything resolves relative to this script's own directory, so the tree can be
# unpacked anywhere (tar.gz) or into a temporary directory (self-extracting .run).
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)

# Per-user runtime directory for the rewritten caches. XDG_RUNTIME_DIR is a
# tmpfs on a normal desktop; /tmp is the fallback. Nothing here is executable.
RUNTIME_BASE=${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}
RUNTIME="$RUNTIME_BASE/vedit-runtime-$(id -u)"
mkdir -p "$RUNTIME"

expand() {
  # Rewrites the @APPDIR@ placeholder in one cache template.
  if [ -f "$HERE/etc/$1.in" ]; then
    sed "s|@APPDIR@|$HERE|g" "$HERE/etc/$1.in" > "$RUNTIME/$1"
  fi
}
expand loaders.cache
expand immodules.cache
expand fonts.conf

export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export GDK_PIXBUF_MODULE_FILE="$RUNTIME/loaders.cache"
if [ -f "$RUNTIME/immodules.cache" ]; then
  export GTK_IM_MODULE_FILE="$RUNTIME/immodules.cache"
fi
export GSETTINGS_SCHEMA_DIR="$HERE/share/glib-2.0/schemas"
export GTK_DATA_PREFIX="$HERE"
export GTK_EXE_PREFIX="$HERE"
export GTK_PATH="$HERE/lib/gtk-3.0"
export FONTCONFIG_FILE="$RUNTIME/fonts.conf"
export FONTCONFIG_PATH="$HERE/etc"
export XDG_DATA_DIRS="$HERE/share:${XDG_DATA_DIRS:-/usr/share}"
# GtkSourceView reads this for the language definitions and style schemes; without
# it the editor would open with no syntax colouring (measured in T11).
export VEDIT_GSV_DATADIR="$HERE/share/gtksourceview-4"
# Bundled tool locations. Package-relative resolution would find them too, but
# naming them explicitly keeps the payload unambiguous and makes the selftest's
# configuration check meaningful.
export VEDIT_VERIBLE_DIR="$HERE/bin"
export VEDIT_SLANG="$HERE/bin/slang"
# The selftest suites need two Verilog fixtures; the packaged copy is used instead
# of the build-tree path compiled into the binary.
export VEDIT_SELFTEST_FIXTURES="$HERE/share/vedit/testdata"

# libdbus aborts in environments whose /etc/machine-id is empty (bare containers).
# The AT-SPI bridge is the only consumer and it is not bundled, so disable it only
# when the machine id is unusable; a normal CentOS 7 desktop keeps accessibility.
if [ -z "${NO_AT_BRIDGE:-}" ]; then
  mid=$(cat /etc/machine-id 2>/dev/null || true)
  if [ "${#mid}" -ne 32 ]; then
    export NO_AT_BRIDGE=1
  fi
fi

# Diagnostics hook (used by the packaging evidence and by bug reports): print the
# environment this launcher would export, then exit without starting the GUI.
if [ "${VEDIT_PRINT_ENV:-0}" = "1" ]; then
  printf 'LAUNCHER_DIR=%s\n' "$HERE"
  printf 'LD_LIBRARY_PATH=%s\n' "$LD_LIBRARY_PATH"
  printf 'GDK_PIXBUF_MODULE_FILE=%s\n' "$GDK_PIXBUF_MODULE_FILE"
  printf 'GTK_IM_MODULE_FILE=%s\n' "${GTK_IM_MODULE_FILE:-<unset>}"
  printf 'GSETTINGS_SCHEMA_DIR=%s\n' "$GSETTINGS_SCHEMA_DIR"
  printf 'XDG_DATA_DIRS=%s\n' "$XDG_DATA_DIRS"
  printf 'VEDIT_GSV_DATADIR=%s\n' "$VEDIT_GSV_DATADIR"
  printf 'FONTCONFIG_FILE=%s\n' "$FONTCONFIG_FILE"
  printf 'VEDIT_VERIBLE_DIR=%s\n' "$VEDIT_VERIBLE_DIR"
  printf 'VEDIT_SLANG=%s\n' "$VEDIT_SLANG"
  printf 'VEDIT_SELFTEST_FIXTURES=%s\n' "$VEDIT_SELFTEST_FIXTURES"
  exit 0
fi

exec "$HERE/bin/vd-app" "$@"
