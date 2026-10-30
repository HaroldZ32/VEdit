#!/usr/bin/env bash
# Assemble a self-contained AppDir (GTK3 runtime + GtkSourceView 4 + app) and emit
# both T5 deliverables: an AppImage and a relocatable tar.gz. Every audit line goes
# to evidence/; out/ receives only the finished products.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/out"
EVID="$ROOT/evidence"
APPDIR="$ROOT/AppDir"
BIN="$OUT/vedit-gui-spike"
GSV_PREFIX=/opt/gsv4
VERSION=0.1.0
NAME="VEdit-Spike-${VERSION}-x86_64"
mkdir -p "$OUT" "$EVID"
test -x "$BIN"
rm -rf "$APPDIR"
mkdir -p "$APPDIR"/usr/bin "$APPDIR"/usr/lib "$APPDIR"/usr/share "$APPDIR"/etc
cp "$BIN" "$APPDIR/usr/bin/vedit-gui-spike"

echo "== collect GTK3 runtime closure (recursive ldd) =="
# Host-OS-only libraries: glibc itself and the C/C++ runtime. Bundling these is the
# classic way to break an AppImage on the target machine.
EXCLUDE_RE='^/(lib64|usr/lib64)/(ld-linux[^/]*|libc|libm|libmvec|libpthread|libdl|librt|libresolv|libnsl|libcrypt|libutil|libanl|libBrokenLocale|libthread_db|libnss_[^/]*|libgcc_s|libstdc\+\+)\.so'

declare -A SEEN=()
QUEUE=("$BIN")
while [ ${#QUEUE[@]} -gt 0 ]; do
  cur="${QUEUE[0]}"
  QUEUE=("${QUEUE[@]:1}")
  while IFS= read -r lib; do
    [ -n "$lib" ] || continue
    [ -f "$lib" ] || continue
    if [[ "$lib" =~ $EXCLUDE_RE ]]; then continue; fi
    key="$(basename "$lib")"
    if [ -n "${SEEN[$key]:-}" ]; then continue; fi
    SEEN[$key]="$lib"
    cp -L "$lib" "$APPDIR/usr/lib/$key"
    QUEUE+=("$lib")
  done < <(ldd "$cur" 2>/dev/null | awk '/=> \//{print $3} /^\t\/[^ ]+ \(/{print $1}')
done
{
  echo "# GTK3 runtime + transitive closure bundled into the AppDir"
  echo "# method: recursive ldd from the spike binary, excluding glibc/libgcc/libstdc++"
  echo "# count: ${#SEEN[@]}"
  for key in $(printf '%s\n' "${!SEEN[@]}" | sort); do printf '%s\t%s\n' "$key" "${SEEN[$key]}"; done
} | tee "$EVID/bundled-libs.txt"

echo "== GtkSourceView 4 data (language specs + styles) =="
mkdir -p "$APPDIR/usr/share/gtksourceview-4"
cp -a "$GSV_PREFIX/share/gtksourceview-4/language-specs" "$APPDIR/usr/share/gtksourceview-4/"
cp -a "$GSV_PREFIX/share/gtksourceview-4/styles" "$APPDIR/usr/share/gtksourceview-4/"
ls "$APPDIR/usr/share/gtksourceview-4/language-specs" | grep -E '^(verilog|systemverilog)\.lang$' | tee "$EVID/gsv4-languages.txt"

echo "== gdk-pixbuf loaders =="
LOADERS_SRC=/usr/lib64/gdk-pixbuf-2.0/2.10.0/loaders
mkdir -p "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders"
cp -a "$LOADERS_SRC"/*.so "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/"
gdk-pixbuf-query-loaders-64 "$APPDIR"/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so \
  | sed "s|$APPDIR|@APPDIR@|g" > "$APPDIR/etc/loaders.cache.in"
{ echo "loaders bundled: $(ls "$APPDIR"/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so | wc -l)"
  echo "cache file: etc/loaders.cache.in (paths hold an @APPDIR@ placeholder, rewritten by AppRun)"
  ls "$APPDIR"/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/; } | tee "$EVID/gdk-pixbuf-loaders.txt"

echo "== GTK input-method modules =="
IMMOD_SRC=/usr/lib64/gtk-3.0/3.0.0/immodules
if compgen -G "$IMMOD_SRC/*.so" > /dev/null; then
  mkdir -p "$APPDIR/usr/lib/gtk-3.0/3.0.0/immodules"
  cp -a "$IMMOD_SRC"/*.so "$APPDIR/usr/lib/gtk-3.0/3.0.0/immodules/"
  gtk-query-immodules-3.0-64 "$APPDIR"/usr/lib/gtk-3.0/3.0.0/immodules/*.so \
    | sed "s|$APPDIR|@APPDIR@|g" > "$APPDIR/etc/immodules.cache.in"
fi
( cd "$APPDIR/usr/lib/gtk-3.0/3.0.0/immodules" 2>/dev/null && ls -1 *.so ) | tee "$EVID/gtk-immodules.txt" || true

echo "== GSettings schemas =="
mkdir -p "$APPDIR/usr/share/glib-2.0/schemas"
# Only the schemas this payload owns (GTK3 + GtkSourceView). Desktop-environment
# schemas from other packages reference enums this bundle does not ship.
cp -a /usr/share/glib-2.0/schemas/org.gtk.*.gschema.xml "$APPDIR/usr/share/glib-2.0/schemas/"
cp -a "$GSV_PREFIX"/share/glib-2.0/schemas/*.gschema.xml "$APPDIR/usr/share/glib-2.0/schemas/" 2>/dev/null || true
glib-compile-schemas "$APPDIR/usr/share/glib-2.0/schemas"
{ echo "schemas: $(ls "$APPDIR/usr/share/glib-2.0/schemas"/*.gschema.xml | wc -l)"
  ls "$APPDIR/usr/share/glib-2.0/schemas"; } | tee "$EVID/gsettings-schemas.txt"

echo "== themes, icons, fonts, fontconfig =="
mkdir -p "$APPDIR/usr/share/themes" "$APPDIR/usr/share/icons"
for t in Adwaita HighContrast HighContrastInverse; do
  [ -d "/usr/share/themes/$t" ] && cp -a "/usr/share/themes/$t" "$APPDIR/usr/share/themes/"
done
# Adwaita icon theme: keep the raster sizes a GTK3 app actually uses; the 12 MB
# cursor theme, the 256/512 px sizes and the SVG source set are not needed by the
# spike and are excluded (T11 can widen this list for the full editor).
for i in hicolor; do
  [ -d "/usr/share/icons/$i" ] && cp -a "/usr/share/icons/$i" "$APPDIR/usr/share/icons/"
done
if [ -d /usr/share/icons/Adwaita ]; then
  mkdir -p "$APPDIR/usr/share/icons/Adwaita"
  for size in 16x16 22x22 24x24 32x32 48x48; do
    [ -d "/usr/share/icons/Adwaita/$size" ] && cp -a "/usr/share/icons/Adwaita/$size" "$APPDIR/usr/share/icons/Adwaita/"
  done
  [ -f /usr/share/icons/Adwaita/index.theme ] && cp /usr/share/icons/Adwaita/index.theme "$APPDIR/usr/share/icons/Adwaita/"
fi
mkdir -p "$APPDIR/usr/share/fonts/dejavu" "$APPDIR/etc"
cp -a /usr/share/fonts/dejavu/*.ttf "$APPDIR/usr/share/fonts/dejavu/"
cat > "$APPDIR/etc/fonts.conf.in" <<'EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>@APPDIR@/usr/share/fonts</dir>
  <cachedir>/tmp/vedit-fontconfig-cache</cachedir>
</fontconfig>
EOF
{ echo "themes: $(ls "$APPDIR/usr/share/themes" 2>/dev/null | tr '\n' ' ')"
  echo "icons:  $(ls "$APPDIR/usr/share/icons" 2>/dev/null | tr '\n' ' ')"
  echo "fonts:  $(ls "$APPDIR/usr/share/fonts/dejavu" 2>/dev/null | tr '\n' ' ')"
} | tee "$EVID/gtk-runtime-assets.txt"

echo "== launcher (AppRun) =="
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
# Relocatable launcher for the VEdit GTK3 spike (tested on CentOS 7, glibc 2.17).
# The payload is self-contained: GTK3, GtkSourceView 4, gdk-pixbuf loaders, schemas,
# fonts and themes all resolve inside $HERE.
set -e
HERE=$(dirname "$(readlink -f "$0")")
RUNTIME="${TMPDIR:-/tmp}/vedit-spike-runtime"
mkdir -p "$RUNTIME"
sed "s|@APPDIR@|$HERE|g" "$HERE/etc/loaders.cache.in" > "$RUNTIME/loaders.cache"
sed "s|@APPDIR@|$HERE|g" "$HERE/etc/fonts.conf.in" > "$RUNTIME/fonts.conf"
if [ -f "$HERE/etc/immodules.cache.in" ]; then
  sed "s|@APPDIR@|$HERE|g" "$HERE/etc/immodules.cache.in" > "$RUNTIME/immodules.cache"
fi
# libdbus aborts in environments whose /etc/machine-id is empty (bare containers).
# The AT-SPI bridge is the only consumer here and it is not bundled, so disable it
# only when the machine-id is unusable; a normal CentOS 7 desktop keeps a11y.
mid=$(cat /etc/machine-id 2>/dev/null || true)
if [ "${#mid}" -ne 32 ] && [ -z "${NO_AT_BRIDGE:-}" ]; then
  export NO_AT_BRIDGE=1
  echo "vedit: /etc/machine-id is not a 32-hex id; disabling the AT-SPI bridge (no D-Bus session here)." >&2
fi
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export GDK_PIXBUF_MODULE_FILE="$RUNTIME/loaders.cache"
[ -f "$RUNTIME/immodules.cache" ] && export GTK_IM_MODULE_FILE="$RUNTIME/immodules.cache"
export GSETTINGS_SCHEMA_DIR="$HERE/usr/share/glib-2.0/schemas"
export GTK_DATA_PREFIX="$HERE/usr"
export GTK_EXE_PREFIX="$HERE/usr"
export GTK_PATH="$HERE/usr/lib/gtk-3.0"
export FONTCONFIG_FILE="$RUNTIME/fonts.conf"
export FONTCONFIG_PATH="$HERE/etc"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/share}"
export VEDIT_GSV_DATADIR="$HERE/usr/share/gtksourceview-4"
exec "$HERE/usr/bin/vedit-gui-spike" "$@"
EOF
chmod +x "$APPDIR/AppRun"

echo "== desktop entry + icon =="
cat > "$APPDIR/vedit-gui-spike.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=VEdit GTK3 Spike
Comment=GtkSourceView 4 highlighting spike for VEdit
Exec=vedit-gui-spike
Icon=vedit-gui-spike
Categories=Development;IDE;
Terminal=false
EOF
cp "$ROOT/assets/icon.png" "$APPDIR/vedit-gui-spike.png"
ln -sf vedit-gui-spike.png "$APPDIR/.DirIcon" 2>/dev/null || true

echo "== relaunch/host-path leakage audit =="
{
  echo "# build-tree path leakage audit (upstream icon artwork is out of scope:"
  echo "#   several Adwaita SVGs carry the upstream author's /home/sam/... metadata)"
  echo "# 1) build-machine paths embedded in the main binary (Rust panic locations):"
  leak=$(strings "$APPDIR/usr/bin/vedit-gui-spike" | grep -cE '/root/\.cargo|/work/|/home/' || true)
  echo "count=$leak"
  strings "$APPDIR/usr/bin/vedit-gui-spike" | grep -E '/root/\.cargo|/work/|/home/' | head -3 || true
  echo "# 2) launch-time config templates (expect @APPDIR@ placeholders only):"
  grep -nE '/work/|/home/|/root/' "$APPDIR"/etc/*.in || echo "none"
  echo "# 3) @APPDIR@ placeholder count in etc/loaders.cache.in:"
  grep -c '@APPDIR@' "$APPDIR"/etc/loaders.cache.in
  echo "# 4) RPATH/RUNPATH on the main binary:"
  readelf -d "$APPDIR/usr/bin/vedit-gui-spike" | grep -E 'RPATH|RUNPATH' || echo "none"
} | tee "$EVID/host-path-audit.txt"

echo "== ELF audit + GLIBC <= 2.17 gate for every bundled ELF =="
bash "$ROOT/scripts/glibc-gate.sh" "$APPDIR" "$EVID/glibc-gate.txt"
tail -3 "$EVID/glibc-gate.txt"

echo "== AppImage =="
APPIMAGETOOL="${APPIMAGETOOL:-/recon/appimagetool.AppImage}"
test -f "$APPIMAGETOOL"
echo "appimagetool sha256: $(sha256sum "$APPIMAGETOOL" | cut -d' ' -f1)" | tee "$EVID/appimagetool-sha256.txt"
rm -f "$OUT/$NAME.AppImage"
ARCH=x86_64 "$APPIMAGETOOL" --appimage-extract-and-run --no-appstream "$APPDIR" "$OUT/$NAME.AppImage" \
  2>&1 | tee "$EVID/appimagetool.log"
chmod +x "$OUT/$NAME.AppImage"

echo "== tar.gz (FUSE-free fallback) =="
TARROOT="$ROOT/tarroot"
rm -rf "$TARROOT"
mkdir -p "$TARROOT/vedit-spike-${VERSION}-linux-x86_64"
cp -a "$APPDIR/." "$TARROOT/vedit-spike-${VERSION}-linux-x86_64/"
ln -sf AppRun "$TARROOT/vedit-spike-${VERSION}-linux-x86_64/vedit"
( cd "$TARROOT" && tar czf "$OUT/vedit-spike-${VERSION}-linux-x86_64.tar.gz" "vedit-spike-${VERSION}-linux-x86_64" )
rm -rf "$TARROOT"

echo "== sizes + checksums =="
{
  ls -l "$OUT/$NAME.AppImage" "$OUT/vedit-spike-${VERSION}-linux-x86_64.tar.gz"
  du -sh "$APPDIR"
  echo "bytes: AppImage $(stat -c%s "$OUT/$NAME.AppImage"), tar.gz $(stat -c%s "$OUT/vedit-spike-${VERSION}-linux-x86_64.tar.gz")"
} | tee "$EVID/sizes.txt"
sha256sum "$OUT/$NAME.AppImage" "$OUT/vedit-spike-${VERSION}-linux-x86_64.tar.gz" | tee "$EVID/sha256.txt"
echo "PACKAGE OK"
