# B2 repair — round 2 (frozen operation query)

- Contract: `.mpd/plans/ux-batch-2/00-plan.md` §`B2`
- Re-review that failed the lane: `evidence/ux-batch-2/review-b2-r2/REPORT.md`
  (Blocker 1, H3, H4, H5, Mediums 6–9 pass; **High 2 not fixed**: the range
  validator is keyed to the mutable UI query)
- Repair date: 2026-11-05
- Write scope touched: `src/workspace/search/**` only (no
  `src/editor/kernel/find_replace.*` change was needed, no `crates/**`, no
  `src/app/**`, no docs/translations)
- Method: the four required regression tests were written FIRST and run on the
  reviewed tree (`logs/01-red-new-checks.log`, 15 failures / 211 checks), then
  the fix was applied and the same tests re-run green
  (`logs/12-green-final.log`, 229 checks, 0 failures, 0 skips).

## Per item: change, observed before, observed after

### Item 1 — freeze the operation's query at scan time

**Changed**

- `src/workspace/search/project_replace.h`: `ReplaceQuery` gained equality;
  new `ReplaceFileStamp {size, lastModifiedMs}` and
  `ReplaceSnapshot {ReplaceQuery query; quint64 scanId; QHash<QString,
  ReplaceFileStamp> files}`.
- `src/workspace/search/search_types.h`: `SearchHit::scanId` and
  `ReplaceHit::scanId` — the matcher identity of the scan that produced each
  range.
- `src/workspace/search/search_panel.h/.cpp`: `SearchPanel::FrozenScan`
  (query + the exact `SearchOptions` request + `scanId` + per-file stamps),
  built by `SearchPanel::makeFrozenScan()` when a scan finishes; every hit is
  tagged in `appendBatch()` (`hit.scanId = activeScanId_`). The panel now has
  NO `replaceQuery()` at write time; `liveReplaceQuery()` is only used to
  compare the live widgets against the frozen value.
- `src/workspace/search/project_replace.cpp`: `RangeMatcher` is constructed
  from `snapshot.query` on the worker thread. `ProjectReplacer::start/run` take
  `const ReplaceSnapshot &` (no default query argument).

**Observed before** (probe F3 reproduced at panel level,
`logs/01-red-new-checks.log`):
`CHECK frozen-option.no-write FAIL (BAR_bar` — a whole-word scan's stale range
was validated by the literal live query and wrote `BAR_bar`.
**After** (`logs/12-green-final.log`): `frozen-option.refused PASS`,
`frozen-option.no-write PASS` (file still `foo\n`).

**Remains:** the scan id is an in-process monotonic counter, not a
cross-process revision; a restart simply has no rows to replace.

### Item 2 — refuse an operation whose live query/options changed; refuse empty

**Changed**

- `search_panel.cpp::onSearchFinished` (counting phase): refuses before the
  save/confirmation when `scanInputChanged_ || operationInputChanged_ ||
  activeScanQuery_ != operationQuery_`.
- `search_panel.cpp::confirmAndReplace`: copies the counting scan's
  `FrozenScan` BEFORE the confirmation handler and refuses after it if
  `operationInputChanged_` or `sameLiveIdentity()` fails (the handler is a
  re-entrancy point).
- `search_panel.cpp::startReplace`: final gate — refuses an empty pattern, a
  `FrozenScan` without identity, a live query/scope mismatch, or any hit whose
  `scanId` differs from the frozen scan.
- `search_panel.cpp::replaceCurrentHit`: refuses when the result list's frozen
  identity no longer matches the live query/options/scope or the selected row's
  `scanId`.
- `project_replace.cpp::applyReplaceRanges`: an EMPTY query is now invalid (the
  `RangeMatcher` has no "validation disabled" mode). The historical 3-argument
  overload therefore refuses a non-empty hit list instead of silently writing
  unvalidated ranges.
- `project_replace.cpp::ProjectReplacer::run`: also refuses an empty snapshot
  query and `scanId == 0` before opening any file.

**Observed before:** `CHECK frozen-option.refused FAIL (ok=1 confirmed=1)`,
`CHECK frozen-empty.refused FAIL (ok=1 confirmed=1)`,
`CHECK frozen-empty.no-write FAIL (BAR BAR`, `CHECK frozen-single.refused FAIL
(started=1 ok=1)` — clearing the query took the legacy length-only path and
wrote the stale ranges.
**After:** all four refuse; `frozen-empty.no-write` and `frozen-single.no-write`
PASS byte-identical; `frozen-single.no-save PASS (saves=0)` — the single
「替换」 does not even save the buffer.

**Remains:** none known for the write path; the low-level
`applyReplaceRanges()` still cannot detect a line shift by itself (it only sees
text + range) — the panel-level stamps (item 4) close that.

### Item 3 — disable the query field and option toggles for the whole operation

**Changed**: `search_panel.cpp::updateControlState()` is the single owner of
enabled/visible state; `idle = !searching_ && !replacing_ && !operationActive_`.
`beginScan()` sets `searching_`, `startReplace()` sets `replacing_`, and
`replaceAllRequested()` sets `operationActive_` for the whole counting-scan →
save → re-scan → confirmation → write sequence. Query field, case/whole/regex/
Verilog toggles, scope combo, Search/Replace/Replace All buttons and the
replacement field are disabled for that whole window; the pre-existing
`setBusy()` now just re-evaluates this state.

**Observed before:** controls stayed editable during the two counting scans
(`review-b2-r2` F3/E3); the tests now assert
`frozen-option.controls-disabled-during-scan PASS` and
`frozen-empty.query-disabled-during-scan PASS`, then
`frozen-option.query-controls-restored PASS` after the refusal.

### Item 4 — invalidate hits on query/option/document change

**Changed**

- `search_panel.cpp::onQueryControlsChanged()` is connected to the query field,
  all four option toggles and the scope combo: while idle it clears the rows and
  the frozen identity (`invalidateResults()`); while a scan/replace is in flight
  it records the change and cancels the now-meaningless scan; the operation
  refuses at the next safe point.
- Document change: each scan freezes size + millisecond mtime per hit file
  (`SearchPanel::captureFileStamp`). `ProjectReplacer::run` compares the stamp
  BEFORE and AFTER reading each file; a change refuses that file with the file
  name and line (no range from an older document state is applied). This closes
  the line-shift hole where the same text sits at the old line/column.
- Scope/file-filter/root are part of the frozen identity
  (`sameLiveIdentity`), so a CurrentFile operation cannot silently become a
  whole-project operation.

**Observed before** (review probes C4/D5/A3):
`CHECK frozen-shift.refused FAIL (ok=1 replacements=1)` and
`CHECK frozen-shift.no-wrong-occurrence FAIL (abc foo
xyz BAR)`; `CHECK frozen-single.query-cleared FAIL (query='' hits=1)`.
**After:** `frozen-shift.refused PASS`,
`frozen-shift.no-wrong-occurrence PASS` (the user's shifted text is kept, no
`BAR`), `frozen-single.query-cleared PASS (hits=0)`.

**Remains:** size + mtime cannot distinguish an external write that keeps the
same byte length AND lands in the same millisecond; the frozen-query range
check is the second line of defence, and only an identical text at an identical
offset in that window is indistinguishable (same class as the disclosed
`b2-repair` residual, now recorded in `src/workspace/search/README.md` §8).

### Item 5 — regression tests (red before / green after)

All in `src/workspace/search/search_replace_selftest.cpp`; the reviewer's exact
probe inputs are used:

| Test | Reviewer probe input | Before (`logs/01-red-new-checks.log`) | After (`logs/12-green-final.log`) |
|---|---|---|---|
| `testFrozenOptionDuringCountingScan` | F3: whole-word scan of `foo`; toggle cleared while the count runs; the user saves `foo_bar\n` | `frozen-option.no-write FAIL (BAR_bar` | `frozen-option.refused PASS`, `no-write PASS` (still `foo\n`), `controls-disabled-during-scan PASS` |
| `testFrozenEmptyQueryDuringCountingScan` | E3: query cleared while the count runs; the user saves `fix fix\n` | `frozen-empty.no-write FAIL (BAR BAR` | `frozen-empty.refused PASS`, `no-write PASS` (still `foo foo\n`), `query-disabled-during-scan PASS` |
| `testSingleReplaceAfterQueryCleared` | D5: hit at `  assign bar = foo + foo;` column 16; query cleared; `ShiftedBridge` saves `  assign bar = fix + foo;` | `frozen-single.query-cleared FAIL (hits=1)`, `no-write FAIL (  assign bar = BAR + foo;)`, `no-save FAIL (saves=1)` | all PASS: rows invalidated, no save, file byte-identical |
| `testLineShiftRefused` | C4/A3: `foo\nabc foo\nxyz foo\n`, hit line 2 column 5; `ShiftedBridge` saves `abc foo\nxyz foo\n` | `frozen-shift.refused FAIL (ok=1 replacements=1)`, `no-wrong-occurrence FAIL` | `refused PASS`, `no-wrong-occurrence PASS`, `error-names-file PASS` |

The same red run also contains the residual tests for item 6:
`post-save-scope.plan FAIL (…/a.v,…/b.v)` and
`cancel-wording.saved-named FAIL (Replace All cancelled; nothing was written.)`.

Two further checks were added after the first red run, because the re-review's
own F3/E3 probes change the query **inside the confirmation handler** (after
both counting scans), which is a later re-entrancy point than the `hitsReady`
hooks above: `testConfirmWindowChangeRefused` (query cleared / whole-word
cleared in the handler; the user's own save is kept, no `BAR` is written) —
`confirm-window.empty.*` and `confirm-window.option.*` PASS in
`logs/12-green-final.log`. They exercise the same post-confirmation
`sameLiveIdentity()` gate; their pre-fix behaviour is the identical live-query
write shown by the four red tests.

### Item 6 — re-review residuals

- **Matches only in an unsaved buffer (Medium) — CLOSED.**
  `OpenDocumentBridge::bufferText()` (new, default empty) exposes the open
  buffer text; `SearchPanel::bufferOnlyModifiedCandidates()` finds modified open
  documents outside the disk-scan hits whose buffer can contain the query
  (exact ASCII-literal prefilter; regex/non-ASCII save-and-rescan, never a false
  negative), and `finishInitialReplaceScan()` saves them so the post-save
  re-scan counts and replaces the buffer-only match. Test
  `testBufferOnlyMatchIsSavedAndReplaced`: disk `no match on disk\n`, buffer
  `assign foo = 1;\n` → `buffer-only.saved PASS (saves=1)`,
  `buffer-only.counted PASS (planned=1)`, `buffer-only.replaced PASS` (disk
  `assign qux = 1;\n`). This test was added with the residual fix (the main
  red run predates it); the pre-fix path refused with "no match to replace".
- **Post-save re-scan widening CurrentFile (Medium) — CLOSED.**
  `finishInitialReplaceScan()` re-runs `beginScan(operationOptions_, PostSave)`
  with the request frozen at `replaceAllRequested()` instead of calling
  `buildOptions()` again. Test `testPostSaveScopeFrozen`: the active pane loses
  its document during the first scan; the plan is still exactly `a.v`
  (`post-save-scope.plan PASS`), `b.v` is untouched
  (`post-save-scope.no-widen PASS`). Before: the plan widened to `a.v,b.v`.
- **Cancel/timeout wording after saves (Medium) — CLOSED.**
  `confirmAndReplace()` cancel branch and every
  `onSearchFinished()` counting-scan refusal go through
  `savedDocumentsSuffix()`/the cancel wording, which names the already-saved
  documents and says "nothing was replaced". Test
  `testCancelWordingAfterSave`: before `…nothing was written.`; after
  `Replace All cancelled; 1 document(s) were already saved and remain on disk;
  nothing was replaced.`
- **Null document bridge silently skipped (Nit) — CLOSED.**
  `finishInitialReplaceScan()` now refuses with
  "the editor bridge is unavailable, so the unsaved-buffer state cannot be
  checked" instead of treating it as "no affected documents". Not runtime-tested
  in the root build (it always links `vedit_editor_core`, whose default bridge is
  non-null); the refusal branch is compile-verified and mirrors
  `ensureBuffersSaved()`, which was already covered by `unsaved-buffer.refused`.
- **Medium 10 (RE2/PCRE note in `docs/USER_GUIDE.md`) — carried by the captain.**
  `docs/**` is outside this lane's write scope; nothing changed there.

## Verification (observed)

Lane suite (pinned image, offscreen):

```
docker run --rm -v "$PWD":/vedit-build -w /vedit-build \
  -e HOME=/tmp -e XDG_RUNTIME_DIR=/vedit-build/target/qt6-build/xdg-runtime-search \
  -e LANG=C.UTF-8 -e QT_QPA_PLATFORM=offscreen vedit-qt6-build:local \
  bash -lc 'target/qt6-build/src/app/workspace-lane-search/vedit-search-replace-tests --selfcheck'
SUMMARY checks=229 failures=0 skips=0
```

- `logs/01-red-new-checks.log`: `SUMMARY checks=211 failures=15 skips=0` — the
  four required tests fail on the reviewed tree with the reviewer's corruptions
  (`BAR_bar`, `BAR BAR`, `assign bar = BAR + foo`, wrong-occurrence replaced).
- `logs/12-green-final.log`: `SUMMARY checks=229 failures=0 skips=0`.
- `logs/05-original-parity.txt` (regenerated as `logs/11-original-parity-final.txt`):
  every one of the previous 174 check names is present and PASS; 55 new checks.

T23 search selftest (unchanged lane, re-run):

```
WORKSPACE-SEARCH-SELFTEST ok=1 checks=106 failures=0 skipped=2
```

Full gate: `bash scripts/build-qt.sh` — see §Gate below.

## Gate

`bash scripts/build-qt.sh` on the final tree: **exit 0**
(`logs/13-build-qt-gate-final.log`), pinned image `vedit-qt6-build:local`:

- Rust service layer: cargo build + `cargo test -p vd-ffi -p vd-search -p
  vd-settings -p vd-vcs` + GTK-isolation tree check + generated-header check, all
  green;
- CMake configure + build (warnings-as-errors), then
  `100% tests passed, 0 tests failed out of 49` — the B2 lane is ctest #32
  (`vedit-search-replace-tests`, Passed) and the T23 lane is #31
  (`vedit-workspace-search-selftest`, Passed); the previous batch's gate was
  47/47, so the ctest count did not decrease;
- xcb/software-rendering window smoke:
  `M0-SMOKE ok=1 platform=xcb qt=6.10.2 … window=1 editor=1 document=1`.

An earlier gate run (`logs/10-build-qt-gate.log`) was also exit 0 with 49/49
ctest, but it predates the two extra confirmation-window tests; the acceptance
run is `logs/13-build-qt-gate-final.log`.

## Bounds / what is not proven

- The panel tests inject the query/option change in a `GlobalSearch::hitsReady`
  handler (exactly the code point the re-review's probes use), not via a real
  mouse/keyboard; the widgets are disabled in the product, so a real user cannot
  produce the change through them, and the programmatic path is the backstop.
- The 15 s real-project timeout path is exercised by the existing 50 ms-budget
  proxy, not a real 15 s project.
- The null-bridge refusal is not reachable in the root build (editor core is
  always linked); no runtime test.
- The buffer-only closure's regex/non-ASCII prefilter conservatively saves all
  modified documents in scope before the re-scan (correctness over a minimal
  save set); the ASCII-literal path uses the exact `QString::contains` prefilter.
- Document-change detection is size + millisecond mtime (see item 4 "Remains").
