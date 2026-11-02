#!/usr/bin/env bash
# Build the SELF-BUILT KSyntaxHighlighting (task T60, plan B′) inside the pinned
# M0 Qt6 image, then verify it:
#   * G3 build configuration (CMakeCache: QRC_SYNTAX=OFF, NO_STANDARD_PATHS=ON),
#   * ABI with the distro KTextEditor 6.24.0 / katepart (symbols + real load),
#   * behaviour: empty built-in definition set, and a whitelisted Verilog
#     definition loaded through addCustomSearchPath that actually highlights.
#
# The G1/G2 library scans run on the HOST (they need python3-zstandard, which the
# pinned image deliberately does not add) and are part of
# evidence/m0-ksh-selfbuild/run-verify.sh; this script runs them too when the
# module is importable.
#
#   bash scripts/build-ksh-selfbuild.sh              # the selected form (KSH_FORM)
#   bash scripts/build-ksh-selfbuild.sh --form F1     # NO_STANDARD_PATHS=ON
#   bash scripts/build-ksh-selfbuild.sh --form F2     # NO_STANDARD_PATHS=OFF
#
# Forms (see evidence/m0-ksh-selfbuild/FORM-DECISION.md): F1/F2 differ ONLY in
# NO_STANDARD_PATHS; QRC_SYNTAX stays OFF in both. Each form builds into its own
# tree and writes its own evidence subdirectory, so the comparison evidence and
# the selected form coexist.
#
# Environment (all optional):
#   VEDIT_QT6_BUILD_IMAGE_REF / VEDIT_QT6_BUILD_IMAGE_PULL / VEDIT_SKIP_IMAGE_BUILD
#     are honoured exactly as in scripts/build-qt.sh (same pinned image).
set -euo pipefail

# shellcheck source=lib-qt.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib-qt.sh"
# shellcheck source=versions-ksh.env
set -a
. "$ROOT/scripts/versions-ksh.env"
set +a
load_qt_versions

form="$KSH_FORM"
while [ "$#" -gt 0 ]; do
  case "$1" in
    --form)
      [ "$#" -ge 2 ] || { echo "FATAL: --form needs a value (F1 or F2)" >&2; exit 2; }
      form="$2"
      shift 2
      ;;
    F1 | F2)
      form="$1"
      shift
      ;;
    -h | --help)
      sed -n '2,24p' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *)
      echo "FATAL: unknown argument: $1 (supported: --form F1|F2)" >&2
      exit 2
      ;;
  esac
done
case "$form" in
  F1) nosp="ON" ;;
  F2) nosp="OFF" ;;
  *) echo "FATAL: KSH_FORM/--form must be F1 or F2 (got '$form')" >&2; exit 2 ;;
esac
form_tag=$(printf '%s' "$form" | tr 'A-Z' 'a-z')
if [ "$form" = "$KSH_FORM" ]; then
  evid_sub="build-logs"
else
  evid_sub="build-logs-form-$form_tag"
fi
build_root="$KSH_BUILD_ROOT/$form"
EVID="$ROOT/$KSH_EVIDENCE_DIR/$evid_sub"
mkdir -p "$EVID" "$ROOT/$build_root"

echo "[ksh] self-build KSyntaxHighlighting ${KSH_VERSION} (tag ${KSH_TAG}, commit ${KSH_COMMIT:0:12})"
echo "[ksh] form ${form}: QRC_SYNTAX=${KSH_QRC_SYNTAX} NO_STANDARD_PATHS=${nosp} BUILD_TESTING=${KSH_BUILD_TESTING}"
echo "[ksh] build tree: ${build_root} · evidence: ${KSH_EVIDENCE_DIR}/${evid_sub}"
echo "[ksh] toolchain: the M0 Qt6 image ${QT_BUILD_IMAGE_TAG} (Qt ${VEDIT_QT6_VERSION}, ECM 6.24)"

build_qt6_image "$EVID/00-image-build.log"

run_in_qt6_container bash scripts/build-ksh-selfbuild-in-container.sh "$form"

# --- G1/G2: the library must contain no embedded definitions -----------------
LIB=$(find "$ROOT/$build_root/install" -name 'libKF6SyntaxHighlighting.so.6.*' -type f | head -1)
if [ -z "$LIB" ]; then
  echo "FATAL: the self-built library was not found under $build_root/install" >&2
  exit 1
fi
echo "[ksh] installed library: ${LIB#"$ROOT"/}"

if python3 -c 'import zstandard' >/dev/null 2>&1; then
  echo "[ksh] G1: scan-embedded-defs --expect-clean (self-built library)"
  set +e
  python3 "$ROOT/evidence/r2-ksh-embedded/scan-embedded-defs.py" --expect-clean "$LIB" \
    > "$EVID/50-g1-scan-selfbuilt.log" 2>&1
  g1=$?
  set -e
  cat "$EVID/50-g1-scan-selfbuilt.log"
  echo "[ksh] G1 self-built exit=$g1 (expect 0)"
  [ "$g1" -eq 0 ] || { echo "FATAL G1: the self-built library is not clean" >&2; exit 1; }

  echo "[ksh] G1 negative control: the SAME scan on the distro library must exit 1"
  DISTRO=/usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6
  set +e
  python3 "$ROOT/evidence/r2-ksh-embedded/scan-embedded-defs.py" --expect-clean "$DISTRO" \
    > "$EVID/51-g1-scan-distro.log" 2>&1
  neg=$?
  set -e
  tail -3 "$EVID/51-g1-scan-distro.log"
  echo "[ksh] G1 negative control exit=$neg (expect 1)"
  [ "$neg" -eq 1 ] || { echo "FATAL G1: the gate is no longer able to see the distro embedding" >&2; exit 1; }
else
  echo "[ksh] WARNING: python3-zstandard is unavailable on this host; G1/G2 were NOT run here (see evidence/m0-ksh-selfbuild/run-verify.sh)"
fi

echo "[ksh] PASS: build + G3 + ABI + behaviour checks inside ${QT_BUILD_IMAGE_TAG}"
echo "[ksh] evidence: ${KSH_EVIDENCE_DIR}/${evid_sub}/"
