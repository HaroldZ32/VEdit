#!/usr/bin/env bash
# Write the three release packages from a validated AppDir into an empty staging directory.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <AppDir> <empty staging directory> <release version>" >&2
  exit 2
fi
APPDIR=$(cd "$1" && pwd -P)
OUT=$(cd "$2" && pwd -P)
VERSION=$3
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$ ]]; then
  echo "ERROR: invalid release version: $VERSION" >&2
  exit 2
fi
if [ ! -x "$APPDIR/AppRun" ] || [ ! -x "$APPDIR/bin/vd-app" ] || [ -e "$APPDIR/PROBE-NOT-FOR-DELIVERY.txt" ]; then
  echo "ERROR: expected a delivery AppDir at $APPDIR" >&2
  exit 1
fi
case "$OUT/" in
  "$APPDIR/"*) echo "ERROR: staging directory must be outside the AppDir" >&2; exit 1 ;;
esac
if [ -n "$(find "$OUT" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
  echo "ERROR: package staging directory is not empty: $OUT" >&2
  exit 1
fi

IMAGE="VEdit-$VERSION-x86_64.AppImage"
RUN="VEdit-$VERSION-x86_64.run"
STEM="vedit-$VERSION-linux-x86_64"
TARBALL="$STEM.tar.gz"
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$HERE" log -1 --format=%ct)}
export SOURCE_DATE_EPOCH
bash "$HERE/build-appimage.sh" --out "$OUT" "$APPDIR"
test -s "$OUT/$IMAGE"

# Prefix archive member names and hard-link targets. Keep symlink targets verbatim.
LC_ALL=C tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
  --transform="flags=rh;s|^\.|$STEM|" -C "$APPDIR" -cf - . | gzip -n -1 > "$OUT/$TARBALL"

{
  printf '%s\n' '#!/bin/bash' 'set -eo pipefail' "top_dir='$STEM'"
  cat <<'RUN_HEADER'
archive=$(/usr/bin/readlink -f -- "$0")
payload_line=$(/usr/bin/awk '/^__VEDIT_PAYLOAD__$/{print NR+1; exit}' "$archive")
if [ -z "$payload_line" ]; then
  echo 'VEdit: embedded archive is missing' >&2
  exit 1
fi

extract() {
  /usr/bin/tail -n "+$payload_line" -- "$archive" | /bin/gzip -dc | /bin/tar --no-same-owner --same-permissions -xf - -C "$1"
}

case "${1:-}" in
  --extract)
    if [ "$#" -ne 2 ] || [ -z "$2" ]; then
      echo "Usage: $0 --extract NEW_DIRECTORY" >&2
      exit 2
    fi
    /bin/mkdir -- "$2"
    if extract "$2"; then
      exit 0
    else
      status=$?
      /bin/rm -rf -- "$2"
      exit "$status"
    fi
    ;;
  --) shift ;;
esac

tmp=$(/bin/mktemp -d -- "${TMPDIR:-/tmp}/vedit-run.XXXXXX")
child=
trap '/bin/rm -rf -- "$tmp"' EXIT
stop() {
  if [ -n "$child" ]; then
    kill -TERM "$child" 2>/dev/null || true
    wait "$child" || true
  fi
  exit "$1"
}
trap 'stop 129' HUP
trap 'stop 130' INT
trap 'stop 143' TERM
extract "$tmp"

# Settings restart uses APPIMAGE as the original launcher. The successor gets
# its own temporary tree before the old process exits and this tree is removed.
APPIMAGE="$archive" "$tmp/$top_dir/AppRun" "$@" <&0 &
child=$!
wait "$child"
exit "$?"
__VEDIT_PAYLOAD__
RUN_HEADER
  cat "$OUT/$TARBALL"
} > "$OUT/$RUN"
chmod 0755 "$OUT/$RUN"
(
  cd "$OUT"
  sha256sum -- "$IMAGE" "$RUN" "$TARBALL" > SHA256SUMS
)
stat -c 'PACKAGE %n %s bytes' "$OUT/$IMAGE" "$OUT/$RUN" "$OUT/$TARBALL"
