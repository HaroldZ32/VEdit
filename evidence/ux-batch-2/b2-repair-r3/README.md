# Repair — UX batch 2, lane B2, round 3 (`b2-repair-r3`)

- Frozen contract: `.mpd/plans/ux-batch-2/00-plan.md` §`B2` (acceptance ①–⑥)
- Failing review: `evidence/ux-batch-2/review-b2-r3/REPORT.md` (verdict FAIL: the scan-window
  stamp baseline, the Ctrl+F hand-off during a counting scan, the dirty-buffer save-first refusal)
- Write scope used: `src/workspace/search/**` only (`src/editor/kernel/find_replace.*` was **not**
  touched; `CMakeLists.txt` was **not** touched — no source file was added or removed)
- Repaired: 2026-11-05
- Verdict claimed: all four items repaired; the residuals are listed in §6, including the one the
  gate sentence can still answer "yes" to (an identical-text duplicate inserted above a hit).

## 1. What changed (path + symbol)

| Path | Symbol | Change |
|---|---|---|
| `src/workspace/search/search_types.h` | `SearchHit::lineFingerprint`, `ReplaceHit::lineFingerprint` | per-hit CONTENT evidence: SHA-256 of the matched line exactly as the Rust matcher reported it (empty = "no evidence supplied", e.g. hand-built test ranges) |
| `src/workspace/search/search_types.h/.cpp` | `matchedLineFingerprint()` | the one normalizer: strips one leading U+FEFF (the BOM; `QString::fromUtf8` drops it on the scan side while the replace path keeps it via `ConvertInitialBom`), trims, then caps at `TREE_SNIPPET_CHARS`=400 + `…` exactly like `crates/vd-search::snippet_of` |
| `src/workspace/search/global_search.cpp` | `GlobalSearch::runScan` | computes `hit.lineFingerprint` from `item.preview` (the scanner's own view of the line) for every hit |
| `src/workspace/search/project_replace.h` | `ReplaceFileStamp::writtenDuringScan` | the SCAN-WINDOW half of the file identity; documented as cleared only by the panel's own save-first write |
| `src/workspace/search/project_replace.cpp` | `applyReplaceRanges()` | before rebuilding a line, recomputes the fingerprint of the CURRENT line and refuses the file when it differs (`line %2 no longer holds the text the search read`) |
| `src/workspace/search/project_replace.cpp` | `ProjectReplacer::run()` | refuses a file whose frozen stamp says it was written during the scan (last line of defence) |
| `src/workspace/search/search_panel.h` | `FrozenScan::scanStartedMs`, `captureFileStamp(file, scanStartedMs)`, `frozenScanWindowFile()`, `operationBusy()`, `replaceCurrentHit()` return contract | the scan START is part of the frozen identity; `replaceCurrentHit()` returns false only when the request cannot be attempted (document-identity refusals are reported through `replaceFinished`) |
| `src/workspace/search/search_panel.cpp` | `beginScan()` | captures `activeScanStartedMs_` **before** `GlobalSearch::start`, and refuses a browse scan (`phase == None`) while a replacement owns the panel |
| `src/workspace/search/search_panel.cpp` | `captureFileStamp()` | sets `writtenDuringScan = scanStartedMs > 0 && mtime > scanStartedMs && mtime <= now` |
| `src/workspace/search/search_panel.cpp` | `makeFrozenScan()` | freezes `scanStartedMs` + the per-file flags together with the hits |
| `src/workspace/search/search_panel.cpp` | `onSearchFinished()` (counting phase) | refuses the whole Replace All when any hit file was written during the counting scan — **before** the save-first step and the confirmation |
| `src/workspace/search/search_panel.cpp` | `replaceCurrentHit()` | (a) refuses a hit file flagged `writtenDuringScan`; (b) for a modified open buffer, checks the disk stamp and the BUFFER's hit-line fingerprint **before saving**; (c) after the panel's own save, re-captures that file's stamp (and clears the flag) so the write is validated against the saved content |
| `src/workspace/search/search_panel.cpp` | `applyHandoff()`, `startSearch()`, the hub subscription | refuse / ignore while `operationBusy()` (counting scan, save-first, confirmation, write) and show a status line; the running operation keeps its frozen request |
| `src/workspace/search/search_panel.cpp` | `replaceAllRequested()`, `replaceCurrentHit()` entry guards | `operationBusy()` instead of `searching_ || replacing_ || replacer_.isRunning()`, so a second operation cannot take over the state machine in the gap between the two counting scans |
| `src/workspace/search/search_replace_selftest.cpp` | `testScanWindowSingleReplace`, `testScanWindowReplaceAll`, `testHandoffDuringOperation`, `testDirtyBufferSingleReplace`, `testDirtyBufferStaleEditNotSaved`, `testLineFingerprintGuard`, `SavingBridge::bufferText` | the red-before/green-after regressions |
| `src/workspace/search/README.md` | invariants + §8 residuals | the invariant statement now describes what the code guarantees (and no longer overclaims) |

## 2. Commands and observed results

All runs are in the pinned Qt6 image (`vedit-qt6-build:local`), offscreen
(`QT_QPA_PLATFORM=offscreen`).

```
# build the lane's test target
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 \
  vedit-qt6-build:local bash -c 'ninja -C target/qt6-build vedit-search-replace-tests'

# the lane suite (raw log: logs/01-red-suite.log, logs/02-green-suite.log)
docker run ... bash -c 'target/qt6-build/src/app/workspace-lane-search/vedit-search-replace-tests \
  --selfcheck --evidence target/qt6-build/src/app/workspace-lane-search/search-replace-evidence'

# the reviewer's own probe, rebuilt unchanged against this tree (logs/04-reviewer-probes.log)
docker run ... bash -c 'bash evidence/ux-batch-2/b2-repair-r3/probes/build-probe.sh &&
                        ./evidence/ux-batch-2/b2-repair-r3/probes/b2-r3-repair-probe'

# the repository gate
bash scripts/build-qt.sh          # logs/03-build-qt.log
```

| Run | Exit | Observed |
|---|---|---|
| suite, BEFORE the fix (`logs/01-red-suite.log`) | 1 | `SUMMARY checks=271 failures=16 skips=0` — the 229 pre-existing checks pass, the 16 new scan-window / hand-off / dirty-buffer checks fail |
| suite, AFTER the fix (`logs/02-green-suite.log`) | 0 | `SUMMARY checks=275 failures=0 skips=0` (229 previous + 42 behavioural + 4 fingerprint-unit checks); three consecutive runs identical |
| 229-name parity | — | every check name in the reviewer's `review-b2-r3/probes/01-229-suite.log` is present and `PASS` in `logs/02-green-suite.log`; every name from the red log is present in the green log (no assertion removed or renamed) |
| reviewer probe, AFTER the fix (`logs/04-reviewer-probes.log`) | 0 | `PROBE SUMMARY failures=0` (63 `PROBE` lines, 0 `FAIL`; P1–P4 and N1–N5 all pass) |
| `bash scripts/build-qt.sh` (`logs/03-build-qt.log`) | 0 | `100% tests passed, 0 tests failed out of 50` + `M0-SMOKE ok=1 platform=xcb …` + `[qt6] PASS` |

## 3. Item 1 — the scan window

**Before** (reviewer's `02-probe-output.log`, and this repair's `logs/01-red-suite.log`):

```
PROBE N1.after='abc foo\nxyz foo\n'     (the user's save during the scan)
PROBE N1.no-wrong-occurrence FAIL (abc foo   → the file became "abc foo\nxyz BAR\n"
PROBE N1b.after='BAR c\nBAR a\nfoo b\n'
PROBE N1b.no-wrong-occurrence FAIL (BAR c    → the new "foo c" replaced, the scanned "foo b" left
CHECK scan-window.single.refused FAIL (ok=1 replacements=1 error=)
CHECK scan-window.all.refused FAIL (ok=1 replacements=2 error=)
```

**After** (`logs/04-reviewer-probes.log`):

```
PROBE N1.after='abc foo\nxyz foo\n'          ← byte-identical to the user's save
PROBE N1.no-wrong-occurrence PASS
PROBE N1b.after='foo c\nfoo a\nfoo b\n'      ← byte-identical to the user's save
PROBE N1b.no-wrong-occurrence PASS
```

Mechanism: the scan START is frozen (`FrozenScan::scanStartedMs`) and every hit file whose mtime is
strictly later is flagged (`writtenDuringScan`); the counted operation refuses at
`onSearchFinished` (nothing saved, no dialog), a single 「替换」 refuses that file. Independently,
each hit carries a SHA-256 of the line the matcher read, and the writer recomputes it from the
current file: a hit line that changed under the scan can never be applied, whatever the ms timing.

## 4. Item 2 — the Ctrl+F hand-off during an operation

**Before** (reviewer's N2, quoted): `queryEnabled=0 replaceAllEnabled=0 replaceEditEnabled=0
outcomeOk=0 error=''` — idle but dead dock.

**After** (`logs/04-reviewer-probes.log`):

```
PROBE N2.handed=1 idle=1 queryEnabled=1 replaceAllEnabled=1 replaceEditEnabled=1 \
      outcomeOk=0 error='Replace All cancelled; nothing was written.'
```

`applyHandoff()`, `startSearch()` and `beginScan()` refuse while `operationBusy()` and write the
reason into the replace status line; the hub subscription ignores external snapshots for the same
window. The running operation therefore reaches its own terminal state and clears
`operationActive_`; the new checks (`handoff-op.apply.*`, `handoff-op.search.*`) assert controls are
restored in both routes.

## 5. Item 3 — the dirty-buffer single 「替换」

**Before** (`logs/01-red-suite.log`):

```
CHECK dirty-single.ok FAIL (ok=0 error=replace: dirty.v: line 1: the document changed after the scan \
                            (size or modification time); nothing was written to that file ...)
CHECK dirty-single.replaced-on-saved-text FAIL (foo\nabc foo edited\n)
CHECK dirty-stale.no-save FAIL (saves=1)          ← the buffer was written before the refusal
CHECK dirty-stale.disk-unchanged FAIL
```

**After** (`logs/02-green-suite.log`, `logs/04-reviewer-probes.log`): the disk stamp and the
buffer's hit-line fingerprint are checked **before** the save-first write; when they pass, the save
runs and the file's stamp is re-captured, so the worker validates against the saved content:

```
PROBE N3.after='BAR\nabc foo edited\n'   PROBE N3.save-first-ran PASS
CHECK dirty-single.ok PASS   CHECK dirty-single.replaced-on-saved-text PASS (BAR\nabc foo edited\n)
CHECK dirty-stale.no-save PASS (saves=0)   CHECK dirty-stale.disk-unchanged PASS
```

## 6. Residual bounds (stated honestly)

1. **Same-size, same-mtime external rewrite.** If an external write preserves both the size and the
   mtime, the per-file stamp cannot see it; the per-hit line fingerprint still refuses when the hit
   line's text changed. Only an external rewrite that leaves every hit line byte-identical at the
   same line number is indistinguishable — and then the replacement touches the same text at the
   same coordinates.
2. **Duplicate line inserted above a hit.** The fingerprint is per line (the matcher reports the
   line's preview). If a line identical to the hit line is inserted above it, the hit's line number
   still holds that text and the fingerprint matches; the replacement lands on the same coordinates
   in the same text (the copy). Detecting that needs a whole-file identity the ABI does not carry;
   the scan-window stamp catches it whenever the insert happens during the scan (mtime > start),
   which is the case the probes cover.
3. **Same-millisecond write at scan start.** `writtenDuringScan` uses `mtime > scanStartedMs`
   (ms precision) and requires `mtime <= now`. A write that lands in the same millisecond as the
   scan start is not flagged by the window layer; the per-hit fingerprint is the backstop. A future
   mtime is not flagged (it would otherwise flag the file forever).
4. **Bridge without `bufferText()`.** The pre-save buffer check needs the buffer text. The product
   bridge (KTextEditor) always provides it; a test/alternate bridge returning an empty string falls
   back to "save, then let the writer's line fingerprint refuse" — no wrong characters, but the
   buffer is written first. The lane's own `SavingBridge` now exposes the text like the real bridge,
   so `testLineShiftRefused` takes the pre-save refusal path; that path's "nothing was saved" half
   is asserted explicitly by `dirty-stale.no-save` (`bridge.saves == 0`).
5. **Ctrl+F bar wording.** When the hand-off is refused, the dock shows the honest reason, but the
   bar's own transient text still reads "Whole project: searching in the Search panel"
   (`src/editor/kernel/find_replace.cpp`, outside this lane's write scope).
6. **Not re-run here:** a real xcb/xdotool GUI session (the programmatic injection at `hitsReady`
   is the reviewer's own method), the default modal `QMessageBox` path (tests inject handlers), and
   the null-bridge branch of `finishInitialReplaceScan` (not reachable in the root build).

## 7. Evidence files

- `logs/01-red-suite.log` — suite BEFORE the fix: 271 checks, 16 failures (all new checks).
- `logs/02-green-suite.log` — suite AFTER the fix: 275 checks, 0 failures, 0 skips.
- `logs/03-build-qt.log` — `bash scripts/build-qt.sh`: Rust layer + Qt6 build + ctest 50/50 + xcb smoke.
- `logs/04-reviewer-probes.log` — the reviewer's probe rebuilt unchanged against this tree:
  `PROBE SUMMARY failures=0`.
- `probes/probe.cpp`, `probes/build-probe.sh` — byte-copies of the reviewer's probe with only the
  output path changed (the reviewer's own artefacts are untouched).
