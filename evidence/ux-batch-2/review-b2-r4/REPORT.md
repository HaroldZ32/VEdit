# Review — UX batch 2, lane B2, FOURTH pass (terminal verdict on repair `b2-repair-r3`)

- Frozen contract: `.mpd/plans/ux-batch-2/00-plan.md` §`B2` (acceptance ①–⑥, the unsaved-buffer sentence,
  the RE2-vs-Ctrl+F documentation sentence) and §0 row 6 ("两个按钮")
- Handover instruction: `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U7
- Previous verdict: `evidence/ux-batch-2/review-b2-r3/REPORT.md` (FAIL: the stamp baseline was captured at
  scan finish; the Ctrl+F hand-off during the counting scan stranded the panel; a dirty-buffer single
  「替换」 saved and then refused)
- Repair under review: `evidence/ux-batch-2/b2-repair-r3/` (README + red 271/16, green 275/0/0, probe
  rebuild 0 failures, gate 50/50)
- Review date: 2026-11-06
- Reviewer: independent; findings only — no product source, test, gate or repair artefact was edited.
  The probes in `evidence/ux-batch-2/review-b2-r4/probes/` are reviewer-only and outside the product build.
- **Risk level: MEDIUM** (the three defect classes that failed the previous three passes are genuinely
  closed; one narrow but real wrong-target write remains, reachable only through the 400-character
  per-hit identity, and the lane's own disclosure describes that bound as harmless)
- **Verdict: FAIL** — the gate sentence ("Replace All / 替换 must not write wrong characters into a file
  the user just saved") still answers **yes** for a preview-identical duplicate inserted above a hit
  inside a dirty buffer (probe P5 below); the Lead's rule for this terminal verdict is that any residual
  that can corrupt a just-saved file fails the lane regardless of how narrow it is.

## Assessment

The repair is real and it closes the three defects I failed the third pass on. The scan window is now
frozen from the scan **start** (`SearchPanel::beginScan` stamps `activeScanStartedMs_` before
`GlobalSearch::start`; `SearchPanel::makeFrozenScan` writes `FrozenScan::scanStartedMs` and every hit file
whose mtime is strictly later is flagged `writtenDuringScan`; `SearchPanel::onSearchFinished` refuses a
counted Replace All before the save-first step and the confirmation, and `SearchPanel::replaceCurrentHit`
refuses a single hit of a flagged file). Every hit also carries a SHA-256 of the matched line the matcher
read (`GlobalSearch::runScan` → `SearchHit::lineFingerprint`; `ProjectReplacer::applyReplaceRanges`
recomputes it from the current line and refuses before a character is rebuilt). The Ctrl+F hand-off and the
dock's own Search path now answer `SearchPanel::operationBusy()` with a status line instead of taking the
panel over, and the panel recovers at the operation's terminal state. The dirty-buffer single 「替换」
checks the disk stamp **and** the buffer's hit-line fingerprint before the save-first write, then re-captures
the stamp it itself wrote, so the old false refusal with the document-blaming message is gone. I rebuilt the
reviewer's probe unchanged from source (sha256 identical to `review-b2-r3/probes/probe.cpp`), ran it in the
pinned image against the current lane library, and got `PROBE SUMMARY failures=0` with N1/N1b/N2/N3 all
PASS — the exact four failures the third pass recorded now refuse with the correct reason and leave the file
byte-identical to the user's save. The 229-check baseline is intact: I re-ran the lane suite and did my own
name diff against the third pass's log — 275 checks, 0 failures, all 229 baseline names present and PASS,
46 new names added. The user's binding rulings are honoured: the dock has two buttons (`search-replace` /
`search-replace-all`, `SearchPanel` replace row), both surfaces carry the same four-option model
(`src/workspace/search/search_query.h::QueryOptions` + `SearchHub`, asserted by `model.dock.*` and
`model.bar.*`), and the RE2-vs-PCRE difference is disclosed (dock tooltip in `SearchPanel`'s regex button,
lane README §8 item 4, and — as of 2026-11-06 10:06 — `docs/USER_GUIDE.md` §12.4/§16.3, which I verified in
the current tree).

What still fails is narrower and it is the same data-safety class as before: the per-hit identity is not
the line, it is the **trimmed first 400 characters** of the line (`search_types.cpp::matchedLineFingerprint`,
`kSnippetChars = 400`, mirroring `crates/vd-search::snippet_of` / `TREE_SNIPPET_CHARS`). Two lines that share
that 400-character prefix but differ later are therefore "the same line" to every guard. If a dirty buffer
inserts such a near-duplicate above the hit, the pre-save buffer check accepts it, the panel saves the buffer
and re-captures the stamp (its own write, so the stamp cannot object), the writer's line fingerprint accepts
and the range check passes because the same token sits at the same column — and 「替换」 writes the
replacement into the inserted line while the scanned line moves down untouched, reporting success. I
reproduced exactly that with a reviewer-only probe (`probeFingerprintCap`): `ok=1 replacements=1`, the file
after the operation has the inserted line `BAR … TAIL_B` and the scanned line still `foo … TAIL_A`. The
repair README §6.2 and the lane README §8 item 8 ("重复行") both describe this residual as an **identical**
line — for a fully identical line the resulting file is the same either way, so the disclosed bound is
indeed harmless — but the code's equivalence class is the 400-character preview, and for a preview-identical
line with a different tail the file does get the wrong characters on the wrong line. The user guide §16.4 now
repeats the same "完全相同" wording, so a reader is told this bound is safe when the implemented one is not.
That gap is not disclosed anywhere and no test covers it. Everything else in the repair holds; the terminal
verdict is FAIL on this one path, per the gate sentence and the Lead's no-narrowness-exception rule.

## The three required checks

### 1. The reviewer's probe, re-run verbatim — 0 failures (matches the repair's claim)

Source identity: `sha256(evidence/ux-batch-2/review-b2-r4/probes/probe.cpp)` =
`75fd28e065bbe56a48d8a7804e2a3b71c6b5245e8c05f546b64652b24c1da8c2` =
`sha256(evidence/ux-batch-2/review-b2-r3/probes/probe.cpp)`. The build script is the third-pass script with
only the output paths changed (same compiler, defines, includes and link line).

Command (pinned image, offscreen):

```
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 \
  -e QT_QPA_PLATFORM=offscreen vedit-qt6-build:local bash -c \
  'bash evidence/ux-batch-2/review-b2-r4/probes/build-probe.sh &&
   ./evidence/ux-batch-2/review-b2-r4/probes/b2-r4-review-probe'
```

Observed (raw: `logs/02-probe-run.log`, exit 0): 62 `PROBE` check lines + summary, **0 FAIL**, and

```
PROBE SUMMARY failures=0
```

The four failures the third pass recorded now pass, with the file byte-identical to the user's save:

```
PROBE N1.after='abc foo\nxyz foo\n' started=1 ok=0 replacements=0
      status='Replace refused: "freeze.v" was written while the search was running, so this match may
      have moved; nothing was written. Run the search again.'
PROBE N1.no-wrong-occurrence PASS
PROBE N1b.after='foo c\nfoo a\nfoo b\n' confirmed=0 ok=0 replacements=0
      status='Replace All refused: "freeze-all.v" was written while the counting scan was running, …'
PROBE N1b.no-wrong-occurrence PASS
PROBE N2.handed=1 idle=1 queryEnabled=1 replaceAllEnabled=1 replaceEditEnabled=1
      outcomeOk=0 error='Replace All cancelled; nothing was written.'
PROBE N3.after='BAR\nabc foo edited\n' started=1 saves=1 ok=1 error=''
```

The probe therefore confirms the scan-window, hand-off and dirty-buffer repairs; it does not test the
400-character identity (see finding 1), which the repair's own suite does not test either.

### 2. The 229-check baseline — present and PASS (my own diff, not the writer's parity file)

Command (same image, against the current lane test binary; no source file under `src/` is newer than the
binary, so it is the current tree):

```
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 \
  -e QT_QPA_PLATFORM=offscreen vedit-qt6-build:local bash -c \
  'target/qt6-build/src/app/workspace-lane-search/vedit-search-replace-tests --selfcheck \
     --evidence /tmp/b2-r4-suite-evidence'
```

Observed (raw: `logs/01-suite-green.log`, exit 0): `SUMMARY checks=275 failures=0 skips=0` (275 `CHECK`
lines, 0 non-PASS). My own name diff against `review-b2-r3/probes/01-229-suite.log`
(`logs/04-parity.txt`): baseline 229 names, **229/229 present and PASS**, none missing, 46 new names
(the repair's 42 behavioural + 4 fingerprint-unit checks). Three-run stability is the repair's claim
(`logs/02-green-suite.log`); I ran once, which is what a terminal spot-check needs.

### 3. The three disclosed residual bounds against the gate sentence — bound 2 fails

| # | Disclosed bound (repair README §6 / lane README §8 / guide §16.4) | Verdict |
|---|---|---|
| 1 | External rewrite that preserves size **and** mtime, with the SHA-256 collision surface | **Outside** the gate sentence for the product path. A user's own save moves mtime (the per-file stamp refuses) unless the save lands in the same millisecond as the scan start, and that sub-millisecond residual is backstopped by the per-hit fingerprint (a change inside the 400-character prefix is refused) — and a change beyond it still writes the intended token at the same coordinates. A true SHA-256 collision is cryptographically out of reach. Caveat: the "hash" covers only the trimmed 400-character prefix, so the *practical* collision surface is wider than a digest collision — see finding 1/2 (this is the same cap, not a second defect). |
| 2 | An identical text line inserted above a hit outside the scan window | **NOT outside** as implemented. A *fully* identical line is harmless (the resulting file is the same whichever copy is replaced), but the code's identity is the trimmed 400-character preview, and a preview-identical line with a different tail is accepted and corrupted — probe P5, finding 1. The disclosure's word "identical" is narrower than the code. |
| 3 | A non-product bridge with no `bufferText()` | **Outside** the product surface. The product bridge (`src/workspace/search/document_sync.cpp::bufferText`) always returns the open document's text; only a test/alternate bridge can return empty, and that fallback still saves first and lets the writer's per-hit fingerprint refuse — no wrong characters on that path. |

Probe P5 (`logs/03-cap-probe.log`; source `probes/probe_cap.cpp::probeFingerprintCap`, a copy of the
reviewer probe plus this one case, built against the same lane library with `probes/build-cap-probe.sh`):

```
# scanned file: one 510-character line  "foo " + 500×'X' + "TAIL_A"   (hit at 1:1)
# the user's dirty buffer pastes above it: "foo " + 500×'X' + "TAIL_B"  (differs only after column 400)
PROBE P5.search PASS
PROBE P5.hit PASS (1)
PROBE P5.idle PASS
PROBE P5.started=1 saves=1 ok=1 replacements=1 line0-right='XXXXXXTAIL_B' line1-equals-original=1
PROBE P5.no-wrong-target FAIL (BAR was written into the inserted near-duplicate, the scanned line is
      untouched)
PROBE SUMMARY failures=1
```

So 「替换」 saved the dirty buffer (`saves=1`), wrote `BAR` into the inserted line, left the scanned line
untouched, and reported success — the wrong-target class on a file the operation itself just saved.

## The user's binding rulings

| Ruling (`00-plan.md` §0 row 5/6, §B2) | Status |
|---|---|
| Replace is **two** buttons (替换 / 全部替换), not one | **Honoured.** `SearchPanel`'s replace row builds `replaceButton_` (`search-replace`, tooltip "Replace the current match and advance") and `replaceAllButton_` (`search-replace-all`, tooltip "Replace every match (asks for confirmation with the file and hit counts first)"), wired to `replaceCurrentHit()` / `replaceAllRequested()`. The shared catalogue now has 替换 / 全部替换 for both the panel and the bar (`translations/vedit_zh_CN.ts`; `lrelease` 1186/1186 finished, 0 unfinished at review time). |
| The search panel shares its options with Ctrl+F | **Honoured.** One header-only model `src/workspace/search/search_query.h::QueryOptions` (case / whole word / regex / scope) and `SearchHub`; the dock's `search-case`/`search-whole`/`search-regex`/scope combo and the bar's `vedit-find-case`/`vedit-find-whole`/`vedit-find-regex`/`vedit-find-scope` are asserted by `model.dock.widgets.*`, `model.dock.options.round-trip`, `model.bar.widgets.*` in the 275-check suite, and both directions of the hand-off are in the same suite (`handoff.*`, `model.bar.options`). |
| Project-wide regex semantics (RE2-style) are disclosed rather than silently different from Ctrl+F | **Honoured as of review time.** The dock's regex tooltip says "project-wide: RE2 style, no backreferences…"; `src/workspace/search/README.md` §8 item 4 states the difference; `docs/USER_GUIDE.md` §12.4 and §16.3 now state it in user terms (landed 2026-11-06 10:06; I read the current file). Before that landing the guide had no search section — if the final acceptance is taken from an earlier tree state, re-check this item. |

## Findings (ranked)

### High — the per-hit identity is the trimmed 400-character preview, so a preview-identical duplicate inserted above a hit is replaced instead of the scanned line

**Where:** `src/workspace/search/search_types.cpp::matchedLineFingerprint` (hashes `trimmed` capped at
`kSnippetChars = 400` + U+2026), fed from `src/workspace/search/global_search.cpp::GlobalSearch::runScan`
(`hit.lineFingerprint = matchedLineFingerprint(item.preview)`, and `item.preview` is
`crates/vd-search/src/lib.rs::snippet_of`, which is itself the trimmed 400-character prefix); consumed by
`src/workspace/search/search_panel.cpp::replaceCurrentHit` (the pre-save check of the buffer's hit line) and
by `src/workspace/search/project_replace.cpp::applyReplaceRanges` (the write-time check of the current
line). The range matcher (`RangeMatcher::matchesAt`) only proves the query token is at the old column, so it
cannot distinguish two lines whose shared prefix contains the token.

**What happens:** a 400+-character line is the hit. The user's dirty buffer pastes above it a line that is
identical for the first 400 trimmed characters and differs after (the natural "copy the long line, edit the
tail" edit). `SearchPanel::replaceCurrentHit` compares the buffer's line at the old hit line number — now the
inserted line — with the hit's preview fingerprint; they are equal, so the save-first step runs. The panel
re-captures the stamp it wrote itself, the worker's stamp check passes, `applyReplaceRanges` recomputes the
same 400-character fingerprint, the token is at the same column, and the replacement is written into the
inserted line. The scanned line (now line+1) is untouched. The outcome reports success.

**Evidence:** `evidence/ux-batch-2/review-b2-r4/logs/03-cap-probe.log` (`PROBE P5.*`); probe source
`evidence/ux-batch-2/review-b2-r4/probes/probe_cap.cpp::probeFingerprintCap`, built by
`probes/build-cap-probe.sh` against the reviewed `libvedit_workspace_search.a`. No check in
`src/workspace/search/search_replace_selftest.cpp` covers a preview-identical duplicate; the existing
`frozen-shift.*` / `dirty-*` checks use lines that differ inside the 400-character prefix, which the guard
does catch.

**Why it matters:** this is the C4/A3 wrong-target class — the replacement is written to a different
occurrence of the same query on a file the operation just saved — with a success status, and the lane's own
disclosure (`b2-repair-r3/README.md` §6.2, `src/workspace/search/README.md` §8, `docs/USER_GUIDE.md` §16.4)
tells the user this bound is harmless because the line is "identical". It is harmless only for a fully
identical line; the implemented identity is the preview. The gate sentence answers yes.

**Direction of a fix (described, not applied):** make the per-hit identity cover the whole line. The Rust
`TreeHit` already carries the preview only, so the clean fix is to carry a full-line digest (or a
`line_length + digest_of_tail` pair) from `crates/vd-search` in the C++ `SearchHit`/`ReplaceHit` and compare
it in both guards; a whole-file digest captured at scan time and compared at write time is the stronger
variant. If an ABI change is out of scope for this batch, the conservative fallback is to refuse a
single-replace on a dirty buffer when the hit line's preview is at least `TREE_SNIPPET_CHARS` characters
(the identity is not known to be complete), and to refuse a Replace All in the same situation — safe, at the
cost of asking the user to re-run the search. Add a regression test that inserts a preview-identical line
above a >400-character hit in the dirty buffer and asserts both buttons refuse (or replace the scanned line).
Whichever way it is fixed, the disclosure in the three READMEs/guide must say "identical 400-character
preview", not "identical line".

### Medium — the disclosure overstates the identity in three places and one of them is user-facing

**Where:** `docs/USER_GUIDE.md` §16.4 ("在扫描窗口之外插入与命中行**完全相同**的文本行 … 不会写错字符"),
`src/workspace/search/README.md` §8 item 8 ("其文本与命中行完全相同"), and
`evidence/ux-batch-2/b2-repair-r3/README.md` §6.2 ("a line identical to the hit line") / §6.1 ("every hit line
byte-identical"). **Why it matters:** the repair's §1 does disclose the 400-character normalizer, so the
information exists, but the bound's conclusion is wrong for preview-identical lines and the user guide repeats
it. **Direction:** state the cap in all three places and remove the "no wrong characters outside these
bounds" sentence until finding 1 is fixed.

### Nit — the Ctrl+F bar's transient wording still claims a whole-project search when the hand-off was refused

**Where:** `src/editor/kernel/find_replace.cpp` (the bar's status text after a whole-project hand-off; the
panel's refusal is correct, the bar's one-line text is not). The repair disclosed this (`README` §6.5) but
also called `find_replace.cpp` "outside this lane's write scope", which is inaccurate — the frozen plan's B2
write scope includes `src/editor/kernel/find_replace.*`. **Direction:** show the hub/panel refusal reason
in the bar, or at least do not claim "searching in the Search panel" when the panel refused.

### Declined to judge

- **The narrow read→write race inside the worker** (the before/after read stamp check around the actual
  `rewriter` call): unchanged from the previous two passes, the window is a single file read and the
  fingerprint is the backstop; no new evidence changes it.
- **The same-millisecond write at scan start** (`captureFileStamp` uses `mtime > scanStartedMs` and
  `mtime <= now`): disclosed by the repair, backstopped by the per-hit fingerprint for changes inside the
  400-character prefix; a same-ms change beyond it writes the same token at the same coordinates. Not a new
  finding.
- **The 15 s real-project timeout / the 50 ms-budget suite proxy**: I did not run a real 15 s project; the
  suite's proxy and the repair's logs are what exist.
- **The GUI-thread stat burst in `makeFrozenScan`** (one `QFileInfo` per hit file at scan finish): a
  performance bound, not a correctness one; unchanged from the third pass.
- **The hub handler keyed by `const void *` and the shell-lifetime fallback**: pre-existing seam, not this
  repair's.
- **The null-bridge branch of `finishInitialReplaceScan`**: not reachable in the root build, which always
  links the product bridge; unchanged.
- **The AppImage / packaging / the other lanes / the full 7 gates**: not this lane; the batch acceptance
  review covers the batch-level state.
- **A2/A4's user-guide wording** (`docs/USER_GUIDE.md` was being rewritten by another agent during this
  review; I verified only §12/§16's search content, at 10:06).

## Bounds of this review

- **Read:** the frozen plan §0/§B2/§5.1; the handover §3.U7 and §1; `b2-repair-r3/README.md` and its logs
  (`01`, `02`, `04` — `03-build-qt.log` quoted, not re-run); the three previous reviews; the repair's own
  probe source and build script; `src/workspace/search/{search_panel.h/.cpp, project_replace.h/.cpp,
  search_types.h/.cpp, global_search.cpp, document_sync.h/.cpp, search_query.h, README.md,
  search_replace_selftest.cpp}` (the fingerprint, stamp, save-first, refusal and control-lock paths);
  `src/editor/kernel/find_replace.h` (the bar's option/scope widgets); `crates/vd-search/src/lib.rs`
  (`snippet_of`, `TREE_SNIPPET_CHARS`, the tree-hit preview); the just-landed
  `docs/USER_GUIDE.md` §12/§16.4; `translations/vedit_zh_CN.ts` (the two replace labels) and a fresh
  `lrelease` run.
- **Ran (in `vedit-qt6-build:local`, offscreen, against the current lane library):** the reviewer's probe
  rebuilt from the byte-identical source (0 failures); the lane suite (275/0/0); my own 229-name diff
  (229/229); the reviewer-only P5 cap probe (reproduces the wrong-target write); `lrelease` on the current
  catalogue (1186 finished / 0 unfinished, exit 0).
- **Did not run:** `bash scripts/build-qt.sh` (the repair's `logs/03-build-qt.log` 50/50 + xcb smoke is
  quoted, not re-run — the shared build tree is in use by other lanes); a user-driven xcb session (the
  programmatic injection is the documented backstop and the widgets are disabled in the product); a real
  15 s project; a preserved-mtime external rewrite; the null-bridge branch; the AppImage; the other lanes.
- **Only read (no run):** the full 1300-line `find_replace.cpp` outside the hand-off paths, the rest of the
  editor kernel, and the other lanes' implementations.

## Verdict

**FAIL** — the three defects that failed the previous three passes are genuinely fixed (the reviewer's
probe is 0 failures: a save during the scan, a hand-off during the count and a dirty-buffer save-first all
refuse and leave the file byte-identical), the 229-check baseline is intact at 275/0/0, and the user's two
buttons, shared options and RE2 disclosure are honoured. The remaining wrong-target write is narrow (a
400+-character line, a preview-identical duplicate inserted above it in a dirty buffer, the single 「替换」)
but it is the same data-safety class the lane exists to remove, it reports success, and the lane's own
disclosure describes that bound as harmless. Under the gate sentence and the Lead's no-narrowness-exception
rule, the lane cannot be signed off.

## Evidence files added by this pass

- `logs/01-suite-green.log` — the lane suite, 275 checks, 0 failures (command in §2).
- `logs/02-probe-run.log` — the reviewer probe rebuilt verbatim, 0 failures.
- `logs/03-cap-probe.log` — the P5 cap probe, 1 failure (the finding).
- `logs/04-parity.txt` — my own 229-name diff.
- `logs/base229.txt`, `logs/current275.txt`, `logs/base.names`, `logs/cur.names` — the diff inputs.
- `logs/vedit_zh_CN.qm` — the fresh lrelease output (not shipped; evidence only).
- `probes/probe.cpp` (sha256 identical to `review-b2-r3/probes/probe.cpp`), `probes/build-probe.sh`,
  `probes/probe_cap.cpp`, `probes/build-cap-probe.sh`, `probes/b2-r4-review-probe`,
  `probes/b2-r4-cap-probe` — reviewer-only; the product build does not consume them.
