# RM-2 + RM-3 final independent review — project switch re-rooting and variables add/delete

- Task: T8 (independent verification of T6 / RM-2 and T7 / RM-3), read-only.
- Reviewer: `reviewer-3` (Reviewer).
- Date: 2026-11-06 (Asia/Shanghai).
- **Verdict: PASS. Risk: MEDIUM.**
- Basis: the RM-2/RM-3 cards in `evidence/ux-batch-2/REAL-MACHINE-FINDINGS.md` and the
  recon conclusion (the four seams + the environment, the ancestor-config leak, the two input
  sets, the unconditional `startExpansion`, the single switch command, the append-only variables
  dialog); the writers' evidence `rm2-project-switch/` (README sha256 `f187403d…`) and
  `rm3-variables/` (README sha256 `4a74d250…`); the code.
- Reviewed tree hashes (`05-provenance.txt`): `app_shell.cpp` `1bba079f…`,
  `ffi_backends.cpp` `307c69ee…`, `diagnostics_integration.cpp` `14568eb7…`,
  `crates/vd-config/src/project.rs` `aa5ed365…`, `project_model.cpp` `3c7345e8…`,
  `project_state.cpp` `ffc88c9d…`, `project_variables_dialog.cpp` `aab529a3…`,
  `project_panel.cpp` `a6183b0b…`, `language_registry.h` `42cda8c2…`,
  `crates/vd-config/src/language.rs` `f573dd79…`.

I re-ran the two new E2E targets, the `vd-config` suite and the lrelease gate myself; the
writers' red/negative logs were read (they need product reverts, which a reviewer does not do).

---

## 1. What I re-derived (commands and observed output; logs in this directory)

### 1.1 The new E2Es, green

`ctest -R "vedit-rm2-project-switch-e2e|vedit-project-variables-e2e" -V`
(`01-ctest-rerun.log`):

```
vedit-project-variables-e2e ... Passed 0.17 s   SUMMARY checks=36 failures=0
vedit-rm2-project-switch-e2e ... Passed 1.60 s  SUMMARY checks=133 failures=0
```

The writer's final logs say 125 for the RM-2 E2E (`12-e2e-final.log`); the current source has
133 checks (the E2E grew after the README snapshot; L2). The RM-2 E2E drives the REAL
`ApplicationShell` + project + file-tree + diagnostics lanes, switches A→B→A→B→A through the
NEW top-level entry, and keeps an unsaved, edited tab. Key observed lines:

```
PROJECT-ROOT-ENV root='.../project-a' source=project pinned=1 include-dirs=1 defines=1 \
  env='-I.../project-a/a_inc -DA_ONLY=1'
RM2-START root='.../project-a' analysis-files=3 panel-files=3 imported='.../lists/a-imported.f'
... switch A->B:
PROJECT-ROOT-ENV root='.../project-b' source=cleared include-dirs=0 defines=0 env=''
PROJECT-ROOT-ENV root='.../project-b' source=project pinned=1 include-dirs=1 defines=1 \
  env='-I.../project-b/b_inc -DB_ONLY=1'
RM2-SWITCH A-to-B to='.../project-b' from='.../project-a' expansions=3 settled=1
RM2-ENV root='.../project-b' include-dirs='.../project-b/b_inc' defines='B_ONLY=1'
CHECK PASS rm2.A-to-B.one-expansion  before=2 after=3 expected=3
CHECK PASS rm2.A-to-B.tree-root / tree-has-target-file / tree-no-previous-file
CHECK PASS rm2.A-to-B.unsaved-tab-survives / unsaved-buffer-survives
```

The B→A, A→B-again and B→A-again switches repeat the same checks (the buffer still contains the
`unsaved-marker`, the tab is still open, the env is re-pinned with the new root's `-I`/`-D`, the
expansion count increments by exactly one, the tree has the new root's file and not the old one).
The RM-3 E2E drives the real dialog + the real `ProjectPanel`/`ProjectModel` (no modal window):
add, delete, duplicate/invalid-name refusal with an inline reason, required-name re-offer, the
panel replace/substitution, and A→B→A per-root restore (`rm3.switch.a-table-restored NEWVAR=v1`,
`rm3.switch.a-substitutes-again`, `rm3.switch.b-has-no-a-variable`,
`rm3.switch.both-roots-persisted`).

### 1.2 The config-discovery rule and its test

`cargo +stable test -p vd-config --offline` (`02-vdconfig-rerun.log`): **63 passed, 0 failed**
(the writer's README says 62; the current suite has 63). The rule, as implemented in
`crates/vd-config/src/project.rs` (`Loader`/`ProjectConfig`):

> the `[project]` table is **project membership, not a preference**: it applies only when the
> opened folder IS the discovered config root. An enclosing `vedit.toml` still supplies the
> non-project layers (UI/editor/tool paths), but its filelists/include_dirs/defines do not
> follow a subfolder open.

The test `expand_drops_an_ancestor_projects_membership_but_keeps_its_own` is in that suite
(the writer's `03`/`04`/`13` logs show it red with the old merge and green after the rule change);
it is part of the 63/0 I re-ran.

### 1.3 The variables model and the switch entry

- `ProjectModel::open` preserves `variablesByRoot` across the root wipe and restores the NEW
  root's table; `setVariables` replaces wholesale (`project_model.cpp`, `project_state.cpp`);
  the panel's `applyVariableTable()` is the ONE write path (no `append`); the dialog always shows
  the whole known table plus the missing rows, with Add/Delete and inline validation
  (`project_variables_dialog.cpp`). The RM-3 E2E and the writer's `09-project-selftest.log`
  (`PROJECT-MODEL-SELFTEST ok=1 checks=153 failures=0`, incl. the A-9 re-expansion/value/persist
  checks) cover add/delete/validation/required-name/per-root.
- `ApplicationShell::addCommand("file.switch-project", tr("S&witch Project…"))`; the handler
  calls `openProjectFolderDialog()` (the same reset path as Open Folder → `setProjectRoot()` →
  `projectRootChanged`); the toolbar order list contains it immediately after `file.open-folder`;
  the File menu is built from the same commands. The E2E connects the real
  `action-file.switch-project`, `menu-file` and `view.toolbar` actions and passes.
- `translations/vedit_zh_CN.ts` has `切换工程(&amp;W)…`; `bash build/gates/lrelease.sh`
  (`03-lrelease.log`): **1244 translations, 0 unfinished, 0 untranslated, PASS** (the writer's
  README says 1212; the catalogue has since grown with other lanes — the string is present and
  the gate is green; L2).

### 1.4 The negative controls (writers' logs)

- `02-e2e-negative-control.log`: with (i)+(iii)+(iv) reverted, 18 failures — the env target
  empty, analysis=1 vs panel=2, two expansions per switch, a previous-root file in the set.
- `11-e2e-rm11-negative-control.log`: with the language registry + filelist-entry filter
  reverted, 9 failures — the file set grows to 5 including `notes.md`/`run.tcl`, and
  `sources.f` is handed to the analyzer (`available=1`).
- `03-vd-config-negative-control.log`: the ancestor test fails with the ancestor's
  filelist/-I/-D still applied; `04-vd-config-green.log` restores 62/0.
These are real red runs of the same harnesses; I did not reproduce them (that requires reverting
the product source), but they match the code paths and the current green runs.

---

## 2. The eight checks

| # | Check | Result |
|---|---|---|
| 1 | Switch A→B: no state from A; four seams + environment (-I/-D per root) | **PASS with two bounds (M1/M2).** The E2E asserts the file tree re-roots (new file present, previous file unresolvable), the project file set is the new root's (analysis == panel == persisted, no previous path), the diagnostics count is the new set's (`analysis-files=3`), and the LSP state is cleared then re-pinned with the new root's `-I`/`-D` (`PROJECT-ROOT-ENV`/`RM2-ENV`, `pinned=1`). M1: the proof is at the seam (`setProjectEnvironment`/`selectBackend`), not a live session's `commandLine()`; the writer discloses it. M2: the module-catalog *module count* for B is not asserted in this harness (its input and the separate real-slang `vedit-module-tree-catalog-test` are). |
| 2 | Subfolder of a bigger project does not inherit its filelists/defines; state the rule | **PASS.** The rule above is implemented in `crates/vd-config/src/project.rs` and locked by `expand_drops_an_ancestor_projects_membership_but_keeps_its_own` (suite 63/0). The enclosing `vedit.toml`'s non-project layers remain; its membership does not follow a subfolder open. |
| 3 | One set: analysis (lint/LSP/catalog) == Project panel, for a project with `.f` | **PASS.** The E2E's `RM2-START … analysis-files=3 panel-files=3 imported='…/a-imported.f'`; the project lane publishes `vedit.project.inputs` and the diagnostics `ProjectFileSetPoller` expands with `vd_project_expand_v2` using them; the module-tree fallback passes the same inputs (settings always; imported lists/variables only for the current root). The RM-11 filelist/registry contract is covered by the E2E and the vd-config tests. |
| 4 | One expansion per switch | **PASS.** The E2E's `one-expansion before=N after=N+1` for every switch; `expansions=… settled=1`; the negative control shows `before=1 after=3` with the old unconditional `startExpansion()`. The dedupe is in `ProjectModel::startExpansion` (identical in-flight inputs dropped; explicit Refresh/settings change force a new job) and `expansionCount()` is observable. |
| 5 | Top-level switch entry in BOTH toolbar and File menu, same reset path, zh_CN in catalogue (lrelease green) | **PASS.** `file.switch-project` is registered once (`ApplicationShell::addCommand`), appears in the toolbar order list after open-folder and in the File menu, and its handler calls the same `openProjectFolderDialog()`. The E2E connects and triggers the real action/menu/toolbar. The `切换工程(&W)…` row is present and lrelease is 1244/0/0 PASS. |
| 6 | Unsaved documents survive a switch | **PASS — not regressed.** The E2E opens an edited buffer with an `unsaved-marker`, switches A→B→A→B→A, and every switch passes both `unsaved-tab-survives` and `unsaved-buffer-survives` (`document->isModified()` + the marker text). `setProjectRoot`/`projectRootChanged` does not close tabs or touch the editor documents. |
| 7 | Variables: add/delete really work; required name cannot vanish silently; inline validation; A→B→A per root, no leak | **PASS with the programmatic bound (M3).** The RM-3 E2E (36/0) drives the real dialog (`addVariable`, delete, duplicate/`BAD NAME`/`A=B`/`$X`/`1BAD`/empty refusals with non-empty inline reasons, OK refuses), the real `ProjectPanel`/`ProjectModel` replace path, the required-name re-offer via `missingVariables()`, the file-set substitution, and A→B→A restore (`a-table-restored`, `a-substitutes-again`, `b-has-no-a-variable`, `both-roots-persisted`). The writer's project selftest 153/0 covers the A-9 contract. M3: the dialog is driven programmatically (no modal `exec()`, no screenshot) and "restart" is a new `ProjectModel` over the same state directory, not a new OS process; the writer discloses both. |
| 8 | Both writers disclose their bounds honestly | **PASS.** RM-2 §5 (the LSP at the seam; the catalog not in the RM-2 harness; the filelist-after-switch follow-up; the mirrored language tables; the SIGPIPE harness fix; the shared Rust lib) and RM-3 §8 (programmatic dialog; restart = new model; values with `=`; the per-root map growth) are honest and match what I observed. §7 of RM-3 also discloses the cross-lane `project_panel.cpp` class-scope fix for the RM-9 lane (L1). |

---

## 3. Findings (ranked)

### M1 — MEDIUM — the LSP `-I`/`-D` proof is at the seam, not a live session's argv

**What.** The E2E asserts `setProjectEnvironment` clears then pins the new root's include dirs
and defines through the ONE writer (`selectBackend`, `pinned=1`), and the next `start()` reads
exactly those; it does not start a real verible/slang session and quote its `commandLine()`
(no toolchain configured offscreen). The task's #1 wants the post-switch LSP environment to be
B's; the seam is proven, the live argv is not.
**Where.** `src/workspace/diagnostics/ffi_backends.cpp` (`BackendSwitchState::setProjectEnvironment`),
`src/workspace/diagnostics/diagnostics_integration.cpp` (`followProjectRoot`),
`src/workspace/diagnostics/selftests/rm2_project_switch_e2e.cpp`; RM-2 README §5.
**Why it matters.** A future `start()` path that bypasses `selectBackend`, or a toolchain that
ignores the pinned values, would not be seen by this harness.
**Direction.** Configure a fake local LSP/verible in the pinned image and assert the live
session's argv (or a `commandLine()` echo) shows B's `-I`/`-D` after each switch.

### M2 — MEDIUM — the module-catalog component count is not asserted in the RM-2 harness

**What.** The E2E asserts the persisted project model / panel file set is B's (no A path) and
the module-tree fallback input; the catalog's **module count** for B is not part of the harness
(the writer says so). The separate real-slang `vedit-module-tree-catalog-test` (79 checks) passes
in the writer's affected-suite run, but it is a different fixture, not the A→B switch.
**Where.** `src/workspace/diagnostics/selftests/rm2_project_switch_e2e.cpp`;
`src/workspace/module_tree/catalog_client.cpp` (the input); RM-2 README §5.
**Why it matters.** The card's four seams include the module catalog; a stale catalog for A
would be a user-visible "files from the old project" (the reported defect), and the current
harness only proves the input, not the resulting catalog.
**Direction.** In the E2E, run the catalog (or a fake tool returning a deterministic per-root
catalog) for B after the switch and assert the module count/names are B's, not A's.

### M3 — LOW/MEDIUM — the variables dialog and the restart are programmatic

**What.** The RM-3 E2E drives the real dialog/panel/model objects but not a modal `exec()` or a
click-level/screenshot path; "restart" is a new `ProjectModel` over the same state directory, not
a new OS process. The task's #7 asks that add/delete really work (not just buttons that exist) —
the E2E calls the real dialog operations and the real panel replace path, and the writer's
project selftest covers the A-9 contract; the click-level UI is not captured.
**Where.** `src/workspace/project/project_variables_e2e.cpp`;
`src/workspace/project/project_variables_dialog.cpp`; RM-3 README §8.
**Why it matters.** A button wired to nothing, or a modal-only difference, would be outside the
E2E; the OS-restart persistence of the per-root map is not exercised cross-process.
**Direction.** Add a Qt-side click on Add/Delete/OK (the dialog is already a real widget tree) and
a fresh-process load of `project-model.json` version 3.

### L1 — LOW — the RM-3 cross-lane `project_panel.cpp` class-scope edit

**What.** The RM-3 writer moved the anonymous-namespace closing brace above
`FileStatusListDelegate` so the definition matches the header's RM-9 declaration; disclosed in
RM-3 §7. The RM-9 lane should keep that shape.
**Where.** `src/workspace/project/project_panel.cpp`; RM-3 README §7; the RM-9 card.
**Why it matters.** A cross-lane edit in a file both tasks touch; the disclosure is correct, the
RM-9 review (T21) must build on the current shape.
**Direction.** The RM-9 owner keeps the project-scope class definition (and the header forward
declaration) as shipped; no revert.

### L2 — LOW — the writer READMEs' evidence counts lag the current tree

**What.** RM-2 E2E 125 in the README/`12-e2e-final` vs 133 in my rerun; vd-config 62 vs 63;
lrelease 1212 vs 1244. The current tests are green; the numbers changed with later lane work
(the E2E grew, the catalogue grew). The writer's final logs are the snapshot.
**Where.** `evidence/ux-batch-2/rm2-project-switch/README.md` §3/§6/§7; the rm3 README §4;
`translations/vedit_zh_CN.ts` (the later 17:30 edit by the i18n/RM-4b lane).
**Why it matters.** Evidence hygiene: a reader matching the README number to the current run
sees a mismatch; the shipped assertions are not weaker.
**Direction.** Re-state the current counts (or scope the quoted numbers to the snapshot) at
integration.

### L3 — LOW — a filelist imported AFTER a switch reaches the analysis at the next expansion

**What.** The writer's bound: the panel shows a newly imported filelist immediately, but the
one-shot analysis poller sees it on the next `vedit.project.inputs` expansion event; closing the
import-time gap needs a re-expand trigger (recorded as follow-up, not claimed).
**Where.** `src/workspace/diagnostics/diagnostics_integration.cpp` (`ProjectFileSetPoller`);
RM-2 README §5.
**Why it matters.** The reported defect is the A↔B switch (covered); an import-after-switch is a
narrower flow; the bound is stated.
**Direction.** Add the `vedit.project.inputs` re-expand trigger before a filelist import is
declared "one set".

### L4 — NIT — the per-root variable map grows with project count

**What.** `variablesByRoot` keeps an entry per opened project (an empty list included) until the
user deletes a variable entry; bounded by project count, not file count (RM-3 §8).
**Where.** `src/workspace/project/project_state.cpp`; `project_model.cpp`.
**Why it matters.** A long-lived session with many projects grows a small map; not a defect,
worth the stated bound.

### N1 — NIT — the SIGPIPE harness change

**What.** The RM-2 harness (and `b3_wiring_e2e.cpp`) now sets `SIGPIPE` to ignore, as AGENT.md §4
requires for a lane selftest that links Rust and spawns children; without it ctest reported a
harness `SIGPIPE` death, not a product verdict. No assertion was relaxed (RM-2 §5).
**Where.** `src/workspace/diagnostics/selftests/rm2_project_switch_e2e.cpp`; RM-2 README §5.
**Why it matters.** Correct per the repository rule; disclosed; no product behaviour change.

### N2 — NIT — the A-9 selftest contract update

**What.** `ProjectPanel` now replaces the whole table; the dialog shows every known + missing
name, so `project_selftest`'s A-9 block expects one row per unresolved name *and* the known
rows (not exactly one). The semantic checks (value used, unresolved cleared, table persisted)
are unchanged (RM-3 §2).
**Where.** `src/workspace/project/project_selftest.cpp`; RM-3 README §2.
**Why it matters.** A contract change, not a relaxed assertion; the RM-3 E2E supersedes.

---

## 4. Declined to judge (reason on the same line)

- **A live verible/slang LSP session's argv** — no toolchain configured offscreen; the seam is proven (M1); the writer's bound.
- **The module-catalog child in the RM-2 harness** — the writer's bound; the separate real-slang module-tree test passes (M2).
- **Modal/click-level variables UI and a real OS-process restart** — the programmatic E2E + the new-model restart; the writer's bound (M3).
- **The filelist import-after-switch re-expand** — the writer's follow-up; the A↔B defect is covered (L3).
- **The per-root map growth** — bounded by project count; the writer's bound (L4).
- **The RM-9 cross-lane `project_panel.cpp` shape** — disclosed; T21 owns the RM-9 review (L1).
- **The full workspace gate** — the captain runs it; I ran the two E2Es, the vd-config suite and lrelease.
- **The negative-control logs** — the writers' reverts; I read them, did not re-run (a reviewer does not revert the product).
- **The writers' evidence-count drift** — L2; the current runs are green.
- **The shared `catalog_client.cpp` RM-2 hunk vs the U9a hunk** — both writers disclose; the U9a README also notes the RM-2 hunk; attribution only.
- **The mirrored Rust/C++ language tables** — the RM-2 §2 follow-up (ABI publication); the current tests pin both sides.
- **The SIGPIPE harness + A-9 contract** — N1/N2; disclosed, no product change.

---

## 5. Bounds of this review (what a real desktop/process would add)

- I ran the RM-2 E2E and the RM-3 E2E in the pinned Qt6 image, offscreen, using the real
  `ApplicationShell`/project/file-tree/diagnostics/panel/model objects; no real display server,
  window manager or mouse/keyboard.
- I did **not** start a live LSP/verible/slang session; the `-I`/`-D` proof is the seam
  (`setProjectEnvironment` + `selectBackend`), not the live argv (M1).
- I did **not** run the module-catalog child in the RM-2 harness; its input and the separate
  module-tree catalog test are the evidence (M2).
- I ran `cargo +stable test -p vd-config` (63/0) and the lrelease gate (1244/0/0), the two
  E2Es; I did **not** run the writer's project selftest binary (the writer's `09` log shows
  153/0; the ctest name is not in my narrow selection).
- The variables dialog was driven programmatically, not with a modal `exec()`/click/screenshot;
  "restart" is a new model, not a new process (M3).
- I did **not** re-run the writers' negative controls (they require reverting the product
  source); I read the red logs and the code.
- I did **not** run the full gate or the real desktop; the writers' bounds cover the differences.
- No product file was edited. The only writes are this report and `01`–`05` under
  `evidence/ux-batch-2/review-rm23-final/`.

---

## 6. Verdict

**PASS — risk MEDIUM.**

The two cards are met at the user's acceptance: switching A→B→A→B→A through the new
File-menu + main-toolbar `S&witch Project…` entry clears and re-pins the LSP `-I`/`-D` per root,
re-roots the file tree and the project file set (analysis == panel == persisted, with the imported
`.f`), increments exactly one expansion per switch (the old duplicate is red in the writer's
negative control), and keeps the unsaved tab and its edited buffer alive on every switch — the
Blocker-shaped regression is explicitly green. A subfolder open no longer inherits the enclosing
project's filelists/defines (the `[project]` membership rule + its unit test). The variables
dialog really adds/deletes, refuses malformed/duplicate names inline, re-offers a required name
instead of losing it, uses one wholesale write path, and restores A's values after A→B→A without
leaking B's. The new entry's `切换工程(&W)…` is in the catalogue; lrelease is clean.

The Medium risk is the last mile the offscreen evidence does not execute: the live LSP argv
(M1), the module catalog count in the same harness (M2), and the dialog/restart as a real
click/process (M3); the writers state all three bounds, and the RM-3's cross-lane
`project_panel.cpp` change is disclosed for the RM-9 owner. The user's real-machine run is the
remaining check; nothing in this review contradicts it.
