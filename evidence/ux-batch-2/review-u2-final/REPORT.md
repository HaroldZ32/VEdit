# U2 close-out — independent review (T10)

Reviewer: reviewer-1 (read-only lane; no file under `translations/**` or `src/**` was
edited by this review). Writer reviewed: `junior-engineer-2` (T2), on top of the
regeneration recorded in `evidence/ux-batch-2/integration-translations/`.
Contract: `.mpd/plans/ux-batch-2/00-plan.md` §0/§0.1 (R1–R3) and §2; assignment:
`docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U2 steps 4–5, plus the
Lead's six verification items.

Risk level: **Medium** — the catalogue content is clean (no lost translation, no
unfinished entry, no source touched); the only High-impact item is a
delivery/staging risk that lives outside this lane and is already disclosed by the
writer.

## 1. Result

**PASS** for the U2 close-out. `lrelease` is green with 0 unfinished, the finished
catalogue is set-equal to the post-lupdate baseline minus exactly the 21 vanished
entries, every one of the 1004 pre-existing finished translations is byte-identical,
all 182 newly extracted messages carry a real translation, and the 21 removals are
correctly classified (checked individually, not sampled). The findings below are
carried risks/defects owned by other lanes; none is a regression introduced by T2.

## 2. What I ran (independent re-derivation, not the writer's claims)

Baselines used:

- `evidence/ux-batch-2/integration-translations/_parsed.json` — the **actual
  post-lupdate state**: 1207 rows = **1004 finished + 182 unfinished + 21 vanished**
  (exactly the state the handover §3.U2 records, and the "1004 finished" baseline
  named in the assignment). This is the authoritative "before".
- `evidence/ux-batch-2/integration-translations/00-catalogue-before.ts` — the
  pre-lupdate backup: 1025 rows, of which 1018 carry a normal translation and 7
  carry `<translation type="vanished">` (a naive `<message type="vanished">` count
  on that file returns 0 — the markers sit on the translation element). Used as a
  second, independent cross-check.
- `02-unfinished-before.txt` — the 182-row list, parsed and matched 1:1.

Commands (raw logs in this directory):

| # | check | result |
|---|---|---|
| 1 | `/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/review-u2.qm` | `Generated 1186 translation(s) (1186 finished and 0 unfinished)`, exit 0 — `00-lrelease.txt` |
| 1b | `xmllint --noout translations/vedit_zh_CN.ts` | exit 0 — `00-lrelease.txt` |
| 1c | `lconvert` round-trip of the compiled `.qm` | 1186 messages, 0 unfinished, 0 vanished; `接口` / `全部替换` / `例化次数` readable from the `.qm` |
| 2 | full key-set diff vs `_parsed.json` (`12b-vs-baseline.py`) | 1004/1004 finished present with **identical** text, 0 lost, 0 changed; 182/182 unfinished now translated, 0 missing, 0 still-empty; 21/21 vanished gone and recorded; 0 current keys outside baseline-active; 0 baseline-active keys missing — `12b-vs-baseline.log` (`VERDICT SET-EQUALITY: OK`) |
| 2b | same diff vs `00-catalogue-before.ts` (`10-compare.py`) | 1004 carried with same text; 21 missing = exactly the 21 removed sources (`lost == stated removed sources: True`); 0 translation bodies changed — `10-compare.log` |
| 3 | 182-row placeholder/accelerator/emptiness audit (`11-new182.py`) | 182/182 present; 0 `%n` set mismatches; 0 `&` accelerator mismatches; 0 identical-to-source **English** copies (the only three same-as-source rows are `%1 · %2 · %3`, the symbols-only ` · ⚠ %1`, and the pre-existing Chinese source `修复`); whole-file garbage/placeholder scan 0 — `11-new182.log` |
| 4 | per-entry classification check, all 21 (`13-source-checks.log`) | 18/21 vanished sources have **0** occurrences in `src/**`; `&Settings` has only member-pointer/comment false positives; the two `Save As` strings are live under context `VEdit` (that is why they are classified stale duplicates); all "reworded" replacement texts exist in `src`; "dead" claims verified — see §4 |
| 5 | terminology audit (both search surfaces + panels + dots) | see §5 |
| 6 | source-immutability proof | set-equality (checks 2/2b) — current = baseline-active; no source added/changed; only 182 translation bodies filled + 21 vanished blocks removed |

Spot-check of the 182: I read every source→translation pair (all 182 printed in
`11-new182.log`), covering the required sections — module tree (#1–#4, #36–#38,
#74–#102), interface panel (#30–#48), search/Ctrl+F hand-off (#111–#129),
diagnostics/autofix (#17–#29, #52–#73), and replace refusals (#103–#182). No
garbage, no English copy, no placeholder, no mistranslated term found.

## 3. Ranked findings

### RF1 — High — `translations/` is untracked; a clean checkout silently builds an English UI
`translations/` shows as `?? translations/`; `git ls-tree -r HEAD -- translations/`
is empty (0 files ever tracked), and `git check-ignore` does not match, so this is
not an ignore rule. `src/app/CMakeLists.txt` only compiles the catalogue when
`EXISTS "${VEDIT_TRANSLATIONS_SOURCE_DIR}/vedit_zh_CN.ts"` (`qt_add_lrelease`), and
the `else` branch logs `…vedit_zh_CN.ts missing); UI stays English` — it does not
fail the build. The same applies to the vendored `translations/qtbase_zh_CN.qm`,
which the build copies in for Qt standard-button Chinese. Consequently none of this
close-out (and none of the earlier 1004 translated rows) lands in a commit unless
the captain stages the directory, and a release built from a clean checkout is
English-only with no visible failure. Direction: captain's U9 commit must stage
`translations/vedit_zh_CN.ts` and `translations/qtbase_zh_CN.qm` (explicit paths,
per AGENT.md §6), and the release build should assert the `.qm` exists. This is
**not** introduced by T2 and is disclosed in the writer's README (its F2); it changes
no content and does not affect the U2 verdict.
Where: `translations/` (both files), `src/app/CMakeLists.txt` `qt_add_lrelease` guard.

### RF2 — Medium — the hover "Fix" button has a Chinese **source** string
`src/workspace/diagnostics/diagnostic_hover.cpp`, `DiagnosticHoverPopup::fixButton_`
is constructed with `QCoreApplication::translate("VEdit", "修复")`. The catalogue
therefore carries `<source>修复</source><translation>修复</translation>` (one of the
182). The zh_CN UI reads correctly, but the English UI shows 修复, and there is no
English source string for future translations to track. Direction: a writer of
`src/**` changes the source to `Fix`, the catalogue translates `Fix` → `修复`, and
the `.qm` is regenerated — one source file and one catalogue line together.
Disclosed by the writer (its F1); outside this lane's `translations/**`-only write
scope, and it must not be "fixed" by editing the catalogue source alone (that would
break the `translate("VEdit", "修复")` lookup and fall back to 修复 in every
language).

### RF3 — Medium — `<location>` paths are relative although the mandated regeneration form is absolute
All 1351 `<location filename=…>` entries are `../src/...`; 0 are absolute. Handover
§3.U2 step 4 and plan §5.1 require `lupdate -locations absolute` precisely because a
previous batch lost 6 translations to relative paths, so the next full regeneration
must use `-locations absolute -recursive src` and be re-verified with the §2
set-equality check. Not a content defect today (`lrelease` and the compiled `.qm`
are clean); the file is not re-derived here because running `lupdate` now would
rewrite locations. Disclosed by the writer (its F3).
Where: `translations/vedit_zh_CN.ts` `<location>` elements.

### RF4 — Medium — the plan's `lrelease` gate has no gate surface to run
Plan §5.1 step 3 names `lrelease` in the final 7-gate list, and the handover §4/U8
calls it a `package.json` gate id. `package.json` has no `verify:lrelease` (or
`lrelease`) script and `build/gates/*.sh` contains no `lrelease` invocation (grep
returns nothing). `qt_add_lrelease` inside the build compiles the `.qm` but does not
fail on unfinished translations, so the "0 unfinished" acceptance can only be a
manual command today. Direction: add a `build/gates/lrelease.sh` (run
`/usr/lib/qt6/bin/lrelease … -qm <tmp>` and assert `0 unfinished`) or record in U8
that the gate is run manually. Not disclosed by the writer; owner is the captain/U8,
not the U2 lane — listed so the final acceptance claim is honest about its evidence.


## 4. The 21 vanished entries — classification checked individually

The writer's table claims 14 reworded / 5 dead / 2 stale duplicates. I checked
**all 21**, not the required 8:

- For 18 of the 21 the source string has **0** occurrences in `src/**` (the lupdate
  scan set is confirmed by `01-lupdate.log`: `Scanning directory 'src'...`). The
  three exceptions are accounted for: `&Settings` matches only C++ member-pointer
  expressions and a comment, and the two `Save As` strings are live under context
  `VEdit` — which is precisely why those two rows are stale duplicates, not dead.
- 14 "reworded": each named replacement exists in `src` (verified by grep; two are
  line-wrapped C++ literals — `module_tree_integration.cpp` `installModuleTree` (the
  `setPlaceholderHint` call) and `module_tree_panel.cpp` `ModuleTreePanel::activateItem`
  — so whole-phrase grep needs a fragment),
  and each replacement message exists in the catalogue **translated** (e.g.
  `module catalog: retrying…`→`模块目录：工程模型就绪后重试…`,
  `cataloguing %1 project file(s)…`→`正在编目 %1 个工程文件…`,
  `Invalid branch name…`→`分支名称无效…`, `No tags`→`没有标签`).
- 5 "dead": `Kind` has no translatable literal anywhere in `src`; the signal-driver /
  signal-load jump strings are gone with the net/driver/load rows designed out
  (`module_tree_panel.h` states the pure module hierarchy has no nets/drivers/loads);
  `&Settings` is gone — its only two `src` hits are a C++ member-pointer
  `&SettingsDialog::accept`/`reject`/`applyPending` (grep false positive) and a
  comment in `main.cpp`, while the live label is `&Edit` (`app_shell.cpp`
  `setMenuLabel("edit", tr("&Edit"))`) — translated `编辑(&E)`.
- 2 "stale duplicate": `Save As failed: %1` and `Save As refused (headless)…` are
  emitted with context `VEdit` (`app_shell.cpp`
  `QCoreApplication::translate("VEdit", …)` in `ApplicationShell::saveActiveDocumentAs` and
  `ApplicationShell::confirmOverwrite`), and the active `VEdit`
  rows are translated (`另存为失败：%1`, `另存为已被拒绝（无界面）…`); the removed
  `vedit::app::ApplicationShell` rows were unreachable duplicates.
- The removed-JSON rows' `prior_translation` values match `00-catalogue-before.ts`
  verbatim for all 21 (0 mismatches), and all 21 are absent from the finished file
  (0 still present, 0 unrecorded).

Discrepancy worth recording: the pre-lupdate backup already carried 7 of the 21 as
`<translation type="vanished">`; lupdate then marked 14 more. 7 + 14 = the 21 of the
1207 baseline, which is why the backup has 1025 rows rather than 1207.

## 5. Terminology — verified against the user's words

- `替换` / `全部替换`: `vedit::editor::FindReplaceBar` and
  `vedit::workspace::search::SearchPanel` both translate `Replace`→`替换`,
  `Replace All`→`全部替换`; the two buttons are distinct and consistent across the
  Ctrl+F bar and the panel.
- `修复`: the hover Fix button translation is `修复` (see RF2 for the source-string
  defect).
- Interface panel columns: `Name`/`Direction`/`Type`/`Width` → `名称`/`方向`/`类型`/`位宽`;
  the extra real column `Location` → `位置`.
- Module tree columns: `Module`/`Instantiations`/`Definition` → `模块`/`例化次数`/`定义位置`.
- Interface panel title: `module_tree_integration.cpp`
  `QCoreApplication::translate("VEdit", "Interface")` → `接口` (not `界面`).
- Three dot states: `FileStatusDotDelegate` paints colour only (red error / amber
  warnings-only / green clean / no dot = never reported) and contains no `tr()` or
  `translate()` call, so there is no catalogue string to check; the legend is U3's
  manual content, correctly out of this lane.
- No misuse of `界面` for the port panel: the single `SettingsPanel`-context
  `Interface` → `界面` is the Preferences **appearance group box**
  (`QGroupBox(tr("Interface"))` in `settings_panel.cpp` `buildAppearancePage`), a
  different UI element that must not be renamed.

## 6. Declined to judge

- `vedit::workspace::settings::SettingsPanel` `Interface`→`界面` — it is the
  appearance group box, not the interface panel; correct as written, changing it
  would be a defect.
- Plan §B1 suggests the module-tree column `例化数`; the shipped/Lead's-frozen brief
  says `例化次数`. §B1 calls the columns a suggestion ("列建议") and the Lead's
  assignment names `例化次数`, so no finding.
- Three dot states — no catalogue string exists (colour-only delegate); the legend
  belongs to U3, so there is nothing for U2 to translate.
- English technical tokens kept in translations (`Ctrl+click`, RE2, slang,
  verible-verilog-lint, KIB units, `VEdit`, `URL`, `Git`, `Subversion`) — intentional
  key/term names, not untranslated copies.
- The 5 translations lupdate pre-filled via its "same-text heuristic" — they are
  indistinguishable from the 182 in the baseline and read correctly in the finished
  file; no action.
- Literary quality of the 182 translations — judged for completeness, terminology
  and placeholder fidelity, not as a native-speaker proofread.
- The packaged AppImage/AppDir `.qm` — not rebuilt by U2; I verified the build-tree
  `target/qt6-build/src/app/translations/vedit_zh_CN.qm` is newer than the `.ts` and
  contains 1186/0-unfinished with the new strings; the actual repack is U8.
- The pre-existing 936-entry A2 baseline — not reconstructible from this workspace;
  the immediate pre-U2 baseline (1004 finished) is the correct superset comparison
  and is the one the assignment names.

## 7. Verdict

**PASS** — the U2 close-out is content-correct: `lrelease` reports 1186 finished /
0 unfinished, all 1004 pre-existing translations are preserved byte-for-byte, all
182 new messages are translated, all 21 vanished entries are correctly classified
and removed, no `<source>` was modified, and the terminology matches the user's
words; the carried findings (RF1–RF4) are delivery/process actions for other owners,
not regressions of this lane.

## 8. Bounds of the review

- Did not run `lupdate` (deliberate: it would re-derive locations and undo the
  close-out), the full build/ctest, or the AppImage repack; the only toolchain runs
  were `lrelease`, `lconvert`, `xmllint`, grep and the Python diff scripts recorded
  here.
- No GUI screenshot: this is a catalogue review; the dot states were checked in code
  (`FileStatusDotDelegate`), not on a rendered file tree.
- Read the writer's README and the handover first, then re-derived every claim
  above from the baseline files, the source tree and the finished catalogue; no
  item rests on the writer's narrative alone.
- Raw evidence: `00-lrelease.txt`, `10-compare.py/.log`, `11-new182.py/.log`,
  `12b-vs-baseline.py/.log`, `13-source-checks.log`, `14-repo-build.log` in this
  directory.
