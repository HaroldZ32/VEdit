#!/bin/sh
# Post-release regression: run the PUBLIC GitHub Release assets in a clean,
# offline CentOS 7 container (no gtk3/GtkSourceView rpm, --network none, Xvfb).
#
# The files under /art are exactly what `gh release download v0.1.0` produced;
# nothing from the repository's local packaging/*/out is mounted or copied here.
set -u

ART=${ART:-/art}
EVID=${EVID:-/evid}
FAIL=0
say() { echo "[release-verify] $*"; }
check() { # check <name> <rc> <detail>
  if [ "$2" -eq 0 ]; then say "PASS $1 ($3)"; echo "RESULT $1=PASS" >> "$EVID/99-results.txt"
  else say "FAIL $1 ($3)"; echo "RESULT $1=FAIL" >> "$EVID/99-results.txt"; FAIL=$((FAIL + 1)); fi
}
: > "$EVID/99-results.txt"
mkdir -p /tmp/xdg
export DISPLAY=:99 XDG_RUNTIME_DIR=/tmp/xdg LIBGL_ALWAYS_SOFTWARE=1 LANG=en_US.UTF-8 NO_AT_BRIDGE=1

say "assets under /art (downloaded from GitHub Release v0.1.0, not local builds):"
ls -l "$ART"

say "negative assertion: no gtk3 / gtksourceview rpms in this image"
if rpm -q gtk3 gtksourceview3 gtksourceview4 >/dev/null 2>&1; then
  check "no_system_gtk" 1 "a GTK rpm is installed"
else
  check "no_system_gtk" 0 "clean image"
fi
say "glibc: $(rpm -q --qf '%{version}-%{release}' glibc)"

Xvfb :99 -screen 0 1280x900x24 -nolisten tcp >"$EVID/00-xvfb.log" 2>&1 &
XVFB_PID=$!
trap 'kill "$XVFB_PID" 2>/dev/null' EXIT
sleep 2
xdpyinfo >/dev/null 2>&1 && check "xvfb_up" 0 "DISPLAY=:99" || { check "xvfb_up" 1 "no X"; exit 1; }

CHECKS=51
mkdir -p /tmp/ci

# ---------------------------------------------------------------- AppImage
say "== AppImage =="
cp "$ART/VEdit-0.1.0-x86_64.AppImage" /tmp/ci/VEdit.AppImage
chmod +x /tmp/ci/VEdit.AppImage
( cd /tmp/ci && ./VEdit.AppImage --appimage-extract >/dev/null 2>&1 )
app_vd=$(sha256sum /tmp/ci/squashfs-root/usr/bin/vd-app | cut -d' ' -f1)
echo "APPIMAGE_vd_app_sha256=$app_vd" >> "$EVID/01-hashes.txt"
APPIMAGE_EXTRACT_AND_RUN=1 timeout 300 /tmp/ci/VEdit.AppImage --selftest all \
  >"$EVID/10-appimage-selftest.log" 2>&1
check "appimage_selftest_all" "$?" "exit=$?  (log 10-appimage-selftest.log)"
n=$(grep -c '"name":' "$EVID/10-appimage-selftest.log")
check "appimage_check_count" "$([ "$n" -eq "$CHECKS" ] && echo 0 || echo 1)" "checks=$n (expected $CHECKS)"

# ---------------------------------------------------------------- tar.gz
say "== tar.gz =="
mkdir -p /tmp/ci/tar && tar xzf "$ART/vedit-0.1.0-linux-x86_64.tar.gz" -C /tmp/ci/tar
tar_vd=$(sha256sum /tmp/ci/tar/VEdit/bin/vd-app | cut -d' ' -f1)
echo "TARBALL_vd_app_sha256=$tar_vd" >> "$EVID/01-hashes.txt"
timeout 300 /tmp/ci/tar/VEdit/vedit.sh --selftest all >"$EVID/11-targz-selftest.log" 2>&1
check "targz_selftest_all" "$?" "exit=$?  (log 11-targz-selftest.log)"
n=$(grep -c '"name":' "$EVID/11-targz-selftest.log")
check "targz_check_count" "$([ "$n" -eq "$CHECKS" ] && echo 0 || echo 1)" "checks=$n (expected $CHECKS)"

# ---------------------------------------------------------------- .run
say "== self-extracting .run =="
timeout 300 sh "$ART/VEdit-0.1.0-x86_64.run" --selftest all >"$EVID/12-run-selftest.log" 2>&1
check "run_selftest_all" "$?" "exit=$?  (log 12-run-selftest.log)"
n=$(grep -c '"name":' "$EVID/12-run-selftest.log")
check "run_check_count" "$([ "$n" -eq "$CHECKS" ] && echo 0 || echo 1)" "checks=$n (expected $CHECKS)"
# the extracted tree must be gone again
if ls -d /tmp/vedit.* /tmp/xdg/vedit.* 2>/dev/null | grep -q .; then
  check "run_cleanup" 1 "an extracted tree is still present: $(ls -d /tmp/vedit.* 2>/dev/null | head -1)"
else
  check "run_cleanup" 0 "no extracted tree left behind"
fi
# hash the vd-app inside the .run payload (marker + GNU tail; no python here)
marker=$(awk '/^__PAYLOAD_BELOW__$/{print NR; exit}' "$ART/VEdit-0.1.0-x86_64.run")
mkdir -p /tmp/ci/runpayload
tail -n +$((marker + 1)) "$ART/VEdit-0.1.0-x86_64.run" | tar xz -C /tmp/ci/runpayload
run_vd=$(sha256sum /tmp/ci/runpayload/VEdit/bin/vd-app | cut -d' ' -f1)
echo "RUN_vd_app_sha256=$run_vd" >> "$EVID/01-hashes.txt"

say "expected vd-app sha256: 41d02dcac944c7bff614ab6c44a51f243cece7692afc4d9dda1fc644adef1611"
for pair in "appimage:$app_vd" "targz:$tar_vd" "run:$run_vd"; do
  name=${pair%%:*}; sha=${pair#*:}
  check "vd_app_matches_frozen:$name" \
    "$([ "$sha" = "41d02dcac944c7bff614ab6c44a51f243cece7692afc4d9dda1fc644adef1611" ] && echo 0 || echo 1)" \
    "sha=$sha"
done

say "RELEASE VERIFICATION RESULT: failures=$FAIL"
echo "FAILURES=$FAIL" >> "$EVID/99-results.txt"
[ "$FAIL" -eq 0 ] || exit 1
