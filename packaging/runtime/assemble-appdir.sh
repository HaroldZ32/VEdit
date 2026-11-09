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
APP="$ROOT/target/qt6-build/src/app/vedit-app"
PROBE_TOOL="$ROOT/target/qt6-build/cmake/smoke/vedit-ksh-env-probe"
KSH_F2=""
OUT="$ROOT/packaging/runtime/out/AppDir"
WHITELIST="$ROOT/resources/syntax"
TOOLCHAIN_VERIBLE=""
TOOLCHAIN_SLANG=""
REQUIRE_TOOLCHAIN=0

while [ $# -gt 0 ]; do
  case "$1" in
    --ksh-f2) KSH_F2=${2:?}; shift 2 ;;
    --out) OUT=${2:?}; shift 2 ;;
    --whitelist) WHITELIST=${2:?}; shift 2 ;;
    --toolchain-verible) TOOLCHAIN_VERIBLE=${2:?}; shift 2 ;;
    --toolchain-slang) TOOLCHAIN_SLANG=${2:?}; shift 2 ;;
    --require-toolchain) REQUIRE_TOOLCHAIN=1; shift ;;
    -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[ -n "$KSH_F2" ] || KSH_F2="$ROOT/target/ksh-selfbuild/F2/install"
[ -d "$KSH_F2" ] || { echo "ERROR: F2 install tree missing: $KSH_F2 (run scripts/build-ksh-selfbuild.sh with KSH_FORM=F2)" >&2; exit 1; }
[ -x "$APP" ] || { echo "ERROR: product binary missing: $APP (run bash scripts/build-qt.sh)" >&2; exit 1; }
[ -x "$PROBE_TOOL" ] || { echo "ERROR: env probe missing: $PROBE_TOOL" >&2; exit 1; }
[ -d "$WHITELIST" ] || { echo "ERROR: whitelist dir missing: $WHITELIST" >&2; exit 1; }

# Pins for the bundled external toolchain (T82). Sourced, never guessed.
# shellcheck source=../../scripts/versions.env
set -a; . "$ROOT/scripts/versions.env"; set +a
: "${VERIBLE_LS_SHA256:?missing VERIBLE_LS_SHA256 pin}"
: "${VERIBLE_LINT_SHA256:?missing VERIBLE_LINT_SHA256 pin}"
: "${VERIBLE_FORMAT_SHA256:?missing VERIBLE_FORMAT_SHA256 pin}"
: "${SLANG_BIN_SHA256:?missing SLANG_BIN_SHA256 pin}"
: "${NOTO_CJK_FONT_SHA256:?missing NOTO_CJK_FONT_SHA256 pin}"

if [ -z "$TOOLCHAIN_VERIBLE" ]; then
  for candidate in "$ROOT/.mpd/recon/verible/bin" "$ROOT"/target/verible-pin/extract/*/bin; do
    [ -d "$candidate" ] || continue
    [ -x "$candidate/verible-verilog-ls" ] || continue
    TOOLCHAIN_VERIBLE=$candidate
    break
  done
fi
if [ -z "$TOOLCHAIN_SLANG" ]; then
  for candidate in "$ROOT/build/slang/out/slang" "$ROOT/target/slang/out/slang"; do
    [ -f "$candidate" ] || continue
    TOOLCHAIN_SLANG=$candidate
    break
  done
fi

LIBDIRS=(/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib /lib)
PLUGROOT=/usr/lib/x86_64-linux-gnu/qt6/plugins
log() { printf '[assemble] %s\n' "$*"; }

F2LIB=$(ls -1 "$KSH_F2"/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.* 2>/dev/null | head -1)
[ -n "$F2LIB" ] || F2LIB=$(ls -1 "$KSH_F2"/lib/libKF6SyntaxHighlighting.so.6.* 2>/dev/null | head -1)
[ -n "$F2LIB" ] && [ -f "$F2LIB" ] || { echo "ERROR: self-built libKF6SyntaxHighlighting.so not found under $KSH_F2" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"/{bin,lib,plugins/platforms,plugins/xcbglintegrations,plugins/kf6/parts} \
         "$OUT"/{share/fonts,etc/fonts} \
         "$OUT"/usr/share/org.kde.syntax-highlighting/syntax "$OUT"/usr/share/vedit

# --- 1. programs -------------------------------------------------------------
# T31: the delivered product binary is the Qt/KTextEditor `vedit-app`, installed as
# `bin/vd-app` (the M0 smoke binary is no longer part of the AppDir).
install -m 0755 "$APP" "$OUT/bin/vd-app"
install -m 0755 "$PROBE_TOOL" "$OUT/bin/vedit-ksh-env-probe"
log "vd-app: $(sha256sum "$APP" | cut -d' ' -f1)  probe: $(sha256sum "$PROBE_TOOL" | cut -d' ' -f1)"

# T18 i18n: the compiled .qm lives in <build>/translations next to the binary and
# is shipped under bin/translations. AppRun starts the product through the bundled
# loader, so the product's applicationDirPath() is the AppDir ROOT and the runtime
# probes <root>/bin/translations first (src/app/i18n.cpp). Offline: a local file,
# no network lookup.
if [ -d "$(dirname "$APP")/translations" ]; then
  mkdir -p "$OUT/bin/translations"
  install -m 0644 "$(dirname "$APP")"/translations/*.qm "$OUT/bin/translations/"
  log "translations: $(cd "$OUT/bin/translations" && ls -1 | tr '\n' ' ')"
else
  log "translations: none in the build tree (UI stays English)"
fi

# --- 2. plugins (dlopen-only) ------------------------------------------------
# ldd cannot see Qt platform plugins (they are dlopen'ed), so they are copied
# explicitly. xcb and offscreen are REQUIRED (the delivery and headless evidence
# both need them); minimal/wayland stay optional.
for p in libqxcb.so libqoffscreen.so; do
  [ -f "$PLUGROOT/platforms/$p" ] || { echo "ERROR: required Qt platform plugin missing: $PLUGROOT/platforms/$p" >&2; exit 1; }
  install -m 0755 "$PLUGROOT/platforms/$p" "$OUT/plugins/platforms/$p"
done
for p in libqminimal.so libqwayland.so; do
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
for root in "$OUT/bin/vd-app" "$OUT/bin/vedit-ksh-env-probe" "$OUT/lib/libKF6SyntaxHighlighting.so.6" \
            "$OUT/plugins/kf6/parts/katepart.so" "$OUT"/plugins/platforms/*.so "$OUT"/plugins/xcbglintegrations/*.so; do
  [ -e "$root" ] || continue
  while read -r dep; do copy_dep "$dep"; done < <(closure "$root")
done

# --- 3b. locale data for the bundled glibc (T115) ---------------------------
# The host CentOS 7 locale archive is glibc-2.17 data and the bundled glibc
# cannot use it (setlocale returns NULL). Ship the build image's tiny C.utf8
# locale (compatible with the bundled glibc) and let AppRun set LOCPATH.
LOCALE_SRC=/usr/lib/locale/C.utf8
if [ -d "$LOCALE_SRC" ]; then
  mkdir -p "$OUT/usr/lib/locale"
  cp -a "$LOCALE_SRC" "$OUT/usr/lib/locale/C.utf8"
  log "locale: C.utf8 $(du -sh "$OUT/usr/lib/locale/C.utf8" | cut -f1) (bundled glibc)"
else
  echo "ERROR: the build image has no /usr/lib/locale/C.utf8; cannot ship a UTF-8 locale" >&2
  exit 1
fi

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
DATA_ROOT="$OUT/usr/share/org.kde.syntax-highlighting"
for x in "$WHITELIST"/*.xml; do
  [ -e "$x" ] || continue
  install -m 0644 "$x" "$DATA_ROOT/syntax/$(basename "$x")"
done
# A3: the five definitions the product needs MUST ship. AppRun narrows
# XDG_DATA_DIRS to the AppDir, so a definition that is absent here has no
# fallback at all: the editor would render that language uncoloured with no
# error. Packaging therefore fails instead of degrading silently.
# `.sdc` / `.xdc` need no definition of their own: the product's extension table
# maps them to the Tcl definition shipped below.
REQUIRED_SYNTAX=(vedit-verilog.xml vedit-systemverilog.xml vedit-vhdl.xml
                 vedit-markdown.xml vedit-tcl.xml)
for required in "${REQUIRED_SYNTAX[@]}"; do
  [ -f "$DATA_ROOT/syntax/$required" ] ||
    { echo "ERROR: required syntax definition not installed: $required (A3)" >&2; exit 1; }
done
# T84/T110: AppRun exports VEDIT_SYNTAX_DIR as the DATA ROOT and the product's
# shared syntax-directory helper probes <root>/syntax, so ONE copy under
# <root>/syntax/ is the complete layout. No root-level XML copy is shipped (it
# would only drift and make the THIRD_PARTY_NOTICES hash ambiguous).
log "syntax data root: $(ls -1 "$DATA_ROOT/syntax"/*.xml 2>/dev/null | wc -l) xml under <root>/syntax/"
# COLLATION CONTRACT with AppRun's ksh_guard: this file is written in Python's
# sorted() order, which for these ASCII names is code-point order = the C
# collation. The guard normalises BOTH sides with `LC_ALL=C sort` before
# comparing, because the caller's locale (zh_CN.UTF-8 / en_US.UTF-8) orders
# `Verilog` before `VHDL` and used to fail a correct package with exit 2
# (A3 blocker 1). Keep the producer and the guard on the same explicit
# collation; never rely on the ambient locale.
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

# --- 5b. external toolchain binaries (T82) ----------------------------------
# CentOS 7 has neither verible nor slang in its repositories, so the delivered
# product carries them. Every binary is pinned by sha256 and the discovery order
# (bundled > local checkout > PATH > env) plus the chosen source is reported by the product; this
# file records what was actually installed.
mkdir -p "$OUT/bin" "$OUT/usr/share/doc/vedit/licenses"
TOOLCHAIN_FILE="$OUT/usr/share/vedit/toolchain.txt"
{
  echo "# VEdit bundled toolchain (T82)"
  echo "# discovery order in the product: bundled > local checkout > PATH > env (source is reported)"
  echo "# expected-bundled-dir: bin/ (next to vd-app)"
} > "$TOOLCHAIN_FILE"
BUNDLED_VERIBLE=0
BUNDLED_SLANG=0
MISSING_TOOLS=""
bundle_binary() {
  local name=$1 pin=$2 src=$3
  [ -f "$src" ] || { echo "ERROR: $name missing at $src" >&2; return 1; }
  local actual
  actual=$(sha256sum "$src" | cut -d' ' -f1)
  if [ "$actual" != "$pin" ]; then
    echo "ERROR: $name sha256 mismatch: got $actual want $pin ($src)" >&2
    return 1
  fi
  install -m 0755 "$src" "$OUT/bin/$name"
  echo "binary $name sha256=$actual"
  echo "$name sha256=$actual bytes=$(stat -c%s "$src") source=$src" >> "$TOOLCHAIN_FILE"
}
if [ -n "$TOOLCHAIN_VERIBLE" ] && [ -d "$TOOLCHAIN_VERIBLE" ]; then
  if bundle_binary verible-verilog-ls "$VERIBLE_LS_SHA256" "$TOOLCHAIN_VERIBLE/verible-verilog-ls" \
     && bundle_binary verible-verilog-lint "$VERIBLE_LINT_SHA256" "$TOOLCHAIN_VERIBLE/verible-verilog-lint" \
     && bundle_binary verible-verilog-format "$VERIBLE_FORMAT_SHA256" "$TOOLCHAIN_VERIBLE/verible-verilog-format"; then
    BUNDLED_VERIBLE=1
    install -m 0644 "$ROOT/docs/licenses/Apache-2.0.txt" "$OUT/usr/share/doc/vedit/licenses/Apache-2.0.txt"
    echo "license Apache-2.0=usr/share/doc/vedit/licenses/Apache-2.0.txt" >> "$TOOLCHAIN_FILE"
    log "toolchain: verible bundled (3 binaries, sha256 pinned)"
  else
    MISSING_TOOLS="${MISSING_TOOLS}verible,"
  fi
else
  MISSING_TOOLS="${MISSING_TOOLS}verible,"
fi
if [ -n "$TOOLCHAIN_SLANG" ] && [ -f "$TOOLCHAIN_SLANG" ]; then
  if bundle_binary slang "$SLANG_BIN_SHA256" "$TOOLCHAIN_SLANG"; then
    BUNDLED_SLANG=1
    install -m 0644 "$ROOT/docs/licenses/slang-LICENSE.txt" "$OUT/usr/share/doc/vedit/licenses/slang-LICENSE.txt"
    echo "license MIT=usr/share/doc/vedit/licenses/slang-LICENSE.txt" >> "$TOOLCHAIN_FILE"
    log "toolchain: slang bundled (sha256 pinned)"
  else
    MISSING_TOOLS="${MISSING_TOOLS}slang,"
  fi
else
  MISSING_TOOLS="${MISSING_TOOLS}slang,"
fi
MISSING_TOOLS=${MISSING_TOOLS%,}
if [ -n "$MISSING_TOOLS" ]; then
  echo "missing=[$MISSING_TOOLS]" >> "$TOOLCHAIN_FILE"
  echo "fix=put the binary in bin/ next to vd-app, install it on PATH, or set VEDIT_VERIBLE_LS / VEDIT_SLANG" >> "$TOOLCHAIN_FILE"
  log "toolchain: NOT bundled for [$MISSING_TOOLS] (the product will report this explicitly)"
  [ "$REQUIRE_TOOLCHAIN" -eq 0 ] || { echo "ERROR: --require-toolchain but missing [$MISSING_TOOLS]" >&2; exit 1; }
else
  echo "missing=[]" >> "$TOOLCHAIN_FILE"
  log "toolchain: complete (verible + slang), missing=[]"
fi

# --- 5c. VCS toolchain: git + subversion bundled (rev3 / T23) ----------------
# The product ships its own runtime, so it carries the VCS tools too: the host
# may have no git/svn, or an ancient one. The binaries are unpacked from
# sha256-pinned .debs into usr/lib/vcs/ and started through the AppDir's own
# loader, with the VCS libraries in their OWN directory — the product's
# Qt/libssl/libsqlite3 are never used by, and never shadowed by, these
# processes. svnadmin is extracted to build/vcs-tools/ for E2E only and must NOT
# appear in the AppDir.
VCS="$OUT/usr/lib/vcs"
VCS_CACHE="${VEDIT_VCS_DEB_CACHE:-$ROOT/packaging/runtime/.cache/vcs-debs}"
VCS_STAGE="$VCS_CACHE/stage"
VCS_TOOLS="$ROOT/build/vcs-tools"
: "${VCS_GIT_VERSION:?missing VCS_GIT_VERSION}" "${VCS_SVN_VERSION:?missing VCS_SVN_VERSION}"
: "${VCS_GIT_MIN_VERSION:?missing VCS_GIT_MIN_VERSION}" "${VCS_SVN_MIN_VERSION:?missing VCS_SVN_MIN_VERSION}"
[ "${#VCS_DEB_URLS[@]}" -eq "${#VCS_DEB_SHA256S[@]}" ] || {
  echo "ERROR: VCS_DEB_URLS/VCS_DEB_SHA256S length mismatch" >&2; exit 1; }
mkdir -p "$VCS_CACHE" "$VCS/bin" "$VCS/libexec/helpers" "$VCS/git-core" \
         "$VCS/lib" "$VCS/share/git-core" "$VCS_TOOLS"
for i in "${!VCS_DEB_URLS[@]}"; do
  url=${VCS_DEB_URLS[$i]}
  pin=${VCS_DEB_SHA256S[$i]}
  deb="$VCS_CACHE/$(basename "$url")"
  if [ ! -f "$deb" ]; then
    log "vcs: fetching $(basename "$url")"
    curl -fsSL "$url" -o "$deb" || { echo "ERROR: cannot fetch $url" >&2; exit 1; }
  fi
  actual=$(sha256sum "$deb" | cut -d' ' -f1)
  [ "$actual" = "$pin" ] || {
    echo "ERROR: $(basename "$url") sha256 mismatch: got=$actual want=$pin" >&2; exit 1; }
done
rm -rf "$VCS_STAGE"
mkdir -p "$VCS_STAGE"
for i in "${!VCS_DEB_URLS[@]}"; do
  dpkg-deb -x "$VCS_CACHE/$(basename "${VCS_DEB_URLS[$i]}")" "$VCS_STAGE"
done
install -m 0755 "$VCS_STAGE/usr/bin/git" "$VCS/libexec/git"
install -m 0755 "$VCS_STAGE/usr/bin/svn" "$VCS/libexec/svn"
install -m 0755 "$VCS_STAGE/usr/bin/svnadmin" "$VCS_TOOLS/svnadmin.bin"

# One wrapper shape for every entry point: it derives the AppDir from its own
# real path (four levels up from <appdir>/usr/lib/vcs/bin[/git-core]) and starts
# the bundled binary through the AppDir loader + the VCS library directory.
write_vcs_wrapper() { # <target> <kind: git|svn|helper>
  local target=$1 kind=$2
  {
    echo '#!/bin/sh'
    echo '# Generated by packaging/runtime/assemble-appdir.sh - do not edit.'
    echo 'set -u'
    echo 'SELF=$(readlink -f -- "$0" 2>/dev/null || printf "%s" "$0")'
    echo 'DIR=$(dirname -- "$SELF")'
    echo 'APPDIR=$(CDPATH= cd -- "$DIR/../../../../" && pwd -P)'
    echo 'NAME=$(basename -- "$0")'
    # git re-executes itself for some subcommands (`git maintenance run`,
    # `git reset` in worktree add, ...). Give those children the AppDir's bin
    # first on PATH, so they resolve to this wrapper no matter how the parent
    # was started.
    echo 'PATH="$APPDIR/bin${PATH:+:$PATH}"'
    echo 'export PATH'
    case "$kind" in
      git)
        echo 'exec /usr/bin/env GIT_EXEC_PATH="$APPDIR/usr/lib/vcs/git-core" GIT_TEMPLATE_DIR="$APPDIR/usr/lib/vcs/share/git-core/templates" \'
        echo '  "$APPDIR/ld-linux-x86-64.so.2" --library-path "$APPDIR/usr/lib/vcs/lib:$APPDIR/lib" \'
        echo '  --argv0 "$NAME" "$APPDIR/usr/lib/vcs/libexec/git" "$@"'
        ;;
      svn)
        echo 'exec /usr/bin/env "$APPDIR/ld-linux-x86-64.so.2" \'
        echo '  --library-path "$APPDIR/usr/lib/vcs/lib:$APPDIR/lib" \'
        echo '  "$APPDIR/usr/lib/vcs/libexec/svn" "$@"'
        ;;
      helper)
        echo 'exec /usr/bin/env "$APPDIR/ld-linux-x86-64.so.2" \'
        echo '  --library-path "$APPDIR/usr/lib/vcs/lib:$APPDIR/lib" \'
        echo '  "$APPDIR/usr/lib/vcs/libexec/helpers/$NAME" "$@"'
        ;;
    esac
  } > "$target"
  chmod 0755 "$target"
}
write_vcs_wrapper "$VCS/bin/git" git
write_vcs_wrapper "$VCS/bin/svn" svn
# git dispatches subcommands by argv[0]: builtins are symlinks to `git` in the
# distribution, so they become symlinks to our wrapper (the wrapper passes the
# basename through `--argv0`); external helpers are real files and get their own
# wrapper so they also start through the AppDir loader.
for entry in "$VCS_STAGE"/usr/lib/git-core/*; do
  name=$(basename "$entry")
  [ "$name" = "git" ] && continue
  if [ -L "$entry" ]; then
    ln -sfn ../bin/git "$VCS/git-core/$name"
  elif [ -f "$entry" ]; then
    install -m 0755 "$entry" "$VCS/libexec/helpers/$name"
    write_vcs_wrapper "$VCS/git-core/$name" helper
  fi
done
if [ -d "$VCS_STAGE/usr/share/git-core/templates" ]; then
  cp -a "$VCS_STAGE/usr/share/git-core/templates" "$VCS/share/git-core/"
fi

# Library closure: resolve against the staged libs, then copy everything except
# the glibc family (the AppDir ships exactly one glibc, in lib/).
GLIBC_FAMILY=" libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1 libresolv.so.2 libutil.so.1 libanl.so.1 libmvec.so.1 "
vcs_closure() { ldd "$1" 2>/dev/null | awk '/=> \// {print $3} /^[ \t]*\/.*\.so/ {print $1}'; }
copy_vcs_lib() {
  local dep=$1 base real
  [ -e "$dep" ] || return 0
  base=$(basename "$dep")
  case "$GLIBC_FAMILY" in *" $base "*) return 0 ;; esac
  [ -f "$VCS/lib/$base" ] && return 0
  real=$(readlink -f "$dep")
  [ -f "$real" ] || return 0
  install -m 0644 "$real" "$VCS/lib/$base"
}
for binary in "$VCS/libexec/git" "$VCS/libexec/svn" "$VCS"/libexec/helpers/*; do
  [ -f "$binary" ] || continue
  while read -r dep; do
    copy_vcs_lib "$dep"
  done < <(LD_LIBRARY_PATH="$VCS_STAGE/usr/lib/x86_64-linux-gnu" vcs_closure "$binary")
done

# Top-level entry points: the resolver's `bundled` candidate bin/<tool> and the
# plain shell convenience (`git`/`svn` inside AppRun).
ln -sfn ../usr/lib/vcs/bin/git "$OUT/bin/git"
ln -sfn ../usr/lib/vcs/bin/svn "$OUT/bin/svn"

# build-only svnadmin (E2E in batch 6b); never shipped inside the AppDir
{
  echo '#!/bin/sh'
  echo '# Generated by packaging/runtime/assemble-appdir.sh - build-only E2E tool.'
  echo '# Default AppDir: resolved RELATIVE to this wrapper, so it works both in'
  echo '# the build image (repo at /vedit-build) and on the host checkout.'
  echo 'set -u'
  echo 'SELF=$(readlink -f -- "$0" 2>/dev/null || printf "%s" "$0")'
  echo 'TOOLS=$(dirname -- "$SELF")'
  echo 'APPDIR=${VEDIT_APPDIR:-$(CDPATH= cd -- "$TOOLS/../../packaging/runtime/out/AppDir" && pwd -P)}'
  printf 'exec /usr/bin/env "$APPDIR/ld-linux-x86-64.so.2" --library-path "$APPDIR/usr/lib/vcs/lib:$APPDIR/lib" "$TOOLS/svnadmin.bin" "$@"\n'
} > "$VCS_TOOLS/svnadmin"
chmod 0755 "$VCS_TOOLS/svnadmin"

# Licence texts for the bundled VCS stack (GPLv2 for git, Apache-2.0 for svn).
if [ -f /usr/share/common-licenses/GPL-2 ]; then
  install -m 0644 /usr/share/common-licenses/GPL-2 "$OUT/usr/share/doc/vedit/licenses/git-GPL-2.0.txt"
fi
install -m 0644 "$ROOT/docs/licenses/Apache-2.0.txt" "$OUT/usr/share/doc/vedit/licenses/Apache-2.0.txt"

# Build-time proof that the wrappers really run the bundled versions.
git_ver=$("$VCS/bin/git" --version 2>/dev/null | awk '{print $3}')
svn_ver=$("$VCS/bin/svn" --version --quiet 2>/dev/null)
[ "$git_ver" = "$VCS_GIT_VERSION" ] || {
  echo "ERROR: bundled git reports '$git_ver', expected $VCS_GIT_VERSION" >&2; exit 1; }
[ "$svn_ver" = "$VCS_SVN_VERSION" ] || {
  echo "ERROR: bundled svn reports '$svn_ver', expected $VCS_SVN_VERSION" >&2; exit 1; }
{
  echo "# VEdit bundled VCS toolchain (rev3, T23)"
  echo "# started through the AppDir loader: <appdir>/ld-linux-x86-64.so.2 --library-path usr/lib/vcs/lib:lib"
  echo "git version=$git_ver min=$VCS_GIT_MIN_VERSION path=bin/git executable=usr/lib/vcs/libexec/git sha256=$(sha256sum "$VCS/libexec/git" | cut -d' ' -f1)"
  echo "svn version=$svn_ver min=$VCS_SVN_MIN_VERSION path=bin/svn executable=usr/lib/vcs/libexec/svn sha256=$(sha256sum "$VCS/libexec/svn" | cut -d' ' -f1)"
  echo "vcs_lib_files=$(find "$VCS/lib" -type f | wc -l) git_core_entries=$(find "$VCS/git-core" -maxdepth 1 | wc -l)"
  echo "svnadmin=not-shipped build_only=$VCS_TOOLS/svnadmin"
  for i in "${!VCS_DEB_URLS[@]}"; do
    echo "deb $(basename "${VCS_DEB_URLS[$i]}") sha256=${VCS_DEB_SHA256S[$i]} url=${VCS_DEB_URLS[$i]}"
  done
} > "$OUT/usr/share/vedit/vcs-toolchain.txt"
log "vcs: git=$git_ver svn=$svn_ver libs=$(find "$VCS/lib" -type f | wc -l) git-core=$(find "$VCS/git-core" -maxdepth 1 | wc -l) (svnadmin build-only, not shipped)"

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
  <!-- T119: Latin keeps DejaVu first; CJK falls back to the packaged Noto.
       The editor (monospace) gets the Mono CJK face so full-width comments
       align in columns. -->
  <alias binding="same"><family>DejaVu Sans</family><prefer><family>Noto Sans CJK SC</family></prefer></alias>
  <alias binding="same"><family>DejaVu Sans Mono</family><prefer><family>Noto Sans Mono CJK SC</family></prefer></alias>
  <config></config>
</fontconfig>
EOF
# the whitelist definitions are LGPL-2.1-or-later derivatives: ship the licence text
if [ -f "$ROOT/resources/syntax/LICENSE-LGPL-2.1" ]; then
  mkdir -p "$OUT/usr/share/doc/vedit/syntax"
  install -m 0644 "$ROOT/resources/syntax/LICENSE-LGPL-2.1" "$OUT/usr/share/doc/vedit/syntax/LICENSE-LGPL-2.1"
  install -m 0644 "$ROOT/resources/syntax/README.md" "$OUT/usr/share/doc/vedit/syntax/README.md"
fi
# T119: packaged CJK font (OFL-1.1) + the licence text. The fontconfig aliases
# above make the UI fall back to Noto Sans CJK SC and the editor to
# Noto Sans Mono CJK SC (Latin stays DejaVu).
FONT_SRC="$ROOT/packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc"
FONT_LICENSE_SRC="$ROOT/packaging/runtime/assets/licenses/Noto-CJK-OFL-1.1.txt"
[ -f "$FONT_SRC" ] || { echo "ERROR: packaged CJK font missing: $FONT_SRC" >&2; exit 1; }
font_sha=$(sha256sum "$FONT_SRC" | cut -d' ' -f1)
[ "$font_sha" = "$NOTO_CJK_FONT_SHA256" ] || {
  echo "ERROR: Noto CJK font sha256 mismatch: want=$NOTO_CJK_FONT_SHA256 got=$font_sha" >&2; exit 1; }
install -m 0644 "$FONT_SRC" "$OUT/share/fonts/NotoSansCJK-Regular.ttc"
mkdir -p "$OUT/usr/share/doc/vedit/licenses"
install -m 0644 "$FONT_LICENSE_SRC" "$OUT/usr/share/doc/vedit/licenses/Noto-CJK-OFL-1.1.txt"
log "cjk font: $(stat -c%s "$OUT/share/fonts/NotoSansCJK-Regular.ttc") bytes sha256=$font_sha; licence: usr/share/doc/vedit/licenses/Noto-CJK-OFL-1.1.txt"

install -m 0644 "$ROOT/packaging/runtime/assets/vedit.desktop" "$OUT/vedit.desktop"
sed -i "s/@VERSION@/$VERSION/g" "$OUT/vedit.desktop"
# UI-Mosaic A4: the icon chain is generated by scripts/build-app-icons.py and
# committed (resources/icons/**). The AppDir root icon and .DirIcon are the
# 256 px LIGHT artwork, and the same artwork is installed into the hicolor tree
# so a desktop that integrates the AppDir picks a native size instead of
# scaling one 512 px master. Missing sources are a hard error here: silently
# falling back to the old placeholder is how the icon chain rots.
ICON_SET="$ROOT/resources/icons/hicolor"
ICON_256="$ICON_SET/256x256/apps/io.github.vedit.VEdit.png"
[ -f "$ICON_256" ] || {
  echo "ERROR: AppDir icon source missing: $ICON_256 (run scripts/build-app-icons.py)" >&2; exit 1; }
install -m 0644 "$ICON_256" "$OUT/io.github.vedit.VEdit.png"
install -m 0644 "$ICON_256" "$OUT/.DirIcon"
for size in 16 24 32 48 64 128 256 512; do
  icon="$ICON_SET/${size}x${size}/apps/io.github.vedit.VEdit.png"
  [ -f "$icon" ] || {
    echo "ERROR: hicolor icon missing: $icon (run scripts/build-app-icons.py)" >&2; exit 1; }
  mkdir -p "$OUT/usr/share/icons/hicolor/${size}x${size}/apps"
  install -m 0644 "$icon" "$OUT/usr/share/icons/hicolor/${size}x${size}/apps/io.github.vedit.VEdit.png"
done
log "icons: AppDir root + .DirIcon + hicolor 16..512 from resources/icons/hicolor (256px light)"
install -m 0755 "$ROOT/packaging/runtime/AppRun" "$OUT/AppRun"

# --- 7. hard conditions that must hold BEFORE we leave ----------------------
# `find -print -quit` stops find itself: a `find ... | grep -q .` pipeline would
# let grep exit on the first hit and SIGPIPE find (this script sets pipefail),
# which reads as "not found" and would wave a forbidden syntax-bundled/ through.
if [ -n "$(find "$OUT" -type d -name 'syntax-bundled' -print -quit)" ]; then
  echo "ERROR: syntax-bundled/ present (C5 violated)" >&2
  exit 1
fi
active=$(python3 - "$OUT/usr/share/org.kde.syntax-highlighting/syntax" <<'PY'
import glob, os, sys
print(len(glob.glob(os.path.join(sys.argv[1], "*.xml"))))
PY
)
log "active whitelist files: $active (no syntax-bundled/)"

# --- 7b. delivery invariants (T31) ------------------------------------------
for required in bin/vd-app bin/verible-verilog-ls bin/slang \
                bin/git bin/svn usr/lib/vcs/bin/git usr/lib/vcs/bin/svn \
                usr/lib/vcs/libexec/git usr/lib/vcs/libexec/svn \
                usr/share/vedit/vcs-toolchain.txt \
                plugins/platforms/libqxcb.so plugins/platforms/libqoffscreen.so \
                plugins/kf6/parts/katepart.so usr/share/org.kde.syntax-highlighting/syntax/vedit-verilog.xml \
                usr/lib/locale/C.utf8/LC_CTYPE \
                share/fonts/NotoSansCJK-Regular.ttc \
                usr/share/doc/vedit/licenses/Noto-CJK-OFL-1.1.txt; do
  [ -e "$OUT/$required" ] || { echo "ERROR: delivery artifact missing: $required" >&2; exit 1; }
done
[ -z "$(find "$OUT/bin" -maxdepth 1 -name 'vedit-m0-smoke' -print -quit)" ] || {
  echo "ERROR: M0 smoke binary must not be part of the delivery AppDir" >&2; exit 1;
}
[ -z "$(find "$OUT" -name 'svnadmin' -print -quit)" ] || {
  echo "ERROR: svnadmin must not be shipped in the AppDir (build-only tool)" >&2; exit 1;
}
# UI-Mosaic A4: an installed launcher whose icon files are absent is worse than
# no icon at all — the desktop shows a generic placeholder and never says why.
# Missing or EMPTY artifacts in the icon chain therefore fail the build.
for icon_required in vedit.desktop io.github.vedit.VEdit.png .DirIcon \
                     usr/share/icons/hicolor/16x16/apps/io.github.vedit.VEdit.png \
                     usr/share/icons/hicolor/64x64/apps/io.github.vedit.VEdit.png \
                     usr/share/icons/hicolor/256x256/apps/io.github.vedit.VEdit.png \
                     usr/share/icons/hicolor/512x512/apps/io.github.vedit.VEdit.png; do
  [ -s "$OUT/$icon_required" ] || {
    echo "ERROR: icon-chain artifact missing or empty: $icon_required" >&2; exit 1; }
done
png_magic=$(head -c 8 "$OUT/io.github.vedit.VEdit.png" | od -An -tx1 | tr -d ' \n')
[ "$png_magic" = "89504e470d0a1a0a" ] || {
  echo "ERROR: io.github.vedit.VEdit.png is not a PNG (magic=$png_magic)" >&2; exit 1; }
grep -qx 'Icon=io.github.vedit.VEdit' "$OUT/vedit.desktop" || {
  echo "ERROR: vedit.desktop lacks Icon=io.github.vedit.VEdit" >&2; exit 1; }
grep -qx 'StartupWMClass=io.github.vedit.VEdit' "$OUT/vedit.desktop" || {
  echo "ERROR: vedit.desktop lacks StartupWMClass=io.github.vedit.VEdit" >&2; exit 1; }
log "icon invariants: vedit.desktop + StartupWMClass + root/16/64/256/512 PNGs present"
log "delivery invariants: vd-app + Qt platform plugins + toolchain + whitelist present"

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
  echo "git (bundled)       ${VCS_GIT_VERSION}  GPL-2.0-only  bin/git -> usr/lib/vcs/  licence: usr/share/doc/vedit/licenses/git-GPL-2.0.txt"
  echo "Subversion (bundled) ${VCS_SVN_VERSION}  Apache-2.0    bin/svn -> usr/lib/vcs/  licence: usr/share/doc/vedit/licenses/Apache-2.0.txt"
  echo "  bundled with: APR / APR-util / serf (Apache-2.0), utf8proc (MIT), and the runtime libraries they link."
  echo "  svnadmin is NOT shipped (build-only tool under build/vcs-tools/)."
  echo
  if [ "$BUNDLED_VERIBLE" -eq 1 ]; then
    echo "Verible (bundled)    ${VERIBLE_VERSION:-pinned by versions.env}  Apache-2.0  bin/verible-verilog-{ls,lint,format}"
    echo "  licence text: usr/share/doc/vedit/licenses/Apache-2.0.txt"
  else
    echo "Verible              NOT bundled (user/PATH/env must provide it)"
  fi
  if [ "$BUNDLED_SLANG" -eq 1 ]; then
    echo "slang (bundled)      12.1  MIT  bin/slang"
    echo "  licence text: usr/share/doc/vedit/licenses/slang-LICENSE.txt"
  else
    echo "slang                NOT bundled (user/PATH/env must provide it)"
  fi
  echo "Toolchain discovery  bundled > local checkout > PATH > env; the chosen source and the missing list are"
  echo "                     reported in the status bar, the outline/diagnostics panel and --selfcheck."
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
