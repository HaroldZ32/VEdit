#!/usr/bin/env bash
# Fetch the pinned Verible binaries so a CLEAN checkout can run the vd-lsp
# integration tests (`cargo test --workspace`) and the app's lsp/slang selftest
# suites. `.mpd/` is not in git (harness state), so without this step a fresh
# checkout has no `verible-verilog-ls` and vd-lsp's tests fail:
#   missing Verible binary at …/.mpd/recon/verible/bin/verible-verilog-ls
#
#   bash scripts/fetch-verible.sh
#
# What it does (idempotent; re-running is a no-op):
#   1. downloads VERIBLE_URL into target/verible-pin/ and verifies
#      VERIBLE_TARBALL_SHA256;
#   2. extracts it into target/verible-pin/extract/, keeping the
#      `verible-<version>/bin` layout that scripts/build-in-container.sh probes
#      (that probe is what upgrades `--selftest` to the full `all` run);
#   3. exposes that bin directory as `<repo>/.mpd/recon/verible/bin` (a relative
#      symlink) so crates/vd-lsp/tests/verible_ls.rs finds it without
#      VEDIT_VERIBLE_BIN_DIR — the same path works inside the pinned image,
#      because the whole repository is mounted at /vedit-build;
#   4. verifies the three binaries against the pinned per-binary sha256 values.
#
# On any download/checksum/extract failure it prints FATAL and exits 1.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# shellcheck source=versions.env
set -a
# shellcheck disable=SC1091
. "$ROOT/scripts/versions.env"
set +a
: "${VERIBLE_URL:?missing VERIBLE_URL pin}" "${VERIBLE_TARBALL_SHA256:?missing VERIBLE_TARBALL_SHA256 pin}"
: "${VERIBLE_LS_SHA256:?missing pin}" "${VERIBLE_LINT_SHA256:?missing pin}" "${VERIBLE_FORMAT_SHA256:?missing pin}"

PIN="$ROOT/target/verible-pin"
TARBALL="$PIN/verible.tar.gz"
EXTRACT="$PIN/extract"
RECON_BIN="$ROOT/.mpd/recon/verible/bin"

# The expected sha256 of one extracted binary.
pinned_sha() {
  case "$1" in
    verible-verilog-ls) printf '%s\n' "$VERIBLE_LS_SHA256" ;;
    verible-verilog-lint) printf '%s\n' "$VERIBLE_LINT_SHA256" ;;
    verible-verilog-format) printf '%s\n' "$VERIBLE_FORMAT_SHA256" ;;
    *) return 1 ;;
  esac
}

# True when `dir` holds the three pinned binaries with the expected bytes.
verify_binaries() {
  local dir="$1" name want
  for name in verible-verilog-ls verible-verilog-lint verible-verilog-format; do
    want=$(pinned_sha "$name") || return 1
    [ -x "$dir/$name" ] || return 1
    printf '%s  %s\n' "$want" "$dir/$name" | sha256sum -c - >/dev/null 2>&1 || return 1
  done
  return 0
}

# The `verible-<version>/bin` directory inside the extract dir, if present.
extracted_bin_dir() {
  local candidate
  candidate=$(compgen -G "$EXTRACT/verible-*/bin" | head -1 || true)
  if [ -n "$candidate" ]; then
    printf '%s\n' "${candidate%/}"
  else
    return 1
  fi
}

mkdir -p "$PIN" "$EXTRACT"

# 1) pinned tarball: reuse when it already matches, otherwise download.
if [ -f "$TARBALL" ] && printf '%s  %s\n' "$VERIBLE_TARBALL_SHA256" "$TARBALL" | sha256sum -c - >/dev/null 2>&1; then
  echo "[verible] reusing the verified tarball $TARBALL"
else
  echo "[verible] downloading $VERIBLE_URL"
  rm -f "$TARBALL.part"
  if ! curl -fsSL --retry 3 --connect-timeout 20 -o "$TARBALL.part" "$VERIBLE_URL"; then
    rm -f "$TARBALL.part"
    echo "FATAL: could not download $VERIBLE_URL" >&2
    exit 1
  fi
  mv "$TARBALL.part" "$TARBALL"
  if ! printf '%s  %s\n' "$VERIBLE_TARBALL_SHA256" "$TARBALL" | sha256sum -c -; then
    rm -f "$TARBALL"
    echo "FATAL: $TARBALL does not match VERIBLE_TARBALL_SHA256 ($VERIBLE_TARBALL_SHA256)" >&2
    exit 1
  fi
fi

# 2) extract, then verify the three binaries against their pinned sha256.
BIN_DIR=""
if BIN_DIR=$(extracted_bin_dir) && verify_binaries "$BIN_DIR"; then
  echo "[verible] reusing the verified binaries in $BIN_DIR"
else
  echo "[verible] extracting $TARBALL into $EXTRACT"
  rm -rf "$EXTRACT"
  mkdir -p "$EXTRACT"
  if ! tar -xzf "$TARBALL" -C "$EXTRACT"; then
    echo "FATAL: could not extract $TARBALL" >&2
    exit 1
  fi
  if ! BIN_DIR=$(extracted_bin_dir); then
    echo "FATAL: $TARBALL has no verible-*/bin directory" >&2
    exit 1
  fi
  if ! verify_binaries "$BIN_DIR"; then
    echo "FATAL: the extracted binaries in $BIN_DIR do not match the pinned sha256 values" >&2
    exit 1
  fi
fi

# 3) explicit, visible verification against the pinned per-binary sha256 values
# (the probes above are silent; this is the evidence trail of every run).
for name in verible-verilog-ls verible-verilog-lint verible-verilog-format; do
  want=$(pinned_sha "$name")
  if ! printf '%s  %s\n' "$want" "$BIN_DIR/$name" | sha256sum -c -; then
    echo "FATAL: $BIN_DIR/$name does not match the pinned sha256 ($want)" >&2
    exit 1
  fi
done

# 4) expose the bin dir where the vd-lsp tests look by default. The link is
# relative (no realpath dependency) so it works on the host and inside the
# pinned image, where the repository is mounted at /vedit-build.
case "$BIN_DIR" in
  "$ROOT"/*) LINK_TARGET="../../../${BIN_DIR#"$ROOT"/}" ;;
  *)
    echo "FATAL: the extracted bin dir $BIN_DIR is outside the repository $ROOT" >&2
    exit 1
    ;;
esac
mkdir -p "$(dirname "$RECON_BIN")"
if [ -L "$RECON_BIN" ]; then
  ln -sfn "$LINK_TARGET" "$RECON_BIN"
  echo "[verible] relinked $RECON_BIN -> $(readlink "$RECON_BIN")"
elif [ -e "$RECON_BIN" ]; then
  # Never delete harness state: keep an existing directory only when it already
  # holds exactly the pinned binaries.
  if [ -d "$RECON_BIN" ] && verify_binaries "$RECON_BIN"; then
    echo "[verible] keeping the existing verified directory $RECON_BIN"
  else
    echo "FATAL: $RECON_BIN exists but does not hold the pinned Verible binaries." >&2
    echo "       Move it away and re-run this script." >&2
    exit 1
  fi
else
  ln -s "$LINK_TARGET" "$RECON_BIN"
  echo "[verible] linked $RECON_BIN -> $(readlink "$RECON_BIN")"
fi

echo "[verible] binaries: $BIN_DIR"
echo "[verible] recon path: $RECON_BIN"
echo "[verible] ready (selftest can now use the lsp/slang suites; vd-lsp tests can run)"
