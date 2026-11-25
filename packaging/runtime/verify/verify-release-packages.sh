#!/usr/bin/env bash
# Check the three staged release packages, retaining the AppImage extract for the runtime gates.
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "Usage: $0 <AppDir> <package directory> <release version> <new AppImage extract path>" >&2
  exit 2
fi
APPDIR=$(cd "$1" && pwd -P)
OUT=$(cd "$2" && pwd -P)
VERSION=$3
EXTRACTED=$(cd "$(dirname -- "$4")" && pwd -P)/$(basename -- "$4")
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$ ]]; then
  echo "ERROR: invalid release version: $VERSION" >&2
  exit 2
fi
if [ -e "$EXTRACTED" ] || [ -L "$EXTRACTED" ]; then
  echo "ERROR: AppImage extract path already exists: $EXTRACTED" >&2
  exit 1
fi
IMAGE="VEdit-$VERSION-x86_64.AppImage"
RUN="VEdit-$VERSION-x86_64.run"
STEM="vedit-$VERSION-linux-x86_64"
TARBALL="$STEM.tar.gz"
(
  cd "$OUT"
  shopt -s dotglob nullglob
  files=(*)
  test "${#files[@]}" -eq 4
  for asset in "$IMAGE" "$RUN" "$TARBALL" SHA256SUMS; do
    test -f "$asset" && test ! -L "$asset" && test -s "$asset" || exit 1
  done
  test -x "$IMAGE" && test -x "$RUN"
  printf '%s\n' "$IMAGE" "$RUN" "$TARBALL" | cmp - <(cut -c67- SHA256SUMS)
  sha256sum --check --strict SHA256SUMS
)

WORK=$(mktemp -d "$(dirname -- "$EXTRACTED")/.verify-release.XXXXXX")
trap 'rm -rf -- "$WORK"' EXIT
mkdir "$WORK/home" "$WORK/tmp" "$WORK/tar"

payload_modes() {
  local root=$1
  shift
  ( cd "$root" && find . -mindepth 1 "$@" -printf '%y %m %P\0' | LC_ALL=C sort -z )
}

payload_matches() {
  local label=$1 extracted=$2
  shift 2
  diff -qr --no-dereference -- "$APPDIR" "$extracted"
  payload_modes "$APPDIR" "$@" > "$WORK/appdir-modes"
  payload_modes "$extracted" "$@" > "$WORK/extracted-modes"
  cmp "$WORK/appdir-modes" "$WORK/extracted-modes"
  echo "CHECK PASS  $label matches AppDir contents and links"
}

launch() {
  local host_path=$1
  shift
  timeout 300 env -i PATH="$host_path" HOME="$WORK/home" TMPDIR="$WORK/tmp" \
    LANG=C.UTF-8 QT_QPA_PLATFORM=offscreen VEDIT_NO_ENV_AUTOCAPTURE=1 "$@"
}

version_matches() {
  local actual
  actual=$(launch "$@" --version)
  if [ "$actual" != "vedit-app $VERSION" ]; then
    printf 'ERROR: expected vedit-app %s, got %s\n' "$VERSION" "$actual" >&2
    return 1
  fi
  printf '%s\n' "$actual"
}

startup() {
  local label=$1 host_path=$2
  shift 2
  launch "$host_path" /usr/bin/env VEDIT_STARTUP_TRACE=1 "$@" --no-session-restore --hold 500 > "$WORK/startup.log" 2>&1 || {
    cat "$WORK/startup.log" >&2
    return 1
  }
  grep -F 'launch-ready' "$WORK/startup.log"
  grep -F 'VCS-TOOLCHAIN ' "$WORK/startup.log"
  echo "CHECK PASS  $label starts VEdit without FUSE"
}

tar --no-same-owner --same-permissions -xzf "$OUT/$TARBALL" -C "$WORK/tar"
payload_matches 'tar.gz' "$WORK/tar/$STEM"
version_matches /usr/bin:/bin "$WORK/tar/$STEM/AppRun"
startup 'tar.gz' /usr/bin:/bin "$WORK/tar/$STEM/AppRun"
rm -rf -- "$WORK/tar"

"$OUT/$RUN" --extract "$WORK/run tree"
payload_matches '.run' "$WORK/run tree/$STEM"
rm -rf -- "$WORK/run tree"
version_matches '' "$OUT/$RUN"
startup '.run' '' "$OUT/$RUN"
grep -Fq 'VCS-TOOLCHAIN git=<missing> version=<missing> svn=<missing> version=<missing> source=missing ' "$WORK/startup.log"
status=0
launch '' "$OUT/$RUN" --selfcheck-ffi "$WORK/missing fixture" > "$WORK/run-failure.log" 2>&1 || status=$?
test "$status" -eq 2
grep -Fx "FFI-ABI fixture=$WORK/missing fixture" "$WORK/run-failure.log"
grep -Fx 'FFI-ABI-SELFTEST ok=0 reason=wrong-fixture' "$WORK/run-failure.log"
test -z "$(find "$WORK/tmp" -maxdepth 1 -name 'vedit-run.*' -print -quit)"
echo 'CHECK PASS  .run forwards arguments, preserves exit 2 and cleans up temporary extracts'

( cd "$WORK" && "$OUT/$IMAGE" --appimage-extract >/dev/null )
# The AppImage runtime creates extracted directories mode 0700.
payload_matches 'AppImage' "$WORK/squashfs-root" ! -type d
version_matches /usr/bin:/bin "$WORK/squashfs-root/AppRun"
startup 'AppImage' /usr/bin:/bin "$WORK/squashfs-root/AppRun"
mv -T -- "$WORK/squashfs-root" "$EXTRACTED"
