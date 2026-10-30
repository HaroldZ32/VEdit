#!/bin/sh
# VEdit self-extracting single-file package (POSIX sh + tar + gzip only).
#
# Usage:  sh VEdit-<ver>-x86_64.run [args...]   (or chmod +x and run it)
#         ./VEdit-<ver>-x86_64.run --selftest all
#
# The payload is this same file: everything after the PA YLOAD line is one gzip'd
# tar archive containing VEdit/. It is written once (no duplicated copy).
#
# Extraction directory: ${TMPDIR:-/tmp} first, but only if that filesystem allows
# executing files (some /tmp mounts are noexec). Otherwise it falls back to
# ${XDG_CACHE_HOME:-$HOME/.cache}/vedit and probes again. If neither can execute,
# the script exits with an explicit message instead of failing silently.
# Set VEDIT_KEEP_TMP=1 to keep the extracted tree and print its path.
set -eu

OFFSET=__PAYLOAD_OFFSET__
SELF=$0
KEEP=0
[ "${VEDIT_KEEP_TMP:-0}" = "1" ] && KEEP=1

probe_exec() {
  dir=$1
  probe="$dir/.vedit-exec-probe"
  printf '#!/bin/sh\nexit 0\n' > "$probe" 2>/dev/null || return 1
  chmod +x "$probe" 2>/dev/null || { rm -f "$probe"; return 1; }
  if "$probe" >/dev/null 2>&1; then
    rm -f "$probe"
    return 0
  fi
  rm -f "$probe"
  return 1
}

fail_noexec() {
  cat >&2 <<MSG
VEdit: no extraction directory allows executing files.
  tried: $WORK
  cause: the filesystem is mounted noexec (or the directory is not writable).
Alternatives:
  1. use the tar.gz package instead (no FUSE, no temp execution needed):
       tar xzf vedit-<ver>-linux-x86_64.tar.gz && ./VEdit/vedit.sh
  2. use the AppImage with extract-and-run:
       ./VEdit-<ver>-x86_64.AppImage --appimage-extract-and-run
  3. point TMPDIR at a writable, exec-capable directory and retry.
MSG
  exit 3
}

WORK=""
cleanup() {
  if [ -n "$WORK" ] && [ -d "$WORK" ]; then
    if [ "$KEEP" = "1" ]; then
      echo "VEdit: VEDIT_KEEP_TMP=1, keeping the extracted tree at $WORK" >&2
    else
      rm -rf "$WORK"
    fi
  fi
}
trap cleanup EXIT INT TERM

BASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$BASE/vedit.XXXXXX" 2>/dev/null) || {
  echo "VEdit: cannot create a temporary directory under $BASE" >&2
  exit 2
}
if ! probe_exec "$WORK"; then
  rm -rf "$WORK"
  CACHE_BASE=${XDG_CACHE_HOME:-$HOME/.cache}
  mkdir -p "$CACHE_BASE/vedit" 2>/dev/null || {
    echo "VEdit: cannot create $CACHE_BASE/vedit" >&2
    exit 2
  }
  WORK=$(mktemp -d "$CACHE_BASE/vedit/run.XXXXXX" 2>/dev/null) || {
    echo "VEdit: cannot create a fallback directory under $CACHE_BASE/vedit" >&2
    exit 2
  }
  if ! probe_exec "$WORK"; then
    fail_noexec
  fi
fi

echo "VEdit: extracting to $WORK" >&2
tail -c +"$OFFSET" "$SELF" | tar xz -C "$WORK" || {
  echo "VEdit: payload extraction failed (corrupt download?)" >&2
  exit 4
}

# Run in the foreground (not exec) so the EXIT trap can remove the tree again.
"$WORK/VEdit/vedit.sh" "$@"
STATUS=$?
exit "$STATUS"
__PAYLOAD_BELOW__
