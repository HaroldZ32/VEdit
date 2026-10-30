# T13 — configuration and project discovery: evidence

Owner: Deep Worker · task board **T13** (split key **T6**) · scope `crates/vd-config/**`
· evidence `evidence/config/`.

## What the crate does (and what a consumer gets)

`vd-config` is pure logic: no GTK, no GL, no network, no process-global mutation.
Tests inject `Env`/`XdgDirs`, so they run in parallel in a container without a
display and without touching the process environment.

Precedence, later layers overriding earlier ones — every key records the layer
that set it and the layers it shadowed:

1. **defaults** (`model.rs`; UI language `zh-CN`, theme `cobalt`, FIM **off**,
   no endpoint, no key, tab width 4, the documented default shortcuts);
2. **user config** `$XDG_CONFIG_HOME/vedit/vedit.toml` (default
   `$HOME/.config/vedit/vedit.toml`);
3. **project config** `<project root>/vedit.toml`, where the root is the nearest
   ancestor containing `vedit.toml` or `.git` (start directory = the directory of
   the file the editor opened — `Loader::new(start_dir)` documents this);
4. **environment** `VEDIT_FIM_{ENDPOINT,MODEL,API_KEY,PROTOCOL,TIMEOUT_MS,ENABLED}`.

API surface for the consuming lanes:

| consumer | entry point |
|---|---|
| `vd-app` (T21) | `Loader::new(dir).with_env(Env::from_process()).load()?` → `Loaded { config, project, entries, warnings, tools }` |
| `vd-lsp` (T15) | `project.filelists[*].entries` (source set), `project.include_dirs`, `project.defines`, `project.rules_path`, `config.verible`, `tools.ls.path` |
| `vd-deeplint` (T17) | `project.include_dirs` / `project.defines` (slang arguments) |
| `vd-fim` (T19) | `config.fim` (`enabled`, `protocol`, `endpoint`, `model`, `timeout_ms`, `api_key.expose_secret()` only at the transport call) |
| acceptance (T27) | `render_table(&loaded)`, `to_json(&loaded)` — the `--print-config` forms |

## Machine contract (`--print-config --json`)

Top-level keys: `schema_version` (**1**), `sources`, `entries`, `config`,
`project`, `tools`, `warnings`.

- `entries[]`: one row per key, the **final** value only —
  `{key, value, source, overridden_by, shadowed}`. `source` is the layer in
  force, `overridden_by` is the nearest shadowed layer, `shadowed` is the full
  chain (oldest first). `value` is tagged by `kind`: `text`, `redacted`
  (`set: bool`), `list` (`len`), `number`, `bool`.
- `tools.{ls,format,lint}`: `{path?, source?, attempts[], error?}`;
  **`attempts` is always present**, each `{source: config|env|bundled, path,
  found}`, including on success. `PATH` is never consulted; a missing tool's
  `error` lists every probed path.
- `warnings[]`: `{code, file?, line?, message}`; `code` is the stable API,
  `message` is informational. Codes emitted today:

  `filelist.unknown_token`, `filelist.cycle`, `filelist.depth_exceeded`,
  `filelist.missing`, `filelist.unreadable`, `filelist.define_invalid`,
  `filelist.duplicate_entry`, `rules.search_stopped`, `config.parse_failed`,
  `config.unknown_key`, `config.invalid_value`, `xdg.invalid`.

## `[keys]` action-name contract (cross-lane fix)

`[keys]` is keyed by the **action id the shell installs** (`vd-app`'s GAction
names), never by a config-only alias: the shell looks a binding up with the id it
is about to install (`config_glue::key_binding(keys, shortcut.id, fallback)`).
The old table used config-only names (`deep_lint`, `goto_definition`, …), so a
user rebinding `deep_lint` got a silent no-op. The table and the contract are now
one:

| legacy name (ignored, warned) | current action id |
|---|---|
| `deep_lint` | `slang` |
| `goto_definition` | `definition` |
| `find_references` | `references` |
| `symbol_outline` | `outline` |
| `fim_accept` | `accept-fim` |
| `fim_cancel` | `cancel-fim` |
| `fim_manual` | `fim` |

Behaviour, locked by tests (`model.rs` unit tests +
`tests/config_behaviour.rs`):

- `KNOWN_BINDING_ACTIONS` lists every accepted action id (the full shell
  registry, so non-default actions such as `open` are rebindable too);
- `DEFAULT_KEY_BINDINGS` only uses those ids, with no duplicates and no legacy
  alias (a test rejects a table that reintroduces one);
- an unknown action (`frobnicate`) is stripped with
  `config.unknown_key: unknown configuration key `keys.frobnicate`; ignored`;
- a legacy name is stripped with an explicit replacement message:
  `configuration key `keys.deep_lint` was renamed to `slang`; ignored` — the old
  name is **not** accepted as a second spelling;
- a layer whose every `[keys]` entry was dropped does not replace the defaults
  with an empty map;
- `docs/USER_GUIDE.md` (T29) already uses `slang` / `accept-fim`, matching this
  contract (checked: no legacy name appears under `docs/`).

Evidence: `keys-rename.txt` (mapping table + resolved JSON with both warnings),
`fixture-keys/vedit.toml` (the input), and `scoped-gates.log` (fmt/clippy/test:
31 unit + 14 integration + 4 secret tests pass).

## Secret discipline (AC-15)

`ApiKey` has a redacted `Debug`/`Serialize`, **no `Display`**, and the only
plaintext accessor is `expose_secret()`. Provenance projects it as
`{"kind":"redacted","set":true}`; the rendered config serializes it as
`"<redacted>"`. Configuration parse errors are reported as a line number plus a
static message — TOML/serde error text (which can quote the offending value) is
never propagated. `secret_discipline.rs` asserts both configured and environment
secrets are absent from the table, the JSON, `Debug` output and warning text.

## Files

| file | content |
|---|---|
| `print-config-table.txt` | the resolved table for the fixture project |
| `print-config.json` | the stable JSON document (schema_version 1) |
| `print-config-debug.txt` | `Debug` rendering of `Loaded` (redaction proof) |
| `print-config.txt` | inputs + table + JSON + Debug in one transcript |
| `print-config-degrade.txt` | invalid project TOML → `config.parse_failed`, user layer kept |
| `secret-grep.txt` | `grep -c` over every rendering: **0** matches for each secret pattern |
| `schema-assertions.txt` | machine assertions over the JSON contract (`ALL_SCHEMA_ASSERTIONS_PASS`) |
| `scoped-gates.log` | `cargo fmt/clippy/test` for `vd-config` (all exit 0) |
| `fixture/`, `fixture-invalid/` | the input trees used above |
| `run-evidence.sh` | regenerates everything inside the pinned CentOS 7 image |

## Fixture result (before → after)

Inputs: user config (`tab_width = 2`, `insert_spaces = false`,
`keys.format = "Ctrl+Alt+F"`), project config (`tab_width = 8`,
`[verible] ls = "tools/verible-verilog-ls"`, `filelists = ["extra.f"]`,
`defines = ["CHIP=spike"]`, FIM endpoint/model/key), `top.f` with
`+incdir+inc`, `+define+WIDTH=8` and one unsupported token, `.rules.verible_lint`
one level above the project, and FIM environment overrides.

Observed: `editor.tab_width = 8 source=project overridden_by=user
shadowed=[default,user]`; `editor.insert_spaces = false source=user
overridden_by=default`; `fim.api_key <redacted> source=env overridden_by=project`;
`fim.endpoint` from env; `verible.ls` resolved against the project root and
reported as absolute with `source=project`; two filelists (`extra.f`, `top.f`),
include dir `inc`, defines `CHIP=spike` + `WIDTH=8`, rules file found above the
project root with `rules_search_stop` recorded; one
`filelist.unknown_token` warning; `format`/`lint` unresolved but with three
probed attempts each and an error naming them.

## Reproduce

```bash
# scoped gates (inside the pinned image; see scripts/lib.sh)
cargo fmt -p vd-config -- --check
cargo clippy -p vd-config --all-targets -- -D warnings
cargo test -p vd-config          # 28 unit + 12 behaviour + 4 secret + 1 doc test

# regenerate the evidence
docker run --rm -e HOME=/vedit-build/target/home \
  -e CARGO_HOME=/vedit-build/target/cargo-home -e RUSTUP_HOME=/opt/rust \
  -e RUSTUP_TOOLCHAIN=1.99.0 -v "$PWD:/vedit-build" -w /vedit-build \
  vedit-build:local bash /vedit-build/evidence/config/run-evidence.sh
```

## Gate status note

`cargo fmt/clippy/test -p vd-config` are green (see `scoped-gates.log`).
The repository-wide `bash scripts/gates.sh` was **not** green at the time of this
task because another lane's crate was mid-implementation
(`crates/vd-editor/src/save.rs`: `clippy::type_complexity`,
`clippy::while_let_loop` — task T9, in progress by its owner). That is outside
this task's write scope; the scoped gates above are the ones that cover this
change. Re-run the workspace gates once T9 lands.
