# regenerate: bash evidence/config/run-evidence.sh (inside the pinned image)
Files:
  print-config.txt        inputs + resolved table + JSON + Debug rendering
  print-config-table.txt  table only
  print-config.json       stable JSON document (schema_version 1)
  print-config-debug.txt  Debug rendering of Loaded (secret redaction proof)
  print-config-degrade.txt invalid project TOML -> config.parse_failed, user layer kept
  secret-grep.txt         grep -c assertions: 0 matches for every secret pattern
  schema-assertions.txt   machine assertions over the JSON contract
  scoped-gates.log        cargo fmt/clippy/test for crates/vd-config
