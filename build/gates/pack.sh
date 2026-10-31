#!/usr/bin/env bash
# Gate `pack`: the relocatable tarball package must exist, be a valid gzip tar,
# and contain a launcher (start script or the editor binary). T25 produces it;
# until then this gate is honestly RED.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

shopt -s nullglob
candidates=("$ROOT"/build/artifacts/*.tar.gz "$ROOT"/*.tar.gz)
if [ "${#candidates[@]}" -eq 0 ]; then
  no_gate "[pack] 本项目暂无 tar.gz 目录包（T25 负责）：未找到 *.tar.gz"
fi

fail=0
for archive in "${candidates[@]}"; do
  echo "[pack] checking $archive"
  if ! entries=$(tar tzf "$archive" 2>/dev/null); then
    echo "[pack] FAIL: not a readable gzip tar archive"
    fail=1
    continue
  fi
  count=$(printf '%s\n' "$entries" | grep -cve '^[[:space:]]*$' || true)
  if [ "${count:-0}" -lt 1 ]; then
    echo "[pack] FAIL: archive is empty"
    fail=1
    continue
  fi
  # `grep` without `-q` reads the whole list; `-q` would exit at the first match
  # and SIGPIPE `printf` (this gate runs under `set -o pipefail`).
  if printf '%s\n' "$entries" | grep -E '(^|/)(AppRun|start[^/]*\.(sh|bash)|vedit)$' > /dev/null; then
    echo "[pack] OK: launcher present"
  else
    echo "[pack] FAIL: no launcher entry (AppRun/start*.sh/vedit) in the archive"
    fail=1
  fi
done
[ "$fail" -eq 0 ] || exit 1
echo "[pack] PASS: ${candidates[*]}"
