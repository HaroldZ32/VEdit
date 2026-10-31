#!/usr/bin/env bash
# Turn the assembled AppDir into a single-file AppImage (T11).
#
# The tool is the pinned appimagetool from scripts/versions.env (cached under
# packaging/appimage/.cache); its sha256 is checked before it runs. FUSE is not
# required to BUILD — `APPIMAGE_EXTRACT_AND_RUN=1` lets the tool run extracted.
#
# A probe AppDir (stamped PROBE-NOT-FOR-DELIVERY.txt) can only be built with
# --probe, and the output keeps the `.probe` suffix so it can never be mistaken
# for a deliverable. A delivery build refuses the stamp outright.
#
# Usage: build-appimage.sh [--probe] [--out <dir>] [<AppDir>]
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
APPDIR="$ROOT/packaging/runtime/out/AppDir"
OUTDIR="$ROOT/packaging/runtime/out"
PROBE=0

while [ $# -gt 0 ]; do
  case "$1" in
    --probe) PROBE=1; shift ;;
    --out) OUTDIR=${2:?}; shift 2 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) APPDIR=$1; shift ;;
  esac
done
[ -d "$APPDIR" ] || { echo "ERROR: no AppDir: $APPDIR" >&2; exit 1; }

if [ -f "$APPDIR/PROBE-NOT-FOR-DELIVERY.txt" ] && [ "$PROBE" -ne 1 ]; then
  echo "ERROR: refusing to build a DELIVERY AppImage from a probe AppDir." >&2
  echo "       The distro KSyntaxHighlighting library embeds strict-GPL definitions (T56)." >&2
  echo "       Replace it with the T60 (-DQRC_SYNTAX=OFF) artifact, then re-run." >&2
  exit 1
fi

TOOL="$ROOT/packaging/appimage/.cache/appimagetool.AppImage"
[ -x "$TOOL" ] || { echo "ERROR: appimagetool not cached at $TOOL" >&2; exit 1; }
# shellcheck source=/dev/null
set -a; . "$ROOT/scripts/versions.env"; set +a
want=${APPIMAGETOOL_SHA256:?APPIMAGETOOL_SHA256 missing from scripts/versions.env}
got=$(sha256sum "$TOOL" | cut -d' ' -f1)
[ "$got" = "$want" ] || { echo "ERROR: appimagetool sha256 mismatch: want=$want got=$got" >&2; exit 1; }
echo "appimagetool sha256 OK ($got)"

VERSION=$(awk -F'"' '/^version = / {print $2; exit}' "$ROOT/Cargo.toml" 2>/dev/null || echo 0.0.0)
SUFFIX=""
[ "$PROBE" -eq 1 ] && SUFFIX=".probe"
OUTFILE="$OUTDIR/VEdit-${VERSION}-x86_64${SUFFIX}.AppImage"
mkdir -p "$OUTDIR"

# Reproducibility: mksquashfs embeds timestamps, so two builds of the same tree
# otherwise differ. With SOURCE_DATE_EPOCH pinned, two consecutive runs produce
# bit-identical AppImages (verified: sha 08dc0e8c… twice). Default = the commit
# time of the tree being packaged.
: "${SOURCE_DATE_EPOCH:=$(git -C "$ROOT" log -1 --format=%ct 2>/dev/null || echo 0)}"
export SOURCE_DATE_EPOCH
echo "SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH"

echo "\$ appimagetool --no-appstream $APPDIR $OUTFILE"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" \
  "$TOOL" --no-appstream "$APPDIR" "$OUTFILE"
rc=$?
echo "EXIT=$rc"
[ "$rc" -eq 0 ] || exit "$rc"
[ -f "$OUTFILE" ] || { echo "ERROR: appimagetool reported success but $OUTFILE is missing" >&2; exit 1; }

echo "APPIMAGE=$OUTFILE"
echo "APPIMAGE_BYTES=$(stat -c%s "$OUTFILE")"
echo "APPIMAGE_SHA256=$(sha256sum "$OUTFILE" | cut -d' ' -f1)"
