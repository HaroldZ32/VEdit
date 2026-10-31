#!/usr/bin/env bash
# Runs INSIDE the pinned M0 Qt6 image; invoked by scripts/build-ksh-selfbuild.sh.
# Builds the self-built KSyntaxHighlighting (QRC_SYNTAX=OFF, NO_STANDARD_PATHS=ON)
# and verifies G2/G3 + ABI + behaviour. It never builds the image and never
# touches the host outside the workspace mount.
set -euo pipefail

WORK="${VEDIT_WORKSPACE:-/vedit-build}"
# shellcheck source=versions-ksh.env
set -a
. "$WORK/scripts/versions-ksh.env"
set +a

# Form (T61): F1 = NO_STANDARD_PATHS=ON, F2 = NO_STANDARD_PATHS=OFF. The two
# forms share every other option (QRC_SYNTAX=OFF) and get separate build trees
# and evidence directories, so both comparisons can be kept side by side.
form="${1:-$KSH_FORM}"
case "$form" in
  F1) nosp="ON" ;;
  F2) nosp="OFF" ;;
  *) echo "FATAL: form must be F1 or F2 (got '$form')" >&2; exit 2 ;;
esac
form_tag=$(printf '%s' "$form" | tr 'A-Z' 'a-z')
if [ "$form" = "$KSH_FORM" ]; then evid_sub="build-logs"; else evid_sub="build-logs-form-$form_tag"; fi

KSH_ROOT="$WORK/$KSH_BUILD_ROOT/$form"
SRC="$KSH_ROOT/src"
BUILD="$KSH_ROOT/build"
PREFIX="$KSH_ROOT/install"
EVID="$WORK/$KSH_EVIDENCE_DIR/$evid_sub"
mkdir -p "$KSH_ROOT" "$EVID"
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

echo "== [1/7] pinned source tarball =="
TARBALL="$KSH_ROOT/syntax-highlighting-$KSH_VERSION.tar.xz"
if ! printf '%s  %s\n' "$KSH_TARBALL_SHA256" "$TARBALL" | sha256sum -c - >/dev/null 2>&1; then
  curl -fsSL -o "$TARBALL" "$KSH_TARBALL_URL"
fi
{
  printf '%s  %s\n' "$KSH_TARBALL_SHA256" "$TARBALL" | sha256sum -c -
  echo "url=$KSH_TARBALL_URL"
  echo "tag=$KSH_TAG commit=$KSH_COMMIT version=$KSH_VERSION"
  sha256sum "$TARBALL"
} | tee "$EVID/01-source.txt"
rm -rf "$SRC"
mkdir -p "$SRC"
tar -xJf "$TARBALL" -C "$SRC" --strip-components=1

echo "== [2/7] configure with the B' options =="
rm -rf "$BUILD"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_SKIP_INSTALL_RPATH=ON \
  -DBUILD_TESTING="$KSH_BUILD_TESTING" \
  -DQRC_SYNTAX="$KSH_QRC_SYNTAX" \
  -DNO_STANDARD_PATHS="$nosp" \
  -DKSYNTAXHIGHLIGHTING_USE_GUI="$KSH_USE_GUI" \
  2>&1 | tee "$EVID/10-configure.log"

echo "== [3/7] build + install =="
cmake --build "$BUILD" --parallel "$(nproc)" 2>&1 | tee "$EVID/11-build.log"
rm -rf "$PREFIX"
cmake --install "$BUILD" 2>&1 | tee "$EVID/12-install.log"

echo "== [4/7] G3: build configuration is the cause of the clean library =="
{
  echo "## CMakeCache keys (G3)"
  grep -E '^(QRC_SYNTAX|NO_STANDARD_PATHS|BUILD_TESTING|CMAKE_BUILD_TYPE|CMAKE_INSTALL_PREFIX):' "$BUILD/CMakeCache.txt"
} | tee "$EVID/20-cmakecache-keys.txt"
grep -q "^QRC_SYNTAX:BOOL=${KSH_QRC_SYNTAX}$" "$BUILD/CMakeCache.txt" \
  || { echo "FATAL G3: QRC_SYNTAX is not $KSH_QRC_SYNTAX" >&2; exit 1; }
grep -q "^NO_STANDARD_PATHS:BOOL=${nosp}$" "$BUILD/CMakeCache.txt" \
  || { echo "FATAL G3: NO_STANDARD_PATHS is not $nosp" >&2; exit 1; }
echo "G3 PASS"

echo "== [5/7] installed library identity + G2 (no syntax definition embedded) =="
LIB=$(find "$PREFIX" -name 'libKF6SyntaxHighlighting.so.6.*' -type f | head -1)
[ -n "$LIB" ] || { echo "FATAL: no installed libKF6SyntaxHighlighting.so.6.* found" >&2; exit 1; }
LIBDIR=$(dirname "$LIB")
{
  echo "## installed tree (depth 5)"
  find "$PREFIX" -maxdepth 5 | sort
  echo
  echo "## library"
  ls -l "$LIB" "$LIBDIR"/libKF6SyntaxHighlighting.so*
  sha256sum "$LIB"
  stat -c 'size=%s' "$LIB"
  echo "## SONAME"
  readelf -d "$LIB" | grep SONAME
  echo "## RPATH/RUNPATH"
  readelf -d "$LIB" | grep -E 'RPATH|RUNPATH' || echo '(none)'
  echo "## NEEDED"
  readelf -d "$LIB" | sed -n '/NEEDED/p'
} | tee "$EVID/21-library-identity.txt"

# G2: scan the UTF-16LE resource-name table for *.xml entries. No zstandard
# needed -- this is exactly the table the distro library fills with 450 names.
set +e
python3 - "$LIB" <<'PY' 2>&1 | tee "$EVID/30-g2-resource-names.txt"
import re
import sys
data = open(sys.argv[1], "rb").read()
texts = [data[offset:].decode("utf-16-le", "ignore") for offset in (0, 1)]
xml, theme = set(), set()
for text in texts:
    xml.update(re.findall(r"[A-Za-z0-9][A-Za-z0-9._+-]*\.xml", text))
    theme.update(re.findall(r"[A-Za-z0-9][A-Za-z0-9._+-]*\.theme", text))
bad = sorted(name for name in xml if "systemverilog" in name.lower())
print(f"xml resource names: {len(xml)} -> {sorted(xml)[:10]}")
print(f"theme resource names: {len(theme)} -> {sorted(theme)[:5]}")
print(f"systemverilog.xml present: {bool(bad)} {bad}")
if xml:
    print("RESULT: FAIL - syntax definition files are still embedded")
    sys.exit(1)
print("RESULT: PASS - no syntax definition is embedded (themes stay: upstream has no switch)")
PY
g2=$?
set -e
echo "G2 exit=$g2 (expect 0)"
[ "$g2" -eq 0 ] || { echo "FATAL G2: syntax definitions are still embedded" >&2; exit 1; }

echo "== [6/7] ABI with the distro KTextEditor 6.24.0 / katepart =="
DISTRO_LIB=/usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6
KTE_LIB=/usr/lib/x86_64-linux-gnu/libKF6TextEditor.so.6
KATEPART=/usr/lib/x86_64-linux-gnu/qt6/plugins/kf6/parts/katepart.so
DISTRO_SYMS=$(mktemp)
OUR_SYMS=$(mktemp)
CONSUMERS_UNDEF=$(mktemp)
EXPECTED=$(mktemp)
MISSING=$(mktemp)
nm -D --defined-only "$DISTRO_LIB" | awk '{print $3}' | sort -u > "$DISTRO_SYMS"
nm -D --defined-only "$LIB" | awk '{print $3}' | sort -u > "$OUR_SYMS"
# The ABI question is not "do the two libraries export the same set" (C++ ABI
# plumbing such as typeinfo for std::make_shared differs with build flags), it is
# "does every symbol KTextEditor / katepart actually need resolve against ours".
{ nm -D --undefined-only "$KTE_LIB"; nm -D --undefined-only "$KATEPART"; } \
  | awk 'NF >= 2 {print $2}' | sed 's/@.*//' | sort -u > "$CONSUMERS_UNDEF"
comm -12 "$CONSUMERS_UNDEF" "$DISTRO_SYMS" > "$EXPECTED"
comm -23 "$EXPECTED" "$OUR_SYMS" > "$MISSING"
{
  echo "## defined dynamic symbols"
  echo "distro:     $(wc -l < "$DISTRO_SYMS")"
  echo "self-built: $(wc -l < "$OUR_SYMS")"
  echo
  echo "## symbols KTextEditor + katepart need that the distro library provides: $(wc -l < "$EXPECTED")"
  echo "## of those, missing from the self-built library (must be empty):"
  cat "$MISSING"
  echo
  echo "## export-set differences (informational; C++ ABI plumbing varies with build flags)"
  echo "-- in distro, not in ours:"
  comm -23 "$DISTRO_SYMS" "$OUR_SYMS"
  echo "-- in ours, not in distro (first 20):"
  # `sed -n '1,20p'` consumes the whole stream; `head -20` exits early and would
  # SIGPIPE `comm` under `set -o pipefail`, failing the script on a merely
  # informational listing.
  comm -13 "$DISTRO_SYMS" "$OUR_SYMS" | sed -n '1,20p'
} | tee "$EVID/31-symbol-diff.txt"
if [ -s "$MISSING" ]; then
  echo "FATAL ABI: the self-built library is missing symbols KTextEditor/katepart require" >&2
  exit 1
fi

# Build the M0 smoke binary (same source the T10 gate builds) and run it against
# the overridden library.
echo "== [6/7] build the M0 smoke target =="
# Only the M0 smoke target is built (--target below), so a parallel lane's
# application sources cannot affect this probe; the configure still sees the
# real tree, exactly like the normal build does.
cmake -S "$WORK" -B "$WORK/target/qt6-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra" 2>&1 | tee "$EVID/40-smoke-configure.log"
cmake --build "$WORK/target/qt6-build" --parallel "$(nproc)" \
  --target vedit-m0-smoke vedit-ksh-env-probe \
  2>&1 | tee "$EVID/41-smoke-build.log"
SMOKE="$WORK/target/qt6-build/cmake/smoke/vedit-m0-smoke"
[ -x "$SMOKE" ] || { echo "FATAL: the smoke binary was not produced" >&2; exit 1; }

{
  echo "## which libKF6SyntaxHighlighting resolves with the override (ldd)"
  LD_LIBRARY_PATH="$LIBDIR" ldd "$SMOKE" | grep -E 'KF6SyntaxHighlighting|KF6TextEditor'
  echo
  echo "## unresolved relocations in KTextEditor when linked against our library"
  LD_LIBRARY_PATH="$LIBDIR" ldd -r /usr/lib/x86_64-linux-gnu/libKF6TextEditor.so.6 2>&1 | grep -i 'undefined' \
    || echo '(none)'
  echo
  echo "## unresolved relocations in katepart when linked against our library"
  LD_LIBRARY_PATH="$LIBDIR" ldd -r /usr/lib/x86_64-linux-gnu/qt6/plugins/kf6/parts/katepart.so 2>&1 | grep -i 'undefined' \
    || echo '(none)'
} | tee "$EVID/32-abi-loading.txt"
grep -q "$LIBDIR" "$EVID/32-abi-loading.txt" \
  || { echo "FATAL ABI: the smoke binary did not resolve our library" >&2; exit 1; }

echo "== [7/7] behaviour: empty library, then whitelist + Verilog highlighting =="
# The whitelist is exactly one plain XML file (upstream data/syntax/verilog.xml,
# which declares license="" upstream). This proves the MECHANISM; the shipping
# whitelist itself is T16 / ADR §5's decision.
WHITELIST="$KSH_ROOT/whitelist"
rm -rf "$WHITELIST"
mkdir -p "$WHITELIST/syntax"
cp "$SRC/data/syntax/verilog.xml" "$WHITELIST/syntax/verilog.xml"
{
  echo "## whitelist fixture (plain XML, loaded through addCustomSearchPath)"
  sha256sum "$WHITELIST/syntax/verilog.xml"
  grep -o '<language[^>]*>' "$WHITELIST/syntax/verilog.xml" | head -1
} | tee "$EVID/50-whitelist.txt"

run_smoke() {
  local log="$1"
  shift
  set +e
  LD_LIBRARY_PATH="$LIBDIR" QT_QPA_PLATFORM=offscreen \
    "$SMOKE" --selfcheck --ksh-selfbuild --expect-kshlib-prefix "$LIBDIR" "$@" \
    > "$log" 2>&1
  local status=$?
  set -e
  grep -vE '^(Detected locale|Qt depends|If this|for more|Fontconfig error)' "$log"
  echo "exit=$status"
  return "$status"
}

echo "-- [7a] no definitions provided: must not crash, zero valid built-in definitions"
run_smoke "$EVID/51-run-empty-definitions.log" --expect-empty-syntax

echo "-- [7b] whitelisted Verilog definition: by name, then real highlighting"
run_smoke "$EVID/52-run-whitelist-verilog.log" --expect-empty-syntax \
  --custom-syntax-dir "$WHITELIST" --highlight-check

echo "[ksh] ALL CHECKS PASS"
