#!/usr/bin/env bash
# Runs the real packaging entry point in disposable checkouts with fixture packages.
set -euo pipefail

probe_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
root=$(cd -- "$probe_dir/../../.." && pwd -P)
source_script=${2:-"$root/packaging/runtime/build.sh"}
mode=${1:-fixed}
real_mv=$(command -v mv)
export real_mv

scratch=$(mktemp -d "$probe_dir/.release-swap.XXXXXX")
cleanup() {
  case "$(realpath -- "$scratch")" in
    "$probe_dir"/.release-swap.??????) rm -rf -- "$scratch" ;;
    *) return 1 ;;
  esac
}
trap cleanup EXIT

setup() {
  fixture="$scratch/$1"
  out="$fixture/packaging/runtime/out"
  mkdir -p "$fixture"/{bin,scripts,packaging/runtime/verify,packaging/appimage/vendor,resources/syntax}
  cp -- "$source_script" "$fixture/packaging/runtime/build.sh"
  printf 'version = "0.4.1"\n' > "$fixture/Cargo.toml"
  printf 'DIFFUTILS_SOURCE_RPM=fixture.rpm\n' > "$fixture/scripts/versions.env"
  local file
  local files=(
    target/qt6-build/src/app/vedit-app target/qt6-build/cmake/smoke/vedit-ksh-env-probe
    target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0
    .mpd/recon/verible/bin/verible-verilog-ls build/slang/out/slang
    packaging/appimage/vendor/appimagetool-x86_64 resources/syntax/fixture.xml
  )
  local appdir_files=(
    bin/{vd-app,verible-verilog-ls,slang} plugins/platforms/{libqxcb.so,libqoffscreen.so}
    plugins/kf6/parts/katepart.so lib/libKF6SyntaxHighlighting.so.6
    usr/lib/vedit/file-compare/{diff,diff.bin} usr/share/doc/vedit/diffutils/{GPL-3.0.txt,fixture.rpm}
    usr/share/org.kde.syntax-highlighting/syntax/vedit-verilog.xml usr/share/vedit/ksh-whitelist.txt
  )
  for file in "${files[@]/#/$fixture/}" "${appdir_files[@]/#/$out/AppDir/}"; do
    mkdir -p -- "$(dirname -- "$file")"
    install -m 755 /dev/null "$file"
  done
  mkdir -p "$out/release"
  suite "$out/release" old
  declare -f suite > "$fixture/packaging/runtime/build-release-packages.sh"
  printf '%s\n' 'suite "$2" "${BUILD_ID}"' >> "$fixture/packaging/runtime/build-release-packages.sh"
  printf '%s\n' 'mkdir -p -- "$4"' '[[ $FAULT != gate-failure ]] || exit 29' > "$fixture/packaging/runtime/verify/verify-release-packages.sh"
  for file in verify-not-shipped.sh selfcheck.sh verify-f2-isolation.sh; do
    printf 'exit 0\n' > "$fixture/packaging/runtime/$file"
  done
  cat > "$fixture/bin/docker" <<'STUB'
#!/usr/bin/env bash
if [[ $1 == image ]]; then
  printf '%s\n' "$BUILD_ID" >> "$PWD/preflight-events"
fi
STUB
  cat > "$fixture/bin/mv" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
release="$PWD/packaging/runtime/out/release"
if [[ ${3:-} == "$release" ]]; then
  "$real_mv" "$@"
  case "$FAULT" in
    term) kill -TERM "$PPID" ;;
    kill) kill -KILL "$PPID" ;;
    pause)
      printf 'paused\n' > "$PWD/publish-paused"
      read -r -t 12 -u "$RESUME_FD"
      ;;
  esac
elif [[ ${3:-} == "${release%/*}/.release.previous" && $FAULT == rollback-failure ]]; then
  exit 74
elif [[ ${3:-} == "${release%/*}"/.release.*/release && ${4:-} == "$release" ]]; then
  case "$FAULT" in
    move-failure|rollback-failure) exit 73 ;;
    kill-after-publish)
      "$real_mv" "$@"
      kill -KILL "$PPID"
      ;;
    *) exec "$real_mv" "$@" ;;
  esac
else
  exec "$real_mv" "$@"
fi
STUB
  chmod +x "$fixture/bin/"{docker,mv}
}

suite() (
  cd -- "$1" || exit
  printf '%s\n' "$2" | tee VEdit-0.4.1-x86_64.AppImage VEdit-0.4.1-x86_64.run > vedit-0.4.1-linux-x86_64.tar.gz
  sha256sum -- VEdit-0.4.1-x86_64.AppImage VEdit-0.4.1-x86_64.run vedit-0.4.1-linux-x86_64.tar.gz > SHA256SUMS
)

invoke() {
  PATH="$fixture/bin:$PATH" BUILD_ID="$1" FAULT="$2" VEDIT_PACK_EVIDENCE="$fixture/evidence-$1" \
    bash "$fixture/packaging/runtime/build.sh" --no-centos-probe > "$fixture/$1.log" 2>&1
}

expect_exit() {
  local rc=0
  invoke "$2" "$3" || rc=$?
  printf '%s exit=%d\n' "$2" "$rc"
  if [[ $rc != "$1" ]]; then
    cat -- "$fixture/$2.log" >&2
    return 1
  fi
}

check_suite() (
  cd -- "$1"
  sha256sum --check --strict --status SHA256SUMS
  local file
  for file in VEdit-0.4.1-x86_64.AppImage VEdit-0.4.1-x86_64.run vedit-0.4.1-linux-x86_64.tar.gz; do
    [[ $(<"$file") == "$2" ]]
  done
  printf '%s suite=%s checksums=OK\n' "${1##*/}" "$2"
)

setup term
expect_exit 143 interrupted term
if [[ $mode == baseline ]]; then
  [[ ! -e $out/release && -z $(compgen -G "$out/.release.*" || true) ]]
  printf 'baseline TERM removed both the public release and the staging directory\n'
  exit 0
fi
check_suite "$out/release" old
grep -F 'Restored previous release' "$fixture/interrupted.log"
expect_exit 0 retry none
check_suite "$out/release" retry

setup failure
expect_exit 29 rejected gate-failure
check_suite "$out/release" old
expect_exit 73 rename-failed move-failure
check_suite "$out/release" old
expect_exit 0 published none
check_suite "$out/release" published
[[ ! -e $out/.release.previous ]]

setup rollback-failure
expect_exit 73 rollback-failed rollback-failure
[[ ! -e $out/release ]]
check_suite "$out/.release.previous" old
grep -F 'ERROR: previous release retained in' "$fixture/rollback-failed.log"
expect_exit 29 retry-rejected gate-failure
check_suite "$out/release" old

setup kill
expect_exit 137 killed kill
[[ ! -e $out/release ]]
check_suite "$out/.release.previous" old
expect_exit 29 retry-rejected gate-failure
check_suite "$out/release" old
grep -F 'Restored previous release' "$fixture/retry-rejected.log"
expect_exit 0 retry none
check_suite "$out/release" retry

setup kill-after-publish
expect_exit 137 published-before-kill kill-after-publish
check_suite "$out/release" published-before-kill
check_suite "$out/.release.previous" old
expect_exit 29 retry-rejected gate-failure
check_suite "$out/release" published-before-kill
[[ ! -e $out/.release.previous ]]

setup concurrent
mkfifo "$fixture/resume"
exec {RESUME_FD}<>"$fixture/resume"
export RESUME_FD
invoke first pause &
first=$!
for (( attempt=0; attempt<200; attempt++ )); do
  [[ ! -s $fixture/publish-paused ]] || break
  sleep .02
done
[[ -s $fixture/publish-paused ]]
expect_exit 1 contender none
[[ $(<"$fixture/preflight-events") == first ]]
check_suite "$out/.release.previous" old
printf 'resume\n' >&"$RESUME_FD"
wait "$first"
check_suite "$out/release" first
expect_exit 0 contender none
check_suite "$out/release" contender

setup empty
[[ $(realpath -- "$out/release") == "$scratch/empty/packaging/runtime/out/release" ]]
rm -r -- "$out/release"
expect_exit 0 first-publish none
check_suite "$out/release" first-publish
printf 'release swap probe passed\n'
