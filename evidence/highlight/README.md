# T11 — language detection and syntax highlighting (evidence)

Cell `cell-20261030T020634155Z`, board task **T11** (split key T5), frozen contract
`.mpd/cellwork/cell-20261030T020634155Z/contracts/T5.md`, owner Junior Engineer, verifier
Reviewer (board T12).

Deliverable: `.v/.vh → verilog`, `.sv/.svh/.svi → systemverilog`, using the **upstream**
GtkSourceView 4.6.1 `verilog.lang` / `systemverilog.lang` and the upstream `cobalt` style
scheme. No highlighting engine is written here; `.lang` files are data assets and need no
patch (see `assets/language-specs/README.md`).

Changed paths that are code (`crates/vd-syntax/**`):

| path | role |
|---|---|
| `src/detect.rs` | pure extension → language table (no GTK); `LanguageId`, `language_id_for_path`, `supported_extensions`, `is_supported` |
| `src/gtk_glue.rs` | language/style-scheme managers with the documented search-path priority; `apply_language`, `apply_style_scheme`, `is_language_available`, `missing_languages` |
| `src/lib.rs` | module wiring, re-exports, AC-05 evidence note |
| `src/bin/highlight-probe.rs` | evidence producer (AC-05 rendering probe) |
| `tests/fixtures/ac05.sv` | AC-05 fixture: every probed token category appears once |

## 1. How AC-05 is judged (rendering, not string search)

`systemverilog.lang` does not contain `module` as a keyword — it includes the Verilog root
context instead. Measured in the pinned image, `evidence/highlight/03-langspec-provenance.txt`:

```
$ grep -c '<keyword>module</keyword>' systemverilog.lang   -> 0
$ grep -c '<keyword>module</keyword>' verilog.lang         -> 1
$ grep -n 'context ref="verilog:verilog"' systemverilog.lang -> 256:  <context ref="verilog:verilog"/>
```

A string assertion on `systemverilog.lang` would therefore produce a false FAIL. The probe
instead binds the `systemverilog` language to a buffer, shows it, and reads the **rendered**
style tag at each AC-05 token (foreground colour + weight). That is the same route the T5
spike used, and both are recorded as the contract's "rendered style or inheritance-chain
resolution" options.

## 2. Rendered result (`01-probe.log`, exit code 0)

`cobalt`, 6 distinct colours, `PROBE PASS`:

| construct | position | style id(s) whose scheme style matches | colour | weight |
|---|---|---|---|---|
| module | 4:1 | def:keyword (+ def:builtin, same colour) | `#ff9d00` | 700 |
| interface | 18:1 | def:keyword | `#ff9d00` | 700 |
| package | 23:1 | def:keyword | `#ff9d00` | 700 |
| class | 27:1 | def:keyword | `#ff9d00` | 700 |
| always_ff | 11:3 | def:keyword | `#ff9d00` | 700 |
| constraint | 29:3 | def:keyword | `#ff9d00` | 700 |
| covergroup | 32:1 | def:keyword | `#ff9d00` | 700 |
| macro_define (`` `define ``) | 3:1 | def:preprocessor (candidate set) | `#cccccc` | 400 |
| comment | 2:4 | def:comment | `#0088ff` | 400 |
| string | 15:21 | def:string | `#3ad900` | 400 |
| system_task (`$display`) | 15:11 | def:keyword (cobalt styles system tasks with the keyword colour) | `#ff9d00` | 700 |
| number (`42`) | 5:21 | def:number (candidate set) | `#ff0044` | 400 |
| data_type (`logic`) | 7:10 | def:type | `#80ffbb` | 400 |

`DISTINCT=6`; every keyword-family construct shares one colour; comment / string / number /
macro / data-type are each distinct from it. `$display` is asserted as *styled* (its style is
the keyword colour in cobalt), not as distinct.

**Style-id caveat (measured, not a shortcut):** GtkSourceView 4.6.1 creates its style tags
*unnamed* — the buffer's tag table holds 14 tags of which only the 3 `gtksourceview:context-classes:*`
tags have names. The exact style id is therefore not exposed; the probe derives the candidate
set from the language's declared style ids resolved against the applied scheme
(`STYLE_TABLE` lines). **Colour + weight are the authoritative fields**; the id is a readable
label with ambiguity shown explicitly (`def:builtin|def:keyword`).

## 3. Why the probe shows a window (exception to "no window in vd-syntax")

Measured on this exact GtkSourceView build (all four runs in the pinned image under `xvfb-run`):

| setup | result |
|---|---|
| buffer only (`set_highlight_syntax` + `set_language` + `set_text` + cobalt) | language bound, scheme set, context classes applied (`comment`, `string`), **no styled tag**: all 13 tokens `color=<none>` after 10 s of main-context pumping |
| buffer + `View` not realised | identical to the above |
| buffer + `View` + `window.realize()` without `show()` | identical to the above |
| buffer + `View` + `window.show_all()` (mapped) | **13/13 tokens styled**, `DISTINCT=6` |

So the rendered evidence requires a **shown** view on this upstream version. The compromise,
approved by the captain after this measurement:

* `crates/vd-syntax` **library API and unit tests never create a window or a `gtk::Application`**;
* exactly one `[[bin]]`, `highlight-probe`, may create one window, only to produce this
  evidence, is not referenced by the library path, and is not started by `cargo test`.

Screenshot: `probe-ac05.png` (900×700, produced by the probe's own `view.draw()` into a cairo
image surface; `--screenshot` writes a PPM because `cairo-rs` is pulled in by `gtk` without the
`png` feature, and `render.sh` converts it and deletes the intermediate).

## 4. Search path (why highlighting can silently disappear)

Measured: `GtkSourceLanguageManager::new()` resolves its default search path to
`$XDG_DATA_HOME` + `$XDG_DATA_DIRS` (`<dir>/gtksourceview-4/{language-specs,styles}`) and **does
not** include the GtkSourceView build prefix. The packaged launcher must therefore set
`XDG_DATA_DIRS` (AppRun does) or `VEDIT_GSV_DATADIR` (the AppRun also sets it to
`$APPDIR/usr/share/gtksourceview-4`).

Priority implemented in `gtk_glue.rs`:

1. GtkSourceView's own default path — untouched when no override is set;
2. `VEDIT_GSV_DATADIR` — **appended after** the default entries, never replacing them;
3. `is_language_available()` / `missing_languages()` — the app layer must warn when a language
   is missing instead of showing a window with no syntax colouring.

Probe output line `LANGUAGES verilog=true systemverilog=true missing=[]` exercises the
no-override and override paths (the probe passes `--datadir`, the process is also run with the
variable unset, which is what the packaged launcher relies on).

## 5. Reproduce

Inside the pinned `vedit-build:local` image, from the repository root:

```sh
# AC-05 rendering evidence (needs a display; xvfb is in the image)
sh evidence/highlight/render.sh /opt/gsv4/share/gtksourceview-4   # -> 01-probe.log, probe-ac05.png

# this lane's gates (fmt / clippy / test, isolated to vd-syntax)
sh evidence/highlight/run-gates.sh                                 # -> 02-scope-gates.log

# asset provenance
sha256sum /opt/gsv4/share/gtksourceview-4/language-specs/{verilog,systemverilog}.lang
```

## 6. Gate results and bounds

`02-scope-gates.log` (pinned image, working tree): `cargo fmt -p vd-syntax -- --check` exit 0;
`cargo clippy -p vd-syntax --all-targets -- -D warnings` exit 0; `cargo test -p vd-syntax`
exit 0 (6 tests). The unit tests cover the pure mapping table only — every GTK assertion lives
in the probe because `GtkSourceLanguageManager::new()` panics without `gtk::init()`, and that
needs a display.

`04-workspace-test.log`: `cargo test --workspace` exits 101 **because of another lane's
in-flight code** (`vd-deeplint` has `src/diagnostic.rs` / `src/service.rs` missing at the time
of the run); `cargo test --workspace --exclude vd-deeplint` exits 0 with `vd-syntax`'s 6 tests
passing. All lanes share one working tree, so the repository-wide gate is only meaningful at
the integration points (T21/T27/T28); it was re-run here with a timestamp rather than quoted.

Not verified by this lane: a screenshot from the real application window (T21 owns the app
wiring and its screenshot), and the packaged launcher's environment (T23/T25 own bundling).
