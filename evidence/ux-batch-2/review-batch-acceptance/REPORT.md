# Whole-batch acceptance review — UX batch 2 (14 user requirements + the two extras)

- Frozen contract: `.mpd/plans/ux-batch-2/00-plan.md` (§0 user rulings, §0.1 R1–R3, §B2/§B3/§B4, §5)
- Requirement list: `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §1 (14 items + SDC/XDC + pane activation)
- Task: `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §7 Step 4 ("B2 终审（U7）+ 全批终审（一个
  reviewer，只阻 Blocker/High）")
- Reviewer: independent; findings only — no product code, test, evidence or gate was edited.
- **Snapshot: 2026-11-06 10:15 CST, live tree.** The handover's own unfinished list (U1–U6, U8, U9) is being
  worked concurrently while this review runs: `translations/vedit_zh_CN.ts` was rewritten at 09:55,
  `docs/USER_GUIDE.md` at 10:06, `src/workspace/diagnostics/*` after the last B3 verdict, and the team board
  carries the in-flight tasks. This report is a snapshot of the tree at that time, not a verdict on work that
  lands afterwards.
- **Risk level: HIGH**
- **Verdict: FAIL** — two requirement groups have an open High (item 5 via this reviewer's new cap probe;
  items 9/10 via `review-b3-r3`), one has a functional High (item 6: six lint knobs are persisted but never
  reach the backend), and five requirements (3/7/11, 6/8) still have no independent lane verdict. The batch
  cannot be signed off yet; the requirement-by-requirement status is below.

## Assessment

Most of the batch is genuinely delivered and independently signed off: the module tree and interface panel
(item 1/2, `review-b1-r2` PASS), Ctrl+click / Alt multi-cursor and the pane-activation extra
(item 13 + extra, `review-b4-r2` PASS), the VHDL/Markdown/Tcl highlight definitions and the SDC/XDC mapping
(item 14 + extra, `review-a3-r2` PASS), and the Rename repair (item 4 — `rename 21/21`, 1 NOT-CLAIMED for
cross-file, confirmed not weakened by `review-b3-r3`). The translation catalogue is no longer the handover's
red mid-state: I ran `lrelease` on the current `translations/vedit_zh_CN.ts` myself — 1186 translations,
1186 finished, **0 unfinished**, exit 0 — and the B2 replace strings are now 替换 / 全部替换 in it, so the
two-button ruling is honoured in Chinese as well. The user guide has been rewritten since the handover and
now carries the search section (§12, including the RE2-vs-PCRE disclosure in §12.4/§16.3), the diagnostics
section (§13) and the known-limits list (§16) — the earlier "RE2 not documented" carry is closed **as of
10:06**; anyone reading an older tree should re-check it.

What fails the batch is not one thing. The search/replace lane (item 5) closes the three defects of its
previous three passes — I re-ran its reviewer probe verbatim, 0 failures, and the 229-check baseline is
275/0/0 with all 229 names present and PASS — but its per-hit identity is only the trimmed 400-character
preview, and my own probe shows a preview-identical duplicate inserted above a hit in a dirty buffer being
replaced instead of the scanned line, with a success status (`review-b2-r4`, finding 1). The diagnostics
lane (items 9/10) still has the `N2-r` stale-verdict window (`review-b3-r3` High: a covered file edited
before the first poll and then switched away keeps its old upper verdict — a green dot for a buffer with an
error); the `src/workspace/diagnostics` files are already newer than that verdict, so U1 is presumably
mid-repair, but no new evidence or verdict exists yet. The LINT settings (item 6) are half-wired: the 23
format knobs reach the formatter, but `lint.verible.{ruleset,rules,waiver_files,autofix}` and
`lint.slang.{warnings,error_limit}` are persisted and shown as "transport-pending" and never reach the lint
job, and the project's discovered `.rules.verible_lint` is still not consumed — `LintJobOptions` in
`src/workspace/settings/lint_options.h` has only `backend` and `rulesPath`; U6 is the assigned work and it is
not in the tree yet. Finally, the delivered package is stale: the AppDir's `vedit_zh_CN.qm` is 90,461 bytes
from 2026-11-04 20:20 while the catalogue is 302,283 bytes from 2026-11-06 09:55 (fresh `.qm` 125,624
bytes), and the packaged binaries predate the batch — U8 must repackage before any requirement can be
accepted on the delivered artefact. Independent review coverage is also incomplete: items 3/7/11 (A2) and
6/8 (A4) have no `review-*` directory of their own, and the handover's claim that they are "内含于 B 系评审"
is not supported by any of the B reviews I read.

## Requirement-by-requirement status (14 + 2 extras; Blocker/High only)

| # | User requirement | Status | Independent verdict / evidence |
|---|---|---|---|
| 1 | Module tree (top-level module hierarchy, double-click opens the module) | **PASS** | `review-b1-r2` PASS (the High-1 data-source bug fixed on first-open and in-flight paths) |
| 2 | Interface panel (focus module, type/direction/width, double-click → declaration, Ctrl+double-click → instance) | **PASS** | `review-b1-r2` PASS |
| 3 | Right-click menu + top bar + panel toggles fully translated | **PASS (framework + catalogue)**, with the acceptance-evidence finding below | A2 self-evidence (`evidence/ux-batch-2/a2/`); U2 finished the catalogue (lrelease 1186/0, verified by me); the final package must carry the new `.qm` (U8) |
| 4 | Rename Symbol | **PASS with a disclosed limitation** (cross-file NOT-CLAIMED) | `b3-repair*` evidence; `review-b3-r3` "Not weakened": rename 21/21, 0 skips, 1 NOT-CLAIMED; guide §16.2 states the limitation |
| 5 | Search panel aligned with Ctrl+F + project-wide search and replace | **FAIL** | `review-b2-r4` FAIL (High: 400-character preview identity → wrong-target write on a just-saved file, probe P5); the three earlier defect classes are fixed |
| 6 | Format and LINT options as settings | **FAIL (partial)** | A4 evidence: 23 format knobs wired; **6 lint knobs transport-pending**, discovered `.rules.verible_lint` not consumed (`LintJobOptions` has only `backend`/`rulesPath`); U6 assigned but not in the tree |
| 7 | Top bar / menu reordering, main toolbar | **PASS**, with the acceptance-evidence finding below | A2 self-evidence (`evidence/ux-batch-2/a2/`); no independent lane review |
| 8 | UI font size (instant) + scale (restart) | **PASS**, with the acceptance-evidence finding below | A4 self-evidence (`evidence/ux-batch-2/a4/`); no independent lane review |
| 9 | Error/warning hover + one-click fix | **FAIL** | `review-b3-r3` High `N2-r` (stale upper verdict reachable; hover/panel can show the stale state); U1 in flight (`src/workspace/diagnostics/*` newer than the verdict, no new evidence yet) |
| 10 | Yellow/red/green problem dot in front of the file name | **FAIL** | `review-b3-r3` High `N2-r` — a stale **green** dot for a file that now has an error; U1 in flight |
| 11 | Real manual in Help (F1) | **PASS**, with the acceptance-evidence finding below | A2 self-evidence; the manual is the qrc-embedded `docs/USER_GUIDE.md`, now including §12–§16; no independent lane review |
| 12 | ELAB error modal + jump guidance | **PASS (item behaviour)** | B3 lane E2E covers "modal only on explicit Lint Project, clickable status line on auto runs"; the lane-level FAIL is the separate stale-state bug (items 9/10), not the modal |
| 13 | Ctrl+click = go to definition (multi-cursor moved to Alt+click) | **PASS** | `review-b4-r2` PASS |
| 14 | VHDL / Markdown / Tcl highlighting | **PASS** | `review-a3-r2` PASS |
| + | SDC/XDC → Tcl mapping | **PASS** | `review-a3-r2` PASS |
| + | Clicking a non-active split does not switch it (found by review) | **PASS** | `review-b4-r2` PASS (the pane-activation evidence is included there) |

## Open Blocker/High findings

### High 1 — item 5 (B2): the per-hit identity is the trimmed 400-character preview, so a preview-identical duplicate is replaced instead of the scanned line

**What/where:** `src/workspace/search/search_types.cpp::matchedLineFingerprint` hashes the trimmed line
capped at 400 characters (mirroring `crates/vd-search::snippet_of`), and both guards
(`SearchPanel::replaceCurrentHit` before the save-first write, `ProjectReplacer::applyReplaceRanges` at
write time) compare only that value. A 400+-character hit line, a paste above it of a line identical in the
first 400 trimmed characters but different after, and the single 「替换」 on the dirty buffer: the panel
saves the buffer, the fingerprint and the range check both pass, the replacement is written into the
inserted line and the scanned line is untouched, with success reported. **Evidence:**
`evidence/ux-batch-2/review-b2-r4/REPORT.md` (finding 1) and its `logs/03-cap-probe.log` — `PROBE P5.started=1
saves=1 ok=1 replacements=1 line0-right='XXXXXXTAIL_B' line1-equals-original=1`, `P5.no-wrong-target FAIL`.
The lane's disclosures (repair README §6.2, lane README §8, guide §16.4) call this bound an **identical**
line, which is harmless; the implemented class is preview-identical, which is not. **Direction:** carry a
full-line (or whole-file) digest from `vd-search` and compare it in both guards, or conservatively refuse a
dirty-buffer replace when the hit line's preview is at least 400 characters; fix the three disclosures to say
"400-character preview" and add the regression test. **Owner: none yet** — this is a new finding; it needs a
repair task.

### High 2 — items 9/10 (B3): the stale upper verdict survives for a covered file edited before the first poll and then switched away

**What/where:** `ActiveFileWatcher::poll()` marks a dirty buffer only for the file active at the poll;
`src/workspace/diagnostics/*` keeps the old upper verdict for a covered file that is opened, edited before
its document is subscribed (the 400 ms poll window), and switched away before that poll. It keeps a **green
dot** for a file with an error, and C8 keeps the dirty file out of later passes, so the stale state persists.
**Evidence:** `evidence/ux-batch-2/review-b3-r3/REPORT.md` (High `N2-r`, risk HIGH, verdict FAIL; per-item N2
"PARTIALLY FIXED"); no test covers it. **Status:** U1 is the assigned repair; `src/workspace/diagnostics/
diagnostics_integration.cpp` and `selftests/b3_wiring_e2e.cpp` are newer than the r3 verdict, so work is in
flight — but at snapshot time there is no new evidence and no new verdict, so the requirement is not signed
off.

### High 3 — item 6 (A4/U6): six LINT knobs are persisted but never reach the lint job (and the discovered rules file is not consumed)

**What/where:** `src/workspace/settings/lint_options.h::LintJobOptions` carries only `backend` and
`rulesPath`; `buildLintJobOptions()` fills only those. `lint.verible.{ruleset,rules,waiver_files,autofix}` and
`lint.slang.{warnings,error_limit}` are editable and persisted (and the page/tooltip says
"transport-pending"), but no argv is built from them; the third precedence level (the project's discovered
`.rules.verible_lint`) is permanently empty because `VdProjectInfo` does not expose it. **Evidence:** A4
`README` §4.1/§4.2 (the lane's own disclosure), `src/workspace/settings/lint_options.h` (read in the current
tree), handover §3.U6. **Direction:** U6's single wiring point — add the fields and fill them in
`buildLintJobOptions()`, plus the per-knob argv assertions; then the user's item 6 is genuinely delivered.
**Owner: U6** (assigned, not in the tree at snapshot time).

### High 4 — acceptance evidence: five requirements (3/7/11, 6/8) have no independent lane verdict, and the handover's coverage claim is unsupported

**What/where:** `docs/handover/...` §2 says A2 and A4 are "内含于 B 系评审"; the B reviews I read
(`review-b1-r2`, `review-b2*`, `review-b3*`, `review-b4*`, `review-a3*`) do not audit A2's or A4's
requirements (menu order/toolbar, i18n, qrc manual, font/scale, format knobs, dual-backend lint page). The
lane self-evidence exists (`evidence/ux-batch-2/a2/`, `a4/` with commands and observed results), but under
this repository's verification law a requirement's acceptance rests on a different agent's verdict, not on
the writer's own README. **Why it matters:** the final AC matrix (U8) must not mark these five as
"independently PASS" without a reviewer; either run an independent A2/A4 pass before U8 or record them as
"self-evidence only" in the matrix. This is a process/evidence finding; it does not by itself claim a
user-visible defect.

## Observations that are not Blocker/High (declined to rank)

- **The delivered AppDir/AppImage is stale** (packaged `vedit_zh_CN.qm` 90,461 bytes 2026-11-04 vs the
  302,283-byte catalogue of 2026-11-06, fresh `.qm` 125,624 bytes; packaged binaries predate the batch). This
  is U8's job, not a requirement defect; it does become a delivery blocker if U8 ships the current package.
- **The handover's "lrelease is red" mid-state is resolved** — I ran `lrelease` on the current catalogue:
  1186 finished / 0 unfinished, exit 0. Re-check after U2's final pass.
- **The RE2-vs-PCRE disclosure is now in `docs/USER_GUIDE.md`** (§12.4, §16.3; landed 10:06) and in the
  dock tooltip and lane README — the earlier carry is closed for the current tree.
- **The Ctrl+F bar's transient wording when a whole-project hand-off is refused** (in
  `src/editor/kernel/find_replace.cpp`) is a Nit, not a Blocker/High; it is reported in `review-b2-r4`.
- **The B3 Medium `N3-p`** (a settings/backend switch clears the rows but does not cancel a running pass) is
  a Medium in `review-b3-r3`; it does not change the batch verdict and U1 should take it in the same round.

## Declined to judge

- **Re-running every lane's suite** (B1, A3, B4, A2, A4 and the B3 product E2E): I read their reports and the
  B2/B3 failures I could reproduce; I did not re-run B1/A3/B4/A2/A4, and I say so rather than calling their
  PASS verdicts mine.
- **The full 7-gate run, the AppImage locale guards, the real-machine UX and the CJK tofu** — U8's scope and
  the handover's §5.F6/F9; not re-run here.
- **The other lanes' code outside the requirement paths** (VCS, terminal, FIM, the Rust service layer) —
  not this batch's 14 items.
- **The in-flight U1–U6/U8/U9 work** — the tree changed under this review (translations 09:55, guide 10:06,
  diagnostics after the B3 verdict); a finding may be fixed after this snapshot, and the Lead should re-run
  the affected acceptance rows rather than treat this report as final for them.

## Bounds

- **Read:** the handover (all sections), the frozen plan (§0/§0.1/§1/§2/§B1–§B5/§5), every review report in
  `evidence/ux-batch-2/review-*`, the A2/A4/A3/B1/B4 evidence READMEs' acceptance and limitation sections,
  `review-b3-r3` in full, `review-b2-r4` (my own), the current `docs/USER_GUIDE.md` §12/§13/§16,
  `translations/vedit_zh_CN.ts` (counts + the replace strings), `src/workspace/settings/lint_options.h`,
  the B2 lane sources named in `review-b2-r4`.
- **Ran:** the B2 reviewer probe (verbatim, 0 failures), the B2 lane suite (275/0/0), the 229-name diff, the
  B2 cap probe (P5 reproduces), `lrelease` (1186/0). All in `vedit-qt6-build:local`, offscreen, against the
  current lane library.
- **Did not run:** any B1/A3/B4/A2/A4 suite, `bash scripts/build-qt.sh`, the AppImage repack, the 7 gates,
  a real desktop session. The verdict on those requirements is therefore "as reported by their reviews /
  self-evidence", not "re-verified by me".

## Verdict

**FAIL.** At this snapshot the batch has two open data/state Highs (item 5: the 400-character preview identity
can write the wrong occurrence on a just-saved file; items 9/10: the stale upper verdict can leave a green dot
on a file with an error), one functional High (item 6: six LINT knobs never reach the backend, U6), one
acceptance-evidence High (items 3/7/11 and 6/8 have no independent verdict), and a stale delivered package.
The other eight requirements plus the two extras are independently PASS and their evidence is real. The batch
can be accepted after the B2 cap repair, U1's diagnostics repair (with a new verdict), U6's LINT wiring, an
independent A2/A4 pass, and U8's repackaging with the fresh `.qm`.
