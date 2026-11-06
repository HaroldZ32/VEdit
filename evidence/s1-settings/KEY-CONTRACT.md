# Settings key contract (item 10.2, T10 `settings-pages`)

Every key below lives in the SAME flat TOML store the product already used
(`<stateDir>/settings.toml`, values are strings). The store is written by
`crates/vd-settings` through the existing C ABI (`vd_settings_load` /
`vd_settings_save`); the Preferences dialog is the only writer of these keys.

Two files spell the contract and MUST stay identical:

| side | file |
| --- | --- |
| Qt (writer) | `src/workspace/settings/settings_keys.h` |
| Rust (reader/projection) | `crates/vd-settings/src/schema.rs` (`mod keys`) |

The Rust module is the reference: `ToolSettings::from_entries` and
`FimSettings::from_entries` turn the pairs into typed values, and
`crates/vd-ffi/src/settings.rs::fim_settings_from_store` projects the FIM block
onto `vd_fim::FimSettings` (including the API key).

## Keys

| key | values | default | written by |
| --- | --- | --- | --- |
| `tools.lsp_backend` | `verible`, `slang` | `verible` | Tools page |
| `tools.lint_engine` | `slang`, `verible` | `slang` | Tools page |
| `tools.verible_ls` | path | empty (resolver decides) | Tools page |
| `tools.slang` | path | empty | Tools page |
| `tools.slang_server` | path | empty | Tools page |
| `tools.verible_format` | path | empty | Tools page |
| `tools.verible_rules` | path | empty (built-in rules) | Tools page |
| `tools.include_dirs` | one `-I` dir per line | empty | Tools page |
| `tools.defines` | one `-D` define per line | empty | Tools page |
| `fim.enabled` | `true` / `false` | `false` | FIM page |
| `fim.protocol` | `open_ai`, `llama_cpp` | `open_ai` | FIM page |
| `fim.endpoint` | base URL | empty (no network) | FIM page |
| `fim.model` | model name | empty | FIM page |
| `fim.timeout_ms` | integer ms, 100..60000 | `5000` | FIM page |
| `fim.max_tokens` | integer, 1..512 | `64` | FIM page |
| `fim.ghost_text` | `true` / `false` | `false` | FIM page |

Notes for the lanes that consume these values:

* **Backend switch (AC-1.1/1.2)** — read `tools.lsp_backend` /
  `tools.lint_engine`; the stored value is exactly the status-line token
  (`backend=slang` / `backend=verible`). Both selectors are independent, as the
  requirement demands.
* **-I/-D (AC-2.5)** — `tools.include_dirs` and `tools.defines` are
  newline-separated strings (blank lines ignored, `\r` stripped). These are the
  *editor-level* values the Tools page edits; a project may still persist its own
  `project-model.json` values (AC-2.4). When a project is open its values win and
  these act as the defaults.
* **FIM (AC-10.2.4)** — `vd-ffi/src/settings.rs::fim_settings_from_store` is the
  finished projection onto `vd_fim::FimSettings`, key included. Wiring the
  product's `vd_fim_complete` request path (today: `vd_config`'s `[fim]` block in
  `crates/vd-ffi/src/fim.rs`) to call it is a one-line change in a file outside
  this task's write scope — see the report.
* **Ghost text (AC-4.7)** — `fim.ghost_text` is a display switch; the consumer is
  the editor wiring (`src/editor/kernel/fim_wiring.cpp`), outside this task.

## The FIM API key is not a store value

* There is **no** `fim.api_key` key. A plaintext one (older build / hand edit) is
  ignored by the projection, reported in the Diagnostics box, dropped from the
  in-memory entry set, and therefore removed by the next save.
* Resolution order (Rust `schema::resolve_api_key`, Qt `fimKeySource()`):
  1. `VEDIT_FIM_API_KEY` — wins, never written to disk;
  2. the user-level file `fim_api_key` next to `settings.toml`, **0600 only**;
  3. none.
* A key file that grants group or other access is REFUSED (not read) and reported
  as the `insecure-file` source; the page shows the fix (`chmod 600`).
* Both writers (Qt `writeOwnerOnlyFile`, Rust `write_api_key_file`) create the
  file **owner-only from birth** and replace it atomically: temp file created with
  `O_CREAT|O_EXCL` and mode `0600`, its mode verified with `fstat` **before** the
  first secret byte, then `rename()` over the target. The **new secret is never
  written to a group/other-readable file**; a pre-existing loose file keeps its old
  content until it is atomically replaced (it is not reused). Any failure
  **attempts to** remove the temporary file (the unlink result is not checked) and
  reports "the API key was NOT stored".
* Residual window, stated plainly: a kill or power loss between creating the temp
  file and the rename leaves a `0600` temp file that **contains the secret** — no
  group/other access, but debris with a secret in it. The next key save sweeps
  stale `fim_api_key.tmp.<pid>.<n>` files in the key directory (best effort; a temp
  whose pid is still alive is left alone in case another instance is writing).
* The password field is only ever filled by the user. `FimValues` has no key
  field, page loads/reverts clear the field, and `storeFimApiKey()` writes the key
  without putting it into any widget.
* The UI field is a `QLineEdit::Password` that is never filled from disk; the
  page only reports *where* the key comes from. No key is ever logged, formatted
  into a diagnostic, or included in an error message.
* The file name and mode are documented in
  `crates/vd-settings/src/schema.rs` and in the FIM page's own hint label.
