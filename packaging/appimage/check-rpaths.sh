#!/usr/bin/env bash
# Scan every ELF in the AppDir and assert each RUNPATH/RPATH entry is allowlisted.
# Data-driven: the only logic here is comparison; exceptions live in
# rpath-allowlist.txt (one justified line each).
#
#   check-rpaths.sh <AppDir> <allowlist.txt> <report.txt>
set -euo pipefail

APPDIR="${1:?usage: check-rpaths.sh <AppDir> <allowlist.txt> <report.txt>}"
ALLOWLIST="${2:?usage: check-rpaths.sh <AppDir> <allowlist.txt> <report.txt>}"
REPORT="${3:?usage: check-rpaths.sh <AppDir> <allowlist.txt> <report.txt>}"

allowed=()
while IFS= read -r line; do
  entry="${line%%#*}"
  entry="$(printf '%s' "$entry" | tr -d '[:space:]')"
  if [ -n "$entry" ]; then allowed+=("$entry"); fi
done < "$ALLOWLIST"

elfs=0
with_runpath=0
hits=0
unknown=0
rows=()

while IFS= read -r -d '' file; do
  if ! head -c 4 "$file" | grep -q $'\x7fELF'; then continue; fi
  elfs=$((elfs + 1))
  runpaths="$(readelf -d "$file" 2>/dev/null | grep -E '\((RUNPATH|RPATH)\)' | sed -n 's/.*\[\(.*\)\].*/\1/p' || true)"
  [ -n "$runpaths" ] || continue
  with_runpath=$((with_runpath + 1))
  IFS=':' read -r -a entries <<< "$runpaths"
  for entry in "${entries[@]}"; do
    [ -n "$entry" ] || continue
    matched=0
    for allow in "${allowed[@]}"; do
      if [ "$entry" = "$allow" ]; then matched=1; break; fi
    done
    if [ "$matched" -eq 1 ]; then
      hits=$((hits + 1))
      rows+=("ALLOWED\t$entry\t${file#"$APPDIR"/}")
    else
      unknown=$((unknown + 1))
      rows+=("UNKNOWN\t$entry\t${file#"$APPDIR"/}")
    fi
  done
done < <(find "$APPDIR" -type f -print0)

{
  echo "# RUNPATH/RPATH gate — $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# appdir: $APPDIR"
  echo "# allowlist: $ALLOWLIST"
  for row in "${rows[@]}"; do printf '%b\n' "$row"; done
  echo "ELF_SCANNED=$elfs"
  echo "ELF_WITH_RUNPATH=$with_runpath"
  echo "ALLOWLIST_HITS=$hits"
  echo "UNKNOWN=$unknown"
  if [ "$unknown" -eq 0 ]; then
    echo "RESULT=PASS"
  else
    echo "RESULT=FAIL"
  fi
} > "$REPORT"

tail -6 "$REPORT"
[ "$unknown" -eq 0 ]
