# T21 wiring evidence — tests, audits, and how to re-run them

Owner: **Senior Engineer** · cell `cell-20261030T020634155Z` · task **T21**
Write scope: `crates/vd-app/**` + this evidence directory (plus the selftest stage
in `scripts/build-in-container.sh`, maintainer action).

All commands below were run inside the pinned image
`vedit-build:local` (CentOS 7.9.2009) under `xvfb-run -a`, from the repository
root mounted at `/vedit-build`.

## Environment used for the full run

```sh
export HOME=/vedit-build/target/home
export VEDIT_SLANG=/vedit-build/build/slang/out/slang
export VEDIT_GSV_DATADIR=/opt/gsv4/share/gtksourceview-4
VERIBLE=$(ls -d /vedit-build/target/verible-pin/extract/verible-*/bin | head -1)
export VEDIT_VERIBLE_LS="$VERIBLE/verible-verilog-ls"
export VEDIT_VERIBLE_FORMAT="$VERIBLE/verible-verilog-format"
export VEDIT_VERIBLE_LINT="$VERIBLE/verible-verilog-lint"
```

`target/verible-pin/extract` holds the release tarball downloaded from the URL
pinned in `scripts/versions.env` and verified against the pinned sha256 values
(see `build/evidence/10-verible-pin.txt`); `build/slang/out/slang` is the T3
artifact. A bare container needs a machine id for GApplication registration; the
build script now creates one when `/etc/machine-id` is empty.

## Suites (success criterion: exit 0)

| command | exit | result file |
|---|---|---|
| `xvfb-run -a vd-app --selftest all` | **0** | `selftest-all.json` (raw stdout: `selftest-all.raw`) |
| `xvfb-run -a vd-app --selftest smoke` | 0 | `selftest-smoke.json` |
| `xvfb-run -a vd-app --selftest slang` | 0 | `selftest-slang.json` |
| `xvfb-run -a vd-app --selftest lsp` | 0 | `selftest-lsp.json` |
| `xvfb-run -a vd-app --selftest highlight` | 0 | included in `all` |
| `xvfb-run -a vd-app --selftest config` | 0 | included in `all` |
| `xvfb-run -a vd-app --selftest fim` | 0 | included in `all` |

The JSON reports are the raw report object; the `.raw` files keep the full
process output (GLib may print warnings before the JSON on stdout).

## What each suite proves (against the ACs)

- **smoke (AC-02/03/04-UI/06 partially/10 partially)** — window realized; a tab
  opens per file and the current tab follows the opened file; `.sv` applies the
  SystemVerilog language; an edit marks the document modified; `undo` restores
  through `Document`; save-as writes the document; problem panel receives a
  diagnostic; the gutter mark is created with category `vedit-error`; all
  contract shortcut actions are installed with the expected accelerator
  (`accels_for_action` reports GDK's `<Primary><Shift>f`); find-next finds a
  needle through `Document`; the status bar reports the cursor.
- **highlight (AC-05 UI)** — the real GtkSourceView language manager provides
  `verilog` and `systemverilog`; opening a `.sv` buffer sets a language; the
  cobalt style scheme applies. The negative path is recorded separately in
  `language-warning.raw` (startup warning when the data dir is missing), which is
  what the startup guard exists for.
- **config** — `vd-config` loads, editor settings are present, every Verible tool
  resolution attempt is recorded and none uses `PATH`, formatter/linter paths are
  resolved, FIM defaults to off.
- **lsp (AC-06/07/09)** — the real Verible language server is started by
  `vd-lsp`; `publishDiagnostics` arrives (generation > 0); one-key formatting
  returns edits which are applied through `Document::apply_edits_one_step`, and
  **one undo restores the pre-format content**.
- **slang (AC-10/11)** — the real SLANG binary runs through `vd-deeplint`; the
  undeclared identifier is reported; its byte columns are converted to editor
  UTF-16 ranges (line 2, characters 4..19); the problem panel and the gutter
  receive it (mark at line 2).
- **fim (AC-13/14, default-off path)** — FIM is off, has a short reason, the off
  path starts **no worker** (`vd_fim::worker_spawns()` unchanged) and shows no
  ghost text; the status bar explains why it is unavailable. Accept (Tab) and
  cancel (Esc) are wired in the view key handler but require a configured
  endpoint, so the suite only asserts the default-off path.

## Position units (AC-05/08/10 cross-check)

`position-table.txt` is printed by `crates/vd-app/examples/position_table.rs`
from a real `Document` over the line `  // 你好😀 e\u{301}`: for every character it
shows the UTF-8 byte column (slang's unit), the GTK character offset
(`TextIter::offset`) and the UTF-16 column (LSP / `vd-editor`). It also converts
a slang byte column through `vd_deeplint::SourceLines` and prints the editor
position. The only conversion sites in the app are `vd-editor`'s
`position_at_iter`/`iter_at_position` (via `position_adapter`) and
`vd-deeplint`'s byte conversion.

## No lane logic re-implemented (`audits.txt`)

- no direct buffer writes in `crates/vd-app/src` (audit 1 empty);
- every text change goes through `vd_editor::Document` (audit 2 lists them);
- no JSON-RPC framing / SystemVerilog parsing / tree-sitter in the app (audit 3
  empty); the app calls `vd_lsp`, `vd_deeplint`, `vd_fim`, `vd_config`,
  `vd_syntax`;
- the app spawns no processes itself (audit 4 empty);
- dependencies are the workspace crates plus the already-pinned workspace deps
  (`lsp-types`, `tokio`, GTK, GtkSourceView, serde).

## Workspace gates

`gates-workspace.txt`: `cargo fmt --all -- --check` exit 0,
`cargo clippy --workspace --all-targets -- -D warnings` exit 0,
`cargo test --workspace` exit 0.

## Shortcut table

`shortcuts.md` is generated from `commands::SHORTCUTS`, the same table the window
installs. Config key bindings override the defaults through
`config_glue::key_binding` + `gtk_accel` (both the config's `<Ctrl>o` form and a
human `Ctrl+Shift+F` form are accepted).

## Known limitations (honest list)

- **Screenshots**: on this Xvfb (no compositor) `gdk_pixbuf_get_from_window`
  returned a blank frame, so the PNG evidence was removed instead of shipping a
  misleading image. The window is proven realized by the selftest check and by
  the `xwininfo` step in `scripts/build-in-container.sh`.
- **Definition / references / outline / rename** are wired through `LspBridge`
  to `vd-lsp` and rendered by the UI; the selftest does not assert them because
  the Verible server's support varies and the outline/references dialogs are
  modal. They are exercised manually against the same client.
- **Rename preview** shows the edit summary and deliberately does **not** apply
  it (the contract asks for a preview).
- **FIM accept/cancel** with a real endpoint and **IME (AC-16)** are manual
  checks; there is no endpoint in the image, and FIM must stay off by default.
