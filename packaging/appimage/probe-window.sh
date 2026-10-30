#!/usr/bin/env bash
# Diagnostic helper: run a GTK program under Xvfb, capture its toplevel window(s)
# with xwd and report the pixel statistics. Used while validating the payload.
set -u
APP=${1:?usage: probe-window.sh <binary> [args...]}; shift
TAG=${TAG:-probe}
printf '0123456789abcdef0123456789abcdef\n' > /etc/machine-id
Xvfb :99 -screen 0 1280x900x24 >/dev/null 2>&1 &
sleep 2
export DISPLAY=:99 NO_AT_BRIDGE=1 LIBGL_ALWAYS_SOFTWARE=1
"$APP" "$@" >"/evid/$TAG.log" 2>&1 &
APP_PID=$!
sleep 6
xwininfo -root -tree >"/evid/$TAG-windows.txt" 2>&1
for id in $(grep -i 'vedit\|vd-app\|VEdit' "/evid/$TAG-windows.txt" | awk '{print $1}'); do
  xwininfo -id "$id" >"/evid/$TAG-$id-info.txt" 2>&1
  grep -E 'Map State|Width|Height' "/evid/$TAG-$id-info.txt"
  if xwd -silent -id "$id" -out "/tmp/$id.xwd" 2>/dev/null; then
    xwdtopnm "/tmp/$id.xwd" 2>/dev/null | pnmtopng >"/evid/$TAG-$id.png" 2>/dev/null
    echo "capture $id bytes=$(wc -c <"/evid/$TAG-$id.png" 2>/dev/null || echo 0)"
  fi
done
kill "$APP_PID" 2>/dev/null || true
sleep 0.3
