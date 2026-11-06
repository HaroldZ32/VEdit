# Final acceptance — round 2 (UX batch 2, final tree)

- Task: **T37** — round 2 of the whole-batch acceptance; round-1 record:
  `evidence/ux-batch-2/review-batch-acceptance/REPORT.md` (FAIL, 4 High); checklist:
  `evidence/ux-batch-2/review-batch-acceptance-r2/CHECKLIST.md` (run against the final tree)
- Frozen inputs read: `evidence/ux-batch-2/u8-final/` (the captain's one full gate run + the
  repackaged-artefact verification), `packaging/runtime/out/` (AppImage + AppDir, 2026-11-06 12:42),
  `evidence/final-acceptance-3/AC-MATRIX.md` (draft, 12:43)
- Review date: 2026-11-06, snapshot ~12:45–12:55
- Reviewer: independent; **read-only** — no product file, test, gate, the matrix or round-1 evidence was
  modified. The only files this row adds are this report (and it re-used the pre-built checklist).
- **Verdict: RELEASE** — all 14 user requirements + the SDC/XDC addendum + the pane-activation fix are
  **PASS** on the final tree with an independent review and a final-tree gate log behind each; all four
  round-1 Highs are closed; no Blocker/High remains. The release is gated only by four Medium
  delivery/currency items (below), none of which is a product defect.

## 1. Requirement matrix (final tree)

| # | User requirement (handover §1) | Verdict | Evidence pointer (opened by me) |
|---|---|---|---|
| 1 | Module tree: top level = never-instantiated modules only, module info only, double-click opens the module code | **PASS** | `review-b1-r2/REPORT.md` PASS; `src/workspace/module_tree/**` unchanged since that verdict (no file newer than the report) |
| 2 | Interface panel: follows the focused module; type/direction/width; double-click → declaration, Ctrl+double-click → instantiation | **PASS** | same `review-b1-r2` (its acceptance table covers both jumps) |
| 3 | Right-click menu + top bar fully translated | **PASS** | `review-a2-final` PASS + `review-u2-final` PASS + `review-u2b-final` PASS; my own `.ts` spot-check: `&File`→文件, `&Edit`→编辑, `&View`→视图, `&Project`→项目, `&Help`→帮助, `&Undo`→撤销, `&Redo`→重做, `&Preferences…`→首选项, all five `To&ggle … Panel` toggles translated; final `lrelease` = 1211 finished / 0 unfinished (`u8-final/lrelease.log`) |
| 4 | Rename works where supported, greyed with a reason where not, and the **request path** refuses too | **PASS** | `review-u1-final` (rename E2E `checks=21 failures=0 skips=0 not-claimed=1`, `10-direct-runs.log`); the request-path gating was independently re-verified in `review-b3-r3`'s "Not weakened" section; cross-file rename NOT-CLAIMED (§6.2) |
| 5 | Search aligned with Ctrl+F + project-wide search + **two** replace buttons | **PASS** | `review-b2-r5/REPORT.md` PASS (P5 closed: refusal before save, 0 saves, byte-identical disk, positive control replaces the scanned line); suite 299/0/0; 229/229 + 275/275 baselines; final `ctest` re-ran the lane; `src/workspace/search/**` unchanged since the r5 run (mtimes 10:49) |
| 6 | format / LINT options are real settings; unsupported knobs greyed with a reason | **PASS** | `review-a4-final` PASS (23 format knobs; a real process receives them) + `review-u6-final` PASS (five lint knobs reach the batch argv with per-knob proof; defaults neutral; two configs independent; rules precedence page → legacy → discovered → none; autofix honest `TransportPending` with the panel stating why) |
| 7 | Top bar re-ordered like other editors (undo/redo in the toolbar, Preferences under Edit) | **PASS** | `review-a2-final` PASS (menu order / toolbar / Preferences under Edit); the A2-carried shortcut High (`Ctrl+Alt+F` shared) is **closed** by `review-u3-final-r2` PASS (registry now has `diagnostics.format=Ctrl+Shift+F`, `view.panel.file-tree=Ctrl+Alt+F`, duplicate detector, no two bindings share a sequence) |
| 8 | Optional UI font size (immediate) + overall scale (restart) | **PASS** | `review-a4-final` PASS (150→1.5, 200→2, 100 no override; scale is read before `QApplication`); the shipped AppRun logs show `UI-SCALE scale_percent=100 … font_point_size=9` |
| 9 | error/warning hover popup; one-click fix only when the engine can fix, undoable in one step | **PASS** | `review-u1-final` PASS (R2/R3 re-run; hover/fix behaviour intact; `review-u2b-final` PASS for the `Fix`→修复 source string) |
| 10 | Yellow/red/green problem dots before file names | **PASS** | `review-u1-final` PASS (`n2r.e-no-longer-green` status follows invalidation; the lint-experience R2 block's dot checks all green, 98/0) |
| 11 | Help contains a real user manual | **PASS** | `review-a2-final` PASS (qrc-embedded `USER_GUIDE.md`, F1, terminal pass-through) + `review-u3-final-r2` PASS (manual content + registry truth; 23 bindings); the manual now carries §12–§16 including the corrected 400-character replace bound (verified in the current file, mtime 12:37) |
| 12 | ELAB errors guide with jump; modal only on explicit lint | **PASS** | `review-u1-final` PASS (`modal.automatic-never-shows`, `modal.dialog-not-visible-after-auto`, product `product.automatic-no-dialog`) |
| 13 | Ctrl+click = go to definition; multi-cursor on Alt+click | **PASS** | `review-b4-r2` PASS (coordinate space, same F12 dispatch, collapse behaviour); final ctest re-ran the B4 lane |
| 14 | VHDL / Markdown / Tcl highlighting | **PASS** | `review-a3-r2` PASS (5 definitions, table priority, licence) + `review-u4-final`/`review-u4b-final` PASS (startup test asserts the **resolved definition names**, not just file names) + `review-u5-final` PASS (packaging selfcheck covers all five, fail-closed); shipped AppRun: `definitions-ok=[Markdown,SystemVerilog,Tcl,VHDL,Verilog]` |
| + | SDC/XDC → Tcl highlighting | **PASS** | `review-a3-r2` (extension table `.sdc/.xdc → Tcl`; AppDir five-definition check) |
| + | Pane activation (review-found bug) | **PASS** | `review-b4-r2` PASS (its independent probe covers the app-level filter) + `pane-activation/` |
| (a) | Cross-cutting: project lint UPPER, module lint LOWER everywhere (editor/gutter/panel/hover/dots/counts), no double counting | **PASS** | `review-u1-final` §3.4: one verdict per file, one count (`composeSessionMessage` uses `mergedDiagnostics()`), the panel uses the merged set, the hover uses `mergedDiagnosticsForFile()`, the dot derives from the same sets; R2 block 98/0 |
| (b) | Cross-cutting: auto project lint after save is silent, debounced, single-flight, never blocks the save | **PASS** | `review-u1-final` §3.5: `ProjectLintNotice` only status-bar for Automatic; 700 ms single-shot debounce; `fire()` cancels a running pass (no queue); the save seam only starts the timer; `modal.automatic-never-shows` / `product.automatic-no-dialog` / `c8.auto-pass-after-save*` green |

## 2. The four round-1 Highs — all closed

1. **Item 5 (the P5 wrong-target write) — closed.** `review-b2-r5`: the dirty-buffer single replace now
   proves the whole line before saving (`readDocumentLine` + `normalizedLineIdentity`) and refuses with a
   visible reason, 0 saves, disk byte-identical; my byte-identical P5 probe reports
   `saves=0 ok=0 replacements=0`, and the positive control still replaces the scanned line. The final tree
   is the reviewed tree (`src/workspace/search/**` unchanged since 10:49; my probes ran against it at
   11:04–11:07), and the final `ctest` re-ran the 299-check lane suite.
2. **Items 9/10 (stale upper verdict / green dot) — closed.** `review-u1-final` PASS: N2 (`N2-r`) and N3
   are closed with regressions that fail on the pre-fix source; the end-to-end point
   `n2r.e-no-longer-green` shows the dot follows the invalidation; R2/R3 re-run green.
3. **Item 6 (the six knobs) — closed as scoped.** `review-u6-final` PASS: five knobs demonstrably reach
   the batch argv with the contracted flags and neutral defaults; the two configurations are independent;
   the rules precedence is exactly page → legacy tools → discovered → none; `lint.verible.autofix` is an
   honest `TransportPending` (the frozen ABI has no field), the panel states why, and setting it produces
   no flag (never a silent invalid parameter). The guide's §15.3 now states this explicitly.
4. **The A2/A4 acceptance-evidence gap — closed.** `review-a2-final` PASS and `review-a4-final` PASS are
   the independent verdicts; their carried Highs are closed by the newer reviews (`review-u3-final-r2`
   for the shortcut collision; `review-u6-final` for the lint transport/discovered-rules consumer).

## 3. Matrix audit — no overclaim

- **Every row marked PASS is backed by a review report or a gate log I opened.** I read the verdicts of
  `review-a2-final`, `review-a4-final`, `review-u1-final`, `review-u2-final`, `review-u2b-final`,
  `review-u3-final-r2`, `review-u4-final`, `review-u4b-final`, `review-u5-final`, `review-u6-final`,
  `review-b2-r5`, `review-b1-r2`, `review-b3-r3`, `review-b4-r2`, `review-a3-r2`, and the five gate logs.
- **Carried exemptions are still listed as exemptions** (`AC-MATRIX.md` §2): the MPD `typecheck` gate
  (pre-existing header drift) and the MPD `tests` gate (path that does not exist; Rust tests via
  `scripts/gates.sh`, C++ via ctest), plus no real-machine GUI and the lane-selftest SIGPIPE(141).
- **The known limitations are present and correctly worded** (§6): no real-machine GUI (1); cross-file
  rename NOT-CLAIMED (2); RE2 vs PCRE (3); the shadowed-definition / KDE-repository disclosure (4); the
  FIM Rust-side Chinese strings (5); the replace residual bound worded as the **400-character preview**
  with the whole-line pre-save refusal and the fully-identical-copy exemption (6). The user guide (mtime
  12:37) matches this wording — the docs carry from `review-b2-r5` is closed.
- **Medium (matrix currency):** the draft still contains stale “pending” cells that are now PASS:
  §3 B2 (`复审中`) and U3 (`复核中`), §4 rows 5/6/7/11 (`待`), and the §8 ledger row
  `review-u3-final-r2` (`待填`) — the reviews are PASS (`review-b2-r5` 11:13, `review-u6-final` 11:19,
  `review-u3-final-r2` 11:57). Update the matrix before it is handed to the user; this is a document
  edit, not a re-review.
- **Medium (frozen input shape):** `run-gates.sh` prints the “U8.1 GATE SUMMARY” table to stdout; the
  table itself is not saved as a file in `u8-final/`. The five individual gate logs are present and each
  carries its own PASS line (quoted below), and §5 of the matrix tabulates the exits — so the gate
  result is fully reconstructable; save/tee the summary next time for a self-contained frozen input.

## 4. Final gate logs (read, not re-run — the captain runs the one gate)

| Gate | Log | Observed |
|---|---|---|
| rust-gates (clippy + fmt + cargo test) | `u8-final/rust-gates.log` | `GATES PASS (local)` and `GATES PASS (host: dependency policy locally + compile gates in the pinned image)` |
| qt6-lane (configure + build + ctest + xcb smoke) | `u8-final/qt6-lane.log` | `100% tests passed, 0 tests failed out of 50` (75.04 s); `M0-SMOKE ok=1 platform=xcb … window=1 editor=1 document=1`; `[qt6] PASS` |
| selfcheck | `u8-final/selfcheck.log` | `APP-SHELL-SELFTEST ok=1 checks=319 failures=0` (and `EDIT-CORE-SELFTEST` in the same log) |
| docs | `u8-final/docs.log` | `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md` |
| lrelease | `u8-final/lrelease.log` | `1211 translation(s) (1211 finished, 0 unfinished, 0 untranslated)`; `.qm` 129,051 B sha256 `b7f2f50c…` |
| package build | `u8-final/package-build.log` | `BUILD: F2 runtime OK`, `steps_failed=0`; AppImage 136,051,192 B sha256 `2b95e56f…`; AppRun guard `CHECK PASS` under `LC_ALL=C` and `zh_CN.UTF-8` |

**Repackaged artefact (verified on the final artefact, not the build tree):**
- AppDir selfcheck: `SELFCHECK PASS` (`u8-final/verify-selfcheck-appdir.log`).
- AppRun guard under **both** locales: `verify-apprun-zh_CN.UTF-8.log` and
  `verify-apprun-en_US.UTF-8.log` both start with `AppRun: locale '…' (UTF-8) -> 'C.UTF-8'` and both end
  with `APP-SHELL-SELFTEST ok=1 checks=319 failures=0` — i.e. the guard let the app run under both.
- Packaged catalogue: the AppDir's `bin/translations/vedit_zh_CN.qm` is **129,051 B, 1211 messages**, mtime
  12:41:53 (newer than the catalogue 12:01:31), and its sha256 `b7f2f50c…` is **identical to a fresh
  `lrelease` of the current `.ts`** (`u8-final/lrelease.log`). I also extracted the AppImage payload
  myself (`--appimage-extract bin/translations/vedit_zh_CN.qm`, read-only) and it is the same file:
  129,051 B, sha256 `b7f2f50c…`.
  **Input note:** the Lead's expected “127,731 B / 1208 messages” is stale — the final catalogue grew
  (U2d/U3b added messages: 1211 now) and the verify script deliberately switched to the sha comparison
  because a byte count drifts. The artefact is correct; do not “fix” it to the old number.

## 5. Blocker / High findings

**None.** No Blocker/High remains on the final tree; every round-1 High is closed and the final gate is
green on the same tree.

## 6. Mediums met (short list; none blocks the release verdict)

1. **Matrix currency** — §3/§4/§8 still show “pending” for reviews that are now PASS (B2, U3, U6); the
   `review-u3-final-r2` ledger cell says “待填”. Fill them before the matrix goes to the user.
2. **Gate summary not persisted** — `run-gates.sh` prints the summary table; only the five per-gate logs
   exist. Reconstructable (all five are green), but the frozen input set should contain the table.
3. **`translations/` is untracked** (matrix §6.11) — the AppImage already ships the correct `.qm`, but the
   commit (captain, post-acceptance) must `git add translations/` explicitly or a clean checkout builds an
   English UI.
4. **Verification-currency note** — four lane reviews (`review-a2-final` 10:44, `review-u1-final` 10:44,
   `review-a4-final` 10:55, `review-u6-final` 11:19) predate the final integration edits
   (`app_shell.cpp` 11:21, `diagnostics_integration.cpp` 12:35, `main.cpp` 12:33, `USER_GUIDE.md` 12:37 —
   the U3b/T34 close-out). The final gate re-ran the affected suites on the final tree
   (ctest 50/50 incl. the B3/B4/search/settings tests, selfcheck 319, AppRun selfcheck) and the U3b
   changes are the `review-u3-final-r2` Nits being taken — so the evidence chain is sound, but there is
   no separate review report for those last integration edits.

## 7. Bounds

- **Read (not re-run):** the frozen contract §0/§0.1/§2 and the handover §1/§8; the matrix draft;
  `u8-final/run-gates.sh` and every `*.log` in it; the repackaged `packaging/runtime/out/` (AppImage
  mtime/sha, AppDir `.qm` mtime/sha/size, AppRun logs); every new review's verdict and the sections named
  above; the relevant source mtimes for the reviewed areas; the current `docs/USER_GUIDE.md` §12/§15.3/§16
  and the `.ts` entries for the menu/panel strings.
- **Ran (narrow, read-only):** the AppImage payload extraction + sha256 of the packaged `.qm`; a fresh
  `lrelease` count check was already in `u8-final/lrelease.log` (I compared its sha to the packaged file);
  no build, no product relink, no test suite re-run this round (the final gate logs are the frozen
  evidence, and my own B2 probes/runs from `review-b2-r5` are the B2 on-tree proof — the lane sources are
  unchanged since then).
- **Could not verify without a real desktop:** the real-machine smoke list (§7 of the matrix, 13 items) — appearance,
  DPI, fonts, CJK rendering, the actual mouse gestures, the AppImage FUSE start on CentOS 7. These remain
  the user's real-machine acceptance.
- **Not read:** the full source of every lane again (the independent reviews are the verdicts, per the
  acceptance standard); the commit history (none — the batch is uncommitted by design).

## 8. Release verdict (one sentence)

**RELEASE** — the final tree passes all 14 user requirements plus the SDC/XDC and pane-activation extras
(and both cross-cutting lint rules), every PASS row rests on an openable independent review or the
captain's green gate log, all four round-1 Highs are closed, and no Blocker/High remains; only the four
delivery Mediums in §6 (matrix currency, gate-summary file, untracked translations at commit time, and
the last integration edits having no separate review) need attention before/at the commit.
