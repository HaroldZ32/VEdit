# RM-9 final independent review — project-wide status dots and retained per-file verdicts

- Task: T21 (independent verification of T20 / RM-9), read-only.
- Reviewer: `reviewer-3` (Reviewer).
- Date: 2026-11-06 (Asia/Shanghai).
- **Verdict: PASS. Risk: MEDIUM.**
- Basis: the RM-9 card in `evidence/ux-batch-2/REAL-MACHINE-FINDINGS.md` (the user's
  words, the two root causes, the acceptance a–e); the writer's evidence
  `evidence/ux-batch-2/rm9-dots/` (README sha256; the red/green logs, the e2e, the
  screenshot); the code.
- Reviewed code: `diagnostics_controller.{h,cpp}`, `diagnostics_integration.{h,cpp}`,
  `project_lint_trigger.{h,cpp}`, `file_status.h`, `file_status_delegate.{h,cpp}`,
  `file_tree_model.{h,cpp}`, `file_tree_widget.{h,cpp}`, `project_panel.{h,cpp}`,
  `project_variables_e2e.cpp`/`rm9_status_dots_e2e.cpp`/`verdict_retention_selftest.cpp`.

I re-ran the RM-9-targeted ctest set in the pinned image; the writer's wider logs
(filetree 127, lint-experience 103, b3-wiring 76, rm2 133, orchestration 62) were read.
The red-before log is a standalone build of the new controller test against
`git show HEAD:…diagnostics_controller.cpp`, which a reviewer can read but does not re-run
(it would require reverting the product source).

---

## 1. What I re-derived (commands and observed output; `01-ctest-rerun.log`)

```
ctest -R "vedit-verdict-retention-selftest|vedit-rm9-status-dots-e2e|
           vedit-filetree-tests|vedit-workspace-project-selftest|
           vedit-project-lint-e2e|vedit-workspace-diagnostics-selftest" -V
```

The built ctest targets that exist in the current image:

| target | observed |
|---|---|
| `vedit-verdict-retention-selftest` | **checks=24 failures=0** (the new controller test) |
| `vedit-rm9-status-dots-e2e` | **RM9-E2E checks=33 failures=0** (real shell/project/file-tree/diagnostics) |
| `vedit-workspace-project-selftest` | **checks=153 failures=0** (the panel compiles against the new delegate) |
| `vedit-project-lint-e2e` | **checks=11 failures=0** |
| `vedit-workspace-diagnostics-selftest` | **checks=43 failures=0** |

(The `vedit-filetree-tests` target is an isolated standalone build in this tree; the writer's
`03-filetree-tests.log` shows **127/0** including `MEASURE rm9.big-data-changed-emissions 3` for a
1176-row listing. The `vedit-lint-experience-selftest` needs the verible path; the writer's
`04-lint-experience.log` shows **103/0**. The `b3-wiring` 76/0, `rm2` 133/0 and the
orchestration probe 62/0 are the writer's logs; the RM-2 E2E is the one I re-ran during T8.)

The writer's red-before log (`01-red-before-controller.log`): the new controller test against the
HEAD controller fails **8 checks** (`rm9.a-retained-after-switch` 0, `rm9.a-retained-in-map` -1,
`rm9.a-retained-rows-feed-merged` 0, `rm9.count-follows-retained` 0, `rm9.pending-a-visible` 0,
`rm9.pending-b-in-map` -1, `rm9.e-not-green-after-edit` 3, `rm9.e-map-says-not-analysed` 3) —
the exact "only the active file has a verdict" behaviour; the green run is 24/0.

The screenshot (`10-rm9-window.png`, viewed): the Project panel's **Files (3)** shows
`shared/outside.sv` with a **yellow** dot, `a.sv` **red**, `b.sv` **green**; the file tree shows
`a.sv` **red**, `b.sv` **green** and a synthetic **"From filelist (1)"** group; the diagnostics
list carries the `outside.sv:3:2 — fake-warning-…` row; the status bar shows the automatic
"Linting project with slang…". `a.sv` is the active tab and `b.sv` (not active) still has its
green dot; the never-opened `outside.sv` has a dot in both the Project panel and the tree group.

---

## 2. The Lead's four targets

### Target 1 — "one derivation feeds the dot, the panel count and the hover"

**PASS, verified in the code and the controller test.** The ONE classification is
`classifyStatusRows()`; `statusForFile()` calls `statusValueForFile()`, which reads the same
`coveredFiles_`/`cliDiagnostics_` (upper), `retainedLowerRows_`/`retainedLowerEvaluated_`
(lower, retained), `notAnalyzedReason_` and `projectFiles_`. `fileStatuses()` classifies the same
stores in one pass. `mergedDiagnostics()` / `mergedDiagnosticsForFile()` read the **same**
retained-lower store (with `coveredFiles_` precedence) and return the same rows; the session
count comes from the same merged rows (`rm9.count-follows-retained`). The controller test asserts,
for the same A in the same state: `rm9.a-retained-after-switch` (status Error),
`rm9.a-retained-in-map` (fileStatuses 1), `rm9.a-retained-rows-feed-merged` (merged rows = 1),
`rm9.count-follows-retained` (session.diagnostics = 1); after switching back,
`rm9.a-still-error-after-return`.

The **R2 edit** direction is the same store: `markFileEdited` drops the retained verdict, so an
edited file's `mergedDiagnosticsForFile()` returns **no rows** and `statusValueForFile()` returns
`NotAnalyzed`; `rm9.e-not-green-after-edit` (NotAnalyzed) and `rm9.e-map-says-not-analysed`
(fileStatuses = 4) pass; the ruling-1 fresh verdict `rm9.e-dirty-fresh-verdict-wins` passes, and
the fresh re-analysis restores the real verdict. The upper verdict is never resurrected by the
hover or the map: the same state is "no rows / NotAnalyzed" everywhere.

### Target 2 — "a verdict is retained across a tab switch" (file tree, not just the map)

**PASS.** The controller test's `rm9.a-retained-after-switch`/`-in-map`/`-rows-feed-merged`/`-still-error-after-return`
prove the store; the RM-9 e2e (33/0) drives the real shell and the real **file tree**:
`a.sv` (never opened) is Error and shows the tree `FileStatusRole`; `b.sv` is Clean; the
screenshot shows `a.sv` red and the non-active `b.sv` still green. The mirror case is the
dirty-buffer leg of the e2e: an edited file is `NotAnalyzed` at once (hollow grey, never green),
is excluded from the next explicit pass (`filesSeen` without it) and is not covered by that pass
while the other files are. The `r2.edit-voids-lower` / `r2.edit-status-not-analysed` /
`r2.edit-never-green` checks (the writer's lint-experience 103/0) assert the same edit direction
in the product wiring; the old assertions that encoded the defect ("opening B makes A disappear")
were replaced by stronger ones (README §5, `30-old-behaviour-quotes.txt`).

### Target 3 — "filelist-only files appear in the tree group and the Project panel list, with dots"

**PASS.** The e2e's `rm9.filelist-only-outside-is-warning` (status 2), `rm9.tree-filelist-group-has-outside`
(the "From filelist" group's row answers `FileTreeModel::FileStatusRole`),
`rm9.panel-outside-status`/`fileStatusAt(row) == 2`, and the delegate-paints-the-shared-colour
check pass; the screenshot shows the yellow dot in both the Project panel Files list and the
tree group. The dot is derived from the same store: the tree's `FileStatusDotDelegate` and the
panel's `FileStatusListDelegate` both paint the shared `file_status.h` colours/`FileStatus`
values; the panel's `fileStatusAt()` calls the injected `statusSource_` (the diagnostics map), not
a private colour table. The writer's bounds: a standalone `ProjectPanel` needs
`setFileStatusSource()` (the e2e injects the real shell environment); that is a testability bound,
not a second colour source.

### Target 4 — the RM-3 cross-lane `project_panel.cpp` anonymous-namespace shape

**PASS.** The current `project_panel.cpp` defines `FileStatusListDelegate` at project scope
(matching the `project_panel.h` forward declaration); the RM-3 writer's two-line move is in the
current tree and the RM-9 delegate compiles; the writer's `08-project-selftest.log` (153/0) and my
re-run of `vedit-workspace-project-selftest` (153/0) pass with the panel's delegate. The shape is
sane after both lanes settled.

---

## 3. The acceptance a–e

| # | Acceptance | Result |
|---|---|---|
| a | two files/two tabs, switch back and forth, both dots always present | **PASS.** The controller test and the e2e; the screenshot shows the non-active `b.sv` still green while `a.sv` is active; the old "A disappears" behaviour is the red-before (8 failures). |
| b | after a project pass every project file has a verdict (including never-opened) | **PASS.** `adoptProjectFiles()` → `startFullProjectAnalysis()`; the e2e's whole-set request (`a.sv`, `b.sv`, `outside.sv`), `a.sv` Error, `b.sv` Clean, `outside.sv` Warning; `rm9.project-every-file-verdict` / `-no-file-left-as-no-verdict`; `rm9.pending-a-visible` / `-b-in-map` for the queued/hollow state. |
| c | filelist-only file has a dot in the tree group AND the Project panel list | **PASS.** The e2e's tree group + `fileStatusAt(row)` + the screenshot; the RM-11 filelist pipeline is the one from T8/RM-2, and the e2e's project set includes `shared/outside.sv`. |
| d | dirty buffer is an explicit "not analyzed/excluded" state, never green | **PASS.** `rm9.e-not-green-after-edit`, `rm9.e-map-says-not-analysed`, the e2e's `dirty` leg (excluded from the pass), and the `r2.edit-*` checks; the hollow grey ring + tooltip text are the writer's §3. |
| e | 1176-file tree does not repaint per file; the pass stays cancellable | **PASS with a model-level bound.** The writer's filetree `MEASURE rm9.big-data-changed-emissions 3` for a 1176-row listing (the model emits dataChanged for the changed ranges, not per file; Qt repaints only visible rows); `cancelProjectPass()` is a true stop (the U8 scale probe + the e2e's explicit-pass leg); the writer's bound says the 1176 claim is model-level, not pixel-level. |

The cost story (writer's §2/§7): a project open runs ONE automatic full pass (~6.6 s of ASYNC work
on a 1176-file project, cancellable); a save re-lints only the saved file (the incremental
`c8.save-pass-is-incremental` / `-b` / `c8.other-file-verdict-kept` / `c8.unmodified-open-verdict-kept`);
the automatic pass is deferred while another pass runs (never replaces an explicit Lint Project);
a project switch cancels it. This matches the user's "every file gets a verdict" at the stated
cost.

---

## 4. Findings (ranked)

### M1 — MEDIUM — the new user-facing strings have no zh_CN entry yet

**What.** The "From filelist (%1)" group label and the status hints are new translatable strings;
the writer's §7 says `translations/vedit_zh_CN.ts` is outside this task's write scope and is
being edited by the i18n/manual lane, so under zh these fall back to English until the entry
lands. The rest of the RM-9 UI is Chinese.
**Where.** `src/workspace/filetree/file_tree_model.cpp`/`filetree_integration.cpp` (the group
label), the hover/status strings; README §7.
**Why it matters.** The user is Chinese; the new "From filelist" group and the status hint are
visible in English — a partial split-language UI; not a functional dot defect, but the follow-up
must land.
**Direction.** The i18n lane adds the `From filelist (%1)` and status-hint entries to
`vedit_zh_CN.ts`; lrelease then covers them (the current 1244/0/0 is clean but not complete for
these).

### M2 — LOW — the Project panel's marks need the shell-injected source

**What.** The panel reads the diagnostics map from the top-level window's properties; a
standalone `ProjectPanel` paints no marks unless `setFileStatusSource()` is injected (the e2e
injects the real shell environment). The writer's bound, not a product bug in the shipped shell.
**Where.** `src/workspace/project/project_panel.cpp` (`setFileStatusSource`, `statusTimer_`),
README §7.
**Why it matters.** Lane tests/standalone panels can silently show no dots; the shipped app is
fine.
**Direction.** Keep the injection explicit in the standalone harnesses (already done); no product
change.

### L1 — LOW — the 1176 repaint bound is model-level, not pixel-level

**What.** The writer measures `dataChanged` emissions (3 for 1176 rows), not actual pixel
repaints; Qt repaints only visible rows of each emitted range. The bound is honest and the model
behaviour correct; the claim is not a screenshot-level proof.
**Where.** README §7; the filetree test `MEASURE rm9.big-data-changed-emissions 3`.
**Why it matters.** A future delegate that repaints everything on any dataChanged would not be
caught by the emission count alone.
**Direction.** Add a visible-row repaint counter (or a painted-row probe) if the 1176 cost
becomes a regression risk.

### L2 — LOW — the automatic open pass is deferred while another pass runs

**What.** The automatic project-open pass never starts while another pass is in flight, so a
project whose set is re-adopted during a long pass gets its full pass right after that pass, not
during it; the writer states the flip side.
**Where.** `diagnostics_integration.cpp` (the pass scheduler), README §7.
**Why it matters.** A banner "every file gets a verdict" can be briefly delayed; no file is
silently skipped.
**Direction.** No action required; keep the deferral visible in the status line.

### L3 — LOW — the project-open pass costs ~6.6 s of async work on 1176 files

**What.** The writer's cost probe; the pass is cancellable and async, the price of "every file
gets a verdict"; a save does only the saved file.
**Where.** `evidence/ux-batch-2/rm9-dots/20/21/22-cost-*.log`; README §2/§7.
**Why it matters.** The user will see a busy status on a big project open; the pass must remain
cancellable (it is) and not block the UI (it does not).
**Direction.** Keep the cancellation and the incremental save; no change.

### L4 — LOW/NIT — the red-before is the controller test, not the file-tree/e2e layer

**What.** The `01-red-before-controller.log` builds the new controller test against the HEAD
controller (8 failures) — the exact retention defect. The file-tree group and the RM-9 e2e are
new targets; their pre-fix absence is not a red run of the same harness (the tree group did not
exist, the model's status role had only the active-file behaviour). The writer's §4 is explicit.
**Where.** README §4; `01-red-before-controller.log`; the old `30-old-behaviour-quotes.txt`.
**Why it matters.** The task's #6 "the regression would fail on the pre-fix code" is satisfied
for the controller/retention defect; the tree/panel surfaces are new code proven green.
**Direction.** State the red's scope in the acceptance matrix (controller retention; the tree/e2e
are new).

### N1 — NIT — the requirement-driven assertion changes

**What.** The writer replaced old assertions that encoded the defect (`n1.a-no-green-dot-after-file-switch`,
`r2.edit-falls-back`/`-status-yellow`, `c8.unmodified-open-included`) with stronger ones
(README §5, the HEAD quotes in `30-old-behaviour-quotes.txt`): A keeps its Error across the
switch, an edit is NotAnalyzed never green, a save is incremental. This is a requirements change
from the user's report, recorded, not a relaxed assertion.
**Where.** `src/workspace/diagnostics/selftests/lint_experience_selftest.cpp`; README §5.
**Why it matters.** The change is legitimate and documented; a reviewer can verify the old
assertions were the defect, not a guard that was dropped.

### N2 — NIT — an intermediate RM-2 E2E failure from another lane

**What.** The writer notes one `vedit-rm2-project-switch-e2e` run failed on another lane's
in-flight harness edit (an undeclared `FakeLintBackend` + debug loop); after it settled the target
passed 133/133 (`07-2026…`). The current tree passes (my T8 rerun 133/0).
**Where.** `evidence/ux-batch-2/rm9-dots/07-rm2-project-switch.log`; README §7.
**Why it matters.** Shared-tree contention, not an RM-9 defect; the current run is green.

---

## 5. Declined to judge (reason on the same line)

- **A real display-server/desktop run** — the e2e and the screenshot are offscreen Qt; a real
  compositor adds nothing the model/role assertions do not already cover (the screenshot is the
  real product window, offscreen).
- **The 1176-row pixel repaint** — model-level bound (L1); a pixel-level probe is not in the
  evidence.
- **The new zh_CN strings** — the i18n lane owns the catalogue; the writer's bound (M1).
- **A standalone ProjectPanel without a shell** — the panel needs an injected status source
  (M2); the shipped shell injects it.
- **The automatic pass's deferral/cost** — the writer's stated policy (L2/L3); not a defect.
- **The old assertion replacements** — the user's report is the requirements change; the old
  quotes are recorded (N1).
- **The intermediate RM-2 harness failure** — another lane's in-flight edit; the current target
  passes (N2).
- **The RM-3 cross-lane `project_panel.cpp` shape** — current and sane; target 4 PASS; T8's L1
  flagged it, now confirmed good.
- **The full workspace gate** — the captain runs it; I ran the RM-9-targeted ctest set and read
  the writer's wider logs.
- **The red-before in the file-tree/e2e layer** — the new surfaces; L4/N1.

---

## 6. Bounds of this review (what a real desktop adds)

- The e2e and the 24/33/153/43/11 tests run **offscreen** in the pinned Qt6 image, with the real
  shell/project/file-tree/diagnostics lanes and a fake backend; no real display server, window
  manager or mouse.
- The `vedit-filetree-tests` (127/0) is an isolated standalone build; the writer's log; the
  `vedit-lint-experience` (103/0) needs the verible path; both are read, not re-run.
- The **cost** numbers are the writer's probe (the project-open pass ~6.6 s async on 1176); not
  re-run here.
- The red-before is a standalone build against the HEAD controller; I read it, did not revert the
  product source.
- The zh_CN gap (M1) is the i18n lane's; the panel source-injection (M2) and the model-level
  repaint (L1) are the stated bounds.
- No product file was edited; the only writes are this report and `01-ctest-rerun.log` under
  `evidence/ux-batch-2/review-rm9-final/`.

---

## 7. Verdict

**PASS — risk MEDIUM.**

The three things the Lead flagged are all true in the shipped tree: the dot, the panel count and
the hover read the **same** per-file retained store (`statusValueForFile`/`fileStatuses`/
`mergedDiagnosticsForFile`), the R2 edit drops the verdict on every surface (no stale squiggle,
no green), and the same file returns the same value after a tab switch; the never-opened and
filelist-only files get real dots in the **file tree** (`FileStatusRole`) and the **Project
panel** (`fileStatusAt`), painted from the shared `file_status.h` colours, not a private palette.
The acceptance a–e are covered by the controller test, the real-shell e2e (33/0) and the
screenshot; the filetree 1176 bound is model-level and the pass remains cancellable; the RM-3
cross-lane `project_panel.cpp` shape is sane and the panel selftest compiles (153/0). The
remaining risk is disclosure-level: the new user-facing strings still fall back to English under
zh_CN (M1), the panel needs an injected status source outside the shell (M2), and the 1176
repaint claim is model-level (L1); the writer states all of them.
