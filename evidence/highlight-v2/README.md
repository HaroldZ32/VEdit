# highlight-v2 evidence — T12 / Slice 1 (G2 link + G3 grammar=verilog)

Frozen contract: `.mpd/cellwork/cell-20261031T085543161Z/contracts/syntax-highlight.md`.
C9 stamp for the whole directory: `00-collect.txt`.

## What was broken (captured BEFORE the change: `00-before.txt`)

* `VEDIT_SYNTAX_GRAMMAR` resolved to **none** in the product configure
  (`CMakeCache.txt` + `grammar_config_generated.h`), because
  `src/syntax/provider/CMakeLists.txt` seeded the cache with `none` **before** the
  grammar lane (added later by the root build) could set `verilog` — the
  provider→cache priority defect.
* `nm target/qt6-build/src/app/vedit-app` had **0** tree-sitter/grammar symbols:
  the three syntax libraries were compiled but never linked into the product.
* `readelf -d vedit-app` had no `libtree-sitter` / `libKF6TextEditor` entry.

## What changed

1. **G3 / grammar-id ownership** (`src/syntax/provider/CMakeLists.txt`):
   * standalone provider build keeps the honest `none` default;
   * a product build no longer seeds the cache — the grammar lane owns the value;
   * the provider's `grammar_config_generated.h` + the test expectation are written
     by a **deferred** call at the end of the top-level configure, after every lane
     has set the cache. An empty id is a `FATAL_ERROR`, never a silent `none`.
     (`cmake_language(DEFER ...)` arguments are expanded when the call runs, so
     the paths are handed over through CACHE variables — see the comment there.)
2. **G2 / product link** (`src/syntax/product/`):
   * a product-wiring translation unit registers the stack with Qt's
     `Q_COREAPP_STARTUP_FUNCTION` (no `src/app` change needed): registers the
     built-in grammars, activates the compiled grammar in `SyntaxService`, creates
     the tree-sitter provider (this is what keeps `libtree-sitter` in the binary),
     loads the bundled definitions through the private-path loader, and proves the
     editing kernel is present (`KTextEditor::Editor::instance()`).
   * `src/syntax/grammars/CMakeLists.txt` pulls the product lane in (the root lane
     list does not name it); the product lane defers the `vedit-app` link and uses
     `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` so the registration object is not dropped.
   * a missing target/package is a `FATAL_ERROR`: the product never links a partial
     syntax stack, and `-DVEDIT_SYNTAX_GRAMMAR=` (no grammar id) is a hard
     configure failure (`07-negative-control.log`).

## The observable result (headless facts, not "how it looks")

`01-after-link.txt`:
* `readelf -d vedit-app` shows **libtree-sitter.so.0.25**, **libKF6TextEditor.so.6**
  and **libKF6SyntaxHighlighting.so.6**;
* `nm -C vedit-app` shows 174 `vedit::syntax`/`vedit::kde` symbols,
  `registerBuiltinGrammars`, `KsyntaxData::load`, `U ts_parser_new`,
  `U KTextEditor::Editor::instance()`;
* `grammar_config_generated.h` = `"verilog"`, cache = `verilog`, grammar manifest =
  `verilog`.

`04-syntax-product-tests.log` (19 checks, exit 0):
* the registry holds `verilog` + `systemverilog`, the compiled grammar is activated
  and the tree-sitter provider is created;
* the loader loads **our** `vedit-verilog.xml` / `vedit-systemverilog.xml`, exposes
  Verilog/SystemVerilog definitions and 30 themes, and refuses nothing (`embargoed=[]`);
* a `.v` document receives **3** applied format ranges and a `.sv` document **4** —
  i.e. highlighting attributes really are produced from these files.

`09-screenshot-L2.log` + `screenshots/`: **L2 only.** The screenshot proves the
product binary runs with the stack linked and the startup hook executed; it does
NOT show syntax colours, because the shell still hosts the demo `QPlainTextEdit`
(G1 / edit-core owns the editor kernel integration). No user-visible highlighting
is claimed by this task.

## File index

| file | what it shows |
|---|---|
| `00-before.txt` | pre-fix baseline: grammar `none`, 0 symbols, no tree-sitter/KTextEditor NEEDED |
| `01-after-link.txt` | post-fix `nm`/`readelf`/generated-header/cache/manifest + real startup line |
| `03-build-qt-gate.log` | `bash scripts/build-qt.sh` (the declared gate) on the change |
| `04-syntax-product-tests.log` | the headless product acceptance test (19 checks) |
| `06-standalone-lanes.log` | the documented standalone provider/grammars builds still pass |
| `07-negative-control.log` | no grammar id ⇒ configure fails (`FATAL`-level), never a silent `none` |
| `08-ctest.log` | the gate's ctest log (12 tests incl. the two new syntax entries) |
| `09-screenshot-L2.log` + `screenshots/` | L2 only, limitation stated |
| `10-licensing-and-embargo.txt` | C11: only our LGPL-2.1+ XML + the MIT grammar ship; KDE GPLv3+ refused |

## Deliberately NOT claimed

* That a user sees coloured `.v`/`.sv` text: the demo editor does not paint through
  the syntax stack until edit-core (T10) lands; the screenshot is L2 and says so.
* That tree-sitter *parsing* of a real project succeeds end-to-end in the product:
  the provider is created and the grammar is activated (the runtime path is linked
  and reachable), but the overlay/consumer is T17's scope.
