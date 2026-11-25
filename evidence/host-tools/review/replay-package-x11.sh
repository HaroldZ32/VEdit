#!/usr/bin/env bash
set -euo pipefail

root=$(pwd -P)
package=$(realpath "${1:-packaging/runtime/out/AppDir}")
evidence="$root/evidence/host-tools/review"
shot="${2:-package-terminal.png}"
[[ $shot =~ ^[a-zA-Z0-9_-]+\.png$ ]]
helper=$(realpath "$root/../../target/desktop-ux-panes-qt/native-x11")
xtst=$(realpath "$root/../../target/desktop-ux-standalone-repair/gui/native-libs/libXtst.so.6")
debs=$(realpath "$root/../../target/vcs-debs-cache")
scratch=$(mktemp -d "$root/target/review-package-ui.XXXXXX")
printf 'Review GUI scratch=%s\n' "$scratch"
sha256sum "$package/AppRun" "$package/bin/vd-app"

docker run --rm -i --network none \
  -v "$package:/package:ro" -v "$scratch:/review:rw" \
  -v "$helper:/native-x11:ro" -v "$xtst:/native-libs/libXtst.so.6:ro" \
  -v "$debs:/vcs-debs:ro" -v "$root/target/host-tools-test-debs:/shell-debs:ro" \
  -v "$root/crates/vd-ffi/tests/fixtures/project:/fixture:ro" \
  --entrypoint /bin/bash vedit-qt6-build:local -s <<'BASH'
set -euo pipefail
dpkg -i /vcs-debs/libapr1t64_*.deb /vcs-debs/libaprutil1t64_*.deb /vcs-debs/libutf8proc3_*.deb /vcs-debs/libserf-1-1_*.deb /vcs-debs/libsvn1_*.deb /vcs-debs/subversion_*.deb /shell-debs/zsh*.deb
export HOME=/review/home XDG_RUNTIME_DIR=/review/xdg QT_QPA_PLATFORM=xcb SHELL=/bin/bash
export VEDIT_NO_ENV_AUTOCAPTURE=1 VEDIT_STARTUP_TRACE=1 DISPLAY=:83
mkdir -p "$HOME" "$XDG_RUNTIME_DIR" /review/profile /review/bin /review/project
chmod 700 "$XDG_RUNTIME_DIR"
cp -a /fixture/. /review/project/
git -C /review/project init -q
git -C /review/project add .
git -C /review/project -c user.name=Fixture -c user.email=fixture@example.invalid commit -qm baseline
printf '\n// modified\n' >> /review/project/rtl/rtl_a.sv
printf '"tools.host_shell" = "/review/bin/zsh"\n' > /review/profile/settings.toml
printf '%s\n' '{"version":1,"global":[{"name":"PATH","value":"/package/bin:/review/bin:/usr/bin:/bin"},{"name":"VEDIT_REVIEW_IMPORT","value":"synthetic"},{"name":"LD_LIBRARY_PATH","value":"/package/lib"},{"name":"LOCPATH","value":"/package/usr/lib/locale"}],"projects":[]}' > /review/profile/environment.json

cat > /review/bin/zsh <<'SH'
#!/bin/sh
set -eu
test -z "${LD_LIBRARY_PATH+x}${LOCPATH+x}"
test "$PATH" = '/review/bin:/usr/bin:/bin'
test "$VEDIT_REVIEW_IMPORT" = synthetic
if [ -t 0 ]; then
    echo selected-pty >> /review/launches
    printf 'Host shell and imported environment are ready.\n'
else
    echo selected-vcs >> /review/launches
fi
export VEDIT_REVIEW_SHELL_SELECTED=1
exec /usr/bin/zsh "$@"
SH
for tool in git svn; do
  printf '%s\n' '#!/bin/sh' 'set -eu' 'test "$VEDIT_REVIEW_SHELL_SELECTED" = 1' \
    'test -z "${LD_LIBRARY_PATH+x}${LOCPATH+x}"' \
    'test "$PATH" = "/review/bin:/usr/bin:/bin"' 'test "$VEDIT_REVIEW_IMPORT" = synthetic' \
    "echo $tool >> /review/host-tools" \
    "for arg do if [ \"\$arg\" = status ]; then echo $tool-status >> /review/host-tools; fi; done" \
    "exec /usr/bin/$tool \"\$@\"" > "/review/bin/$tool"
done
chmod +x /review/bin/*

x11_width=1440
x11_height=960
Xvfb "$DISPLAY" -screen 0 "${x11_width}x${x11_height}x24" -ac -nolisten tcp > /review/xvfb.log 2>&1 &
for attempt in $(seq 1 40); do test ! -S /tmp/.X11-unix/X83 || break; sleep .1; done
input() { LD_LIBRARY_PATH=/native-libs /native-x11 "$@"; }

set -x
xvfb-run -a timeout 55s /package/AppRun --selfcheck-ffi /review/project
/package/AppRun --state-dir /review/profile --no-session-restore --project /review/project /review/project/rtl/rtl_a.sv --hold 12000 > /review/app.log 2>&1 &
app_pid=$!
for attempt in $(seq 1 90); do
    if awk '/launch-ready/{seen=1} END{exit !seen}' /review/app.log; then break; fi
    kill -0 "$app_pid"
    sleep .2
done
awk '/VCS-TOOLCHAIN|window-shown|launch-ready/' /review/app.log
awk '/VCS-TOOLCHAIN.*git=\/review\/bin\/git.*svn=\/review\/bin\/svn/ {picked=1} /launch-ready/{ready=1} END{exit !(picked && ready)}' /review/app.log
window=$(input windows | awk '/rtl_a.sv/ && !found++ {print $1}')
input resize "$window" "$x11_width" "$x11_height"
input key Control_L+Alt_L+j
for attempt in $(seq 1 30); do
    if awk '/^selected-pty$/{seen=1} END{exit !seen}' /review/launches; then break; fi
    sleep .1
done
awk '/^selected-pty$/{pty=1} /^selected-vcs$/{vcs=1} END{exit !(pty && vcs)}' /review/launches
awk '/^git$/{git=1} /^svn$/{svn=1} END{exit !(git && svn)}' /review/host-tools
sleep .4
input image "$x11_width" "$x11_height" > /review/package-terminal.ppm
input key Control_L+Alt_L+c
for attempt in $(seq 1 30); do
    if awk '/^git-status$/{seen=1} END{exit !seen}' /review/host-tools; then break; fi
    sleep .1
done
awk '/^git-status$/{seen=1} END{exit !seen}' /review/host-tools
sleep .4
input image "$x11_width" "$x11_height" > /review/package-vcs.ppm
wait "$app_pid"
printf 'Selected shell, imported environment and sanitized loader variables reached the packaged PTY and VCS tools.\n'

APPDIR=/package PATH= /package/AppRun --state-dir /review/profile-without-tools --no-session-restore --project /review/project /review/project/rtl/rtl_a.sv --hold 6000 > /review/missing.log 2>&1 &
missing_pid=$!
for attempt in $(seq 1 25); do
    if awk '/launch-ready/{seen=1} END{exit !seen}' /review/missing.log; then break; fi
    kill -0 "$missing_pid"
    sleep .2
done
awk '/VCS-TOOLCHAIN|window-shown|launch-ready/' /review/missing.log
awk '/VCS-TOOLCHAIN.*git=<missing>.*svn=<missing>/ {missing=1} /launch-ready/{ready=1} END{exit !(missing && ready)}' /review/missing.log
window=$(input windows | awk '/rtl_a.sv/ && !found++ {print $1}')
input resize "$window" "$x11_width" "$x11_height"
input key Control_L+Alt_L+c
sleep .8
input image "$x11_width" "$x11_height" > /review/package-missing.ppm
wait "$missing_pid"
BASH

ffmpeg -hide_banner -loglevel error -y -f image2pipe -c:v ppm \
  -i "$scratch/package-terminal.ppm" -frames:v 1 -threads 1 "$evidence/$shot"
ffmpeg -hide_banner -loglevel error -y -f image2pipe -c:v ppm \
  -i "$scratch/package-vcs.ppm" -frames:v 1 -threads 1 "$evidence/${shot%.png}-vcs.png"
ffmpeg -hide_banner -loglevel error -y -f image2pipe -c:v ppm \
  -i "$scratch/package-missing.ppm" -frames:v 1 -threads 1 "$evidence/${shot%.png}-missing.png"
printf 'AppDir packaged GUI verification exit=0\n'
