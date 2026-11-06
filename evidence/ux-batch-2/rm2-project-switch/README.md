# RM-2 — switching projects in one window re-roots the file set AND the environment

Branch `feat/ux-batch-2`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
Owner: Senior Engineer (shared task T6). User words: switching to another folder in
the SAME window kept a pile of stale files and a stale environment
(“堆着一大堆多余的文件”, “环境变量不刷新”), and there was no top-level way to switch
projects (“文件/新工程，顶部给吧”). The RM-11 addendum (a `.f` filelist must never be
read as HDL source; no skip list — a positive language/capability registry) is folded
into this same batch and recorded in §4.

## 1. What changed (by defect)

| # | Where | Before | After |
|---|-------|--------|-------|
| (i) LSP `-I`/`-D` stale | `src/workspace/diagnostics/ffi_backends.cpp` (`BackendSwitchState`), `diagnostics_integration.cpp` | `includeDirs_`/`defines_` were written ONLY by `selectBackend()` from a settings apply. A project switch never re-pushed them, so every session started after A→B ran with **A's** `-I`/`-D`. | `setProjectEnvironment()` re-selects the SAME backend/server through the ONE writer (`selectBackend`, plus `selectEngine` for lint) with the NEW root's values; `followProjectRoot()` clears them the moment the switch starts, so a session started mid-switch cannot inherit the previous project either. `apply()` now prefers the project values over the tools-page ones (same precedence as `buildLintRequest`), so a later settings change cannot resurrect the old environment. |
| (ii) ancestor config leak | `crates/vd-config/src/project.rs` (registry-driven expansion) | `Loader` walks UP from the opened folder and its `[project]` table (filelists/include_dirs/defines) kept applying when a subfolder is opened: “open a subfolder of a bigger project” kept the enclosing project's file set and `-I/-D`. | `[project]` is project MEMBERSHIP, not a preference: it applies only when the opened folder IS the discovered config root. An enclosing `vedit.toml` still supplies non-project layers (UI/editor/tool paths); every expansion re-reads from the NEW root, so no previous merge state can be carried. |
| (iii) analysis set ≠ panel set | `src/workspace/diagnostics/diagnostics_integration.cpp` (`ProjectFileSetPoller`), `src/workspace/project/project_integration.cpp`, `src/workspace/module_tree/catalog_client.cpp` | the diagnostics poller called `vd_project_expand(root, nullptr, nullptr)`: no imported `.f` lists, no variables table, no settings store → lint/LSP expanded a DIFFERENT project than the panel (a restored imported list was silently dropped on startup). The module-tree fallback did the same. | the project lane publishes its expansion INPUTS (`vedit.project.inputs`: root, imported filelists, variables, settings path, expansion count); the diagnostics poller expands with them via `vd_project_expand_v2`, so the two sides ask the same question. The module-tree fallback passes the same inputs (settings path always; imported lists/variables only when the persisted model belongs to THIS root — never the previous project's). |
| (iv) duplicate expansions | `src/workspace/project/project_model.cpp` | `open()` always called `startExpansion()`, and one switch is reported by several live surfaces (file tree `directoryOpened`, shell `projectRootChanged`, `.f` importer) → cancel/restart of the same job. | `startExpansion()` drops a duplicate report whose inputs are identical to the job already in flight; explicit Refresh / settings-file change force a fresh job. `expansionCount()` makes it observable. |
| (v) top-level switch entry | `src/app/app_shell.cpp`, `translations/vedit_zh_CN.ts` | only `file.open-folder` (“Open F&older…”) existed. | new `file.switch-project` — “S&witch Project…” / “切换工程(&W)…” — in the **File menu** and the **main toolbar**, immediately after open-folder; it calls the same reset path (`openProjectFolderDialog()` → `setProjectRoot()` → `projectRootChanged`). |

## 2. RM-11 addendum — a `.f` is a manifest, never a source (language registry)

**Design (user's ruling: no skip list, positive capability model).**

- The **authoritative data** is `crates/vd-config/src/language.rs`: `LANGUAGES`
  is a table of `LanguageSpec { id, display_name, extensions, capabilities }`,
  with `Capability { Source, Diagnostics, Lint, Catalog }` named after the real
  consumers. Queries: `language_for_path()`, `supports(path, capability)`.
  The project scan (`is_scanned_source`) and the filelist expansion ask it —
  a `.f`/`.md`/`.tcl` has **no language entry**, so it was never a candidate.
- **The C++ consumers** use the mirror in `src/workspace/project/language_registry.h`
  (same table, `registerLanguage()` is the ONE arrival seam):
  the active-document analysis (`ActiveFileWatcher::analyze`), the project file-set
  entry (`BackendWiring::setProjectFiles`), the lint request (`buildLintRequest`)
  and the module-catalog input (`CatalogClient::beginCatalog`). The file-status
  dots are driven by the analysis rows, so a manifest never gets one.
- **A filelist that lists non-source entries** (`notes.md`, `run.tcl`, another
  `.f`) no longer injects them into the project file set: the expansion filters
  ENTRIES through the registry. This was a real leak (negative control §3b: the
  set grew to 5 files including `notes.md`/`run.tcl`).
- **No analyzer = a state, not a log line**: `vedit.diagnostics.analysisAvailability`
  publishes `{path, available:false, reason:"not a source language; nothing to
  analyse"}` (or “no analyzer for <language> in this build”). The file simply
  carries no diagnostics — never a syntax error.
- **Adding a language is data**: register a `LanguageSpec` row
  (`crates/vd-config/src/language.rs` + the C++ mirror row, or
  `registerLanguage()` at runtime for a C++-side analyzer) with its extensions
  and the capabilities landed for it. No consumer changes — proven by
  `rm11.fake-language-*` in the e2e (a fake `.fkh` language becomes analysable
  without touching `ActiveFileWatcher`).
- **Why the table is mirrored**: the authoritative data is Rust, but the C++
  consumers live on the other side of the FFI, and `crates/vd-ffi` (where a
  `vd_language_*` call would be added) is outside this change's write scope and
  is being edited by the deep-lint lane right now. Both sides pin the SAME
  extension contract in their own tests (`the_builtin_set_is_the_documented_hdl_set`
  and `rm2.start-registry-source-extensions`) so the tables cannot drift
  silently. Follow-up: publish the Rust table over the ABI and delete the C++
  table (one file + one test to delete).

## 3. Evidence — the regression that fails before the fix

The harness is `src/workspace/diagnostics/selftests/rm2_project_switch_e2e.cpp`
(target/ctest `vedit-rm2-project-switch-e2e`): the REAL shell + project + file-tree
+ diagnostics lanes, switching A → B → A → B → A through the NEW top-level entry
(its folder dialog answered by the provider seam), plus an unsaved edited tab.

**3a. GREEN run** (`01…`/`12-e2e-final.log`, 125 checks, 0 failures) — quoted lines:

```
PROJECT-EXPAND-INPUTS root='…/project-a' filelists=1 variables=0 settings='…/state/settings.toml'
PROJECT-ROOT-ENV root='…/project-a' source=project pinned=1 include-dirs=1 defines=1 \
  env='-I…/project-a/a_inc -DA_ONLY=1'
RM2-START root='…/project-a' analysis-files=3 panel-files=3 imported='…/lists/a-imported.f'
RM11-NON-SOURCE path='…/project-a/sources.f' opened=1 available=0 \
  reason='not a source language; nothing to analyse' rows=0
RM11-NON-SOURCE path='…/project-a/notes.md'  opened=1 available=0 reason='not a source language; nothing to analyse' rows=0
RM11-NON-SOURCE path='…/project-a/run.tcl'   opened=1 available=0 reason='not a source language; nothing to analyse' rows=0
PROJECT-ROOT-ENV root='…/project-b' source=cleared include-dirs=0 defines=0 env=''
PROJECT-EXPAND-INPUTS root='…/project-b' filelists=0 variables=0 settings='…/state/settings.toml'
PROJECT-ROOT-ENV root='…/project-b' source=project pinned=1 include-dirs=1 defines=1 \
  env='-I…/project-b/b_inc -DB_ONLY=1'
RM2-SWITCH A-to-B to='…/project-b' from='…/project-a' expansions=3 settled=1
RM2-ENV root='…/project-b' include-dirs='…/project-b/b_inc' defines='B_ONLY=1'
… (B→A→B→A repeated; every switch: env cleared, then re-pinned with the NEW root's values)
RM2-SWITCH B-to-A-again to='…/project-a' from='…/project-b' expansions=6 settled=1
RM2-ENV root='…/project-a' include-dirs='…/project-a/a_inc' defines='A_ONLY=1'
```

Every switch asserts (all PASS in the green run): analysis set == panel set ==
persisted set, no path under the previous root in the analysis/persisted set,
neither the previous root's `-I` nor `-D` present and the new root's present,
exactly ONE expansion per switch (a second `openRoot()` for the same root is
called right after the trigger — a duplicate the old code honoured), the file
tree re-rooted (previous file unresolvable, new file present), and the unsaved
tab + edited buffer survive.

**3b. RED runs** — the same harness with the fix reverted (kept as logs):

| Negative control | Observed |
|---|---|
| drop (i) env re-pin + (iii) inputs + (iv) guard | `env-target-include-dirs dirs=''`; `analysis=1 panel=2`; `one-expansion before=1 after=3` (2 per switch); later `analysis-no-previous-file … first-outside='…/a.sv'` — 18 failures (`02-e2e-negative-control.log`) |
| drop the RM-11 registry check + the filelist-entry filter | file set = 5 files **including `notes.md` and `run.tcl`**; `rm11.not-a-source.sources.f available=1 reason=''` (the manifest WAS handed to the analyzer); 9 failures (`11-e2e-rm11-negative-control.log`) |
| `[project]` ancestor rule (Rust) | `expand_drops_an_ancestor_projects_membership_but_keeps_its_own` FAILED with the ancestor's filelist/-I/-D still applied (`03-vd-config-negative-control.log`); GREEN after restore (`04-vd-config-green.log`, 62 unit tests) |

**3c. The RM-11 unit contract** (`13-vd-config-final.log`):
`the_builtin_set_is_the_documented_hdl_set`, `a_manifest_and_non_source_files_have_no_language`,
`a_filelist_contributes_sources_never_itself_or_non_hdl_entries`,
`the_scan_extensions_are_the_registrys_source_projection` — 62 unit tests + 14 +
2 + 4 + 1 doc tests, all green.

**3d. Affected existing suites** (`14-ctest-affected.log`): app-shell, edit-core,
workspace-project, module-tree-catalog, workspace-diagnostics, b3-wiring-e2e,
rm2-project-switch — **7/7 passed, 0 failed**.

## 4. The new user-facing entry

`file.switch-project` — “S&witch Project…” / Chinese “切换工程(&W)…” — is a File-menu
command (order 7, right after open-folder) AND a main-toolbar button (order right
after open-folder). It has no shortcut of its own (open-folder has none either)
and calls `openProjectFolderDialog()`, i.e. the same `setProjectRoot()` reset path
as Open Folder; the e2e triggers the real QAction and asserts the action exists,
is on the toolbar, is in the `menu-file` menu and carries the label. The zh_CN
catalogue row was added and `bash build/gates/lrelease.sh` reports
`1212 translation(s) (1212 finished, 0 unfinished, 0 untranslated)` + PASS.

## 5. Bounds / what is NOT proven here

- **The LSP `-I`/`-D` proof is at the seam**: `setProjectEnvironment` passes the
  new lists through `selectBackend` (the ONE writer of the session's `-I`/`-D`,
  `pinned=1` in the log) and the next `start()` reads exactly those. No real
  verible/slang session is started in this harness (no toolchain configured
  offscreen), so the argv echo of a LIVE session (`commandLine()`) is not quoted.
- **The module catalog** itself is not run in the RM-2 harness (slang child). Its
  INPUT is the persisted project model, which the harness asserts is the new
  root's set with no previous project path; the catalog input is additionally
  capability-filtered. `vedit-module-tree-catalog-test` (real slang, 79 checks)
  passes in `14-ctest-affected.log`.
- **A filelist imported AFTER a switch** reaches the panel immediately and the
  analysis side at the next expansion event (the poller is one-shot). The
  reported defect (switch A↔B) is covered; closing the import-time gap needs a
  re-expand trigger on `vedit.project.inputs` — recorded as follow-up, not
  claimed here.
- **The two registry tables are mirrored** (see §2); until the ABI publishes the
  Rust table, adding a language touches one Rust row + one C++ row.
- **Harness fix**: `b3_wiring_e2e.cpp` (and the new RM-2 harness) now set
  `SIGPIPE` to ignore — AGENT.md §4 requires it for a lane selftest that links
  Rust and spawns children; without it ctest reported a `SIGPIPE` death of the
  HARNESS (not a product verdict). No assertion was relaxed.
- Not touched: `crates/vd-deeplint/**` (another lane owns it). The Rust static
  library was rebuilt from the current shared tree, so it also contains the
  other lanes' in-progress Rust changes.

## 6. Commands (observed)

```text
# Rust contract
source scripts/lib.sh && run_in_build_container cargo test -p vd-config
  -> 62 + 14 + 2 + 4 unit/integration + 1 doc test, 0 failed

# Rust FFI static lib (the C++ product links it)
run_in_build_container bash -lc 'source scripts/versions.env; cargo build -p vd-ffi --release'
  -> Finished release profile

# Qt product + the new harness
run_in_qt6_container cmake --build target/qt6-build --target vedit-app vedit-rm2-project-switch-e2e
  -> Linking CXX executable src/app/vedit-app
     Linking CXX executable …/vedit-rm2-project-switch-e2e

# the regression (headless)
run_in_qt6_container env QT_QPA_PLATFORM=offscreen … vedit-rm2-project-switch-e2e
  -> SUMMARY checks=125 failures=0   (12-e2e-final.log)
  -> SUMMARY checks=125 failures=9   (11-e2e-rm11-negative-control.log, fix reverted)

# affected suites
run_in_qt6_container ctest --test-dir target/qt6-build -R "vedit-(…)" --output-on-failure
  -> 100% tests passed, 0 tests failed out of 7

# catalogue gate
bash build/gates/lrelease.sh
  -> PASS: 0 unfinished and 0 untranslated
```

## 7. Evidence files

| file | what |
|---|---|
| `01-e2e-green.log`, `12-e2e-final.log` | final green RM-2+RM-11 run (125 checks) |
| `02-e2e-negative-control.log` | RED for (i)/(iii)/(iv) with the fixes reverted |
| `11-e2e-rm11-negative-control.log` | RED for the manifest/non-HDL analysis + filelist-entry leak |
| `03-vd-config-negative-control.log`, `04-vd-config-green.log`, `13-vd-config-final.log` | (ii) RED/GREEN, full vd-config suite |
| `14-ctest-affected.log` | 7/7 affected ctest suites |
| `05-e2e-green-repeat-2.log`, `06-e2e-green-repeat-3.log` | repeat green runs |

Raw logs contain absolute temp paths (`/tmp/vedit-rm2-project-switch-e2e-…`); they
are per-run and disposable. No secret material is present.
