#!/usr/bin/env bash
# Build `slang-server` (hudson-trading/slang-server v0.3.1) for the CentOS 7 /
# glibc 2.17 baseline, INSIDE a fresh centos:7 container (matches the recipe the
# repository already uses for the `slang` binary: build/slang/scripts/build-in-container.sh).
#
# Why: the upstream prebuilt `slang-server-linux-x64` links GLIBC_2.27/2.25/2.18
# and cannot start on CentOS 7 (evidence/w2-spike/02-prebuilt-run.txt). The spike
# therefore builds from source with the same toolchain the repo already uses:
#   * CentOS 7.9.2009 + its vault mirror
#   * devtoolset-11 (gcc 11.2.1, the newest compiler available for CentOS 7)
#   * CMake 3.31.6 (Kitware binary, runs on glibc 2.17)
#   * CPython 3.12.8 built here (slang's code generators need >= 3.9)
#   * -static-libstdc++ -static-libgcc so the artifact needs only glibc
#
# Usage (driven by probes/slang-spike/run-spike.sh):
#   docker run --rm -v <work>:/work -w /work centos:7 bash /work/build.sh
#
# Outputs under /work/out: 01-yum.log 02-toolchain.txt 03-source.txt
# 04-configure.log 05-build.log 06-artifact.txt 07-timings.txt and slang-server.
set -euo pipefail
set -E
trap 'rc=$?; printf "FATAL: line %s exited %s\n" "$LINENO" "$rc" >&2' ERR

log() { printf '[%s] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*"; }
die() { log "FATAL: $*"; exit 1; }

: "${SLANG_SERVER_TAG:?}" "${SLANG_SERVER_REPO:?}" "${SLANG_SERVER_COMMIT:?}"
: "${CMAKE_VERSION:?}" "${PYTHON_VERSION:?}"

MIRROR="${CENTOS_VAULT_BASE:?}"
WORK=/work
OUT=$WORK/out
CACHE=$WORK/cache
BUILD=/build
mkdir -p "$OUT" "$CACHE" "$BUILD"

log "building slang-server $SLANG_SERVER_TAG for $(cat /etc/centos-release 2>/dev/null || echo unknown)"

# ---------------------------------------------------------------- toolchain --
rm -f /etc/yum.repos.d/CentOS-*.repo
cat > /etc/yum.repos.d/vault.repo <<REPO
[base]
name=CentOS-7.9.2009-Base
baseurl=$MIRROR/os/x86_64/
gpgcheck=0
[updates]
name=CentOS-7.9.2009-Updates
baseurl=$MIRROR/updates/x86_64/
gpgcheck=0
[extras]
name=CentOS-7.9.2009-Extras
baseurl=$MIRROR/extras/x86_64/
gpgcheck=0
[sclo-rh]
name=CentOS-7.9.2009-SCLo-rh
baseurl=$MIRROR/sclo/x86_64/rh/
gpgcheck=0
[sclo-sclo]
name=CentOS-7.9.2009-SCLo-sclo
baseurl=$MIRROR/sclo/x86_64/sclo/
gpgcheck=0
REPO
t0=$(date +%s)
log "installing devtoolset-11 + build tools from the vault mirror"
yum -y install devtoolset-11-gcc-c++ devtoolset-11-binutils devtoolset-11-runtime \
  devtoolset-11-libstdc++-devel git make tar gzip which file ca-certificates \
  zlib-devel openssl-devel bzip2-devel xz-devel libffi-devel >"$OUT/01-yum.log" 2>&1 \
  || { tail -40 "$OUT/01-yum.log"; die "yum install failed"; }
t1=$(date +%s)

set +u
# shellcheck disable=SC1091
source /opt/rh/devtoolset-11/enable
set -u
gcc_major=$(gcc -dumpversion | cut -d. -f1)
[ "$gcc_major" -ge 11 ] || die "gcc is $gcc_major, expected >= 11"
{
  echo "# toolchain ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  cat /etc/centos-release; ldd --version | sed -n 1p
  gcc --version | sed -n 1p; g++ --version | sed -n 1p
  make --version | sed -n 1p; git --version; nproc
} | tee "$OUT/02-toolchain.txt"

# -------------------------------------------------------------------- cmake --
cmake_tgz=$CACHE/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz
[ -f "$cmake_tgz" ] || curl -fsSL -o "$cmake_tgz" \
  "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz" \
  || die "cmake download failed"
echo "5a1133ff103c71eb5120e2cc3de922733e7d8a26a98ae716397e8676adb367bf  $cmake_tgz" | sha256sum -c - \
  || die "cmake archive does not match the pinned sha256"
tar xzf "$cmake_tgz" -C "$BUILD"
CMAKE=$BUILD/cmake-$CMAKE_VERSION-linux-x86_64/bin/cmake
[ -x "$CMAKE" ] || die "cmake binary missing"

# ---------------------------------------------------------------- python 3 ---
python_tgz=$CACHE/Python-$PYTHON_VERSION.tgz
[ -f "$python_tgz" ] || curl -fsSL -o "$python_tgz" \
  "https://www.python.org/ftp/python/$PYTHON_VERSION/Python-$PYTHON_VERSION.tgz" \
  || die "CPython download failed"
echo "5978435c479a376648cb02854df3b892ace9ed7d32b1fead652712bee9d03a45  $python_tgz" | sha256sum -c - \
  || die "CPython archive does not match the pinned sha256"
log "building CPython $PYTHON_VERSION (slang code generation needs >= 3.9)"
rm -rf "$BUILD/Python-$PYTHON_VERSION" /opt/python3
tar xzf "$python_tgz" -C "$BUILD"
(
  cd "$BUILD/Python-$PYTHON_VERSION"
  ./configure --prefix=/opt/python3 --without-ensurepip >"$OUT/02b-python.log" 2>&1
  make -j"$(nproc)" >>"$OUT/02b-python.log" 2>&1
  make install >>"$OUT/02b-python.log" 2>&1
) || { tail -30 "$OUT/02b-python.log"; die "CPython build failed"; }
PYTHON3=/opt/python3/bin/python3
[ -x "$PYTHON3" ] || die "python3 missing"
t2=$(date +%s)

# ------------------------------------------------------------------- source --
SRC=$BUILD/slang-server-src
log "cloning $SLANG_SERVER_REPO at $SLANG_SERVER_TAG (with submodules)"
rm -rf "$SRC"
git clone --quiet --depth 1 --branch "$SLANG_SERVER_TAG" "$SLANG_SERVER_REPO" "$SRC" \
  || die "git clone failed"
actual=$(cd "$SRC" && git rev-parse HEAD)
[ "$actual" = "$SLANG_SERVER_COMMIT" ] || die "commit $actual != pinned $SLANG_SERVER_COMMIT"
( cd "$SRC" && git submodule update --init --recursive ) \
  || die "submodule update failed"
{
  echo "# source ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  echo "repository=$SLANG_SERVER_REPO"; echo "tag=$SLANG_SERVER_TAG"
  echo "pinned_commit=$SLANG_SERVER_COMMIT"; echo "checked_out_commit=$actual"
  echo "## submodules:"; ( cd "$SRC" && git submodule status --recursive )
} | tee "$OUT/03-source.txt"

# ----------------------------------------------------------------- configure --
log "configuring (Release, static libstdc++/libgcc)"
"$CMAKE" -S "$SRC" -B "$BUILD/obj" -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_CXX_FLAGS="-include /work/gcc11_path_hash.h" \
  -DPython_EXECUTABLE="$PYTHON3" -DPYTHON_EXECUTABLE="$PYTHON3" \
  "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc" \
  >"$OUT/04-configure.log" 2>&1 \
  || { tail -40 "$OUT/04-configure.log"; die "cmake configure failed"; }
t3=$(date +%s)

# --------------------------------------------------------------------- build --
log "building target slang_server with -j$(nproc)"
"$CMAKE" --build "$BUILD/obj" --target slang_server -j"$(nproc)" >"$OUT/05-build.log" 2>&1 \
  || { tail -60 "$OUT/05-build.log"; die "build failed"; }
t4=$(date +%s)

built=$(find "$BUILD/obj" -maxdepth 4 -type f -name slang-server -perm -u+x | sed -n 1p)
[ -n "$built" ] || die "slang-server binary not found under $BUILD/obj"
cp "$built" "$OUT/slang-server"
strip --strip-all "$OUT/slang-server" || log "WARN: strip failed"
chmod 755 "$OUT/slang-server"
{
  echo "# artifact ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  ls -l "$OUT/slang-server"; file "$OUT/slang-server"; sha256sum "$OUT/slang-server"
  echo "## highest GLIBC symbol versions required:"
  objdump -T "$OUT/slang-server" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -6
  echo "## ldd:"; ldd "$OUT/slang-server" || true
} | tee "$OUT/06-artifact.txt"

{
  echo "# phase durations ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  echo "yum_install_s=$((t1 - t0))"
  echo "python_build_s=$((t2 - t1))"
  echo "configure_s=$((t3 - t2))"
  echo "compile_link_s=$((t4 - t3))"
  echo "total_s=$((t4 - t0))"
  echo "cpus=$(nproc)"
} | tee "$OUT/07-timings.txt"
log "DONE: $(sha256sum "$OUT/slang-server")"
