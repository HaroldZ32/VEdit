# Verdict — fix-evidence-c9 (T44, independent review of T43)

captured: 2026-10-31T10:27Z
git_head: 9957f5fe4d63c17c681e8753288262fa2c00955e
branch: feat/qt6-ktexteditor-migration
reviewer: Reviewer (instance 2), session b0af789c-16ef-42ef-ad05-9e137a41d604
loop: `loop-20261031T101645-727109`
work contract: `.mpd/cellwork/cell-20261031T085543161Z/contracts/fix-evidence-c9.md`
review contract: `.mpd/cellwork/cell-20261031T085543161Z/contracts/rev-fix-evidence-c9.md`

## Verdict: PASS (bound)

Recorded from the frozen contract, the requirements document, the reported
report (treated as a claim), and the git/diff/gate evidence below. The writer's
REPORT was never accepted as proof: every substantive claim in it was re-derived
from `git show HEAD:…`, `git diff`, byte comparison, and the gate run.

## Documents the verdict rests on

- `.mpd/cellwork/cell-20261031T085543161Z/requirements.md` — **C9** (evidence
  header: capture time + `git rev-parse HEAD` + `git status --porcelain`);
  **C10** (never modify an existing test/gate); C8 (L3 = another agent's verdict).
- `.mpd/cellwork/cell-20261031T085543161Z/contracts/fix-evidence-c9.md` —
  deliverable (five lanes' C9 headers, copy the existing format, honest backfill
  annotation, evidence content unchanged, no test/gate edits), allowed write
  scopes (the five lane directories).
- `.mpd/cellwork/cell-20261031T085543161Z/contracts/rev-fix-evidence-c9.md` —
  this review's deliverable and verification protocol.
- `AGENT.md` §4 (declared gates), §5 (re-run checks; never edit a gate), §6 (git
  discipline; captain as single git writer).
- `evidence/fixture/README.md` + its files and `evidence/docs-amendments/00-context.txt`,
  `evidence/appimage/source-tree.txt` — the pre-existing C9 header formats the
  backfill was told to follow. Verified present: `collected_at:`/`git_head:`/
  `git_status_porcelain:` (docs-amendments), `git_head=`/`git_status_porcelain:`
  (appimage), `generated:`/`git_head:`/`git_status_porcelain:` (fixture).
- `evidence/ws-filetree/fix-evidence-c9-REPORT.md` — the writer's report (claims,
  re-checked below).

## Evidence produced by the reviewer (commands + observed results)

| # | check | observed |
| --- | --- | --- |
| 1 | `git diff --numstat -- <five dirs>` | **63 files, 6048 insertions, 0 deletions**; every row is `96 0` → pure prefix, nothing replaced or removed |
| 2 | body preservation: for each of the 63 files, `cmp <(git show HEAD:$f) <(tail -n +97 $f)` | **all 63 bodies byte-identical to HEAD** — evidence content unchanged (C9 requirement) |
| 3 | header shape, all 63 files | first line `collected_at: unknown (header added retroactively at 2026-10-31T10:19:00Z)`, then `git_head: 9957f5f…` (= current HEAD), `git_status_porcelain:`, 91 status lines, a `backfill: 补记 …` line, `---`; header block = 96 lines in every file (63/63) |
| 4 | porcelain snapshot equality | the 91-line status block is **identical in all 63 files** (byte compare) |
| 5 | snapshot plausibility | snapshot lines are a strict subset of the current `git status --porcelain`; the 5 current-only lines are changes made after the backfill (`src/workspace/filetree/tools/rootcheck/**`, `evidence/fix-filetree-open/`, `evidence/highlight-v2/`, the report itself) — no snapshot line is absent from the current status ⇒ consistent with a genuine snapshot taken at backfill time |
| 6 | pre-backfill state: `git show HEAD:$f \| head -15` for all 63 | **none** of the 63 artifacts carried `git_head:`, `generated:` or `collected_at:` at HEAD ⇒ the "all five lanes lacked the C9 header" premise is true |
| 7 | coverage: 89 tracked files in the five dirs minus 63 modified | unmodified = 23 `.png` + 2 `.sh` + 1 `.json`; **every `.txt`/`.log`/`.md` (63/63) got the header**; no text artifact was missed |
| 8 | report tables vs files: parse the 63 rows of REPORT.md §5 and the `backfill:` mtime cell of each file | all 63 mtime cells match the header's recorded mtime; per-lane counts 13+19+10+10+11 = **63** |
| 9 | backfill marker outside the five dirs: `grep -rl 'header added retroactively at 2026-10-31T10:19:00Z'` over the repo | **no hit outside** the five lane directories ⇒ the batch did not leak into other lanes |
| 10 | existing tests/gates: `git status --porcelain` filter `tests/`, `build/gates/`, `scripts/gates.sh` | **none modified** ⇒ C10 holds |
| 11 | `mpd_verify_evidence {kind:"gate", gate:"docs", loop_id:"loop-20261031T101645-727109"}` | exit **0**; log `.mpd/verify/evidence/ev-20261031T102716-0e5baf.log` (declared repo gate `verify:docs`) |
| 12 | evidence/verdicts untouched: mtimes + first lines | reviewer verdict files predate the backfill and carry no backfill header (= not touched) |

## Honesty of the backfill (the hard point)

- `collected_at: unknown (…)` — the original capture time is **not** asserted.
  No original timestamp was invented; the file mtime is recorded on the
  `backfill:` line and explicitly labelled `仅供参考`.
- `git_head` / `git_status_porcelain` are stated in every file to be the
  **backfill-time snapshot, not the original capture time**; check 5 shows the
  snapshot really is a coherent backfill-time snapshot.
- The report's §8 records both correction passes (first pass wrongly used
  `generated: <mtime>`; rewritten after the captain ruled; and a markdown
  separator mishap in `ws-filetree/REPORT.md` restored byte-for-byte from HEAD).
  Check 2 confirms that file's body is intact now. This kind of self-correction
  log is exactly what an honest backfill looks like.
- The email record `.mpd/cellwork/cell-20261031T085543161Z/mail.jsonl` contains
  the captain's ruling quoted in the report (report-path relocation to
  `evidence/ws-filetree/fix-evidence-c9-REPORT.md`, mandatory honest
  `collected_at: unknown (header added retroactively at <time>)`, no fabricated
  capture times, no edits to lane evidence content, verdicts excluded). The
  report matches that ruling.

## Findings

No blocking findings. The five lanes' text evidence (63/63) now carries a C9
header consistent with the repository's existing formats, the backfill is
honestly labelled, and no evidence byte below a header changed.

Non-blocking (LOW) findings / notes:

1. **REPORT.md §3 has an arithmetic error (documentation only).** It states
   "92 porcelain lines" and "96 lines = 1+1+1+92+1+1"; the actual block is
   **91** status lines and 96 = 1+1+1+**91**+1+1. The headers themselves are
   correct and complete; only the report's line count is off by one. Fix in a
   future report edit if the record is promoted; no repair task needed for the
   evidence itself.
2. **Backfill timestamp label is approximate.** Every header says "header added
   retroactively at 2026-10-31T10:19:00Z"; the final write mtimes are
   10:19:53–10:20:34Z (18:19:53–18:20:34 +0800), and the report §8 documents
   the two passes (first pass ≈10:19, rewrite ≈10:20). This is not a fabricated
   capture time and the report discloses it, but a future edit should either
   cite the final rewrite time or state "first pass / final rewrite" explicitly.
3. **Report path deviates from the frozen work contract.** The contract names
   `evidence/fix-evidence-c9/REPORT.md`, which is outside its own allowed write
   scopes; the writer followed the captain's recorded ruling and placed the
   report at `evidence/ws-filetree/fix-evidence-c9-REPORT.md` (inside an allowed
   scope). The deliverable's substance exists; the frozen contract is internally
   inconsistent and should be corrected in a later requirement/split revision so
   the frozen record matches reality. This is a contract defect, not a writer
   defect.
4. **Deliberate exclusions, documented:** 23 screenshots (`.png`), 1
   `bench.json`, 2 executable harness scripts (`.sh`) and `evidence/verdicts/**`
   were not given text headers. A text prefix would corrupt the binary/JSON
   artifacts, the scripts are reproduction code rather than captured output, and
   verdicts are reviewer-owned and captain-excluded. This is consistent with the
   captain's "五条 lane 文本产物" instruction and the report's §4 table; recorded
   here so the bound is visible, not hidden.

## Not verified / bounds

- The **original** capture time and HEAD/porcelain of the 63 artifacts cannot be
  reconstructed (that is precisely why the header says `unknown`); only the
  honesty of the substitute values is verifiable, and it is.
- The full pinned-image gate wrapper (`bun run verify:gates` → `scripts/gates.sh`,
  `build/gates/qt6-cxx.sh`) was not run: this task changed evidence text only,
  and the shared tree carries other lanes' in-flight source changes, so a
  whole-tree result would not be attributable to this task. The declared gate
  that applies to a text/evidence-only change (`verify:docs`) was run, exit 0,
  evidence id `ev-20261031T102716-0e5baf`.
- No GUI/visual behaviour is involved or certified (no display server).
