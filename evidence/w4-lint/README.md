# T8 · lint-orchestration — evidence

Task: **T8 工程级 lint 编排：job+poll、底部进度、取消/重跑、去重、失败原因**
(cell `cell-20261102T050548232Z`, verifier loop `loop-20261102T063252-a1b8d2`).

The probe (`lint_orchestration_probe.cpp`) is also the lane's ctest entry
(`vedit-lint-orchestration-probe`): it drives the real `DiagnosticsController`
with a fake `ProjectLintBackend`, so no toolchain and no Rust service are needed
to prove the orchestration rules.

```sh
bash evidence/w4-lint/run.sh
```

## What was verified (observed)

| evidence | probe section | observed |
|---|---|---|
| `logs/10-probe.log` | merge (AC-2.13) | two inputs, **2 rows**: the same problem from LSP and CLI collapses to one row with origin **Both**, the CLI-only one is marked **CLI**, CLI severity is converted to the panel's LSP scale (`MERGE rows=2 both=1 lsp=0 cli=1`) |
| same | C8 snapshot (AC-2.6) | 5 files + 2 open files → the backend receives **3**; include dirs and defines from the project model are forwarded; a file opened DURING the pass does not change the running set; after the pass `cliDiagnostics=3`, merged display=3 |
| same | cancel/rerun (AC-2.8/2.9) | before `phase=Running done=20/100 running=1`; after `phase=Cancelled running=0 message=«已取消 — 20/100 文件» diagnostics=0`; 20 further polls append **nothing**; the job is released; a rerun finishes and delivers 3 rows |
| same | failure (AC-2.10) | unavailable tool: `tool=fake-verible stage=resolve message=«fake-verible: the lint tool could not be resolved»`; spawn failure: `phase=Failed tool=fake-slang stage=spawn message=«tool=fake-slang stage=spawn: the lint child could not be started»` |
| same | 1k interactivity + over-scale (C11, AC-2.14) | 1000 files: 27 poll ticks, **worst GUI tick 0 ms**, done=1000; 1500 files: `large pass: 1500 files — cancel is available (10/1500)` and the cancel lands in `Cancelled` |
| same | bottom strip (`lint_progress.*`) | hidden before a pass; running → cancel visible + `n/total`; cancel → `已取消 — …`; rerun enabled → rerun reaches 100 % and shows the finished sentence |
| `logs/11-ctest.log` | `ctest -R "vedit-workspace-diagnostics-selftest\|vedit-lint-orchestration-probe"` | **2/2 passed** (the existing lane self-test is unchanged — the new API is additive) |
| `logs/15-a15-red.log` / `15-a15-green.log` | **ADDENDUM A-15** rerun-after-first-failure | pre-fix controller copy: `rerun.request-recorded FAIL`, `A15 first run: … rerunAvailable=0 request_files=0`, `A15 rerun attempt: error=«no project lint pass has run yet»`, **8 failures**; in-tree fix: `rerunAvailable=1 request_files=4`, rerun `phase=3 diagnostics=3`, **62 checks / 0 failures**. Also `rerun.empty-request-{button-disabled,explained}` (no request → disabled + hint, never a dead button) |
| `logs/15-a15-ctest.log` | A-15 authoritative run | CMake-built `vedit-lint-orchestration-probe` exit 0; `ctest -R …` **2/2 passed** |
| product build | `bash scripts/build-qt.sh --no-test` | PASS (warnings-as-errors; the controller compiles with the real `vd_deeplint_batch_*` adapter) |

The probe's final line is `LINT-ORCHESTRATION-PROBE ok=1 checks=47 failures=0`
(47 checks, exit 0).

## What was NOT proved here (owner / next task)

1. **The real Rust batch service on real files** (`vd_deeplint_batch_*` spawning
   slang / verible-verilog-lint and the child being killed on cancel): the FFI
   adapter is compiled and linked, but the probe uses a fake so it runs without
   a toolchain. The Rust side has its own tests (`a_batch_reports_progress_and_
   delivers_its_result_once`, `a_batch_cancel_really_stops_it_and_delivers_no_
   results`); the two halves meet at the ABI.
2. **Mounting the strip in the main window / toolbar buttons** — T20's
   shell-integration work; this task provides the widget and its wires.
3. **The diagnostics panel consuming `displayDiagnostics()` / `cliDiagnostics()`**
   — T9/T20 wiring; `src/workspace/diagnostics/diagnostics_integration.*` is the
   backend-switch lane's file and was not touched.
4. **Visual appearance** of the strip (headless probe; no screenshot claim).
