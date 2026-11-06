# Re-review — UX batch 2, lane B1 (after repair)

- Contract: `.mpd/plans/ux-batch-2/00-plan.md` §`B1`, with §5.1 (integration carries)
- Original review: `evidence/ux-batch-2/review-b1/REPORT.md` (verdict FAIL)
- Repair evidence: `evidence/ux-batch-2/b1-repair/` (README, REPORT, `logs/01..10`)
- Re-review evidence: this directory — `independent-rerun.log` (current build, `VEDIT_DEBUG=1`),
  `independent-selftest.log`, `ctest-list.log`
- Review date: 2026-11-05
- Reviewer: independent; findings only; no source, test or gate edits
- **Risk level: LOW** for the repaired lane code; **MODERATE** for batch integration because two
  contract items (the translation catalogue and the user-guide sentence) are carried, and one
  regression assertion is weaker than the writer's claim
- **Verdict: PASS** — the High 1 data-source bug is genuinely fixed on the normal first-open and
  in-flight paths; the remaining items are declared carries or test-rigor improvements.

## Assessment

The repair fixes the defect that failed B1. The project-model watch is now a real state machine:
`projectModelStamp()` produces an `mtime_ms|size` token, `ModuleTreeHolder::refresh()` records the
token *before* `CatalogClient::start()` (which loads the persisted model synchronously), a changed
token seen while a run is in flight sets `modelPending_`, `ModuleTreeHolder::finishRun()` schedules
exactly one follow-up when pending or when the on-disk token differs from the run's token, and an
unchanged model never re-runs because `checkProjectModel()` short-circuits on `stamp == modelStamp_`
and `finishRun()` sees `current == runModelStamp_`. I traced both scenarios named in the task — a
model that appears after the fallback run started, and a model that changes while a run is in
flight — through the code and independently re-ran the current build in the pinned image with
`VEDIT_DEBUG=1`: fallback run 7 files → model written → run 2 `project model (project-model.json)`
6 files → `refreshNow` run 3 6 files → model changed mid-flight → run 4 `project model` 7 files →
`h1.in-flight.settles` 4 → 4, `PASS: 80 check(s), 0 failure(s)`, exit 0. The RED log (`logs/01`)
shows the pre-repair signature directly: first appearance `source=vd_project_expand_v2` and
`runs 1 -> 1`, in-flight `modelPending=0` and the catalog stuck on the 6-file seeded set
(`fileSet=6 modules=5 generation=2 runs=2`), 12 failures and exit 1. The tree/width/jump headline
behaviour is unchanged and still passes; M3 now fails closed on a missing fixture/slang (exit 1,
`logs/05`); M4 now discriminates a definition from two real instance sites via the instantiated
`shared` module on the real KTextEditor cursor; Nit 7's single conversion seam is real. The writer's
numbers reconcile (the RED run lacks the one `h1.in-flight.settles` check added for the green run:
79 → 80). Two things are not closed and must not be read as done: the 65 new translation strings are
not in `translations/vedit_zh_CN.ts` and §5.1 still does not name B1's strings (captain/A2 carry),
and the user-guide sentence required by §B1 is still absent (captain/A2 carry). One test-rigor gap
also remains: the in-flight regression's final model equals the fixture's root scan, so the test
proves a follow-up ran but not that the follow-up used the persisted model; the current code's run 4
source is proven only by the debug log, not by an assertion.

## Findings (ranked)

### Medium — the in-flight regression does not prove the follow-up used the persisted model

**Where:** `src/workspace/module_tree/module_tree_catalog_test.cpp`, `main`, the `h1.in-flight.*`
group and `writeProjectModel(allSources)`; `src/workspace/module_tree/catalog_client.cpp`,
`CatalogClient::start`.

**What happens:** H1-A seeds a real model whose 6-file set differs from the root scan (orphan.sv
excluded) and asserts the data source (`h1.first-appearance.source-is-model`) — that test is strong.
H1-B then writes `allSources`, which is exactly the fixture's 7-file root scan, while a run is in
flight. The final tree has `fileSet=7`, six modules and `orphan` present — the same result a
fallback root scan would produce. The test asserts that a new generation ran
(`catalogGeneration >= runsBefore + 2`) and that the tree has the 7-file set, but it never asserts
`catalogClient->sourceDescription()` after the follow-up. A future regression that schedules the
follow-up but has it fall back to the root scan (or uses a stale file set) would pass H1-B; only the
debug log (`logs/02` and my `independent-rerun.log`, run 4 source
`project model (project-model.json)`) shows the current code is right.

**Why it matters:** this is the same data-source class as the original High 1. H1-A catches a client
that always ignores the model, and H1-B catches the original "change swallowed, no follow-up" case,
but the in-flight half's *data source* is not regression-guarded. The writer's claim that the two
regressions prove the H1 fix therefore overstates what the test asserts.

**Direction of a fix:** assert the source after the follow-up (e.g. a new
`h1.in-flight.source-is-model` reading `catalogClient->sourceDescription()` once the generation
converges), and/or make the in-flight model's file set differ from the root scan while restoring the
root set before the later `acceptance.i/iv` checks (which compare against the catalog captured before
the H1 runs). This is a test-rigor improvement, not a product defect: the current behaviour is
correct and I verified it independently.

**Evidence:** the test source has `sourceDescription()` only in the H1-A check; the fixture root
scan is exactly `bus.sv, consumer.sv, orphan.sv, reflink.sv, shared.sv, top_a.sv, top_b.sv`; my
debug run shows the correct run-4 source.

### Nit — `test.checks-ran` can never fail

**Where:** `src/workspace/module_tree/module_tree_catalog_test.cpp`, `main`, the final
`test.checks-ran` check; the `check()` helper.

**What happens:** `check()` increments `g_checks` before evaluating its condition, so
`check("test.checks-ran", g_checks > 0)` is always true. The green log proves it: it prints
`test.checks-ran — checks=79` and the summary immediately after says `PASS: 80 check(s)`. A green run
cannot have zero checks in practice because `fixture.present` and `slang.present` are unconditional
after the M3 guard and every early-return path returns non-zero, but the intended zero-check guard is
decorative.

**Direction of a fix:** capture the count before the call (`const int before = g_checks;`) and
assert `before > 0`, or assert a minimum expected check count.

### Nit — two repair logs contain undisclosed non-product failures

**Where:** `evidence/ux-batch-2/b1-repair/logs/02-red-debug.log`, `logs/04-selftest-and-ctest-module-tree.log`,
cited in the repair README as “run sequence diagnostic” and “ctest … 2/2 passed”.

**What happens:** `logs/02` ends with `EXIT=139` (segfault) after
`acceptance.focus.selection-beats-caret`; its check set predates the final test (no
`h1.in-flight.settles`, and the `final-file-set` detail lacks the generation/runs gating), and it
contains a later `run source='project model'` from H1-B's `refreshNow`, so the README's “the model is
never re-read” is only true of the first appearance. `logs/04` begins with a host
`vedit-module-tree-selftest` abort (`SELFTEST_EXIT=134`, no display) before the pinned-image ctest
2/2 pass. Neither failure is a product failure, and the final RED/green claims rest on `logs/01`,
`logs/08` and `logs/09` — which are coherent — but the README does not say so.

**Direction of a fix:** label `logs/02` as an earlier diagnostic (not the RED proof) and note the
harness crash that the later test's re-find-the-row comment fixed; state that `logs/04`'s host
selftest aborted because there was no display and the image ctest is the result.

## Original findings — verdicts

| Original finding | Verdict | Artefact checked |
|---|---|---|
| **High 1** — project-model watch drops the first appearance / a change during a run | **FIXED** | `module_tree_integration.cpp`: `projectModelStamp`, `ModuleTreeHolder::checkProjectModel` / `finishRun` / `refresh` / `setProjectRoot`; `module_tree_catalog_test.cpp` H1-A/H1-B; `logs/01` RED (79/12/exit 1, first-appearance `source=vd_project_expand_v2`, `runs 1 -> 1`, in-flight `modelPending=0`, `fileSet=6`), `logs/08` GREEN (80/0), `logs/02` sequence, and my independent `independent-rerun.log` (run 1 fallback 7 → run 2 model 6 → run 3 model 6 → run 4 model 7 → settles 4→4; 80/0, exit 0). Both named scenarios are correct; an unchanged model does not loop. |
| **Medium 2** — remaining user-visible strings not wrapped | **FIXED (wrapping); translation catalogue carried** | `module_catalog.cpp` (`summaryLine`, all five `portWidthFromType` problems), `module_hierarchy.cpp` (four guard notes), `module_tree_panel.cpp` (`deliver`, `rowLabel`, refresh fallback, `\nroot`), `interface_panel.cpp` (`deliver`, chooser action, tooltip), `catalog_client.cpp` (failure messages). `logs/06` extracts 72 strings, 65 new; every user-visible status/header/tooltip I spot-checked is wrapped. Formats/paths/identifiers are not translatable by design. The 65 strings are still absent from `translations/vedit_zh_CN.ts` and §5.1 still names only A2/A4 — captain/A2 carry, not a lane defect. |
| **Medium 3** — a missing fixture/slang exits 0 with zero checks | **FIXED** | `module_tree_catalog_test.cpp`, `main`: the missing-fixture/slang branch prints `FAIL catalog.real` and `FAIL: 0 check(s), 1 failure(s)` and returns 1; `logs/05` shows `MISSING_EXIT=1`; CMake registers the test with the fixture/slang and a comment stating the policy. Green run has 80 checks. Nit: `test.checks-ran` is tautological (above). |
| **Medium 4** — declaration vs instantiation not proven | **FIXED** | `module_tree_catalog_test.cpp`, `acceptance.iii.shared-*`: `shared` is instantiated twice (`sites=2`), the tree `shared` row is double-clicked, the file is `shared.sv` (not `top_a.sv`/`top_b.sv`), the real KTextEditor cursor is `1:8` on `module shared`, and `acceptance.vi.declaration-cursor` now asserts the port column (`1:67`). |
| **Medium 5** — persisted-project-model path not exercised | **FIXED** | H1-A/H1-B write `project-model.json` through the real `ProjectStateStore::save` (QSaveFile atomic); `catalogClient->sourceDescription()` is `project model (project-model.json)`; H1-A's set is 6 files vs the 7-file root scan. The in-flight follow-up's source is not asserted (Medium finding above), but the path is exercised and the current code verified. |
| **Medium 6** — user-guide sentence missing | **NOT FIXED — carried** | `docs/USER_GUIDE.md` has no module-tree/interface sentence; the plan §B1 requires it in the tooltip *and* the user manual; the tooltip is present and asserted (`acceptance.v.abi-limit-tooltip`). `docs/USER_GUIDE.md` is outside B1's write scope; the repair README/REPORT hand it to the captain/A2. Must close before batch acceptance. |
| **Nit 7** — “one conversion seam” claim | **FIXED** | `module_tree_panel.cpp`, `ModuleTreePanel::addRow`: roles now come from `jumpFromPosition`; `module_catalog.h` comment corrected; the selftest (76/76) and the catalog test (80/80) pass. |
| **Nit 8** — production chooser menu seam-only | **NOT FIXED — carried** | The production `QMenu` path is still exercised only through `setInstantiationChooser`; no defect found in the menu code; disclosed in the lane README. |
| **Nit 9** — guard labels use compile-time caps | **NOT FIXED — carried** | `ModuleTreePanel::rowLabel` still formats `kModuleTreeMaxDepth`/`kModuleTreeMaxRows`; production uses the defaults (64 / 20000), so not user-facing today; latent if the options are wired. |

## Carried / not closed before batch acceptance

1. **Translation catalogue (§5.1).** The 65 new strings listed in
   `b1-repair/logs/06-lupdate-strings.txt` are not in `translations/vedit_zh_CN.ts`, and §5.1 names
   only A2/A4's strings and the menu/preferences items, not B1's. Captain/A2 must run
   `lupdate -locations absolute` + `lrelease` (0 unfinished), and the old
   `evidence/ux-batch-2/b1/README.md` item 2 (“Every string is wrapped”) should be corrected or
   struck; the lane's own README now states the honest English-until-catalogued status.
2. **User-guide sentence (M6).** Add “the module catalog has no per-connection position, so
   Ctrl+double-click targets the instance statement, not the connected port” (or equivalent) to
   `docs/USER_GUIDE.md`.
3. **Nits 8/9.** Chooser seam-only and the compile-time cap labels; carried, no code defect found /
   latent.
4. **Test rigor.** Add the H1-B source assertion; make `test.checks-ran` non-tautological.

## Declined to judge

- **`mtime_ms|size` token: same-millisecond, same-size rewrite.** Disclosed by the writer; not
  observed; the project lane writes `project-model.json` only after an asynchronous expansion, and
  the project lane's own `ProjectModel::settingsStamp` uses the same idiom. A bounded residual risk,
  not a demonstrated defect.
- **Imported `.f` / `-I` / `-D` forwarding is not separately asserted.** The persisted file-set path
  is exercised; the original M5 direction said “e.g. a file only in the model or a project `-I`/`-D`”.
  The ABI option forwarding is a different plumbing surface; not judged here.
- **The live project-panel expansion is not driven.** The test writes through the real store; the
  writer discloses this bound. I verified the file format/path and the client's synchronous load.
- **Root-switch + model-change combination.** The state machine resets its tokens/flag on
  `setProjectRoot`; the existing root-switch checks pass; the combination is not separately tested.
  Not judged without a failing case.
- **`modelPending` probe is implementation-coupled.** It asserts the chosen pending-flag design; a
  future behaviour-preserving refactor could fail the probe. The behaviour is correct; not a product
  finding.
- **The later `acceptance.i/iv` checks use the `catalog` captured before the H1 reruns.** Green passes
  because H1-B restores the root set; the checks are not independent of the H1 sequence, but I found
  no product issue in the sequence as written.
- **Old B1 README's “Every string is wrapped” sentence.** Superseded by the repair report; carried to
  the captain (see above).
- **Screenshot / visual QA.** The repair changes no visual layout; the original xcb screenshot was
  accepted; not re-run (other lanes are mid-flight).
- **Other lanes' mid-flight changes.** The current 11:28 build includes other lanes' code; B1's
  sources are unchanged since 07:57. My B1 rerun passes; unrelated failures are attributed to those
  lanes.
- **ABI/Rust catalog internals, `translations/**` content, `docs/**`, AppImage/`--selfcheck`/clippy/
  fmt/docs/lrelease, live Chinese GUI, network-filesystem mtime granularity.** Outside this re-review's
  lane; not run.

## What the evidence establishes (the task's questions)

- **Is the H1 state machine correct?** Yes, for the normal first-open and in-flight paths. The token
  is recorded before the synchronous persisted-model load; a change seen while running sets the
  pending flag; `finishRun` schedules exactly one follow-up when pending or when the token on disk
  differs from the run's token; `refresh` stamps/clears; an unchanged model does not re-run
  (`h1.in-flight.settles` 4→4 and my rerun). The token's same-ms/same-size edge is a disclosed,
  bounded residual risk.
- **Did the RED log exercise the pre-repair behaviour?** Yes: `logs/01` fails the first-appearance
  data source (`source=vd_project_expand_v2`, `runs 1 -> 1`) and the in-flight change
  (`modelPending=0`, `fileSet=6`, `runs 2`), exactly the original bug, with 79 checks / 12 failures /
  exit 1. `logs/02` is an earlier diagnostic that crashed; I do not rely on it for the verdict.
- **Do the new regressions seed a real persisted model whose set differs from the root scan?** H1-A
  yes (real `ProjectStateStore::save`, 6 files vs the 7-file root scan, source asserted). H1-B seeds
  a real model but its 7-file set equals the root scan; it proves the in-flight follow-up runs, not
  that it used the model. My debug run shows the current code uses the model; the assertion gap is
  the Medium finding.
- **Is M3 fail-closed?** Yes: missing fixture/slang prints `FAIL catalog.real`, `FAIL: 0 check(s),
  1 failure(s)`, exit 1 (`logs/05`); the ctest registration keeps the test and the CMake comment
  states the policy. The green run is not zero-check, but the `test.checks-ran` guard itself is
  tautological (Nit).
- **Does M4 discriminate definition from instantiation?** Yes: `shared` is instantiated by both
  `top_a` and `top_b`; the double-clicked tree row opens `shared.sv` at the definition (`1:8`), not
  either instance site (`top_a.sv:4`, `top_b.sv:2`), on the real KTextEditor document; the port
  declaration now asserts the column (`1:67`).
- **Are there user-visible strings lupdate cannot extract?** I found none in the lane's six product
  translation units: statuses, tooltips, headers, empty states and error messages are wrapped;
  formats/paths/identifiers are not translatable. The remaining gap is the catalogue (65 strings not
  yet in the `.ts`), not extraction.
- **Was anything weakened?** No. The original 56 catalog checks are all present (the new set is a
  superset), the selftest is still 76 with the same check names, and the only changed assertion is
  `acceptance.vi.declaration-cursor`, strengthened from line-only to line+column. ctest was 43 at the
  original B1 delivery, 47 at the repair (other lanes added four) and is 49 now; B1's two test
  executables are still present (`#34` selftest, `#35` catalog test) and no test was removed.

## Bounds

- **Read:** the frozen plan §B1 and §5.1, the original review, the repair README/REPORT and
  `logs/01..10`; the lane's integration/state-machine, `catalog_client`, `module_catalog`,
  `module_hierarchy`, both panels, the catalog test, CMake and README; `ProjectStateStore`
  save/load and the project model's `saveState` call sites; the jump helpers; the fixture; the
  translation string list; the user-guide search.
- **Did not read:** other lanes' sources/diffs; the full 833-line catalog test line by line (I read
  the H1, M3, M4 and root-change sections and diffed every check name/detail against the original
  B1 log); the Rust catalog/ABI internals; the full `translations/vedit_zh_CN.ts`; the deleted old
  `hierarchy_*` sources (absent; the original review covered their removal).
- **Ran:** in the pinned `vedit-qt6-build:local` image against the current 11:28 build (B1 sources
  unchanged since 07:57): `vedit-module-tree-catalog-test ...` with `VEDIT_DEBUG=1` →
  `PASS: 80 check(s), 0 failure(s)`, exit 0 (`independent-rerun.log`); `vedit-module-tree-selftest`
  → `PASS: 76 check(s), 0 failure(s)`, exit 0 (`independent-selftest.log`); `ctest -N` → 49 tests
  with `#34`/`#35` present (`ctest-list.log`).
- **Did not run:** the full `bash scripts/build-qt.sh` / 47-test suite (other lanes mid-flight; the
  writer's `logs/09` is the full-gate record and is coherent), the xcb screenshot, the AppImage,
  `--selfcheck`, clippy, fmt, docs, lrelease, a live project-panel expansion, a Chinese-locale GUI,
  a host/network-filesystem mtime probe, or the exact pre-repair source (untracked and overwritten;
  the RED log signature is the before-evidence).

## Verdict

**PASS —** the High 1 data-source bug is fixed for the normal first-open and in-flight scenarios
(verified by the state-machine trace, the RED/GREEN logs and an independent 80/80 debug rerun), and
M3/M4/M5/Nit 7 are fixed; the translation catalogue, the user-guide sentence and Nits 8/9 are
explicit captain/A2 carries, and the H1-B data-source assertion is a non-blocking test-rigor
follow-up that must not be mistaken for a product defect.
