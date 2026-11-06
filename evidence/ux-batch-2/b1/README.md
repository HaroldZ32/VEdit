# UX batch 2 — lane B1 evidence (pure module tree + Interface panel)

Task: items 1 and 2 of the user's batch. Contract: `.mpd/plans/ux-batch-2/00-plan.md`
§4 `### B1`, plus §1 (measured facts) and §2 (known limits).

Write scope actually touched: **`src/workspace/module_tree/**` only**.
`src/workspace/integration.cpp` already registered `installModuleTree()` in this
batch's working tree and was **not modified by this lane**; the two docks are
registered from inside the lane (`module_tree_integration.cpp`), so no
registration edit was needed. No file under `src/app/**`, `src/editor/**`,
`src/workspace/search/**`, `src/workspace/diagnostics/**`, `crates/**` or
`docs/USER_GUIDE.md` was modified.

## 1. What changed (path + symbol)

| Path | Symbol / change |
|---|---|
| `src/workspace/module_tree/module_catalog.{h,cpp}` | **new.** Plain model of the ABI: `ModuleCatalog`, `ModuleDefinition`, `ModulePort`, `ModuleInstance`; `portWidthFromType()`; `moduleAtCaret()` (focus rule b); `instantiationsOf()`; `jumpFromDefinition/Port/Instance()` (the ONE 0-based→1-based conversion). |
| `src/workspace/module_tree/module_hierarchy.{h,cpp}` | **new.** `buildModuleTree()`: roots = `is_top` modules, children = instantiated modules, one row per distinct child with `instantiationCount`; a module with two parents appears twice. Guards: `ModuleRowKind::{Cycle,DepthLimit,Truncated}` visible markers, `kModuleTreeMaxDepth = 64`, `kModuleTreeMaxRows = 20000`. |
| `src/workspace/module_tree/catalog_client.{h,cpp}` | **new.** `CatalogClient`: `vd_catalog_start` / `vd_catalog_poll` / `vd_catalog_release` / `vd_module_catalog_free`, 50 ms `QTimer` poll, project-model file set with `vd_project_expand_v2` fallback. slang never runs on the GUI thread. Normalises relative ABI source paths against the project root. |
| `src/workspace/module_tree/module_tree_panel.{h,cpp}` | **rewritten.** `ModuleTreePanel`: columns `Module / Instantiations / Definition`; double-click a module row → `jump::jumpTo()` at its definition; filter; selection callback; one-line empty/failure states. **No net/driver/load/chain row exists in the model or the panel.** |
| `src/workspace/module_tree/interface_panel.{h,cpp}` | **new.** `InterfacePanel`: columns `Name / Direction / Type / Width / Location`; width from the verbatim `type` string (empty + tooltip when unparsable); focus (a) tree selection / (b) caret module; port double-click → declaration; Ctrl+double-click → instance statement (chooser for several parents); ABI-limitation tooltip. |
| `src/workspace/module_tree/module_tree_integration.{h,cpp}` | **rewritten.** `ModuleTreeHolder` owns the client, selection, a 250 ms caret poll (editor/pane switch) and the 1 s project-model watch; registers **two docks**: `module-tree` and `interface-panel` (both right dock area); pushes catalog/selection/caret/no-project state into recreated docks. |
| `src/workspace/module_tree/module_tree_selftest.cpp` | **rewritten.** Deterministic model + panel checks (loop ctest id `vedit-module-tree-selftest` kept, no slang needed). |
| `src/workspace/module_tree/module_tree_catalog_test.cpp` | **new.** Offscreen acceptance test `vedit-module-tree-catalog-test`: real slang, real product client, real KTextEditor jump target. |
| `src/workspace/module_tree/fixtures/ux-batch-2/` | **new fixture project** (7 `.sv` files): `shared` instantiated by **both** `top_a` and `top_b`, `orphan` never instantiated, `bus_if` an interface, `reflink` a `Ref` port. |
| deleted `hierarchy_types.*`, `hierarchy_tree.*`, `hierarchy_client.*` | the old elaborated instance/net/driver/load model is **removed**, not hidden. |
| `src/workspace/module_tree/CMakeLists.txt`, `README.md` | sources/tests regrouped; lane README rewritten. |

## 2. Acceptance matrix (plan §B1 ①–⑥) — observed output

Real run: `vedit-module-tree-catalog-test` with `build/slang/out/slang`, product
`ApplicationShell`, the product's `EditorJumpTarget` and a real KTextEditor
document. Full log: `logs/02-catalog-test.log` (offscreen) and
`logs/04-xcb-screenshot-run.log` (Xvfb/xcb).

| # | Requirement | Observed (`ok` line from the log) |
|---|---|---|
| ① | root set == never-instantiated modules | `acceptance.i.roots-equal-is-top — roots=[orphan, top_a, top_b] tops=[orphan, top_a, top_b]`; `acceptance.i.roots-fixture-set — orphan, top_a, top_b` |
| ② | module with two parents appears twice | `acceptance.ii.shared-under-both-parents — top_a.u_shared, top_b.u_shared`; `acceptance.ii.distinct-instance-paths — top_a.u_shared, top_b.u_shared` |
| ③ | double-click lands the cursor on the declaration | `acceptance.iii.file-opened — …/fixtures/ux-batch-2/top_a.sv`; `acceptance.iii.cursor-on-declaration — cursor=1:8 expected=1:8`; `acceptance.iii.declaration-text — module top_a (input logic clk, inout wire pad, output logic [3:0] y);` |
| ④ | no row represents a net/driver/load | `acceptance.iv.only-module-rows — rows=7`; `acceptance.iv.row-count — rows=7`; `acceptance.iv.widget-row-count — rows=7`; `acceptance.iv.no-net-labels — orphan, top_a, shared, consumer, reflink, top_b, shared` |
| ⑤ | interface panel direction/type/width correct | `acceptance.v.port-clk — clk / In / logic / 1`; `acceptance.v.port-pad — pad / InOut / 1`; `acceptance.v.port-y — y / Out / logic[3:0] / 4`; `acceptance.v.location-column — top_a.sv:1:27` |
| ⑥ | port double-click → declaration; Ctrl+double-click → instantiation | `acceptance.vi.declaration-file — …/top_a.sv`; `acceptance.vi.declaration-cursor — cursor=1 expected=1`; `acceptance.vi.chooser-two-parents — candidates=2`; `acceptance.vi.chooser-parents — top_a, top_b`; `acceptance.vi.instantiation-file — …/top_b.sv`; `acceptance.vi.instantiation-cursor — cursor=2 expected=2` |

Extra checks in the same run (deliverable details):

* focus rule (b) through the product holder: `acceptance.focus.caret-module — top_a`;
  selection beats caret: `acceptance.focus.selection-beats-caret — shared`;
* project-root change clears both panels: `acceptance.root-change.tree-empty-state — No project open — …`
  and `acceptance.root-change.interface-empty-state — No project open — …`;
* a root switch **while a catalog run is in flight** cancels it, so a late answer
  cannot paint the previous project under the new root:
  `acceptance.root-change.in-flight-started`, `…in-flight-cancelled`,
  `…no-late-catalog`, `…no-stale-panel` (regression found and fixed in this lane);
* the ABI limitation is in the panel tooltip:
  `acceptance.v.abi-limit-tooltip — … The module catalog has no per-connection position, so Ctrl+double-click targets the instance statement, not the connected port.`

Deterministic model checks (`logs/01-selftest.log`, 76 checks) additionally prove:

* cycle guard: `tree.cycle.visible-marker — rows=3`, `tree.cycle.terminates — rows=3`,
  `tree.cycle.marker-note — cycle: A is already an ancestor of this row — not expanded`;
* depth cap: `tree.depth.capped-at-64 — deepest=64`,
  `tree.depth.marker-note — depth limit 64 reached — this module's children are not expanded`,
  `tree.depth.rows-bounded — rows=65`;
* width derivation: `width.logic-4`, `width.no-dimension-1`, `width.interface-1`,
  `width.reversed-range-4`, `width.multi-dimension-8`, `width.spaces-8`,
  `width.parameterised-unknown — the packed range \`[WIDTH-1:0]\` … cannot be derived`,
  `width.empty-unknown`;
* focus rule units: `focus.nearest-above`, `focus.exact-definition-line`,
  `focus.before-first-definition-none`, `focus.other-file-none`;
* chooser: `instantiation.two-parents`, `instantiation.sorted`, `instantiation.top-has-none`.

## 3. Commands and observed results

All commands run from the repository root on the pinned toolchain
(`vedit-qt6-build:local`, Ubuntu 26.04 / Qt 6.10.2 / KF6 6.24.0; see
`logs/07-environment.txt`).

| Command | Observed result | Log |
|---|---|---|
| `bash scripts/build-qt.sh` | `100% tests passed, 0 tests failed out of 43`; `[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke`; `M0-SMOKE ok=1 platform=xcb …` | `logs/05-build-qt.log`, `logs/06-full-ctest-and-build-summary.log` |
| `ctest --test-dir target/qt6-build -R '^vedit-module-tree-selftest$' -V` | `PASS: 76 check(s), 0 failure(s)`, exit 0 | `logs/01-selftest.log` |
| `ctest --test-dir target/qt6-build -R '^vedit-module-tree-catalog-test$' -V` | `PASS: 56 check(s), 0 failure(s)`, exit 0 | `logs/02-catalog-test.log` |
| `ctest --test-dir target/qt6-build -N` | `Total Tests: 43` (was 42; lane added `#34 vedit-module-tree-catalog-test`, kept `#33 vedit-module-tree-selftest`) | `logs/03-ctest-list.log` |
| Xvfb/xcb run: `DISPLAY=:77 QT_QPA_PLATFORM=xcb … vedit-module-tree-catalog-test <fixture> <slang> evidence/…/b1-panels.png` (inside `vedit-qt6-build:local`, `Xvfb :77 -screen 0 1600x1000x24`) | `PASS: 57 check(s), 0 failure(s)`; `screenshot.saved`; `SHOT …/b1-panels.png` | `logs/04-xcb-screenshot-run.log` |

Screenshot: `screenshots/b1-panels.png` (1400x900 PNG, software GL llvmpipe).
It shows the Module Tree dock (roots `orphan`, `top_a` with `shared/consumer/reflink`,
`top_b` with the second `shared`, `Definition` column) and the Interface dock
(`Module top_a · 3 port(s)`; `clk / In / logic / 1`, `pad / InOut / logic / 1`,
`y / Out / logic[3:0] / 4`).

Note on build contention: none of the full `build-qt.sh` gate runs needed the
contract's "wait 90 s and retry" path (three full runs during this lane, all
exit 0; the last one is `logs/05-build-qt.log`). The only failures seen while
building the lane were the lane's OWN compile errors, fixed before the gate runs.
Ninja printed its usual `warning: premature end of file; recovering` for the
shared `.ninja_log` (concurrent lanes touching the same build tree) but the
build completed with exit 0.

## 4. What is NOT proven / known limits (honest)

1. **User-guide sentence about the instantiation jump is NOT in the user guide.**
   `docs/USER_GUIDE.md` is outside this lane's write scope (the captain
   consolidates it). The honest ABI limitation ("no per-connection position, so
   Ctrl+double-click targets the instance statement") is in the panel tooltip and
   the lane README; the user-guide paragraph is left to the captain's integration
   step.
2. **New UI strings are not translated yet — and "every string is wrapped" was
   NOT true when this lane first landed.** The B1 review found user-visible
   strings outside `tr()`; `evidence/ux-batch-2/b1-repair/REPORT.md` §M2 then
   wrapped all of them with `QCoreApplication::translate("VEdit", …)`, and the
   batch's integration step completed the zh_CN catalogue
   (`translations/vedit_zh_CN.ts`: 1018 finished / 0 unfinished). The "every
   string is wrapped" statement therefore holds only for the repaired tree, not
   for the state this README originally described. This lane did not touch
   `translations/**`.
3. **Chooser UX is not driven through a modal dialog in the test.** The test
   injects the deterministic `setInstantiationChooser()` seam and asserts the
   candidate list (2 parents) and the selected target; the production path
   (`QMenu::exec` listing `parent · path · file:line`) is code-reviewed, not
   clicked, because an offscreen modal would block. Both the injected seam and the
   production menu build the same candidate vector from
   `instantiationsOf()`.
4. **"slang never on the GUI thread" is architectural, not instrumented.** The
   client uses the ABI's async job (`vd_catalog_start` returns a job id; a 50 ms
   `QTimer` polls `vd_catalog_poll`), exactly like the existing `vd_hier_*`
   client. No thread-id assertion is made in the test.
5. **The ABI positions are 0-based, not 1-based.** The task text said
   "file,line,column, 1-based", but `crates/vd-ffi/include/vd_ffi.h` documents
   every catalog position as **0-based** and the Rust builder converts slang's
   1-based source positions with `saturating_sub(1)`. The consumer follows the
   header + `crates/vd-ffi/tests/ux_batch_2.rs` and converts exactly once in
   `jumpFromPosition()`. Flagged as a contract-text discrepancy for the reviewer.
6. **No automated assertion for a live project-root switch to a DIFFERENT
   non-empty project** (that requires a second slang run and a second fixture).
   The switch to "no project" is asserted end-to-end
   (`acceptance.root-change.*`); the non-empty branch is the same `refresh()`
   path covered by the initial load.
7. Screenshot rendering is Xvfb + llvmpipe inside the pinned image, not a real
   desktop; the user's visual acceptance remains the final word (plan §2).

## 5. Evidence files

```
evidence/ux-batch-2/b1/
├── README.md                        (this file)
├── logs/
│   ├── 01-selftest.log              ctest -V #33, 76 checks, PASS
│   ├── 02-catalog-test.log          ctest -V #34, 56 checks, PASS (real slang + real KTextEditor)
│   ├── 03-ctest-list.log            ctest -N: 43 tests, ids #33/#34
│   ├── 04-xcb-screenshot-run.log    Xvfb/xcb acceptance run, 57 checks, SHOT line
│   ├── 05-build-qt.log              full `bash scripts/build-qt.sh` output (final state)
│   ├── 06-full-ctest-and-build-summary.log  extracted 43/43 + gate lines
│   └── 07-environment.txt           host/image/toolchain/slang/screenshot facts
└── screenshots/
    └── b1-panels.png                1400x900, module tree + interface dock
```
