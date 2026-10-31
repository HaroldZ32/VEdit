#!/usr/bin/env bash
# Deterministically generate the file-tree test dataset `filetree-testdata/`.
#
# Why this exists: the file-tree lane's laziness fixture (10,000 zero-byte
# `big/module_NNNNN.v` files) used to be produced only as a side effect of
# running `vedit-filetree-tests` from the repository root — its scratch
# fallback is `$PWD/filetree-testdata` (tests/filetree_tests.cpp,
# `scratchRoot()` / `main()`), which left 10,007 untracked files in the worktree.
# That dataset is generated data, so `.gitignore` ignores it and this script
# reproduces it instead of committing it.
#
# Both lane entries still auto-generate their own data and never skip on a
# missing dataset: `vedit-filetree-tests` wipes its scratch and rebuilds it on
# every run (`testLaziness()` creates the 10k folder), and
# `vedit-filetree-demo --prepare <dir> --big-count 10000` builds the screenshot
# fixture. This script reproduces the same `filetree-testdata/` layout without a
# Qt toolchain, so the dataset can be recreated on any machine.
#
#   bash src/workspace/filetree/tools/generate-testdata.sh [--force] [DEST]
#
# DEST defaults to `$PWD/filetree-testdata`. The output is deterministic: run it
# twice and compare the two manifest hashes it prints (`structure_manifest_sha256`
# over the directory/file listing, `content_manifest_sha256` over every file's
# sha256 and path); both are identical for identical trees.
#
# Nothing here is random and nothing depends on the host: same file names, same
# contents (all zero bytes except `plain.txt`, which holds `x`), same directories,
# including the intentionally empty ones (`empty/`, `locked/`, `tree/zeta/`,
# `ops/rtl/`).
set -euo pipefail

usage() {
  cat <<'EOF'
usage: generate-testdata.sh [--force] [DEST]

  DEST      directory to create (default: $PWD/filetree-testdata)
  --force   clear DEST first when it already exists and is not empty
EOF
}

FORCE=0
DEST=""
while [ $# -gt 0 ]; do
  case $1 in
    --force)
      FORCE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      echo "generate-testdata: unknown option '$1'" >&2
      usage >&2
      exit 2
      ;;
    *)
      DEST=$1
      shift
      ;;
  esac
done
[ -n "$DEST" ] || DEST="$PWD/filetree-testdata"
case $DEST in
  /|/..|.|..)
    echo "generate-testdata: refusing to use '$DEST' as the dataset directory" >&2
    exit 2
    ;;
esac

if [ -e "$DEST" ] && [ ! -d "$DEST" ]; then
  echo "generate-testdata: '$DEST' exists and is not a directory" >&2
  exit 2
fi
if [ -d "$DEST" ] && [ -n "$(ls -A -- "$DEST" 2>/dev/null)" ]; then
  if [ "$FORCE" -ne 1 ]; then
    echo "generate-testdata: '$DEST' is not empty; pass --force to replace it" >&2
    exit 2
  fi
  rm -rf -- "$DEST"
fi

mkdir -p -- \
  "$DEST/big/aaa_child" \
  "$DEST/big/bbb_child" \
  "$DEST/tree/alpha/deep" \
  "$DEST/tree/beta" \
  "$DEST/tree/zeta" \
  "$DEST/ops/rtl" \
  "$DEST/ops/locked" \
  "$DEST/empty" \
  "$DEST/locked"

# The only non-empty file: `plain.txt` holds a single `x` (state.fixture-file
# opens a regular file as the tree root).
printf 'x' > "$DEST/plain.txt"
: > "$DEST/tree/top.v"
: > "$DEST/tree/alpha/a1.v"
: > "$DEST/tree/alpha/deep/d1.sv"
: > "$DEST/tree/beta/b1.f"
: > "$DEST/ops/external.v"
: > "$DEST/ops/locked/keep.v"

# 10,000 zero-byte sources: the laziness fixture (same naming rule as
# testLaziness(): module_%05d.v, 00000 .. 09999).
for i in $(seq 0 9999); do
  printf -v name '%05d' "$i"
  : > "$DEST/big/module_$name.v"
done

# Deterministic anchors, printed for the evidence that pins this generator.
# Directory sizes are filesystem metadata (they change with allocation history),
# so they are normalised to "-": the manifest pins names, kinds and file sizes.
structure_manifest() {
  ( cd -- "$1" && find . -mindepth 1 -printf '%P\t%y\t%s\n' | LC_ALL=C sort ) \
    | awk -F'\t' 'BEGIN { OFS = "\t" } { if ($2 == "d") print $1, $2, "-"; else print }'
}

content_manifest() {
  ( cd -- "$1" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum ) \
    | sed 's| \./| |'
}

files=$(find "$DEST" -type f | wc -l)
dirs=$(find "$DEST" -mindepth 1 -type d | wc -l)
bytes=$(find "$DEST" -type f -printf '%s\n' | awk '{ total += $1 } END { print total + 0 }')

printf 'generated %s\n' "$DEST"
printf 'files=%s dirs=%s file_bytes_total=%s\n' "$files" "$dirs" "$bytes"
printf 'structure_manifest_sha256=%s\n' "$(structure_manifest "$DEST" | sha256sum | awk '{print $1}')"
printf 'content_manifest_sha256=%s\n' "$(content_manifest "$DEST" | sha256sum | awk '{print $1}')"
