#!/usr/bin/env bash
# Headless host matrix for the DELIVERY AppImage (T31). Run from the repo:
#   bash packaging/runtime/hostmatrix.sh [--appimage <file>] [--evidence <dir>]
#
# Each row runs a stock container with NO KF6/Qt/verible/slang installed, then:
#   * uname -r / glibc (the container's userspace; the kernel is the HOST kernel),
#   * extract the AppImage, run its AppRun --selfcheck (highlight/fold/toolchain),
#   * start it with --hold 300 (offscreen) and require exit 0,
#   * run the BUNDLED verible-verilog-lint on a line-length violation and require
#     a real "line-length" diagnostic (exit 1).
#
# HONEST LIMIT: containers share the host kernel. This matrix cannot and does not
# prove a real CentOS 7 (kernel 3.10) machine; that row stays UNTESTED.
set -uo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
APPIMAGE=${VEDIT_APPIMAGE:-$ROOT/packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage}
EVID=${VEDIT_MATRIX_EVIDENCE:-$ROOT/evidence/slice3-pack/final-206033c/matrix}
CENTOS=docker.m.daocloud.io/library/centos:7
IMAGES=(
  "$CENTOS"
  docker.m.daocloud.io/library/ubuntu:24.04
  docker.m.daocloud.io/library/rockylinux:9
  docker.m.daocloud.io/library/ubuntu:26.04
)
while [ $# -gt 0 ]; do
  case "$1" in
    --appimage) APPIMAGE=${2:?}; shift 2 ;;
    --evidence) EVID=${2:?}; shift 2 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown: $1" >&2; exit 2 ;;
  esac
done
[ -f "$APPIMAGE" ] || { echo "ERROR: no AppImage: $APPIMAGE" >&2; exit 1; }
APPIMAGE=$(cd "$(dirname "$APPIMAGE")" && pwd -P)/$(basename "$APPIMAGE")
mkdir -p "$EVID"
printf 'image\tuname_r\tglibc\tselfcheck\tstartup\tlint\thighlight\n' > "$EVID/summary.tsv"
printf '# all containers share the host kernel (%s); a real CentOS 7 (3.10) host is UNTESTED\n' "$(uname -r)" > "$EVID/README.txt"
printf '# appimage sha256: %s\n# appimage: %s\n' "$(sha256sum "$APPIMAGE" | cut -d' ' -f1)" "$APPIMAGE" >> "$EVID/README.txt"
pass=0; fail=0
for image in "${IMAGES[@]}"; do
  name=$(printf '%s' "$image" | tr '/:@' '___')
  log="$EVID/$name.log"
  docker run --rm -v "$APPIMAGE:/img/app.AppImage:ro" \
    -v "$ROOT/crates/vd-lsp/fixtures/project/bad_line_length.sv:/fixtures/bad_line_length.sv:ro" \
    "$image" bash -c '
    set -u
    echo "image=$(grep -m1 "PRETTY_NAME" /etc/os-release | cut -d= -f2-)"
    echo "uname_r=$(uname -r)"
    echo "glibc=$(ldd --version 2>/dev/null | head -1 || echo unavailable)"
    cd /tmp && APPIMAGE_EXTRACT_AND_RUN=1 /img/app.AppImage --appimage-extract >/dev/null 2>&1 || { echo "EXTRACT-FAIL"; exit 9; }
    R=/tmp/squashfs-root
    E="env -i PATH=/usr/bin:/bin HOME=/tmp QT_QPA_PLATFORM=offscreen LANG=C.UTF-8"
    echo "== selfcheck =="
    $E "$R/AppRun" --selfcheck > /tmp/self.log 2>&1; echo "selfcheck_exit=$?"
    grep -E "APP-SHELL-SELFTEST|TOOLCHAIN source|syntax-product|fold.wiring.closed.hidden-975|CHECK FAIL" /tmp/self.log | head -8
    echo "highlight_lines=$(grep -cE "CHECK editcore.highlight.verilog.(mode|format-ranges|folding-regions) PASS" /tmp/self.log)"
    echo "== startup =="
    $E "$R/AppRun" --hold 300 >/tmp/start.log 2>&1; echo "startup_exit=$?"
    echo "== lint =="
    "$R/bin/verible-verilog-lint" /fixtures/bad_line_length.sv > /tmp/lint.log 2>&1; echo "lint_exit=$?"
    grep -m1 "line-length" /tmp/lint.log || echo "lint_no_line_length"
  ' > "$log" 2>&1
  rc=$?
  uname_r=$(sed -n 's/^uname_r=//p' "$log" | head -1)
  glibc=$(sed -n 's/^glibc=//p' "$log" | head -1)
  sc=$(grep -q "APP-SHELL-SELFTEST ok=1" "$log" && echo PASS || echo FAIL)
  start=$(grep -q "startup_exit=0" "$log" && echo PASS || echo FAIL)
  lint=$(grep -q "lint_exit=1" "$log" && grep -q "line-length" "$log" && echo PASS || echo FAIL)
  hi=$sc
  [ "$rc" -eq 0 ] && [ "$sc" = PASS ] && [ "$start" = PASS ] && [ "$lint" = PASS ] && pass=$((pass+1)) || fail=$((fail+1))
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$image" "${uname_r:-?}" "${glibc:-?}" "$sc" "$start" "$lint" "$hi" >> "$EVID/summary.tsv"
  echo "ROW $image uname=$uname_r glibc=$glibc selfcheck=$sc startup=$start lint=$lint"
done
echo "MATRIX pass=$pass fail=$fail (containers; host kernel $(uname -r); real CentOS 7/3.10 UNTESTED)"
[ "$fail" -eq 0 ]
