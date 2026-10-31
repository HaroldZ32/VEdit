# Verdict — fixture (T15, independent review of T14)

captured: 2026-10-31T10:06Z
git_head: 9957f5fe4d63c17c681e8753288262fa2c00955e
branch: feat/qt6-ktexteditor-migration
reviewer: Reviewer (instance 2), session b0af789c-16ef-42ef-ad05-9e137a41d604
loop: `loop-20261031T095153-f2c4d4`
work contract: `.mpd/cellwork/cell-20261031T085543161Z/contracts/fixture.md`
review contract: `.mpd/cellwork/cell-20261031T085543161Z/contracts/rev-fixture.md`

## Verdict: PASS (bound)

Recorded before reading `scripts/verify-fixture.sh` or any product implementation.
Basis: the frozen contracts, the requirements document, the fixture data
(the deliverable under review), and the runs below. The script's internals were
never read; only its observed behaviour was used.

## Documents the verdict rests on

- `.mpd/cellwork/cell-20261031T085543161Z/requirements.md` — C13 (fixture must
  carry `.f` with `+incdir+` / `+define+` / nested `-f` / comments, several
  `.v/.sv`, a hierarchy; repo-internal, user project optional), AC-S1.5
  (fixture in-repo, acceptance script re-runnable, file list + expectations in
  evidence), C9 (evidence header: time + HEAD + `git status`), C10 (never modify
  an existing test/gate), non-goals 8 (own fixture, not the user's project) and
  10 (no test/gate weakening).
- `.mpd/cellwork/cell-20261031T085543161Z/contracts/fixture.md` — deliverable,
  allowed write scopes (`tests/fixtures/`, `scripts/verify-fixture.sh`,
  `evidence/fixture/`), acceptance criteria.
- `.mpd/cellwork/cell-20261031T085543161Z/contracts/rev-fixture.md` — this
  review's deliverable, gate command clause, verification protocol.
- `AGENT.md` §4 (declared gates), §5 (re-run checks, never edit a gate), §6
  (git discipline; the captain is the single git writer).
- `tests/fixtures/verilog-project/README.md` — the fixture's own documentation.
- `evidence/fixture/README.md`, `manifest.txt`, `expected.txt`,
  `verify-output.txt`, `idempotence.txt`, `failure-modes.txt` — the writer's
  evidence records (treated as claims, re-checked below).

## Evidence produced by the reviewer (commands + observed results)

| # | command | observed |
| --- | --- | --- |
| 1 | `bash scripts/verify-fixture.sh` | exit **0**; `21 passed, 0 failed, 1 skipped`; `git status --porcelain` hash identical before/after → plain mode writes nothing |
| 2 | `bash scripts/verify-fixture.sh` twice, sha256 of stdout | exit 0 / exit 0; both `2f625093730042f12d1863505c25d9252418b79b5fde95ed9a02bb450aa3493e` — byte-identical, and equal to the recorded `idempotence.txt` |
| 3 | copy fixture to `/tmp`, `rm rtl/mem.sv`, `bash scripts/verify-fixture.sh --fixture <copy>` | exit **1**; `19 passed, 2 failed, 1 skipped`; FAIL `fixture tree is incomplete`, FAIL `resolved source entries are missing` — matches `failure-modes.txt` case 2 |
| 4 | copy fixture to `/tmp`, inject `rtl/ghost.sv` into `expected/files.txt`, `--fixture <copy>` | exit **1**; `20 passed, 1 failed, 1 skipped`; FAIL `entries does not match expected/files.txt` — matches `failure-modes.txt` case 1 |
| 5 | `bash scripts/verify-fixture.sh --evidence <tmpdir>` | exit **0**; writes `manifest.txt`, `expected.txt`, `verify-output.txt` (overwriting, outside the tree) |
| 6 | `mpd_verify_evidence {kind:"gate", gate:"docs", loop_id:"loop-20261031T095153-f2c4d4"}` | exit **0**; log `.mpd/verify/evidence/ev-20261031T100521-696f95.log` (declared repo gate `verify:docs`, `build/gates/docs.sh`) |
| 7 | sha256 recomputation of all 15 fixture source/filelist/header files vs `evidence/fixture/manifest.txt` | identical — `MANIFEST MATCHES ACTUAL`; fixture total 21 files, matching the script's `21 required files` check |
| 8 | `git status --porcelain`, `git diff --name-only` | tracked modifications: only ` M Cargo.toml` (Slice-0 service-layer lane adds `crates/vd-ffi|vd-search|vd-settings|vd-vcs` to workspace members; not this task's scope). No `tests/**`, `scripts/**` or `build/gates/**` file is modified → C10 "never modify an existing test/gate" holds |

## C13 / AC-S1.5 coverage check (from the fixture data, not from the writer's prose)

- `.f` with `+incdir+`: 4 resolved include dirs, incl. one multi-directory line
  (`+incdir+inc/generated+rtl/include`); every `+incdir+` resolves to a real directory.
- `+define+`: `WIDTH=8`, `DEPTH=16` (value form) and `DEBUG`, `SIMULATION` (bare form).
- nested `-f`: two levels — `project.f` → `filelists/tb.f` → `filelists/nested/tb_aux.f`,
  plus a second nested filelist `filelists/extra.f`; entries resolved relative to
  the containing filelist (`../rtl/...`), as documented in the fixture README.
- comments: both `//` and `#` forms in `project.f`, `filelists/tb.f`, `filelists/extra.f`.
- `.v` / `.sv`: `rtl/counter.v` (plain Verilog-2001) plus `alu_pkg.sv`, `alu.sv`,
  `mem.sv`, `top.sv`, `tb/tb_top.sv`, `tb/tb_aux.sv`.
- hierarchy: `tb_top` → `top` → `alu` (`u_alu`), `counter` (`u_counter`), `mem`
  (`u_mem`); package use through `alu_pkg`; headers reached via include dirs.
  Confirmed both by the script's PASS lines and by direct grep of the sources.
- duplicate entry and simulator-only tokens (`+libext+.v+.sv`, `-y`) exercised,
  with expectations checked in.
- own project, not the user's: fixtures are synthetic (`alu`, `counter`, `mem`,
  `tb_top`) and repo-internal under `tests/fixtures/` (C13 / non-goal 8).

## Findings

No blocking findings. The deliverable exists, all its changed paths are inside
the allowed write scopes, the acceptance script runs re-runnably (exit 0,
byte-identical output), fails non-zero on broken expectations/sources, and the
file list + expectations are in `evidence/fixture/` with the C9 header.

Non-blocking observations:

1. **NOT-YET-COMMITTED (process, not a defect).** The lane's paths are still
   untracked (`?? tests/fixtures/`, `?? scripts/verify-fixture.sh`,
   `?? evidence/fixture/`). "入库" (C13) is satisfied in the working tree; the
   commit/PR is the captain's single-git-writer step (AGENT.md §6, C7), so this
   review does not treat it as a failure. It must be committed before the
   Slice-1 stop point / PR.
2. **Declared SKIP.** `verible-verilog-syntax` is not installed on this host, so
   the Verilog syntax parse of the fixture is not checked (the script prints
   `SKIP` and `evidence/fixture/README.md` declares the gap). C13 / AC-S1.5 do
   not require it; syntax/diagnostic acceptance belongs to AC-S1.2 / AC-S1.4 in
   a toolchain-bearing run.
3. **Script is a self-contained bash implementation of the filelist semantics**
   (it documents `crates/vd-config/src/filelist.rs` as the product contract, and
   that file exists). It therefore proves fixture ↔ expectation consistency, not
   that the Rust project-model parser agrees. The downstream project-model lane
   (AC-S1.3) must consume this fixture through the real parser; that is not this
   task's acceptance criterion.
4. **Compile gates not run in this review.** `bun run verify:gates`
   (`build/gates/gates.sh` → `scripts/gates.sh`, `build/gates/qt6-cxx.sh`) builds
   the pinned images and the whole workspace/Qt lane; this lane adds no Rust/C++
   code, and the shared tree carries other in-flight lanes' untracked crates, so
   a full-tree result would not be attributable to this task. The declared repo
   gate that does apply here (`verify:docs`, exit 0) was run and its evidence id
   is cited. This bound is declared, not hidden.

## Could not verify

- GUI behaviour on a display machine (no display server here): AC-S1.6 remains a
  user-only acceptance; nothing visual was certified.
- Verilog syntax parse of the fixture (`verible-verilog-syntax` absent).
- The full pinned-image build/compile gates, for the attribution reason above.
