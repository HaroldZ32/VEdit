#!/usr/bin/env bash
# C9 collection header writer for evidence/slice2-overlay (T93 evidence repair).
# Usage: collect-header.sh <log> <what> <command-line>
# Writes collected_at, HEAD, the collection-time porcelain, machine facts and the
# command into <log>. Kept in the evidence directory so the header can be
# reproduced exactly (the sandbox gives each command its own /tmp).
set -euo pipefail
LOG="$1"; WHAT="$2"; CMD="$3"
{
  echo "## C9 collection header — $WHAT"
  echo "## collected_at: $(date -Is)"
  echo "## collected_at_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "## git_head: $(git rev-parse HEAD)"
  echo "## git_head_subject: $(git log -1 --format=%s)"
  echo "## git_status_porcelain (collection-time snapshot; SHARED worktree, so other lanes' in-flight files appear too):"
  git status --porcelain | sed 's/^/##   /'
  echo "## machine: $(lscpu | sed -n 's/^Model name:[[:space:]]*//p' | head -1) | nproc=$(nproc) | kernel=$(uname -r) | mem=$(sed -n 's/^MemTotal:[[:space:]]*//p' /proc/meminfo)"
  echo "## loadavg_at_collection: $(cat /proc/loadavg)"
  echo "## image: vedit-qt6-build:local $(docker image inspect vedit-qt6-build:local --format '{{.Id}}')"
  echo "## host: $(uname -srmo)"
  echo "## lane_sources (src/syntax/overlay is UNTRACKED in git, so these hashes are what binds this evidence to the code it measured):"
  (cd "$(git rev-parse --show-toplevel)" && sha256sum src/syntax/overlay/* 2>/dev/null | sed 's/^/##   /')
  echo "## command: $CMD"
  echo
} > "$LOG"
