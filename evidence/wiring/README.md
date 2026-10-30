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
  fallback: runtime discovery of crates/vd-app/tests/fixtures (cwd, then exe ancestors)
```

Every suite goes through `selftest::fixture_root()`; there is **no compile-time
absolute path in the binary** (the old `env!("CARGO_MANIFEST_DIR")` fallback was
removed: remap-path-prefix cannot rewrite `env!` literals, so it would have leaked
the build directory even in a remapped build). `--selftest` prints the root it
actually uses:

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
| `smoke-window.png` | 34414 | `7f7b1f493d2b86da62af769b013ae3e46e13e75a600e9153e47f88d9591a8c11` |
| `highlight-window.png` | 33890 | `6d4723468d988b38039cb0adb0cf5b4b0eba054afa762136bcff528822e4cdc3` |
| `slang-window.png` | 37860 | `069de60bc1abed8bceea1124dce6bc3711a5a71342168d36701e821e4e317d7b` |
| `lsp-window.png` | 31482 | `7bf52787d72363acddde9c91bf2d0d247146a8ce27b7cb2f00db656b49f5a5ef` |
| `cleanroom-highlight.png` | 33222 | `50633cb69b258699571cd67876e97d9cc8de08b12af6ffc232b9c557f4dba2a1` |

### Clean room (no WM, no host GSV/fonts, `--network none`)

`cleanroom-screenshot.txt` is the before/after proof:

- **Before the fix** (the payload's original binary): `xwininfo -root -tree`
  listed **two mapped 1120×780 `VEdit` toplevels** (`0x200009` rendered,
  `0x200002` blank). This was a **product bug (b)**, not just a screenshot bug:
  `run_interactive` called `register_and_activate()` and then
  `application.run()`, so GTK activated twice and built two windows; the first
  was leaked and never painted. Fixed by letting `run()` own the single
  activation and by making the activate handler present the existing window.
- **After the fix** (`build/artifacts/vd-app`): **one** toplevel.
- **After the fix, `--screenshot`**: exit 0, **33222 bytes**,
  sha256 `50633cb69b258699571cd67876e97d9cc8de08b12af6ffc232b9c557f4dba2a1`,
  PNG signature `89504e470d0a1a0a`, ImageMagick `mean=26046.3 stddev=26379.4`
  (not uniform), and the selftest's own sampled-pixel check reports
  `sampled_colours=15 channel_range=255`. The blank-image failure mode is now
  impossible silent: the layout is force-allocated before the draw, and the
  function refuses to write a uniform image.

### Remap / one binary for every product

`scripts/versions.env` now carries the shared `RELEASE_RUSTFLAGS` (source roots
→ `/vedit`, cargo homes → `/cargo`), sourced by the pinned build. The release
build log records the embedded-path count: **0** `/vedit-build` strings after
the fixture-root `env!` removal (categories: no panic-location paths, no
`env!` literal). `build/artifacts/vd-app` sha256 is recorded in
`cleanroom-screenshot.txt`; T23/T25 must rebuild their payloads with this same
flag set for the two products to carry the same binary (their currently checked-in
outputs predate the shared flags).

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
- **Packaged binaries**: the checked-in AppImage/tar.gz outputs were built before
  the shared remap and before the two-window/screenshot fixes, so their `vd-app`
  sha256 differs from `build/artifacts/vd-app` until T23/T25 rebuild. The e2e
  harness (T27) records both hashes instead of claiming equality.
- **Keys cross-assertion** (`[keys]` ids vs app actions) is pending the
  `vd-config` `KNOWN_BINDING_ACTIONS` export from the config lane; the app's
  `key_binding`/`gtk_accel` unit tests cover the rebinding path meanwhile.
