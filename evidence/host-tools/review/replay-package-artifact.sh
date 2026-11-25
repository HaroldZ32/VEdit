#!/usr/bin/env bash
set -euo pipefail

root=$(pwd -P)
appdir=$(realpath "${1:-packaging/runtime/out/AppDir}")
image=$(realpath "${2:-packaging/runtime/out/VEdit-0.4.0-x86_64.AppImage}")
sha256sum "$appdir/AppRun" "$appdir/bin/vd-app"
(cd "$(dirname "$image")" && sha256sum --check SHA256SUMS)
bash packaging/runtime/verify-not-shipped.sh --expect-clean "$appdir"
for forbidden in bin/git bin/svn usr/lib/vcs usr/bin/git usr/bin/svn bin/bash bin/csh bin/tcsh bin/zsh bin/svnadmin; do
    if [[ -e $appdir/$forbidden || -L $appdir/$forbidden ]]; then
        printf 'Unexpected package entry %s\n' "$forbidden" >&2
        exit 1
    fi
done

scratch=$(mktemp -d "$root/target/review-appdir.XXXXXX")
cleanup() {
    local resolved
    resolved=$(realpath -- "$scratch") || return
    if [[ $resolved == "$scratch" && $resolved == "$root/target/review-appdir."?????? ]]; then
        rm -rf -- "$resolved"
    fi
}
trap cleanup EXIT
cp -al "$appdir" "$scratch/AppDir"
mkdir -p "$scratch/AppDir/usr/lib/vcs/bin"
for legacy in "$scratch/AppDir/bin/git" "$scratch/AppDir/usr/lib/vcs/bin/svn"; do
    printf '#!/bin/sh\necho legacy-executed >> /tmp/host-tools-review-legacy-hit\nexit 77\n' > "$legacy"
    chmod +x "$legacy"
done

docker run --rm -i --network none -v "$scratch/AppDir:/legacy:ro" \
  -v "$image:/delivery/VEdit.AppImage:ro" --entrypoint /bin/bash \
  docker.m.daocloud.io/library/centos:7 -s <<'BASH'
set -euo pipefail
export HOME=$(mktemp -d) XDG_RUNTIME_DIR=$(mktemp -d)
export QT_QPA_PLATFORM=offscreen SHELL=/bin/bash VEDIT_NO_ENV_AUTOCAPTURE=1 VEDIT_STARTUP_TRACE=1
set -x
! command -v git
! command -v svn
VEDIT_GIT=/legacy/bin/git VEDIT_SVN=/legacy/usr/lib/vcs/bin/svn \
    /legacy/AppRun --no-session-restore --state-dir /tmp/review-appdir-state --hold 500 > /tmp/review-startup.log 2>&1
awk '/VCS-TOOLCHAIN|window-shown|launch-ready/' /tmp/review-startup.log
awk '/VCS-TOOLCHAIN.*git=<missing>.*svn=<missing>/ {missing=1} / window-shown/ {window_opened=1} / launch-ready/ {ready=1} END {exit !(missing && window_opened && ready)}' /tmp/review-startup.log
test ! -e /tmp/host-tools-review-legacy-hit
APPIMAGE_EXTRACT_AND_RUN=1 timeout 150s /delivery/VEdit.AppImage --no-session-restore \
    --state-dir /tmp/review-image-state --hold 500 > /tmp/review-image.log 2>&1 || {
      result=$?
      awk 'NR <= 80' /tmp/review-image.log
      exit "$result"
    }
awk '/KSH-GUARD|VCS-TOOLCHAIN|window-shown|launch-ready/' /tmp/review-image.log
awk '/VCS-TOOLCHAIN.*git=<missing>.*svn=<missing>/ {missing=1} / window-shown/ {window_opened=1} / launch-ready/ {ready=1} END {exit !(missing && window_opened && ready)}' /tmp/review-image.log
BASH

docker run --rm -i --network none -v "$appdir:/package:ro" \
  --entrypoint /bin/bash vedit-qt6-build:local -s <<'BASH'
set -euo pipefail
export HOME=$(mktemp -d) XDG_RUNTIME_DIR=$(mktemp -d)
export QT_QPA_PLATFORM=offscreen SHELL=/bin/bash VEDIT_NO_ENV_AUTOCAPTURE=1 VEDIT_STARTUP_TRACE=1
shadow=$(mktemp -d /dev/shm/vedit-qa.XXXXXX)
cp /usr/bin/git "$shadow/git"
set -x
stat -c '%a %n' "$shadow/git"
test ! -x "$shadow/git"
PATH="$shadow:/usr/bin:/bin" /package/AppRun --no-session-restore \
    --state-dir /tmp/review-noexec-state --hold 500 > /tmp/review-noexec.log 2>&1
awk '/VCS-TOOLCHAIN|window-shown|launch-ready/' /tmp/review-noexec.log
awk '/VCS-TOOLCHAIN.*git=\/usr\/bin\/git version=[0-9]/ {host=1} / launch-ready/ {ready=1} END {exit !(host && ready)}' /tmp/review-noexec.log
BASH

printf 'Final package payload, AppDir, AppImage and noexec checks passed.\n'
