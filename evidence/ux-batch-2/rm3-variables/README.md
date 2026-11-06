# RM-3 — project variables: add / delete, inline validation, per-root persistence

Branch `feat/ux-batch-2`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
Owner: Senior Engineer (shared task T7). User words: the “环境变量” dialog that pops
after opening/switching a project only let them EDIT existing values —
“不能手动加、删”.

## 1. What changed

| Area | Before | After |
|---|---|---|
| Dialog (`src/workspace/project/project_variables_dialog.{h,cpp}`) | rows were generated ONLY for known/missing names; no way to type a new name; no way to remove a row | **Add variable** appends a row (editable NAME + value); every row has a **Delete** button; the whole known table is always shown, so a replace can never drop a name the user did not see. |
| Validation | none (an empty/malformed name was silently ignored by the table builder) | inline, next to the row, and OK refuses to close: empty name, malformed name (ASCII `[A-Za-z_][A-Za-z0-9_]*`; no spaces/`=`/`$`, no leading digit — the same grammar the Rust substitution uses), duplicate name. Never a modal, never a silent ignore. |
| Purpose / rule text | one sentence about “unresolved after import” | one short line on what the variables ARE (“substituted into filelist paths and include directories”) plus the deletion rule: *“Deleting a variable removes its value; if a filelist still references it, the project reports it as missing and asks for it again.”* — so a required name can never vanish silently. |
| Panel (`project_panel.{h,cpp}`) | `merged.append(dialog.variableTable())` — appending made a deleted variable immortal, and an emptied value never removed its entry | ONE write path `applyVariableTable()` → `ProjectModel::setVariables()` (wholesale replace). Both entry points (the after-import prompt and the Edit Variables button) use it. The Edit button now opens the dialog even when the project has no variables yet (adding is the point). |
| Persistence (`project_state.{h,cpp}`, `project_model.cpp`) | `project-model.json` held ONE live `variables` list for the last root; `ProjectModel::open()` wiped the state on a root change, so A→B→A lost what the user typed for A | additive `variablesByRoot` map (file `version` 3; a version-2 file loads with an empty map and the model seeds its root's entry). `setVariables()` updates the map entry for the current root; `open()` preserves the map across the wipe and restores the NEW root's own table. Switching to B shows nothing from A; switching back restores A's values and re-expands with them. |
| Registry follow-up (found by this task) | the RM-2 language filter dropped a filelist entry whose path carries a LITERAL trailing `$` (`out_tail.sv$`, ADDENDUM A-17) because `sv$` is not a known extension | both registries (`crates/vd-config/src/language.rs`, `src/workspace/project/language_registry.h`) ignore a trailing run of `$` when looking up the language: that character is part of the file NAME, not the extension. `notes.md$` is still not a source. Covered by a Rust unit test and by the existing `a17.*` selftest (which caught it). |

## 2. The dialog's contract (why the whole table is always shown)

The panel replaces the project table with what the dialog returns. Therefore the
dialog shows **every known name plus every requested (missing) name**, even when
the caller asked only for the missing ones. A replace based on a partial view
would delete the names the user never saw; the A-9 “one fillable row per
unresolved name” prompt is unchanged (it is a subset of the rows shown).

Consequence for an existing regression: `project_selftest.cpp`'s A-9 block
expected exactly one row for a dialog built with one missing name. It is updated
to the new contract (one row per unresolved name **and** the known ones), with
the A-9 intent kept: the missing row still exists, `setValue` fills it, and the
filled value reaches the model and the expansion. This is a documented contract
change, not a relaxed assertion (the semantic checks — value used, unresolved
cleared, table persisted — are unchanged).

## 3. The required-name rule (item 2 of the task)

Picked: **deletion clears the value; a name a filelist still references becomes
unresolved again and the project asks for it on the next expansion.** The rule is
stated in the dialog itself (the line above the table). It is enforced by the
model/pipeline, not by hiding the button: after deleting a name the test asserts
the model re-reports it in `missingVariables()` and the dialog offers its row
again — never a silent disappearance. Deleting a row the dialog created only for
a currently-missing name also just removes the row (nothing is stored for it).

## 4. Regression — `vedit-project-variables-e2e`

New target `src/workspace/project/project_variables_e2e.cpp` (ctest
`vedit-project-variables-e2e`, offscreen), driving the REAL dialog and the REAL
`ProjectPanel`/`ProjectModel` without a modal window:

1. **Dialog, add/delete/validate:** a known row + a requested (missing) row;
   `addVariable("NEW")` adds; a duplicate / `BAD NAME` / `A=B` / `$X` / `1BAD` /
   empty name are refused with a non-empty inline reason; Delete removes the row
   and shows “Removed …”; deleting a required row says it will ask again.
2. **Panel replace + substitution:** fill the required `NEWVAR` → the file set
   contains the variable-expanded source that lives OUTSIDE the root (the scan
   cannot find it, so only the substitution can); add a second variable, delete
   it → the model no longer has it, the first one stays.
3. **Restart:** a new `ProjectModel` over the same state directory restores the
   table and re-expands with it.
4. **Switch A→B→A:** B sees none of A's variables and reports its own missing
   one; B's own table is added; switching back restores A's table and substitutes
   again; the persisted file contains both roots' entries.
5. **Required name cannot vanish:** delete `NEWVAR` → the model reports it in
   `missingVariables()`, the panel no longer lists the value, and the dialog
   offers the row again.

**GREEN:** `SUMMARY checks=36 failures=0` (`03`/`05-e2e-final.log`).

**RED (negative control, fix reverted):** Add/Delete removed (they returned
false, the pre-RM-3 state), the panel appended instead of replacing, and
`open()` wiped the per-root table:
`SUMMARY checks=36 failures=11` (`04-e2e-negative-control.log`) — no add, no
delete, no validation reasons, and after A→B→A A's values were gone
(`a-table-restored`, `a-substitutes-again`, `both-roots-persisted` all FAIL).

## 5. Evidence (commands + observed)

```text
# dialog/model regression (real dialog + real panel/model, headless)
run_in_qt6_container env QT_QPA_PLATFORM=offscreen … vedit-project-variables-e2e
  -> SUMMARY checks=36 failures=0            (05-e2e-final.log)
  -> SUMMARY checks=36 failures=11           (04-e2e-negative-control.log, reverted)

# the repository's project suite (A-9 contract update + the A-17 fix it caught)
vedit-workspace-project-selftest --selfcheck tests/fixtures/verilog-project
  -> PROJECT-MODEL-SELFTEST ok=? checks=? FAIL checks=0   (09-project-selftest.log)
     a17.files / a17.literal-trailing-dollar-kept PASS (the literal `$` entry is kept)
     a9.dialog.* PASS under the updated contract

# the affected suites
ctest -R "vedit-(workspace-project-selftest|project-variables-e2e|rm2-project-switch-e2e|
           app-shell-selftest|module-tree-catalog-test|workspace-diagnostics-selftest|
           b3-wiring-e2e|edit-core-selftest)" --output-on-failure
  -> 100% tests passed, 0 tests failed out of 8   (08-ctest-affected.log)

# language registry (A-17 literal trailing `$`)
cargo test -p vd-config --lib language
  -> 3 passed (incl. a_literal_trailing_dollar_does_not_change_the_language)

# catalogue gate (new user-visible strings)
bash build/gates/lrelease.sh
  -> PASS: 1222 finished, 0 unfinished, 0 untranslated

# product build
cmake --build target/qt6-build --target vedit-app
  -> Linking CXX executable src/app/vedit-app
```

## 6. NOT in scope (reported, not built): per-project `-I` / `-D` editing

The Project dock shows the expansion's include dirs and defines read-only, and
the task is right that there is **no writer for a per-project store** today: the
only writer of `tools.include_dirs` / `tools.defines` is
`<stateDir>/settings.toml` (the global tools page), and the per-project lists are
OUTPUTS of the expansion (config + filelists + those tools values). Making them
addable/deletable from the dock needs a per-project store — `vedit.toml` is not
written by any code path, and `project-model.json` is wiped on a root change
(RM-3 just built the per-root memory for VARIABLES, not for these lists).

Trade-off for the captain:

* **Global settings** (today): two clicks through Preferences; applies to every
  project; no new persistence, no writer, no wipe problem. Correct for toolchain
  paths, wrong for “this project needs `-I../ip`”.
* **New per-project writer**: either write a `[project]` table into the project's
  own `vedit.toml` (a user-visible file the project may be sharing/VCS-tracking —
  writing it silently is a product decision), or extend `project-model.json`
  with per-root include/define lists that survive `ProjectModel::open()` exactly
  like `variablesByRoot` now does (the RM-3 pattern). The second is small but it
  makes the persisted model partially authoritative over the expansion, so a
  later `vedit.toml`/filelist edit must define which one wins. Not built here.

## 7. A cross-lane build fix (disclosed)

While building, `src/workspace/project/project_panel.cpp` did not compile: the
RM-9 in-progress change defined `FileStatusListDelegate` inside the anonymous
namespace while `project_panel.h` forward-declares it at project scope, so the
two were different types and `new FileStatusListDelegate` failed. I moved the
anonymous-namespace closing brace above the class (two lines) so the definition
matches the header's declaration. No behaviour changed; the RM-9 lane should
keep that shape. This was the only edit outside RM-3's own surface.

## 8. Bounds

- The dialog is driven programmatically in the regression (no modal `exec()`);
  the UI pieces (Add/Delete buttons, reason labels, OK validation) are the same
  objects the user clicks, but no screenshot/click-level capture is included.
- “Restart” is a new `ProjectModel` over the same state directory (the process
  boundary the store is written for), not a new OS process.
- Values containing `=` are preserved verbatim (the table splits on the FIRST
  `=`), and values may contain spaces; the value itself is not validated because
  filelist paths legitimately do.
- The per-root map grows with the number of projects opened; it is pruned only
  when the user deletes a variable's entry (an empty list is kept for that root).
  Bounded by project count, not by file count.

## 9. Files

- `src/workspace/project/project_variables_dialog.{h,cpp}` — add/delete, inline validation, purpose/rule text.
- `src/workspace/project/project_panel.{h,cpp}` — `applyVariableTable()` replace path, Edit button opens on an empty table.
- `src/workspace/project/project_model.cpp`, `project_state.{h,cpp}` — per-root `variablesByRoot`, additive version 3.
- `src/workspace/project/project_selftest.cpp` — A-9 contract update (documented above).
- `src/workspace/project/project_variables_e2e.cpp` + `CMakeLists.txt` — the regression.
- `crates/vd-config/src/language.rs`, `src/workspace/project/language_registry.h` — A-17 trailing-`$` language lookup.
- `translations/vedit_zh_CN.ts` — new strings (same context), lrelease PASS.
