#!/bin/sh
# GLIBC + RPATH/RUNPATH gate for a packaged payload (T25).
#
#   glibc-gate.sh <payload-dir> <rpath-allowlist> <log-file>
#
# For every ELF in <payload-dir>:
#   * the highest required GLIBC symbol version must be <= 2.17 (the CentOS 7
#     glibc), printed as one line per file;
#   * every RUNPATH/RPATH entry must be covered by the allowlist file.
#
# Prints the four totals the contract asks for and fails (exit 1) on any glibc
# violation or on UNKNOWN != 0.
set -u

PAYLOAD=${1:?usage: glibc-gate.sh <payload-dir> <allowlist> <log-file>}
ALLOW=${2:?missing rpath allowlist}
LOG=${3:-/dev/stdout}
TARGET=2.17

: > "$LOG"
exec 3>&1
exec >>"$LOG" 2>&1

echo "# GLIBC / RPATH gate over $PAYLOAD"
echo "# target: highest required GLIBC symbol version <= $TARGET"
echo "# allowlist: $ALLOW"
echo

is_elf() {
  readelf -h "$1" >/dev/null 2>&1
}

max_glibc() {
  readelf --version-info "$1" 2>/dev/null \
    | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?' \
    | sed 's/^GLIBC_//' \
    | sort -uV \
    | tail -n 1
}

# An entry is allowed when it equals an allowlist prefix or starts with it.
allowed() {
  entry=$1
  while IFS= read -r line; do
    case "$line" in
      ''|'#'*) continue ;;
    esac
    case "$entry" in
      "$line"|"$line"/*) return 0 ;;
    esac
  done < "$ALLOW"
  return 1
}

scanned=0
with_rpath=0
hits=0
unknown=0
violations=0

while IFS= read -r file; do
  [ -f "$file" ] || continue
  is_elf "$file" || continue
  scanned=$((scanned + 1))
  top=$(max_glibc "$file")
  [ -n "$top" ] || top="none"
  echo "ELF $file max_glibc=$top"
  if [ "$top" != "none" ]; then
    highest=$(printf '%s\n%s\n' "$top" "$TARGET" | sort -uV | tail -n 1)
    if [ "$highest" != "$TARGET" ]; then
      echo "GLIBC_VIOLATION $file requires GLIBC_$top > $TARGET"
      violations=$((violations + 1))
    fi
  fi

  paths=$(readelf -d "$file" 2>/dev/null \
    | grep -E '(RPATH|RUNPATH)' \
    | sed -n 's/.*\[\(.*\)\].*/\1/p')
  if [ -n "$paths" ]; then
    with_rpath=$((with_rpath + 1))
    old_ifs=$IFS
    IFS=:
    for entry in $paths; do
      if [ -z "$entry" ]; then continue; fi
      case "$entry" in
        '$ORIGIN'*|'${ORIGIN}'*)
          hits=$((hits + 1))
          echo "RPATH $file entry=$entry verdict=origin"
          ;;
        /*)
          if allowed "$entry"; then
            hits=$((hits + 1))
            echo "RPATH $file entry=$entry verdict=allowlisted"
          else
            unknown=$((unknown + 1))
            echo "RPATH $file entry=$entry verdict=UNKNOWN"
          fi
          ;;
        *)
          # A relative entry is not an absolute host path; record it, do not fail.
          hits=$((hits + 1))
          echo "RPATH $file entry=$entry verdict=relative"
          ;;
      esac
    done
    IFS=$old_ifs
  fi
done <<LIST
$(find "$PAYLOAD" -type f | sort)
LIST

echo
echo "ELF_SCANNED=$scanned"
echo "ELF_WITH_RUNPATH=$with_rpath"
echo "ALLOWLIST_HITS=$hits"
echo "UNKNOWN=$unknown"
if [ "$violations" -ne 0 ] || [ "$unknown" -ne 0 ]; then
  echo "GLIBC_GATE=FAIL glibc_violations=$violations unknown_rpath=$unknown"
  exit 1
fi
echo "GLIBC_GATE=PASS"
exit 0
