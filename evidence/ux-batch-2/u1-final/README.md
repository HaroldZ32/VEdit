# U1 — B3 final two state bugs (N2 pre-subscription window, N3 in-flight pass) + three carried nits

- **Task:** handover `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U1, findings
  `evidence/ux-batch-2/review-b3-r3/REPORT.md` (High **N2-r**, Medium **N3-p**, three nits).
- **Contract:** `.mpd/plans/ux-batch-2/00-plan.md` §0.1 R2/R3 (BINDING): project pass = UPPER,
  per-file/LSP = LOWER, ONE verdict per file, and the automatic pass NEVER shows a modal.
- **Write scope:** `src/workspace/diagnostics/**` + this directory. Nothing else was edited by
  this lane.
- **Verdict of this document:** both regressions are red before the repair and green after it;
  the three targeted suites are green with unchanged counts (the wiring suite grew by the 10 new
  checks). The full 7-gate run was deliberately not executed (captain's rule for repair lanes);
  three extra diagnostics-lane binaries were run instead.

## 0. Environment facts

| fact | value |
|---|---|
| workspace HEAD | `4e2d0a5` (branch `main`; the batch is a mixed uncommitted tree) |
| build/run image | `vedit-qt6-build:local` = `b1f16395a177` (pinned Qt6/KF6 image) |
| build tree | `target/qt6-build` (pre-existing tree; `cmake --build --target …` = incremental) |
| run command shell | `docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 vedit-qt6-build:local bash -lc '<cmd>'` (rootless daemon, so no `--user`) |
| test env | `QT_QPA_PLATFORM=offscreen`, `XDG_RUNTIME_DIR=/tmp/xdg-runtime`, `VEDIT_QUIET=1` |
| verible | `.mpd/recon/verible/bin/verible-verilog-{ls,lint}` (as passed by the CMake test definitions) |
| edited source hashes | `diagnostics_integration.cpp` `0277f3ac72af…d43216`; `selftests/b3_wiring_e2e.cpp` `2d95bd5dde97…407dd` |

## 1. What changed

### `src/workspace/diagnostics/diagnostics_integration.cpp`

1. **N2-r — subscribe on the open seam.** `ActiveFileWatcher` now connects
   `ApplicationShell::fileOpened` (the ONE open seam, already consumed by the project lane) and
   calls a new `trackOpenedPath(path)`, which tracks that tab's document immediately instead of
   waiting for the 400 ms poll. The first keystroke therefore reaches `textChanged` even when the
   tab is switched away at once.
2. **N2-r — dirty check for EVERY newly tracked document.** `trackDocument()` now calls
   `bufferModified()` for each document it tracks for the first time (both the KTextEditor and the
   `QPlainTextEdit` branch) and calls `controller_->markFileEdited(path)` when the buffer is
   already modified. This is the fallback for a document edited before anything could subscribe
   (restored tab, or an open path that never raised the seam). The old code consulted the dirty
   flag only for the file that happened to be ACTIVE at poll time — the review's exact hole.
3. **N3-p — cancel the in-flight pass in `applySettings()`.** `BackendWiring::applySettings()`
   now calls `controller_->cancelProjectPass()` while `projectPassRunning()`, **before**
   `clearProjectDiagnostics()` — the same order `followProjectRoot()` already used. Clearing the
   store alone left `passRunning_`/`passJob_` alive, so the old engine's job kept running, later
   wrote uncovered rows, and refused the next explicit Lint Project as "already running".
4. **nit (a) — split-pane fix target.** `applyDiagnosticFix()` now requires
   `pane->currentFilePath() == hoverFilePath_` before touching the active view's document. A focus
   change inside the popup's lifetime can no longer apply a fix computed for file A to file B.
5. **nit (b) — server hover placement.** `renderHover()` adopts the active view
   (`adoptHoverView()`) before creating/placing the popup, so a late answer after a tab switch is
   not placed in a bubble still parented to the old (hidden) view.
6. **nit (c) — automatic trigger during load.** The automatic pass's result sink is silent while
   the project file set is still loading (`projectFiles_` empty **and** a root is known — the same
   "pending" predicate `publishProjectState()` uses, and the distinction the explicit
   `lintProject()` message already made). A save in that window no longer blames the user with
   "no project file set".

### `src/workspace/diagnostics/selftests/b3_wiring_e2e.cpp` (test-only additions, +10 checks)

* new fixture `e.sv` in `project1`, never opened before its regression;
* `FakeProjectLint::holdNextPass` — the next pass stays RUNNING (`poll()` pending) until it is
  cancelled, so the N3 switch provably lands in flight;
* **N2-r block**: open → edit → switch away in ONE synchronous turn (no event loop runs, so the
  400 ms poll cannot see `e.sv` active), then assert the upper verdict is dropped and the dot is
  no longer clean;
* **N3-p checks** inside the existing (real settings-watcher) switch block: a held pass is in
  flight when the settings file is rewritten, then assert the pass was cancelled and the next
  explicit Lint Project is accepted.

No other file was edited by this lane. Recent modifications elsewhere in the tree
(`translations/vedit_zh_CN.ts`, `packaging/runtime/selfcheck.sh`, `.mpd/team/**`, …) belong to
other lanes running in parallel, not to this one.

## 2. Regression proof (red before / green after)

Both logs come from the SAME test source (`b3_wiring_e2e.cpp` sha256 `2d95bd5dde97…`).
`logs/10-red-before-fix.log` was produced **before** the six source edits above (mtime
2026-11-06 10:07:58 +08, sha256 `d7916ac5bffa…`); `logs/20-green-after-fix.log` is the same
binary lineage after the edits.

### RED (before the repair) — `logs/10-red-before-fix.log`

```
CHECK PASS n2r.e-covered-before-open  upper=…/project1/a.sv,…/project1/b.sv,…/project1/e.sv,…/project1/rename.sv
CHECK PASS n2r.open-e
CHECK PASS n2r.view-e
CHECK PASS n2r.e-is-dirty
CHECK PASS n2r.e-switched-away-before-poll  active='…/project1/a.sv'
CHECK FAIL n2r.edit-before-poll-invalidates  upper=…/project1/a.sv,…/project1/b.sv,…/project1/e.sv,…/project1/rename.sv
CHECK FAIL n2r.e-no-longer-green  status=3
CHECK PASS n3p.pass-in-flight-before-switch  starts=1 running=1
CHECK FAIL n3p.in-flight-pass-cancelled  cancels=0 running=1
CHECK FAIL n3p.new-explicit-pass-accepted  starts=0 running=1 status='Project lint did not start: a project lint pass is already running'
SUMMARY checks=70 failures=4 skips=0
```

This is the review's N2-r symptom exactly: `e.sv` is covered by the finished pass, the tab is
opened, edited and switched away before the poll, and it keeps its UPPER (green, `status=3`)
verdict. N3-p shows the un-cancelled job (`cancels=0 running=1`) and the wrong "already running"
refusal.

### GREEN (after the repair) — `logs/20-green-after-fix.log`

```
CHECK PASS n2r.e-covered-before-open  upper=…/project1/a.sv,…/project1/b.sv,…/project1/e.sv,…/project1/rename.sv
CHECK PASS n2r.open-e
CHECK PASS n2r.view-e
CHECK PASS n2r.e-is-dirty
CHECK PASS n2r.e-switched-away-before-poll  active='…/project1/a.sv'
CHECK PASS n2r.edit-before-poll-invalidates  upper=…/project1/a.sv,…/project1/b.sv,…/project1/rename.sv
CHECK PASS n2r.e-no-longer-green  status=0
CHECK PASS n3p.pass-in-flight-before-switch  starts=1 running=1
CHECK PASS n3p.in-flight-pass-cancelled  cancels=1 running=0
CHECK PASS n3p.new-explicit-pass-accepted  starts=1 running=1 status='Linting project with verible-verilog-lint…'
SUMMARY checks=70 failures=0 skips=0
```

`e.sv` leaves the upper layer after the switched-away edit (`status=0`, dot-less rather than
green) and the new explicit pass is accepted.

## 3. Targeted suites (observed counts)

| suite | command | result | log |
|---|---|---|---|
| diagnostics wiring E2E | `ctest -R '^vedit-b3-wiring-e2e$'` | **1/1 Passed** (6.91 s); direct run `checks=70 failures=0 skips=0` | `logs/21-green-wiring-ctest.log`, `logs/20-green-after-fix.log` |
| lint experience | `ctest -R '^vedit-lint-experience-selftest$'` | **1/1 Passed** (0.51 s); direct run `checks=98 failures=0 skips=0` (unchanged) | `logs/22-green-lint-experience-ctest.log`, `logs/24-green-lint-experience.log` |
| rename E2E | `ctest -R '^vedit-rename-e2e$'` | **1/1 Passed** (1.16 s); direct run `checks=21 failures=0 skips=0 not-claimed=1` (unchanged) | `logs/23-green-rename-ctest.log`, `logs/25-green-rename.log` |

Baseline before the change (same image/tree): wiring `60/0/0`, lint-experience `98/0/0`,
rename `21/0/0 not-claimed=1` (`logs/00-baseline-*.log`). The wiring suite's +10 checks are the
N2-r/N3-p additions; no check was removed or weakened. Check-name diff between
`00-baseline-wiring.log` and `20-green-after-fix.log` (`LC_ALL=C comm`): **removed = 0,
added = 10** (`n2r.*` × 7, `n3p.*` × 3).

Extra diagnostics-lane binaries (rebuilt against the changed library, all green):

| suite | result | direct counter | log |
|---|---|---|---|
| `vedit-project-lint-e2e` | 1/1 Passed (1.54 s) | `checks=11 failures=0 skips=0` | `logs/26-project-lint-e2e{,-ctest}.log` |
| `vedit-diagnostics-product-seam` | 1/1 Passed (6.21 s) | `checks=16 failures=0 skipped=0` | `logs/27-product-seam{,-ctest}.log` |
| `vedit-b3-repair-api-selftest` | 1/1 Passed (0.03 s) | `checks=16 failures=0 skips=0` | `logs/28-b3-api{,-ctest}.log` |

Build commands (incremental, targeted — no full build/gate):

```sh
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 \
  vedit-qt6-build:local bash -lc \
  'cmake --build /vedit-build/target/qt6-build --target \
     vedit-b3-wiring-e2e vedit-lint-experience-selftest vedit-rename-e2e'
```

## 4. Bounds — what is NOT proven

* **The full gate was not run.** `build/gates/qt6-cxx.sh`, the 50-test ctest, `vedit-app
  --selfcheck` and the xcb smoke are the captain's final gate, not this lane's.
* **One transient SIGPIPE(141) was observed.** The FIRST post-fix wiring run exited `141` after
  `PROJECT-ROOT-CHANGED` (62 PASS checks printed, no failing check); its log was overwritten by
  the clean rerun before it was archived, so it is recorded here from the run output rather than
  as an artefact. After it, **11 further post-fix wiring runs were clean (70/0/0)**: 4 direct runs
  (including the one archived as `logs/20-green-after-fix.log`) and 7 `ctest` runs (including the
  6-attempt loop in `logs/29-wiring-ctest-repeat.log`). This matches the documented latent SIGPIPE
  in lane selftests that spawn real LSP children (`AGENT.md` §4; handover §4 "known noise"). It is
  not attributed to this change, but it is not proven unrelated either.
* **Nits (a) and (b) have no dedicated regression.** Both are defensive guards (split-pane focus
  change; a late server-hover answer) whose conditions the fake backends cannot produce
  deterministically. They are compile-verified and the existing hover/R2 checks remain green.
* **Nit (c) has no dedicated regression.** The window is an automatic save between install and
  the 50 ms project-model poll; the existing harness cannot hold the file-set poll back
  deterministically. Verified by construction + the existing `c8.*` automatic-pass checks, which
  stay green.
* **N3-p's new pass is driven through the FAKE project-lint backend** (as the whole wiring suite
  is); the real engine's pass path is covered by `vedit-project-lint-e2e` (11/11 green here).
  The switch itself is the product path: the real settings store + `SettingsWatcher` +
  `BackendSwitchState::apply()` (backend `verible`, lint `verible-verilog-lint`).
* A verifier reproducing the RED state must revert the six edits in §1 (the tree carries the
  whole batch uncommitted, so `git stash` cannot isolate them).

## 5. Evidence index

| path | content |
|---|---|
| `logs/00-baseline-wiring.log`, `00-baseline-lint-experience.log`, `00-baseline-rename.log` | pre-change suite runs (`60/98/21`) |
| `logs/10-red-before-fix.log` | the two regressions red on the pre-fix source (`failures=4`) |
| `logs/20-green-after-fix.log` | wiring suite green after the fix (`checks=70 failures=0`) |
| `logs/21-green-wiring-ctest.log`, `22-…`, `23-…` | official ctest runs of the three targeted suites |
| `logs/24-green-lint-experience.log`, `25-green-rename.log` | direct runs with the observed counts |
| `logs/26-…`, `27-…`, `28-…` | extra lane binaries (project lint E2E, product seam, B3 API) |
| `logs/29-wiring-ctest-repeat.log` | six consecutive `ctest -R '^vedit-b3-wiring-e2e$'` runs, all 1/1 Passed |
