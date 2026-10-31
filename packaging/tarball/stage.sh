#!/usr/bin/env bash
# Assemble the T25 payload INSIDE the pinned CentOS 7 build image.
#
#   packaging/tarball/build.sh            (host entry point)
#   bash packaging/tarball/stage.sh       (this script, inside the image)
#
# Produces, under packaging/tarball/:
#   out/vedit-<ver>-linux-x86_64.tar.gz   relocatable directory package
#   out/VEdit-<ver>-x86_64.run            self-extracting single file
# and the build-time evidence under evidence/.
set -euo pipefail

ROOT=/vedit-build
LANE="$ROOT/packaging/tarball"
EVID="$LANE/evidence"
OUT="$LANE/out"
STAGE="$LANE/stage"
PKG="$STAGE/VEdit"
GSV_PREFIX=/opt/gsv4

say() { echo "[stage] $*"; }
mkdir -p "$EVID" "$OUT" "$STAGE"
rm -rf "$PKG"
mkdir -p "$PKG"/{bin,lib,share,etc,share/doc/vedit}

VERSION=$(awk '
  /^\[workspace\.package\]/ {inside=1; next}
  /^\[/ {inside=0}
  inside && /^version[[:space:]]*=/ {gsub(/[" ]/,""); sub(/^version=/,""); print; exit}
' "$ROOT/Cargo.toml")
: "${VERSION:?cannot read workspace version}"

# --- pins (single source of truth: scripts/versions.env) ----------------------
# shellcheck source=../../scripts/versions.env
set -a
. "$ROOT/scripts/versions.env"
set +a
: "${VERIBLE_URL:?}" "${VERIBLE_TARBALL_SHA256:?}" "${VERIBLE_LS_SHA256:?}"
: "${VERIBLE_LINT_SHA256:?}" "${VERIBLE_FORMAT_SHA256:?}" "${SLANG_BIN_SHA256:?}"
say "version=$VERSION verible=$VERIBLE_VERSION"

# --- 0. build the application -----------------------------------------------
# The release ships ONE vd-app binary. build/artifacts/vd-app is the canonical
# artifact (built from the frozen commit with RELEASE_RUSTFLAGS and stripped);
# this lane consumes it verbatim so the tar.gz/.run payload is byte-identical to
# the AppImage's. Only when it is missing does this lane build + strip locally,
# and it says so in the evidence.
CANONICAL="$ROOT/build/artifacts/vd-app"
if [ -x "$CANONICAL" ]; then
  say "using the canonical release binary: build/artifacts/vd-app"
  APP="$CANONICAL"
  VD_APP_SOURCE="canonical:build/artifacts/vd-app"
elif [ -x "$ROOT/target/release/vd-app" ]; then
  say "canonical artifact missing; reusing the workspace release build"
  APP="$ROOT/target/release/vd-app"
  VD_APP_SOURCE="workspace:target/release/vd-app"
else
  say "canonical artifact and release build missing; building vd-app here"
  : "${RELEASE_RUSTFLAGS:?RELEASE_RUSTFLAGS is defined in scripts/versions.env}"
  export RUSTFLAGS="${RUSTFLAGS:-} ${RELEASE_RUSTFLAGS}"
  echo "RUSTFLAGS=$RUSTFLAGS" | tee "$EVID/01a-rustflags.txt"
  cargo build --release -p vd-app 2>&1 | tee "$EVID/01-cargo-build.log" | tail -2
  APP="$ROOT/target/release/vd-app"
  VD_APP_SOURCE="local-build:target/release/vd-app"
fi
[ -x "$APP" ] || { say "FAIL: no usable vd-app ($APP)"; exit 1; }

# The canonical pipeline strips the binary (scripts/build-in-container.sh): an
# unstripped 7.9 MB binary is a different file from the shipped 6.0 MB one, which
# is exactly how the products stopped being byte-identical. Strip a local build;
# refuse an artifact that still carries a symbol table.
case "$VD_APP_SOURCE" in
  canonical:*)
    if readelf -S "$APP" 2>/dev/null | grep -q '\.symtab'; then
      say "FAIL: $APP is not stripped; the canonical artifact must be stripped"
      exit 1
    fi
    ;;
  *)
    strip --strip-all "$APP"
    ;;
esac
{
  echo "# vd-app shipped in this payload"
  echo "source=$VD_APP_SOURCE"
  sha256sum "$APP"
  stat -c 'bytes=%s' "$APP"
  if readelf -S "$APP" 2>/dev/null | grep -q '\.symtab'; then
    echo "stripped=no"
  else
    echo "stripped=yes"
  fi
} | tee "$EVID/01b-vd-app-identity.txt"

# --- 1. binaries --------------------------------------------------------------
install -m 0755 "$APP" "$PKG/bin/vd-app"

# Entry point at the payload root, and the exact path the .run stub execs.
install -m 0755 "$LANE/launcher/vedit.sh" "$PKG/vedit.sh"
cat > "$PKG/bin/vedit" <<'SHIM'
#!/bin/sh
# Thin shim so `VEdit/bin/vedit` also works: the real entry point (which sets the
# bundled runtime environment) is VEdit/vedit.sh one level up.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
exec "$HERE/vedit.sh" "$@"
SHIM
chmod 0755 "$PKG/bin/vedit"

say "fetching verible from the pinned URL"
curl -fsSL "$VERIBLE_URL" -o /tmp/verible.tar.gz
echo "$VERIBLE_TARBALL_SHA256  /tmp/verible.tar.gz" | sha256sum -c - > "$EVID/02-verible-sha256.txt" 2>&1
tar xzf /tmp/verible.tar.gz -C /tmp
VERIBLE_DIR=$(find /tmp -maxdepth 2 -type d -name 'verible-*' | head -1)
[ -n "$VERIBLE_DIR" ] || { say "FAIL: verible tarball layout"; exit 1; }
for pair in "ls:$VERIBLE_LS_SHA256" "lint:$VERIBLE_LINT_SHA256" "format:$VERIBLE_FORMAT_SHA256"; do
  kind=${pair%%:*}; want=${pair#*:}
  src=$(find "$VERIBLE_DIR" -name "verible-verilog-${kind}" -type f | head -1)
  [ -n "$src" ] || { say "FAIL: verible-verilog-${kind} missing from tarball"; exit 1; }
  echo "$want  $(basename "$src")" | (cd "$(dirname "$src")" && sha256sum -c -) >> "$EVID/02-verible-sha256.txt" 2>&1
  install -m 0755 "$src" "$PKG/bin/verible-verilog-${kind}"
done
cat "$EVID/02-verible-sha256.txt"

say "installing slang from the T3 artifact (sha256 pinned)"
SLANG="$ROOT/build/slang/out/slang"
[ -x "$SLANG" ] || { say "FAIL: $SLANG missing (run build/slang/build.sh first)"; exit 1; }
echo "$SLANG_BIN_SHA256  $SLANG" | sha256sum -c - | tee "$EVID/03-slang-sha256.txt"
install -m 0755 "$SLANG" "$PKG/bin/slang"

# --- 2. GTK3 runtime closure (recursive ldd over every ELF we ship) -----------
say "collecting the GTK3 runtime closure"
EXCLUDE_RE='^/(lib64|usr/lib64)/(ld-linux[^/]*|libc|libm|libmvec|libpthread|libdl|librt|libresolv|libnsl|libcrypt|libutil|libanl|libBrokenLocale|libthread_db|libnss_[^/]*|libgcc_s|libstdc\+\+)\.so'

declare -A SEEN=()
QUEUE=()
for elf in "$PKG/bin/vd-app"; do QUEUE+=("$elf"); done
walk() {
  local cur lib key
  while [ ${#QUEUE[@]} -gt 0 ]; do
    cur="${QUEUE[0]}"; QUEUE=("${QUEUE[@]:1}")
    while IFS= read -r lib; do
      [ -n "$lib" ] || continue
      [ -f "$lib" ] || continue
      [[ "$lib" =~ $EXCLUDE_RE ]] && continue
      key="$(basename "$lib")"
      [ -n "${SEEN[$key]:-}" ] && continue
      SEEN[$key]="$lib"
      cp -L "$lib" "$PKG/lib/$key"
      QUEUE+=("$lib")
    done < <(ldd "$cur" 2>/dev/null | awk '/=> \//{print $3} /^\t\/[^ ]+ \(/{print $1}')
  done
}
walk

# --- 3. gdk-pixbuf loaders ----------------------------------------------------
say "gdk-pixbuf loaders"
LOADERS_SRC=/usr/lib64/gdk-pixbuf-2.0/2.10.0/loaders
mkdir -p "$PKG/lib/gdk-pixbuf-2.0/2.10.0/loaders"
cp -a "$LOADERS_SRC"/*.so "$PKG/lib/gdk-pixbuf-2.0/2.10.0/loaders/"
gdk-pixbuf-query-loaders-64 "$PKG"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so \
  | sed "s|$PKG|@APPDIR@|g" > "$PKG/etc/loaders.cache.in"
# the loaders' own dependencies join the closure as well
for loader in "$PKG"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so; do QUEUE+=("$loader"); done
walk

# --- 4. input-method modules (explicit deliverable) --------------------------
say "GTK input-method modules (xim + ibus)"
IMMOD_SRC=/usr/lib64/gtk-3.0/3.0.0/immodules
mkdir -p "$PKG/lib/gtk-3.0/3.0.0/immodules"
for mod in im-xim.so im-ibus.so; do
  if [ -f "$IMMOD_SRC/$mod" ]; then
    cp -a "$IMMOD_SRC/$mod" "$PKG/lib/gtk-3.0/3.0.0/immodules/"
  else
    say "WARNING: $mod missing from $IMMOD_SRC (package not installed?)"
  fi
done
gtk-query-immodules-3.0-64 "$PKG"/lib/gtk-3.0/3.0.0/immodules/*.so \
  | sed "s|$PKG|@APPDIR@|g" > "$PKG/etc/immodules.cache.in"
for mod in "$PKG"/lib/gtk-3.0/3.0.0/immodules/*.so; do QUEUE+=("$mod"); done
walk

# --- 5. GtkSourceView data (language definitions + styles) -------------------
say "GtkSourceView 4.6.1 data"
mkdir -p "$PKG/share/gtksourceview-4"
cp -a "$GSV_PREFIX/share/gtksourceview-4/language-specs" "$PKG/share/gtksourceview-4/"
cp -a "$GSV_PREFIX/share/gtksourceview-4/styles" "$PKG/share/gtksourceview-4/"

# --- 6. GSettings schemas ----------------------------------------------------
say "GSettings schemas"
mkdir -p "$PKG/share/glib-2.0/schemas"
cp -a /usr/share/glib-2.0/schemas/org.gtk.*.gschema.xml "$PKG/share/glib-2.0/schemas/"
cp -a "$GSV_PREFIX"/share/glib-2.0/schemas/*.gschema.xml "$PKG/share/glib-2.0/schemas/" 2>/dev/null || true
glib-compile-schemas "$PKG/share/glib-2.0/schemas"

# --- 7. fonts, icons, themes, fontconfig ------------------------------------
say "fonts / icons / themes"
mkdir -p "$PKG/share/fonts/dejavu" "$PKG/share/fonts/wqy-zenhei" "$PKG/share/icons" "$PKG/share/themes"
cp -a /usr/share/fonts/dejavu/*.ttf "$PKG/share/fonts/dejavu/"
# CJK: a minimal CentOS 7 desktop has no Chinese font, and the product's users
# write Chinese comments — without this they would see tofu. Emoji fonts are
# deliberately NOT bundled (size/licence, documented in the README).
yum -y install wqy-zenhei-fonts >/dev/null 2>&1 || say "WARNING: wqy-zenhei-fonts unavailable"
if [ -d /usr/share/fonts/wqy-zenhei ]; then
  cp -a /usr/share/fonts/wqy-zenhei/*.ttc "$PKG/share/fonts/wqy-zenhei/"
else
  say "FAIL: wqy-zenhei.ttc missing after install"
  exit 1
fi
{
  echo "# CJK font bundled for Chinese comments/identifiers"
  echo "package: $(rpm -q --qf '%{NAME}-%{VERSION}-%{RELEASE}' wqy-zenhei-fonts 2>/dev/null)"
  echo "licence: $(rpm -q --qf '%{LICENSE}' wqy-zenhei-fonts 2>/dev/null)  (GPLv2 with font-embedding exception)"
  sha256sum "$PKG/share/fonts/wqy-zenhei/"*.ttc
  echo "dejavu files: $(ls "$PKG/share/fonts/dejavu" | wc -l)"
  echo "cjk files: $(ls "$PKG/share/fonts/wqy-zenhei" | wc -l)"
} > "$EVID/18-cjk-font.txt"
cat "$EVID/18-cjk-font.txt"
for t in Adwaita HighContrast HighContrastInverse; do
  [ -d "/usr/share/themes/$t" ] && cp -a "/usr/share/themes/$t" "$PKG/share/themes/"
done
[ -d /usr/share/icons/hicolor ] && cp -a /usr/share/icons/hicolor "$PKG/share/icons/"
if [ -d /usr/share/icons/Adwaita ]; then
  mkdir -p "$PKG/share/icons/Adwaita"
  for size in 16x16 22x22 24x24 32x32 48x48; do
    [ -d "/usr/share/icons/Adwaita/$size" ] && cp -a "/usr/share/icons/Adwaita/$size" "$PKG/share/icons/Adwaita/"
  done
  [ -f /usr/share/icons/Adwaita/index.theme ] && cp /usr/share/icons/Adwaita/index.theme "$PKG/share/icons/Adwaita/"
fi
cat > "$PKG/etc/fonts.conf.in" <<'FC'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>@APPDIR@/share/fonts</dir>
  <cachedir>/tmp/vedit-fontconfig-cache</cachedir>
</fontconfig>
FC

# --- 7b. selftest fixtures (the app resolves them at run time) ---------------
# The lsp selftest's "definition" check opens two SystemVerilog fixtures. They are
# resolved from the build tree by default, so a package must carry them and point
# VEDIT_SELFTEST_FIXTURES at the copy (T21 makes the variable authoritative).
say "selftest fixtures"
mkdir -p "$PKG/share/vedit/testdata"
cp -a "$ROOT/crates/vd-app/tests/fixtures/definition" "$PKG/share/vedit/testdata/definition"
find "$PKG/share/vedit/testdata" -type f | sed "s|$PKG/||" | tee "$EVID/17-selftest-fixtures.txt"

# --- 8. notices shipped with the payload ------------------------------------
cp "$ROOT/packaging/tarball/PAYLOAD-NOTICES.txt" "$PKG/share/doc/vedit/THIRD_PARTY_NOTICES.txt" 2>/dev/null || true

# --- 9. audits: manifest, rpath/glibc, host paths ----------------------------
say "audits"
{
  echo "# payload manifest ($(date -u +%FT%TZ))"
  echo "# every file, its sha256 and where it came from"
  ( cd "$PKG" && find . -type f | sort ) | while IFS= read -r rel; do
    sha=$(sha256sum "$PKG/$rel" | cut -d' ' -f1)
    printf '%s  %s\n' "$sha" "${rel#./}"
  done
} > "$EVID/10-payload-manifest.txt"

{
  echo "# bundled binary identities"
  sha256sum "$PKG/bin/vd-app" "$PKG/bin/slang" "$PKG/bin/verible-verilog-ls" \
            "$PKG/bin/verible-verilog-lint" "$PKG/bin/verible-verilog-format"
  echo
  echo "# immodules actually bundled"
  ls -l "$PKG/lib/gtk-3.0/3.0.0/immodules/"
  echo
  echo "# immodule cache (both entries must be present)"
  cat "$PKG/etc/immodules.cache.in"
} > "$EVID/12-immodules-and-binaries.txt"

sh "$LANE/glibc-gate.sh" "$PKG" "$LANE/rpath-allowlist.txt" "$EVID/11-glibc-rpath.txt"
tail -6 "$EVID/11-glibc-rpath.txt"

{
  echo "# host-path audit for the payload"
  echo
  echo "## 1) launch-time templates must contain only @APPDIR@ placeholders"
  if grep -nE '/vedit-build|/home/|/root/|/opt/gsv4' "$PKG"/etc/*.in; then
    echo "FAIL: absolute host path in a launch-time template"
  else
    echo "none (PASS)"
  fi
  echo
  echo "## 2) @APPDIR@ placeholder counts"
  for f in "$PKG"/etc/*.in; do printf '%s: %s\n' "$(basename "$f")" "$(grep -c '@APPDIR@' "$f")"; done
  echo
  echo "## 3) RPATH/RUNPATH on the shipped ELF binaries"
  for f in "$PKG/bin/"*; do
    [ -f "$f" ] || continue
    readelf -h "$f" >/dev/null 2>&1 || continue
    printf '%s: ' "$(basename "$f")"
    readelf -d "$f" | grep -E 'RPATH|RUNPATH' || echo none
  done
  echo
  echo "## 4) embedded build-tree strings in the app binary (panic locations)"
  # --remap-path-prefix rewrites the paths rustc emits; it does not necessarily
  # rewrite every compile-time literal (e.g. an `env!("CARGO_MANIFEST_DIR")`
  # default). So the count is REPORTED and classified, not assumed to be 0: a
  # single surviving literal is acceptable if it is a default value that is never
  # opened, and it must be named here so a reviewer can judge it.
  build_strings=$(strings "$PKG/bin/vd-app" | grep -c '/vedit-build' || true)
  echo "count(/vedit-build): $build_strings"
  if [ "$build_strings" -eq 0 ]; then
    echo "PASS: the release remap (RELEASE_RUSTFLAGS) removed the build-tree prefix"
  else
    echo "WARN: $build_strings build-tree strings remain; distinct values:"
    # `sed -n '1,5p'` reads the whole stream (unlike `head -5`, which exits early
    # and SIGPIPEs `sort` under this script's `set -o pipefail`); the visible
    # output is identical.
    strings "$PKG/bin/vd-app" | grep '/vedit-build' | sort -u | sed -n '1,5p'
    echo "classify each: a rustc-emitted source path means the remap was not applied;"
    echo "a compile-time literal default (env!) is inert unless opened at run time."
  fi
  echo "count(/vedit/ remapped prefix): $(strings "$PKG/bin/vd-app" | grep -c '/vedit/' || true)"
} > "$EVID/13-host-path-audit.txt"
cat "$EVID/13-host-path-audit.txt"

# --- 10. tar.gz --------------------------------------------------------------
say "creating the tar.gz"
TARBALL="$OUT/vedit-${VERSION}-linux-x86_64.tar.gz"
( cd "$STAGE" && tar --owner=0 --group=0 --numeric-owner \
    -cf - VEdit | gzip -9 -n > "$TARBALL" )

# --- 11. self-extracting .run ------------------------------------------------
say "creating the self-extracting .run"
build_stub() {
  stub=$(mktemp)
  sed "s/__PAYLOAD_OFFSET__/$1/" "$LANE/run-stub.sh" > "$stub"
  stat -c%s "$stub"
}
offset=1
while :; do
  stub_size=$(build_stub "$offset")
  next=$((stub_size + 1))
  [ "$next" = "$offset" ] && break
  offset=$next
done
STUB=$(mktemp)
build_stub "$offset" >/dev/null
sed "s/__PAYLOAD_OFFSET__/$offset/" "$LANE/run-stub.sh" > "$STUB"
RUN="$OUT/VEdit-${VERSION}-x86_64.run"
cat "$STUB" "$TARBALL" > "$RUN"
chmod 0755 "$RUN"
echo "$STUB" > /dev/null
rm -f "$STUB" /tmp/verible.tar.gz

# sanity: the payload must round-trip
VERIFY_DIR=$(mktemp -d)
head -c "$((offset - 1))" "$RUN" > "$VERIFY_DIR/stub"
tail -c +"$offset" "$RUN" | tar tz -C "$VERIFY_DIR" > "$VERIFY_DIR/list.txt"
echo "entries=$(wc -l < "$VERIFY_DIR/list.txt")" > "$EVID/14-run-selfcheck.txt"
echo "stub_bytes=$((offset - 1))" >> "$EVID/14-run-selfcheck.txt"
echo "tar_bytes=$(stat -c%s "$TARBALL") run_bytes=$(stat -c%s "$RUN")" >> "$EVID/14-run-selfcheck.txt"
rm -rf "$VERIFY_DIR"

# --- 12. launcher + sizes evidence ------------------------------------------
cp "$PKG/vedit.sh" "$EVID/15-launcher.sh.txt"
{
  echo "# products ($(date -u +%FT%TZ))"
  stat -c '%s  %n' "$TARBALL" "$RUN"
  echo
  echo "payload tree: $(du -sh "$PKG" | cut -f1)"
  echo "files in payload: $(find "$PKG" -type f | wc -l)"
  echo "ELF files in payload: $(find "$PKG" -type f -exec sh -c 'readelf -h "$1" >/dev/null 2>&1 && echo x' _ {} \; | wc -l)"
  echo
  sha256sum "$TARBALL" "$RUN"
} > "$EVID/16-products.txt"
cat "$EVID/16-products.txt"
say "STAGE OK"
