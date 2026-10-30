# T21 wiring evidence — suites, screenshots, audits, and how to re-run them

Owner: **Senior Engineer** · cell `cell-20261030T020634155Z` · task **T21**
Write scope: `crates/vd-app/**` + this evidence directory (plus the selftest
stage in `scripts/build-in-container.sh`, maintainer action).

All commands were run inside the pinned image `vedit-build:local` (CentOS
7.9.2009) under `xvfb-run -a`, from the repository root mounted at
`/vedit-build`.

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

`target/verible-pin/extract` is the release tarball downloaded from the URL
pinned in `scripts/versions.env`, verified against the pinned sha256 values
(`build/evidence/10-verible-pin.txt`); `build/slang/out/slang` is the T3
artifact. A bare container needs a machine id for GApplication registration; the
build script creates one when `/etc/machine-id` is empty. The screenshots used an
**evidence-only** `wqy-zenhei-fonts` install (CJK glyphs); the product does not
bundle fonts, and the image has no emoji font (the 😀 in the highlight screenshot
stays a placeholder box — the surrounding CJK glyphs and the highlighting are
the point).

## Selftest fixture root (runtime override — packaging contract)

`--selftest` reads its cross-file fixtures from:

```
VEDIT_SELFTEST_FIXTURES   (runtime; root that contains definition/a.sv, definition/b.sv)
  fallback: <CARGO_MANIFEST_DIR>/tests/fixtures   (source tree only)
```

Every suite goes through `selftest::fixture_root()`; the compile-time path is
**only a fallback** and `--selftest` prints the root it actually uses:

```
vd-app selftest: fixture_root=... (VEDIT_SELFTEST_FIXTURES=Some("..."))
```

Packaging (T25) puts the fixtures at `share/vedit/testdata/` and exports
`VEDIT_SELFTEST_FIXTURES=<share>/vedit/testdata`; the directory name and
contents are fixed: `definition/a.sv` (`module a_top; b_top u_b (); endmodule`)
and `definition/b.sv` (`module b_top; endmodule`). `fixture-root-override.txt`
proves the packaged layout by renaming the source fixture directory away and
running the lsp suite with the override pointing outside the repository: exit 0,
`fixture_root=/tmp/vedit-fixtures-def`.

## Suites (success criterion: exit 0)

| command | exit | result file |
|---|---|---|
| `xvfb-run -a vd-app --selftest all` | **0** | `selftest-all.json` (raw: `selftest-all.raw`) |
| `xvfb-run -a vd-app --selftest smoke` | 0 | `selftest-smoke.json` |
| `xvfb-run -a vd-app --selftest highlight` | 0 | `selftest-highlight.json` |
| `xvfb-run -a vd-app --selftest slang` | 0 | `selftest-slang.json` |
| `xvfb-run -a vd-app --selftest lsp` | 0 | `selftest-lsp.json` |
| `xvfb-run -a vd-app --selftest config` / `fim` | 0 | included in `all` |

`all` = 51 checks: smoke 20, highlight 8, config 6, lsp 6, slang 6, fim 5. The
JSON reports are the report object extracted from stdout; `.raw` keeps the full
process output (GLib may print warnings before the JSON).

## What each suite proves (AC ↔ check)

- **smoke (AC-02/03/04 UI)** — window realized; one tab per opened file and the
  current tab follows the opened file; `.sv` applies SystemVerilog; an edit marks
  the document modified; `undo` restores through `Document`; save-as writes the
  file; **goto-line moves the cursor and clamps to the last line**; **line
  numbers are visible** (`shows_line_numbers()`); **replace-next through the
  replace bar changes the text and one undo restores it**; **an unsaved close is
  refused and the confirmation request is recorded** (injected confirmation
  seam); problem panel lists diagnostics; a gutter mark exists with category
  `vedit-error`; the contract shortcut actions are installed with the expected
  accelerator (`<Primary><Shift>f`); find-next finds a needle through
  `Document`; the status bar reports the cursor.
- **highlight (AC-05 UI, CJK rendering)** — real GtkSourceView provides
  `verilog`/`systemverilog`; `.sv` buffers get a language; cobalt style scheme
  applies; a CJK/emoji fixture opens with the text intact; startup warning is
  absent in this image. Negative path: `language-warning.raw` (run without
  `VEDIT_GSV_DATADIR`) shows the loud startup warning; that run exits 1 because
  the highlight checks fail, which is the intended guard.
- **config** — `vd-config` loads, editor settings present, every Verible tool
  resolution attempt recorded and none uses `PATH`, formatter/linter resolved,
  FIM defaults off.
- **lsp (AC-06/07/08/09)** — real Verible LS; diagnostics generation > 0;
  one-key formatting applies through `Document::apply_edits_one_step` and **one
  undo restores the pre-format content**; **F12 on `b_top` in `a.sv` jumps to
  `b.sv` line 1 character 8** (the existing script asserts the 0-based
  `Position { line: 0, character: 7 }`).
- **slang (AC-10/11)** — real SLANG through `vd-deeplint`; undeclared identifier
  reported; byte columns → editor UTF-16 (line 2, characters 4..19); panel and
  gutter receive it (line 2 mark); failure messages come from the lane.
- **fim (AC-13/14 default-off)** — FIM off with a short reason, no worker spawned,
  no ghost text, status bar shows the reason. Accept (Tab)/cancel (Esc) are wired
  in the view key handler but need a configured endpoint, so they are manual.

## Screenshots (real widget-tree render, not window readback)

`--screenshot <png>` renders the realized widget tree into a Cairo RGB24 surface
(the T11 highlight-probe path; `gdk_pixbuf_get_from_window` is blank under Xvfb)
and writes PNG via gdk-pixbuf. Files in this directory:

| file | bytes | sha256 |
|---|---:|---|
| `smoke-window.png` | 28497 | `03cb3fea2e7e20dcbf8b490435d707f860e7f217c85cb4448264712d3d14feb6` |
| `highlight-window.png` | 31598 | `9e69d589b40f57d5b923fd6a64279090124835e989d9de23eb6756901b61d9ab` |
| `slang-window.png` | 30434 | `e8499d0f0410c6c1e2acd37d8b7c65ae749d95c66445d3641fb8ebb6dff392ec` |
| `lsp-window.png` | 26310 | `12731ae79fc9e22e43a2f4db87b6fe052519b0cb7aeef899e68cef165c637ce0` |

`highlight-window.png` shows the CJK line with syntax colours and the status bar
(`systemverilog`, cursor, counts); `slang-window.png` shows the gutter marks and
the Problems panel; `smoke-window.png` shows the panel populated. They are real
renders of the same `GtkSourceView` + `Document` the user sees.

## Position units (AC-05/08/10 cross-check)

`position-table.txt` (from `examples/position_table.rs`) lists, for
`  // 你好😀 e\u{301}`, the UTF-8 byte column (slang's unit), the GTK character
offset (`TextIter::offset`) and the UTF-16 column (LSP / `vd-editor`), and
converts a slang byte column through `vd_deeplint::SourceLines`. The only
conversion sites are `vd-editor`'s `position_at_iter`/`iter_at_position` (via
`position_adapter`) and `vd-deeplint`'s byte conversion.

## No lane logic re-implemented (`audits.txt`)

- Audit 1 (narrowed): buffer calls that **write text** — empty;
- Audit 1b: the only other buffer calls are gutter mark bookkeeping
  (`create_source_mark`/`delete_mark`), which do not change text;
- Audit 2: every text change goes through `vd_editor::Document`;
- Audit 3: no JSON-RPC framing / SystemVerilog parsing / tree-sitter in the app;
- Audit 4: the app spawns no processes itself;
- Audit 5: the only `CARGO_MANIFEST_DIR` use is the fixture-root fallback.

## Workspace gates

`gates-workspace.txt`: `cargo fmt --all -- --check` exit 0,
`cargo clippy --workspace --all-targets -- -D warnings` exit 0,
`cargo test --workspace` exit 0.

## Shortcut table

`shortcuts.md` is generated from `commands::SHORTCUTS`, the same table the window
installs. Config bindings override the defaults via
`config_glue::key_binding` + `gtk_accel` (both `<Ctrl>o` and `Ctrl+Shift+F` forms
are accepted).

## Known limitations (honest list)

- **references / outline** are wired (dialogs) but not asserted by the suites;
  **definition is now asserted end-to-end** (real Verible + real jump).
- **rename preview** shows the edit summary and deliberately does not apply it.
- **FIM accept/cancel with a live endpoint** and **IME input (AC-16)** are manual:
  configure `[fim]`, press Alt+\ then Tab/Esc; for IME, run under a desktop with
  `gtk3-immodule-xim`/`ibus-gtk3` (the image has both) and type Chinese.
- The **real modal close dialog** is not driven by the suite; the injected
  confirmation seam asserts the refusal and the recorded request, and T27 covers
  the dialog itself.
- **Emoji glyph**: the image has no emoji font, so 😀 renders as a placeholder
  box; CJK renders after the evidence-only font install.
