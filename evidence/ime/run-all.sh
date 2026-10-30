#!/usr/bin/env bash
# Host entry for the AC-16 probes: build the IME image, compile the probes in the
# pinned image, then run the three tiers offline inside the IME image.
#
#   bash evidence/ime/run-all.sh            # build image + probes, then run
#   SKIP_IMAGE_BUILD=1 bash evidence/ime/run-all.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IME="$ROOT/evidence/ime"
mkdir -p "$IME/logs" "$IME/out/bin" "$ROOT/target/docker-config"
export DOCKER_CONFIG="$ROOT/target/docker-config"

if [ "${SKIP_IMAGE_BUILD:-0}" != "1" ]; then
  echo "== building the IME image =="
  DOCKER_BUILDKIT=0 docker build -t vedit-ime:local -f "$IME/docker/Dockerfile.ime" "$IME/docker" \
    > "$IME/logs/01-image-build.log" 2>&1
  tail -1 "$IME/logs/01-image-build.log"
fi

echo "== compiling the probes in the pinned build image =="
bash "$IME/build-probes.sh" > "$IME/logs/02-probe-build.log" 2>&1
tail -2 "$IME/logs/02-probe-build.log"

echo "== running the tiers offline =="
docker run --rm --network none \
  -v "$ROOT/packaging/tarball/out:/art:ro" \
  -v "$ROOT:/work" \
  -v "$IME/out/bin:/probes:ro" \
  vedit-ime:local sh /work/evidence/ime/run.sh 2>&1 | tee "$IME/logs/90-run.log"

grep -E "^RESULT " "$IME/logs/99-results.txt" || true
