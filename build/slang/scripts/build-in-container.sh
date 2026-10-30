#!/usr/bin/env bash
# Build slang for the CentOS 7 / glibc 2.17 baseline, INSIDE a fresh
# docker.m.daocloud.io/library/centos:7 container (the workspace is mounted at
# /work). Driven by build.sh; not meant to run on the host.
#
# Chain of evidence written to /work/evidence/:
#   01-yum-install.log    package installation from the CentOS 7.9.2009 vault
#   02-toolchain.txt      container, glibc, gcc/g++/ld/make/git/cmake versions
#   03-source.txt         cloned tag, commit and pin check
#   04-configure.log      cmake configure output
#   05-deps.txt           exact FetchContent dependency commits
#   06-build.log          compile + link output
#   07-artifact.txt       file(1), size, sha256, ldd of the built binary
#   08-timings.txt        phase durations
set -euo pipefail
# Report the failing line instead of aborting silently inside a pipeline.
set -E
trap 'rc=$?; printf "FATAL: line %s exited %s\\n" "$LINENO" "$rc" >&2' ERR

log() { printf '[%s] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*"; }
die() { log "FATAL: $*"; exit 1; }

: "${SLANG_VERSION:?SLANG_VERSION is empty}"
: "${SLANG_TAG:?SLANG_TAG is empty}"
: "${SLANG_REPO:?SLANG_REPO is empty}"
: "${SLANG_COMMIT:?SLANG_COMMIT is empty}"
: "${CMAKE_VERSION:?CMAKE_VERSION is empty}"
: "${CMAKE_URL:?CMAKE_URL is empty}"
: "${CMAKE_SHA256:?CMAKE_SHA256 is empty}"
MIRROR="${CENTOS_VAULT_BASE:?CENTOS_VAULT_BASE is empty}"

WORK=/work
EV=$WORK/evidence
CACHE=$WORK/cache
BUILD=/build
OBJ=$BUILD/obj
SRC=$BUILD/slang-src
mkdir -p "$EV" "$CACHE" "$BUILD"

log "build target: slang $SLANG_VERSION (tag $SLANG_TAG, commit $SLANG_COMMIT)"
log "container: $(cat /etc/centos-release 2>/dev/null || echo unknown)"

# ---------------------------------------------------------------- toolchain --
# The stock CentOS-*.repo files point at mirror.centos.org, which no longer
# serves CentOS 7; use the vault mirror recipe recorded in
# .mpd/recon/build-recipe-centos7.md (SCLo packages included).
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
log "installing devtoolset-11 + git/make/tar from $MIRROR"
if ! yum -y install \
    devtoolset-11-gcc-c++ devtoolset-11-binutils \
    devtoolset-11-runtime devtoolset-11-libstdc++-devel \
    git make tar gzip which file ca-certificates \
    zlib-devel openssl-devel bzip2-devel xz-devel libffi-devel \
    >"$EV/01-yum-install.log" 2>&1; then
  tail -40 "$EV/01-yum-install.log"
  die "yum install failed (log: evidence/01-yum-install.log)"
fi
t1=$(date +%s)

# devtoolset is enabled for this shell only; the produced binary does not
# depend on it (libstdc++/libgcc are linked statically below).
set +u
# shellcheck disable=SC1091
source /opt/rh/devtoolset-11/enable
set -u

gcc_major=$(gcc -dumpversion | cut -d. -f1)
[ "$gcc_major" -ge 11 ] || die "gcc is $gcc_major, expected >= 11 (devtoolset-11)"
static_libcxx=$(find /opt/rh/devtoolset-11 -name 'libstdc++.a' -print -quit)
[ -n "$static_libcxx" ] || die "devtoolset-11 does not ship libstdc++.a; static link impossible"

{
  echo "# toolchain inside the build container ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  echo "## container"; cat /etc/centos-release
  echo "## glibc"; ldd --version | sed -n '1p'
  echo "## gcc"; gcc --version | sed -n '1p'
  echo "## g++"; g++ --version | sed -n '1p'
  echo "## ld"; ld --version | sed -n '1p'
  echo "## make"; make --version | sed -n '1p'
  echo "## git"; git --version
  echo "## static libstdc++: $static_libcxx"
  echo "## bash"; bash --version | sed -n '1p'
  echo "## cpus"; nproc
} | tee "$EV/02-toolchain.txt"

# ---------------------------------------------------------------- python 3 ---
# slang's code generators (scripts/syntax_gen.py, diagnostic_gen.py) are Python
# 3 scripts that use str.removesuffix, i.e. Python >= 3.9. CentOS 7 ships only
# 2.7.5 (base python3 is 3.6, the newest SCL python is 3.8), so a build-time
# CPython is compiled here. It is used to generate headers only; the artifact
# does not depend on it.
: "${PYTHON_VERSION:?PYTHON_VERSION is empty}"
: "${PYTHON_URL:?PYTHON_URL is empty}"
: "${PYTHON_SHA256:?PYTHON_SHA256 is empty}"
python_tgz=$CACHE/Python-$PYTHON_VERSION.tgz
if [ ! -f "$python_tgz" ]; then
  log "downloading CPython $PYTHON_VERSION"
  curl -fsSL -o "$python_tgz" "$PYTHON_URL" || die "CPython download failed"
fi
echo "$PYTHON_SHA256  $python_tgz" | sha256sum -c - \
  || die "CPython archive does not match the pinned sha256"
log "building CPython $PYTHON_VERSION (build-time code generator only)"
rm -rf "$BUILD/Python-$PYTHON_VERSION" /opt/python3
tar xzf "$python_tgz" -C "$BUILD"
(
  cd "$BUILD/Python-$PYTHON_VERSION"
  ./configure --prefix=/opt/python3 --without-ensurepip >"$EV/02b-python-build.log" 2>&1 \
    || die "CPython configure failed (log: evidence/02b-python-build.log)"
  make -j"$(nproc)" >>"$EV/02b-python-build.log" 2>&1 \
    || die "CPython build failed (log: evidence/02b-python-build.log)"
  make install >>"$EV/02b-python-build.log" 2>&1 \
    || die "CPython install failed (log: evidence/02b-python-build.log)"
)
export PATH=/opt/python3/bin:$PATH
PYTHON3=/opt/python3/bin/python3
[ -x "$PYTHON3" ] || die "python3 was not installed at $PYTHON3"
"$PYTHON3" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 9) else 1)' \
  || die "python3 is older than 3.9, slang code generation cannot run"
{
  echo "# build-time python ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  echo "## system python (CentOS 7 base): $(/usr/bin/python -V 2>&1)"
  echo "## build-time python:"; "$PYTHON3" --version
  echo "## executable: $("$PYTHON3" -c 'import sys; print(sys.executable)')"
  echo "## path: $(command -v python3 || echo NOT-IN-PATH)"
} | tee "$EV/02c-python.txt"

# --------------------------------------------------------------------- cmake --
# slang v12.1 requires CMake >= 3.28; CentOS 7 has 2.8 and no usable cmake3 in
# its vault. The official Kitware x86_64 build runs on glibc 2.17 (verified).
cmake_tgz=$CACHE/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz
if [ ! -f "$cmake_tgz" ]; then
  log "downloading cmake $CMAKE_VERSION"
  curl -fsSL -o "$cmake_tgz" "$CMAKE_URL" || die "cmake download failed"
fi
echo "$CMAKE_SHA256  $cmake_tgz" | sha256sum -c - \
  || die "cmake archive does not match the pinned sha256"
tar xzf "$cmake_tgz" -C "$BUILD"
CMAKE=$BUILD/cmake-$CMAKE_VERSION-linux-x86_64/bin/cmake
[ -x "$CMAKE" ] || die "cmake binary not found at $CMAKE"
log "cmake: $("$CMAKE" --version | sed -n '1p')"

# -------------------------------------------------------------------- source --
log "cloning $SLANG_REPO at $SLANG_TAG (shallow)"
rm -rf "$SRC"
git clone --quiet --depth 1 --branch "$SLANG_TAG" "$SLANG_REPO" "$SRC" \
  || die "git clone failed"
actual_commit=$(cd "$SRC" && git rev-parse HEAD)
[ "$actual_commit" = "$SLANG_COMMIT" ] \
  || die "source commit $actual_commit != pinned $SLANG_COMMIT"
{
  echo "# slang source provenance ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  echo "repository=$SLANG_REPO"
  echo "tag=$SLANG_TAG"
  echo "pinned_commit=$SLANG_COMMIT"
  echo "checked_out_commit=$actual_commit"
  echo "git_describe=$(cd "$SRC" && git describe --tags --always)"
  echo "working_tree_status_lines=$(cd "$SRC" && git status --porcelain | wc -l)"
} | tee "$EV/03-source.txt"

# --------------------------------------------------- compatibility patches ----
# GCC 11.2.1 (devtoolset-11, the newest compiler available for CentOS 7)
# predates std::hash<std::filesystem::path> in libstdc++ (added in GCC 11.3/12),
# which slang's hashing fallback needs. The patches under /work/patches are
# applied verbatim to the pinned checkout; a patch that stops applying fails the
# build loudly, so upstream changes surface here instead of silently diverging.
compgen -G "/work/patches/*.patch" >/dev/null \
  || die "no patches found in /work/patches: the GCC 11.2 build cannot proceed without 0001"
{
  echo "# applied patches ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  for patch_file in /work/patches/*.patch; do
    name=$(basename "$patch_file")
    sha=$(sha256sum "$patch_file" | cut -d' ' -f1)
    ( cd "$SRC" && git apply --check "$patch_file" ) \
      || die "patch $name does not apply to $SLANG_TAG"
    ( cd "$SRC" && git apply "$patch_file" ) || die "patch $name failed to apply"
    echo "applied=$name sha256=$sha"
  done
  echo "## resulting diff:"
  ( cd "$SRC" && git --no-pager diff )
} | tee "$EV/03b-patches.txt"

# ----------------------------------------------------------------- configure --
t2=$(date +%s)
log "configuring (Release, static libstdc++/libgcc)"
"$CMAKE" \
  -S "$SRC" -B "$OBJ" -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++ \
  -DSLANG_INCLUDE_TESTS=OFF \
  -DSLANG_INCLUDE_DOCS=OFF \
  -DSLANG_INCLUDE_TOOLS=ON \
  -DSLANG_INCLUDE_PYLIB=OFF \
  -DPython_EXECUTABLE="$PYTHON3" \
  -DPYTHON_EXECUTABLE="$PYTHON3" \
  "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc" \
  2>&1 | tee "$EV/04-configure.log" \
  || die "cmake configure failed (log: evidence/04-configure.log)"
t3=$(date +%s)

{
  echo "# FetchContent dependency checkouts ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  for d in "$OBJ"/_deps/*-src; do
    [ -d "$d" ] || continue
    name=$(basename "$d")
    commit=$(cd "$d" && git rev-parse HEAD 2>/dev/null || echo unknown)
    describe=$(cd "$d" && git describe --tags --always 2>/dev/null || echo unknown)
    echo "$name commit=$commit describe=$describe"
  done
} | tee "$EV/05-deps.txt"

# --------------------------------------------------------------------- build --
log "building target slang_driver with -j$(nproc)"
"$CMAKE" --build "$OBJ" --target slang_driver -j"$(nproc)" 2>&1 | tee "$EV/06-build.log" \
  || die "build failed (log: evidence/06-build.log)"
t4=$(date +%s)

built=$(find "$OBJ" -maxdepth 3 -type f -name slang -perm -u+x | sed -n '1p')
[ -n "$built" ] || die "slang binary not found under $OBJ"
mkdir -p "$WORK/out"
cp "$built" "$WORK/out/slang"
unstripped_bytes=$(stat -c '%s' "$WORK/out/slang")
strip --strip-all "$WORK/out/slang" || log "WARN: strip failed (artifact kept unstripped)"
chmod 755 "$WORK/out/slang"
stripped_bytes=$(stat -c '%s' "$WORK/out/slang")

{
  echo "# artifact ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  ls -l "$WORK/out/slang"
  file "$WORK/out/slang"
  sha256sum "$WORK/out/slang"
  echo "unstripped_bytes=$unstripped_bytes"
  echo "stripped_bytes=$stripped_bytes"
  echo "## ldd inside the build container:"
  ldd "$WORK/out/slang" || true
} | tee "$EV/07-artifact.txt"

{
  echo "# phase durations ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
  echo "started_utc=$(date -u -d "@$t0" +%Y-%m-%dT%H:%M:%SZ)"
  echo "finished_utc=$(date -u -d "@$t4" +%Y-%m-%dT%H:%M:%SZ)"
  echo "yum_install_s=$((t1 - t0))"
  echo "configure_s=$((t3 - t2))"
  echo "compile_link_s=$((t4 - t3))"
  echo "total_s=$((t4 - t0))"
  echo "cpus=$(nproc)"
} | tee "$EV/08-timings.txt"

log "BUILD DONE: $(sha256sum "$WORK/out/slang")"
