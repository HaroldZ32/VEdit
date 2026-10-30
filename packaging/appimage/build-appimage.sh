#!/usr/bin/env bash
# Assemble the self-contained VEdit AppDir and emit the AppImage (T23).
#
# Runs INSIDE packaging/appimage/Dockerfile.package (a pinned vedit-build image
# plus the GTK input-method modules), with the workspace mounted at
# /vedit-build. Every external input is fetched from its pinned source and
# verified against scripts/versions.env; the .mpd/recon copies are never used.
set -euo pipefail

ROOT=${ROOT:-/vedit-build}
PKG="$ROOT/packaging/appimage"
OUT="$PKG/out"
APPDIR="$PKG/AppDir"
CACHE="$PKG/.cache"
EVID="$ROOT/evidence/appimage"
GSV_PREFIX=/opt/gsv4
VERSION=$(awk -F'"' '/^version = / {print $2; exit}' "$ROOT/Cargo.toml")
NAME="VEdit-${VERSION}-x86_64"

mkdir -p "$OUT" "$CACHE" "$EVID" "$EVID/screenshots"

# shellcheck source=/dev/null
set -a; . "$ROOT/scripts/versions.env"; set +a
: "${VERIBLE_URL:?}" "${VERIBLE_TARBALL_SHA256:?}" "${VERIBLE_LS_SHA256:?}"
: "${VERIBLE_FORMAT_SHA256:?}" "${VERIBLE_LINT_SHA256:?}"
: "${SLANG_BIN_SHA256:?}" "${APPIMAGETOOL_SHA256:?}"

log_cmd() {
  echo "\$ $*"
  "$@"
  local status=$?
  echo "EXIT=$status"
  return $status
}

echo "== [1/9] consume the shared release binary (identical in all three products) =="
# The tar.gz/.run lanes and this AppImage MUST install byte-identical binaries;
# T27 asserts it across products. scripts/build.sh builds the release binary at
# build/artifacts/vd-app (with the RELEASE_RUSTFLAGS remap); this lane therefore
# consumes that artefact instead of compiling its own variant, and the assertion
# below turns "same source" into "same bytes" at build time.
SHARED_APP="$ROOT/build/artifacts/vd-app"
if [ ! -f "$SHARED_APP" ]; then
  echo "ERROR: $SHARED_APP is missing; run \`bash scripts/build.sh\` (release build) first." >&2
  exit 1
fi
SHARED_SHA=$(sha256sum "$SHARED_APP" | cut -d' ' -f1)
GIT_HEAD=$( (cd "$ROOT" && git rev-parse HEAD 2>/dev/null) || echo unknown)
GIT_STATUS=$( (cd "$ROOT" && git status --porcelain 2>/dev/null) || true)
# The fixtures are payload support data, so they come from the same revision as
# the shared binary (a worktree copy could be a half-saved edit).
ARCHIVE_DIR="$CACHE/src-head"
rm -rf "$ARCHIVE_DIR"
mkdir -p "$ARCHIVE_DIR"
(cd "$ROOT" && git archive HEAD) | tar -x -C "$ARCHIVE_DIR"
{
  echo "source_mode=shared-release-artifact"
  echo "git_head=$GIT_HEAD"
  echo "git_status_porcelain:"
  if [ -n "$GIT_STATUS" ]; then printf '%s\n' "$GIT_STATUS"; else echo "  clean"; fi
  echo "shared_vd_app=$SHARED_APP"
  echo "shared_vd_app_sha256=$SHARED_SHA"
  echo "archive_revision=$GIT_HEAD"
  echo "archive_dir=$ARCHIVE_DIR (git archive HEAD, fixtures + provenance)"
} | tee "$EVID/source-tree.txt"
if [ -n "${VEDIT_VD_APP_SHA256:-}" ] && [ "$SHARED_SHA" != "$VEDIT_VD_APP_SHA256" ]; then
  echo "ERROR: build/artifacts/vd-app sha256 $SHARED_SHA != pinned $VEDIT_VD_APP_SHA256" >&2
  exit 1
fi
LEAKS=$(tr '\0' '\n' < "$SHARED_APP" | grep -c '/vedit-build' || true)
if [ "$LEAKS" -ne 0 ]; then
  echo "ERROR: shared vd-app embeds /vedit-build (count=$LEAKS); the release build is not remapped" >&2
  exit 1
fi

echo "== [2/9] fetch and verify the pinned Verible suite =="
TARBALL="$CACHE/verible.tar.gz"
if [ ! -f "$TARBALL" ]; then
  log_cmd curl -fsSL "$VERIBLE_URL" -o "$TARBALL" | tee "$EVID/verible-fetch.log"
else
  echo "using cached $TARBALL" | tee "$EVID/verible-fetch.log"
fi
echo "$VERIBLE_TARBALL_SHA256  $TARBALL" | sha256sum -c -
rm -rf "$CACHE/verible-extract"
mkdir -p "$CACHE/verible-extract"
tar -xzf "$TARBALL" -C "$CACHE/verible-extract"
VBIN=$(find "$CACHE/verible-extract" -type d -name bin | head -1)
{
  echo "# pinned source"
  echo "VERIBLE_URL=$VERIBLE_URL"
  echo "VERIBLE_VERSION=$VERIBLE_VERSION"
  echo
  echo "\$ sha256sum $TARBALL"
  sha256sum "$TARBALL"
  echo "expected=$VERIBLE_TARBALL_SHA256"
  echo "tarball sha256: OK"
  echo "extracted_bin=$VBIN"
  for pair in "verible-verilog-ls:$VERIBLE_LS_SHA256" \
              "verible-verilog-format:$VERIBLE_FORMAT_SHA256" \
              "verible-verilog-lint:$VERIBLE_LINT_SHA256"; do
    binary=${pair%%:*}; expected=${pair##*:}
    echo "\$ sha256sum $VBIN/$binary"
    sha256sum "$VBIN/$binary"
    echo "expected=$expected"
    echo "$expected  $VBIN/$binary" | sha256sum -c -
    echo "EXIT=$?"
  done
} 2>&1 | tee "$EVID/verible-verification.txt"

rm -rf "$APPDIR"
mkdir -p "$APPDIR"/usr/bin "$APPDIR"/usr/lib "$APPDIR"/usr/share "$APPDIR"/etc
mkdir -p "$APPDIR"/usr/lib/vedit/bin
cp "$SHARED_APP" "$APPDIR/usr/bin/vd-app"
chmod 0755 "$APPDIR/usr/bin/vd-app"
PAYLOAD_SHA=$(sha256sum "$APPDIR/usr/bin/vd-app" | cut -d' ' -f1)
if [ "$PAYLOAD_SHA" != "$SHARED_SHA" ]; then
  echo "ERROR: payload vd-app sha256 $PAYLOAD_SHA != shared $SHARED_SHA" >&2
  exit 1
fi
{
  echo "shared_vd_app_sha256=$SHARED_SHA"
  echo "payload_vd_app_sha256=$PAYLOAD_SHA"
  echo "BYTES_EQUAL=true"
  echo "VEDIT_BUILD_LEAKS=$(tr '\0' '\n' < "$APPDIR/usr/bin/vd-app" | grep -c '/vedit-build' || true)"
} | tee "$EVID/payload-vd-app.txt"
cp "$VBIN/verible-verilog-ls" "$VBIN/verible-verilog-format" "$VBIN/verible-verilog-lint" \
   "$APPDIR/usr/lib/vedit/bin/"
for binary in verible-verilog-ls verible-verilog-format verible-verilog-lint; do
  test -x "$APPDIR/usr/lib/vedit/bin/$binary"
done

echo "== [3/9] consume the T3 slang artifact by its pinned sha256 (never rebuilt) =="
SLANG_SRC="$ROOT/build/slang/out/slang"
test -f "$SLANG_SRC"
cp "$SLANG_SRC" "$APPDIR/usr/lib/vedit/bin/slang"
chmod 0755 "$APPDIR/usr/lib/vedit/bin/slang"
{
  echo "\$ sha256sum $SLANG_SRC"
  sha256sum "$SLANG_SRC"
  echo "expected=$SLANG_BIN_SHA256"
  echo "$SLANG_BIN_SHA256  $APPDIR/usr/lib/vedit/bin/slang" | sha256sum -c -
  echo "EXIT=$?"
  echo
  echo "# payload manifest (name, size, sha256, static?)"
  for binary in verible-verilog-ls verible-verilog-format verible-verilog-lint slang; do
    path="$APPDIR/usr/lib/vedit/bin/$binary"
    static=$(ldd "$path" 2>&1 | head -1 || true)
    printf '%s\t%s\t%s\t%s\n' "$binary" "$(stat -c%s "$path")" "$(sha256sum "$path" | cut -d' ' -f1)" "$static"
  done
} 2>&1 | tee "$EVID/payload-binaries.txt"

echo "== [4/9] GTK3 runtime closure, GtkSourceView data, loaders, immodules =="
EXCLUDE_RE='^/(lib64|usr/lib64)/(ld-linux[^/]*|libc|libm|libmvec|libpthread|libdl|librt|libresolv|libnsl|libcrypt|libutil|libanl|libBrokenLocale|libthread_db|libnss_[^/]*|libgcc_s|libstdc\+\+)\.so'
declare -A SEEN=()
QUEUE=("$APPDIR/usr/bin/vd-app" "$GSV_PREFIX/lib64/libgtksourceview-4.so.0")
LOADERS_SRC=/usr/lib64/gdk-pixbuf-2.0/2.10.0/loaders
IMMOD_SRC=/usr/lib64/gtk-3.0/3.0.0/immodules
mkdir -p "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders"
cp -a "$LOADERS_SRC"/*.so "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/"
mkdir -p "$APPDIR/usr/lib/gtk-3.0/3.0.0/immodules"
for module in im-xim.so im-ibus.so; do
  test -f "$IMMOD_SRC/$module"
  cp -a "$IMMOD_SRC/$module" "$APPDIR/usr/lib/gtk-3.0/3.0.0/immodules/"
done
QUEUE+=("$APPDIR"/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so)
QUEUE+=("$APPDIR"/usr/lib/gtk-3.0/3.0.0/immodules/*.so)
while [ ${#QUEUE[@]} -gt 0 ]; do
  current="${QUEUE[0]}"; QUEUE=("${QUEUE[@]:1}")
  while IFS= read -r lib; do
    [ -n "$lib" ] || continue
    [ -f "$lib" ] || continue
    if [[ "$lib" =~ $EXCLUDE_RE ]]; then continue; fi
    key=$(basename "$lib")
    if [ -n "${SEEN[$key]:-}" ]; then continue; fi
    SEEN[$key]="$lib"
    cp -L "$lib" "$APPDIR/usr/lib/$key"
    QUEUE+=("$lib")
  done < <(ldd "$current" 2>/dev/null | awk '/=> \//{print $3} /^\t\/[^ ]+ \(/{print $1}')
done
{
  echo "# GTK3 runtime + transitive closure (recursive ldd; glibc/libgcc/libstdc++ excluded)"
  echo "# count: ${#SEEN[@]}"
  for key in $(printf '%s\n' "${!SEEN[@]}" | sort); do printf '%s\t%s\n' "$key" "${SEEN[$key]}"; done
} | tee "$EVID/bundled-libs.txt"

mkdir -p "$APPDIR/usr/share/gtksourceview-4"
cp -a "$GSV_PREFIX/share/gtksourceview-4/language-specs" "$APPDIR/usr/share/gtksourceview-4/"
cp -a "$GSV_PREFIX/share/gtksourceview-4/styles" "$APPDIR/usr/share/gtksourceview-4/"
ls "$APPDIR/usr/share/gtksourceview-4/language-specs" | grep -E '^(verilog|systemverilog)\.lang$' | tee "$EVID/gsv4-languages.txt"

gdk-pixbuf-query-loaders-64 "$APPDIR"/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so \
  | sed "s|$APPDIR|@APPDIR@|g" > "$APPDIR/etc/loaders.cache.in"
gtk-query-immodules-3.0-64 "$APPDIR"/usr/lib/gtk-3.0/3.0.0/immodules/*.so \
  | sed "s|$APPDIR|@APPDIR@|g" > "$APPDIR/etc/immodules.cache.in"
{
  echo "# gdk-pixbuf loaders"
  ls "$APPDIR"/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/
  echo
  echo "# GTK input-method modules (AC-16)"
  ls -l "$APPDIR"/usr/lib/gtk-3.0/3.0.0/immodules/
  sha256sum "$APPDIR"/usr/lib/gtk-3.0/3.0.0/immodules/*.so
  echo
  echo "# gtk-query-immodules-3.0-64 output (before @APPDIR@ substitution)"
  cat "$APPDIR/etc/immodules.cache.in"
} 2>&1 | tee "$EVID/immodules-and-loaders.txt"

echo "== [5/9] schemas, fonts, icons, desktop entry =="
mkdir -p "$APPDIR/usr/share/glib-2.0/schemas"
cp -a /usr/share/glib-2.0/schemas/org.gtk.*.gschema.xml "$APPDIR/usr/share/glib-2.0/schemas/"
cp -a "$GSV_PREFIX"/share/glib-2.0/schemas/*.gschema.xml "$APPDIR/usr/share/glib-2.0/schemas/" 2>/dev/null || true
glib-compile-schemas "$APPDIR/usr/share/glib-2.0/schemas"
mkdir -p "$APPDIR/usr/share/fonts/dejavu" "$APPDIR/usr/share/icons"
cp -a /usr/share/fonts/dejavu/*.ttf "$APPDIR/usr/share/fonts/dejavu/"
mkdir -p "$APPDIR/usr/share/fonts/wqy-zenhei"
cp -a /usr/share/fonts/wqy-zenhei/*.ttc "$APPDIR/usr/share/fonts/wqy-zenhei/"
{
  echo "# fonts bundled in the payload (no host font is required)"
  rpm -q wqy-zenhei-fonts dejavu-sans-fonts dejavu-sans-mono-fonts
  echo
  echo "# CJK font licence (recorded for THIRD_PARTY_NOTICES)"
  rpm -q --qf '%{NAME} %{VERSION} | licence=%{LICENSE} | url=%{URL}\n' wqy-zenhei-fonts
  echo
  ls -l "$APPDIR/usr/share/fonts/wqy-zenhei" "$APPDIR/usr/share/fonts/dejavu"
} 2>&1 | tee "$EVID/fonts.txt"
# Selftest fixtures are part of the packaged acceptance surface (T25 shares the
# same convention: share/vedit/testdata + VEDIT_SELFTEST_FIXTURES).
mkdir -p "$APPDIR/usr/share/vedit/testdata"
cp -a "$ARCHIVE_DIR/crates/vd-app/tests/fixtures/." "$APPDIR/usr/share/vedit/testdata/"
find "$APPDIR/usr/share/vedit/testdata" -type f | sort | tee "$EVID/selftest-fixtures.txt"
# Complete icon themes: a partial Adwaita tree makes GTK warn about missing
# symbolic icons, and the CSD/toolbar need the full index (cursors included).
for theme in Adwaita hicolor; do
  if [ -d "/usr/share/icons/$theme" ]; then cp -a "/usr/share/icons/$theme" "$APPDIR/usr/share/icons/"; fi
done
cat > "$APPDIR/io.github.vedit.VEdit.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=VEdit
GenericName=Verilog/SystemVerilog Editor
Comment=Verilog/SystemVerilog editor with Verible LSP, SLANG deep lint and optional FIM
Exec=vd-app %F
Icon=io.github.vedit.VEdit
Categories=Development;IDE;TextEditor;
MimeType=text/x-systemverilog;text/x-verilog;
Terminal=false
StartupWMClass=io.github.vedit.VEdit
DESKTOP
cp "$PKG/assets/io.github.vedit.VEdit.png" "$APPDIR/io.github.vedit.VEdit.png"
ln -sf io.github.vedit.VEdit.png "$APPDIR/.DirIcon"
cat > "$APPDIR/etc/fonts.conf.in" <<'FONTCONF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>@APPDIR@/usr/share/fonts</dir>
  <cachedir>/tmp/vedit-fontconfig-cache</cachedir>
</fontconfig>
FONTCONF

echo "== [6/9] AppRun (relocatable launcher) =="
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
# Relocatable launcher for VEdit (tested on CentOS 7, glibc 2.17).
# The payload is self-contained: GTK3, GtkSourceView 4, gdk-pixbuf loaders,
# input-method modules, GSettings schemas, fonts, icons, verible and slang all
# resolve inside $HERE. No host path is exported.
set -e
HERE=$(dirname "$(readlink -f "$0")")

# Per-user runtime directory for the rewritten caches; mirrors the tar.gz/.run
# launcher (T25): XDG_RUNTIME_DIR -> TMPDIR -> explicit failure. mkdir can fail
# (a /proc path, a read-only mount, someone else's directory), which must not
# abort the launch silently.
make_runtime() {
  _base=$1
  _dir="$_base/vedit-runtime-$(id -u)"
  if mkdir -p "$_dir" 2>/dev/null; then
    printf '%s' "$_dir"
    return 0
  fi
  return 1
}

PREFERRED=${XDG_RUNTIME_DIR:-}
FALLBACK=${TMPDIR:-/tmp}
if [ -n "$PREFERRED" ] && RUNTIME=$(make_runtime "$PREFERRED"); then
  :
elif RUNTIME=$(make_runtime "$FALLBACK"); then
  if [ -n "$PREFERRED" ]; then
    echo "vedit: cannot use XDG_RUNTIME_DIR=$PREFERRED (not writable or not creatable); using $FALLBACK instead" >&2
  fi
else
  echo "vedit: cannot create a runtime directory; tried '$PREFERRED' and '$FALLBACK'" >&2
  echo "vedit: set XDG_RUNTIME_DIR (or TMPDIR) to a writable directory and retry" >&2
  exit 1
fi
sed "s|@APPDIR@|$HERE|g" "$HERE/etc/loaders.cache.in" > "$RUNTIME/loaders.cache"
sed "s|@APPDIR@|$HERE|g" "$HERE/etc/immodules.cache.in" > "$RUNTIME/immodules.cache"
sed "s|@APPDIR@|$HERE|g" "$HERE/etc/fonts.conf.in" > "$RUNTIME/fonts.conf"
# libdbus aborts when /etc/machine-id is empty (bare containers). The AT-SPI
# bridge is not bundled, so disable it only in that degenerate environment.
mid=$(cat /etc/machine-id 2>/dev/null || true)
if [ "${#mid}" -ne 32 ] && [ -z "${NO_AT_BRIDGE:-}" ]; then
  export NO_AT_BRIDGE=1
  echo "vedit: /etc/machine-id is not a 32-hex id; disabling the AT-SPI bridge." >&2
fi
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export GDK_PIXBUF_MODULE_FILE="$RUNTIME/loaders.cache"
export GTK_IM_MODULE_FILE="$RUNTIME/immodules.cache"
export GSETTINGS_SCHEMA_DIR="$HERE/usr/share/glib-2.0/schemas"
export GTK_DATA_PREFIX="$HERE/usr"
export GTK_EXE_PREFIX="$HERE/usr"
export GTK_PATH="$HERE/usr/lib/gtk-3.0"
export FONTCONFIG_FILE="$RUNTIME/fonts.conf"
export FONTCONFIG_PATH="$HERE/etc"
export XDG_DATA_DIRS="$HERE/usr/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export VEDIT_GSV_DATADIR="$HERE/usr/share/gtksourceview-4"
export VEDIT_SLANG="${VEDIT_SLANG:-$HERE/usr/lib/vedit/bin/slang}"
export VEDIT_SELFTEST_FIXTURES="${VEDIT_SELFTEST_FIXTURES:-$HERE/usr/share/vedit/testdata}"
exec "$HERE/usr/bin/vd-app" "$@"
APPRUN
chmod 0755 "$APPDIR/AppRun"
sed 's/^/  /' "$APPDIR/AppRun" > "$EVID/AppRun.txt"

echo "== [7/9] host-path audit, RUNPATH gate, GLIBC gate =="
{
  echo "# build-tree path leakage audit"
  echo "# 1) build-host checkout paths (the release remap target is /vedit, never /vedit-build):"
  host_hits=$(tr '\0' '\n' < "$APPDIR/usr/bin/vd-app" | grep -E '/vedit-build|/root/\.cargo|/home/' || true)
  if [ -n "$host_hits" ]; then printf '%s\n' "$host_hits" | head -5; else echo "none"; fi
  host_count=$(printf '%s' "$host_hits" | grep -c . || true)
  echo "BUILD_HOST_PATH_HITS=$host_count"
  echo
  echo "# 1b) remapped source paths under the sandbox root /vedit (panic locations):"
  sandbox_hits=$(tr '\0' '\n' < "$APPDIR/usr/bin/vd-app" | grep -c '/vedit/' || true)
  echo "SANDBOX_SOURCE_PATH_LITERALS=$sandbox_hits"
  sample=$(tr '\0' '\n' < "$APPDIR/usr/bin/vd-app" | grep -a -m 2 -E '^/vedit/(crates|target)/' | tr -cd '[:print:]\n' | cut -c1-140 || true)
  if [ -n "$sample" ]; then printf '%s\n' "$sample"; else echo "none"; fi
  echo "CONCLUSION: /vedit-build count is 0 (measured on NUL-delimited strings, not"
  echo "\`strings\` output, which glues adjacent literals). What remains are $sandbox_hits"
  echo "panic source locations remapped to the sandbox root /vedit — build metadata,"
  echo "not a host checkout path, and no runtime lookup. The former env!() fixture"
  echo "fallback is gone: vd-app derives the fixtures at runtime and the payload's"
  echo "VEDIT_SELFTEST_FIXTURES points at <payload>/usr/share/vedit/testdata."
  echo
  echo "# 2) launcher/config templates (expect @APPDIR@ placeholders only):"
  grep -nE '/vedit-build|/root/\.cargo|/home/' "$APPDIR"/etc/*.in || echo "none"
  echo "# 3) @APPDIR@ placeholder counts:"
  printf 'loaders.cache.in: '; grep -c '@APPDIR@' "$APPDIR"/etc/loaders.cache.in
  printf 'immodules.cache.in: '; grep -c '@APPDIR@' "$APPDIR"/etc/immodules.cache.in
  echo "# 4) RPATH/RUNPATH on the main binary:"
  readelf -d "$APPDIR/usr/bin/vd-app" | grep -E 'RPATH|RUNPATH' || echo "none"
} 2>&1 | tee "$EVID/host-path-audit.txt"
bash "$PKG/check-rpaths.sh" "$APPDIR" "$PKG/rpath-allowlist.txt" "$EVID/rpath-gate.txt"
bash "$PKG/glibc-gate.sh" "$APPDIR" "$EVID/glibc-gate.txt"

echo "== [8/9] AppImage (appimagetool from the pinned sha256) =="
APPIMAGETOOL="$CACHE/appimagetool.AppImage"
if [ ! -f "$APPIMAGETOOL" ]; then
  curl -fsSL "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" -o "$APPIMAGETOOL"
fi
chmod 0755 "$APPIMAGETOOL"
{
  echo "APPIMAGETOOL_URL=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
  echo "APPIMAGETOOL_SHA256=$APPIMAGETOOL_SHA256"
  sha256sum "$APPIMAGETOOL"
  echo "$APPIMAGETOOL_SHA256  $APPIMAGETOOL" | sha256sum -c -
  echo "EXIT=$?"
} 2>&1 | tee "$EVID/appimagetool-verification.txt"
rm -f "$OUT/$NAME.AppImage"
ARCH=x86_64 "$APPIMAGETOOL" --appimage-extract-and-run --no-appstream \
  "$APPDIR" "$OUT/$NAME.AppImage" 2>&1 | tee "$EVID/appimagetool.log"
chmod 0755 "$OUT/$NAME.AppImage"

echo "== [9/9] sizes and checksums =="
{
  ls -l "$OUT/$NAME.AppImage"
  du -sh "$APPDIR"
  echo "AppImage_bytes=$(stat -c%s "$OUT/$NAME.AppImage")"
  echo "AppDir_bytes=$(du -sb "$APPDIR" | cut -f1)"
  sha256sum "$OUT/$NAME.AppImage"
} | tee "$EVID/sizes.txt"
echo "BUILD APPIMAGE OK: $OUT/$NAME.AppImage"
