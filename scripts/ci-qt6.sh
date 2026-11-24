#!/usr/bin/env bash
# Entry points for the fresh hosted jobs in .github/workflows/image.yml.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

version() {
  local workspace_version
  workspace_version=$(python3 -c 'import tomllib; print(tomllib.load(open("Cargo.toml", "rb"))["workspace"]["package"]["version"])')
  if [[ ! "$workspace_version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$ ]]; then
    echo "Invalid release version in Cargo.toml: $workspace_version" >&2
    return 1
  fi
  printf '%s\n' "$workspace_version"
}

rust_gates() {
  # shellcheck source=lib.sh
  source scripts/lib.sh
  load_versions
  run_in_build_container cargo fmt --all -- --check
  run_in_build_container cargo clippy --locked --all-targets \
    -p vd-ffi -p vd-search -p vd-settings -p vd-vcs -- -D warnings
}

qt_image() {
  # shellcheck source=lib-qt.sh
  source scripts/lib-qt.sh
  load_qt_versions
  mkdir -p "$QT_EVIDENCE_DIR"
  build_qt6_image "$QT_EVIDENCE_DIR/image-build.log"
}

qt_gates() {
  qt_image
  run_in_qt6_container bash scripts/build-qt-in-container.sh --tests
  run_in_qt6_container bash build/gates/lrelease.sh
  bash build/gates/docs.sh
  local expected actual
  expected="vedit-app $(version)"
  actual=$(run_in_qt6_container env QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --version)
  if [[ "$actual" != "$expected" ]]; then
    printf 'Expected CLI version %s, got %s\n' "$expected" "$actual" >&2
    return 1
  fi
  printf 'Compiled CLI version %s\n' "$actual"
}

f2_inputs() {
  # shellcheck source=lib-qt.sh
  source scripts/lib-qt.sh
  run_in_qt6_container bash scripts/build-ksh-selfbuild-in-container.sh F2
  tar --zstd -cf "${RUNNER_TEMP:?}/qt6.tar.zst" \
    target/qt6-build/src/app/vedit-app \
    target/qt6-build/cmake/smoke/vedit-ksh-env-probe \
    target/qt6-build/src/app/translations/vedit_zh_CN.qm \
    target/qt6-build/src/app/translations/qtbase_zh_CN.qm \
    target/ksh-selfbuild/F2/install/lib
}

package() {
  qt_image
  bash scripts/fetch-verible.sh
  run_in_qt6_container bash packaging/runtime/build-vhdl-payload.sh
  # shellcheck source=versions.env
  source scripts/versions.env
  export VEDIT_QT6_BUILD_IMAGE="$QT_BUILD_IMAGE_TAG"
  export VEDIT_CENTOS_PROBE_IMAGE="docker.io/library/centos@$VEDIT_BASE_IMAGE_DIGEST"
  export VEDIT_PACK_EVIDENCE="$ROOT/${VEDIT_CI_LOG_DIR:?}/package"
  export VEDIT_F2_EVIDENCE="$VEDIT_PACK_EVIDENCE"
  export SOURCE_DATE_EPOCH
  SOURCE_DATE_EPOCH=$(git show -s --format=%ct HEAD)
  docker pull "$VEDIT_CENTOS_PROBE_IMAGE"
  df -h "$ROOT" "${RUNNER_TEMP:?}"
  bash packaging/runtime/build.sh
  df -h "$ROOT" "$RUNNER_TEMP"
  # shellcheck source=../build/slang/versions.env
  source build/slang/versions.env
  # shellcheck source=versions-vhdl.env
  source scripts/versions-vhdl.env
  # shellcheck source=versions-scala.env
  source scripts/versions-scala.env

  local release_version out appimage extracted sha256
  release_version=$(version)
  out="$ROOT/packaging/runtime/out"
  appimage="VEdit-$release_version-x86_64.AppImage"
  extracted="$out/centos7-appdir"

  # The production packer extracted these bytes directly from the AppImage.
  # Require the two product binaries to be the ones compiled in this run.
  cmp target/qt6-build/src/app/vedit-app "$extracted/bin/vd-app"
  cmp target/qt6-build/cmake/smoke/vedit-ksh-env-probe "$extracted/bin/vedit-ksh-env-probe"
  cmp target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6 \
    "$extracted/lib/libKF6SyntaxHighlighting.so.6"
  cmp target/qt6-build/src/app/translations/vedit_zh_CN.qm "$extracted/bin/translations/vedit_zh_CN.qm"
  cmp target/qt6-build/src/app/translations/qtbase_zh_CN.qm "$extracted/bin/translations/qtbase_zh_CN.qm"

  # Exercise the outer AppImage runtime and the extracted payload on the glibc
  # 2.17 userspace, with the original source fixtures on a read-only mount.
  docker run --rm --network none -v "$out/$appimage:/payload:ro" \
    "$VEDIT_CENTOS_PROBE_IMAGE" /payload --appimage-version
  local user_args=()
  if ! docker info --format '{{json .SecurityOptions}}' | grep '"name=rootless"' >/dev/null; then
    user_args=(--user "$(id -u):$(id -g)")
  fi
  docker run --rm --network none "${user_args[@]}" \
    -e HOME=/tmp -e LANG=C.UTF-8 -e QT_QPA_PLATFORM=offscreen \
    -v "$extracted:/app:ro" -v "$ROOT:/source:ro" \
    "$VEDIT_CENTOS_PROBE_IMAGE" bash -euc '
      run_version() {
        local want=$1 output
        shift
        output=$("$@" 2>&1)
        printf "%s\n" "$output"
        [[ "$output" == *"$want"* ]]
      }
      test "$(getconf GNU_LIBC_VERSION)" = "glibc 2.17"
      test "$(/app/AppRun --version)" = "vedit-app $1"
      /app/AppRun --selfcheck-ffi /source/crates/vd-ffi/tests/fixtures/project
      run_version "slang version $2" /app/bin/slang --version
      run_version "slang-server version $3" /app/bin/slang-server --version
      run_version "built at $4" /app/bin/verible-verilog-ls --version
      test "$(/app/bin/git --version)" = "git version $5"
      test "$(/app/bin/svn --version --quiet)" = "$6"
      run_version "diff (GNU diffutils) $7" /app/usr/lib/vedit/file-compare/diff --version
      run_version "vhdl_ls $8" /app/bin/vhdl_ls --version
      run_version "GHDL $9" /app/bin/ghdl --version
      run_version "${10}" /app/bin/vsg --version
      run_version "${11}" /app/bin/scala-cli version --offline
      run_version "${12}" /app/bin/java -version
      timeout 300 /app/bin/scala-example spinalhdl
      timeout 300 /app/bin/scala-example chisel
    ' _ "$release_version" "$SLANG_VERSION" "$SLANG_SERVER_VERSION" "$VERIBLE_VERSION" \
      "$VCS_GIT_VERSION" "$VCS_SVN_VERSION" "$DIFFUTILS_VERSION" \
      "$VHDL_LS_VERSION" "$GHDL_VERSION" "$VSG_VERSION" "$SCALA_CLI_VERSION" "${SCALA_JDK_VERSION%_*}" \
      2>&1 | tee "$VEDIT_PACK_EVIDENCE/80-delivered-tools.log"

  (
    cd "$out"
    sha256sum --check --strict SHA256SUMS
  )
  sha256=$(sha256sum "$out/$appimage" | cut -d' ' -f1)
  printf 'appimage=%s\nsha256=%s\n' "$appimage" "$sha256" >> "${GITHUB_OUTPUT:?}"
}

case "${1:-}" in
  version) version ;;
  rust-gates) rust_gates ;;
  qt) qt_gates ;;
  f2) f2_inputs ;;
  package) package ;;
  *) echo 'Usage: bash scripts/ci-qt6.sh {version|rust-gates|qt|f2|package}' >&2; exit 2 ;;
esac
