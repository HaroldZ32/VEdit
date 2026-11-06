# batch 4b — Git history viewer (AC-4.3)

Contract: `.mpd/cellwork/cell-20261103T123639841Z/contracts/git-history-viewer.md`
(AC-4.3). Board task: `T17`. Verifier: `review-git-history`.

This directory is the evidence for that contract: the lane probe, the xcb
screenshots, the seven-gate run and the generated logs. Every claim below quotes
the check name the lane prints.

## What was built (and where)

| Layer | Path | What changed |
| --- | --- | --- |
| History reads (Rust) | `crates/vd-vcs/src/history.rs` | `commits` / `commit_detail` / `file_history` / `blame` / `compare`, each through the SAME read-only `CommandRunner` gate as the rest of the backend. Pure parsers (`parse_log`, `parse_numstat`, `parse_commit_header`, `parse_blame`) + `PageRequest` clamping (`MAX_PAGE = 200`, `MAX_BLAME_LINES = 20000`, `MAX_TEXT_BYTES = 2 MiB`). An EMPTY repository (`error = false` + note) is distinct from a FAILED read (`error = true`). |
| C ABI | `crates/vd-ffi/src/vcs.rs`, `crates/vd-ffi/include/vd_ffi.h` | Five asynchronous start functions (`vd_vcs_history_{commits,commit,file,blame,compare}_job`) + ONE `vd_vcs_history_poll` / `vd_vcs_history_free` over a single `VdVcsHistoryResult` whose `kind` selects the payload. The header is cbindgen-generated. **Superseded by batch 7 (T45)**: the history reads now have FIVE typed `JobValue` payloads (`VcsHistoryCommits` / `VcsHistoryCommit` / `VcsHistoryFile` / `VcsHistoryBlame` / `VcsHistoryCompare`) in `crates/vd-ffi/src/service.rs`; the former `VcsInfoData` slot workaround described in the batch-4b source is gone, and `VcsInfo` means exactly the branch/refs identity read. |
| Panel seam | `src/workspace/vcs/vcs_source.h`, `vcs_source.cpp` | `HistoryKind`, `HistoryRequest`, `HistoryData`, and `historyCapable()/history()/requestHistory()` on `VcsSource`; `FfiVcsSource` runs the job and polls it on its QTimer (one pending slot per request, several may be in flight); `StubVcsSource` records every request and answers canned data. |
| History UI | `src/workspace/vcs/vcs_history.{h,cpp}` | Commit list (paging: Previous/Next + page label; search box), commit detail tab (meta + files + patch), file-history tab, blame tab, compare tab (two selected commits, oldest → newest). Four visible states: loading, empty (no commits), error, ready. Every read is asynchronous. |
| Integration | `src/workspace/vcs/vcs_integration.cpp` | The `vcs` dock is now a two-tab widget: **Working tree** (batch 3b/4a) and **History** (batch 4b); both are re-pointed at the workspace root on a project switch. |
| Translations | `translations/vedit_zh_CN.ts` | `lupdate` added 37 history/tab messages, all translated (map in `evidence/vcs-git-remote/tools/translations-4a.json`), `lrelease` reports `792 finished and 0 unfinished`. |

## Claims → evidence

All check names below are in `logs/10-selftest.log`
(`WORKSPACE-VCS-SELFTEST ok=1 checks=400 failures=0`; 345 of those are the
previous batches, unchanged — coverage only grew). Screenshot checks are in
`logs/20-screenshots.log` (`WORKSPACE-VCS-SELFTEST screenshots ok=1 checks=52
failures=0`).

### Commit list — paging and search

| Claim | Checks |
| --- | --- |
| a page carries exactly the requested window and `has_more` | `vcs.history.rows`, `vcs.history.page-label`, `vcs.history.request-kind` (limit 50) |
| the panel issues the EXACT page request | `vcs.history.next-page-offset`, `.next-page-uses-offset`, `.previous-page-uses-offset`, `.previous-never-negative`, `.last-page-no-more` |
| search is literal + case-insensitive and resets to page 1 | `vcs.history.search-request` (query + offset 0), `.search-box-kept`, `.search-cleared` |
| rows render hash/date/author/subject from the read | `vcs.history.row-columns`, `.hashes-in-data` |
| real repository: 1000 commits, 50-row pages | `vcs.history.e2e.p1000`, `.page`, `.page-newest-first`, `.last-page` |
| real repository: a search finds exactly the matching commit | `vcs.history.e2e.search`; a search with no hit is the empty state (`.search-empty`) |

### Commit detail

| Claim | Checks |
| --- | --- |
| selecting a row issues the detail read for THAT hash | `vcs.history.detail-request` |
| meta + file stats + patch are rendered | `vcs.history.detail-text`, `.detail-patch` (real repo: `+// 999` and the path) |
| the root commit lists its files (git needs `--root`) | `crates/vd-vcs/tests/history.rs::commit_detail_reports_meta_files_and_patch` |

### Single-file history

| Claim | Checks |
| --- | --- |
| the path is the request and only that path's commits come back | `vcs.history.file-request`, `.file-text`, `vcs.history.e2e.file-history`, `.file-history-rows` |
| the Rust reader follows the path and pages it | `file_history_lists_only_the_paths_commits` (in `crates/vd-vcs/tests/history.rs`) |

### Blame

| Claim | Checks |
| --- | --- |
| blame requests the path and renders line/commit/author/text | `vcs.history.blame-request`, `.blame-text`, `vcs.history.e2e.blame`, `.blame-author` |
| each line is attributed to the commit that introduced it | `blame_attributes_each_line_to_its_commit` (real git, asserts the changed line's hash) |

### Two-commit comparison

| Claim | Checks |
| --- | --- |
| the comparison needs two selected commits and says so | `vcs.history.compare-needs-two` |
| the request is oldest → newest | `vcs.history.compare-request` |
| files + patch render | `vcs.history.compare-text`, `vcs.history.e2e.compare`, `.compare-patch` |
| the Rust reader reports files and the patch, and an empty comparison is not an error | `comparison_lists_the_files_and_the_patch` |

### Asynchronous, non-blocking (≥ 1000 commits)

The 1000-commit fixture is built with ONE `git fast-import` process (a
`git commit` loop would make the fixture, not the panel, the slow part). The lane
measures the FIRST page load of a fresh panel:

* `vcs.history.nonblocking.returns-fast` — `setSource()` returned in < 200 ms;
* `vcs.history.nonblocking.ticks` — a 5 ms timer fired **≥ 2 ticks** while the
  read was in flight (**16–18 ms**, 50 rows), so the event loop kept running;
* `vcs.history.nonblocking.sync-control` — the SAME measurement against a
  synchronous stub source fires **0 ticks**, so the metric really detects
  blocking rather than trivially passing.

The timing line is recorded in `logs/10-selftest.log` as
`HISTORY-TIMING p1000 rows=50 read_ms=<n> ticks=<n>` and copied into
`logs/gate-ac43-evidence.log`.

### Empty / no-commit / error states + i18n

| Claim | Checks |
| --- | --- |
| an empty repository shows the empty state with a reason | `vcs.history.empty-state`, `.empty-text`, `vcs.history.e2e.empty-repo`, `.empty-repo-text` |
| a failed read is the error state (not the empty one) and shows git's reason | `vcs.history.error-state`, `.error-text`, `vcs.history.e2e.raw-page` (ABI) |
| a source with no history path answers with a note, never a crash | `vcs.history.no-source` |
| every history string is in the compiled catalogue | `vcs.i18n.history` (Blame / Compare selected / Working tree / History / no-commits) |

## Screenshots (xcb, 1500×1000)

`bash evidence/vcs-git-history/screenshots.sh` starts `Xvfb :80`
(1500x1000x24) and runs the probe with `--require-xcb`. Each shot asserts bytes
> 4096, exact 1500×1000 and ≥ 8 sampled colours, so a flat/empty image cannot
pass. Log: `logs/20-screenshots.log` (52 checks, 0 failures).

| File | Shows |
| --- | --- |
| `screenshots/30-vcs-history-commits-1500x1000.png` | commit list (40 real commits, first page), selection, detail tab with meta + Files + patch |
| `screenshots/31-vcs-history-blame-1500x1000.png` | blame tab with per-line commit/author/text |
| `screenshots/32-vcs-history-empty-1500x1000.png` | empty repository state ("This repository has no commits yet.") |
| `screenshots/33-vcs-history-error-1500x1000.png` | error state with the backend's real reason |

The other shots in that directory are the batch-3b/4a working-tree views,
re-rendered by the same probe.

## The seven gates

`bash evidence/vcs-git-history/run-gates.sh` (build-qt, ctest, --selfcheck,
clippy, fmt, docs, lrelease) plus the AC-4.3 evidence step (screenshots exist and
are non-trivial, the timing line is present). Logs: `logs/gate-build-qt.log`,
`logs/gate-ctest.log`, `logs/gate-selfcheck.log`, `logs/gate-clippy.log`,
`logs/gate-fmt.log`, `logs/gate-docs.log`, `logs/gate-lrelease.log`,
`logs/gate-ac43-evidence.log`, `logs/gate-summary.log`.

## Boundaries (what this evidence does NOT prove)

* **Every read is read-only by construction**: the history functions run through
  the same `SystemRunner` allow-list (`git log/show/blame/diff/rev-parse/...`);
  `rev-list` and `diff-tree` are NOT used, so no new subcommand was added to the
  gate. The Rust test suite (`crates/vd-vcs/tests/readonly.rs`) still proves a
  write subcommand is refused before any process exists.
* **The comparison ignores renames.** `git diff --numstat A B` reports a rename as
  delete + add (no rename detection is requested); the panel says nothing about
  renames. Commit detail has the same property.
* **Merge commits have no per-file list** in `git show --numstat` (git prints
  nothing for a combined diff); the detail view says so
  (`note = "this commit carries no file changes here … use the comparison view"`)
  and the comparison view is the tool for merges.
* **Caps**: one page ≤ 200 commits, blame ≤ 20 000 lines, patch/body ≤ 2 MiB
  (marked `… (truncated)`); the 1000-commit fixture is far below the page cap but
  the cap itself is not exercised by a larger repository.
* **The 1000-commit timing is one measurement on this machine**, not a
  performance guarantee: it proves the READ is off the GUI thread (event-loop
  ticks) and that the call returns immediately, not a latency budget for other
  machines.
* **The ABI takes ONE result struct** for five kinds; a job id of the wrong kind is
  a typed misuse (`-1`), asserted in `crates/vd-ffi/tests/abi.rs`
  (`vcs_history_jobs_return_pages_detail_blame_and_comparison`).
