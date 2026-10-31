#!/usr/bin/env bash
# T59 / C17 v2 — re-run the C17 overlay probe on the OFFICIAL build image
# (T10's `vedit-qt6-build:local`, built by scripts/build-qt.sh from the pinned
#  scripts/docker/Dockerfile.build stage).
#
# v1 lives in evidence/r2-c17/ (one directory up) and is NEVER written by this
# script: every artifact goes to evidence/r2-c17/v2/. The pre/post sha256 of the
# v1 artefacts (17-v1-untouched-*.txt) proves it.
#
# Reproducibility anchor is NOT the image id (T10 measured three ids for three
# builds of the same Dockerfile): it is
#   scripts/versions-qt.env + scripts/docker/qt6-locked-packages.txt
#   (22 exact .deb + sha256, combined fingerprint c97a2566...)
# plus the M0-SMOKE output. Both are captured here.
#
# Nothing here modifies the probe: probes/c17-overlay/scripts/run-probe.sh is
# hashed before and after (16-run-probe-sha.txt). The probe BUILD TREE is
# removed first so the probe is compiled inside the official image instead of
# reusing v1's vprobe-image objects (the v1 binary hash is recorded first).
#
#   bash evidence/r2-c17/v2/run-v2.sh
set -uo pipefail

OUT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$OUT/../../.." && pwd)
IMG="${IMG:-vedit-qt6-build:local}"
V1="$ROOT/evidence/r2-c17"
mkdir -p "$OUT"

v1_list() {
  find "$V1" -maxdepth 1 -type f -printf '%f\n' | sort | while read -r f; do
    sha256sum "$V1/$f"
  done
}

echo "[v2] root=$ROOT"
echo "[v2] image=$IMG"
# The tag is shared with the concurrent T10 lane, which re-points it on every
# rebuild (observed during this session: de2b7f70 -> cf2ca614 -> 2548f11d ->
# ba08930b, all the same created timestamp). Resolve ONCE and run every step
# against that id, so the pin assertions and the probe cannot see different
# images. The id is still only a locator: content identity is the lock-file
# assertion (12-...) plus the RootFS layer fingerprint (11-...).
IMG_ID=$(docker image inspect "$IMG" --format '{{.Id}}' 2>/dev/null)
if [ -z "$IMG_ID" ]; then
  echo "FATAL: image $IMG not found" >&2
  exit 1
fi
echo "[v2] image resolved: $IMG -> $IMG_ID"
echo "[v2] output=$OUT"
echo "[v2] host load: $(uptime)"

# --- 0. v1 must stay byte-identical (proved again at the end) ---------------
v1_list >"$OUT/17-v1-untouched-before.txt"

# --- 1. host-side pin anchor ------------------------------------------------
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "git HEAD: $(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "host: $(uname -srm) / nproc=$(nproc)"
  echo "host load at start: $(uptime)"
  echo "== sha256 of the pinned inputs =="
  sha256sum "$ROOT/scripts/versions-qt.env" \
    "$ROOT/scripts/docker/qt6-locked-packages.txt" \
    "$ROOT/scripts/docker/Dockerfile.build"
  echo
  echo "== combined lock fingerprint = sha256(versions-qt.env + qt6-locked-packages.txt + Dockerfile.build) =="
  cat "$ROOT/scripts/versions-qt.env" \
    "$ROOT/scripts/docker/qt6-locked-packages.txt" \
    "$ROOT/scripts/docker/Dockerfile.build" | sha256sum
  echo
  echo "== locked .deb entries (expect 22) =="
  grep -v '^#' "$ROOT/scripts/docker/qt6-locked-packages.txt" | grep -v '^$' | wc -l
} >"$OUT/10-pins-anchor.txt" 2>&1
echo "[v2] pins anchor -> 10-pins-anchor.txt (exit=$?)"

# --- 2. image locator (a LOCATOR, never the criterion) ----------------------
{
  echo "tag: $IMG"
  echo "resolved id for THIS run (used by every docker run step below): $IMG_ID"
  echo
  echo "NOTE: the id only says WHICH local image was used. It is NOT the"
  echo "      reproducibility criterion. T10's own persisted evidence file"
  echo "      evidence/m0-build/build-logs/21-qt6-image-id.txt records"
  echo "      de2b7f70... for this very tag (written 11:39); this run resolved"
  echo "      a DIFFERENT id at 11:52 -- same tag, same created=..., different id."
  echo "      (Two further transient ids were seen in the terminal: cf2ca614,"
  echo "      2548f11d; not persisted separately.) Content identity is asserted"
  echo "      against the lock file in 12-image-pin-assertions.txt, and by the"
  echo "      layer fingerprint below."
  echo
  echo "== content fingerprint = sha256(json RootFS.Layers) =="
  echo "   (layer diff_ids are content-addressed: equal fingerprint = identical filesystem)"
  docker image inspect "$IMG_ID" --format '{{json .RootFS.Layers}}' | sha256sum
  if docker image inspect vedit-qt6-build:t10-evidence >/dev/null 2>&1; then
    echo "== same fingerprint for vedit-qt6-build:t10-evidence (T10's own evidence tag) =="
    docker image inspect vedit-qt6-build:t10-evidence --format '{{json .RootFS.Layers}}' | sha256sum
  else
    echo "== vedit-qt6-build:t10-evidence absent =="
  fi
  echo
  echo "== created =="
  docker image inspect "$IMG_ID" --format 'created={{.Created}}'
} >"$OUT/11-image-locator.txt" 2>&1
echo "[v2] image locator -> 11-image-locator.txt"

# --- 2c. what image actually ran, and why 00-pins.txt says otherwise -------
# scripts/run-probe.sh is UNMODIFIED (its sha256 is recorded in
# 16-run-probe-sha.txt) and it prints a hardcoded v1 image label at lines 23/27
# into 00-pins.txt. That label is stale for v2; this file is authoritative.
{
  echo "actual image for this run: $IMG"
  echo "resolved id (LOCATOR only, never the criterion): $IMG_ID"
  echo
  echo "runner: probes/c17-overlay/scripts/run-probe.sh (UNMODIFIED)"
  sha256sum "$ROOT/probes/c17-overlay/scripts/run-probe.sh"
  echo
  echo "why 00-pins.txt contains 'image: vprobe-qt6kf6:1':"
  grep -n 'vprobe' "$ROOT/probes/c17-overlay/scripts/run-probe.sh" | sed 's/^/  /'
  echo "  -> hardcoded echo lines in the runner, left untouched by design;"
  echo "     authoritative identity is THIS file + 12-image-pin-assertions.txt"
  echo "     (22 locked packages asserted against the lock file inside the image)."
} >"$OUT/00-image-identity.txt" 2>&1
echo "[v2] image identity -> 00-image-identity.txt"

# --- 2b. record v1's probe build tree, then drop it (forces a rebuild here) -
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "== probe binary present at the START of this run (removed right below) =="
  sha256sum "$ROOT/probes/c17-overlay/build/c17-overlay" 2>/dev/null || echo "no binary"
  echo "== build tree file count =="
  find "$ROOT/probes/c17-overlay/build" -type f 2>/dev/null | wc -l
  echo "NOTE: this tree is a build artefact (not evidence). It is removed so each"
  echo "      run compiles the probe inside the official image; 01-build.log must"
  echo "      show '[1/2] Building CXX object' + '[2/2] Linking'."
  echo "NOTE (honesty): on the FIRST v2 execution this was v1's vprobe-image"
  echo "      binary; later re-runs of this script regenerated this file, so the"
  echo "      hash above belongs to the previous v2 build (official image)."
} >"$OUT/18-v1-probe-build-manifest.txt" 2>&1
rm -rf "$ROOT/probes/c17-overlay/build"

# --- 3. in-container pin assertions ----------------------------------------
docker run --rm -v "$ROOT":/work -w /work "$IMG_ID" \
  bash /work/evidence/r2-c17/v2/scripts/in-container-pins.sh \
  >"$OUT/12-image-pin-assertions.txt" 2>&1
rc=$?
echo "[v2] in-container pins exit=$rc -> 12-image-pin-assertions.txt"

# --- 3b. negative API verification on THIS image ---------------------------
docker run --rm -v "$ROOT":/work -w /work "$IMG_ID" \
  bash /work/evidence/r2-c17/v2/scripts/in-container-negative-api.sh \
  >"$OUT/19-negative-api.txt" 2>&1
rc_neg=$?
echo "[v2] negative API check exit=$rc_neg -> 19-negative-api.txt"

# --- 4. M0 smoke re-run inside the same image -------------------------------
# Mount at /vedit-build: the smoke binary resolves its custom-syntax-path
# fixture against its configured workspace root.
docker run --rm -v "$ROOT":/vedit-build -w /vedit-build -e HOME=/tmp "$IMG_ID" \
  bash /vedit-build/evidence/r2-c17/v2/scripts/in-container-smoke.sh \
  >"$OUT/13-m0-smoke.log" 2>&1
rc_smoke=$?
echo "[v2] m0 smoke exit=$rc_smoke -> 13-m0-smoke.log"

# --- 5. the probe, via the SAME unmodified scripts/run-probe.sh as v1 -------
sha256sum "$ROOT/probes/c17-overlay/scripts/run-probe.sh" >"$OUT/16-run-probe-sha.txt"
docker run --rm -e HOME=/tmp -v "$ROOT":/work -w /work/probes/c17-overlay "$IMG_ID" \
  bash scripts/run-probe.sh /work/evidence/r2-c17/v2 >"$OUT/14-console.log" 2>&1
rc_probe=$?
echo "probe exit=$rc_probe" >"$OUT/15-probe-exit.txt"
sha256sum "$ROOT/probes/c17-overlay/scripts/run-probe.sh" >>"$OUT/16-run-probe-sha.txt"
echo "[v2] probe exit=$rc_probe -> 14-console.log / 15-probe-exit.txt"

# --- 5b. second run on the SAME image: run-to-run noise band ---------------
# Same script, same image, back-to-back. Without this band a v1->v2 difference
# could not be attributed to the image rather than to host load.
echo "[v2] host load before noise rerun: $(uptime)"
docker run --rm -e HOME=/tmp -v "$ROOT":/work -w /work/probes/c17-overlay "$IMG_ID" \
  bash scripts/run-probe.sh /work/evidence/r2-c17/v2/rerun >"$OUT/14b-console-rerun.log" 2>&1
rc_rerun=$?
echo "rerun exit=$rc_rerun" >"$OUT/15b-probe-exit-rerun.txt"
echo "[v2] noise rerun exit=$rc_rerun -> rerun/02-run.log, rerun/02b-run-verilog.log"

# --- 6. v1 still untouched --------------------------------------------------
v1_list >"$OUT/17-v1-untouched-after.txt"
if diff -q "$OUT/17-v1-untouched-before.txt" "$OUT/17-v1-untouched-after.txt" >/dev/null; then
  echo "v1-untouched: YES (all top-level files byte-identical)" >"$OUT/17-v1-untouched-verdict.txt"
else
  echo "v1-untouched: NO -- DIFF FOLLOWS" | tee "$OUT/17-v1-untouched-verdict.txt"
  diff "$OUT/17-v1-untouched-before.txt" "$OUT/17-v1-untouched-after.txt" | tee -a "$OUT/17-v1-untouched-verdict.txt"
fi

echo "[v2] summary: pins exit=$rc, negative-api exit=$rc_neg, smoke exit=$rc_smoke, probe exit=$rc_probe, rerun exit=$rc_rerun"
echo "[v2] v1 verdict: $(cat "$OUT/17-v1-untouched-verdict.txt")"
echo "[v2] host load after: $(uptime)"
