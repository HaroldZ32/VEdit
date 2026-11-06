# Review — UX batch 2, lane B1 (pure module tree + Interface panel)

- Contract: `.mpd/plans/ux-batch-2/00-plan.md` §`B1`, with §1 (measured facts) and §2 (bounds)
- Deliverable under review: `evidence/ux-batch-2/b1/` (README, `logs/01..07`, `screenshots/b1-panels.png`)
- Review date: 2026-11-05
- Reviewer: independent; findings only, no source/test/gate edits
- **Risk level: HIGH**
- **Verdict: FAIL**

## Assessment

The headline B1 outcomes are real. The tree is a module hierarchy, not a reskinned instance tree:
`ModuleCatalog` is keyed by `InstanceBody` definitions, `buildModuleTree` uses the `is_top` set as
roots and groups a parent's `children` by module name, the fixture's `shared` appears under both
`top_a` and `top_b` with distinct instance paths, every row kind is either a module or one of the
three visible guards, and the old `hierarchy_*` sources are gone (not merely unreachable). The
Interface panel really is fed by the product path: `InterfacePanel::renderPorts` calls
`portWidthFromType`, and the real-slang run shows `clk`/`pad` = 1 and `y = logic[3:0]` = 4; the
parameterised case is blank plus a tooltip. Both double-click gestures go through
`jump::jumpTo`/`EditorJumpTarget`, not a private file-open, and the real KTextEditor cursor lands on
1:8 for `top_a` and the instantiation at `top_b.sv:2:10`. The 0/1-based conversion is correct: the
ABI header is 0-based, the Rust builder subtracts 1, the C++ adds 1 once per path; I found no
off-by-one. The writer's 43/43 ctest + xcb evidence is coherent, and I independently re-ran the
deterministic self-test (76/76) and the real catalog test in the pinned image (56/56).

The failure is elsewhere: the module catalog can be built from the **wrong file set on the first
project open** (and after a root switch), because `ModuleTreeHolder::checkProjectModel` consumes the
project model's first appearance and every change it sees while a catalog run is in flight. On a
fresh state directory the holder's initial `QTimer::singleShot(0, refresh)` starts the root-scan
fallback before the project lane's asynchronous expansion saves `project-model.json`; the first
watch observation then records the file's mtime/size and returns without a refresh. Imported `.f`
file lists, `-I` and `-D` are therefore missing from the module tree until the user presses
Refresh. The acceptance test cannot catch this because its state directory is empty and it only
exercises the fallback branch. Secondary findings are the partially-untranslated UI (some strings
are not even wrapped in `tr()`), a new acceptance test that exits 0 with zero checks when its
fixture/slang is missing, and a declaration-jump test that uses a never-instantiated module and so
cannot distinguish a declaration jump from an instance jump.

## Findings (ranked)

### High 1 — the project-model watch drops the model's first appearance and any change seen during a run

**Where:** `src/workspace/module_tree/module_tree_integration.cpp`, `ModuleTreeHolder::checkProjectModel`
(and `ModuleTreeHolder::ModuleTreeHolder`, `ModuleTreeHolder::refresh`); `src/workspace/module_tree/catalog_client.cpp`,
`CatalogClient::start` / `CatalogClient::beginProjectFallback`.

**What happens:** `checkProjectModel` records `modelModified_` and `modelSize_` *before* the
`if (first || client_->isRunning()) return;` guard. The first-ever observation of
`project-model.json` is always consumed without a refresh; a change observed while a catalog job is
running is likewise consumed without a refresh after the job finishes. The holder starts its first
catalog run at `QTimer::singleShot(0, …)`, but the project lane opens its root and expands
asynchronously, saving the model only when its expansion completes (`ProjectModel::adopt` →
`saveState`); the module-tree lane's own failure comment acknowledges that "the first attempt can
legitimately find no persisted project model yet". When the model did not exist at `start()`,
`CatalogClient::start` takes `beginProjectFallback`, which expands the root with no imported file
lists, `-I` or `-D`; the first watch tick then records the newly written model and never re-runs.
The final tree is the root-scan catalog, not the project model's effective file set.

**Why it matters:** the plan's B1 tree is supposed to be the project's module hierarchy, and the
lane README calls the persisted project model "the ABSOLUTE source of truth for the project root and
for the effective file set". A first open (the normal case for a new user or a fresh state dir) can
therefore omit modules from imported file lists or elaborate without the project's include
defines/dirs; the interface panel follows the same incomplete catalog. There is no crash and
Refresh repairs it, but the default deliverable is wrong for those projects and silently so.

**Evidence:** `CatalogClient::start` only uses the persisted state when it exists and has a matching
root and a non-empty file list; otherwise it calls `beginProjectFallback`, whose comment says it
passes no file lists/settings. `ModuleTreeHolder::checkProjectModel` sets the stamps before the
`first || isRunning` return. The acceptance test creates a `QTemporaryDir stateDir` and never seeds
`project-model.json`, so `catalog.answered`/`catalog.module-count` in `logs/02-catalog-test.log`
exercise only the fallback branch; the README's "What is NOT proven" section names the root-switch
case but not this one.

**Direction of a fix:** keep a `modelDirty_` (or a "run used the fallback" flag); on a model
observation that is first or that happens while running, set the flag instead of only stamping it,
and when the catalog run reports ready/failed, schedule one more run if the flag is set. Track the
model stamp the current run actually read, so an unchanged model is not re-run forever. Add a
regression test that seeds a `project-model.json` whose file set differs from the root scan (e.g. a
file only in the model or a project `-I`/`-D`) and asserts the tree/interface use it, plus a test
that writes the model while a run is in flight.

### Medium 2 — new user-visible strings: the deferred `tr()` strings are an acceptable lane bound only with the captain, but several strings are not wrapped at all

**Where:** `src/workspace/module_tree/module_catalog.cpp` (`ModuleCatalog::summaryLine`,
`portWidthFromType` problem strings), `module_tree_panel.cpp` (`ModuleTreePanel::deliver`,
`ModuleTreePanel::rowLabel` markers, refresh fallback), `interface_panel.cpp`
(`InterfacePanel::deliver`, width tooltip), `catalog_client.cpp` (`CatalogClient::fail` and
`beginCatalog` failure messages); `translations/vedit_zh_CN.ts`.

**What happens:** the new headers are wrapped in `QCoreApplication::translate("VEdit", …)`, but
`translations/vedit_zh_CN.ts` has no entry for `Module`, `Instantiations`, `Definition`,
`Direction`, `Width`, `Name` or `Type`; the old module-tree entries in the file still carry the pre-rewrite locations and
texts (`hierarchy: retrying…`, `No hierarchy yet…`). The writer's README says every string is
wrapped, which is not true: status lines in both `deliver` implementations, the whole
`ModuleCatalog::summaryLine`, the width-problem tooltips, the "nothing to refresh" fallback and
several `CatalogClient::fail` messages are raw `QStringLiteral` English. Those will never be
extracted by `lupdate`, so the captain's translation step cannot fix them.

**Why it matters:** the Chinese user gets a mixed UI even after integration: the deferred headers
can be translated in the captain's step, but the unwrapped statuses/summary/tooltip text cannot.
The plan's §5.1 translation checklist names the A2/A4 strings but not B1's, so nothing currently
owns adding and translating B1's new strings. (The deferral of the *wrapped* strings itself is a
legitimate lane bound: `translations/**` belongs to A2 during the batch and the plan's final gate
requires `lrelease` with 0 unfinished — provided the captain actually runs `lupdate` and adds B1 to
the checklist.)

**Direction of a fix:** wrap every remaining user-visible status/summary/tooltip string in
`QCoreApplication::translate("VEdit", …)` (including the width-problem text), and add B1's new
strings to the §5.1 translation-checklist ownership. Do not claim "every string is wrapped" in the
evidence README.

### Medium 3 — the real acceptance test exits 0 with zero checks when its fixture or slang is missing

**Where:** `src/workspace/module_tree/module_tree_catalog_test.cpp`, `main` (the
`skip("catalog.real", …)`/`return 0` branch); `src/workspace/module_tree/CMakeLists.txt`,
`add_test(vedit-module-tree-catalog-test …)` (no `SKIP_RETURN_CODE`).

**What happens:** ctest #34 is registered with the in-tree fixture path and
`build/slang/out/slang`; if either is absent, CMake passes empty strings, the test prints
`SKIP catalog.real — …` and `SKIP: 0 check(s) were not run`, and returns 0. ctest reports
"Passed". I reproduced this with nonexistent paths: `0 check(s)` and exit code 0. The writer's
`logs/02-catalog-test.log` is a real run and I re-ran it successfully, but the *gate* itself can be
green with the whole acceptance suite skipped.

**Why it matters:** the plan's final gate is "ctest数量只增不减" and the real-slang test is the
only automated proof that the product client, the fixture and KTextEditor agree. A missing
`build/slang/out/slang` in a future image would silently remove that proof while the gate stays
green.

**Direction of a fix:** for the product build the fixture is in-tree and slang is a build
dependency, so a missing dependency should fail the test (return non-zero), or at minimum use
ctest's `SKIP_RETURN_CODE` and make the gate treat skipped as not-green. Also assert
`g_checks > 0` before returning success.

### Medium 4 — the declaration-jump acceptance cannot distinguish a declaration from an instantiation

**Where:** `src/workspace/module_tree/module_tree_catalog_test.cpp`, `acceptance.iii` (the tree
double-click uses `top_a`); `src/workspace/module_tree/fixtures/ux-batch-2/top_a.sv` (`top_a` is
never instantiated); `shared.sv` (instantiated by both `top_a` and `top_b`).

**What happens:** the test double-clicks the `top_a` root and asserts the cursor opens `top_a.sv`
on the line containing `module top_a`. Because `top_a` has no instantiation edge, this cannot catch
the regression the user cares about — a module tree row opening the **instance statement** instead
of the **definition**. The discriminating fixture case, `shared`, is selected for the interface
panel and used for Ctrl+double-click, but its tree row is never double-clicked. The real test also
checks only the line (not the column) for the port declaration, while the column is asserted only
by the synthetic self-test.

**Why it matters:** the frozen B1 item 1 explicitly says the module row opens the definition, and
the old hierarchy tree opened the instance site; the evidence should prove the distinction on a
module that has both a definition and instantiations. I read the code path
(`ModuleTreePanel::addRow` fills the row from `ModuleTreeNode::definitionFile/Line`, and
`buildNode` takes those from the child's `ModuleDefinition`), so I believe the behavior is correct
for `shared` — but the acceptance evidence does not establish it.

**Direction of a fix:** add a tree double-click on the `shared` row and assert `shared.sv:1` (the
definition), not `top_a.sv`/`top_b.sv` instance lines; keep the `top_a` check as a secondary case.
Optionally assert the real port-declaration column, not just the line.

### Medium 5 — the persisted-project-model path is not exercised by the acceptance test

**Where:** `src/workspace/module_tree/module_tree_catalog_test.cpp`, `main` (`QTemporaryDir
stateDir`, no `ProjectStateStore::save` / `project-model.json` seed); `CatalogClient::start`
(persisted branch vs `beginProjectFallback`).

**What happens:** the test's state directory is empty, so the catalog always takes the fallback
root-scan branch. The normal post-open path — a `project-model.json` with the effective file set,
imported `.f` lists, `-I`/`-D` — is not exercised at all. That is exactly the path where High 1
lives, and it is also absent from the README's "What is NOT proven" list.

**Why it matters:** the lane's README and `CatalogClient` claim the project model is the source of
truth, but the only automated evidence for the client is the fallback path.

**Direction of a fix:** seed a `project-model.json` in the temp state dir with a file set that
differs from the root scan and assert the catalog's module set reflects it; add the in-flight model
write case from High 1.

### Medium 6 — the B1 contract's user-guide sentence is missing and no integration step owns it

**Where:** `.mpd/plans/ux-batch-2/00-plan.md` §`B1` ("这一点写进面板 tooltip 与用户手册");
`docs/USER_GUIDE.md` (no module-tree/interface sentence); `evidence/ux-batch-2/b1/README.md` item 1
(admits it); plan §5.1 (the captain/A2 checklist does not name B1's docs).

**What happens:** the tooltip side is present and asserted
(`InterfacePanel::activationTooltip`), but the user-manual sentence — "the catalog has no
per-connection position, so Ctrl+double-click targets the instance statement, not the connected
port" — is not in the user guide. The lane's write scope excludes `docs/USER_GUIDE.md`; A2's scope
includes it, but no integration step assigns B1's sentence.

**Why it matters:** the frozen contract makes the user-manual statement part of B1's honest
deliverable; a tooltip alone is not the user manual. This is a contract/ownership gap rather than a
code bug in `src/workspace/module_tree/**`, but it must be carried explicitly.

**Direction of a fix:** assign the docs sentence to A2/captain's integration step, or amend the
plan to name the owner; do not mark B1 complete on the tooltip alone.

### Nit 7 — "ONE 0-based→1-based conversion" is not literally true

**Where:** `src/workspace/module_tree/module_catalog.h` (the `jumpFrom...()` conversion comment);
`src/workspace/module_tree/module_tree_panel.cpp`, `ModuleTreePanel::addRow` and
`ModuleTreePanel::activateItem`; `InterfacePanel` uses `jumpFromPort`.

**What happens:** the interface panel converts through `jumpFromPort`/`jumpFromInstance`, but the
tree panel performs its own `definitionLine + 1` / `definitionColumn + 1` when filling the item
roles and constructs a `ModuleJump` directly in `activateItem`. `describePosition` also converts
for display. I found no off-by-one: the real log `cursor=1:8`, port `line=1 column=67`, instance
`line=2 column=10`, and the self-test's `jump.one-based`/`panel.activate.target` all agree. The
conversion is applied once per path, but it is not centralized as the header claims; a later change
to `jumpFromPosition` (e.g. clamping) would be bypassed by the tree path.

**Direction of a fix:** have `addRow`/`activateItem` use `jumpFromDefinition` (and keep a single
conversion helper), or correct the comment/evidence claim rather than claiming a single seam.

### Nit 8 — the production chooser menu is exercised only through a test seam

**Where:** `src/workspace/module_tree/interface_panel.cpp`, `InterfacePanel::jumpToInstantiation`
(`QMenu::exec` production path); `module_tree_catalog_test.cpp` uses
`setInstantiationChooser`; README item 3 discloses this.

**What happens:** the test proves the candidate list (two parents) and the selected target, but the
actual modal `QMenu` path is not driven. I read it and found no defect (`action->setData(candidate)`
→ `chosen->data().toInt()`; single-parent case uses index 0), but the acceptance evidence is
seam-only.

**Direction of a fix:** add a deterministic menu-level test or a small UI double; not a blocker on
the code, but it is a gap in the "chooser when several parents" evidence.

### Nit 9 — guard-label text uses the compile-time constant, not the effective `ModuleTreeOptions`

**Where:** `src/workspace/module_tree/module_tree_panel.cpp`, `ModuleTreePanel::rowLabel`; the
corresponding markers are built in `buildModuleTree` with the effective `state.maxDepth`/`maxRows`.

**What happens:** with non-default options the marker note says "depth limit 64" / "tree truncated
(20000 rows)" even if the builder stopped at a different cap. Production uses the defaults (64 /
20000) and the self-test uses the defaults, so this is not user-facing today; it becomes wrong if
the options are ever wired to a setting or a test uses a smaller cap.

**Direction of a fix:** carry the effective cap in the marker's note/row, or document that only the
default options are supported.

## Declined to judge

- **Old instance-tree source removal:** verified the `hierarchy_*` sources are absent from
  `src/workspace/module_tree/`, `build.ninja` has 0 references, and only stale `.o` objects remain
  from a previous build; the `vd_hierarchy_*` ABI/Rust hierarchy report still exists for other
  consumers. Pass for B1's UI scope; not a finding.
- **Width of unpacked dimensions:** `portWidthFromType` scans every `[...]` as a packed range; there
  is no evidence the ABI `type` string can carry unpacked dimensions, and the contract/fixture only
  cover packed ranges. Declined without evidence.
- **Project-job release on failure/timeout:** `vd_project_poll` has no `vd_project_release` in the
  header; the catalog client's abandoned project job is a service-lifetime concern, not judged.
- **Ref-port UI display:** the fixture's `reflink` Ref port is not selected/asserted by the B1 test;
  the panel displays the ABI direction string verbatim, so the risk is low. Not judged.
- **Stale `.ts` entries for old hierarchy strings:** `translations/**` is A2/captain's write scope
  and the plan's integration step must regenerate it; attributed to the translation carry, not B1.
- **`sameFile` canonical-path cost on every caret line change:** not measured at project scale; the
  caret poll only refreshes on file/line changes. Not judged.
- **Visual quality / dark theme:** the screenshot is a real 1400x900 Xvfb/llvmpipe capture and shows
  both docks; the plan leaves final visual acceptance to the user. Declined.
- **AppImage / `--selfcheck` / clippy / fmt / docs / lrelease gates:** outside B1's lane; the final
  integration step owns them. Not run here (see Bounds).

## What the evidence establishes (the questions asked)

- **Tree module-level?** Yes. `ModuleCatalog` is definition-keyed; `buildModuleTree` roots = `is_top`,
  children = instantiated modules; a two-parent module appears twice; only three guard row kinds
  exist; the real fixture row count is 7 and the widget row count is 7; the self-test's negative
  control forbids `u_shared`/net labels. The old `hierarchy_*` sources are gone.
- **Double-click declaration and single jump seam?** The code path is real and uses
  `jump::jumpTo`; the real KTextEditor log shows `line=1 column=8 (cursor=1:8)` on `module top_a`
  and the instance target `top_b.sv:2:10`. The acceptance test's `top_a` choice is not a
  discriminating case for a module with instantiations (Medium 4).
- **Interface panel focus and width?** Yes: tree selection beats caret, caret rule (b) uses
  `moduleAtCaret` same-file/definition-line semantics, and the holder pushes catalog/selection/caret
  updates. `logic[3:0]` → 4, scalar → 1, interface → 1, parameterised → blank + tooltip are all
  covered by the product function/panel self-test; the real ABI run shows `y = 4`.
- **Chinese headers?** Not yet. The wrapped header strings are absent from `vedit_zh_CN.ts` and
  several statuses/summary/tooltip strings are not wrapped at all (Medium 2); the deferral itself is
  acceptable only as a captain-owned integration step.
- **0-based vs 1-based?** Correct. The ABI header documents 0-based, the Rust builder subtracts 1,
  and the C++ adds 1 once per path. The real logs and the self-test's 1-based checks show no
  off-by-one; the "single conversion site" claim is a Nit (7).
- **Anything weakened?** No existing test was deleted or relaxed as far as the evidence shows:
  ctest #33 (`vedit-module-tree-selftest`) is kept with 76 checks (the old evidence log
  `evidence/module-tree-slang-fix/22-selftest-after-rebuild.log` had 61), #34 is new with 56, and
  the total is 43 (baseline 42). The old hierarchy tests were removed with the old model as the
  contract requires. The new #34's SKIP-as-pass behavior is the one test-rigor weakening (Medium 3).

## Bounds

- **Read:** the frozen contract (§1/§2/§B1), the B1 README and all seven logs, the screenshot, the
  new `src/workspace/module_tree/**` sources/tests/CMake/README, the fixture files, the A1 Rust
  catalog/hierarchy position code, the jump registry and `EditorJumpTarget`, the project-model
  persistence path, the translation file, and the user-guide search.
- **Did not read:** the rest of A1/A2/B2/B3/B4 implementations, the full 30 KB B1 test files line by
  line, and the deleted old `hierarchy_*` sources (they are untracked/absent; I used the current
  filesystem, `build.ninja` and old evidence logs). The current tree has 416 modified/untracked
  files from other lanes; B1's sources are unchanged since 04:09, and I attribute unrelated changes
  to those lanes.
- **Ran:** the host `vedit-module-tree-selftest` → `PASS: 76 check(s), 0 failure(s)`; the pinned-image
  `vedit-module-tree-catalog-test` against the fixture and real slang → `PASS: 56 check(s), 0
  failure(s)`; a nonexistent-fixture/slang invocation → `SKIP: 0 check(s)` with exit 0. I did not
  rerun the full `bash scripts/build-qt.sh` (other lanes are mid-flight and the writer's 43/43 log is
  from 04:11), nor the AppImage, `--selfcheck`, clippy, fmt, docs or lrelease gates. I did not run a
  persisted-project-model probe, a Chinese-locale GUI, the production chooser menu, or a real
  parameterised port through the ABI.

## Verdict

**FAIL** — the tree/width/jump headline behavior is real, but the module tree can be built from the
wrong project file set on the first open (and after a root switch) because the project-model watch
consumes the model's first appearance and any change seen during a run; the untranslated/unwrapped
UI strings, the #34 skip-as-pass behavior and the missing persisted-model coverage are additional
changes to request.
