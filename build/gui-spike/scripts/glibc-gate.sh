#!/usr/bin/env bash
# Walk every ELF in an AppDir and assert the highest required GLIBC symbol
# version is <= 2.17 (CentOS 7 / glibc 2.17). Exits non-zero on violation.
set -euo pipefail

TARGET="${1:?usage: glibc-gate.sh <dir> <report.txt>}"
REPORT="${2:?usage: glibc-gate.sh <dir> <report.txt>}"
MAX_OK="2.17"
limit="0.0"
status=0
elfs=0

: > "$REPORT"
while IFS= read -r -d '' f; do
  if ! head -c 4 "$f" | grep -q $'\x7fELF'; then continue; fi
  v=$(readelf --version-info "$f" 2>/dev/null \
      | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?' \
      | sed 's/GLIBC_//' | sort -uV | tail -1 || true)
  [ -n "$v" ] || continue
  printf '%s\t%s\n' "$v" "${f#./}" >> "$REPORT.rows"
  elfs=$((elfs + 1))
  if [ "$(printf '%s\n%s\n' "$v" "$limit" | sort -V | tail -1)" = "$v" ]; then limit="$v"; fi
  if [ "$(printf '%s\n%s\n' "$v" "$MAX_OK" | sort -V | tail -1)" != "$MAX_OK" ]; then
    echo "VIOLATION: $f requires GLIBC_$v > $MAX_OK" >&2
    status=1
  fi
done < <(find "$TARGET" -type f -print0)

{
  echo "# GLIBC gate — $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# target: $TARGET"
  echo "# rule: max required GLIBC symbol version <= $MAX_OK"
  sort -t$'\t' -k1,1V "$REPORT.rows" 2>/dev/null | tail -20 || true
  echo "ELFS_SCANNED=$elfs"
  echo "MAX_REQUIRED_GLIBC=$limit"
  echo "RESULT=$([ "$status" -eq 0 ] && echo PASS || echo FAIL)"
} >> "$REPORT"
rm -f "$REPORT.rows"
exit "$status"
