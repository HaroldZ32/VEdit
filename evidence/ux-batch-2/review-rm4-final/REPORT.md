# RM-4 final independent review — interface-panel / module-tree jumps

- Task: T9 (independent verification of T5 / RM-4), read-only.
- Reviewer: `reviewer-3` (Reviewer).
- Date: 2026-11-06 (Asia/Shanghai).
- **Verdict: FAIL (request changes). Risk: HIGH.**
- Basis: the RM-4 card in `evidence/ux-batch-2/REAL-MACHINE-FINDINGS.md`; the writer's
  evidence `evidence/ux-batch-2/rm4-jump/` (README sha256
  `7f9b5fbcd8d61d952310af8cb234590697265460b62e99e82973518f982aeaa9`); the code.
- Reviewed tree: `src/workspace/search/search_jump_target.cpp` sha256 `3de823a9…`,
  `src/workspace/jump/jump_registry.cpp` `53b37aed…`, `jump_target.h` `0b794f8f…`,
  `interface_panel.cpp` `8b9f15ce…`, `module_tree_panel.cpp` `f6411276…`,
  `jump_tests.cpp` `55dba879…`; the probe binary `target/rm4-jump-probe/rm4-jump-probe`
  sha256 `fe7adfe6…` (built 16:12, newer than `search_jump_target.cpp` at 16:11). Provenance:
  `05-provenance.txt`.

---

## 1. What I re-derived (not copied from the writer's narrative)

### 1.1 The writer's after-state, re-read

- `logs/12-after-project-and-seam.out` records the full triple for both gestures:
  declaration `PASSED/RECEIVED/RESOLVED … child.sv:1:34`, `identifier-under-cursor='clk'`;
  instantiation `PASSED/RECEIVED/RESOLVED … top.sv:3:9`, `identifier-under-cursor='u_child'`;
  the panel line `port clk declaration → child.sv:1:34` / `instantiation of child in top → top.sv:3:9`.
- `logs/22-cases-after.out` records the T25 cases: case 1 (cwd = HOME/`/`, bare launch → Open
  Folder, both panels) delivered; case 2 (real catalog, relative persisted file set) delivered;
  case 2b (seam handed `child.sv` with cwd `/tmp`) delivered under the workspace root; case 3
  (fixture-c, CJK prefix) cursor `1:53` → `1:45`, `clk`; case 4 (ghost refusal) panel line names
  the full path, the workspace root and the reason, and the status is still `rejected`.
- `logs/21-case2-before.out` is the failing-before half for case 2b: `delivered=0` with
  `cannot open /tmp/child.sv: not a regular file`; `logs/10-before-position-path.out` is the
  failing-before half for the byte column (`cursor=1:49`, `identifier-under-cursor=''`) and for the
  relative path; `logs/11-before-panel-reason.out` is the failing-before half for the panel
  message (no `— <reason>`), with the rest already fixed.
- I re-ran the jump lane's ctest in the pinned image (`01-jump-tests-rerun.log`):
  `SUMMARY checks=53 failures=0`, `1/1 Passed 0.01 sec`, including all 13 `reason.*` checks
  (target sentence survives, silent-target default, NoTarget, InvalidRequest, delivered empty).

### 1.2 The custom fixture that breaks the byte-column claim (new evidence)

The writer's fixture-c puts the port near the end of the line, so the catalog's byte column
overflows the line's character count and the seam's `column > lineChars + 1` heuristic fires.
I built a **second** two-file project with a short CJK prefix and a long line —
`evidence/ux-batch-2/review-rm4-final/fixture-bytecol/`:

```
module child (/* 中文 */ input  logic       clk, /* padding … 12× */ output logic [3:0] y);
```

The catalog column for `clk` is **47** (bytes), the character ground truth from the fixture
text is **43**, and the line's character length is **171**, so `47 > 171 + 1` is false and the
seam performs **no** conversion. I ran the writer's own probe (product libraries + real
`EditorJumpTarget` + real panel wiring) in the pinned image (`07-bytecol-probe.log`):

```
ok   rm4.ground-truth.port-declaration — child.sv:1:43
     PASSED   : …/fixture-bytecol/child.sv:1:47 (status=delivered)
     RECEIVED : …/fixture-bytecol/child.sv:1:47
     RESOLVED : …/fixture-bytecol/child.sv cursor=1:47
     PANEL    : port clk declaration → child.sv:1:47
FAIL rm4.double-click.port-declaration.cursor-on-identifier —
     identifier-under-cursor='' expected='clk' column=47
ok   rm4.ctrl-double-click.instantiation.cursor-on-identifier — 'u_child' column=9
checks=25 failures=1
```

The jump **succeeds** (status delivered, the panel says "→ child.sv:1:47") but the cursor is
four columns past `clk` and sits on no identifier. The same test source would also fail on the
pre-fix code (it would clamp `47` to `47`), so this is not a regression introduced by the fix —
it is an **unfixed general case** the fix's bound claims to cover.

### 1.3 Code facts (why the custom fixture fails)

- `crates/vd-deeplint/src/hierarchy.rs :: symbol_position` reads slang's `source_column` and
  exposes it 0-based; `module_catalog.cpp :: jumpFromPosition` adds 1. The catalog's column is a
  **UTF-8 byte** column (fixture-c: 53 for character 45; my fixture: 47 for character 43).
- `search_jump_target.cpp :: EditorJumpTarget::jumpTo` converts only when
  `column1 > lineChars + 1` — i.e. when the byte column is **beyond the line**. A byte column
  that is inside the line's character count (my fixture: 47 ≤ 172) is treated as a character
  column. The code comment and the README §3.2 bound say "UTF-8 byte columns are handled";
  only the beyond-the-line subset is.
- `catalog_client.cpp :: absoluteSourcePath` is **pre-existing in HEAD** (not added by this
  task), which is why the T25 case 2 "real catalog, relative file set" delivered before and
  after; the seam relative-path fix is correctly described as defence in depth, not the user's
  cause.
- The panel reason path is real: `interface_panel.cpp :: InterfacePanel::deliver` prints
  `— result.reason` for `Rejected`/`InvalidRequest`; `jump_registry.cpp` fills the reason from
  `IJumpTarget::lastError()`; `interface_panel.cpp`/`module_tree_panel.cpp` both do it.

---

## 2. The six checks

| # | Check | Result |
|---|---|---|
| 1 | Root cause stated precisely on both sides, matching the code, both gestures addressed | **FAIL.** The refusal chain is mapped precisely (panel messages, `jump_registry.cpp` `Rejected` set point, `EditorJumpTarget::jumpTo` check list), but the writer explicitly says **"I could not reproduce the user's refusal … so I will not name a cause"** (§4, "RM-4 root cause itself" not proven). The T25 matrix shows the product path delivered **before and after** the fixes; the two fixes are not claimed as the user's cause. The mapping is a diagnostic, not a root cause of the every-jump refusal. |
| 2 | Both gestures land on the right place; line/column on the identifier | **FAIL (partial).** On the writer's fixtures the declaration lands on `clk` (child.sv:1:34; fixture-c 1:45) and the instantiation on `u_child` (top.sv:3:9) in the correct pane, and the module-tree gesture lands on `child`; those are correct. But the declaration gesture **succeeds with the cursor off the identifier** on a general byte-column line (my `fixture-bytecol`: ground truth 1:43, resolved 1:47, `identifier-under-cursor=''`, `failures=1`). The fix only catches columns beyond the line. |
| 3 | Genuine refusal still refuses and reports WHAT + WHY | **PARTIAL.** The ghost/missing file still returns `rejected`, the panel prints the full path, the workspace root and the reason (`cannot open /nonexistent-rm4/ghost.sv: not a regular file [workspace root: …]`); no-target prints its cause. But the card's third named genuine failure, **outside the workspace**, is not tested: `EditorJumpTarget::jumpTo` opens any existing absolute path (no containment check) and only re-roots relative paths; whether that is intended (RM-9 filelist-outside files must be jumpable) or should refuse is not stated or tested. The interface panel's own `!jump.isValid()` guard also drops the reason (L1). |
| 4 | Red run fails pre-fix | **PARTIAL.** Red/green pairs exist for the three fixes: relative path (`21` refused → `22` delivered), panel reason (`11` dropped → `12` carries), byte column (`10` cursor 1:49/empty → `22` 1:45/`clk`). **No red run exists for the user's every-jump refusal** — the writer could not reproduce it; the writer says so. My custom fixture fails on **both** pre- and post-fix, so the new regression does not cover the general byte-column case. Residual risk is High. |
| 5 | Triple (panel path → seam receives → editor resolves) recorded | **PASS (with a Nit).** `logs/12` records `PASSED / RECEIVED / RESOLVED / PANEL` for both gestures; my `07-bytecol-probe.log` records the same triple and exposes the wrong column. The "final one-shot" `logs/17` and the T25 `logs/24` contain no `RECEIVED` line (0 occurrences), and the Xvfb `logs/05` also omits it — the triple exists in `12`/`07` but not in the final runs. |
| 6 | Bounds honest about the real desktop | **PARTIAL.** The bounds are honest about offscreen/Xvfb (no real display server/mouse, no compositor) and about the unknown root cause/the unmatched AppImage. But the §3.2 bound "**UTF-8 byte columns are handled**" is an overclaim: only beyond-the-line byte columns are converted; the custom fixture is a counterexample (see H2). |

---

## 3. Findings (ranked)

### H1 — HIGH — the user's every-jump refusal is not reproduced and no root cause is claimed

**What.** The writer's own conclusion is "UNKNOWN root cause, COMPLETE diagnostic": no case
reproduces the user's refusal through the product's own code path; the T25 matrix (fresh run,
second run, non-ASCII path, Xvfb/xcb, cwd `/`, restore→switch, cwd = HOME/`/` + Open Folder,
relative project-model) all delivered, and the pre-fix code also delivered the product-path cases.
The two fixes are not the user's cause: the relative path is not reachable through the product
catalog (`absoluteSourcePath` is pre-existing), and the byte-column bug mis-positions, it does not
refuse. The panel-reason fix is a diagnostic, not a success fix.
**Where.** `evidence/ux-batch-2/rm4-jump/README.md` §2/§4 ("What is NOT proven — the RM-4 root
cause itself"); the probe matrix `logs/20`, `logs/21`, `logs/22`, `logs/24`.
**Why it matters.** The user's acceptance is "every interface-panel jump refused"; that failure is
still unexplained and unproven fixed. A PASS would tell the user the bug is fixed when the next
refusal may reproduce. A red run for the user's defect does not exist because the defect cannot
be reproduced; the reviewer must rank that residual risk, and it is High.
**Direction (described, not applied).** Get the user's one line from the panel's status after one
refused double-click — it now prints
`port <name> declaration: the editor rejected the jump to <file>:<line>:<col> — <reason> [workspace root: <root>]`
— and fix the actual `<reason>` branch; or have the Lead/user explicitly accept a
"diagnostic-only" scope for RM-4 while the user's next run supplies the datum. Do not mark the
user acceptance PASS before that.

### H2 — HIGH — the byte-column fix only handles columns beyond the line; a declaration jump can still land on the wrong place

**What.** `EditorJumpTarget::jumpTo` converts the column only when
`column > document->lineLength(line) + 1`. The catalog's column is a UTF-8 **byte** column for
every line (`symbol_position`/slang `source_column`), so a line with a short CJK prefix and a long
tail has a byte column **inside** the line's character count; the seam treats it as a character
column and the cursor lands at the byte column. My two-file fixture (`fixture-bytecol`) reproduces
it through the real panel/target: ground truth `child.sv:1:43`, panel/registry/target/editor all
`1:47`, `identifier-under-cursor=''`, `checks=25 failures=1`. The writer's fixture-c only covers
the overflow case (byte 53 > char 49+1), which passes.
**Where.** `src/workspace/search/search_jump_target.cpp :: EditorJumpTarget::jumpTo` (the
`column1 > lineChars + 1` guard); `crates/vd-deeplint/src/hierarchy.rs :: symbol_position`
(the producer's byte `source_column`); `src/workspace/module_tree/module_catalog.cpp ::
jumpFromPosition`; the writer's bound in README §3.2 ("UTF-8 byte columns are handled").
**Why it matters.** The parent's #2: a jump that succeeds but lands 3 lines/columns off is a
finding, not a pass. The user's code has Chinese comments; a port on a long line with a short CJK
prefix will land on the wrong identifier while the panel reports success. The writer's own note
that the producer is outside the RM-4 write scope is correct — but then the seam's compensation is
incomplete and the bound must say so, or the producer must be fixed in the catalog lane.
**Direction.** Fix the producer to emit **character** columns (convert `source_column` bytes to
characters in `hierarchy.rs`/`module_catalog`, or carry the column convention across the ABI), or
make the seam's conversion unconditional for a catalog that declares byte columns; add a
short-CJK-prefix + long-line fixture to the probe (the current `fixture-c` only tests the
overflow). Until then the declaration gesture is not generally on the identifier.

### M1 — MEDIUM — "outside the workspace" is one of the card's genuine failures but is untested and undefined

**What.** `EditorJumpTarget::jumpTo` re-roots a **relative** path under `shell_->projectRoot()`
when the file exists there; it does **not** contain an absolute path to the workspace, and it
opens any existing absolute path. The card's three genuine failures are file missing / outside the
workspace / no editor lane; the probe tests missing (`ghost.sv`) and no target, but not an existing
absolute file outside the root. The intended rule is also cross-cut by RM-9: filelist-only files
**outside** the opened directory are legitimate and must stay jumpable. The writer's evidence does
not say what happens or what should.
**Where.** `src/workspace/search/search_jump_target.cpp :: EditorJumpTarget::jumpTo` (relative
re-root only; no containment); README §1/§3.1; the probe's `runSeamChecks` (ghost/missing only).
**Why it matters.** The parent's #3 names outside as a genuine failure that must report what and
why; a malformed catalog path outside the root would silently open a file the user did not expect,
while a filelist-only file outside must stay legal. The behaviour must be decided and tested, not
left implicit.
**Direction.** Add a probe case with an absolute existing path outside the root; decide and
document: allow it (filelist-only files) with the path shown in the jump status, or refuse it with
a reason naming the path and the root; if allowing, add a positive check that a filelist-only
outside file still jumps.

### L1 — LOW — the interface panel's own invalid-jump guard still drops the reason

**What.** `InterfacePanel::deliver` starts with `if (!jump.isValid()) { ++jumpsSkipped_; setStatus(description); return false; }`.
`description` names the gesture but not why the catalog had no usable position. The module-tree
panel's per-row path does say `X has no definition position in the module catalog, so nothing was
opened`; the interface panel's per-port path does not. The writer's "message #3 cannot fire from
this panel" is about the registry's `InvalidRequest`, not this panel-local guard.
**Where.** `src/workspace/module_tree/interface_panel.cpp :: InterfacePanel::deliver` (the
`!jump.isValid()` branch); `module_tree_panel.cpp :: ModuleTreePanel::deliver` has the same guard
but its row path already explains.
**Why it matters.** A malformed catalog entry is a true failure; the card says a true failure must
report what and why. Today it reports only what.
**Direction.** Set a reason such as "the module catalog has no position for this port" in the
invalid branch, matching the module-tree row wording.

### N1 — NIT — the final one-shot and Xvfb runs do not print the `RECEIVED` triple

**What.** `logs/12` and my `07` print `PASSED / RECEIVED / RESOLVED / PANEL`; `logs/17` (final
one-shot), `logs/24` (T25 final) and `logs/05` (Xvfb) contain no `RECEIVED` line. The parent's #5
triple is in `12` (and in my custom run), but not in the "final" evidence the Lead is most likely
to read.
**Where.** `logs/17-final-all.out`, `logs/24-final-t25-all.out`, `logs/05-xcb-run.out`; the
`runGestures` print in `probe/rm4_probe.cpp`.
**Why it matters.** The triple is the artifact that was wrong; a final run that omits it makes the
final acceptance look one line shorter than the evidence in §2.
**Direction.** Include the `RECEIVED` line in the final run (or cite `logs/12` explicitly in the
final table).

### N2 — NIT — the probe's `identifierAt` requires the identifier to *start* at the cursor

**What.** `locate`/`identifierAt` in `probe/rm4_probe.cpp` treat a position as valid only when it
is the **start** of an identifier; a cursor in the middle of the same identifier returns empty.
This is stricter than the parent's "lands on the identifier" (a middle position is still on it);
it cannot produce a false pass, but it can fail a legitimate near-miss in a future fix.
**Where.** `probe/rm4_probe.cpp :: identifierAt`.
**Why it matters.** Test-design note for the next iteration; no current product defect.
**Direction.** Optionally accept any position inside the identifier, or keep the strict start
position and say so in the check's message.

---

## 4. Declined to judge (reason on the same line)

- **Module-tree acceptance 3 failures** (`h1.first-appearance.file-set/.uses-seeded-set/.source-is-model`) — the identical 3 appear in `logs/09-acceptance-test.out`, captured at 15:31 before this task's changes, and `src/workspace/project/project_model.*`/`project_integration.*` are modified by another lane (RM-2/RM-3); not an RM-4 defect, and every interface-panel cursor check in that suite passes — declined as another lane's known failure.
- **The exact AppImage the user ran** — `target/release-assets/v0.2.0/…` predates the interface panel and `build/artifacts/vd-app` is the legacy GTK artifact; the binary cannot be matched to this source — declined as a bound (writer's §4).
- **UTF-16/LSP offsets** — the catalog path here is UTF-8 bytes; a UTF-16 offset that overflows a line is still approximate, as the writer's bound says; not triggered by this producer — declined.
- **The `message #3` dead branch** — the writer's check that the registry's `InvalidRequest` cannot fire from the panel is correct (`ModuleJump::isValid` + `jumpFromPosition` ≥ 1); the panel's own `!jump.isValid()` guard is a different path and is L1 — declined as covered by L1.
- **The module-tree panel's identical reason suffix** — verified in `module_tree_panel.cpp::deliver`; shares the registry reason; no separate finding.
- **The probe's synchronous GUI-thread execution / no real compositor** — the writer's bound; the Xvfb/xcb log (`05`) is the real-platform evidence available; declined as a bound.
- **The filelist-relative path shape** — the user's project has `.f` filelists; the T25 case 2 used a persisted project-model, not a filelist expansion; the producer's normalisation for filelist paths is not in the matrix — folded into H1, not judged separately (the catalog/filelist lane owns it).
- **Tooltip/other UI behaviour** — outside this card; not read.

---

## 5. Bounds of this review (what I did not read or run)

- I ran the writer's probe with a **custom two-file fixture** (`fixture-bytecol`) in the pinned
  `vedit-qt6-build:local` image; the probe links the product's own static libraries and installs
  the real `EditorJumpTarget`/panels. I reread the writer's logs and re-ran the jump lane's
  `vedit-jump-tests` (53/0).
- I did **not** run the real AppImage or a real display server; the only Xvfb/xcb evidence is the
  writer's `logs/05`; my custom run is offscreen.
- I did **not** rebuild the product from the current tree; the probe binary is the writer's
  16:12 build (newer than `search_jump_target.cpp` 16:11), and my custom run used it. The other
  changed files in the probe's link are the writer's 16:12 build.
- I did **not** run the module-tree acceptance suite in full; I read the writer's `23`/`09` logs
  and the 3 pre-existing failures; I did not test the outside-workspace absolute path
  empirically (code read; M1).
- I did **not** verify the user's real machine or the user's actual project (the reason line is
  the missing datum).
- I did **not** edit any product file; the only writes are this report, the custom fixture, and
  the review logs in `evidence/ux-batch-2/review-rm4-final/`: `01-jump-tests-rerun.log`,
  `05-provenance.txt`, `07-bytecol-probe.log`, `fixture-bytecol/`.

---

## 6. Verdict

**FAIL — request changes. Risk HIGH.**

What passes and should be kept: the refusal chain is mapped precisely; the panel now carries the
target's reason (path + workspace root + why) and a real refusal is still a refusal; the jump
lane's reason contract is tested (53/0, independently re-run); the relative-path seam resolution
has a genuine failing-before/passing-after pair; the fixture-c byte-overflow case has one too; both
gestures land on `clk`/`u_child` on the writer's fixtures, in the correct pane; the triple is
recorded in `logs/12` and in my custom run; the module-tree panel shares the fix. The diagnostic
work is solid and should not be reverted.

What fails the card: (1) the user's every-jump refusal is **not reproduced** and the writer
explicitly claims **no root cause**; the two fixes are not the user's cause, so the user's
acceptance remains unproven (H1). (2) The declaration gesture still **succeeds with the cursor off
the identifier** for a general byte-column line — my short-CJK/long-line fixture resolves `1:47`
against a `1:43` ground truth and lands on no identifier — so "double-click = declaration on the
identifier" is not generally true (H2), and the README's "UTF-8 byte columns are handled" bound
overclaims. (3) The card's "outside the workspace" genuine-failure case is neither tested nor
defined (M1). Until H1 has the user's reason line and H2 is fixed at the producer (or the seam's
conversion is made general), RM-4 cannot be marked fixed for the user.
