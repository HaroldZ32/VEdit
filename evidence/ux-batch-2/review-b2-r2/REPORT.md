# Review — UX batch 2, lane B2 RE-REVIEW after the repair (project search / replace)

- Frozen contract: `.mpd/plans/ux-batch-2/00-plan.md` §`B2` (acceptance ①–⑥, the unsaved-buffer sentence)
- Original review: `evidence/ux-batch-2/review-b2/REPORT.md` (FAIL: 1 Blocker, High 2–5, Medium 6–10)
- Repair under review: `evidence/ux-batch-2/b2-repair/` (README, `logs/01`–`06`, `bom-probe.log`,
  `acceptance/`)
- Review date: 2026-11-05
- Reviewer: independent; findings only — no source, test or gate was edited. The probe in
  `evidence/ux-batch-2/review-b2-r2/probes/` is reviewer-only, outside the product build.
- **Risk level: HIGH**
- **Verdict: FAIL**

## Assessment

The repair is real work, not a test rename. The Blocker is closed: the reload now goes through
`OpenDocumentBridge::refresh(path, expectedToken, …)`, which refuses a modified buffer **and** a buffer
whose SHA-256 content token no longer equals the token captured right after the save-first step; the
panel reloads only `ReplaceOutcome::writtenFiles` and names `notReloadedFiles` in the status. I reproduced
the guard with the real `EditorOpenDocumentBridge` and a real KTextEditor document: an edit typed after the
save survives, the document stays modified, an edit that the user also *saved* during the batch is kept
(token mismatch), an unmodified buffer holding the written content is reloaded, and a buffer that was not
open at capture time follows the modified/unmodified rule (probe B3–B7). The writer's 174/174 suite also
reproduces on the current tree (`SUMMARY checks=174 failures=0 skips=0` in `vedit-qt6-build:local`), the
original 81 names are all present and PASS (`logs/06-original-81-parity.txt`), and the full gate log is
47/47 ctest plus the xcb smoke.

H3, H4 and H5 are closed too: the counting scan's `timedOut` is refused for both phases before the
confirmation (test `replace-timeout.*`; the code branch is `onSearchFinished`'s `phase != None` branch),
the replace writer decodes with `Stateless | ConvertInitialBom` so the BOM counts as U+FEFF on line 1 and
round-trips (my probe keeps `EF BB BF`, CRLF and astral columns; the test's fixture really starts with
`EF BB BF` and asserts the exact bytes), and the dock's CurrentFile scope resolves
`editorArea()->activePane()->currentFilePath()` at search/replace time through the factory-installed
provider (real-shell, two-pane test `current-file.*`). The Mediums 6–9 are fixed with real assertions, and
the `unsaved-buffer.refused` check is **not** weakened: it still asserts refused, error-names-refused and
no disk write, and the new asynchronous refusal is covered by `save-failure.*`.

The problem is High 2. The range re-check exists and is correct *when the query used for the scan is the
query the writer validates against*: literal (escaped both sides), whole-word (`\b(?:…)\b` both sides),
regex, case, BOM/CRLF/astral columns, and a mismatch refuses the whole file (probe A1–A11; test
`stale-batch.*`). But the validator is built inside `SearchPanel::startReplace` from
`SearchPanel::replaceQuery()`, i.e. from the **current query field and option buttons at write time**, not
from the `SearchOptions` that produced `hits_`. The query field and the case/whole-word/regex toggles stay
enabled during the whole two-phase counting scan (`beginScan` disables only the Search button; `setBusy` is
not called until the write and even then does not disable the query/options), the confirmation dialog shows
only the query text (not the option state), and an empty query makes `RangeMatcher` inactive by design. So
a user who keeps working during the count can turn a whole-word scan into a literal validator, or clear
the query into the legacy "length-only" path, and the old ranges are applied to the file the user has
just saved. My panel-level probes show exactly that: a whole-word scan of `foo` → the toggle is cleared
during the count and `foo_bar` is saved → the dialog still says `Replace "foo" with "BAR"` → the file
becomes `BAR_bar` (F3); clearing the query and saving `fix fix` → the old ranges overwrite it as
`BAR BAR` (E3); the single 「替换」 with the query cleared writes `  assign bar = BAR + foo;` over `fix`
(D5), the original review's corruption; and a stale row after a line-shift edit replaces a *different*
occurrence of the same query with no signal (A3/C4). The writer's README documents only the
"same text at the same offset" residual, not the query/option-identity hole, so this is not a disclosed
limitation.

Secondary: the Medium-9 save-set narrowing now misses matches that exist only in an unsaved buffer (a
modified open document with no disk hit is neither saved nor rescanned), the post-save scan re-resolves the
CurrentFile scope and can silently widen to WholeProject if the active pane loses its document, the cancel
status says "nothing was written" although the affected buffers were saved before the dialog, the
no-editor-core build silently skips the whole rule (the header promises it does not), and Medium 10 is
still not in `docs/USER_GUIDE.md` (carried; the plan's §5.1 still has no docs step that would merge it).

## Per original finding

| # | Finding | Status | Artefact / probe |
|---|---|---|---|
| Blocker 1 | Replace All reloads written buffers unconditionally, discarding edits made after the save-first step | **FIXED** | `logs/03-b2-green.log` `edit-during-replace.*` PASS; probe B3–B7 with the real `EditorOpenDocumentBridge`: `refresh` → `Changed` for a modified buffer and for a saved-but-different buffer, `Reloaded` for the unmodified/token-matching control, `Changed`/`Reloaded` for the empty-token cases; `onReplaceFinished` reloads only `writtenFiles` and reports `notReloadedFiles`; the second-batch guards hold (`searching_ || replacing_ || replacer_.isRunning()`, `ProjectReplacer::start`'s thread wait). Residual: the narrow read→write save race (declined below), and a skipped reload leaves the buffer unmodified (nit). |
| High 2 | Out-of-date ranges are applied with only a length check | **NOT FIXED** | Guard itself works for an unchanged query: probe A1 (literal), A2 (stale text refused), A4 (literal metachar), A5 (whole-word embedded refused/standalone accepted), A6 (regex valid/stale), A7 (case-insensitive), A8–A10 (BOM/CRLF/astral), A11 (two hits); `stale-one.*`, `stale-batch.*`, `regex-replace.*`, `whole-word-replace.*` PASS. But the guard is keyed to the **mutable current query/options**, not the scan's: probe F3 (whole-word scan + toggle cleared during the count + saved `foo_bar` → `BAR_bar`), E3 (query cleared + saved `fix fix` → `BAR BAR`), D5 (single 「替换」, query cleared + saved `fix` → `BAR`), A3/C4 (line shift → different occurrence replaced). The empty pattern makes `RangeMatcher` inactive; `startReplace` does not reject it. |
| High 3 | A timed-out counting scan is not refused | **FIXED** | `search_panel.cpp::onSearchFinished` refuses `summary.timedOut` for both `ReplaceScanPhase::Initial` and `PostSave`, before `finishInitialReplaceScan`/`confirmAndReplace`; test `replace-timeout.*` (50 ms budget, 1.5 M-line file, 2-hit file) asserts `!ok && !confirmed` and no write; the single-hit path has no counting scan, so no partial batch. |
| High 4 | UTF-8 BOM shifts line-1 columns between the Rust matcher and the C++ writer | **FIXED** | `project_replace.cpp::ProjectReplacer::run` decodes with `Stateless | ConvertInitialBom`; probe A8 keeps `EF BB BF` and replaces both line-1/line-2 hits correctly; A9 CRLF bytes `4241520d0a…`; A10 astral `😀 BAR`; test `bom.start-bytes`/`bom.mid-bytes`/`bom.astral-bytes` use a real `EF BB BF` fixture and assert exact bytes; `bom-probe.log` shows the decoder flags. |
| High 5 | CurrentFile scope is a stale hub path, not the active document | **FIXED** | `search_integration.cpp::installSearch` installs `setCurrentFileProvider([&shell]{ … editorArea()->activePane()->currentFilePath() })`; `SearchPanel::resolveCurrentFile` is called by `buildOptions` and by `replaceAllRequested` at operation time; test `current-file.*` uses a real `ApplicationShell`, two files in two panes, `setActivePane`, and asserts 1 hit in each pane; golden log `filter='…/pane-b.v'` then `…/pane-a.v`. |
| Medium 6 | Mid-batch failure reports no counts; range errors do not name the file | **FIXED** | `onReplaceFinished` prints `Replace failed after writing %1 occurrence(s) in %2 file(s): %3`; `applyReplaceRanges` names the file in the overlap/short/runs-past errors; `ensureBuffersSaved` names the failing buffer; test `mid-batch.*` (1 file, 2 replacements, b.v named) PASS. |
| Medium 7 | 「替换」 follows `currentHitIndex_`, not the selected row | **FIXED** | `replaceCurrentHit` reads `tree_->currentItem()->data(0, Qt::UserRole)` and the panel tracks `currentItemChanged`; test `selected-row.*` (third row selected without a click, third line replaced). |
| Medium 8 | Creating the dock resets a live Ctrl+F bar | **FIXED** | The constructor no longer publishes; `connectSignals` seeds from `SearchHub::findBarSnapshot()` via `adoptFromHub`, and the bar publishes through `publishFromFindBar`; test `dock-open.*` (bar query/options/matches preserved, dock adopts them). |
| Medium 9 | Save-first saves every modified document; wording overstates | **FIXED** (with residuals) | `replaceAllRequested` scans first; `finishInitialReplaceScan` intersects the hit files with `modifiedPaths()` and saves only those, then re-scans; the failure message says the earlier saves persist and the confirm dialog names the saved documents; tests `save-only.*`, `save-failure.*` PASS. Residuals: a buffer-only match in a modified doc with no disk hit is not saved/rescanned (new Medium below); the cancel status still says "nothing was written"; the null-bridge path silently skips the save rule. |
| Medium 10 | The RE2/PCRE note is missing from `docs/USER_GUIDE.md` and no integration step owns it | **NOT FIXED** | `b2-repair/README.md` carries the Chinese paragraph, but `docs/USER_GUIDE.md` still has no RE2/PCRE, backreference/lookaround or project-search text (`grep` = 0) and plan §5.1 still has no docs step; the second half of the finding ("no integration step owns it") is unchanged. Correctly outside the lane's write scope, but it is a captain carry, not a fix. |

## The gate sentence

**Could a user still lose typed text or have the wrong characters replaced by using Replace All on this
tree? — Yes.** With the whole-word scan's counting pass running, the user clears the whole-word toggle
(the dialog still shows only the query) and saves `foo_bar` at the scanned range; the worker's literal
validator accepts the stale range and the file becomes `BAR_bar` (probe F3). Clearing the query instead
takes the empty-pattern fast path and writes the old ranges as `BAR BAR` over the saved `fix fix` (probe
E3). The reload guard keeps the user's buffer, but the wrong characters are on disk and the status reports
a successful replace. That is not a defensible no, so the lane fails.

## Findings (ranked)

### Blocker (remaining, the H2 guard) — Replace All validates against the current query/options, not the scan's, so a query/option change during the counting pass writes the wrong characters

**Where:** `src/workspace/search/search_panel.cpp` — `SearchPanel::replaceQuery()` (reads `query_->text()`,
`caseButton_`, `wholeButton_`, `regexButton_` at write time), `SearchPanel::startReplace()` (passes that
query to `ProjectReplacer::start`), `SearchPanel::beginScan()` / `SearchPanel::setBusy()` (the query field
and the case/whole/regex toggles stay enabled during the counting scans and the replace), and
`SearchPanel::replaceCurrentHit()` (no non-empty-query check at all);
`src/workspace/search/project_replace.cpp` — `RangeMatcher` (an empty `pattern` means "no validation"; the
options come from the current query), `ProjectReplacer::run`.

**What happens:** the counting scan is built from `SearchOptions` captured at `beginScan`; the worker's
guard is built from the widgets at `startReplace`, after the (async) initial scan, the affected-buffer
saves, the (async) re-scan and the confirmation. The user can edit the query/options throughout that
window (the editor and the dock's option widgets are not disabled). A whole-word scan whose toggle is
cleared before the write becomes a literal validator, so a stale range sitting on the `foo` inside
`foo_bar` is accepted; an emptied query makes `RangeMatcher::active()` false, i.e. the old length-only
behaviour; a changed non-empty query either refuses valid hits or accepts a different one. The
confirmation dialog's text is `Replace "<query>" with "<replace>"…` and does not show the option state, so
the whole-word change is invisible. In the single 「替换」 path the save-first step writes the edited
buffer *before* the worker reads it, so a stale row is applied to the user's just-saved text with only the
(possibly disabled) guard between it and the disk; the reload afterwards replaces the buffer and wipes the
undo history.

**Evidence:** `evidence/ux-batch-2/review-b2-r2/probes/probe-output.log`:
`PROBE F3.after='BAR_bar'` (whole-word scan; toggle cleared during the count; the saved `foo_bar` is
corrupted), `PROBE E3.after='BAR BAR'` (query cleared; the saved `fix fix` is overwritten),
`PROBE D5.after='  assign bar = BAR + foo;'` (single replace, query cleared, the stale `fix` range is
written), `PROBE C4.wrong-occurrence-replaced` (line shift; a different `foo` is replaced). The probe
sources and the exact checks are in `…/review-b2-r2/probes/probe.cpp`; the product suite's own tests do not
change the query/options between the scan and the write, so 174/174 does not cover this.

**Why it matters:** it is the original H2 corruption, in the lane's headline operation, with a success
status and no warning; the user's just-saved text is replaced with the wrong characters. The contract's
acceptance ③ ("改且只改预期处、计数正确") and ② ("「替换」只替换当前选中的那一条") are both violated,
and the fix's own guard is what is bypassed.

**Direction of a fix:** freeze the operation's query at scan time (capture `ReplaceQuery` together with
`SearchOptions` when the scan starts and carry it through `finishInitialReplaceScan`/`confirmAndReplace`
to `startReplace`); refuse in `startReplace` when the pattern is empty or when the current query/options
differ from the scan's, and re-scan instead; disable the query field and the case/whole/regex/scope
widgets while a counting scan or replace is running; invalidate `hits_` (or re-run the query for the
affected file) when the query/options change or an open document is edited. For the single 「替换」, the
cheapest closure is the review's earlier direction: invalidate/refresh the result list on document change,
or carry a file version/hash with the hits. Add a test that changes the whole-word toggle and clears the
query during the count and asserts the replace is refused.

### High — the single 「替换」 can replace a different occurrence of the same query after a line-shift edit

**Where:** `src/workspace/search/search_panel.cpp::replaceCurrentHit` (uses `hits_.at(index)` and saves the
current buffer first), `src/workspace/search/project_replace.cpp::RangeMatcher::matchesAt` (text at the
range only), `SearchPanel::activateHit`/the result tree (no invalidation on document change).

**What happens:** the user deletes a line above a selected hit; `replaceCurrentHit` saves the edited buffer,
then the worker reads the new text and the old line/column. If the new line at that index contains the same
query at the same column, the validator accepts and replaces that occurrence — not the one the user
selected. `PROBE A3.line-shift-accepted` (`abc foo\nxyz BAR\n` from a hit that referred to the old line 2)
and `PROBE C4.wrong-occurrence-replaced` at panel level (`abc foo\nxyz BAR\n`) show it; the reload then
shows the result and discards the undo history. The writer's README documents this only as "the exact same
matched text at the exact same offset", so it is a disclosed limitation — but it is still a wrong-target
write in acceptance ②.

**Direction of a fix:** invalidate/re-run the result list when the document text changes (or carry a
document version/file hash with each hit and refuse on mismatch); for the single replace, re-scan the
hit's file and match the selected occurrence by identity, not by stale coordinates.

### Medium — the Medium-9 save set misses matches that exist only in an unsaved buffer

**Where:** `src/workspace/search/search_panel.cpp::finishInitialReplaceScan` (intersects `hitFiles()` — the
**disk** scan — with `OpenDocumentBridge::modifiedPaths()`), `SearchPanel::replaceAllRequested` (the initial
scan runs before any save).

**What happens:** a modified open document with **no** hit in its disk copy is not in `hitFiles()`, so it is
not saved and not rescanned; a query match that exists only in that buffer is never counted or replaced,
while Replace All reports a successful count for the other files. The user has unsaved content the
operation silently skipped. This is a consequence of the Medium-9 direction (save only the affected files)
and is not covered by `save-only.*` (its `b.v` has no match in either copy).

**Direction of a fix:** scan the open buffers (or save all modified documents in the selected scope and
re-scan) before deciding the affected set, or state in the confirmation that buffer-only matches are not
included.

### Medium — the post-save scan re-resolves CurrentFile and can silently widen to WholeProject

**Where:** `src/workspace/search/search_panel.cpp::finishInitialReplaceScan` calls
`beginScan(buildOptions(true), ReplaceScanPhase::PostSave)`; `buildOptions` calls `resolveCurrentFile()`
again, and `beginScan` does not repeat the "empty filter under CurrentFile" guard that
`replaceAllRequested`/`startSearch` have.

**What happens:** if the active pane loses its document between the initial scan and the post-save scan
(close/untitled switch), the re-scan's `fileFilter` is empty and the scan walks the whole project; the
confirmation lists the project files and the replace writes them, although the user selected CurrentFile.
The initial scan's saves were only for the original current file. The dialog's file list is the only
signal.

**Direction of a fix:** carry the initial scan's scope/fileFilter into the post-save scan and refuse (or
re-prompt) when the current file is gone.

### Medium — the cancel/refusal wording contradicts the already-persisted saves

**Where:** `src/workspace/search/search_panel.cpp::confirmAndReplace` (cancel branch:
`Replace All cancelled; nothing was written.`), `onSearchFinished`'s `timedOut`/`truncated`/`cancelled`
replace branches (after `finishInitialReplaceScan` has already saved the affected buffers).

**What happens:** the confirmation dialog names the saved documents, but cancelling then says "nothing was
written", and a timed-out/truncated/cancelled post-save scan refuses without mentioning the saved files.
The user's own buffers are on disk; the status says otherwise. This is the Medium-9 "accurate wording"
concern, narrowed to the paths the repair did not reword.

**Direction of a fix:** say "N document(s) were saved; nothing was replaced" in those branches.

### Medium 10 (carry) — the RE2/PCRE note is still not in the user guide

As in the original finding: the text is carried in `evidence/ux-batch-2/b2-repair/README.md`, but
`docs/USER_GUIDE.md` has no project-search/RE2 paragraph and `.mpd/plans/ux-batch-2/00-plan.md` §5.1 has no
integration step that would merge it. The lane's write scope excludes `docs/`, so the correct action is a
captain carry — but the finding is not closed in the deliverable.

### Nits

- `src/workspace/search/search_panel.cpp::finishInitialReplaceScan` treats a null `OpenDocumentBridge`
  (`bridge == nullptr`) as "no affected documents" and proceeds to the confirmation; the single-replace
  path's `ensureBuffersSaved` refuses in that case. `document_sync.h` says the rule is never silently
  skipped. Product builds link the editor core, so this is a latent non-product path, but the two paths
  disagree.
- `SearchPanel::onReplaceFinished`'s token-mismatch `Changed` leaves the buffer unmodified while the disk
  has the worker's output; the status names the file, but a close without saving discards the only copy
  (only reachable in the narrow read→write race).
- `testCurrentFileScopeFollowsActivePane` inherits the process-wide `SearchHub::findBarSnapshot_` from the
  earlier `testDockOpenKeepsBar` (the green log shows `whole=1`), so its option state is contaminated; the
  scope assertion is still valid, but the test is not isolated. `waitForPanelIdle`'s return is ignored at
  the `unsaved-buffer.refused` call (the subsequent error assertion catches a hang).
- The `captureReloadTokens`/`refresh` path calls `openDocuments()` (a widget-tree walk) once per hit file
  on the GUI thread; for a project with many open-adjacent hits this is avoidable work, not a data-safety
  defect.

## Weakening check (item 6)

`unsaved-buffer.refused` is now asynchronous because the save runs after the first counting scan. The test
waits for the panel to be idle and still asserts the same three properties: `lastReplaceOutcome().ok` is
false, the error contains "refused", and the disk is byte-identical (`readText(aPath) == diskBefore`).
The assertion is genuinely preserved, not silently weakened; the red run's PASS is expected because the
check already passed before the repair, and the new asynchronous refusal is covered by `save-failure.*`
(the failing buffer is named, the earlier save is reported, no replacement is written) and
`save-only.*`. The only test-quality issue is that `waitForPanelIdle`'s return value is ignored at that
call site, but a hung pipeline still fails on the error assertion.

## Declined to judge

- **The read→write save race.** The worker reads a file, the user saves it during `atomicSaveText`, and the
  worker's rename overwrites the just-saved bytes. The reload guard does not discard the buffer (the token
  mismatch returns `Changed`), and the buffer keeps the user's text, but the disk copy is lost if the user
  closes without saving. This is the same class as the external-file change the original review declined
  (no hash/compare-and-swap in the contract); the window is per-file and narrow. Not counted as the lane's
  Blocker; the unmodified skipped-reload buffer is a nit above.
- **BOM drop on open/save.** `QString::fromUtf8` in `loadFileIntoDocument` drops U+FEFF; pre-existing
  editor-kernel behaviour outside B2 (`src/editor/kernel/document_io.cpp`); the replace writer's BOM
  preservation is verified.
- **Symlinked/aliased open paths.** `openDocuments()`/`isOpen()` compare `QFileInfo::absoluteFilePath()`,
  not canonical paths; a file opened through an alias may not be recognized by the bridge. The buffer is
  never discarded, and the original review did not require canonicalisation.
- **Per-file atomic, not transactional, batch; the 2000-hit cap; the 15 s budget; skipped non-UTF-8
  files.** Accepted policy from the first review; the timed-out refusal is now correct.
- **`SearchHub` handlers keyed by raw `const void *` / the shell-lifetime fallback.** Pre-existing seam;
  the product shell is app-lifetime and the test removes its handler.
- **Dock defaults vs bar defaults, and reopening the dock resets its query.** UI policy; Medium 8's
  bar-preservation is fixed.
- **Other lanes' dirty files / the full build.** I did not rerun `bash scripts/build-qt.sh` end to end;
  the writer's 47/47 + xcb gate log and my direct 174/174 run in `vedit-qt6-build:local` are what I stand
  behind.
- **The file-row selection fallback in `replaceCurrentHit`** — the fallback is the last current hit, not a
  new defect.

## Verified vs only read

**Verified by running (in `vedit-qt6-build:local`, the pinned image):**

- the B2 suite on the current tree: `SUMMARY checks=174 failures=0 skips=0` (binary built after the repair's
  sources; the writer's `logs/03-b2-green.log` has the same summary), including `replace-timeout.*`,
  `stale-one.*`, `stale-batch.*`, `bom.*`, `mid-batch.*`, `selected-row.*`, `regex-replace.*`,
  `whole-word-replace.*`, `dock-open.*`, `save-only.*`, `save-failure.*`, `edit-during-replace.*`,
  `current-file.*`;
- the independent probe at `evidence/ux-batch-2/review-b2-r2/probes/probe.cpp` (output
  `probe-output.log`), linked against the reviewed `libvedit_workspace_search.a`:
  A1–A11 (literal/whole-word/regex/case, stale text/complement, BOM/CRLF/astral/line-1, two hits),
  B1–B7 (real `EditorOpenDocumentBridge` + real KTextEditor document: modified-after-save survives,
  saved-after-save survives, unmodified/token-matching reloads, empty-token cases),
  C4 (panel-level line-shift wrong occurrence), D5 (panel-level single replace with a cleared query writes
  the stale range), E3 (panel-level Replace All with a cleared query writes the stale range), F3
  (panel-level Replace All after a whole-word→literal option change writes `BAR_bar`).

**Only read (no run):**

- the timed-out path on a real >15 s project (the test's 50 ms budget is the proxy);
- the real modal dialog and a human typing/closing panes at the exact scan-time positions (the probes
  inject at the same code point; the C4/D5/E3/F3 handlers stand in for the user's save, but the code path
  is the same);
- the other lanes, the xcb-interactive session, the AppImage, clippy/fmt/docs/lrelease;
- `bash scripts/build-qt.sh` end to end.

## Bounds

- **Read:** the repair README and all its logs, `bom-probe.log`, the acceptance before/after diff;
  `document_sync.{h,cpp}`, `project_replace.{h,cpp}`, `search_panel.{h,cpp}`, `search_query.h`,
  `search_types.h`, `search_integration.cpp`, `global_search.cpp` (the summary mapping), `crates/vd-search/src/lib.rs`
  (matcher, columns, BOM/CRLF, timeout), `document_io.cpp`, `atomic_save.h`, `find_replace.cpp` (the
  publish seam and the scope path), `editor_area.cpp` (Save As/open-path property), `docs/USER_GUIDE.md`,
  the plan §B2/§5.1, `search/README.md`, and the shipped test source `search_replace_selftest.cpp`.
- **Not read:** the full 1300-line `find_replace.cpp` outside the bar/hub/scope paths, the other lanes'
  implementations, the rest of the editor kernel, or the prior batch evidence.
- **Could not run:** a user-driven GUI/xcb session, the real 15 s large-project scenario, a real
  concurrent user save during the worker's atomic write, and the other gates (the writer's 47/47 gate log
  is quoted, not re-run).

## Verdict

**FAIL** — the Blocker 1 reload discard is genuinely fixed, and H3, H4, H5 and Mediums 6–9 are closed, but
High 2 is not: the range validator is keyed to the query field and option buttons the user can change while
the counting scan runs, and an emptied query disables it, so Replace All can still apply the old ranges to
a file the user has just saved and write the wrong characters (`foo_bar` → `BAR_bar` in probe F3,
`fix fix` → `BAR BAR` in E3), with the single 「替换」 path reproducing the original `fix` → `BAR`
corruption (D5); the gate sentence therefore answers **yes**, and a remaining real wrong-character /
saved-text-loss path must fail the lane.
