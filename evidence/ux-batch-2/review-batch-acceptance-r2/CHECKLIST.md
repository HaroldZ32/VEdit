# Round-2 whole-batch acceptance — pre-built checklist (reviewer notes)

- Owner: Reviewer (this agent). Round-1 record is frozen:
  `evidence/ux-batch-2/review-b2-r4/` (B2 terminal verdict, FAIL) and
  `evidence/ux-batch-2/review-batch-acceptance/` (whole batch, FAIL, snapshot 2026-11-06 10:15).
  Nothing in those two directories is edited by round 2; round-2 evidence goes here.
- Status: **pre-built, not started** — the Lead opens the round-2 rows after the repairs land (B2 repair +
  re-review, U4b, U6, U2b, U1, U3, A2/A4 independent reviews, AppImage repackage). Do not run a round-2
  verdict against the current tree: it would re-report the round-1 Highs.
- Method for round 2: re-read the frozen contract §0/§0.1/§B2/§B3/§5 and this checklist; for each row demand
  the evidence named below; re-run the B2 suite/probes and the full 7 gates where the row depends on them;
  keep a declined-to-judge list. A row is PASS only on evidence in the tree at verdict time, never on a
  promise or on a writer's README alone.

## Row evidence — one line per requirement (what would make it PASS)

1. **Module tree** — `review-b1-r2` PASS still applies to the current `src/workspace/module_tree/**` (no
   diff since the verdict that touches the reviewed code) + the round-2 full `ctest` shows the tree/interface
   test ids green; spot-check one live hierarchy in the rebuilt binary.
2. **Interface panel** — same as 1, plus the width/type/direction assertions and the double-click/Ctrl+
   double-click jump still pass in the round-2 `ctest`.
3. **Chinese translation** — fresh `lrelease translations/vedit_zh_CN.ts` = 0 unfinished; the **repackaged
   AppImage** ships that `.qm` (sha match); a zh_CN startup/screenshot shows the menu/panel/replace labels in
   Chinese; the U2/U2b catalogue includes the final strings (Replace=替换, Replace All=全部替换, the hover
   Fix source string corrected).
4. **Rename** — `rename 21/21`, 0 skips, 1 NOT-CLAIMED (cross-file) still in the round-2 logs; the
   capability gate (non-verible backend grey + request refused) still asserted; the guide §16.2 limitation
   still present.
5. **Search / project-wide replace (B2)** — the round-1 High is closed by evidence, not by wording: a new
   regression (red-before/green-after) shows a preview-identical duplicate inserted above a >400-char hit in
   a dirty buffer either **refuses** or **replaces the scanned line**, both buttons; the repair carries a
   full-line/whole-file digest (or the conservative ≥400-char refusal); the 229 baseline names are all
   present+PASS and the suite count grows, not shrinks; my own re-run of the new probe reproduces the fixed
   behaviour; the three disclosures say "identical 400-character preview" (repair README, lane README,
   `docs/USER_GUIDE.md` §16.4) and no longer claim "no wrong characters outside these bounds"; the round-1
   reviewer probe still 0 failures; the two-button/shared-option/RE2 rulings still hold.
6. **Format + LINT settings (U6)** — every knob's panel value reaches argv: per-knob assertions for
   `lint.verible.{ruleset,rules,rules_config,waiver_files,autofix}` and `lint.slang.{warnings,error_limit}`;
   defaults send no flag; the two configurations never overwrite each other; the discovered
   `.rules.verible_lint` is the third precedence level (manual > legacy tools > discovered > none); the
   `transport-pending` state is gone from both the UI and the READMEs; read `buildLintJobOptions()` and the
   ABI to confirm the flags are the ones the engines accept.
7. **Menu / toolbar (A2)** — the new independent **A2 review** PASS (Reviewer C) is the verdict of record;
   my batch row cites it and re-checks: File/Edit/View/Project/Help order, main toolbar buttons wired,
   undo/redo on KTextEditor, Preferences under Edit.
8. **Font size + scale (A4)** — the new independent **A4 review** PASS; my row re-checks: font size instant,
   scale applied before `QApplication`, restart note, the two settings keys.
9. **Hover + one-click fix (U1)** — the B3 `N2-r` High is closed with a red/green test (a covered file
   edited before the first poll and switched away invalidates its upper verdict — no stale hover/panel
   state); the new independent B3 verdict PASS; the fix button only when the engine can fix, one undo
   reverts; the U2b hover Fix source string is correct and translated.
10. **File status dot (U1)** — same repair: the stale **green** dot for a file that now has an error is
    gone (test at the switched-away edit path); red/amber/green from the merged view, not a per-frame rescan;
    the lane-level verdict PASS.
11. **Built-in manual (U3)** — the U3 review's **two false-claim findings** are fixed and the manual's
    claims match the code (search §12 incl. the two buttons/save-first/refusal cases/RE2 §12.4; diagnostics
    §13; the known-limits wording after B2's disclosure fix); spot-check every manual claim that names a
    behavior against the current source; the manual is still qrc-embedded and F1 opens it.
12. **ELAB modal** — the explicit-Lint-Project modal and the auto-lint clickable status line both still
    asserted; no modal on auto runs; the U1 repair does not regress the trigger path.
13. **Ctrl+click / Alt multi-cursor** — `review-b4-r2` PASS still applies to the current
    `multi_cursor.*`/`lsp_actions.*`; the pane-activation extra still green in the round-2 `ctest`.
14. **VHDL / Markdown / Tcl (+ SDC/XDC)** — `review-a3-r2` PASS plus **U4b**: `vedit-app-syntax-startup`
    asserts the **resolved definition names**, not just the XML file name, so a corrupt definition cannot
    ship green; the AppDir syntax check runs against the repackaged artifact.
+  **SDC/XDC → Tcl** — covered by row 14 (extension table assertion + AppDir).
+  **Pane activation** — row 13.

## Batch-level gates for round 2 (all must be green in the same run)

- **Build-tree discipline (captain's notice, 2026-11-06 10:34):** one shared `target/qt6-build`; a
  non-reproducing failure is re-run ONCE and reported as transient; never hand-edit generated files
  (`CTestTestfile.cmake`, generated headers, Makefiles) — copy a test definition under `target/<lane>/` if
  one must change; prefer `ctest -R <name>` or the already-built binary over a full build; a lane must NOT
  run the full gate (`build/gates/qt6-cxx.sh`, `scripts/gates.sh`) — the captain runs it once after freeze;
  if evidence needs a product relink (e.g. a UI label rather than a widget), say so as a **bound** instead of
  building.
- Full 7 gates: `build-qt`, `ctest` (count ≥ 50, no id removed), `--selfcheck` (312+ PASS), `clippy`
  `-D warnings`, `fmt --check`, `docs`, `lrelease` — **read the captain's one full-gate log; do not run the
  gate as a lane**. My own round-2 runs are narrow: the B2 `ctest -R` / the lane test binary, the probes,
  `lrelease` on the catalogue, and the repackaged-AppImage guards.
- **AppImage repackage (U8)**: `assemble-appdir.sh` + pack from the final tree; AppRun guard PASS under
  **both** `zh_CN.UTF-8` and `en_US.UTF-8`; the packaged `.qm` sha matches a fresh `lrelease`; the packaged
  `vedit-app` is from the final tree; a smoke run of the packaged artefact.
- **Acceptance matrix (U8)**: every one of the 14 + 2 rows has an evidence pointer; rows 3/7/11 (A2) and
  6/8 (A4) cite their new independent reviews, not "内含于 B 系评审"; the carried items are named as carries
  (cross-file rename NOT-CLAIMED; GUI real-machine acceptance left to the user).
- No open Blocker/High without an owner; every repair has a **different-agent** verdict (B2's new repair
  gets its own re-review, not my round-2 row only).

## Round-2 stop conditions (do not sign off if any is true)

- The B2 fix is wording-only or a test that still uses lines differing inside 400 characters.
- U1's `N2-r` is claimed fixed without the switched-away-edit red case; U6 is claimed without a per-knob
  argv assertion; A2/A4 rows are marked PASS without a review report.
- The AppImage is not repackaged, or its `.qm`/binary predates the final tree.
- The 229 B2 baseline names are not all present+PASS in the round-2 suite log.

## Endgame evidence (added 2026-11-06 per the Lead's endgame message — run only when T37 is delivered)

- **T14 is superseded** — when it becomes dispatchable, claim it and complete with the one-line note
  “superseded by T37 (round 2), round-1 report unchanged”; do NOT re-run the round-1 review.
- **T37 (Final acceptance round 2)** — run this checklist against the FINAL tree; do not start before the
  row is delivered.
- **`evidence/ux-batch-2/u8-final/` — read the captain's ONE full gate run, not second-hand:**
  `run-gates.sh`, `rust-gates.log`, `qt6-lane.log` (configure + build + ctest + xcb smoke), `selfcheck.log`,
  `docs.log`, `lrelease.log`, and the gate summary table. A **missing gate log** or a **non-zero summary
  line** is a finding, not a formality — quote the path and the exact line.
- **Repackaged AppImage + AppDir:**
  - AppRun guard PASS under **both** `zh_CN.UTF-8` and `en_US.UTF-8` (quote both runs).
  - the packaged `vedit_zh_CN.qm` is **127,731 B (1208 messages)** — the cheap proof that the new catalogue
    really made it into the artefact; check the AppDir and the AppImage payload (and sha against a fresh
    `lrelease` if the size alone is ambiguous).
  - the packaged `vedit-app` is from the final tree (mtime/sha vs the final build).
- **`evidence/final-acceptance-3/AC-MATRIX.md` (draft) — check it does not overclaim:**
  - every PASS row is backed by a review report or a gate log I can open (not a writer README, not a
    promise, not “内含于 B 系评审”);
  - the carried exemptions (**typecheck**, **tests**) are still listed as exemptions;
  - §6 known limitations all present and correct: no real-machine GUI; cross-file rename NOT-CLAIMED;
    RE2 regex; the replace bound worded as the **400-character preview** (not the old “identical line”);
    the FIM Rust-side Chinese strings; the shadowed-definition disclosure. Any missing or old-wording item
    is a finding.
