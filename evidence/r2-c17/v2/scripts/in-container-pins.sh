#!/usr/bin/env bash
# T59 / C17 v2 — in-container identity + pin assertions, run INSIDE the official
# build image (vedit-qt6-build:local). Read-only: nothing is written.
#
# Reproducibility anchor is the pin set + the image's own build-time facts, NOT
# the image id (T10 measured three different ids for three builds of the same
# Dockerfile: apt unpack timestamps).
set -u

echo "== container os =="
head -3 /etc/os-release
echo "== glibc =="
ldd --version | head -1
echo "== kernel (shared with host) =="
uname -r

echo "== 22 locked packages: lock version vs installed =="
n=0
fail=0
while read -r spec sha rest; do
  case "$spec" in '' | '#'*) continue ;; esac
  n=$((n + 1))
  pkg="${spec%%=*}"
  ver="${spec#*=}"
  got=$(dpkg-query -W -f="\${Version}" "$pkg" 2>/dev/null || echo MISSING)
  if [ "$got" = "$ver" ]; then
    st=OK
  else
    st=MISMATCH
    fail=$((fail + 1))
  fi
  printf '%-34s lock=%-22s installed=%-22s %s\n' "$pkg" "$ver" "$got" "$st"
done </work/scripts/docker/qt6-locked-packages.txt
echo "checked=$n mismatches=$fail"

echo "== /opt/vedit image facts (written by the Dockerfile at image build time) =="
cat /opt/vedit/build-image.txt 2>/dev/null || echo "MISSING /opt/vedit/build-image.txt"

echo "== image package snapshot vs the T10 evidence copy (provenance w/o image id) =="
sha256sum /opt/vedit/package-snapshot.txt \
  /work/evidence/m0-build/build-logs/30-qt6-package-snapshot.txt 2>&1

echo "== locked-package list: image copy vs repo copy =="
sha256sum /opt/vedit/qt6-locked-packages.txt \
  /work/scripts/docker/qt6-locked-packages.txt 2>&1

echo "== probe prerequisites =="
for d in /usr/lib/x86_64-linux-gnu/cmake/KF6TextEditor \
  /usr/lib/x86_64-linux-gnu/cmake/KF6SyntaxHighlighting \
  /usr/lib/x86_64-linux-gnu/cmake/Qt6Widgets; do
  if [ -d "$d" ]; then echo "OK $d"; else echo "MISSING $d"; fi
done
echo "xvfb-run: $(command -v xvfb-run || echo MISSING)"
echo "katepart: $(ls /usr/lib/x86_64-linux-gnu/qt6/plugins/kf6/parts/katepart.so 2>/dev/null || echo MISSING)"
echo "cmake: $(cmake --version | head -1)"
echo "c++: $(c++ --version | head -1)"
echo "qt: $(qmake6 -query QT_VERSION 2>/dev/null || echo unknown)"
echo "tree-sitter (pkg-config): $(pkg-config --modversion tree-sitter 2>/dev/null || echo unknown)"
echo "KTextEditor cmake version: $(grep -m1 'set(PACKAGE_VERSION' /usr/lib/x86_64-linux-gnu/cmake/KF6TextEditor/KF6TextEditorConfigVersion.cmake 2>/dev/null || echo unknown)"
