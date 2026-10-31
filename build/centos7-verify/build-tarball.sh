#!/usr/bin/env bash
# Build the self-contained CentOS 7 on-machine verification tarball (T83).
#
#   bash build/centos7-verify/build-tarball.sh
#
# Output: build/centos7-verify/dist/vedit-centos7-verify-<git-sha>.tar.gz
#
# The AppDir comes from packaging/runtime/assemble-appdir.sh (T82: toolchain +
# private runtime, glibc + loader included) and then gets the product binary
# bin/vedit-app added; samples/, verify-centos7.sh and STEPS.md are copied in.
# Everything is assembled inside the pinned Qt6/KF6 image, so the tarball is
# reproducible on any host that has docker + the image.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
IMAGE="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
OUT="$ROOT/build/centos7-verify"
APPDIR="$OUT/AppDir"
STAGE="$OUT/stage"
SHA=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo nogit)
NAME="vedit-centos7-verify-$SHA"

echo "== 1/5: assemble the AppDir (private runtime + bundled toolchain) =="
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build "$IMAGE" bash -lc "
    set -e
    rm -rf build/centos7-verify/AppDir
    packaging/runtime/assemble-appdir.sh --out /vedit-build/build/centos7-verify/AppDir \
        --toolchain-verible /vedit-build/.mpd/recon/verible/bin \
        --toolchain-slang /vedit-build/build/slang/out/slang --require-toolchain
    # the product binary itself (the assembler ships the smoke binary only)
    install -m 0755 target/qt6-build/src/app/vedit-app build/centos7-verify/AppDir/bin/vedit-app
    # optional product-side probes, included when the build produced them
    for probe in \
        target/qt6-build/src/app/workspace-lane-diagnostics/vedit-diagnostics-ffi-selftest; do
        [ -x \"\$probe\" ] && install -m 0755 \"\$probe\" build/centos7-verify/AppDir/bin/ || true
    done
    # every dependency of the added binaries must already be in AppDir/lib
    for bin in build/centos7-verify/AppDir/bin/vedit-app build/centos7-verify/AppDir/bin/vedit-diagnostics-ffi-selftest; do
        [ -x \"\$bin\" ] || continue
        ldd \"\$bin\" 2>/dev/null | awk '/=> \//{print \$3}' | sort -u | while read -r lib; do
            base=\$(basename \"\$lib\")
            if [ ! -e \"build/centos7-verify/AppDir/lib/\$base\" ]; then
                echo \"  + adding missing closure member \$base\"
                cp -L \"\$lib\" build/centos7-verify/AppDir/lib/
            fi
        done
    done
"
echo "   AppDir: $(du -sh "$APPDIR" | cut -f1)"

echo "== 2/5: stage the tarball tree =="
rm -rf "$STAGE"
mkdir -p "$STAGE/$NAME"
cp -a "$APPDIR" "$STAGE/$NAME/AppDir"
cp -a "$OUT/samples" "$STAGE/$NAME/samples"
install -m 0755 "$OUT/verify-centos7.sh" "$STAGE/$NAME/verify-centos7.sh"
install -m 0644 "$OUT/STEPS.md" "$STAGE/$NAME/STEPS.md"

echo "== 3/5: checksums (MANIFEST.sha256, relative paths) =="
(
    cd "$STAGE/$NAME"
    find . -type f ! -name MANIFEST.sha256 -print0 | sort -z | xargs -0 sha256sum > MANIFEST.sha256
    echo "   $(wc -l < MANIFEST.sha256) files"
)

echo "== 4/5: tarball =="
mkdir -p "$OUT/dist"
TARBALL="$OUT/dist/$NAME.tar.gz"
rm -f "$TARBALL"
tar -C "$STAGE" -czf "$TARBALL" "$NAME"
( cd "$OUT/dist" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256" )

echo "== 5/5: done =="
echo "   $TARBALL"
echo "   size: $(du -h "$TARBALL" | cut -f1)"
echo "   sha256: $(cut -d' ' -f1 "$TARBALL.sha256")"
