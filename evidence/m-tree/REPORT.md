# T17 — module tree + signal source/target one-click jump (Qt side)

Cell `cell-20261102T050548232Z` · task **T17** · member **se-tree** · contract
`.mpd/cellwork/cell-20261102T050548232Z/contracts/module-tree.md`.
All commands below were run in the pinned Qt6/KF6 image on this tree; no gate or
test was edited. Fixture sizes are stated honestly (no 1k/10k claim — that is the
scale task T44's measurement, and the service defect T46 is inherited, see §5).

## 1. What was delivered

New lane `src/workspace/module_tree/`:

| File | Responsibility |
|---|---|
| `hierarchy_types.h/.cpp` | Plain value model of one `VdHierarchy` answer; the ONE 0-based→1-based conversion (`jumpFromEndpoint`). |
| `hierarchy_tree.h/.cpp` | Instance nesting by hierarchical path, net attachment to the longest owning instance, endpoint (driver/load/chain) child nodes; the "signal under the cursor" lookup with a deterministic ranking and an honest ambiguity reason; source/target/declaration choice + the sentence shown when there is none. |
| `hierarchy_client.h/.cpp` | The async chain: project model state → `vd_project_expand_v2` fallback → `vd_hier_start` → `vd_hier_poll`, on one `QTimer`; no GUI blocking; 30 s bound; C memory released. |
| `module_tree_panel.h/.cpp` | `QTreeWidget` of full instance paths + nets + driver/load/chain rows, filter box, Refresh, **Jump to source/target**, double-click; all jumps via `jump::jumpTo()`. |
| `module_tree_integration.h/.cpp` | Registers the `module-tree` dock spec/factory through the public `PanelRegistry`, holds the client for the shell's lifetime, reads the cursor word from the ACTIVE editor (QPlainTextEdit or KTextEditor view), watches `project-model.json` for project changes. |
| `module_tree_selftest.cpp` | The headless evidence: model checks, panel+registry checks with a recording target, the REAL service run, the `generate` fixture observation. |

Wiring: `src/workspace/integration.cpp` (+`.h`) installs it after the project
lane; `src/app/CMakeLists.txt` adds `module_tree` to `VEDIT_WORKSPACE_LANES`
(one-line lane registration, per the contract's boundary).

## 2. Acceptance matrix

| AC | Evidence (log → check) | Result |
|---|---|---|
| **3.1** module tree expands the cross-file instance hierarchy; double-click jumps an instance to `file:line` | `evidence/m-tree/product-module-tree.png` (real shell: `top` → `top.u0` → `top.u1`, nets and endpoint rows with `Location`); `10-selftest.log`: `tree.instance-nesting`, `tree.full-instance-path`, `tree.instance-jump`, `real.instance-paths` (`top, top.u0, top.u1`), `product.double-click-instance` → recorded `/…/top.sv:11:7` (1-based; ABI line 10/col 6). | PASS |
| **3.2** signal source/target jump to the exact driver/load `file:line`, cross-file | `10-selftest.log`: `signal.source` (`top.link` → `top.sv:11:38`), `signal.target` → `top.sv:12:25`, `panel.cursor-source`, `panel.cursor-target`, `panel.cursor-cross-file` (`state` → `mid.sv:10:5`); `20-product-probe.log`: `product.cursor-source` → `top.sv:11:38`, `product.cursor-target` → `top.sv:12:25`; real service: `real.signal-source`, `real.signal-target`, `real.cross-file-source` (`mid.sv:10:5`). | PASS |
| **3.4** the whole project file set, not the open file | `hierarchy_client.cpp` reads `ProjectState.files` (project model, union of `.f` imports + scan) and passes it to `vd_hier_start`; `10-selftest.log` `measurement: slang · 2 file(s) · 3 instance(s) · 14 net(s)` and `real.source-description — vd_project_expand_v2(/…/fixture)` for the no-model fallback; `product.summary-mentions-project` shows the root. | PASS |
| **3.5** data via hierarchy-service job+poll, GUI not blocked, oversize/warning surfaced | `hierarchy_client.cpp` (one QTimer, 50 ms poll, 600-tick/30 s bound, no blocking call); `HierarchySnapshot::warning` is painted in the summary line; `hierarchy.answered` checks. | PASS (fixture-scale timing: 5–60 ms; scale is T44's measurement) |
| **AC-7.3** jumps go through `jump::Registry` (no second jump path) | Every panel path calls `jump::jumpTo()`; the self-test's recording `IJumpTarget` in the real registry receives every jump (`panel.double-click-instance`, `panel.cursor-*`, `real.*`); the panel contains no file-open call. | PASS |
| Negative control: a node with no location does nothing, no crash | `10-selftest.log`: `panel.no-location-no-jump` (registry call count unchanged, status says why), `panel.null-row-safe`; `20-product-probe.log`: the same on the real tree (`product.no-location-no-jump`, `product.null-row-safe`). | PASS |
| Project root is the single source of truth; a missing root surfaces visibly | `hierarchy_client.cpp` prefers `ProjectStateStore` and only falls back to `vd_project_expand_v2`; `10-selftest.log` `panel.failure-visible` (`no project root: open a folder`), `panel.empty-state`; the product probe's first run (before `VEDIT_SLANG`) showed the missing-toolchain status line in the dock instead of an empty panel. | PASS |

## 3. Commands and observed results (this tree, pinned image)

```
cmake -S . -B target/qt6-build -G Ninja
  -> "Configuring done / Generating done", exit 0
cmake --build target/qt6-build --target vedit-app
  -> [148/148] Linking CXX executable src/app/vedit-app, exit 0
cmake --build target/qt6-build --target vedit-module-tree-selftest
  -> exit 0
ctest --test-dir target/qt6-build -R vedit-module-tree-selftest --output-on-failure
  -> 1/1 Test #29: vedit-module-tree-selftest ... Passed  0.24 sec, exit 0
```

`10-selftest.log` (lane self-test, real service): **50 checks, 0 failure(s)**,
`SELFTEST_EXIT=0`.
`20-product-probe.log` (real `ApplicationShell` + `installModuleTree`): **15
checks, 0 failure(s)**, `PROBE_EXIT=0`.
Screenshots: `product-module-tree.png` (real shell dock), `module-tree.png`
(standalone panel, Xvfb).

## 4. How to reproduce

```
# lane self-test + both fixtures (build dir inside the pinned image)
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e QT_QPA_PLATFORM=offscreen \
  -e VEDIT_MODULE_TREE_SHOT=/vedit-build/evidence/m-tree/module-tree.png vedit-qt6-build:local \
  bash -lc './target/qt6-build/src/app/workspace-lane-module_tree/vedit-module-tree-selftest \
    probes/slang-spike/fixture build/slang/out/slang "" evidence/m-tree/fixture-generate'

# product-path probe (real shell; VEDIT_SLANG is the documented override)
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e QT_QPA_PLATFORM=offscreen \
  -e VEDIT_SLANG=/vedit-build/build/slang/out/slang vedit-qt6-build:local \
  bash -lc './target/m-tree-probe/product-module-tree-probe probes/slang-spike/fixture \
    evidence/m-tree/product-module-tree.png'
```
(The product probe's manual link line is recorded in this directory's git history;
it links `libvedit_app_shell.a`, `libvedit_workspace_module_tree.a`,
`libvedit_workspace_project.a`, `libvedit_workspace_jump.a`, `libvd_ffi.a`, Qt6
Widgets/KF6.)

## 5. Honest limits — what this lane does NOT prove

1. **Tree completeness is false at scale until T46 lands.** Measured by the scale
   task: ground truth `roots=1000 / instance_nodes=11000` vs the service returning
   `5` roots' worth / `11` nodes. Service defects (T46, owner se-i18n):
   `crates/vd-deeplint/src/hierarchy.rs` walks only the FIRST root and does not
   enter `GenerateBlockArray`. This lane renders exactly what the ABI reports.
   Evidence runs on this tree record it (`10-selftest.log`):
   ```
   generate fixture: slang · 2 file(s) · 2 instance(s) · 6 net(s) · 34 ms · 19688 KiB peak
   observed instance paths: gen_top, gen_top.u_bank
   observed 2/5 ground-truth instance(s); missing: gen_top.u_bank.g[0].u_leaf,
     gen_top.u_bank.g[1].u_leaf, other_top
   note: the gap is T46's … not the UI consumer's
   ```
   Fixture `evidence/m-tree/fixture-generate/` (two roots + a generate loop).
   Re-run this section when T46 lands.
2. **Scale**: no 1k-file/10k-instance claim. Measured only at fixture size
   (2 files / 3 instances / 14 nets; 5–60 ms, ~19.6 MB peak). T44 owns scale.
3. **Packaging**: the `slang` binary is not this lane's artifact; the probe used
   `build/slang/out/slang` and the documented `VEDIT_SLANG` override.
4. **LSP route not wired** (T7's bound, unchanged): the shipped data path is the
   CLI/AST-JSON elaboration, not an LSP request.
5. **End-to-end file opening in the product depends on T20's target**: no product
   code registers the REAL `IJumpTarget` yet (T20's contract: "把真实
   IJumpTarget 注册进 jump 注册表 … 使 outline/诊断行/模块树/工程文件列表/搜索/
   文件树/面包屑实际跳转"). Until that lands, `jump::jumpTo()` from this panel
   reports `NoTarget` and the panel says so in its status line; the probes prove
   delivery with a recording target in the same registry. This lane adds no second
   file-open path.
6. **UVM / dynamically generated designs** are outside the service's guarantee
   (slang static elaboration).
7. The `module-tree` dock is shown by `installModuleTree()` on first run; its
   View-menu command is T20's (no `view.panel.module-tree` action exists yet).

## 6. Files changed (all inside the contract's scopes)

`src/workspace/module_tree/*` (new lane: 6 sources + CMake + README),
`src/workspace/integration.{h,cpp}` (install call + guard),
`src/app/CMakeLists.txt` (one line: lane registration),
`evidence/m-tree/*` (this report, logs, fixtures, probes, screenshots).

## 7. T49 — real-event coverage for double-click and the two jump buttons (plus a fail-first control)

**A trap worth knowing:** in this Qt build `QTest::mouseDClick()` alone cannot make
`QAbstractItemView` emit `doubleClicked()` — a future probe written the obvious way
would silently test nothing (no failure, no jump) and still look green. Always send
the press that sets `pressedIndex`, or verify the signal, not just that the helper
returned.

**What the red proves:** the same injected disconnection turned the lane self-test
red (5 failures) *and* the product-path probe red (3 failures) — the probe going red
is what proves the real `ApplicationShell` wiring, not merely that the test calls the
method; after restoring with the injection removed both went to 51/0 and 16/0.

Before T49 the panel's double-click and the two cursor-button connections were
only exercised by calling `activateItem()` / `jumpToSource()` / `jumpToTarget()`
directly, so a broken Qt `connect` would not have failed any check. T49 replaces
those with real input events:

- **lane self-test** (`src/workspace/module_tree/module_tree_selftest.cpp`):
  `QTest::mousePress` + `QTest::mouseDClick` + `QTest::mouseRelease` on
  `panel.tree()->viewport()` at `panel.tree()->visualItemRect(item).center()`,
  and `QTest::mouseClick` on the `module-tree-jump-source` /
  `module-tree-jump-target` tool buttons (`findChild<QToolButton*>` by object
  name). The extra press is required because this Qt is built WITHOUT
  `QTEST_QPA_MOUSE_HANDLING`: `QTest::mouseDClick` then delivers the
  `MouseButtonDblClick` event alone (no synthesised press), and
  `QAbstractItemView` emits `doubleClicked()` only when its `pressedIndex` is the
  row under the cursor. The event TYPES sent are exactly a real double click's
  (press, release, dblclick, release). This is a property of the test harness,
  not of the product: no product wiring bug was found.
- **product-path probe** (`evidence/m-tree/probe/product_module_tree_probe.cpp`):
  the same events against the real `ApplicationShell` dock.

Checks (green logs): `panel.event.row-clickable`, `panel.event.double-click-instance`
→ `top.sv:11:7`, `panel.event.double-click-signal-declaration` → `top.sv:11:38`,
`panel.event.button-source` → `top.sv:11:38`, `panel.event.button-target` →
`top.sv:12:25`, `panel.event.button-source-cross-file` → `mid.sv:10:5`,
`panel.event.no-location-double-click` (no jump, no crash). The product probe
mirrors them as `product.event.*` plus `product.event.row-clickable`.

### Fail-first control (two logs kept, fault removed afterwards)

| phase | log | observed |
|---|---|---|
| RED — `itemDoubleClicked` + both button connections disconnected (`#if 0` in `module_tree_panel.cpp`) | `evidence/m-tree/41-qtest-fail-first.log` | **50 checks, 5 failure(s), exit 1** — `panel.event.double-click-instance`, `panel.event.double-click-signal-declaration`, `panel.event.button-source`, `panel.event.button-target`, `panel.event.button-source-cross-file` |
| RED — same fault, product probe | `evidence/m-tree/43-product-fail-first.log` | **16 checks, 3 failure(s), exit 1** — `product.event.double-click-instance`, `product.event.button-source`, `product.event.button-target` |
| GREEN — wiring restored, selftest | `evidence/m-tree/40-qtest-events.log` | **51 checks, 0 failure(s), exit 0** |
| GREEN — wiring restored, product probe | `evidence/m-tree/42-product-events.log` | **16 checks, 0 failure(s), exit 0** |

The fault injection was removed after the RED run; the tree contains no test
hook (`grep 'T49 FAULT' src/workspace/module_tree/module_tree_panel.cpp` → no
match), all three `QObject::connect` calls are back at their original lines, and
`ctest -R vedit-module-tree-selftest` → **1/1 passed, exit 0** on the restored
tree. Previous coverage is not regressed: every earlier check still passes (the
selftest total moved 50 → 51 by the added `panel.event.row-clickable`; the probe
15 → 16 by `product.event.row-clickable`).
