# Verdict — ts-overlay (T18, independent review of T17)

captured: 2026-11-01T05:47Z (13:47 +08:00)
git_head: 31a7c2d10111a9ab6078a433b1aa8289b31197a6
branch: feat/qt6-ktexteditor-migration
reviewer: Reviewer (instance 2), session b0af789c-16ef-42ef-ad05-9e137a41d604
loop bound: `loop-20261031T095153-f1b316` (taskId **T17**, contract `ts-overlay.md`,
writer = Deep Worker) — the work loop the dispatch names.
Note: the rev-overlay contract also names `loop-20261031T095153-dabbdb` (taskId
T18, contract `rev-overlay.md`, writer = another Reviewer session); per the
dispatch ("the contract it names") this review records against the T17 work
loop. The T18 meta-loop remains unbound; flagged in the parent message.

## Verdict: FAIL (evidence integrity; the functional deliverable itself passes)

The implementation claims are **true and independently reproduced** (28/28
checks, full ctest 19/19, perf 108–110 ms ≤ 150 ms, injection p95 0.070 ms
≤ 2 ms), but the evidence package does not meet the cell's binding C9 rule and
the review contract's "C12 numbers recorded with machine and environment"
criterion: the README declares a capture window that ends in the future, two
logs carry no C9 header at all, the other two lack the porcelain part, and no
machine (CPU/nproc/kernel) is recorded for the performance numbers. The repair
is evidence-only — no code change is needed.

## Documents / sources the verdict rests on

- `.mpd/cellwork/cell-20261031T085543161Z/contracts/rev-overlay.md` — my
  deliverable (A1g, A1h, C12 numbers with machine+environment).
- `.mpd/cellwork/cell-20261031T085543161Z/contracts/ts-overlay.md` — the work
  contract (A1g single owner, A1h runtime fallback no crash, C12/Open Q2
  budgets, evidence labels agent/user).
- `.mpd/cellwork/cell-20261031T085543161Z/requirements.md` — **C9** (evidence
  file head = capture time + `git rev-parse HEAD` + `git status --porcelain`;
  a time that has not happened is not a capture time), **C12** (budgets and
  "in the recorded machine"), AC-S2.1 (A1g/A1h), AC-0.5 (agent/user labels).
- `AGENT.md` §4/§5 (declared gates, never weaken a gate).
- The deliverable: `src/syntax/overlay/` and `evidence/slice2-overlay/`.
- Pre-existing ownership policy used by the lane: `src/syntax/provider/owner_arbiter.{h,cpp}`.

## Evidence produced by the reviewer (commands + observed output)

| # | check | observed |
| --- | --- | --- |
| 1 | `cmake --build target/qt6-build --target vedit-syntax-overlay-selftest` (pinned image) | `BUILD-EXIT=0`; the lane is registered in the root build (`CMakeLists.txt:71 VEDIT_LANE_DIRECTORIES … src/syntax/overlay`) |
| 2 | `QT_QPA_PLATFORM=offscreen …/vedit-syntax-overlay-selftest --selfcheck` ×3 | every run `SYN-OVERLAY-SELFTEST ok=1 checks=28 failures=0 skipped=0`, `EXIT=0`; **28 PASS / 0 FAIL**; the 28 checks cover A1g (`overlay.inject.claims`, `overlay.single-owner` "single owner across 494 span(s)", `overlay.single-owner.overlay-present`, `.known-ranges lines 0,5,12,60`, 6 `overlay.paint.*` incl. `raw-overlap=663` and `arbiter-agrees`), A1h (`overlay.fallback.state/claims-released before=181 after=0/base-owns-everything/inject-is-noop/toggle-stress 100 cycles/reenabled`, `overlay.service-switch.no-provider/no-claims/restored`), C12 (`incremental.equivalence 812=812`, `scroll.window-isolated 50 windows`, `perf.rebuild-50`, `perf.injection-p95`) and the guards |
| 3 | my 3 perf runs (recorded machine: **AMD Ryzen 7 7840H, nproc=16, kernel 7.0.0-38-generic**, pinned image Qt 6.10.2 / KF6 6.24 / tree-sitter 0.25.9, doc 25,388 B / 1,051 lines) | `rebuild50_ms = 109.894 / 108.358 / 108.731` (budget 150) — PASS; `injection_p95_ms = 0.070 / 0.070 / 0.071` (budget 2) — PASS; matches the shape of `03-perf-runs.log` (their 102.6–105.5 ms on an unrecorded machine) |
| 4 | `ctest --test-dir target/qt6-build` (full, pinned image) | `100% tests passed, 0 tests failed out of 19`, `CTEST-EXIT=0` — the new `vedit-syntax-overlay-selftest` is test #8 and passes; no regression in the other 18 |
| 5 | `mpd_verify_evidence {gate:"typecheck"}` (declared gate) | **exit 101 / gate FAIL**, log `.mpd/verify/evidence/ev-20261101T054420-ae107b.log`: clippy `expect_used` errors in `crates/vd-vcs/src/runner.rs` — an **untracked in-flight file of another lane** (mtime 13:44:06, outside T17's scope); not attributable to the overlay lane but the repo-wide gate is red at review time |
| 6 | code verification of the single-owner path | `OverlayController` claims through `OwnerArbiter` only (`overlay.cpp:269-302`), `releaseAll()` on disable/close (`:48,91,115,246,270`), `state()=Disabled/NoProvider` when off (`:126-129`), `assertSingleOwner` delegated to the arbiter (`:311-314`). `OwnerArbiter::claim` rejects overlapping overlay intervals and merges adjacent ones; `ownerSpans()` partitions the range into Base/Overlay/None; `assertSingleOwner` checks first/last coverage and exact adjacency (no gap/overlap) — the A1g judgement is structural, not a name-only assertion. `buildPaintPlan` gives overlay precedence, clips base (`paint.cpp`), and a per-segment cross-check against the arbiter is one of the 28 passing checks. |
| 7 | baseline / write scope | writer's paths are `src/syntax/overlay/` (new) and `evidence/slice2-overlay/` (new), both inside the work contract's write scopes; no test/gate file was changed to make the lane green (count 18→19 is one additional lane test); the lane's ctest and the full ctest are green |

## Findings

| id | severity | symptom | expected / fix | document source |
| --- | --- | --- | --- | --- |
| **F1** | **HIGH (evidence integrity)** | `evidence/slice2-overlay/README.md:4` records `采集时间：2026-11-01 13:29–15:05 (Asia/Shanghai)`, but the file was written at 13:38:58 and the review runs at 13:4x — **15:05 is in the future**; no log reaches it (01 `13:38:01`, 02 `05:38:04Z = 13:38:04`). This is a capture time that never happened. | Record the real capture window (≈13:29–13:38) or regenerate the evidence; never declare an unelapsed time as a collection time. | requirements **C9** (line 76: "证据文件头写采集时间 …"); `README.md:4`; log headers |
| **F2** | **HIGH (C9 header)** | `03-perf-runs.log` and `04-ctest.log` have **no C9 header at all** (no capture time, no HEAD, no porcelain); `01-build.log` and `02-selftest.log` have time+HEAD but **no `git status --porcelain`**. Yet `README.md:5` says "构建/采集时的值见每份日志头部". This repeats the C9 defect this cell already remediated (task `fix-evidence-c9`). | Every evidence log carries the C9 three-field header; or the README stops claiming per-log headers. | requirements **C9**; `README.md:5`; `03-perf-runs.log`; `04-ctest.log` |
| **F3** | **MEDIUM (acceptance criterion)** | The C12/Open-Q2 numbers have environment (pinned image, Qt/KF6/TS versions, doc size) but **no machine**: no CPU model, `nproc`, kernel or load context, in the README or the perf log. The review contract explicitly requires the numbers "有记录机器与环境". | Record the machine alongside the numbers (e.g. CPU model, nproc, kernel, image) or state the measured machine in the perf log. My reproduction for the record: AMD Ryzen 7 7840H, nproc=16, kernel 7.0.0-38-generic → 108.4–109.9 ms / p95 0.070–0.071 ms. | `rev-overlay.md` deliverable ("C12 性能数字有记录机器与环境"); `ts-overlay.md` C12/Open Q2; `README.md` §2/环境 |
| **F4** | LOW (undeclared test fixture) | A1g's paint-plan evidence uses a **synthetic base-highlighter stand-in** — `baseLineSpans()` "one style span per line" (`overlay_selftest.cpp:118-135,201`), not real KSyntaxHighlighting spans; `raw-overlap=663` is therefore an overlap with simulated base spans. The arbiter-level single-owner assertion is real (real tree-sitter claims), but the evidence README's §6 boundary list does not declare this limitation while §结论 presents the overlap as base-vs-overlay proof. | Declare in §6: paint-plan overlap is measured against a synthetic per-line base span set; the real KSH span geometry arrives with the editor integration (out of this lane). | `overlay_selftest.cpp:118-135,201`; `README.md` §结论 row A1g and §6 |
| **F5** | INFO | `README.md` says "ctest 绿" citing `04-ctest.log`, but that log only runs `ctest -R vedit-syntax-overlay-selftest`; the repo-wide suite was not recorded. My run: 19/19 PASS (no regression), so this is a record-completeness note. | If claiming repo-gate green, record the full `ctest` run. | `04-ctest.log`; requirements C10/gate discipline |
| **F6** | INFO (external, not T17) | The declared `typecheck` gate currently **fails (exit 101)** because another lane's untracked `crates/vd-vcs/src/runner.rs` trips clippy `expect_used` (mtime 13:44:06). The overlay lane's own build/selftest/ctest are green, but the repo gate is red at review time and must not ship red. | The vd-vcs lane fixes its clippy; re-run `typecheck` before handoff. | evidence log `.mpd/verify/evidence/ev-20261101T054420-ae107b.log`; `git status` (`?? crates/vd-vcs/src/runner.rs`) |

## What was verified as correct (functional deliverable)

- A1g: every overlay claim goes through the real `OwnerArbiter`; the arbiter's
  `assertSingleOwner` (contiguous partition, no gap/overlap) passes over the
  whole document and 4 known ranges; the paint plan is a single-engine
  partition of the requested range with overlay precedence and a per-segment
  agreement check against the arbiter; the clipping path is exercised
  (`raw-overlap=663 > 0`).
- A1h: `setEnabled(false)` releases every claim synchronously (181→0), the base
  owner owns everything afterwards, disabled injection is a no-op, 100
  enable/disable cycles survive, and the `SyntaxService` tree-sitter switch
  disables the provider (`NoProvider`, no claims) and restores to `Active`
  without a crash.
- C12: incremental edits stay equal to a full rebuild of the same text
  (812=812 intervals), scrolling replaces the claim set and never claims
  outside the current window (50 windows), rebuild median 108–110 ms (≤150)
  and single-injection p95 0.070 ms (≤2) on the recorded machine above.
- Evidence labels agent-doable vs user-only (AC-0.5) are present and honest:
  window screenshots and real-editor scrolling feel are marked **not done /
  user-only**.

## Not verified / bounds

- **GUI screenshots and real-window behaviour** (open/close overlay, visual
  colour difference after disabling tree-sitter, scrolling frame feel) are
  user-only with a display server; the lane correctly declares them undone.
- Real **KSyntaxHighlighting** span geometry is not part of the lane's test
  (synthetic base stand-in, F4); the editor canvas does not yet consume
  `buildPaintPlan()` (declared out of this lane's write scope), so A1g at the
  product-pixel level is not reachable from this evidence.
- The perf document is synthetic (1,051 lines / 25 KB, declared in §6.1);
  numbers scale with document size and the lane honestly records the 1.5k-line
  boundary.
