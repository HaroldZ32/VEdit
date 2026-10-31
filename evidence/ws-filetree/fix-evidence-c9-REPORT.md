generated: 2026-10-31T10:22:08Z
git_head: 9957f5fe4d63c17c681e8753288262fa2c00955e
git_status_porcelain:
 M CMakeLists.txt
 M Cargo.lock
 M Cargo.toml
 M THIRD_PARTY_NOTICES.md
 M docs/decisions/r2-license.md
 M docs/handover/README.md
 M evidence/m0-build/build-logs/21-qt6-image-id.txt
 M evidence/m0-build/build-logs/31-qt6-configure.log
 M evidence/m0-build/build-logs/32-qt6-build.log
 M evidence/m0-build/build-logs/33-qt6-ctest.log
 M evidence/m0-build/build-logs/34-qt6-elf.txt
 M evidence/syn-grammars/00-pins.txt
 M evidence/syn-grammars/01-build.log
 M evidence/syn-grammars/02-tests.log
 M evidence/syn-grammars/03-manifest.txt
 M evidence/syn-grammars/04-manifest-none.txt
 M evidence/syn-grammars/04-manifest-static.txt
 M evidence/syn-grammars/04-manifest-systemverilog.txt
 M evidence/syn-grammars/04-manifest-verilog.txt
 M evidence/syn-grammars/04-swap-proof.txt
 M evidence/syn-grammars/05-layering.txt
 M evidence/syn-grammars/06-licensing.txt
 M evidence/syn-grammars/07-conclusion.md
 M evidence/syn-grammars/09-result.txt
 M evidence/term-panel/00-pins.txt
 M evidence/term-panel/01-configure.log
 M evidence/term-panel/02-build.log
 M evidence/term-panel/03-ctest.log
 M evidence/term-panel/03b-checks.log
 M evidence/term-panel/04-elf-needed.txt
 M evidence/term-panel/05-no-network.txt
 M evidence/term-panel/06-xcb-and-screenshots.log
 M evidence/term-panel/07-conclusion.md
 M evidence/term-panel/08-structure.txt
 M evidence/term-panel/09-root-build-with-lane.log
 M evidence/ws-filetree/00-pins.txt
 M evidence/ws-filetree/00-steps.txt
 M evidence/ws-filetree/01-configure.log
 M evidence/ws-filetree/02-build.log
 M evidence/ws-filetree/03-ctest.log
 M evidence/ws-filetree/03a-tests-full.log
 M evidence/ws-filetree/04-fixture.log
 M evidence/ws-filetree/05-screenshots.log
 M evidence/ws-filetree/06-permissions-nonroot.log
 M evidence/ws-filetree/07a-gate-qt6-cxx-pre-seam.log
 M evidence/ws-filetree/08-real-shell-seamcheck.log
 M evidence/ws-filetree/09-real-app-install.log
 M evidence/ws-filetree/10-rootcheck-configure.log
 M evidence/ws-filetree/11-rootcheck-build.log
 M evidence/ws-filetree/12-gate-build-qt-final.log
 M evidence/ws-filetree/12a-gate-build-qt-first.log
 M evidence/ws-filetree/13-gate-qt6-cxx-final.log
 M evidence/ws-filetree/14-real-shell-check-run.log
 M evidence/ws-filetree/REPORT.md
 M evidence/ws-palette/00-pins.txt
 M evidence/ws-palette/30-build-qt.log
 M evidence/ws-palette/31-bench.log
 M evidence/ws-palette/32-ctest-palette.log
 M evidence/ws-palette/33-elf.txt
 M evidence/ws-palette/34-artifacts.txt
 M evidence/ws-palette/36-app-seam-diff.md
 M evidence/ws-palette/37-gate-qt6-cxx.log
 M evidence/ws-palette/REPORT.md
 M evidence/ws-palette/SEAM.md
 M evidence/ws-search/04-selfcheck-root.log
 M evidence/ws-search/05-selfcheck-nonroot.log
 M evidence/ws-search/06-screenshots.log
 M evidence/ws-search/07-screenshots-nonroot.log
 M evidence/ws-search/08-build-qt.log
 M evidence/ws-search/09-structure.txt
 M evidence/ws-search/10-ctest-root-search.log
 M evidence/ws-search/13-integration-proof.log
 M evidence/ws-search/14-standalone.log
 M evidence/ws-search/REPORT.md
 M scripts/build-qt-in-container.sh
 M scripts/build-qt.sh
 M src/app/CMakeLists.txt
 M src/app/main.cpp
?? cmake/VEditRustFfi.cmake
?? crates/vd-ffi/
?? crates/vd-search/
?? crates/vd-settings/
?? crates/vd-vcs/
?? docs/handover/appendix/amendments-rev2.md
?? evidence/docs-amendments/
?? evidence/ffi-abi/
?? evidence/fixture/
?? evidence/m0-build/build-logs/37-qt6-rust-abi.txt
?? evidence/verdicts/
?? evidence/ws-filetree/fix-evidence-c9-REPORT.md
?? scripts/verify-fixture.sh
?? tests/fixtures/
note: this header itself is NOT backfilled — this REPORT was written at 2026-10-31T10:22:08Z, so its time/HEAD/status are true capture values (the 63 backfilled files carry collected_at: unknown)
---
# fix-evidence-c9 — batch backfill of C9 headers (T43)

Scope: give the text evidence of five lanes (syn-grammars T17, ws-filetree T21,
ws-palette T22, ws-search T23, term-panel T35) the C9 header that was missing:
capture time + `git rev-parse HEAD` + `git status --porcelain`.

## 1. Contract defect and captain ruling (recorded as instructed)

The frozen contract (split revision 3) names the deliverable
`evidence/fix-evidence-c9/REPORT.md`, but its Allowed write scopes are only the
five lane directories — that deliverable path is **outside the write scope**.
Rather than bump the requirement/split revision (and re-activate a 43-task
graph) for one report path, the captain ruled **B**: the report lives at

```
evidence/ws-filetree/fix-evidence-c9-REPORT.md
```

Captain's reason, quoted verbatim:

> `evidence/ws-filetree/` 是**停点 1 的前置 lane**（`slice1-stop` 直接依赖
> `rev-fix-filetree-open`），是用户在验收时最可能翻的证据目录；把批量报告放在这里最容易被人看到。
> 选它不是因为它"属于 filetree"，而是**可发现性**。

No contract, split or requirement revision was changed for this path.

## 2. Format source — no new format invented

The three required fields and the `---` terminator follow the artifacts already
in this repository:

- `evidence/fixture/README.md` and the files generated next to it
  (`manifest.txt`, `expected.txt`, `verify-output.txt`, …) — the reference this
  task names: colon keys `git_head:`, `git_status_porcelain:` followed by the
  porcelain lines, then `---`;
- `evidence/docs-amendments/00-context.txt` — a pre-existing headed artifact
  using `collected_at:` (ISO UTC), `git_head:`, `git_status_porcelain:`;
- `evidence/appimage/source-tree.txt` — a pre-existing headed artifact using the
  same two keys in `key=value` form.

Key choice: backfilled files use **`collected_at:`** (as in
`docs-amendments/00-context.txt`) rather than `generated:`, because the value is
**not** a known collection time. The task wording suggested recording the
capture time and the *then* HEAD/porcelain; neither can be recovered for these
artifacts, so nothing is asserted.

## 3. Honest annotation (hard requirement)

Every backfilled file starts with:

```text
collected_at: unknown (header added retroactively at 2026-10-31T10:19:00Z)
git_head: 9957f5fe4d63c17c681e8753288262fa2c00955e
git_status_porcelain:
<92-line snapshot taken at 2026-10-31T10:19:00Z, identical in every file>
backfill: 补记 —— C9 头为事后补齐；git_head / git_status_porcelain 为补记时刻快照，非原始采集时刻；原始采集时刻不可回溯，文件 mtime <t> 仅供参考
---
```

- `collected_at: unknown (…)` is deliberate: **no original timestamp was
  fabricated**. The file mtime is recorded for reference only and is explicitly
  *not* asserted as the capture time.
- `git_head` / `git_status_porcelain` are the backfill-time values (HEAD
  `9957f5f`, snapshot taken 2026-10-31T10:19:00Z). The snapshot therefore lists
  in-flight work of other lanes, including these very edits; that is the honest
  maximum available and it is labelled as such in every file.
- Header size per file: **96 lines** = 1 `collected_at` + 1 `git_head` +
  1 `git_status_porcelain` + 92 porcelain lines + 1 `backfill` + 1 `---`.

## 4. What was missing, and what was excluded

Checked before writing: none of the 63 text artifacts in the five lanes carried
`git_head:`, `generated:` or `collected_at:` in its first 15 lines — all of them
lacked the C9 header.

Backfilled: every `*.txt`, `*.log` and `*.md` under the five lane directories —
**63 files**.

Deliberately **not** backfilled, with reasons:

| excluded | reason |
| --- | --- |
| `evidence/ws-filetree/screenshots/*.png`, `evidence/ws-palette/screenshots/*.png`, `evidence/ws-search/*.png`, `evidence/term-panel/*.png` | binary images — a text header would corrupt the artifact |
| `evidence/ws-palette/bench.json` | structured JSON — a text header would break parsing |
| `evidence/term-panel/run-evidence.sh`, `evidence/term-panel/run-root-rehearsal.sh` | executable harness code — a text header would break execution |
| `evidence/verdicts/**` | reviewer-owned artifacts, outside this task and explicitly excluded by the captain |

No existing evidence line was edited or removed: the backfill is a pure prefix
(`git diff` shows insertions only — §6).

## 5. Per-lane file lists

Each row: path — lines inserted (the 96-line block of §3, identical for all) —
the file mtime recorded in that file's `backfill:` line (reference only, not
asserted as the capture time).


### syn-grammars — 13 files

| file | lines added | mtime reference |
| --- | --- | --- |
| `evidence/syn-grammars/00-pins.txt` | 96 | 2026-10-31T04:22:01Z |
| `evidence/syn-grammars/01-build.log` | 96 | 2026-10-31T04:22:15Z |
| `evidence/syn-grammars/02-tests.log` | 96 | 2026-10-31T04:22:17Z |
| `evidence/syn-grammars/03-manifest.txt` | 96 | 2026-10-31T04:22:17Z |
| `evidence/syn-grammars/04-manifest-none.txt` | 96 | 2026-10-31T04:22:17Z |
| `evidence/syn-grammars/04-manifest-static.txt` | 96 | 2026-10-31T04:22:17Z |
| `evidence/syn-grammars/04-manifest-systemverilog.txt` | 96 | 2026-10-31T04:22:17Z |
| `evidence/syn-grammars/04-manifest-verilog.txt` | 96 | 2026-10-31T04:22:17Z |
| `evidence/syn-grammars/04-swap-proof.txt` | 96 | 2026-10-31T04:22:17Z |
| `evidence/syn-grammars/05-layering.txt` | 96 | 2026-10-31T04:22:17Z |
| `evidence/syn-grammars/06-licensing.txt` | 96 | 2026-10-31T04:22:17Z |
| `evidence/syn-grammars/07-conclusion.md` | 96 | 2026-10-31T04:23:25Z |
| `evidence/syn-grammars/09-result.txt` | 96 | 2026-10-31T04:22:17Z |

### ws-filetree — 19 files

| file | lines added | mtime reference |
| --- | --- | --- |
| `evidence/ws-filetree/00-pins.txt` | 96 | 2026-10-31T04:19:18Z |
| `evidence/ws-filetree/00-steps.txt` | 96 | 2026-10-31T04:19:53Z |
| `evidence/ws-filetree/01-configure.log` | 96 | 2026-10-31T04:19:18Z |
| `evidence/ws-filetree/02-build.log` | 96 | 2026-10-31T04:19:18Z |
| `evidence/ws-filetree/03a-tests-full.log` | 96 | 2026-10-31T04:19:20Z |
| `evidence/ws-filetree/03-ctest.log` | 96 | 2026-10-31T04:19:19Z |
| `evidence/ws-filetree/04-fixture.log` | 96 | 2026-10-31T04:19:21Z |
| `evidence/ws-filetree/05-screenshots.log` | 96 | 2026-10-31T04:19:22Z |
| `evidence/ws-filetree/06-permissions-nonroot.log` | 96 | 2026-10-31T04:19:21Z |
| `evidence/ws-filetree/07a-gate-qt6-cxx-pre-seam.log` | 96 | 2026-10-31T04:07:34Z |
| `evidence/ws-filetree/08-real-shell-seamcheck.log` | 96 | 2026-10-31T04:19:53Z |
| `evidence/ws-filetree/09-real-app-install.log` | 96 | 2026-10-31T04:19:52Z |
| `evidence/ws-filetree/10-rootcheck-configure.log` | 96 | 2026-10-31T04:19:53Z |
| `evidence/ws-filetree/11-rootcheck-build.log` | 96 | 2026-10-31T04:19:53Z |
| `evidence/ws-filetree/12a-gate-build-qt-first.log` | 96 | 2026-10-31T04:10:55Z |
| `evidence/ws-filetree/12-gate-build-qt-final.log` | 96 | 2026-10-31T04:17:29Z |
| `evidence/ws-filetree/13-gate-qt6-cxx-final.log` | 96 | 2026-10-31T04:17:34Z |
| `evidence/ws-filetree/14-real-shell-check-run.log` | 96 | 2026-10-31T04:17:40Z |
| `evidence/ws-filetree/REPORT.md` | 96 | 2026-10-31T04:18:29Z |

### ws-palette — 10 files

| file | lines added | mtime reference |
| --- | --- | --- |
| `evidence/ws-palette/00-pins.txt` | 96 | 2026-10-31T04:12:49Z |
| `evidence/ws-palette/30-build-qt.log` | 96 | 2026-10-31T04:14:43Z |
| `evidence/ws-palette/31-bench.log` | 96 | 2026-10-31T04:13:10Z |
| `evidence/ws-palette/32-ctest-palette.log` | 96 | 2026-10-31T04:12:37Z |
| `evidence/ws-palette/33-elf.txt` | 96 | 2026-10-31T04:12:50Z |
| `evidence/ws-palette/34-artifacts.txt` | 96 | 2026-10-31T04:14:43Z |
| `evidence/ws-palette/36-app-seam-diff.md` | 96 | 2026-10-31T04:12:20Z |
| `evidence/ws-palette/37-gate-qt6-cxx.log` | 96 | 2026-10-31T04:14:23Z |
| `evidence/ws-palette/REPORT.md` | 96 | 2026-10-31T04:13:44Z |
| `evidence/ws-palette/SEAM.md` | 96 | 2026-10-31T04:07:29Z |

### ws-search — 10 files

| file | lines added | mtime reference |
| --- | --- | --- |
| `evidence/ws-search/04-selfcheck-root.log` | 96 | 2026-10-31T04:11:52Z |
| `evidence/ws-search/05-selfcheck-nonroot.log` | 96 | 2026-10-31T04:11:54Z |
| `evidence/ws-search/06-screenshots.log` | 96 | 2026-10-31T04:12:06Z |
| `evidence/ws-search/07-screenshots-nonroot.log` | 96 | 2026-10-31T04:12:19Z |
| `evidence/ws-search/08-build-qt.log` | 96 | 2026-10-31T04:11:40Z |
| `evidence/ws-search/09-structure.txt` | 96 | 2026-10-31T04:13:38Z |
| `evidence/ws-search/10-ctest-root-search.log` | 96 | 2026-10-31T04:12:07Z |
| `evidence/ws-search/13-integration-proof.log` | 96 | 2026-10-31T04:09:04Z |
| `evidence/ws-search/14-standalone.log` | 96 | 2026-10-31T04:08:28Z |
| `evidence/ws-search/REPORT.md` | 96 | 2026-10-31T04:13:38Z |

### term-panel — 11 files

| file | lines added | mtime reference |
| --- | --- | --- |
| `evidence/term-panel/00-pins.txt` | 96 | 2026-10-31T04:13:17Z |
| `evidence/term-panel/01-configure.log` | 96 | 2026-10-31T04:13:17Z |
| `evidence/term-panel/02-build.log` | 96 | 2026-10-31T04:13:23Z |
| `evidence/term-panel/03b-checks.log` | 96 | 2026-10-31T04:13:25Z |
| `evidence/term-panel/03-ctest.log` | 96 | 2026-10-31T04:13:24Z |
| `evidence/term-panel/04-elf-needed.txt` | 96 | 2026-10-31T04:13:25Z |
| `evidence/term-panel/05-no-network.txt` | 96 | 2026-10-31T04:13:25Z |
| `evidence/term-panel/06-xcb-and-screenshots.log` | 96 | 2026-10-31T04:13:30Z |
| `evidence/term-panel/07-conclusion.md` | 96 | 2026-10-31T04:18:12Z |
| `evidence/term-panel/08-structure.txt` | 96 | 2026-10-31T04:13:30Z |
| `evidence/term-panel/09-root-build-with-lane.log` | 96 | 2026-10-31T04:18:03Z |

## 6. Verification (observed, not claimed)

Run from the repository root at HEAD `9957f5f`:

```text
$ git diff --numstat -- evidence/syn-grammars evidence/ws-filetree evidence/ws-palette evidence/ws-search evidence/term-panel
files=63  insertions=6048  deletions=0
```

- Every one of the 63 entries is `96 0` — insertions only, no deletions.
- Body-preservation check (for each file, compare the committed body with the
  bytes after line 96):

  ```sh
  git diff --numstat -- <dirs> | while IFS=$'\t' read -r ins del f; do
    cmp <(git show "HEAD:$f") <(tail -n +$((ins+1)) -- "$f") || echo "BODY-DIFF $f"
  done
  ```

  observed: `body_mismatches=0` (63/63 byte-identical below the header).
- All 63 files start with
  `collected_at: unknown (header added retroactively at ` — observed count 63.
- `git status --porcelain -- <five dirs>` shows only ` M` on those 63 files plus
  this untracked report; nothing outside the five lane directories changed.
- Declared gate re-run unchanged: `bash build/gates/docs.sh` →
  `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md`,
  exit 0.
- No test and no gate file was modified (`evidence/verdicts/**` untouched).

## 7. Reproduction of the backfill

For each file in the five lanes (extension `*.txt`, `*.log`, `*.md`), prepend
the 96-line block of §3 with `collected_at: unknown (header added retroactively
at <backfill time>)`, the current `git rev-parse HEAD`, the current
`git status --porcelain`, and the file's mtime as a reference-only line; leave
every original byte after the header untouched.

## 8. Process note (honest log of two corrections)

1. The first pass used `generated: <file mtime>` as the capture time. The
   captain's ruling forbids presenting mtime as the original capture time, so all
   headers were rewritten to `collected_at: unknown (header added retroactively
   at 2026-10-31T10:19:00Z)` with the mtime moved to a reference-only
   `backfill:` line.
2. While stripping that first header, a strip rule also matched the markdown
   separator lines `---` inside `evidence/ws-filetree/REPORT.md` and removed 11
   of them. That file was restored byte-for-byte from `git show
   HEAD:evidence/ws-filetree/REPORT.md` before the header was re-applied; the
   `body_mismatches=0` check in §6 is what proves the final state is clean.

## 9. Limits

The backfill cannot reconstruct the capture-time HEAD/porcelain; it substitutes
honestly labelled backfill-time values. Binary, JSON and executable artifacts are
excluded rather than corrupted. Reviewer verdict files were not touched.
