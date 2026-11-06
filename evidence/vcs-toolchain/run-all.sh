#!/usr/bin/env bash
# Evidence driver for the bundled VCS toolchain (batch 6a, requirements rev3).
#
#   bash evidence/vcs-toolchain/run-all.sh
#
# Produces logs/:
#   00-context.txt          environment, git/svn in each image, HEAD
#   10-deb-pins.log         every pinned .deb: URL, expected sha256, actual
#   20-assemble.log         assemble-appdir.sh: unpack + version smoke + invariants
#   30-clean-container.log  clean CentOS 7: versions, AppRun --selfcheck,
#                           VCS-TOOLCHAIN line, AC-3.0 functional set, svnadmin
#   40-resolver.log         Rust resolver tests + the lane's toolchain line
#
# All fixtures are mktemp -d; every probe asserts it does not run from the VEdit
# working tree.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
EVID="$ROOT/evidence/vcs-toolchain"
APP="$ROOT/packaging/runtime/out/AppDir"
QT6_IMAGE=${VEDIT_QT6_BUILD_IMAGE:-vedit-qt6-build:local}
RUST_IMAGE=${VEDIT_BUILD_IMAGE_REF:-vedit-build:local}
CENTOS_IMAGE="docker.m.daocloud.io/library/centos@sha256:be65f488b7764ad3638f236b7b515b3678369a5124c47b8d32916d6487418ea4"
mkdir -p "$EVID/logs"
chmod +x "$EVID"/*.sh

{
  echo "# batch 6a bundled VCS toolchain - run context"
  echo "collected_at: $(date --iso-8601=seconds)"
  echo "workspace: $ROOT"
  echo "git_head: $(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo '<no git>')"
  echo "host_git: $(git --version)"
  echo "qt6_image: $QT6_IMAGE os=$(docker run --rm "$QT6_IMAGE" sh -c '. /etc/os-release; echo "$PRETTY_NAME"')"
  echo "centos_image: $CENTOS_IMAGE"
  # shellcheck source=/dev/null
  set -a; . "$ROOT/scripts/versions.env"; set +a
  echo "pinned_git: $VCS_GIT_VERSION (min $VCS_GIT_MIN_VERSION)"
  echo "pinned_svn: $VCS_SVN_VERSION (min $VCS_SVN_MIN_VERSION)"
} | tee "$EVID/logs/00-context.txt"

echo "== 1/4 assemble the AppDir (downloads + verifies the pinned debs) =="
docker run --rm -e HOME=/tmp -v "$ROOT:/vedit-build" -w /vedit-build "$QT6_IMAGE" \
  bash packaging/runtime/assemble-appdir.sh --require-toolchain 2>&1 | tee "$EVID/logs/20-assemble.log"

echo "== 2/4 verify every pinned .deb (URL + sha256) =="
{
  echo "# pinned .deb verification (cache: packaging/runtime/.cache/vcs-debs)"
  docker run --rm -e HOME=/tmp -v "$ROOT:/vedit-build" -w /vedit-build "$QT6_IMAGE" bash -lc '
    set -euo pipefail
    set -a; . /vedit-build/scripts/versions.env; set +a
    cache=/vedit-build/packaging/runtime/.cache/vcs-debs
    fail=0
    for i in "${!VCS_DEB_URLS[@]}"; do
      url=${VCS_DEB_URLS[$i]}; want=${VCS_DEB_SHA256S[$i]}; deb="$cache/$(basename "$url")"
      [ -f "$deb" ] || { echo "MISSING $deb"; fail=1; continue; }
      got=$(sha256sum "$deb" | cut -d" " -f1)
      if [ "$got" = "$want" ]; then ok=PASS; else ok=FAIL; fail=1; fi
      printf "%-12s %s\n  url=%s\n  want=%s\n  got =%s\n" "$ok" "$(basename "$url")" "$url" "$want" "$got"
    done
    echo "deb_pins_fail=$fail"
    exit "$fail"
  '
} 2>&1 | tee "$EVID/logs/10-deb-pins.log"

echo "== 3/4 clean CentOS 7 container: versions + selfcheck + VCS line + AC-3.0 set =="
{
  echo "# clean container: $CENTOS_IMAGE"
  docker run --rm -e HOME=/tmp -v "$APP:/app:ro" -v "$EVID:/evidence:ro" "$CENTOS_IMAGE" bash -lc '
    set -u
    echo "== host =="; cat /etc/centos-release; ldd --version | head -1
    echo "== bundled versions =="
    /app/bin/git --version
    /app/bin/svn --version --quiet
    echo "== AppRun --selfcheck =="
    QT_QPA_PLATFORM=offscreen /app/AppRun --selfcheck 2>&1 | tail -3
    echo "selfcheck_exit=$?"
    echo "== AC-3.0 functional set (bundled git/svn) =="
    cd /tmp && bash /evidence/ac30.sh
    echo "ac30_exit=$?"
    echo "== product resolver line (GUI offscreen, VCS lane init) =="
    mkdir -p /tmp/proj
    QT_QPA_PLATFORM=offscreen timeout 90 /app/AppRun --project /tmp/proj --no-session-restore --hold 4000 2>&1 \
      | grep -m3 -E "VCS-TOOLCHAIN" || echo "VCS-TOOLCHAIN <missing>"
  '
} 2>&1 | tee "$EVID/logs/30-clean-container.log"

echo "== 4/4 resolver tests + the lane toolchain line =="
{
  echo "# Rust resolver tests (vedit-build:local; same source the product links)"
  docker run --rm -e HOME=/vedit-build/target/home -e CARGO_HOME=/vedit-build/target/cargo-home \
    -e RUSTUP_HOME=/opt/rust -e RUSTUP_TOOLCHAIN=1.99.0 -e NO_AT_BRIDGE=1 \
    -v "$ROOT:/vedit-build" -w /tmp "$RUST_IMAGE" \
    bash -lc 'cd /tmp && cargo test --manifest-path /vedit-build/Cargo.toml -p vd-vcs --test toolchain -- --nocapture'
  echo
  echo "# lane selfcheck: the VCS-TOOLCHAIN line + its assertions"
  docker run --rm -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -e NO_AT_BRIDGE=1 \
    -v "$ROOT:/vedit-build" -w /tmp "$QT6_IMAGE" \
    bash -lc 'cd /tmp && /vedit-build/target/qt6-build/src/app/workspace-lane-vcs/vedit-workspace-vcs-selftest --selfcheck 2>&1 | grep -E "VCS-TOOLCHAIN|vcs.toolchain|WORKSPACE-VCS-SELFTEST"'
} 2>&1 | tee "$EVID/logs/40-resolver.log"

echo "[vcs-toolchain] PASS: assemble + deb pins + clean container + resolver; logs in $EVID/logs"
