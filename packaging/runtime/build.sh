#!/usr/bin/env bash
# One command for the VEdit private runtime — F2 form (T64).
#   assemble (self-built KSyntaxHighlighting + whitelist) -> licence gate ->
#   self-check (AppDir) -> F2 isolation -> AppImage -> self-check + isolation on
#   the AppImage -> CentOS 7 mechanism probe.
#
# The F2 form (T61) makes the packaging the source of truth for which syntax
# definitions exist: the library embeds none, AppRun narrows XDG to the AppDir,
# and a runtime guard fails closed if the visible set is not the whitelist.
#
# Usage: bash packaging/runtime/build.sh [--no-centos-probe]
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
# Every step below uses `packaging/runtime/...` or `scripts/...` relative to the
# tree being packaged; pin the working directory to that tree so invoking this
# script from another checkout cannot run the wrong build-appimage.sh (caught in
# T31 phase 2: a relative call hashed the caller's appimagetool cache).
cd "$ROOT"
EVID=${VEDIT_PACK_EVIDENCE:-"$ROOT/evidence/slice3-pack"}
OUT="$ROOT/packaging/runtime/out"
IMAGE=${VEDIT_QT6_BUILD_IMAGE:-vedit-qt6-build:local}
APP="$ROOT/target/qt6-build/src/app/vedit-app"
PROBE="$ROOT/target/qt6-build/cmake/smoke/vedit-ksh-env-probe"
KSH_F2="$ROOT/target/ksh-selfbuild/F2/install"
CENTOS_PROBE=1
[ "${1:-}" = "--no-centos-probe" ] && CENTOS_PROBE=0
CENTOS_IMAGE="docker.m.daocloud.io/library/centos@sha256:be65f488b7764ad3638f236b7b515b3678369a5124c47b8d32916d6487418ea4"

mkdir -p "$EVID" "$ROOT/packaging/appimage/.cache"
# The AppImage tool is pinned by URL + sha256; fetch it once into the cache.
# shellcheck source=/dev/null
set -a; . "$ROOT/scripts/versions.env"; set +a
VENDORED="$ROOT/packaging/appimage/vendor/appimagetool-x86_64"
TOOL="$ROOT/packaging/appimage/.cache/appimagetool.AppImage"
if [ -x "$VENDORED" ]; then TOOL="$VENDORED"; fi
if [ ! -x "$TOOL" ]; then
  echo "fetching pinned appimagetool -> $TOOL"
  curl -fsSL "${APPIMAGETOOL_URL:?APPIMAGETOOL_URL missing}" -o "$TOOL"
  chmod +x "$TOOL"
  got=$(sha256sum "$TOOL" | cut -d' ' -f1)
  [ "$got" = "${APPIMAGETOOL_SHA256:?APPIMAGETOOL_SHA256 missing}" ] || {
    echo "FAIL: appimagetool sha256 mismatch: want=$APPIMAGETOOL_SHA256 got=$got" >&2; exit 1; }
fi
fail=0
step() { printf '\n===== %s =====\n' "$*"; }

echo "VEdit M0 private runtime — F2 build ($(date -u +%Y-%m-%dT%H:%M:%SZ))"

step "preflight"
{
  echo "image: $IMAGE"; docker image inspect "$IMAGE" --format 'image_id={{.Id}} size={{.Size}}' 2>/dev/null || { echo "FAIL: image missing"; exit 1; }
  echo "vd-app: $APP"; [ -x "$APP" ] && echo "  sha256=$(sha256sum "$APP" | cut -d' ' -f1)" || echo "FAIL: vd-app missing (run bash scripts/build-qt.sh)"
  echo "probe: $PROBE"; [ -x "$PROBE" ] && echo "  sha256=$(sha256sum "$PROBE" | cut -d' ' -f1)" || echo "FAIL: probe missing"
  echo "f2 install: $KSH_F2"; [ -d "$KSH_F2" ] && sha256sum "$KSH_F2"/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0 2>/dev/null || echo "FAIL: F2 tree missing"
  echo "whitelist: $(ls "$ROOT/resources/syntax"/*.xml 2>/dev/null | wc -l) xml in resources/syntax/"
  echo "toolchain: verible=$ROOT/.mpd/recon/verible/bin slang=$ROOT/build/slang/out/slang"
  for t in "$ROOT/.mpd/recon/verible/bin/verible-verilog-ls" "$ROOT/build/slang/out/slang"; do
    [ -x "$t" ] && echo "  $(sha256sum "$t" | cut -d' ' -f1)  $t" || echo "FAIL: toolchain artifact missing: $t"
  done
  echo "appimagetool: vendored=$ROOT/packaging/appimage/vendor/appimagetool-x86_64 cache=$ROOT/packaging/appimage/.cache/appimagetool.AppImage"
  [ -x "$TOOL" ] || echo "FAIL: appimagetool missing (vendor and cache)"
  # `ldconfig -p` prints the whole cache (tens of KiB), so `grep -m1` would exit at
  # the first match and SIGPIPE it under this script's `set -uo pipefail`: the
  # `|| echo` would then claim libfuse is missing while it is installed. A plain
  # grep reads everything; `sed -n '1p'` keeps the one-line output.
  (ldconfig -p 2>/dev/null | grep libfuse | sed -n '1p') || echo "  libfuse not visible in ldconfig"
} 2>&1 | tee "$EVID/00-preflight.log"
grep -q 'FAIL' "$EVID/00-preflight.log" && fail=1
[ "$fail" -eq 0 ] || { echo "preflight failed"; exit 1; }

step "1/7 assemble AppDir (F2: self-built library + whitelist)"
docker run --rm -e HOME=/tmp -v "$ROOT:/vedit-build" -w /vedit-build "$IMAGE" \
  bash packaging/runtime/assemble-appdir.sh --require-toolchain > "$EVID/10-assemble.log" 2>&1
rc=$?; echo "assemble exit=$rc" | tee -a "$EVID/10-assemble.log"; tail -5 "$EVID/10-assemble.log"
[ "$rc" -eq 0 ] || { echo "assemble FAILED"; exit 1; }

step "1b/7 delivery invariants (T31: vd-app, Qt plugins, static FFI, toolchain)"
{
  echo "== AppDir delivery invariants =="
  for f in bin/vd-app plugins/platforms/libqxcb.so plugins/platforms/libqoffscreen.so \
           plugins/kf6/parts/katepart.so bin/verible-verilog-ls bin/slang \
           bin/git bin/svn usr/lib/vcs/libexec/git usr/lib/vcs/libexec/svn \
           usr/share/vedit/vcs-toolchain.txt \
           usr/share/org.kde.syntax-highlighting/syntax/vedit-verilog.xml; do
    if [ -e "$OUT/AppDir/$f" ]; then echo "OK   $f"; else echo "MISS $f"; fail=1; fi
  done
  echo "== bundled VCS toolchain =="
  "$OUT/AppDir/bin/git" --version || fail=1
  "$OUT/AppDir/bin/svn" --version --quiet || fail=1
  [ -z "$(find "$OUT/AppDir" -name 'svnadmin' -print -quit)" ] && echo "svnadmin=absent" || { echo "FAIL: svnadmin in the AppDir"; fail=1; }
  echo "== FFI linkage (static into vd-app) =="
  readelf -d "$OUT/AppDir/bin/vd-app" | grep -E 'NEEDED' | sed 's/^/  /'
  echo "  libvd_ffi.so NEEDED entries: $(readelf -d "$OUT/AppDir/bin/vd-app" | grep -c 'libvd_ffi' || true)"
  echo "  vd_* symbols in vd-app: $(nm "$OUT/AppDir/bin/vd-app" 2>/dev/null | grep -c ' T vd_' || true)"
  echo "== toolchain sha256 =="
  sha256sum "$OUT/AppDir/bin/verible-verilog-ls" "$OUT/AppDir/bin/slang" | sed 's/^/  /'
  echo "== AppRun VEDIT_SYNTAX_DIR =="
  grep -n 'VEDIT_SYNTAX_DIR' "$OUT/AppDir/AppRun" || true
} > "$EVID/15-delivery-invariants.log" 2>&1
grep -E 'MISS|NEEDED|vd_\*|sha256|VEDIT_SYNTAX_DIR' "$EVID/15-delivery-invariants.log" | head -12
grep -q '^MISS ' "$EVID/15-delivery-invariants.log" && fail=1

step "2/7 licence / not-shipped gate (delivery mode: must be all green)"
bash packaging/runtime/verify-not-shipped.sh "$OUT/AppDir" > "$EVID/20-not-shipped.log" 2>&1
rc=$?; echo "not-shipped exit=$rc" | tee -a "$EVID/20-not-shipped.log"
grep -E 'CHECK (PASS|FAIL)|summary' "$EVID/20-not-shipped.log" | tail -12
[ "$rc" -eq 0 ] || fail=1

step "3/7 runtime self-check (AppDir, clean env)"
bash packaging/runtime/selfcheck.sh --appdir "$OUT/AppDir" > "$EVID/30-selfcheck-appdir.log" 2>&1
rc=$?; echo "selfcheck(appdir) exit=$rc" | tee -a "$EVID/30-selfcheck-appdir.log"
grep -E 'KSH-ENV defs=|KSH-GUARD|APP-SHELL-SELFTEST|TOOLCHAIN|CHECK |SELFCHECK' "$EVID/30-selfcheck-appdir.log"
[ "$rc" -eq 0 ] || fail=1

step "4/7 F2 isolation with a simulated KDE host + user definitions"
bash packaging/runtime/verify-f2-isolation.sh --appdir "$OUT/AppDir" > "$EVID/40-f2-isolation-appdir.log" 2>&1
rc=$?; echo "isolation(appdir) exit=$rc" | tee -a "$EVID/40-f2-isolation-appdir.log"
grep -E 'CHECK |F2-ISOLATION|summary' "$EVID/40-f2-isolation-appdir.log"
[ "$rc" -eq 0 ] || fail=1

step "5/7 build the AppImage"
bash packaging/runtime/build-appimage.sh "$OUT/AppDir" > "$EVID/50-appimage.log" 2>&1
rc=$?; echo "appimagetool exit=$rc" | tee -a "$EVID/50-appimage.log"
grep -E 'APPIMAGE|sha256|SOURCE_DATE' "$EVID/50-appimage.log"
[ "$rc" -eq 0 ] || { echo "AppImage build failed"; exit 1; }
APPIMAGE=$(grep -m1 '^APPIMAGE=' "$EVID/50-appimage.log" | cut -d= -f2-)
if [ -n "$APPIMAGE" ] && [ -f "$APPIMAGE" ]; then
  ( cd "$(dirname "$APPIMAGE")" && sha256sum "$(basename "$APPIMAGE")" > SHA256SUMS )
  cp "$(dirname "$APPIMAGE")/SHA256SUMS" "$EVID/SHA256SUMS"
  echo "SHA256SUMS:"; cat "$EVID/SHA256SUMS"
fi

step "6/7 self-check + F2 isolation on the AppImage"
bash packaging/runtime/selfcheck.sh --appimage "$APPIMAGE" > "$EVID/60-selfcheck-appimage.log" 2>&1
rc1=$?; echo "selfcheck(appimage) exit=$rc1" | tee -a "$EVID/60-selfcheck-appimage.log"
bash packaging/runtime/verify-f2-isolation.sh --appdir "$OUT/AppDir" --appimage "$APPIMAGE" \
  > "$EVID/61-f2-isolation-appimage.log" 2>&1
rc2=$?; echo "isolation(appimage) exit=$rc2" | tee -a "$EVID/61-f2-isolation-appimage.log"
grep -E 'CHECK |F2-ISOLATION|SELFCHECK' "$EVID/60-selfcheck-appimage.log" "$EVID/61-f2-isolation-appimage.log"
[ "$rc1" -eq 0 ] && [ "$rc2" -eq 0 ] || fail=1

if [ "$CENTOS_PROBE" -eq 1 ]; then
  step "7/7 CentOS 7.9 mechanism probe (T12 owns the formal matrix)"
  rm -rf "$OUT/centos7-appdir" "$OUT/squashfs-root"
  ( cd "$OUT" && APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGE" --appimage-extract >/dev/null 2>&1 ) || true
  [ -d "$OUT/squashfs-root" ] && mv "$OUT/squashfs-root" "$OUT/centos7-appdir"
  {
    echo "host: $(docker run --rm "$CENTOS_IMAGE" cat /etc/centos-release 2>&1 | head -1)"
    echo "glibc: $(docker run --rm "$CENTOS_IMAGE" ldd --version 2>&1 | head -1)"
    docker run --rm -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -v "$OUT/centos7-appdir:/app:ro" \
      "$CENTOS_IMAGE" /app/AppRun --selfcheck 2>&1
    echo "centos7_smoke_exit=$?"
    docker run --rm -v "$OUT/centos7-appdir:/app:ro" "$CENTOS_IMAGE" \
      bash -lc '/app/bin/git --version; /app/bin/svn --version --quiet; test ! -e /app/svnadmin && echo svnadmin=absent' 2>&1
    echo "centos7_vcs_exit=$?"
  } > "$EVID/70-centos7-probe.log" 2>&1
  grep -E 'APP-SHELL-SELFTEST|TOOLCHAIN|KSH-GUARD|centos7_smoke_exit' "$EVID/70-centos7-probe.log"
  grep -q 'APP-SHELL-SELFTEST ok=1' "$EVID/70-centos7-probe.log" && grep -q 'centos7_smoke_exit=0' "$EVID/70-centos7-probe.log" \
    && echo "CHECK PASS  CentOS 7 mechanism probe" || { echo "CHECK FAIL  CentOS 7 mechanism probe"; fail=1; }
  grep -q "git version ${VCS_GIT_VERSION}" "$EVID/70-centos7-probe.log" \
    && grep -qx "${VCS_SVN_VERSION}" "$EVID/70-centos7-probe.log" \
    && grep -q 'svnadmin=absent' "$EVID/70-centos7-probe.log" \
    && echo "CHECK PASS  bundled VCS toolchain in the clean container" \
    || { echo "CHECK FAIL  bundled VCS toolchain in the clean container"; fail=1; }
fi

( cd "$OUT/AppDir" && find . -type f | sort ) > "$EVID/appdir-files.txt"
cp "$OUT/AppDir/runtime-manifest.txt" "$EVID/runtime-manifest.txt" 2>/dev/null || true
cp "$OUT/AppDir/RUNTIME-NOTICES.txt" "$EVID/RUNTIME-NOTICES.txt" 2>/dev/null || true
python3 "$ROOT/evidence/r2-ksh-embedded/scan-embedded-defs.py" "$OUT/AppDir/lib/libKF6SyntaxHighlighting.so.6" > "$EVID/71-embedded-defs.log" 2>&1 || true

step "summary"
{
  echo "form=F2"
  echo "appdir=$OUT/AppDir"
  echo "appdir_files=$(wc -l < "$EVID/appdir-files.txt")"
  echo "whitelist=$(cat "$OUT/AppDir/usr/share/vedit/ksh-whitelist.txt")"
  echo "kshlib_sha256=$(sha256sum "$OUT/AppDir/lib/libKF6SyntaxHighlighting.so.6" | cut -d' ' -f1)"
  echo "appimage=$APPIMAGE"
  echo "appimage_bytes=$(stat -c%s "$APPIMAGE" 2>/dev/null || echo 0)"
  echo "appimage_sha256=$(sha256sum "$APPIMAGE" 2>/dev/null | cut -d' ' -f1 || echo unknown)"
  echo "steps_failed=$fail"
} | tee "$EVID/99-summary.txt"
[ "$fail" -eq 0 ] && echo "BUILD: F2 runtime OK" || { echo "BUILD: FAIL"; exit 1; }
