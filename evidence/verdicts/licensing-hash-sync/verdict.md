# Verdict — licensing-hash-sync (T63, independent review of T62)

captured: 2026-10-31T11:42Z
git_head: 95c496e682e3841add1a081edd78d29c34957aac
branch: feat/qt6-ktexteditor-migration
reviewer: Reviewer (instance 2), session b0af789c-16ef-42ef-ad05-9e137a41d604
work brief: T62 (`/home/haroldzhao/MyProj/DshProj/VEdit/.mpd/team/contracts/T62.json`) +
dispatch text of T63
review contract: **none frozen** — `.mpd/cellwork/cell-20261031T085543161Z/contracts/`
has no `fix-license-sync`/`rev-*` contract for T62/T63 and
`.mpd/verify/loops/` has no loop for T63 (newest loops are for other tasks).
No `mpd_verify_record` could be made; this verdict is **ADVISORY** and is
delivered as this file plus the completion note.

## Verdict: FAIL (one blocker; ADVISORY — no verification loop exists)

Items 1, 2 and 3 of the brief pass. Item 4 fails: the GTK half of the new
USER_GUIDE sentence is accurate, the Qt6 half is not supported by the product
code. Details in F1.

## Documents / sources the verdict rests on

- `.mpd/team/contracts/T62.json` and the T63 dispatch text — the work
  deliverable, its four verification items, and the explicit FAIL rule.
- `.mpd/cellwork/cell-20261031T085543161Z/requirements.md` — C9 (evidence
  headers), C11/C14 (the two XMLs and their registration), C10 (never modify a
  test/gate).
- `AGENT.md` §4/§5 (declared gates; never edit a gate) and §6 (git discipline;
  the Lead commits).
- T62's evidence: `evidence/licensing-hash-sync/REPORT.md`,
  `01-hash-recompute.txt`, `02-grep-sweep.txt`, `03-notices-and-gate.txt`.
- Historical snapshot files and their annotations, listed in the evidence below.

## Evidence produced by the reviewer (exact commands + observed output)

| # | check | observed |
| --- | --- | --- |
| 1 | `sha256sum` / `wc -l` / `wc -c` on the two XMLs | verilog `c6adc763c7b280d7493f48d06e5779d0b1e2068ac82f75fe7f31a07b0788a6c2`, **85 lines / 5,111 B**; systemverilog `2abba3342d0878e71b99d358d96e510f857cd7f232786951be8602f513e0b992`, **90 / 5,492 B** — both match the dispatch and the new registrations |
| 2 | `git diff -- THIRD_PARTY_NOTICES.md` | §9.1 `行数/字节` + `sha256` rows replaced with the new values, plus a change-note block naming the old values as pre-T45 history; the licence rows (`LGPL-2.1-or-later`) are untouched |
| 3 | `git diff -- docs/handover/appendix/amendments-rev2.md` | C14 rows and §1 item 2 now carry the new hashes/sizes with a change record; the old values are named as historical; A4.5 is still recorded as unmet |
| 4 | repo sweep `grep -rl '3d1ec41f\|dc6e0a47' THIRD_PARTY_NOTICES.md docs evidence packaging resources src tests scripts` | 19 files; each classified below — **no current registration still old, no historical value silently rewritten** |
| 5 | annotations: `git diff` of `evidence/m0-runtime/runtime-manifest.txt`, `evidence/docs-amendments/00-context.txt`, `evidence/docs-amendments/02-sha256-check.txt`, `evidence/syn-kde-data/04-licensing.txt`, `evidence/highlight-v2/10-licensing-and-embargo.txt` | pure **appended** `NOTE (T62, …)` blocks naming the values historical and pointing at `THIRD_PARTY_NOTICES.md §9.1` + `evidence/licensing-hash-sync/`; no historical value line is edited or removed (insertions only) |
| 6 | `git diff -- evidence/docs-amendments/verify.sh` | the source checks now pin the **new** hashes/sizes; the old values remain only in the packaged-snapshot checks with a comment explaining they are historical; `bash evidence/docs-amendments/verify.sh` → `summary: pass=33 fail=0`, exit **0** |
| 7 | old logs preserved: `head` of `evidence/syn-kde-data/10-folding-before.log` / `11-folding-after.log` | both retained with an `# INVALID (T66): this log measured the DISTRO library …` banner; their values are unchanged (annotation only) |
| 8 | licence conclusions: `git status --porcelain -- src/syntax/kde/ksyntax_data.cpp` (empty), `git diff -- packaging/runtime/verify-not-shipped.sh`, `grep -nE 'sha256\|systemverilog.xml\|verilog.xml\|syntax-bundled\|embedded' packaging/runtime/verify-not-shipped.sh`, `grep -nE 'forbiddenSha256\|GPL-3.0\|forbidden' src/syntax/kde/ksyntax_data.cpp` | `ksyntax_data.cpp` **untouched** (forbidden KDE sha256 list + refusal + report intact); `verify-not-shipped.sh` modified at 19:09 **by another lane** (T64 comment) only to fix SIGPIPE paths — the KDE-sha256, KDE-filename, `syntax-bundled/`, and embedded-scan checks all remain, so the change **strengthens** detection, it does not weaken the embargo; `resources/syntax/` still ships only the two own LGPL XMLs + `LICENSE-LGPL-2.1` + `README.md` |
| 9 | M2 wording vs actual measurement: `grep` of the current amendments text, then `bash evidence/syn-kde-data/folding-probe/run.sh after` and `… before` | amendments says: T66 re-record on the real F2 library (`QRC_SYNTAX=OFF`), probe asserts `author=VEdit / section=Hardware`, `ldd` proves the library resolves into the F2 install tree, **Verilog 7/7, SystemVerilog 11/11** (26-line SV sample), first `10/11` logs superseded with `# INVALID (T66)`, A4.5 still unmet. My own re-run: after → `LDD library=F2 resolves=…/F2/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6`, Verilog `begins=7 ends=7`, SV `begins=11 ends=11` (26 lines), `PROBE-SUMMARY mode=after … all-checks=PASS`, exit 0; before → `begins=0 ends=0` for both + 37 `Unknown keyword list` warnings, `all-checks=PASS`, exit 0. The files it cites (`10-folding-before-F2.log`, `11-folding-after-F2.log`, `FOLDING-fix-T45.md`, `T66-REPORT.md`) exist and contain exactly those lines. The dispatch text said "SV 7/7"; that expectation is stale — the current, truthful SV number is **11/11** (the old 7/7 came from the SV run that wrongly used the 18-line Verilog sample). |
| 10 | `bash build/gates/docs.sh` | `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md`, **exit 0** |
| 11 | USER_GUIDE item 4: `grep -n` of `docs/USER_GUIDE.md`; `sed -n '1470,1485p' crates/vd-app/src/app.rs`; `grep -n 'Loader::new(cwd)' crates/vd-app/src/config_glue.rs`; `sed -n '88,108p' crates/vd-config/src/lib.rs`; `grep -n 'projectRoot' src/app/main.cpp src/workspace/integration.h src/workspace/project/*`; `grep -rn 'openFile' src/app/`; `grep -n 'Open a project with --project' src/workspace/project/project_panel.cpp` | GTK half **true**: `app.rs:1475` `std::env::current_dir()` → `app.rs:1480` `Startup::load(&cwd, …)` → `config_glue.rs:63` `Loader::new(cwd)`. The `.F`/`.f` case-insensitivity claim **true**: `crates/vd-config/src/project.rs:267-270` `has_f_extension` uses `eq_ignore_ascii_case("f")`. Qt6 half **false**: the Qt6 shell takes its project root from `--project` (`src/app/main.cpp:819`), `IntegrationOptions.projectRoot` (`src/workspace/integration.h:26`), opens exactly that root (`src/workspace/project/project_integration.cpp:56,69`), and passes it to `vd_project_parse` (`src/workspace/project/project_service.cpp:53-54`); the project panel itself tells the user `Open a project with --project <dir>` (`src/workspace/project/project_panel.cpp:38`). No code derives the project root from the opened file's directory (`EditorPane::openFile` only opens a document; no project-root update anywhere), and `crates/vd-config`'s `Loader::new(start_dir)` doc comment at `lib.rs:93-103` describes the **intended caller contract** ("the directory that holds the file the editor just opened"), which the current Qt6 shell does not exercise. |

## Findings

| id | severity | symptom | expected | document / code source |
| --- | --- | --- | --- | --- |
| **F1** | **MEDIUM (blocker)** | `docs/USER_GUIDE.md:67-70` states that the Qt6 product's project-root discovery starts at **the opened file's directory** and that "从哪个目录打开文件决定从哪一级向上找根". The Qt6 product actually takes its root from `--project <dir>` or the restored session; there is no file-open → project-root wiring. | The USER_GUIDE must describe the product's real behaviour (or the product must be wired as described). Minimum repair: say that the Qt6 shell takes the project root from `--project`/the restored session and that the `Loader` contract defines `start_dir` as the opened file's directory for the caller that supplies it; do not claim "打开文件所在目录" is currently the root origin. | `docs/USER_GUIDE.md:67-70`; `src/app/main.cpp:819`; `src/workspace/integration.h:26`; `src/workspace/project/project_integration.cpp:56,69`; `src/workspace/project/project_service.cpp:53-54`; `src/workspace/project/project_panel.cpp:38`; `crates/vd-config/src/lib.rs:93-103` |

### Non-blocking observations

- **N1 (open item, planner-owned).** The authoritative cell requirements
  (`.mpd/cellwork/cell-20261031T085543161Z/requirements.md`, C11 line 80 and C14
  lines 87-88) still pin the pre-T45 hashes and sizes. T62's brief explicitly
  forbids touching `requirements.md` (planner's write scope); the T46 verdict's
  M1 already asks the planner to re-register. Not a T62 defect, but the
  "registration complete" goal is not closed until the planner updates it.
- **N2 (process).** T63 has no frozen acceptance contract and no verification
  loop (see header), so this verdict could not be recorded through
  `mpd_verify_record`; it is advisory and the captain owns the repair flow.
- **N3 (stale claim, not a defect).** T62's own `evidence/licensing-hash-sync/REPORT.md`
  §2 says "no folding counts are asserted in this task's documents"; after T62's
  report, T66 updated the same amendments file (mtime 19:31 vs report 19:27) to
  assert Verilog 7/7 / SV 11/11 and the INVALID banner. The **current** text is
  correct against the current evidence (verified in row 9); the report's §2 is
  simply a snapshot of the earlier state.
- **N4 (annotation, not tampering).** `evidence/verdicts/syntax/verdict.md`
  carries a post-hoc "后续注记（2026-10-31 19:2x）" pointing at the new hashes and
  keeps the old values; the values are not rewritten. Its mtime (19:27:15)
  coincides with T62's REPORT mtime, while T62's report claims
  `evidence/verdicts/**` was untouched. Attribution is ambiguous (another agent
  could have written it), but the artifact shows an **annotation, not a silent
  rewrite**; flagged so the captain can confirm who wrote it.
- **N5 (other lane, strengthens).** `packaging/runtime/verify-not-shipped.sh`
  was modified by another lane (mtime 19:09, comment "F2 (T64)"; T62 claimed at
  19:18). The diff replaces `find | grep -q` / `strings | grep -qx` with
  SIGPIPE-safe forms so a forbidden file can no longer be reported clean; the
  embargo checks themselves are unchanged and not weakened.

## Not verified / bounds

- No GUI/visual behaviour is involved; the machine has no display server.
- The T62 writer's identity/attribution of overlapping shared-tree edits could
  not be proven from the board; files are attributed by content, mtime, and the
  writer's own REPORT.
- The Qt6 file-open → project-root behaviour was checked statically (no such
  wiring exists); a runtime interaction test is impossible without a display and
  was not attempted.
