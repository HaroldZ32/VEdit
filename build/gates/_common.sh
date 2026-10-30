#!/usr/bin/env bash
# Shared helpers for build/gates/*.sh (sourced, not executed directly).
# shellcheck shell=bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
export ROOT

# Print an honest RED and exit 1: this project has no such gate (yet).
no_gate() {
  printf '%s\n' "$*" >&2
  exit 1
}
