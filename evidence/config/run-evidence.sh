#!/usr/bin/env bash
# Regenerate the T13 configuration evidence inside the pinned CentOS 7 image.
#
#   docker run --rm -e HOME=/vedit-build/target/home \
#     -e CARGO_HOME=/vedit-build/target/cargo-home -e RUSTUP_HOME=/opt/rust \
#     -e RUSTUP_TOOLCHAIN=1.99.0 -v "$PWD:/vedit-build" -w /vedit-build \
#     vedit-build:local bash /vedit-build/evidence/config/run-evidence.sh
set -u
ROOT=${ROOT:-/vedit-build}
OUT="$ROOT/evidence/config"
FIX="$OUT/fixture"
INVALID="$OUT/fixture-invalid"
EXAMPLE="cargo run -q -p vd-config --example print-config --"

cd "$ROOT" || exit 1
# Fail loudly if the example does not compile: otherwise every captured output
# below would be an empty file that looks like "no findings".
cargo build -q -p vd-config --example print-config || { echo "print-config example failed to build" >&2; exit 1; }
export HOME="$FIX/home"
export VEDIT_FIM_ENDPOINT="https://env.local/v1"
export VEDIT_FIM_MODEL="env-model"
export VEDIT_FIM_API_KEY="test-env-secret-987654"

run() {
  local label="$1"; shift
  echo "\$ HOME=$HOME VEDIT_FIM_ENDPOINT=$VEDIT_FIM_ENDPOINT VEDIT_FIM_MODEL=$VEDIT_FIM_MODEL VEDIT_FIM_API_KEY=<redacted-in-log> $*"
  "$@"
  echo "EXIT=$?"
}

{
  echo "# T13 evidence: resolved configuration for evidence/config/fixture/project"
  echo "# command: \`$EXAMPLE [--json|--debug] <dir>\` inside the pinned CentOS 7 image"
  echo "# env: HOME=<fixture home>, VEDIT_FIM_ENDPOINT/MODEL/API_KEY set (FIM env overrides)"
  echo
  echo "## input: user config (XDG)"
  cat "$FIX/home/.config/vedit/vedit.toml"
  echo
  echo "## input: project config"
  cat "$FIX/project/vedit.toml"
  echo
  echo "## input: project filelists"
  for file in "$FIX/project"/*.f; do echo "--- $(basename "$file")"; cat "$file"; done
  echo
  echo "## resolved table"
  $EXAMPLE "$FIX/project"
  echo "EXIT=$?"
  echo
  echo "## resolved json"
  $EXAMPLE --json "$FIX/project"
  echo "EXIT=$?"
  echo
  echo "## Debug rendering (secrets must be redacted)"
  $EXAMPLE --debug "$FIX/project"
  echo "EXIT=$?"
} > "$OUT/print-config.txt" 2>&1

$EXAMPLE --json "$FIX/project" > "$OUT/print-config.json" 2> "$OUT/print-config.stderr"
$EXAMPLE --debug "$FIX/project" > "$OUT/print-config-debug.txt" 2>> "$OUT/print-config.stderr"
$EXAMPLE "$FIX/project" > "$OUT/print-config-table.txt" 2>> "$OUT/print-config.stderr"

# Degradation: the project file is invalid TOML, the user layer must survive.
$EXAMPLE "$INVALID/project" > "$OUT/print-config-degrade.txt" 2>&1

# [keys] action-name contract: legacy names are reported (not silently kept),
# current action ids rebind, and anything else is reported as unknown.
RENAME="$OUT/fixture-keys"
rm -rf "$RENAME"
mkdir -p "$RENAME"
cat > "$RENAME/vedit.toml" <<'TOML'
# Legacy name (renamed), the current action id, and a typo.
[keys]
deep_lint = "Ctrl+Alt+L"
slang = "Ctrl+Alt+S"
frobnicate = "F9"
TOML
{
  echo "# [keys] action names after the vd-config / vd-app unification"
  echo "# command: \`$EXAMPLE --json evidence/config/fixture-keys\` (pinned image)"
  echo
  echo "## legacy name -> current action id"
  echo "deep_lint       -> slang"
  echo "goto_definition -> definition"
  echo "find_references -> references"
  echo "symbol_outline  -> outline"
  echo "fim_accept      -> accept-fim"
  echo "fim_cancel      -> cancel-fim"
  echo "fim_manual      -> fim"
  echo
  echo "## input: evidence/config/fixture-keys/vedit.toml"
  cat "$RENAME/vedit.toml"
  echo
  echo "## resolved json (config.keys and warnings)"
  $EXAMPLE --json "$RENAME"
  echo "EXIT=$?"
} > "$OUT/keys-rename.txt" 2>&1

{
  echo "# secret leakage assertions (grep -c; 0 matches required)"
  echo "# patterns: test-config-secret, test-env-secret, test-broken-secret"
  for file in print-config-table.txt print-config.json print-config-debug.txt print-config-degrade.txt; do
    printf '%-28s ' "$file"
    grep -c -E 'test-(config|env|broken)-secret' "$OUT/$file" || true
  done
  echo
  echo "# the redacted form must be present in the table and JSON"
  printf 'table has <redacted>: '; grep -c '<redacted>' "$OUT/print-config-table.txt" || true
  printf 'json has "kind": "redacted": '; grep -c '"kind": "redacted"' "$OUT/print-config.json" || true
  echo
  echo "# degradation warning must be present (code, not wording)"
  printf 'config.parse_failed in table: '; grep -c 'config.parse_failed' "$OUT/print-config-degrade.txt" || true
  printf 'broken secret in degradation output: '; grep -c 'test-broken-secret' "$OUT/print-config-degrade.txt" || true
} > "$OUT/secret-grep.txt" 2>&1

python3 - "$OUT" <<'PY' > "$OUT/schema-assertions.txt" 2>&1
import json
import sys
from pathlib import Path

out = Path(sys.argv[1])
doc = json.loads((out / "print-config.json").read_text())
raw = (out / "print-config.json").read_text()

assert doc["schema_version"] == 1, "schema_version"
assert doc["sources"] == ["default", "user", "project", "env"], "sources"
entries = {entry["key"]: entry for entry in doc["entries"]}

width = entries["editor.tab_width"]
assert width["source"] == "project", width
assert width["overridden_by"] == "user", width
assert width["shadowed"] == ["default", "user"], width
assert width["value"] == {"kind": "number", "value": 8}, width

insert = entries["editor.insert_spaces"]
assert insert["source"] == "user", insert
assert insert["overridden_by"] == "default", insert

key = entries["fim.api_key"]
assert key["value"] == {"kind": "redacted", "set": True}, key
assert key["source"] == "env", key
assert key["overridden_by"] == "project", key
assert doc["config"]["fim"]["api_key"] == "<redacted>", "config serialization"

project = doc["project"]
assert project["root"].endswith("/evidence/config/fixture/project"), project["root"]
assert project["root_source"] == "vedit_toml", project["root_source"]
assert any(path["path"].endswith("top.f") for path in project["filelists"]), "top.f"
assert any(path["path"].endswith("extra.f") for path in project["filelists"]), "extra.f"
assert project["rules_path"].endswith(".rules.verible_lint"), project["rules_path"]
assert {define["name"] for define in project["defines"]} == {"CHIP", "WIDTH"}, project["defines"]
assert any(item.endswith("/inc") for item in project["include_dirs"]), project["include_dirs"]

tools = doc["tools"]
assert tools["ls"]["source"] == "config", tools["ls"]
assert tools["ls"]["attempts"][0]["found"] is True, tools["ls"]["attempts"]
for name in ("format", "lint"):
    assert tools[name]["attempts"], f"{name} attempts are always present"
    assert tools[name].get("path") is None, tools[name]

codes = {warning["code"] for warning in doc["warnings"]}
assert "filelist.unknown_token" in codes, codes

keys = doc["config"]["keys"]
assert keys.get("format") == "Ctrl+Alt+F", keys
assert "deep_lint" not in keys and "fim_accept" not in keys, keys
assert entries["keys.slang"]["source"] == "default", entries["keys.slang"]
assert "keys.deep_lint" not in entries, entries.get("keys.deep_lint")

assert "test-env-secret" not in raw, "JSON leaked the environment secret"
assert "test-config-secret" not in raw, "JSON leaked the config secret"
print("ALL_SCHEMA_ASSERTIONS_PASS")
PY

echo "# regenerate: bash evidence/config/run-evidence.sh (inside the pinned image)" > "$OUT/README.txt"
cat >> "$OUT/README.txt" <<'TXT'
Files:
  print-config.txt        inputs + resolved table + JSON + Debug rendering
  print-config-table.txt  table only
  print-config.json       stable JSON document (schema_version 1)
  print-config-debug.txt  Debug rendering of Loaded (secret redaction proof)
  print-config-degrade.txt invalid project TOML -> config.parse_failed, user layer kept
  keys-rename.txt         [keys] contract: legacy rename warning, current id rebinds, typo warned
  secret-grep.txt         grep -c assertions: 0 matches for every secret pattern
  schema-assertions.txt   machine assertions over the JSON contract
  scoped-gates.log        cargo fmt/clippy/test for crates/vd-config
TXT

echo "== generated =="
ls -l "$OUT" | tail -12
echo "== schema assertions =="
cat "$OUT/schema-assertions.txt"
echo "== secret grep =="
cat "$OUT/secret-grep.txt"
