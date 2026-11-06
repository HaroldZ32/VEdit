# RM-11 (T24) — a `.f` filelist is a manifest, never HDL source

Branch `feat/ux-batch-2`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
User words: “不要解析非 RTL 的语法错误，这边在 filelist 文件上还给我报了个错，这玩意儿肯定不行啊”.

**Which lane made the change.** The predicate and most consumers were landed by
**RM-2 (T6)**, which owns the file-set plumbing: the language registry
(`crates/vd-config/src/language.rs`), the C++ mirror
(`src/workspace/project/language_registry.h`), the filelist-entry filter, the
active-document analysis, the project-file-set entry, the lint-request builder
and the module-catalog input. **This row (T24) closed the remaining gap**: the
last boundary before the lint engine (a polluted request must be excluded with a
recorded reason, never parsed), the observables (`LINT-SKIP`, the published
`nonSourceSkipped` list), the explicit T24 assertions in the regression, and the
verification against every item below. The literal-trailing-`$` (A-17) correction
is from **RM-3 (T7)**.

## 1. The ONE predicate

| Language | Role |
|---|---|
| `crates/vd-config/src/language.rs` | **Authoritative data.** `LANGUAGES` = `LanguageSpec { id, display_name, extensions, capabilities }`; `Capability { Source, Diagnostics, Lint, Catalog }`; `language_for_path()`, `supports(path, cap)`. |
| `src/workspace/project/language_registry.h` | **C++ mirror** of the same table (the ABI does not publish the Rust table yet; `crates/vd-ffi` was outside the change scope). `registerLanguage()` is the one arrival seam. Both sides pin the same contract in tests. |

The HDL set is **`.sv .svh .v .vh`** — the repository's own `SCANNED_EXTENSIONS`
(vd-config/src/project.rs) is exactly that set, and it is now a *projection* of the
registry (`the_scan_extensions_are_the_registrys_source_projection`). Nothing
else is a language: `.f`, `.lst`, `.tcl`, `.sdc`, `.xdc`, `.md`, `.txt`, `.json`,
`.toml`, `.yml`, a `.f` named inside a `.f` — none has an entry, so no consumer
ever asks "should I skip it?" (ADDENDUM A-17: a literal trailing `$` in a file
name is part of the name, so `out_tail.sv$` is still Verilog).

**Consumers (all ask the registry — no second extension list):**

| Consumer | Where |
|---|---|
| project scan | `crates/vd-config/src/project.rs` `is_scanned_source` → `language::supports(.., Source)` (also used by the `vd_project_expand` / `_v2` fallback) |
| filelist expansion | the same file: filelist ENTRIES are filtered by `Source` (a `.f`/`.md`/`.tcl` entry never becomes a source; the `.f` itself is only READ) |
| active-document analysis | `ActiveFileWatcher::analyze()` → `supports(path, Diagnostics)`; otherwise publishes `vedit.diagnostics.analysisAvailability {path, available:false, reason}` |
| project file-set entry | `BackendWiring::setProjectFiles()` → `Source` |
| lint request builder | `BackendWiring::buildLintRequest()` → `Lint` |
| **lint engine boundary** | `DiagnosticsController::startProjectPass()` → `Lint`; excluded paths are recorded in `projectPassNonSource()` and the wiring prints `LINT-SKIP path='…' reason='no lint analyzer for this file type'` |
| module-catalog input | `CatalogClient::beginCatalog()` → `Catalog` |
| status dots | follow the analysis rows: a path that is never analysed never gets a dot; `.f`/`.md`/`.tcl` therefore never carry one |

**Note on “skip with a recorded reason”.** The user rejected a skip/deny list in
favour of a positive capability model, and that is what is built: the registry
decides positively, and a file with no analyzer is simply not a candidate. The
last lint boundary keeps the second half of the requirement: if a request still
carries such a path (a caller that bypasses the builders), the pass EXCLUDES it,
records the reason and starts with the remaining files — the engine is never
handed a `.f`, so the user can never see a syntax error on their filelist.

## 2. The fixture and the regression

Harness: `src/workspace/diagnostics/selftests/rm2_project_switch_e2e.cpp`
(ctest `vedit-rm2-project-switch-e2e`), sections `rm2.start-*`, `rm11.*`,
`t24.*`. Real shell + real project/file-tree/diagnostics lanes. Fixture project:

```
project-a/
  project.f            <- the manifest (auto-discovered)
  sources.f            <- lists a.sv, ../shared/listed.sv, notes.md, run.tcl
  notes.md             <- non-HDL file in the tree
  run.tcl              <- non-HDL file in the tree
  a.sv                 <- real source
shared/listed.sv       <- source reached ONLY through the manifest
```

Assertions (all PASS):

```
rm2.start-manifest-not-a-source          the .f is not in the file set
rm2.start-non-hdl-not-sources            notes.md / run.tcl are not sources
rm2.start-manifest-sources-present       a.sv + shared/listed.sv ARE (listed entries honoured)
rm11.not-a-source.sources.f              opened as a document: available=0 rows=0
                                         reason='not a source language; nothing to analyse'
rm11.not-a-source.notes.md               same (available=0, rows=0)
rm11.not-a-source.run.tcl                same
rm11.fake-language-enters-analysis       registering a fake language makes its file analysable
                                         without touching a consumer (the model is data)
t24.predicate.lint / .catalog            the one predicate answers per consumer
t24.lint.engine-never-sees-non-hdl       a POLLUTED request (manifest+note+script+sources)
                                         reaches the engine without them
t24.lint.sources-reach-engine            the closed source reaches the engine; an open modified
                                         buffer is in the pass's open-file skip (C8), as documented
t24.lint.excluded-recorded               the three paths are recorded in projectPassNonSource()
t24.lint.non-hdl-only-refused            a non-HDL-only request does not start
t24.lint.reason-recorded                 and the pass message says
                                         "no files to lint (3 file(s) have no lint analyzer …)"
```

## 3. Red before / green after

| Run | Observed |
|---|---|
| **GREEN** (`03-e2e-final.log`) | `SUMMARY checks=133 failures=0`; `T24-LINT started=1 engine-files=1 excluded=3 files='…/shared/listed.sv' excluded-files='…/sources.f,…/notes.md,…/run.tcl'`; `T24-LINT-NON-HDL-ONLY started=0 message='no files to lint (3 file(s) have no lint analyzer for their file type; …)'` |
| **RED — T24 boundary removed** (`02-e2e-negative-control.log`) | `engine-files=4 excluded=0 files='…/sources.f,…/notes.md,…/run.tcl,…/listed.sv'` (the manifest IS handed to the engine); `t24.lint.non-hdl-only-refused` FAIL (the pass starts); 4 failures — exactly the user's failure mode |
| **RED — the whole registry removed** (RM-2 negative control, `../rm2-project-switch/02-e2e-negative-control.log`) | file set 5 files incl. `notes.md`/`run.tcl`; `rm11.not-a-source.sources.f available=1 reason=''` (the manifest analysed as HDL); 9 failures |

After both restores the suites are green again; the regression is now part of
`vedit-rm2-project-switch-e2e` and its 8-test affected set is
`04-ctest-affected.log` → **8/8 passed**, including the real-slang
`vedit-module-tree-catalog-test` (79 checks) and `vedit-app-shell-selftest`.

## 4. Commands (observed)

```text
run_in_qt6_container env QT_QPA_PLATFORM=offscreen … vedit-rm2-project-switch-e2e
  -> SUMMARY checks=133 failures=0                      (03-e2e-final.log)
  -> SUMMARY checks=133 failures=4 (boundary reverted)  (02-e2e-negative-control.log)

run_in_qt6_container ctest --test-dir target/qt6-build -R "vedit-(…)" --output-on-failure
  -> 100% tests passed, 0 tests failed out of 8          (04-ctest-affected.log)

# product build after the controller change
cmake --build target/qt6-build --target vedit-app
  -> Linking CXX executable src/app/vedit-app
```

## 5. Bounds

- The lint boundary is proven with a fake backend that records the exact request
  the engine receives; the real CLI pass is exercised by
  `vedit-project-lint-e2e`/`vedit-b3-wiring-e2e` (green in the affected set) but
  not with a `.f` fixture there.
- The module-catalog INPUT is asserted through the registry and the persisted
  set; a full slang elaboration over this fixture is not run in this harness
  (`vedit-module-tree-catalog-test` runs real slang over its own fixture).
- The status-dot coverage follows the analysis rows; the harness asserts zero
  rows for the manifest/notes/tcl rather than pixels.
- The two registry tables are mirrored until the ABI publishes the Rust one
  (`crates/vd-ffi` outside the RM-2 scope; a follow-up).

## 6. Evidence files

| file | what |
|---|---|
| `01-e2e-green.log`, `03-e2e-final.log` | green run (133 checks) with the T24 section |
| `02-e2e-negative-control.log` | RED: the lint boundary removed (engine receives the `.f`, `.md`, `.tcl`) |
| `04-ctest-affected.log` | 8/8 affected suites |
| `../rm2-project-switch/02-e2e-negative-control.log` | RED: the whole registry removed |
