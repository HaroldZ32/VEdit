#!/usr/bin/env bash
# Assemble the VEdit private-runtime AppDir in the F2 form (T64 / T61 decision).
#
# F2 = the KSyntaxHighlighting library is the SELF-BUILT one
#      (QRC_SYNTAX=OFF, NO_STANDARD_PATHS=OFF), so it embeds no definitions and
#      looks them up through QStandardPaths. The AppDir ships a WHITELIST only,
#      and AppRun narrows XDG_DATA_DIRS/XDG_DATA_HOME to the AppDir, so a host
#      that has KDE installed cannot leak its definitions into the product.
#
# Hard conditions implemented here (FORM-DECISION C1-C8):
#   C1 self-built library (QRC_SYNTAX=OFF)          -> --ksh-f2 install tree
#   C2 NO_STANDARD_PATHS=OFF                        -> verified from the build
#   C3/C4 both XDG variables set (AppRun, see AppRun)
#   C5 only whitelist XMLs under usr/share/.../syntax; NO syntax-bundled/
#   C7 runtime guard binary shipped (vedit-ksh-env-probe)
#   C8 fonts + icon data self-provided
#
# Runs INSIDE the pinned Qt6/KF6 image (scripts/docker/Dockerfile.build), with
# the workspace mounted at /vedit-build.
#
# Usage (in the image):
#   packaging/runtime/assemble-appdir.sh [--ksh-f2 <install-dir>] [--out <dir>]
set -euo pipefail

ROOT=${ROOT:-/vedit-build}
VERSION=$(awk -F'"' '/^version = / {print $2; exit}' "$ROOT/Cargo.toml" 2>/dev/null || echo "0.0.0")
SMOKE="$ROOT/target/qt6-build/cmake/smoke/vedit-m0-smoke"
PROBE_TOOL="$ROOT/target/qt6-build/cmake/smoke/vedit-ksh-env-probe"
KSH_F2=""
OUT="$ROOT/packaging/runtime/out/AppDir"
WHITELIST="$ROOT/resources/syntax"

while [ $# -gt 0 ]; do
  case "$1" in
    --ksh-f2) KSH_F2=${2:?}; shift 2 ;;
    --out) OUT=${2:?}; shift 2 ;;
    --whitelist) WHITELIST=${2:?}; shift 2 ;;
    -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[ -n "$KSH_F2" ] || KSH_F2="$ROOT/target/ksh-selfbuild/F2/install"
[ -d "$KSH_F2" ] || { echo "ERROR: F2 install tree missing: $KSH_F2 (run scripts/build-ksh-selfbuild.sh with KSH_FORM=F2)" >&2; exit 1; }
[ -f "$SMOKE" ] || { echo "ERROR: smoke binary missing: $SMOKE" >&2; exit 1; }
[ -x "$PROBE_TOOL" ] || { echo "ERROR: env probe missing: $PROBE_TOOL" >&2; exit 1; }
[ -d "$WHITELIST" ] || { echo "ERROR: whitelist dir missing: $WHITELIST" >&2; exit 1; }

LIBDIRS=(/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib /lib)
PLUGROOT=/usr/lib/x86_64-linux-gnu/qt6/plugins
log() { printf '[assemble] %s\n' "$*"; }

F2LIB=$(ls -1 "$KSH_F2"/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.* 2>/dev/null | head -1)
[ -n "$F2LIB" ] || F2LIB=$(ls -1 "$KSH_F2"/lib/libKF6SyntaxHighlighting.so.6.* 2>/dev/null | head -1)
[ -n "$F2LIB" ] && [ -f "$F2LIB" ] || { echo "ERROR: self-built libKF6SyntaxHighlighting.so not found under $KSH_F2" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"/{bin,lib,plugins/platforms,plugins/xcbglintegrations,plugins/kf6/parts} \
         "$OUT"/{share/fonts,share/ksyntaxhighlighting/testdata/syntax,etc/fonts} \
         "$OUT"/usr/share/org.kde.syntax-highlighting/syntax "$OUT"/usr/share/vedit

# --- 1. programs -------------------------------------------------------------
install -m 0755 "$SMOKE" "$OUT/bin/vedit-m0-smoke"
install -m 0755 "$PROBE_TOOL" "$OUT/bin/vedit-ksh-env-probe"
log "binary: $(sha256sum "$SMOKE" | cut -d' ' -f1)  probe: $(sha256sum "$PROBE_TOOL" | cut -d' ' -f1)"

# --- 2. plugins (dlopen-only) ------------------------------------------------
for p in libqoffscreen.so libqminimal.so libqxcb.so libqwayland.so; do
  [ -f "$PLUGROOT/platforms/$p" ] && install -m 0755 "$PLUGROOT/platforms/$p" "$OUT/plugins/platforms/$p"
done
for p in "$PLUGROOT"/xcbglintegrations/*.so; do
  [ -e "$p" ] && install -m 0755 "$p" "$OUT/plugins/xcbglintegrations/$(basename "$p")"
done
install -m 0755 "$PLUGROOT/kf6/parts/katepart.so" "$OUT/plugins/kf6/parts/katepart.so"

# --- 3. library closure (F2 library included) --------------------------------
declare -A COPIED=()
copy_dep() {
  local src=$1 base name
  [ -f "$src" ] || return 0
  case "$src" in */ld-linux-x86-64.so.2) return 0 ;; esac
  base=$(basename "$src")
  [ -n "${COPIED[$base]:-}" ] && return 0
  name=$(readlink -f "$src"); [ -f "$name" ] || return 0
  install -m 0644 "$name" "$OUT/lib/$base"; COPIED[$base]=1
}
closure() { ldd "$1" 2>/dev/null | awk '/=> \// {print $3} /^[ \t]*\/.*\.so/ {print $1}'; }
install -m 0644 "$(readlink -f "$F2LIB")" "$OUT/lib/libKF6SyntaxHighlighting.so.6"
# Mark it as done BEFORE the closure runs: ldd resolves this dependency against
# the DISTRO library (the build image has it installed), and an unmarked copy_dep
# would silently overwrite the self-built library with the distro one.
COPIED[libKF6SyntaxHighlighting.so.6]=1
for root in "$OUT/bin/vedit-m0-smoke" "$OUT/bin/vedit-ksh-env-probe" "$OUT/lib/libKF6SyntaxHighlighting.so.6" \
            "$OUT/plugins/kf6/parts/katepart.so" "$OUT"/plugins/platforms/*.so "$OUT"/plugins/xcbglintegrations/*.so; do
  [ -e "$root" ] || continue
  while read -r dep; do copy_dep "$dep"; done < <(closure "$root")
done

# --- 4. loader + glibc -------------------------------------------------------
install -m 0755 "$(readlink -f /lib64/ld-linux-x86-64.so.2)" "$OUT/ld-linux-x86-64.so.2"
while read -r dep; do
  case "$(basename "$dep")" in ld-linux-x86-64.so.2) ;; *) copy_dep "$dep" ;; esac
done < <(closure "$(readlink -f /usr/lib/x86_64-linux-gnu/libc.so.6)")
for g in libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1 libresolv.so.2 libutil.so.1 libanl.so.1 libmvec.so.1; do
  src=$(readlink -f "/usr/lib/x86_64-linux-gnu/$g" 2>/dev/null || true)
  [ -n "$src" ] && [ -f "$src" ] && copy_dep "$src"
done

# --- 5. whitelist syntax definitions (C5) ------------------------------------
for x in "$WHITELIST"/*.xml; do
  [ -e "$x" ] || continue
  install -m 0644 "$x" "$OUT/usr/share/org.kde.syntax-highlighting/syntax/$(basename "$x")"
done
python3 - "$OUT/usr/share/org.kde.syntax-highlighting/syntax" > "$OUT/usr/share/vedit/ksh-whitelist.txt" <<'PY'
import glob, os, re, sys
names = []
for path in sorted(glob.glob(os.path.join(sys.argv[1], "*.xml"))):
    head = open(path, "r", encoding="utf-8", errors="replace").read(4096)
    m = re.search(r'<language\b[^>]*\bname="([^"]+)"', head)
    names.append(m.group(1) if m else os.path.basename(path))
print(",".join(sorted(names)))
PY
log "whitelist: $(cat "$OUT/usr/share/vedit/ksh-whitelist.txt")"

# --- 6. data: fonts, testdata, desktop/icon ---------------------------------
for f in DejaVuSans.ttf DejaVuSansMono.ttf; do
  [ -f "/usr/share/fonts/truetype/dejavu/$f" ] && install -m 0644 "/usr/share/fonts/truetype/dejavu/$f" "$OUT/share/fonts/$f"
done
cat > "$OUT/etc/fonts/fonts.conf" <<'EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir prefix="relative">../../share/fonts</dir>
  <cachedir>/tmp/vedit-fontconfig-cache</cachedir>
  <config></config>
</fontconfig>
EOF
install -m 0644 "$ROOT/cmake/smoke/testdata/syntax/vedit-smoke.xml" "$OUT/share/ksyntaxhighlighting/testdata/syntax/vedit-smoke.xml"
# the whitelist definitions are LGPL-2.1-or-later derivatives: ship the licence text
if [ -f "$ROOT/resources/syntax/LICENSE-LGPL-2.1" ]; then
  mkdir -p "$OUT/usr/share/doc/vedit/syntax"
  install -m 0644 "$ROOT/resources/syntax/LICENSE-LGPL-2.1" "$OUT/usr/share/doc/vedit/syntax/LICENSE-LGPL-2.1"
  install -m 0644 "$ROOT/resources/syntax/README.md" "$OUT/usr/share/doc/vedit/syntax/README.md"
fi
install -m 0644 "$ROOT/packaging/runtime/assets/vedit.desktop" "$OUT/vedit.desktop"
sed -i "s/@VERSION@/$VERSION/g" "$OUT/vedit.desktop"
install -m 0644 "$ROOT/packaging/runtime/assets/io.github.vedit.VEdit.png" "$OUT/io.github.vedit.VEdit.png"
install -m 0644 "$ROOT/packaging/runtime/assets/io.github.vedit.VEdit.png" "$OUT/.DirIcon"
install -m 0755 "$ROOT/packaging/runtime/AppRun" "$OUT/AppRun"

# --- 7. hard conditions that must hold BEFORE we leave ----------------------
! find "$OUT" -type d -name 'syntax-bundled' | grep -q . || { echo "ERROR: syntax-bundled/ present (C5 violated)" >&2; exit 1; }
active=$(python3 - "$OUT/usr/share/org.kde.syntax-highlighting/syntax" <<'PY'
import glob, os, sys
print(len(glob.glob(os.path.join(sys.argv[1], "*.xml"))))
PY
)
log "active whitelist files: $active (no syntax-bundled/)"

# --- 8. manifest + notices ---------------------------------------------------
{
  echo "# VEdit private runtime manifest (F2)"
  echo "# form=F2 qrc_syntax=OFF no_standard_paths=OFF"
  echo "# os: $( . /etc/os-release; echo "$PRETTY_NAME" )"
  echo "# sha256  size  path"
} > "$OUT/runtime-manifest.txt"
( cd "$OUT" && find . -type f ! -name runtime-manifest.txt -printf '%P\t%s\n' | sort | while IFS=$'\t' read -r p s; do
    printf '%s  %s  %s\n' "$(sha256sum "$p" | cut -d' ' -f1)" "$s" "$p"
  done ) >> "$OUT/runtime-manifest.txt"

{
  echo "VEdit private runtime — components and licences (evidence, not legal advice)"
  echo "form: F2 (self-built KSyntaxHighlighting, QRC_SYNTAX=OFF, NO_STANDARD_PATHS=OFF)"
  echo "whitelist: $(cat "$OUT/usr/share/vedit/ksh-whitelist.txt")"
  echo
  echo "KSyntaxHighlighting  self-built 6.24.0 (KDE tarball, MIT library); definitions: whitelist only (see above)"
  echo "Qt6                  $(dpkg-query -W -f='${Version}' qt6-base-dev 2>/dev/null)  LGPL-3.0 / GPL-3.0 / commercial"
  echo "KF6 (KTextEditor)    $(dpkg-query -W -f='${Version}' libkf6texteditor-dev 2>/dev/null)  LGPL-2.1-or-later"
  echo "tree-sitter          $(dpkg-query -W -f='${Version}' libtree-sitter-dev 2>/dev/null)  MIT"
  echo "glibc                $(dpkg-query -W -f='${Version}' libc6 2>/dev/null)  LGPL-2.1-or-later"
  echo "DejaVu fonts         $(dpkg-query -W -f='${Version}' fonts-dejavu-core 2>/dev/null)  Bitstream Vera + public domain"
  echo
  echo "NOT shipped: KDE systemverilog.xml (GPL-3.0-or-later), KDE verilog.xml (undeclared),"
  echo "             syntax-bundled/, distro libKF6SyntaxHighlighting (embeds 36 strict-GPL definitions)."
} > "$OUT/RUNTIME-NOTICES.txt"

log "AppDir: $OUT files=$(find "$OUT" -type f | wc -l) bytes=$(du -sb "$OUT" | cut -f1)"
log "f2lib sha256: $(sha256sum "$OUT/lib/libKF6SyntaxHighlighting.so.6" | cut -d' ' -f1)"
echo "APPDIR=$OUT"
echo "APPDIR_FORM=F2"
echo "APPDIR_FILES=$(find "$OUT" -type f | wc -l)"
echo "APPDIR_BYTES=$(du -sb "$OUT" | cut -f1)"
echo "APPDIR_WHITELIST=$(cat "$OUT/usr/share/vedit/ksh-whitelist.txt")"

if [ "$(id -u)" = 0 ]; then chown -R 0:0 "$OUT"; fi
