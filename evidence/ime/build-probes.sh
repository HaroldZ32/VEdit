#!/usr/bin/env bash
# Compile the two AC-16 probes inside the pinned build image (it has the GTK 3.22
# development files; the target image deliberately does not). The binaries are
# dynamically linked and will run against the *packaged* GTK via LD_LIBRARY_PATH,
# so the compiled-against and the actually-used version are both 3.22.30.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IME="$ROOT/evidence/ime"
mkdir -p "$IME/out/bin" "$IME/logs"

source "$ROOT/scripts/lib.sh"
load_versions

mkdir -p "$ROOT/target/docker-config"
DOCKER_CONFIG="$ROOT/target/docker-config" docker run --rm \
  -v "$ROOT:/vedit-build" -w /vedit-build \
  -e HOME=/vedit-build/target/home -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust \
  vedit-build:local bash -lc '
    set -euo pipefail
    cd /vedit-build/evidence/ime
    pkgs=$(pkg-config --modversion gtk+-3.0)
    echo "gtk+-3.0 (dev, pinned image) = $pkgs" | tee logs/00-build.log
    gcc -std=gnu99 -O2 -Wall -Wextra -o out/bin/im-probe src/im-probe.c \
        $(pkg-config --cflags --libs gtk+-3.0) 2>&1 | tee -a logs/00-build.log
    gcc -std=gnu99 -O2 -Wall -Wextra -o out/bin/input-probe src/input-probe.c \
        $(pkg-config --cflags --libs gtk+-3.0) 2>&1 | tee -a logs/00-build.log
    { echo "built:"; file out/bin/im-probe out/bin/input-probe; } | tee -a logs/00-build.log
  '
