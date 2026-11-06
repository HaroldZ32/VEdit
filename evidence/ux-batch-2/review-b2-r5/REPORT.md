# Review — UX batch 2, lane B2, repair round 5 (B2b: per-hit identity)

- Frozen contract: `.mpd/plans/ux-batch-2/00-plan.md` §`B2` (acceptance ①–⑥, the unsaved-buffer
  sentence, the RE2-vs-Ctrl+F documentation sentence)
- Task: **T31** — re-review of the B2 repair; the contract this round answers is
  `evidence/ux-batch-2/review-b2-r4/REPORT.md` (FAIL, High: the per-hit identity is the trimmed
  400-character preview, so a preview-identical duplicate inserted above a hit is replaced instead of
  the scanned line — probe P5)
- Repair under review: `evidence/ux-batch-2/b2-repair-r4/` (task T27; write scope
  `crates/vd-search/`, `src/workspace/search/`, its evidence dir; the README claims the reviewer's
  P5 probe rebuilt unchanged is 0 failures and the lane suite is 299/0/0)
- Review date: 2026-11-06
- Reviewer: independent; findings only — no product source, test, gate or repair artefact was edited.
  `review-b2-r4/` is untouched (verified by sha256 on both probe sources before and after this review);
  this round writes only to `review-b2-r5/`.
- **Risk level: LOW** for the repaired lane code (the wrong-target write is now refused before any save,
  with a user-visible reason, and the disk is byte-identical). **MEDIUM carry** for the user-facing
  manual, which still describes the pre-repair behaviour (docs lane, listed in finding 1).
- **Verdict: PASS** — P5 is closed by a refusal that is user-visible and byte-safe, the positive
  control still replaces the scanned line, N1/N1b/N2/N3 and the 229/275 baselines are intact
  (299/0/0), the lane README now states the true 400-character-preview bound, and no digest or new
  collision surface was added. The only remaining false sentences are in `docs/USER_GUIDE.md` (the
  docs lane's job per the task) and two evidence/UX nits, none in the repaired code.

## Assessment

The repair closes P5 for real, and it does so without an ABI change. The defect was that the per-hit
identity was the scanner's **trimmed 400-character preview**, so two lines identical in that prefix
were "the same line" to both guards. The repair adds a pre-save **whole-line** proof in the
dirty-buffer single-replace path: when the hit's preview is truncated and the hit file has a modified
buffer, `SearchPanel::replaceCurrentHit` reads the disk line at the hit number with
`readDocumentLine()` (the writer's decoder and split) — the per-file stamp checked just before proves
the disk still holds what the matcher read — and requires the buffer's line at that number to be
equal to it under `normalizedLineIdentity()` (whole line, BOM stripped and trimmed). If it is not,
the replace is refused **before anything is saved**, with a status-line reason naming the file. When
the bridge cannot expose the buffer text, a truncated preview is refused too ("no proof, no write");
for a short line the existing whole-line fingerprint (a ≤400-character line is the whole line) still
applies. The write-time guard keeps the preview fingerprint as its last line of defence, which leaves
only the already-disclosed external-rewrite bound (size and mtime preserved) — the round-4 review
placed that outside the gate sentence. I re-ran my own P5 probe with byte-identical source
(`sha256 = 449cfb89…`, same as `review-b2-r4/probes/probe_cap.cpp`): it now reports a refusal with
**0 saves, 0 replacements, the scanned line untouched** (`saves=0 ok=0`), and a derived UI probe
confirms the refusal is in `lastReplaceOutcome().error` and in the visible status label, the file on
disk is **byte-identical** to the scanned file, and the positive control (a dirty edit below the hit)
still replaces the scanned line and preserves the edit. The original 62-check reviewer probe is
0 failures (N1/N1b/N2/N3 green), and my own name diff against the round-4 log shows the **275/275**
baseline all present and PASS — and the original **229/229** baseline too — with 24 new checks and
`SUMMARY checks=299 failures=0 skips=0`. I read the new regressions: the cap detector is unit-pinned
at its boundaries, the duplicate regression asserts refuse/no-save/disk-unchanged/error-names-file
plus the tail-different variant, and the positive control asserts the scanned line is the one replaced
and the below-hit edit survives — no vacuous check, and the repair's red log shows exactly those five
checks failing with the fix hunk reverted. The disclosure was corrected in the lane README (it now
states the preview cap, the whole-line pre-save proof, the refusal for a preview-identical duplicate,
and the harmless fully-identical copy); the user manual still repeats the old wording and the old
no-bufferText fallback, which is a docs-lane carry I list rather than edit.

## 1. P5 is closed — my own probe, rebuilt against the repaired lane

Source discipline: `probes/probe.cpp` and `probes/probe_cap.cpp` in `review-b2-r5/` are byte-identical
copies of the round-4 probes (`sha256 75fd28e0…` and `449cfb89…`); only the build-script output paths
were changed. The probes were compiled in `vedit-qt6-build:local` against the current
`libvedit_workspace_search.a` (sources last changed 10:49, library 10:58, no source newer than the
library at run time).

```
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 \
  -e QT_QPA_PLATFORM=offscreen vedit-qt6-build:local bash -c \
  'bash evidence/ux-batch-2/review-b2-r5/probes/build-probe.sh &&
   ./evidence/ux-batch-2/review-b2-r5/probes/b2-r5-review-probe'
docker run … 'bash …/build-cap-probe.sh && ./…/b2-r5-cap-probe'
```

The identical P5 probe now refuses and leaves the disk alone (`logs/03-p5-cap-probe.log`):

```
PROBE P5.started=1 saves=0 ok=0 replacements=0 line0-right='XXXXXXTAIL_A' line1-equals-original=0
PROBE P5.no-wrong-target PASS (not reproduced)
PROBE SUMMARY failures=0
```

The derivation `probes/probe_cap_ui.cpp` (the same source plus one added case, `probeCapUi()`, and its
call — documented so the sha256 delta is explicit) proves the UI/byte-identity requirements
(`logs/04-p5-ui-probe.log`):

```
PROBE P5U.started=1 saves=0 ok=0 replacements=0 byte-identical=1 label-visible=1
      label='Replace refused: the unsaved edits in "dup-ui.v" changed where line 1 sits in the
      buffer, and that line is longer than the 400-character search preview, so the match cannot be
      re-identified from the preview alone; the document was not saved and nothing was replaced.
      Save it and run the search again.'
PROBE P5U.refused PASS / zero-replacements PASS / no-save PASS / disk-byte-identical PASS
      / error-non-empty PASS / error-names-file PASS / status-shows-reason PASS / status-visible PASS
PROBE P5U.ok started=1 saves=1 ok=1 line0-right='XXXXTAIL_A' line1='below edited'
PROBE P5U.ok.replaced PASS / scanned-line-replaced PASS / edit-preserved PASS
```

So the repair chose **refusal**, and the refusal is user-visible (the same text is in
`lastReplaceOutcome().error` and in `replaceStatusLabel()`, and the label is visible), happens with
**0 saves** (nothing touched before the decision), the disk is **byte-identical** (`readAll` compared
before/after), and the legitimate case still lands in the scanned line.

## 2. The cases that were already right still hold

- The original reviewer probe (62 checks + summary, `logs/02-reviewer-probe.log`) is
  `PROBE SUMMARY failures=0`: N1 (save during the scan) and N1b (save during the counting scan) refuse
  with the file byte-identical, N2 (Ctrl+F hand-off during the count) hands off and restores the
  controls, N3 (dirty-buffer single replace with a valid range) saves once and replaces.
- The lane suite, rebuilt from the repaired tree with a **narrow** single-target build
  (`cmake --build target/qt6-build --target vedit-search-replace-tests`, `logs/00-build-target.log`,
  exit 0 — no transient failure, no full gate), is `logs/01-suite-green.log`:
  `SUMMARY checks=299 failures=0 skips=0`.
- My own name diff (`logs/05-parity.txt`): the round-3 baseline log **229/229** names present and
  PASS, the round-4 baseline **275/275** present and PASS, **24 new names** (4 cap-detector, 11
  duplicate-refusal incl. the tail variant, 9 positive control). No baseline name was removed or
  renamed.

## 3. Disclosure wording — the lane is corrected; the list of remaining false sentences

The lane README (`src/workspace/search/README.md` §8 item 8) is corrected: it now says the
fingerprint is the **trimmed first 400 characters** of the preview, that a preview-identical line
inserted above the hit is **refused** (no save, no write) for an unsaved buffer, that a tail edit
after the cap on the hit line is refused, that only a **fully identical** copy remains indistinguishable
(and is harmless), and that an alternate bridge returning no buffer text **refuses** a truncated
preview. `SearchHit::lineFingerprint`'s comment in `search_types.h` is corrected the same way; the new
`search_panel.cpp` comment is accurate; `document_sync.{h,cpp}` carries no bound sentence. Remaining
false or stale sentences:

1. `docs/USER_GUIDE.md` §16.4 (docs lane — **not mine to edit**; the file is unchanged since 10:17):
   - “在扫描窗口之外插入与命中行**完全相同**的文本行（逐命中指纹只看那一行的内容，不知道行号偏移）”
     — now misleading: a preview-identical (only the first 400 characters equal) line is **refused**
     for an unsaved buffer; the remaining accepted copy is the fully identical one (harmless), and the
     parenthetical about the fingerprint is obsolete.
   - “非产品桥…未保存缓冲区的预保存校验退化为'先保存，再让写入端的逐行指纹拒绝'” — **false** for a
     truncated preview: such a bridge now refuses with no save and no write.
   - “这些边界之外…不会写错字符” — true but incomplete: it should name the whole-line pre-save proof
     for unsaved buffers (the repair's own README table, rows 4–6, has the required wording).
2. `evidence/ux-batch-2/b2-repair-r3/README.md` §6 items 1–2 — historical statements from the previous
   repair round: “only an external rewrite that leaves every hit line **byte-identical** … is
   indistinguishable” and the “**identical** line inserted above” paragraph are superseded by
   `b2-repair-r4/README.md` §6 and the lane README. Kept as the round-3 record; no one should cite them
   as the current bound. (Nit; history, not live documentation.)
3. `src/workspace/search/README.md` §8, the “扫描之后” bullet: “且命中行文本完全相同” — the write-time
   guard is still the 400-character preview, so the true condition is “identical in the first 400
   trimmed characters”, not the whole line. The bullet above it explains the cap, so a reader gets the
   full picture; the sentence itself is still stronger than the code. (Nit.)
4. `src/workspace/search/README.md` §8, the “浮条文案” bullet: the Ctrl+F bar's transient text still
   says “Whole project: searching in the Search panel” when the hand-off was refused — user-visible and
   false in that state; it is in `src/editor/kernel/find_replace.cpp` and is a pre-existing round-4
   Nit, not introduced here. (Nit; also note the bullet says “不在本 lane 写作用域”, while the frozen
   plan's B2 write scope does include `src/editor/kernel/find_replace.*` — this repair's own T27 scope
   did not.)

## 4. No new hole was opened

- **No digest was added.** The fix is a direct string equality on the whole line
  (`normalizedLineIdentity(bufferLine) == normalizedLineIdentity(scannedLine)` in
  `SearchPanel::replaceCurrentHit`), so there is no hash-collision or truncated-digest surface at all.
  `normalizedLineText()` strips one leading U+FEFF and trims — a normalisation, not a cap: the tail
  after column 400 is inside the compared string and can only be hidden if it is trailing whitespace
  (harmless; see declined).
- **The cap detector is exact and unit-pinned.** `matchedLinePreviewTruncated()` tests 401 scalar
  values ending in U+2026 (what `snippet_of` produces) and is pinned by
  `preview-cap.detector-truncated` / `-at-cap` / `-short-ellipsis` / `-empty`; a legitimate line ending
  in '…' that is not capped is ≤400 values, so the two cases cannot merge.
- **The disk line is read like the writer.** `readDocumentLine()` opens the file, refuses NUL and
  invalid UTF-8 (a file the writer would never rewrite), decodes with
  `Stateless | ConvertInitialBom` and splits on '\n' exactly like `applyReplaceRanges`; a missing line
  returns a null `QString`, which the panel treats as “cannot prove” and refuses. Bounds/line-count
  errors therefore refuse rather than compare.
- **Empty buffer text cannot save the hole.** With no `bufferText()`, a truncated preview has no proof
  and is refused (`sameLine` requires non-empty buffer text and a non-null disk line); a short line
  keeps the whole-line fingerprint, which *is* the whole line at ≤400 characters.
- **Path coverage.** The pre-save proof is in the dirty-buffer single-replace branch only; Replace All
  saves the affected buffers and then **re-scans** (`finishInitialReplaceScan` →
  `beginScan(…, PostSave)`), so its hits describe the saved content and a stale coordinate cannot be
  applied. A clean file (no dirty buffer) keeps the stamp + preview-fingerprint guard, whose remaining
  exposure is the disclosed external-rewrite bound.
- **The new tests are real and red-before.** The repair's `logs/01-red-before.log` shows the five
  `preview-cap.*` checks failing on the pre-fix code (refused/no-save/disk-unchanged/error-names-file/
  tail-disk-unchanged, `ok=1 replacements=1` and a `BAR …` file); the green log and my own run show
  them passing. No check is a literal `true`, and the 24 new names are in the 299-check suite.

## Findings (ranked)

### Medium — the user-facing manual still describes the pre-repair behaviour (docs lane, must clear before batch acceptance)

**Where:** `docs/USER_GUIDE.md` §16.4 (the three sentences quoted in §3 item 1). **Why it matters:** a
reader is told the remaining bound is “an identical line inserted above a hit” and that a no-bufferText
bridge falls back to save-then-refuse; both are now wrong (preview-identical inserts are refused
without saving; the no-bufferText case refuses a truncated preview without saving). The repair's own
README table rows 4–6 already spells out the required replacement text. **Direction:** the docs lane
adopts those three corrections (it also owns §12.3's refusal list, which should name the whole-line
pre-save proof). **Not a defect of the repaired code**, and not fixed here by instruction.

### Nit — the lane README's external-rewrite bullet still says “命中行文本完全相同”

**Where:** `src/workspace/search/README.md` §8 “扫描之后” bullet. The write-time identity remains the
400-character preview, so the true condition is “identical in the first 400 trimmed characters”; the
bullet above explains the cap, but this sentence is stronger than the code. **Direction:** say
“前 400 个字符相同（含整行相等）” or reference the bullet above.

### Nit — the round-3 evidence README keeps the superseded bound statements

**Where:** `evidence/ux-batch-2/b2-repair-r3/README.md` §6 items 1–2 (“byte-identical” / “identical
line inserted above”). Historical record; the current bound is in `b2-repair-r4/README.md` §6 and the
lane README. **Direction:** a one-line “superseded by b2-repair-r4” note if the evidence owner wants
the record to stay self-consistent; do not cite it as the current bound.

### Nit — the Ctrl+F bar's transient refusal text is still false (pre-existing)

**Where:** `src/editor/kernel/find_replace.cpp` (the bar's status text after a refused whole-project
hand-off). The panel's refusal is correct and visible; the bar still claims a search is in progress.
Pre-existing round-4 Nit; this repair's task scope did not include the file (the plan's B2 lane scope
does). **Direction:** show the refusal reason in the bar (or do not claim the search started).

## Declined to judge

- **The same-millisecond write at scan start on a clean file with a truncated preview** — pre-existing
  and disclosed in round 4 (§6.3); the preview-fingerprint backstop is weaker for a truncated preview,
  but the window is 1 ms and the case is outside “a file the user just saved” (a clean file is written
  by someone else). Unchanged by this repair; not re-raised.
- **The pre-existing read→save window** (the file can change between the stamp check / disk-line read
  and the panel's save-first write) — disclosed by the repair, previously declined by round 4.
- **The conservative refusal of a tail edit on the hit line** (beyond column 400) — the replacement
  would be safe (the token is still at its column), but the repair refuses before saving; safe
  direction, disclosed in the lane README's “逐命中指纹只覆盖预览” bullet, and a save + re-search
  restores the operation.
- **The trim-equal trailing-whitespace copy** — the whole-line identity trims, so a copy differing only
  in trailing whitespace is accepted; the replacement attaches to a line whose visible text is the
  same, and the invisible whitespace is preserved either way. No wrong characters in the text.
- **The leading-whitespace reindent that passes the trimmed identity and then fails the range check
  after the save** — pre-existing (the fingerprint was trim-based before the repair too); the user's
  edit is saved, nothing is replaced, and the operation reports the refusal. No wrong characters.
- **The real KTextEditor bridge path** — the tests/probes use an `OpenDocumentBridge` double; the
  product bridge returns `document->text()` and saves it, so the assumptions hold by construction; the
  live-editor path was not re-exercised (the repair discloses this, and round 4 declined the same).
- **The full gate / `build-qt.sh`** — per the shared-build instruction the captain runs the single
  gate; this review rebuilt only the lane target and the probes.
- **The other lanes' work in the live shared tree** — out of scope.

## Bounds of this review

- **Read:** the frozen plan §0/§B2/§5.1; the round-4 report; `b2-repair-r4/README.md` and its logs
  (`01` red, `02` green, `03` probe, `04` `-Werror` — the last read, not re-run, since it needs the
  exact compile commands); the changed sources `src/workspace/search/{search_types.h,.cpp,
  project_replace.h,.cpp, search_panel.cpp, README.md, search_replace_selftest.cpp}` (the new
  functions, the pre-save branch, the replace-all save/re-scan flow, the new regressions); the current
  `docs/USER_GUIDE.md` §12.3/§16.4; the round-3 evidence README §6; `document_sync.{h,cpp}` comments.
- **Ran (pinned image `vedit-qt6-build:local`, offscreen, shared build tree):** the byte-identical
  reviewer probe (0 failures); the byte-identical cap probe (P5 refuses, 0 saves); the derived UI probe
  (refusal reason in the outcome + status label, label visible, disk byte-identical, positive control
  replaces the scanned line); the lane test target rebuilt with a narrow single-target
  `cmake --build` (exit 0); the lane suite (299/0/0); my own 229/275 name diff; `lrelease` was not
  re-run this round (the repair did not touch translations; the batch acceptance-r2 checklist covers
  it).
- **Did not run:** the full gate (`build/gates/qt6-cxx.sh` / `scripts/gates.sh`), `bash
  scripts/build-qt.sh`, any product relink, a real xcb GUI session, the live KTextEditor bridge path,
  an external same-size/same-mtime rewrite, the null-bridge branch. **Build-tree note:** the library
  was rebuilt by another lane at 10:58 with no source newer than 10:49; my target rebuild and probe
  links were against that library, and no build failure was transient this round.

## Verdict

**PASS.** The P5 wrong-target write is closed by a pre-save whole-line proof: the dirty-buffer single
replace now refuses (before any save) with a visible reason when the buffer's line is not the whole line
the scan read, the disk stays byte-identical, and the legitimate case still replaces the scanned line;
the 229/275 baselines are intact at 299/0/0, the earlier N1/N1b/N2/N3 guarantees hold, and the fix
adds no digest or truncation surface. The lane README now states the true bound (“identical in the
first 400 characters”); the remaining false sentences are in the user guide (docs lane — Medium carry,
listed with the required wording), the round-3 evidence record and the pre-existing Ctrl+F bar text
(Nits). The batch acceptance can count B2 as repaired once the docs-lane sentences land.
