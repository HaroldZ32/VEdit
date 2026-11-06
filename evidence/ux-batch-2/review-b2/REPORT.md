# Review — UX batch 2, lane B2 (search dock ↔ Ctrl+F alignment, project search and replace)

- Contract: `.mpd/plans/ux-batch-2/00-plan.md` §`B2`, with §1 (measured facts) and §2 (bounds)
- Deliverable under review: `evidence/ux-batch-2/b2/` (README with the acceptance matrix, `logs/01..05`,
  `fixture/before|after` + `before-after.diff`)
- Review date: 2026-11-05
- Reviewer: independent; findings only, no source/test/gate edits
- **Risk level: HIGH**
- **Verdict: FAIL**

## Assessment

The headline B2 claims are real. There is **one** option model, not two synchronised copies:
`vedit::search::QueryOptions` lives in `src/workspace/search/search_query.h`, and
`vedit::editor::FindOptions` is a plain `using` alias of it (`src/editor/kernel/find_replace.h`); both
surfaces read/write the same four fields and the same `SearchScope` enum, and the only other
struct that carries the three booleans (`SearchOptions` in `search_types.h`) is the backend
transport built from the widgets, not a second UI model. The dock really gained whole-word, regex,
a scope selector and a replace row; 「替换」 applies exactly the range the Rust matcher reported and
advances (`replace-one.*`), and 「全部替换」 groups per file and calls the **real** `atomicSaveText`
once per file (the test wraps the real writer with a counter: `replace-all.each-file-once`). The
Ctrl+F hand-off is honest and works in both directions, including the on-demand dock creation via
`installSearch()`'s fallback (`handoff.*`, `shell-handoff.*`), and the in-file search still works in
`CurrentFile` scope (`handoff.bar.in-file-first`). The unsaved-buffer rule is implemented and tested
in its basic form: modified in-scope documents are saved first, a refused save refuses the whole
replace with nothing written, and written documents are reloaded (`unsaved-buffer.*`). I reproduced
`SUMMARY checks=81 failures=0 skips=0` inside the pinned `vedit-qt6-build:local` image on the current
tree, so the author's 81/81 claim is not just a log. The reentrancy fix is right: `SearchHub::publish`
and `SearchHub::requestProjectSearch` copy their observer/handler list before iterating, and
`shell-handoff.dock-created` exercises exactly the path that used to segfault (the fallback creates
the panel, whose constructor registers itself as a handler during the call).

The failure is the lane's highest-risk property. The replace pipeline trusts both the **ranges** and
the **buffer state** across asynchronous gaps, and the "protections" the README claims are narrower
than the code:

- after the save-first step the batch **reloads every written document unconditionally**, so edits
  typed while the counting scan (or the async write) runs are silently discarded — the exact
  "disk content overwrites newer memory content" the contract forbids (Blocker 1);
- `applyReplaceRanges` only checks that the line is long enough and the hit is in bounds; it never
  checks that the text at the range is the match, so an out-of-date range silently replaces the
  wrong characters (probe: `STALE ... -> 'X  assign bar =BARo + foo;'`, `ok=1`) — and 「替换」 uses
  hits from an arbitrarily old browse scan (High 2);
- a counting scan that hits the 15 s budget is **not** refused (only `truncated` and non-`Done`
  are), so 「全部替换」 replaces a partial subset while showing the partial count as if it were the
  total (High 3);
- a UTF-8 BOM shifts every line-1 column by one between the Rust matcher (which counts U+FEFF) and
  the C++ replace path (whose `QStringDecoder` drops the BOM): probe `BOM ... -> 'mMODa;'` where the
  correct result is `MOD a;` (High 4);
- the dock's `CurrentFile` scope is not the active editor document at all: `SearchHub::currentFilePath`
  is only written by `FindReplaceBar::openBar`/`requestProjectSearch` and `SearchPanel::activateHit`,
  and the shell never updates it on tab/pane changes, so the scope searches (and 「全部替换」
  writes) the last file touched through the find bar or a result click (High 5).

Secondary: a mid-batch failure is reported without the already-written counts (and some range
errors do not even name the file), the Replace button follows `currentHitIndex_` rather than the
tree's selected row, opening the Search dock wipes a live Ctrl+F bar's query/options, the save-first
saves **every** modified document under the project root (not only the affected ones), and the
contract-required RE2-vs-PCRE note is still absent from `docs/USER_GUIDE.md`.

Nothing was weakened inside B2's own scope: the existing `vedit-workspace-search-selftest` and
`vedit-editor-find-replace-selftest` are untouched by B2 and pass (the diffs in `search_selftest.cpp` /
`search_jump_target.*` predate this lane per `logs/05-write-scope.txt` and belong to the earlier jump
refactor), the Verilog-only filter still means the same extension set (`v/vh/vlg/sv/svh`), the
worker-thread job/poll loop is unchanged, and the full ctest log is 44/44 with the new test at #32.

## Findings (ranked)

### Blocker 1 — 「全部替换」 reloads written buffers unconditionally, silently discarding edits made after the save-first step

**Where:** `src/workspace/search/search_panel.cpp`, `SearchPanel::replaceAllRequested` (saves, then
starts the asynchronous all-matches scan) and `SearchPanel::onReplaceFinished` (loops over
`outcome.writtenFiles` and calls `OpenDocumentBridge::refresh` with no state check);
`src/workspace/search/document_sync.cpp`, `EditorOpenDocumentBridge::refresh` →
`src/editor/kernel/document_io.cpp`, `loadFileIntoDocument` → `KTextEditor::Document::setText`.

**What happens:** the save-first step runs *before* `beginScan()`, so the counting scan and the
worker-thread replace both operate on the just-saved disk content. The scan is asynchronous and the
editor stays editable while the dock says "Counting matches…" (`setBusy` is only called for the
write, not the scan). If the user types during that window (up to the 15 s scan budget, plus the
write), the buffer is modified again, but neither `confirmAndReplace()` nor `startReplace()` checks
that. When the batch finishes, `onReplaceFinished` calls `refresh()` for every written file;
`refresh()` does not look at `isModified()` and `loadFileIntoDocument()` calls `document->setText()`,
which replaces the buffer and clears its modified flag. The user's post-save edits are destroyed
with no prompt and no recovery path in the editor — the contract's own sentence "不得静默用
磁盘旧内容覆盖内存新内容" is violated by the reload, not by the write.

**Why it matters:** this is silent loss of user work, in the lane's headline operation, on a window
the user can hit by continuing to type while the dock counts. The evidence README's known-limitation
#3 only covers an *external* file change between scan and write; it does not mention this path, and
`testUnsavedBuffer` never edits between the save and the replace (it appends the marker *before*
`replaceAllRequested()`, then asserts the reload). `unsaved-buffer.buffer-reloaded` is therefore a
test of the intended reload, not of the guard that does not exist.

**Direction of a fix:** treat the write-time buffer state as part of the contract: re-check
`modifiedPaths()` (or at least the written files' `isModified()`) after the scan and immediately
before `startReplace`, save again or refuse; and never reload a document whose buffer is modified —
prompt, or keep the newer buffer and report the divergence. The reload path should own the
`isModified` check itself, so no future caller can discard work by calling `refresh()`.

### High 2 — out-of-date ranges are applied with only a length check, so the wrong characters can be replaced silently

**Where:** `src/workspace/search/project_replace.cpp`, `applyReplaceRanges` (the `characters != target`
and "runs past the end" checks are the only validation; it never compares the text at the range);
`src/workspace/search/search_panel.cpp`, `SearchPanel::replaceCurrentHit` (uses `hits_.at(index)` from
the last browse scan, then saves the current buffer and applies that old range);
`src/workspace/search/search_types.h`, `ReplaceHit` (carries line/column/length, no matched text);
the claim in `src/workspace/search/README.md` that a range "与当前内容不符" is refused.

**What happens:** for 「替换」, the hit list can be minutes old. `replaceCurrentHit()` first saves the
modified buffer (the user's newer text) and then applies the old range to that new text. If the edit
shifted the match on the line but the line is still long enough, `applyReplaceRanges` succeeds and
overwrites the wrong characters; if lines were inserted above, the old line number now points at a
different line. I compiled the reviewed `project_replace.cpp` (without the editor-core define) in the
pinned image and called the real function:
`STALE ok=1 replaced=1 -> 'X  assign bar =BARo + foo;'` for a hit computed on
`"  assign bar = foo + foo;"`, versus the control `CTRL ... -> '  assign bar = BAR + foo;'`.
For 「全部替换」 the scan is fresh, so the in-session case is the buffer edit; an external process
that changes a file between the scan and the write is the same class, and the same-length variant is
admitted in the evidence README (no hash/re-text check).

**Why it matters:** a successful `Replace Outcome` with the wrong bytes written is worse than a
refusal: the user has no signal, and the file is changed under the editor. The README's "拒绝而不是
部分替换" bullet overstates the guard — only out-of-file and too-short hits are refused.

**Direction of a fix:** make the matcher report the matched text (or let the C++ side re-scan the
specific file immediately before applying and use those ranges), and compare the text at the range
before writing; refuse on mismatch with the file and line in the message. For 「替换」, re-run the
query for the hit's file before applying, or invalidate the result list when a document changes.

### High 3 — a timed-out counting scan is not refused, so 「全部替换」 can replace a partial subset and present its counts as the total

**Where:** `src/workspace/search/search_panel.cpp`, `SearchPanel::onSearchFinished` (replace-scan
branch): it refuses `summary.state != SearchState::Done` and `summary.truncated`, but not
`summary.timedOut`; `src/workspace/search/global_search.cpp` sets `summary.timedOut` from the FFI;
`src/workspace/search/search_types.h`, `SearchSummary::timedOut`.

**What happens:** `vd-search` stops at `maxDurationMs` (default 15 s) with `timed_out=true` and a
partial hit set, but the job is still reported as `Done`. The replace-scan branch sees `Done`,
`truncated=false`, and calls `confirmAndReplace()`; the dialog and the result show the partial
`hitCount`, the replace applies exactly those hits, and the status says "Replaced N occurrence(s) in
M file(s)." Nothing tells the user the scan did not finish, so "全部替换" is silently incomplete —
and the count the user was shown is the incomplete one. The browse path does display
"time budget … reached" (`summaryText`), which makes the omission in the replace path a clear
asymmetry rather than a deliberate design.

**Why it matters:** the acceptance is semantic ("改且只改预期处、计数正确"); a replace-all that
silently covers a scanned prefix is a wrong result for a large project, where the budget is most
likely to expire.

**Direction of a fix:** refuse the replace on `timedOut` exactly like `truncated` (with a message
that says the scan stopped at the budget), or surface the incompleteness in the confirmation and
the result. The same review should decide whether scan-level skips (`skippedTooLarge`,
`skippedUnreadable`) need to be named in the replace confirmation; today they are not.

### High 4 — UTF-8 BOM files: the Rust matcher counts the BOM, the C++ replace path drops it, so line-1 replacements land one character late

**Where:** `src/workspace/search/project_replace.cpp`, `ProjectReplacer::run`
(`QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless)`);
`crates/vd-search/src/lib.rs`, `scan_file` (`String::from_utf8_lossy(&bytes)` then character columns
over the line, which retains U+FEFF on line 1).

**What happens:** a UTF-8 BOM is a valid U+FEFF scalar; `from_utf8_lossy` keeps it, so the matcher
reports line-1 columns that include it (e.g. `module` in `BOM+"module a;"` is column 2). Qt's
`QStringDecoder` with these flags silently drops the leading BOM — I verified it in the pinned
image: `decoder: size=10 first=U+006D startsWithBom=0` for `BOM+"module a;\n"`. The replace path
therefore walks one character less before the hit. Calling the real `applyReplaceRanges` with the
Rust-reported range gives `BOM ok=1 replaced=1 -> 'mMODa;'` (the intended result is `MOD a;`): the
wrong six characters are replaced and the line is corrupted. Hits on lines ≥ 2 are unaffected; the
BOM is also silently dropped when the file is rewritten (so the byte content changes beyond the
replacement).

**Why it matters:** BOM files are common in the Windows-authored Verilog/SystemVerilog this editor
targets, and the corruption is silent. The fixture has no BOM (`writeFixture` writes plain UTF-8),
so 81/81 does not cover it.

**Direction of a fix:** make both sides use the same text. Either strip a leading U+FEFF before
matching in `vd-search`, or decode with `Flag::ConvertInitialBom` in the replace path (and keep the
BOM on write), and add a BOM fixture with a line-1 hit to `vedit-search-replace-tests`.

### High 5 — the dock's `CurrentFile` scope searches the last file touched by the find bar/result click, not the active editor document

**Where:** `src/workspace/search/search_query.h`, `SearchHub::setCurrentFilePath/currentFilePath_`;
`src/workspace/search/search_panel.cpp`, `SearchPanel::buildOptions` (`fileFilter =
SearchHub::currentFilePath()`) and `SearchPanel::openDocumentsInScope`; the only writers are
`FindReplaceBar::openBar` and `FindReplaceBar::requestProjectSearch`
(`src/editor/kernel/find_replace.cpp`) and `SearchPanel::activateHit`; `installSearch`
(`src/workspace/search/search_integration.cpp`) connects only `projectRootChanged`.

**What happens:** `EditorPane`/`EditorArea` expose `currentFilePath()` and active-tab/pane change
callbacks, but nothing wires them to the hub. Consequences: on a fresh start with a document open
and no Ctrl+F use, choosing `Current File` in the dock and pressing Search says "No current file.
Open a document or choose Whole Project."; after clicking a search result in file A and switching
to file B, the scope still searches A; and 「全部替换」 in `CurrentFile` scope saves and rewrites A
(the confirmation lists A, which is the only mitigation). Every panel test in
`search_replace_selftest.cpp` sets `WholeProject` via `setSearch`, so the dock's `CurrentFile` scope
is not exercised anywhere.

**Why it matters:** the scope selector is an explicit contract item ("当前文件 | 全工程"); a
"current file" that is a stale pointer is a wrong-target search, and in the replace flow it writes
a file the user did not intend.

**Direction of a fix:** update the hub from the editor area's active-document changes (the shell
already has the callbacks), or resolve the active pane's `currentFilePath()` at search/replace time;
add a test that opens two documents, switches tabs, and asserts the dock's `CurrentFile` scope hits
the second one.

### Medium 6 — a mid-batch failure reports only the error, not the files already written (and some range errors do not name the file)

**Where:** `src/workspace/search/search_panel.cpp`, `SearchPanel::onReplaceFinished` (the
`!outcome.ok && !outcome.cancelled` branch calls `setReplaceStatus(tr("Replace failed: %1")…)`);
`src/workspace/search/project_replace.cpp`, `ProjectReplacer::run` (breaks on the first failure,
leaving earlier `writtenFiles`/`filesChanged`/`replacements` populated) and `applyReplaceRanges`
(the "line N is shorter…" error has no file name).

**What happens:** the `ReplaceOutcome` carries the counts, but the user-visible status drops them
(and `replaceFinished` has no product consumer). A batch that wrote four files and then failed
reports as a plain failure, so the user cannot tell whether anything changed; for a range mismatch
the message names a line but not the file. The evidence README says "结果里 ok=false + 原因 +
已写文件计数", which is true of the object, not of the UI.

**Why it matters:** after a partial write the user must know the workspace is now half-replaced;
the contract requires the operation to report counts, and a silent partial write invites a
confused retry.

**Direction of a fix:** include `filesChanged`/`replacements` and the failing file path in the
status (and in the error text produced by `applyReplaceRanges`).

### Medium 7 — 「替换」 follows `currentHitIndex_`, not the result row the user selected

**Where:** `src/workspace/search/search_panel.cpp`, `SearchPanel::connectSignals` (only
`itemClicked`/`itemActivated` call `activateItem`) and `SearchPanel::replaceCurrentHit`
(`currentHitIndex_`).

**What happens:** keyboard navigation in the result tree changes the current/selected row without a
click; `currentHitIndex_` keeps the previously clicked hit (or -1 → first hit). Pressing 「替换」
then replaces a different occurrence than the highlighted one. The contract's wording is "只替换
当前选中的那一条".

**Why it matters:** the wrong occurrence is rewritten on disk (same query, different location), with
no undo at the panel level.

**Direction of a fix:** connect `QTreeWidget::currentItemChanged` (or read the tree's current item
in `replaceCurrentHit`) instead of relying on click-only state.

### Medium 8 — creating the Search dock resets a live Ctrl+F bar's query and options

**Where:** `src/workspace/search/search_panel.cpp`, `SearchPanel::connectSignals` (the final
`SearchHub::publish(handoff(), this)` in the constructor) and `SearchHub::publish`
(`src/workspace/search/search_query.h`); `FindReplaceBar::adoptFromHub`
(`src/editor/kernel/find_replace.cpp`).

**What happens:** the panel's constructor publishes its own defaults (empty query,
case-insensitive, `WholeProject`). If the bar exists and is visible (the user searched in the file),
opening the dock for the first time — e.g. View → Search Panel / Ctrl+Alt+S, which goes through
`PanelRegistry::showPanel` — makes the hub notify the bar, which adopts the empty query and
`WholeProject` scope and recomputes to zero in-file matches. The bar is wiped by an action that
should have carried the query *to* the dock. In the bar's own whole-project hand-off the fallback
re-applies the hand-off afterwards, so the final state there is correct; the manual "open the dock"
path has no such repair.

**Why it matters:** it is a surprising state reset in the exact cross-surface workflow B2 exists to
support; the user has to retype a query they already had.

**Direction of a fix:** do not publish the panel's defaults from its constructor; seed a newly
created surface from the hub's current snapshot (subscribe-with-replay) instead of pushing the new
surface's defaults over it, or publish only on user edits.

### Medium 9 — the save-first step saves every modified document under the project root, not the affected files

**Where:** `src/workspace/search/search_panel.cpp`, `SearchPanel::openDocumentsInScope` (filters
`modifiedPaths()` by the project root only) and `SearchPanel::ensureBuffersSaved`.

**What happens:** for `WholeProject`, every open document under the root that differs from disk is
saved before the scan, including files with no hits and files the user never intended to save; the
default dialog only says "N unsaved editor document(s) were saved first" and does not name them.
An unrelated buffer that cannot be saved refuses the whole Replace All. The evidence README §3.2
says "被替换的文件" in one sentence and "工程根下的所有已打开文档" in the next; the code is the
latter. Also, `ensureBuffersSaved` saves the documents one by one and returns on the first failure,
so if the second save fails the first document has already been written to disk — the README's
"整个替换被拒绝，磁盘上不会发生任何写入" is true of the replace writes (they never start) but not of
the save-first writes. This is not data loss (the writes persist the user's own text), but it is an
unrequested side effect on files outside the operation's target, a refusal trigger that has nothing
to do with the query, and an inaccurate user-facing promise.

**Why it matters:** the rule's intent is to protect the files being replaced, not to flush the
project; a user who cancels the confirmation has still had unrelated buffers written.

**Direction of a fix:** either restrict the save set to the files that the (fresh) scan reports and
re-scan after saving, or name the documents in the confirmation and let the user choose; keep the
refusal rule for the affected set.

### Medium 10 — the contract-required RE2/PCRE note is still missing from `docs/USER_GUIDE.md`, and no integration step owns it

**Where:** `docs/USER_GUIDE.md` (no "RE2", "backreference", "lookaround" or project-search section);
the plan §B2 ("差异必须写进 docs/USER_GUIDE.md"); `evidence/ux-batch-2/b2/README.md` §3.1 (the text,
handed to the captain); plan §5.1 (integration steps) has no docs step.

**What happens:** B2's write scope excludes `docs/`, so the lane could not satisfy the sentence; it
recorded the paragraph and deferred it, but the captain's integration list (§5.1) does not include
merging it, so it will be missed. The page users are pointed to for the new feature does not mention
project search/replace at all.

**Why it matters:** the contract made the dialect difference a user-facing requirement precisely
because the two surfaces now look identical; a user who moves a backreference pattern from Ctrl+F to
the dock gets an "invalid regex" error with no explanation in the manual.

**Direction of a fix:** add the paragraph to the user guide in the integration step (and the
project-search/replace section while there); it is a documentation task, not a code change.

### Nits

- `src/workspace/search/search_panel.h` declares `void beginReplaceAllScan();` with no definition or
  call site — a dead declaration.
- `evidence/ux-batch-2/b2/logs/02-ctest-new.log` is a direct `vedit-search-replace-tests` run
  (`EVIDENCE /vedit-build/evidence/ux-batch-2/b2/fixture`), not the ctest invocation (whose registered
  `--evidence` path is `${CMAKE_CURRENT_BINARY_DIR}/search-replace-evidence`); the file name
  overstates what it is, even though the CHECK lines are genuine.
- `src/workspace/search/search_panel.cpp`'s Verilog toggle tooltip lists `.v/.vh/.sv/.svh` while the
  actual filter is `v/vh/vlg/sv/svh` (and `search/README.md` lists the same four); the README's
  extension table and the tooltip disagree with the code by `.vlg`.
- The "每文件一次写" sentence in `search/README.md` and the evidence README is true of the replace
  writer only; a modified open buffer is written once by `document_sync.cpp::save` and again by the
  replace (`atomicSaveText` twice). Both writes are atomic and ordered, so this is wording, not a
  safety defect.
- `src/editor/kernel/find_replace.h` includes `../../workspace/search/search_query.h` (a lower layer
  reaching into a workspace lane by relative path). It is header-only and build-safe, but it makes
  the editor kernel's public header depend on a lane file; a shared header (e.g. under
  `src/common/`) would be the cleaner home.
- The dock's replace path applies the `maxResults` (2000) and `maxDurationMs` (15 s) limits to the
  whole walk even in `CurrentFile` scope; a current-file replace can be refused because the project
  is large, not because the file is. (The `timedOut` case is High 3; this is the false-refusal side.)

## Declined to judge

- **Cross-file transaction.** The contract asks for "每文件一次原子写", not a transaction; the
  implementation is per-file atomic and leaves earlier files written on a mid-batch failure. That
  policy is acceptable; only the *reporting* is a finding (Medium 6).
- **Save-before-confirm side effect.** Clicking 「全部替换」 and then cancelling still saves the
  modified buffers. This is deliberate (the counts must match the saved text) and the dialog says
  so; it is not data loss (the writes persist the user's own content).
- **Different initial switch states** of the dock (case-insensitive, WholeProject) and the bar
  (case-sensitive, CurrentFile). Documented as UI policy; the four options themselves are shared and
  the model is one type.
- **The extra dock-only Verilog filter and the bar-only selection toggle.** They are outside the
  four contract options and are documented as such; I did not treat their absence on the other
  surface as a divergence.
- **External same-length file change between scan and write.** Disclosed in the evidence README as a
  known limitation (no hash check); the contract does not require one. The *in-session* stale-range
  case is High 2; the external case is declined.
- **Non-UTF-8 files are skipped and reported** (`skippedFiles`), never rewritten from lossy text.
  This is the right behaviour; I did not treat the count difference between matches and replacements
  as a defect.
- **EOL/encoding normalization in the save-first path.** `document_sync.cpp::save` writes
  `document->text().toUtf8()`, which bypasses KTextEditor's EOL/encoding mode; the product's normal
  save (`src/editor/kernel/ktexteditor_provider.cpp::saveFile`) does the same, so this is a
  pre-existing product-wide behaviour, not introduced by B2. (It does mean the new automatic save
  can transcode a modified CRLF/Latin-1 buffer; noted for the captain, not counted against B2.)
- **A directory-fsync failure after the rename in `atomicSaveText` returns `ok=false` although the
  file was already replaced** (`src/editor/kernel/atomic_save.cpp`); the replace path then neither
  counts nor reloads that file. Pre-existing kernel behaviour, very rare (EIO on the directory),
  outside B2.
- **Astral characters.** Verified correct by probe (`ASTRAL_AFTER -> '😀 BAR'`, `ASTRAL_SELF -> 'X foo'`),
  but there is no test case; I did not treat the missing case as a defect, only as coverage.
- **The bar's in-file search after switching back from `WholeProject`.** By code
  `FindReplaceBar::recompute()` restores it; no test covers the switch-back. Weak coverage, not a
  defect I could demonstrate.
- **`regex` is added directly to `crates/vd-search/Cargo.toml`** instead of the workspace manifest;
  the plan's A1 wording asked to stop and report if the dependency was not pre-wired. That is A1's
  lane and outside B2's write scope; noted for the captain.
- **`SearchHub` handlers are keyed by raw `const void*`** and the shell's fallback captures `&shell`;
  in the product the shell is app-lifetime and the test removes its handler before the shell dies.
  Low risk; not judged.
- **Translations of the new strings.** `translations/**` is A2's scope and the plan §5.1 step 1
  assigns the merge to the captain; not counted against B2.
- **The 2000-hit cap and 15 s budget themselves.** Deliberate safety limits with visible refusals
  (except the missing `timedOut` refusal, High 3).
- **Other lanes' dirty files.** `search_jump_target.*`, `search_selftest.cpp` and the jump-registry
  edits predate this lane (`logs/05-write-scope.txt`); I attribute them to the earlier batch and did
  not review them.

## Verified vs only read

**Verified by running (in `vedit-qt6-build:local`, the pinned image):**

- the whole B2 acceptance harness on the current tree: `SUMMARY checks=81 failures=0 skips=0`,
  including `model.both-surfaces.same-options`, `replace-one.*`, `replace-all.*` (counts, one write
  per file, CRLF and CJK preservation), `handoff.*`, `shell-handoff.*` (the reentrancy path) and
  `unsaved-buffer.*` (save-first, refuse, reload);
- the real `applyReplaceRanges` compiled from the reviewed source: `BOM … -> 'mMODa;'`,
  `STALE … -> 'X  assign bar =BARo + foo;'` (both `ok=1`), `CTRL -> '  assign bar = BAR + foo;'`,
  `ASTRAL_AFTER -> '😀 BAR'`, `ASTRAL_SELF -> 'X foo'`;
- Qt's UTF-8 decoder with the exact flags used by the replace path: a leading BOM is dropped
  (`size=10 first=U+006D startsWithBom=0`).

**Only read (no run):**

- the unconditional `refresh()` discard of a *modified* buffer (Blocker 1) — the code path is
  `onReplaceFinished → refresh → loadFileIntoDocument → setText`, and no test drives a post-save
  edit; I could not exercise it without writing a new GUI test;
- the timed-out counting scan (High 3) — I did not fabricate a >15 s project; the code path is
  `GlobalSearch` mapping `timed_out` into a `Done` summary and the replace branch not checking it;
- the dock's `CurrentFile` staleness (High 5) — no test touches that scope;
- the partial-batch status (Medium 6) — I did not inject a write failure;
- the keyboard-selection mismatch (Medium 7) and the dock-open wipe (Medium 8) — read only;
- the worker-thread guarantee of `ProjectReplacer` — `ranOnWorkerThread()` exists but no test
  asserts it; the scan's worker-thread check (`large.worker-thread`) does pass;
- the atomic save's crash behaviour — I read `atomic_save.cpp` (write/fsync/rename/dir-fsync) but did
  not inject faults.

## Bounds

- **Read:** the frozen contract (§1/§2/§B2 and §5.1), the B2 README and all five logs, the fixture
  before/after trees and diff, `search_query.h`, `search_types.h`, `search_panel.{h,cpp}`,
  `project_replace.{h,cpp}`, `document_sync.{h,cpp}`, `global_search.cpp`, `search_integration.cpp`,
  `find_replace.{h,cpp}` (the scope/handoff/replace paths), the new and existing search selftests,
  `search/README.md`, `crates/vd-search/src/lib.rs` (matcher/range/timeout), `crates/vd-ffi/src/search.rs`
  (the ABI fields), `atomic_save.cpp`, `document_io.cpp`, `ktexteditor_provider.cpp::saveFile`,
  `editor_area.cpp::saveTab`, `docs/USER_GUIDE.md`, the search CMake, and the plan's integration list.
- **Did not read:** the other lanes' implementations (A1/A2/A3/A4/B1/B3/B4), the full 1300-line
  `find_replace.cpp` outside the B2 paths, the entire Rust crate's tests, or the older batch evidence;
  the current tree has 400+ modified/untracked files from other lanes, and B2's sources are the ones
  dated 04:23–04:55; `search_jump_target.*`, `search_selftest.cpp` and the jump registry predate this
  lane and were not reviewed.
- **Ran:** the B2 acceptance test in `vedit-qt6-build:local` (81/81); three function-level probes
  linking the reviewed `project_replace.cpp` (BOM/stale/astral); one Qt decoder probe. I did **not**
  rerun `bash scripts/build-qt.sh` (other lanes are mid-flight; the writer's 44/44 log is from 04:58),
  nor the host GUI (the host binary hangs after 7 checks — a host KTextEditor/offscreen environment
  issue, the container run is clean), nor xcb, the AppImage, clippy, fmt, docs or lrelease, nor a
  15 s-timeout or mid-batch-failure simulation, nor a real KTextEditor edit-during-scan interaction.

## Verdict

**FAIL** — the shared model, the exact-range replacement, the hand-off, the save-first/refuse rule and
the reentrancy fix are all real and the 81/81 evidence reproduces, but 「全部替换」 unconditionally
reloads written buffers and silently discards edits typed after the save-first step, out-of-date
ranges are applied with only a length check, a timed-out counting scan is replaced as if complete,
a UTF-8 BOM shifts line-1 columns and corrupts the replaced range, and the dock's `CurrentFile`
scope does not follow the active document — the lane's data-safety property is not closed.
