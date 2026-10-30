#!/usr/bin/env bash
# Repository gates: formatting, lint, tests, and the dependency-policy checks
# that keep the lock file inside the project's constraints.
#
#   bash scripts/gates.sh
#
# Where the gates run:
#   * a machine WITH GTK3 + GtkSourceView development files runs everything
#     locally — that is also the path taken inside the pinned build image;
#   * this host has no GTK development files (and no root to install them), so
#     the compile gates run in the pinned CentOS 7 build image, which is the
#     deployment target itself. The dependency-policy checks still run on the
#     host, and the delegation is printed explicitly — no silent downgrade.
set -euo pipefail

# shellcheck source=lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
load_versions

EVID="$ROOT/build/evidence"
mkdir -p "$EVID"

gate_lock_policy() {
  local lock="$ROOT/Cargo.lock"
  if [ ! -f "$lock" ]; then
    echo "FAIL[lock]: Cargo.lock is missing"
    return 1
  fi
  local forbidden
  forbidden=$(grep -nE '^name = "(tree-sitter[^"]*|openssl|openssl-sys|native-tls)"$' "$lock" || true)
  if [ -n "$forbidden" ]; then
    echo "FAIL[lock]: forbidden crate(s) in Cargo.lock (tree-sitter / openssl / native-tls):"
    echo "$forbidden"
    return 1
  fi
  # The gtk-rs stack must stay on the 0.18 generation; sourceview4 on the 0.5 line.
  awk '
    /^name = / { name=$0; sub(/^name = "/,"",name); sub(/"$/,"",name) }
    /^version = / {
      ver=$0; sub(/^version = "/,"",ver); sub(/"$/,"",ver)
      if (name ~ /^(gtk|gtk-sys|glib|glib-sys|glib-macros|gio|gio-sys|gobject-sys|gdk|gdk-sys|gdk-pixbuf|gdk-pixbuf-sys|atk|atk-sys|pango|pango-sys|cairo-rs|cairo-sys-rs|graphene-rs|graphene-sys)$/ && ver !~ /^0\.18\./) {
        print "BAD " name " " ver; bad=1
      }
      if (name ~ /^sourceview4(-sys)?$/ && ver !~ /^0\.5\./) {
        print "BAD " name " " ver; bad=1
      }
    }
    END { exit bad ? 1 : 0 }
  ' "$lock" || {
    echo "FAIL[lock]: the gtk-rs stack is not the 0.18 generation or sourceview4 is not 0.5.x"
    return 1
  }
  echo "PASS[lock]: no tree-sitter/openssl/native-tls; gtk-rs 0.18 generation; sourceview4 0.5 line"
}

gate_fmt() {
  echo "[gate] cargo fmt --all -- --check"
  ( cd "$ROOT" && cargo fmt --all -- --check )
}

gate_clippy() {
  echo "[gate] cargo clippy --workspace --all-targets -- -D warnings"
  ( cd "$ROOT" && cargo clippy --workspace --all-targets -- -D warnings )
}

gate_test() {
  echo "[gate] cargo test --workspace"
  ( cd "$ROOT" && cargo test --workspace )
}

run_all_gates() {
  gate_lock_policy
  gate_fmt
  gate_clippy
  gate_test
}

if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists gtk+-3.0 gtksourceview-4; then
  echo "== gates: local environment (GTK3 + GtkSourceView development files found) =="
  run_all_gates
  echo "GATES PASS (local)"
else
  echo "== gates: host =="
  # The pure-shell policy check is real work the host can do.
  gate_lock_policy
  echo "HOST-LACKS-GTK: no GTK3/GtkSourceView development files on this host (and no root to install them)."
  echo "Running fmt/clippy/test in the pinned CentOS 7 build image, which is the deployment target."
  build_build_image "$EVID/12-gates-image-build.log"
  run_in_build_container bash scripts/gates.sh
  echo "GATES PASS (host: dependency policy locally + compile gates in the pinned image)"
fi
