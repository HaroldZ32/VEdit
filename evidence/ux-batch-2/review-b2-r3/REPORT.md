# Review — UX batch 2, lane B2, THIRD pass (repair `b2-repair-r2`)

- Frozen contract: `.mpd/plans/ux-batch-2/00-plan.md` §`B2` (acceptance ①–⑥, the unsaved-buffer sentence)
- Previous verdict: `evidence/ux-batch-2/review-b2-r2/REPORT.md` (FAIL on the High-2 guard: the range
  validator was keyed to the mutable UI query)
- Repair under review: `evidence/ux-batch-2/b2-repair-r2/` (README with per-item before/after,
  `logs/01` red 15 failures, `logs/12` green 229/229, `logs/13` gate 49/49, `logs/09` T23 106, `logs/11`
  parity)
- Review date: 2026-11-05
- Reviewer: independent; findings only — no source, test or gate was edited. The probe in
  `evidence/ux-batch-2/review-b2-r3/probes/` is reviewer-only, outside the product build.
- **Risk level: HIGH**
- **Verdict: FAIL**

## Assessment

The frozen-snapshot repair is real work and closes the exact hole the second pass failed on: `SearchPanel`
no longer has a write-time `replaceQuery()` (grep = 0; only `liveReplaceQuery()` remains, and it is used
solely by `sameLiveIdentity()` as a refusal gate), `ProjectReplacer::start/run` take a `ReplaceSnapshot`,
the worker's `RangeMatcher` is constructed only from `snapshot.query`, every hit carries the `scanId` of the
scan that produced it, and the empty pattern is a refusal on every path (panel entry, post-scan, post-dialog,
`startReplace`, the worker, and both `applyReplaceRanges` overloads). I re-ran the four corruptions that
failed the lane last time — F3, E3, D5 and C4 — at the same code points and all four now refuse with the file
byte-identical (probe P1–P4); the dock's query/option/scope/replace widgets are disabled for the whole
counting scan (N5); the writer's suite reproduces at `229/229` on the current tree and all 174 previous check
names are present and PASS (I diffed the logs myself, not the writer's parity file); the writer's gate log
is 49/49 plus the xcb smoke (quoted, not re-run); the residuals (buffer-only saves, post-save scope freeze,
cancel wording, null bridge) are closed in code and — except the null-bridge branch, which the root build
cannot reach — covered by the suite.

What it does not close is the **document identity** half of High 2. The per-file stamps are captured in
`makeFrozenScan()` when the scan *finishes*, not when the matcher *reads* each file, so a save that lands
during the scan is baked into the baseline: the hits describe the pre-save content, the stamp describes the
post-save content, both guards agree, and the replacement is applied to the wrong occurrences. I reproduced
this through both buttons: the single 「替换」 writes `xyz BAR` over the user's just-saved shifted file (the
selected occurrence `abc foo` is untouched) and reports `Replaced 1 occurrence(s)` (N1); 「全部替换」 writes
`BAR c\nBAR a\nfoo b\n` over a file where the user just inserted a line at the top, replacing the new
`foo c` and the shifted `foo a` while leaving the scanned `foo b`, and reports `Replaced 2 occurrence(s)`
(N1b). Both are the same wrong-target class as C4/A3, with a success status. Separately, the control lock is
not airtight across the synchronized Ctrl+F surface: a whole-project hand-off during the counting scan
(`applyHandoff()` → `startSearch()` → `beginScan()`) abandons the Replace All, starts a new scan whose
generation bump drops the cancelled scan's `finished`, and leaves `operationActive_` set with no owner — the panel is
idle but query/replace/replace-all/replace-edit stay disabled with no message and no recovery except a
project-root change (N2). And a single 「替换」 on a dirty buffer now saves the buffer and then refuses,
because the panel's own save invalidates the stamps frozen at scan time, with a message that blames the
document (N3). The gate sentence therefore still answers **yes**, so the lane fails again; the remaining
defect is narrower than last round but it is the same data-safety class and it is not disclosed.

## The six required items

### 1. The frozen-snapshot fix — PASS (in the narrow sense)

`ReplaceSnapshot{query, scanId, files}` is built from the scan (`SearchPanel::makeFrozenScan`) and carried
through `startReplace` to `ProjectReplacer::run`; `RangeMatcher` has no access to a widget. `SearchPanel::
replaceQuery()` is deleted (`grep -rn replaceQuery src/workspace/search` = 0; `liveReplaceQuery` is used only
inside `sameLiveIdentity`). The confirmation dialog still reads `query_->text()` for its text, but that is
display only; the write uses the frozen value. **No code path constructs a matcher from `QLineEdit`/option
state at write time.** Verified by reading `search_panel.cpp`/`project_replace.cpp` and by N4 (the worker's
API receives only the snapshot) and P1–P4 (the live widgets can be changed programmatically and the frozen
operation still refuses).

### 2. The empty/mismatch refusal, all paths — PASS

- `replaceAllRequested()` refuses an empty trimmed query before starting.
- `onSearchFinished()` (both counting phases) refuses `scanInputChanged_ || operationInputChanged_ ||
  activeScanQuery_ != operationQuery_`, then the failed/cancelled/truncated/timed-out states.
- `confirmAndReplace()` copies the `FrozenScan` **before** the dialog and refuses after it on
  `operationInputChanged_ || !sameLiveIdentity(frozen, …)`.
- `startReplace()` refuses an empty frozen pattern, a missing/zero scan identity, a live mismatch, and any
  hit whose `scanId` differs.
- `replaceCurrentHit()` refuses when the frozen identity no longer matches the live query/options/scope or
  the row's `scanId`, and an idle query change clears the rows first (`onQueryControlsChanged` →
  `invalidateResults`).
- `ProjectReplacer::run()` refuses an empty query, `scanId == 0`, a foreign `scanId`, a file absent from the
  frozen stamp set, and a size/mtime mismatch before and after the read.
- `applyReplaceRanges()` refuses an empty query in both overloads (the 3-arg overload therefore refuses a
  non-empty hit list); there is no "length-only" mode left.

Probe: P1 (F3, whole-word cleared in the confirm handler) refused, `foo_bar` intact; P2 (E3, query cleared
in the confirm handler) refused, `fix fix` intact; P3 (D5, query cleared while idle) did not start, saved
nothing, wrote nothing; N4 (3-arg/4-arg/worker zero-scan/worker empty-query) all refused before any write.
I could not reach these through a real mouse/keyboard because the widgets are now disabled — I used the same
programmatic entry points the code itself documents as the backstop (`setQuery`/`setWholeWords` and the hub
hand-off), which is exactly the injection point the writer's own tests use. The refusal logic above is
reachable from the GUI: the only way a user can change a disabled widget's value is programmatically, so the
probe exercises the backstop, and the disabled-widget state is separately proven by N5.

### 3. The control lock — PARTIAL

The dock's own controls are locked for the whole operation: `updateControlState()` uses
`idle = !searching_ && !replacing_ && !operationActive_` and disables the query field, the case/whole/regex/
Verilog toggles, the scope combo, the Search/Replace/Replace-All buttons and the replacement field. N5
measured `query-options-scope-replace-disabled` PASS during the counting scan (query, whole, regex, scope,
replacement edit all disabled). `operationActive_` spans the two counting scans, the saves and the modal
confirmation; `replacing_` spans the write.

The lock is **not airtight as a state machine**, because the other synchronized surface is not part of it:
the Ctrl+F bar's whole-project hand-off calls `SearchPanel::applyHandoff()`, which calls `startSearch()` →
`beginScan()` with no guard on `operationActive_`/`replacing_`. N2 shows the result: the panel ends idle
(no scan, no replace) yet `queryEdit`, the replacement field and the Replace-All button are all disabled,
`lastReplaceOutcome()` is empty (no refusal, no message) — a dead dock until the project root changes. This
does not write anything, but it contradicts the operational claim that the controls are locked "for the
whole window": the window is simply abandoned, and the user is not told.

### 4. The stamp-based document identity — FAIL (incomplete)

What works: the stamps are checked before **and** after each read (`QFileInfo` size + `toMSecsSinceEpoch`);
a file that changed after the `FrozenScan` was built is refused with the file name and line; a changed
`scanId` is refused. P4 reproduces C4/A3 with the save landing during the panel's save-first step: it is
refused (`the document changed after the scan (size or modification time)`), and the shifted text is kept.
The disclosed residual (same byte length **and** the same millisecond for an external write) is recorded in
`src/workspace/search/README.md` ("已知陷阱与限制" item 8) and is a different, much narrower case.

What does not work: the baseline is captured at scan **finish** (`makeFrozenScan` runs in
`onSearchFinished`), so any save that lands after the matcher read a file and before the scan finishes is
part of the baseline. The stamp check then cannot see it, and the text check cannot see it either when the
same token sits at the same coordinates. Probes N1/N1b reproduce exactly the C4/A3 wrong-target write this
way:
- N1 (single 「替换」): file `foo\nabc foo\nxyz foo\n`, hit at line 2 col 5 (`abc foo`); during the scan the
  user saves the line-deleted `abc foo\nxyz foo\n`; the worker replaces the old line/column, which now holds
  `xyz foo`, leaving `abc foo` untouched → on disk `abc foo\nxyz BAR\n`, `ok=1 replacements=1`,
  status `Replaced 1 occurrence(s) in 1 file(s).`
- N1b (「全部替换」): file `foo a\nfoo b\n`; during the counting scan the user saves an inserted line at the
  top (`foo c\nfoo a\nfoo b\n`); the two old ranges still hold `foo` → on disk `BAR c\nBAR a\nfoo b\n`,
  `ok=1 replacements=2`, status `Replaced 2 occurrence(s) in 1 file(s).` The new `foo c` was replaced and the
  scanned `foo b` was not.

The status is a success in both cases, the confirmation count is the pre-scan count, and the README's claim
that a document changed "after the scan" is caught by the size/mtime stamps does not cover a change *during*
the scan. The writer's README "Remains" mentions only the same-size/same-millisecond external write, so this
is an undisclosed hole, not an accepted limitation. A same-length edit with a changed mtime is refused by the
stamp; a same-length edit that preserves the mtime falls back to the text check and is only indistinguishable
for the disclosed identical-token case. I did not run an external rewrite with a preserved mtime (read, not
run).

### 5. The residual closures — CLOSED, with one accepted side effect

- Buffer-only matches: `OpenDocumentBridge::bufferText()` + `bufferOnlyModifiedCandidates()` save the
  modified open documents outside the disk-hit set whose buffer may contain the query, before the post-save
  re-scan; the suite's `buffer-only.*` checks pass. The prefilter is exact for ASCII literals and
  conservative (save-and-rescan) for regex/non-ASCII; the README discloses the over-save. On a failed save
  the operation refuses, names the failing buffer and keeps the already-saved documents saved.
- Post-save CurrentFile widening: `finishInitialReplaceScan()` re-runs `beginScan(operationOptions_,
  PostSave)` with the request frozen at `replaceAllRequested()`, not `buildOptions()`; `post-save-scope.*`
  passes.
- Cancel wording: `savedDocumentsSuffix()` and the cancel branch name the saved documents and say "nothing
  was replaced"; `cancel-wording.*` passes. This is the right wording now, including the timeout/refusal
  branches.
- Null bridge: `finishInitialReplaceScan()` refuses instead of treating a null bridge as "no affected
  documents"; compile-verified, not runtime-tested in the root build (which always links the default bridge).
- Medium 10 (RE2/PCRE note in `docs/USER_GUIDE.md`): **still not fixed** — `grep -in 'RE2|PCRE|lookaround|
  backreference' docs/USER_GUIDE.md` = 0, and plan §5.1 still has no docs step. Correctly outside the lane's
  write scope; it remains a captain carry, as the previous review said.

### 6. The original findings' statuses

| # | Finding | Status in this pass | Evidence |
|---|---|---|---|
| Blocker 1 | Replace All reloads written buffers unconditionally, discarding edits made after the save-first step | **FIXED** (unchanged) | `EditorOpenDocumentBridge::refresh()` still refuses a modified buffer and a token mismatch; `reloadTokens_`/`notReloadedFiles` wiring intact; `edit-during-replace.*` PASS in my 229 run. |
| High 2 | Out-of-date ranges are applied with only a length check | **PARTIALLY FIXED — the write-time widget key is gone, the frozen document identity is still incomplete** | Frozen snapshot + empty-pattern refusal verified (N4, P1–P4); the new stamp baseline is captured too late and the same wrong-target write is reproduced through a save during the scan (N1, N1b) — see the Blocker below. |
| High 3 | A timed-out counting scan is not refused | **FIXED** (unchanged) | `onSearchFinished()` refuses `summary.timedOut` in both phases before the save/confirm; `replace-timeout.*` PASS in my 229 run. |
| High 4 | UTF-8 BOM shifts line-1 columns between matcher and writer | **FIXED** (unchanged) | `Stateless | ConvertInitialBom` still in `ProjectReplacer::run`; `bom.*` PASS. |
| High 5 | CurrentFile scope is a stale hub path, not the active document | **FIXED** (unchanged) | `installSearch` provider + `resolveCurrentFile()` at operation time; `current-file.*` PASS. |
| Medium 6 | Mid-batch failure reports no counts; range errors do not name the file | **FIXED** (unchanged) | `onReplaceFinished` count/error text and `applyReplaceRanges` file naming; `mid-batch.*` PASS (but see the coverage note on `mid-batch.short-line-names-file`). |
| Medium 7 | 「替换」 follows `currentHitIndex_`, not the selected row | **FIXED** (unchanged) | `replaceCurrentHit` reads `tree_->currentItem()`; `selected-row.*` PASS. |
| Medium 8 | Creating the dock resets a live Ctrl+F bar | **FIXED** (unchanged) | constructor adopts from the hub and does not publish; `dock-open.*` PASS. |
| Medium 9 | Save-first saves every modified document; wording overstates | **FIXED, residuals CLOSED** | affected-set intersection + buffer-only saves + post-save re-scan; `save-only.*`, `save-failure.*`, `buffer-only.*` PASS; cancel wording corrected. Side effect noted under residual risk. |
| Medium 10 | The RE2/PCRE note is missing from `docs/USER_GUIDE.md` and no integration step owns it | **NOT FIXED (carried)** | grep = 0 in the user guide; plan §5.1 has no docs step. |

## The gate sentence

**Can Replace All or Replace still write wrong characters over a file the user just saved? — Yes.**
The save can land during the scan, before `SearchPanel::makeFrozenScan()` captures the per-file stamps. The
hits then describe the pre-save content while the stamps describe the post-save content; the worker's stamp
check and the text check both pass when the same query token sits at the old coordinates, and the replacement
is written to a different occurrence with a success status. Probe N1: `abc foo\nxyz BAR\n`,
`ok=1 replacements=1`. Probe N1b (Replace All): `BAR c\nBAR a\nfoo b\n`, `ok=1 replacements=2`. This is not
a defensible no, so the lane fails.

## Findings (ranked)

### Blocker — the stamp baseline is captured at scan finish, so a save during the scan still yields a wrong-target write

**Where:** `src/workspace/search/search_panel.cpp::onSearchFinished` → `makeFrozenScan` (the only place the
per-file stamps are created; it runs when the scan is already complete), `SearchPanel::captureFileStamp`
(size + mtime at that moment), `SearchPanel::replaceCurrentHit` (the save-first step precedes
`startReplace(lastScan_)` without re-freezing), `SearchPanel::finishInitialReplaceScan` (same for the post-save
scan); `src/workspace/search/project_replace.cpp::ProjectReplacer::run` (the before/after read stamp check
can only compare against that late baseline), `applyReplaceRanges`'s text check (range-local, so it cannot
tell two occurrences of the same token apart).

**What happens:** the matcher reads a file at T0; the user edits and saves it at T1 (T0 < T1 < scan finish);
`makeFrozenScan` stamps the file at T2 > T1; the hit still carries the T0 line/column. The user selects the
T0 occurrence (or confirms Replace All with the T0 count). At write time the file is unchanged since T2, so
`before`/`after` stamps match; the text at the T0 range is the same query token (a different occurrence, or a
newly inserted one), so `RangeMatcher` accepts. The replacement is written at the stale coordinates and the
status reports success. For Replace All the same mechanism applies to the second counting scan, which is the
one whose hits are written.

**Evidence:** `…/review-b2-r3/probes/02-probe-output.log`: `PROBE N1.after='abc foo\nxyz BAR\n' started=1
ok=1 replacements=1 status='Replaced 1 occurrence(s) in 1 file(s).'` (the selected occurrence `abc foo` is
untouched); `PROBE N1b.after='BAR c\nBAR a\nfoo b\n' confirmed=1 ok=1 replacements=2 status='Replaced 2
occurrence(s) in 1 file(s).'` (the new `foo c` replaced, the scanned `foo b` left). Source:
`…/probes/probe.cpp` (`probeSaveDuringScan`, `probeSaveDuringScanReplaceAll`); the probe is built by
`…/probes/build-probe.sh` against the reviewed `libvedit_workspace_search.a`.

**Why it matters:** this is the C4/A3 wrong-target write — "change only the expected occurrence" (acceptance
③) and the single-replace "exactly the selected one" (acceptance ②) are both violated — on a file the user
just saved, with a success status and the pre-scan count. The lane exists to remove exactly this class, and
the repair's own claim ("document changed between the scan and the write is refused by the stamps") is
overstated. It is not disclosed in the repair README (whose "Remains" covers only the same-size/same-ms
external write) or in the lane README (whose invariant says changes "after the scan" are caught).

**Direction of a fix:** make the identity describe the content the matcher actually read, not the file at
scan-finish. Cheapest robust option: carry a per-hit fingerprint from the scan (the Rust side already returns
a line `preview`; add a hash of the full matched line, or of the file content, to `SearchHit`/`ReplaceHit`)
and compare it at write time — that closes the same-token-at-same-coordinates hole independently of stamps.
Alternative/complementary: record the scan start time in the frozen snapshot and refuse any hit file whose
mtime is at or after it (a file touched during the scan may have been read before the touch), or have the
service return per-file read stamps. Add a regression test that injects the save at `hitsReady` before the
freeze (the same injection point the current tests use) and asserts both buttons refuse.

### High — a Ctrl+F whole-project hand-off during the counting scan abandons Replace All and strands the panel's controls

**Where:** `src/workspace/search/search_panel.cpp::applyHandoff` (calls `startSearch()` unconditionally),
`SearchPanel::startSearch` (no `searching_/replacing_/operationActive_` guard; `beginScan` clears results,
resets `replaceScanPhase_` and bumps `scanGeneration_`), `SearchPanel::onQueryControlsChanged` (sets
`operationInputChanged_`, cancels the scan), `SearchPanel::onSearchFinished` (the cancelled scan's `finished`
is dropped by the `GlobalSearch` generation check, so the refusal path that would clear `operationActive_`
never runs), `SearchPanel::updateControlState` (`idle` is false because `operationActive_` stays true).

**What happens:** the bar's whole-project scope calls `SearchHub::requestProjectSearch`, which publishes the
bar's hand-off and invokes the dock's handler; the panel registers itself in its constructor (`applyHandoff`)
and the shell installs a second handler. During a Replace All counting scan a user opens Ctrl+F, types a
query and selects "Whole project" (or presses Enter in project scope). `applyHandoff` sets the query/scope
and calls `startSearch()`; `beginScan` starts a new scan and sets `replaceScanPhase_ = None`, so the
abandoned counting scan's completion is both generation-dropped and misclassified. `operationActive_`
remains true with no operation to finish: the panel is idle, the query/replace/replace-all/replace-edit
controls stay disabled, `lastReplaceOutcome()` carries no refusal and the status line says nothing. Only
`setRootPath` (a project switch — or recreating the dock, if the shell destroys it on close) recovers;
nothing in the panel itself does.

**Evidence:** `PROBE N2.handed=1 idle=1 queryEnabled=0 replaceAllEnabled=0 replaceEditEnabled=0 outcomeOk=0
error=''` with `N2.handoff-delivered PASS`, `N2.no-write PASS`, `N2.controls-recovered FAIL`,
`N2.replace-all-recovered FAIL`; the probe calls the exact public entry point the bar uses
(`SearchPanel::applyHandoff`), and the product path is `FindReplaceBar::requestProjectSearch()` →
`SearchHub::requestProjectSearch` → the panel handler (`search_integration.cpp`).

**Why it matters:** it is a user-reachable dead end in the lane's headline panel: the Replace All is
silently abandoned (no result, no message) and the search dock becomes unusable until the project is
reopened. It also shows that "the controls are disabled for the whole window" is not a state-machine
guarantee — the window can be replaced under the operation.

**Direction of a fix:** guard `startSearch()`/`applyHandoff()` on `searching_/replacing_/replacer_.isRunning()/
operationActive_` — either refuse the hand-off with a visible status ("a replace is running") or route it
through the same cancellation path as a query change (`finishReplaceRefusal` + reset `operationActive_`)
before starting the new scan. Never call `beginScan` while `operationActive_` is set. Add a regression test
that hands off during the counting scan and asserts both "no write" and "controls restored with a message".

### Medium — a single 「替换」 on a dirty buffer saves the buffer and then refuses, blaming the document

**Where:** `src/workspace/search/search_panel.cpp::replaceCurrentHit` (`ensureBuffersSaved` before
`startReplace(…, lastScan_)`, no re-freeze after the save), `SearchPanel::makeFrozenScan` (stamp captured
before the save), `src/workspace/search/project_replace.cpp::ProjectReplacer::run` (stamp check).

**What happens:** the hit file is open and modified. `ensureBuffersSaved` saves it (the save-first rule),
which changes its size/mtime; the frozen stamp predates the save, so the worker refuses with
`the document changed after the scan (size or modification time)`. The user's buffer has been written to
disk but the selected occurrence is not replaced, and the message attributes the change to the document
rather than to the panel's own save. A dirty-buffer single replace only works if the save happens to leave
the size and the millisecond stamp unchanged.

**Evidence:** `PROBE N3.after='foo\nabc foo edited\n' started=1 saves=1 ok=0 error='replace: dirty.v: line 1:
the document changed after the scan (size or modification time); nothing was written to that file (run the
search again)'` — the edit was on line 2 and the hit on line 1, so the range itself was still valid.

**Why it matters:** it is fail-safe (no wrong characters) and the plan's acceptance ⑥ allows refusal with
unsaved buffers, but the button's advertised save-first behavior is effectively broken for any dirty file,
with a misleading reason and a save side effect the user did not ask for. It is not covered by the suite
(the only dirty-buffer single-replace tests are refusal tests).

**Direction of a fix:** after `ensureBuffersSaved`, re-capture the stamps for the hit file (or re-run the
browse scan) before `startReplace`; when the buffer save is the only change, the new baseline is the saved
content and the range check stays meaningful. At minimum, word the refusal as "the document was saved by
this operation and its ranges are stale; run the search again".

### Nit — `mid-batch.short-line-names-file` no longer exercises the error it was written for

**Where:** `src/workspace/search/search_replace_selftest.cpp` (the check after the `mid-batch.*` block) calls
the **3-argument** `applyReplaceRanges("foo\n", {hit at column 5}, "x")` and asserts `!ok` plus the file name.
The 3-arg overload now constructs an empty `ReplaceQuery`, so the call is refused by the new "no query"
gate — whose message also names the file — before the per-line "line is shorter than the search result"
branch can run. The check still passes, but it no longer covers the line-bounds error. This is not an
assertion relaxed to fit the fix; it is a silent coverage regression caused by the new refusal order. Point
the check at the 4-arg overload with a valid query if the line-bounds message is meant to stay covered.

### Nit — the two `confirm-window.*` tests are green-only

The writer discloses that `testConfirmWindowChangeRefused` was added after the red run. Its checks were never
red, so they cannot demonstrate that the post-dialog gate is what fixes the behaviour; they can only show it
is present. I independently drove the same gate in P1/P2 (query/whole-word changed inside the confirm
handler) and it refuses with the correct message, and the code path (`confirmAndReplace`'s copy-before-dialog
plus the post-dialog `sameLiveIdentity`) is visible, so I do not treat this as a defect — it is an
evidence-strength caveat. The same applies to `testBufferOnlyMatchIsSavedAndReplaced`, which was added with
the residual fix.

### Nit — the lane README's invariant overstates the stamp coverage

`src/workspace/search/README.md` ("全工程替换" invariants) says a document changed "after the scan" is caught
by the per-file size/mtime stamps; the stamps are actually captured at scan finish, so a change *during* the
scan is not caught (Blocker above). The "已知陷阱与限制" item 8 documents a different, narrower residual
(same size + same millisecond external write). Record the scan-window gap or close it; do not let the
invariant read as broader than the implementation.

## Weakening check

- The previous suite is intact: I regenerated the name diff myself from
  `evidence/ux-batch-2/b2-repair/logs/03-b2-green.log` (174 checks, 0 failures) and my own run
  `…/review-b2-r3/probes/01-229-suite.log` (229 checks, 0 failures, 0 skips): `missing_from_new=0`,
  `new_only=55`, `old_non_pass=[]` (file: `…/probes/03-parity.txt`). The writer's parity file is a 3-line
  summary; my regenerated diff is the stronger evidence.
- No assertion was relaxed. The one semantic regression is `mid-batch.short-line-names-file` above (same
  assertion, different error path).
- The four required red tests are genuinely red in `logs/01-red-new-checks.log` (15 failures including the
  corruptions `BAR_bar`, `BAR BAR`, `assign bar = BAR + foo`, the wrong occurrence, the scope widening and
  the old cancel wording) and green in `logs/12-green-final.log`; my run reproduces the green state.
- The two `confirm-window.*` tests and the buffer-only test are green-only (noted); they are backstops, not
  proof of a regression fix. Their gates are separately probed (P1/P2) or code-visible.

## Residual risk: the buffer-only save side effect

The buffer-only closure saves modified open documents before the post-save re-scan. It cannot destroy
content — the bytes written are the user's own buffer (`atomicSaveText` of `document->text()`), never lossy
text of the file — and a save failure refuses the operation and names the failing buffer. But in
regex/non-ASCII mode `bufferMayContainQuery()` returns true for every modified document in scope, so the
operation can persist unsaved work in documents it never replaces; the confirmation names the saved
documents and the cancel wording now says they remain on disk, so the user is informed, but the save happens
before the confirmation (declining the dialog does not undo it). This is the accepted Medium-9 design
("save first or clearly prompt", acceptance ⑥) with the over-save disclosed in the README's bounds; I do not
raise it as a defect. In literal-ASCII mode the prefilter is exact (`QString::contains`), so only documents
whose buffer contains the query are saved.

## Declined to judge

- **The narrow read→write race** (the worker's after-read stamp check to the `rewriter` call): unchanged from
  the second review; a save landing in that microsecond window can still be overwritten. Out of proportion
  for this pass, and not what the repair changed.
- **A same-size external write that preserves the mtime** (or lands in the same millisecond): disclosed
  residual (`README` §limits item 8); the text check is the second line of defence and only identical text
  at identical coordinates is indistinguishable.
- **The 15 s real-project timeout** — the suite's 50 ms-budget proxy is what exists; not re-run on a real
  >15 s project.
- **The GUI-thread stat burst in `makeFrozenScan`** (one `QFileInfo` per hit file at scan finish): a
  performance nit bounded by the 2000-result cap, not a data-safety issue; not judged.
- **The live replacement text read at `startReplace` (`replaceEdit_->text()`)**: no product path changes the
  replacement field during the operation (it is disabled), and the value is what the dialog showed; not a
  defect.
- **The hub handler keyed by raw `const void *` and the shell-lifetime fallback**: pre-existing seam,
  unchanged.
- **Medium 10 / docs**: outside the lane's write scope, correctly left to the captain; not re-raised as a
  lane defect.
- **The other lanes, the xcb interactive session, the AppImage, clippy/fmt/docs/lrelease gates**: not this
  lane's change; I quote the writer's 49/49 gate log rather than re-running it.

## Verified vs only read

**Verified by running (in `vedit-qt6-build:local`, the pinned image, offscreen):**

- the product suite on the current tree: `SUMMARY checks=229 failures=0 skips=0`
  (`…/probes/01-229-suite.log`), including `edit-during-replace.*`, `stale-one.*`, `stale-batch.*`,
  `replace-timeout.*`, `bom.*`, `mid-batch.*`, `selected-row.*`, `regex-replace.*`, `whole-word-replace.*`,
  `dock-open.*`, `save-only.*`, `save-failure.*`, `current-file.*`, `frozen-*`, `buffer-only.*`,
  `post-save-scope.*`, `cancel-wording.*`, `confirm-window.*`;
- the independent probe `…/probes/b2-r3-probe` (source `…/probes/probe.cpp`, built by
  `…/probes/build-probe.sh` against `target/qt6-build/src/app/workspace-lane-search/libvedit_workspace_search.a`),
  output `…/probes/02-probe-output.log`: P1 (F3 confirm-window), P2 (E3 confirm-window), P3 (D5),
  P4 (C4 line-shift after the scan) all refuse; N1/N1b reproduce the scan-window wrong-target write;
  N2 reproduces the stranded control state; N3 records the dirty-buffer single-replace refusal; N4 proves
  the empty-query and zero-scan refusals at both `applyReplaceRanges` overloads and in the worker; N5 proves
  the dock controls are disabled during the counting scan;
- my own 174-name parity diff (`…/probes/03-parity.txt`).

**Only read (no run):**

- the real GUI/xcb interaction (the probes inject at the same code points the writer's tests use; the
  widgets are disabled in the product, so I used the programmatic backstop and say so above);
- the null-bridge refusal branch (not reachable in the root build, which always links the default bridge);
- an external rewrite with a preserved mtime;
- the other lanes, the full `bash scripts/build-qt.sh` (the writer's `logs/13` 49/49 + xcb smoke is quoted,
  not re-run), clippy/fmt/docs/lrelease, and the AppImage.

## Bounds

- **Read:** the repair README and all its logs (red 211/15, green 229/0, gate 49/49, T23 106, parity), the
  plan §B2 and §5.1, `src/workspace/search/{search_panel.h/.cpp, project_replace.h/.cpp, search_types.h,
  document_sync.h/.cpp, global_search.cpp, search_query.h, search_integration.cpp, README.md,
  search_replace_selftest.cpp}`, `src/editor/kernel/find_replace.{h,cpp}` (the bar's
  publish/hand-off/scope paths), `docs/USER_GUIDE.md` (grep), and the previous review's report.
- **Not read:** the full 1300-line `find_replace.cpp` outside the hand-off paths, the Rust `vd-search`
  matcher in full, the other lanes' implementations, the rest of the editor kernel.
- **Could not run:** a user-driven xcb session (the programmatic injection is the documented backstop), the
  real 15 s project, a preserved-mtime external rewrite, the null-bridge branch, the full gate (quoted).

## Verdict

**FAIL** — the frozen-query/empty-pattern refusal half of High 2 is genuinely fixed and the residuals are
closed, but the frozen **document** identity is captured too late: a save that lands during the scan is
invisible to the per-file stamps, so the single 「替换」 and 「全部替换」 can still replace the wrong
occurrence of the same query in a file the user has just saved and report success (probes N1/N1b); a
Ctrl+F whole-project hand-off during the count also strands the search dock in a disabled state with no
message (probe N2), so the control lock is not airtight as a state machine. The gate sentence answers
**yes**, and the remaining wrong-character path must fail the lane.
