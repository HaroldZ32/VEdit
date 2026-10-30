# T21 — application wiring: problem panel, gutter marks, shortcuts, ghost text and selftest suites

Cell `cell-20261030T020634155Z` · task **T21** · owner **Senior Engineer**
Write scope: `crates/vd-app/**` + this evidence directory.

## 1. Verdict

The deliverable exists and the success criterion holds in the pinned CentOS 7
image under Xvfb: **`vd-app --selftest all` exits 0**, with every suite
(`smoke`, `highlight`, `config`, `lsp`, `slang`, `fim`) passing against the real
lanes (real GtkSourceView data, real Verible LSP process, real SLANG binary, real
FIM service in its default-off state): **51 checks**. The suites assert user
behaviour, not configuration presence: goto-line moves the cursor, line numbers
are visible, replace-next + one undo round-trips through the replace bar, an
unsaved close is refused and the confirmation request is recorded, **F12 jumps
to the cross-file definition in b.sv**, formatting is one undo step, SLANG
diagnostics reach the panel and gutter. The packaged layout is supported by the
runtime fixture-root override (proven out-of-source), and `--screenshot` renders
the real widget tree into PNG. Workspace gates are green. Evidence:
`evidence/wiring/` (`README.md` maps every check and command to its AC).

No lane logic is re-implemented in the app: text changes go through
`vd_editor::Document` only, formatting uses `apply_edits_one_step`, positions are
converted only by `vd-editor`/`vd-deeplint`, diagnostics by the lanes, and the
app spawns no processes itself (audits in `audits.txt`).

## 2. What was wired

- **Tabs** (`document_tab.rs`): one `Document` + one `GtkSourceView` per tab,
  language/style applied via `vd-syntax`, line numbers/highlight-current-line
  from `vd-config`, open/save/save-as, unsaved-close confirmation, active-tab
  label with `*`.
- **Problem panel** (`diagnostics.rs` + `app.rs`): a single `ViewDiagnostic`
  model for Verible (`vd_lsp::diagnostic_rule` for the rule name) and SLANG
  (`vd_deeplint`, positions already UTF-16); rows show severity, source, rule and
  message; a row click jumps to the range. Diagnostics without a position
  (`TooManyErrors`) are listed and explained instead of dropping.
- **Gutter marks**: `GtkSourceMark`s (categories `vedit-error/warning/note`, themed
  icons) placed on the worst severity per line; the app owns and deletes the
  marks explicitly (category removal alone is not reliable in GtkSourceView), and
  a re-entrancy guard keeps `refresh_status` from recursing through `mark-set`.
- **Shortcuts** (`commands.rs`): the contract table is installed as window actions
  with the config's bindings overriding the defaults. Ctrl+Shift+F format,
  Ctrl+Shift+L SLANG, F12 definition, Shift+F12 references, Ctrl+Shift+O
  outline, Alt+\ FIM, Tab accept / Esc cancel (view key handler), plus
  open/new/save/save-as/close/quit/undo/redo/find/replace/find-next/goto/panel.
  The table is also printed into the selftest report (`shortcuts.md`).
- **LSP** (`lsp_worker.rs`): one `vd_lsp::LspClient` on a tokio thread; commands
  open/format/definition/references/symbols/rename; diagnostics watch forwarded
  as events; status line shows pid/restarts (AC-09). Formatting replies are
  applied as **one undo step** through `Document::apply_edits_one_step`.
- **SLANG** (`slang_worker.rs`): runs the lane in the background against the saved
  file, or a same-directory snapshot of a modified buffer; the report is
  normalised with `diagnostics::from_slang` and goes to the panel + gutter;
  failures surface `LintFailure::user_message()` (AC-11).
- **FIM ghost text** (`fim_ghost.rs`): default off, status shows the lane's short
  reason, off path starts no worker; manual Alt+\ starts a generation, the 50 ms
  poll timer shows the completion as an overlay label at the cursor, **Tab**
  inserts it via `Document::insert` and **Esc** cancels; typing cancels in-flight
  text.
- **Startup**: `vd-config` loader, language availability check with a loud
  status/log warning when `verilog`/`systemverilog` are missing (AC-05 guard),
  tool attempt reporting, FIM settings mapping.

## 3. Position units (the mistake that would stay invisible on ASCII tests)

There is one conversion entry point (`position_adapter.rs`) on top of
`Document`'s UTF-16↔character conversion; LSP positions are clamped in; SLANG
byte columns are converted inside `vd-deeplint`. The probe in
`position-table.txt` lists, for `你好😀 e\u{301}`, byte column / GTK character
offset / UTF-16 column per character, and converts a real slang byte column to an
editor position. The `slang` suite asserts the editor range `line 2, characters
4..19`, and `position_adapter` unit tests round-trip CJK/emoji positions.

## 4. Shortcut table

See `evidence/wiring/shortcuts.md` (generated from the same `SHORTCUTS` array the
app installs) and `commands.rs`; the selftest checks that all contract bindings
exist and that `win.format` carries the Ctrl+Shift+F accelerator (GDK reports it
as `<Primary><Shift>f`).

## 5. Gates

```
cargo fmt --all -- --check                         exit=0
cargo clippy --workspace --all-targets -- -D warnings  exit=0
cargo test --workspace                             exit=0
```

Full logs: `gates-workspace.txt`. Additionally the one-command build
(`scripts/build.sh`) runs the selftest stage in the image; the stage was updated
to run `smoke highlight config fim` always and `all` when the SLANG/Verible
binaries are present, and it now validates the current JSON report shape.

## 6. Deviations and honest limitations

- **Documented deviation from the literal contract text**: none in behaviour;
  the selftest menu grew a `config` suite (useful evidence for tool resolution)
  and `all`; this is additive.
- **Two toplevels (fixed product bug)**: the clean room showed two mapped
  `VEdit` windows because the interactive path activated the GApplication twice
  (`register_and_activate()` followed by `run()`), leaking an unpainted window.
  `run()` now owns activation and the activate handler refuses to build a second
  UI; the clean room goes from 2 to 1 toplevels (`cleanroom-screenshot.txt`).
- **Blank screenshots (fixed)**: `gtk_widget_draw` was called while a layout
  allocation was pending (`Gtk-CRITICAL alloc_needed`). The capture now maps the
  window, forces `size_allocate` immediately before the draw, refuses to write a
  uniform image, and the selftest reports the sampled colours/channel range. The
  clean-room PNG is non-black (33222 bytes, sha256 in README).
- **Screenshots**: `--screenshot` renders the realized widget tree with its
  own draw handler into a Cairo surface (the T11 highlight-probe path) and writes
  a PNG through gdk-pixbuf; four real renders are in this directory with bytes +
  sha256 in `README.md`. Reading pixels back from the X window is what returns
  blank frames under Xvfb, so it is not used.
- **Navigation UI**: definition is asserted end-to-end (F12 → b.sv line 1,
  character 8). references/outline are wired but not asserted (the dialogs are
  modal and Verible's support varies); rename is a **preview only** (nothing is
  applied), as the contract asks.
- **Selftest fixture root** is runtime-overridable (`VEDIT_SELFTEST_FIXTURES`,
  fallback to the source tree) and prints the root it uses; `fixture-root-override.txt`
  proves the packaged layout by renaming the source fixture dir away and running
  the lsp suite with the override (exit 0). The packaging contract with T25 is
  `share/vedit/testdata/{definition/a.sv,definition/b.sv}`.
- **FIM accept/cancel with a live endpoint** and **IME input (AC-16)** are manual
  checks (no endpoint in the image; FIM must remain off by default). The manual
  steps:
  1. configure `[fim]` with an OpenAI-compatible endpoint and restart;
  2. press Alt+\, wait for ghost text, press Tab (inserts through Document) and
     Esc (cancels); typing cancels an in-flight request;
  3. IME: run under the desktop with `gtk3-immodule-xim`/`ibus-gtk3` (image has
     both) and type Chinese into a `.sv` buffer; the buffer text and the status
     cursor position must agree.
- **Process/engine limits** are the lanes': verible LSP capabilities (T15) and
  SLANG CLI (T17) are unchanged; the app only calls them.
- The build-image machine-id bootstrap was added because GApplication aborts in a
  container with an empty `/etc/machine-id`; it is environment setup, not product
  behaviour.

## 7. Files changed (write-scope audit)

`crates/vd-app/**` (new modules + example + tests), `evidence/wiring/**`, and
`scripts/build-in-container.sh` (selftest stage; maintainer action, noted here so
the verifier sees it). `Cargo.lock` gained only the dependency edges for the
already-pinned workspace crates (`vd-*`, `lsp-types`, `tokio`); no new external
package was added.
