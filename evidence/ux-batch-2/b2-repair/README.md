# B2 repair — review findings closed (ux-batch-2 lane B2)

- Contract: `.mpd/plans/ux-batch-2/00-plan.md` §`B2`; review `evidence/ux-batch-2/review-b2/REPORT.md`
- Repair date: 2026-11-05
- Write scope actually touched: `src/workspace/search/**`,
  `src/editor/kernel/find_replace.cpp` (one call: the bar publishes through the
  new `publishFromFindBar` seam). **No `crates/**` change** — the BOM fix is
  entirely on the C++ decode side (`ConvertInitialBom`), so the conditional
  permission to touch `crates/vd-search/**` was not needed.
- Gate: `bash scripts/build-qt.sh` **exit 0** (see `logs/05-build-qt-gate.log`):
  Rust layer + configure + build, **ctest 47/47 passed, 0 failed**
  (`vedit-search-replace-tests` at #32; the review's 44/44 baseline is not
  decreased), xcb/software-rendering smoke `M0-SMOKE ok=1`; the lane's
  **174/174** checks (81 original + 93 new) and the T23 search selftest
  (106 checks, 0 failures, 2 skips) are green.
- Method: every regression test was written first and run against the reviewed
  tree (`logs/01-red-new-checks.log`: `SUMMARY checks=162 failures=28`), then the
  fix was applied and the same tests were re-run green
  (`logs/03-b2-green.log`: `SUMMARY checks=174 failures=0 skips=0`). The red run was
  taken before the last 12 checks (regex/whole-word replace coverage, below) were
  added; those are extra coverage for the same validator, not finding regressions.

## Per finding: change, observed before, observed after

### BLOCKER 1 — reload no longer discards edits typed after the save-first step

**Changed**

- `src/workspace/search/document_sync.h`, `OpenDocumentBridge`: new
  `contentToken()` and `refresh(path, expectedToken, error)` returning
  `RefreshStatus { Reloaded, NotOpen, Changed }`. The guard lives inside
  `refresh()`, so no future caller can bypass it.
- `src/workspace/search/document_sync.cpp`, `EditorOpenDocumentBridge::refresh`:
  refuses to reload when the document `isModified()` **or** its current
  SHA-256 content token differs from the one captured at save time.
- `src/workspace/search/search_panel.cpp`:
  `SearchPanel::captureReloadTokens()` (called after the save-first step and
  after a single replace's save), `SearchPanel::onReplaceFinished()` reloads
  only through the guarded call and fills `ReplaceOutcome::notReloadedFiles`,
  and the status line says `N file(s) changed during replace — not reloaded: …`.
- `src/workspace/search/search_types.h`: `ReplaceOutcome::notReloadedFiles`.

**Observed before** (`01-red-new-checks.log`):
`edit-during-replace.edit-survives FAIL (modified=0 text='qux qux')` and
`edit-during-replace.reported FAIL (Replaced 2 occurrence(s) in 1 file(s).)` —
the buffer was reloaded and the user's line was destroyed.
**Observed after** (`03-b2-green.log`): the same checks PASS; the buffer keeps
`// typed during replace`, stays modified, and the status names it.

**Test**: `testEditDuringReplace()` — a real KTextEditor document, real bridge,
the edit is made in the replace-confirmation callback (the same “after the
save-first step, before the reload” window).
**Not proven**: a real user typing with a mouse/keyboard during a 15 s scan;
the test injects the edit at the same code point, not via real input events.
It also does not exercise a buffer opened for the first time *during* the batch
(that path relies on the unmodified check, which is covered by the same
`refresh()` guard).

### HIGH 2 — stale ranges are rejected, not written

**Changed**

- `src/workspace/search/project_replace.h`: `ReplaceQuery` (the matcher's own
  pattern + case/whole-word/regex) and a 4-argument `applyReplaceRanges(...,
  const ReplaceQuery &)`.
- `src/workspace/search/project_replace.cpp`: `RangeMatcher` re-checks the text
  at every reported range against the SAME query before any write (literal is
  `QRegularExpression::escape`d; whole-word is wrapped `\b(?:…)\b` exactly like
  `crates/vd-search/src/lib.rs::compile_matcher`; case sensitivity and Unicode
  word boundaries are mirrored). A mismatch refuses the call with the file and
  line; every range error now names the file.
- `ProjectReplacer::start/run` carry the query to the worker thread.
- `src/workspace/search/search_panel.cpp`: `replaceQuery()` is passed for both
  「替换」 and 「全部替换」, so the ranges are re-validated on the worker thread
  against the freshly read file.

**Observed before**: `stale-one.refused FAIL (ok=1 replaced=1)` and the file
became `  assign bar = BAR + foo;` — the old range overwrote `fix` because only
the length was checked. Batch: `stale-batch.refused FAIL (ok=1)`,
`stale-batch.stale-file-untouched FAIL (qux qux)` — the externally changed
`b.v` was overwritten. **Observed after**: both refuse; `a.v` (written before
the failure) is reported with its counts, `b.v` is untouched, and the error
names `b.v` line 1.

**Test**: `testStaleRangeOne()` (edit between the browse scan and 「替换」) and
`testStaleRangeBatch()` (edit between the counting scan and the write, injected
in the confirmation callback).
**Not proven**: if an edit moves content so that the *exact same* matched text
lands on the *exact same* offset in the same file, text-based validation cannot
distinguish it (no file hash/version is carried by the ABI). Also, the
validation engine is QRegularExpression (PCRE); an exotic RE2-only pattern that
PCRE cannot compile is refused (“cannot be re-checked”) rather than written —
fail-closed, documented in `src/workspace/search/README.md` limitation 4.

### HIGH 3 — a timed-out counting scan refuses Replace All

**Changed**: `src/workspace/search/search_panel.cpp`,
`SearchPanel::onSearchFinished()` replace branch now refuses on
`summary.timedOut` exactly like `truncated`, with
`“Replace All refused: the counting scan stopped at the %1 ms time budget …”`.
**Observed before**: `replace-timeout.refused FAIL (ok=1 confirmed=1)`,
`replace-timeout.no-write FAIL` — a partial prefix was confirmed and replaced.
**Observed after**: refused before the confirmation, nothing written.

**Test**: `testReplaceAllTimeout()` — a 2-hit file scanned first plus a 1.5M
line file with a 50 ms budget, so the reviewed tree sees a partial, non-empty
hit set; it proves the partial set is not written.

### HIGH 4 — UTF-8 BOM handled consistently

**Changed**: `src/workspace/search/project_replace.cpp`,
`ProjectReplacer::run()` decodes with
`QStringConverter::Flag::Stateless | Flag::ConvertInitialBom`. The Qt
`ConvertInitialBom` probe (this image) shows it keeps U+FEFF
(`size=11 first=U+FEFF`, round-trip `efbbbf…`), matching Rust's
`String::from_utf8_lossy`; the BOM is written back by `toUtf8()`.
**Observed before**: `bom.start-bytes FAIL (66424152613b0a42415220623b0a)`
= `fBARa;\nBAR b;` (the reviewer's `mMODa;` class of corruption) and
`bom.mid-bytes FAIL (6d6f64756c652066424152202f2f20e4b8ad0a)`
= `module fBAR; // 中`; the BOM was also dropped from the rewritten file.
**Observed after**: `EF BB BF` preserved, `BAR a;`, `module BAR; // 中`, and the
astral fixture (`😀 foo` → `😀 BAR`) stays correct.

**Test**: `testUnicodeBom()` (dedicated BOM/astral fixture in the acceptance
suite). No Rust change was needed.

### HIGH 5 — CurrentFile follows the ACTIVE editor document

**Changed**: `src/workspace/search/search_panel.h/.cpp` gain
`setCurrentFileProvider()` and `resolveCurrentFile()`; `buildOptions()`,
`replaceAllRequested()` and the buffer-scope logic resolve the current file
through it at search/replace time. `src/workspace/search/search_integration.cpp`
installs a provider on the factory-created panel:
`shell.editorArea()->activePane()->currentFilePath()`. A panel with no provider
(standalone lane tests) keeps the old `SearchHub::currentFilePath()` fallback.
The result click still updates the hub, but it no longer decides the scope.
**Observed before**: `current-file.b.hits FAIL (hits=0 files=)` — the scan was
filtered to a *stale* path left by an earlier result click (`rows.v`), so a
CurrentFile search found nothing. **Observed after**: each pane's own file is
searched after a pane switch (1 hit each).

**Test**: `testCurrentFileScopeFollowsActivePane()` — a real
`ApplicationShell` with `makeEditCoreProvider()`, two files opened in two panes,
`setActivePane()` switched, and the dock created through
`installSearch()`/`PanelRegistry::showPanel()`.
**Not proven**: real clicking on the tab bar; the test calls the same
`setActivePane()` the shell uses. A shell whose active pane has no file
(untitled) yields “No current file” by design.

### Medium 6 — mid-batch failure reports counts and names the file

**Changed**: `search_panel.cpp::onReplaceFinished()` prints
`“Replace failed after writing %1 occurrence(s) in %2 file(s): %3”` when
`filesChanged > 0`; `project_replace.cpp::applyReplaceRanges()` names the file
in the overlapping / too-short / runs-past errors; `ensureBuffersSaved()` names
the failing buffer.
**Observed before**: status `Replace failed: cannot write .../b.v: simulated
write failure` (no counts) and `replace: line 1 is shorter than the search
result` (no file). **Observed after**: `…after writing 2 occurrence(s) in 1
file(s): cannot write .../b.v…` and the short-line error names `direct.v`.
**Test**: `testMidBatchFailure()` (injected writer fails on the second file).

### Medium 7 — 「替换」 acts on the selected row

**Changed**: `search_panel.cpp::replaceCurrentHit()` reads the tree's
`currentItem()` (and the panel tracks `currentItemChanged`, not only clicks).
**Observed before**: `selected-row.third-line FAIL (BAR …)` — with no click,
`currentHitIndex_` was -1 and the first hit was replaced. **Observed after**:
the third (selected) line is replaced.
**Test**: `testSelectedRowReplace()` (tree selection, no click).
`testRegexWholeWordReplace()` additionally drives the High 2 validator through
its regex and whole-word branches (a valid regex/whole-word replacement is not
falsely refused; the wrong occurrences are not replaced).
**Not proven**: multi-column sorting / a user collapsing the tree (the current
item is still the authority).

### Medium 8 — opening the dock does not wipe a live Ctrl+F bar

**Changed**: `search_panel.cpp::connectSignals()` no longer publishes the
panel's defaults; the constructor seeds from the **find bar's** snapshot only
(`SearchHub::publishFromFindBar/hasFindBarSnapshot/findBarSnapshot`,
`search_query.h`; the bar publishes through it). Other docks' last searches are
not replayed, so a freshly opened dock never inherits or pushes a query.
**Observed before**: `dock-open.bar-query-preserved FAIL (bar='' dock='')`,
`dock-open.bar-matches-preserved FAIL (matches=0)` — the bar lost its query and
scope. **Observed after**: the bar keeps query/options/matches and the dock
adopts them.
**Test**: `testDockOpenKeepsBar()` (real bar + real document; the dock is
constructed, which is the old wipe path).

### Medium 9 — only affected modified documents are saved; wording matches reality

**Changed**: `replaceAllRequested()` no longer saves before the first scan. The
counting scan runs first (`ReplaceScanPhase::Initial`),
`finishInitialReplaceScan()` intersects its hit files with
`OpenDocumentBridge::modifiedPaths()` and saves **only** those, then re-scans
(`PostSave`) so the confirmation counts reflect the saved content. A later save
failure refuses with “The N document(s) already saved remain on disk; nothing
was replaced.” (or “Nothing was replaced.” when nothing was saved). The
confirmation names the saved documents.
`src/workspace/search/README.md` flow/rows updated to match.
**Observed before**: `save-only.only-affected-saved FAIL (a.v,b.v)` — the
unrelated modified `b.v` was saved; the refusal wording did not mention the
persisted saves. **Observed after**: only `a.v` is saved; the wording names the
failing file and the persisted saves.
**Test**: `testSaveOnlyAffectedDocuments()` and `testSaveFailureWording()` with
a recording bridge.
**Note on the existing check**: `unsaved-buffer.refused` is now asynchronous
(the save happens after the first scan), so the test waits for idle before
asserting `lastReplaceOutcome().ok == false`; the check's safety assertion is
unchanged (refused, error says refused, no write) — it is not weakened.
**Not proven**: a save failure on the *second* of two affected files leaves the
first persisted by design (now stated accurately); the test asserts the message
and that no replacement is written.

### Medium 10 — RE2 vs QRegularExpression note (captain carry)

This lane is not allowed to edit `docs/USER_GUIDE.md`; the text is carried here
for the captain to merge:

> 工程级搜索/替换使用 Rust `regex` crate（RE2 风格）：支持字符类、量词、分组、`\b`、
> 内联标志 `(?i)` 等，但**不支持反向引用（`\1`）和 lookaround（`(?=…)`、`(?<=…)`）**。
> Ctrl+F 的单文件搜索使用 `QRegularExpression`（PCRE 风格），上述两种写法可用。
> 两个界面的选项语义一致（大小写/整词/正则/当前文件|全工程），但正则方言不同；
> 把 Ctrl+F 里的反向引用/lookaround 模式拿到搜索面板会报“无效正则”。
> 搜索面板在替换前会用同一查询重新校验匹配器报告的区间，校验器也是
> `QRegularExpression`（RE2 模式在实践上是 PCRE 的子集），它只校验那一段、
> 不产生搜索结果。

## Evidence map

| File | Content |
|---|---|
| `../b2/logs/02-ctest-new.log` | reviewed tree, original acceptance: `SUMMARY checks=81 failures=0` (the review reproduced it too) |
| `logs/01-red-new-checks.log` | reviewed tree + new regression tests: 28 failures (listed above) |
| `logs/02-green-first-pass.log` | first green pass before the T23 re-check |
| `logs/03-b2-green.log` | final `vedit-search-replace-tests --selfcheck`: 174/174, 0 failures, 0 skips |
| `logs/04-t23-green.log` | pre-existing T23 lane `vedit-workspace-search-selftest`: 106 checks, 0 failures, 2 skips |
| `logs/05-build-qt-gate.log` | `bash scripts/build-qt.sh` full output (Rust layer + configure + build + ctest + xcb smoke) |
| `logs/06-original-81-parity.txt` | name-by-name proof that all 81 original checks are present and PASS in the 174-check green run |
| `acceptance/` | fixture before/after trees + `before-after.diff` written by `testReplaceAll` |
| `bom-probe.log` | Qt `QStringDecoder` probe (Stateless vs ConvertInitialBom) |

## Acceptance fixture (CRLF / CJK / astral)

`acceptance/after/crlf.v` is `71 75 78 0d 0a 71 75 78 0d 0a` (`qux\r\nqux\r\n`):
CRLF is preserved. `acceptance/after/unicode.v` is `// 中文 qux 注释` (CJK
columns unaffected). Astral columns are covered by `bom.astral-bytes`
(`😀 foo` → `😀 BAR`). The full before/after diff is
`acceptance/before-after.diff`; only the query occurrences changed.

## Residual uncertainty

- The regression tests run offscreen in the pinned image; the full gate's xcb
  smoke covers the window path, not an interactive user session.
- Real-time user typing during a scan is not simulated; the injected edit uses
  the same save/reload window and the same guard code.
- The exact-file-identity hole for a coincidentally identical match at the same
  offset remains (text validation per the contract direction, no hash in the ABI).
- Exotic RE2-only patterns that PCRE cannot compile are refused rather than
  written (fail-closed). No such pattern was found in the fixture suite.
