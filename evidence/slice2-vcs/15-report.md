# T95 — wire the read-only VCS panel into vedit-app

C9 header

- collected_at / HEAD: each of `10-wiring-before.log`, `11-wiring-after.log`,
  `12-runtime-wiring.log`, `13-typecheck.log` carries its own header.
- Baseline and after are the same tree; only the wiring edits below were added.

## 1. Change (minimal, guarded, same style as the other lanes)

| file | change |
|---|---|
| `src/app/CMakeLists.txt:99` | `VEDIT_WORKSPACE_LANES` gains `vcs` (target name default `vedit_workspace_vcs`) |
| `src/workspace/integration.cpp` | guarded `#include "vcs/vcs_integration.h"` and a guarded `installVcs(shell, options);` at the end of `installPanels()` — nothing else touched |
| `src/workspace/vcs/vcs_integration.{h,cpp}` | **new** app-facing adapter (`installVcs`): sets the `"vcs"` panel factory, builds the panel with `FfiVcsSource` (read-only C ABI) and the project root |
| `src/workspace/vcs/CMakeLists.txt` | adds `vcs_integration.cpp` and links `vedit_app_shell` only when that target exists; standalone lane builds unchanged |

Why the two extra lane files: the lane had no `<lane>_integration.cpp`, and the
authorized guarded include cannot compile without it. They live inside the lane's
own directory (T23's scope) and follow the palette/search/diagnostics convention.
`src/app/app_shell.cpp`, `src/syntax/**`, `src/editor/**` and every other lane were
not touched.

## 2. Before → after (same commands)

| check | before (`10-wiring-before.log`) | after (`11-wiring-after.log`) |
|---|---|---|
| `ctest -N` | **19** tests | **20** tests; new `#20 vedit-workspace-vcs-selftest` |
| `nm -C vedit-app \| grep -c 'vedit::workspace::vcs'` | **0** | **73** (incl. `vedit::workspace::installVcs(…, IntegrationOptions const&)`, `vcs::VcsPanel::selectFile/refresh`, `FfiVcsSource::info/status/diff`, `backendName`, …) |
| `strings -el vedit-app \| grep vcs-…` | **0** | `vcs-panel`, `vcs-header`, `vcs-empty-state`, `vcs-files`, `vcs-diff` |
| normal start (`--project … --hold 200`) | no VCS line | `vcs: installVcs projectRoot='/…/tests/fixtures/verilog-project'`, `APP-EXIT=0` (`12-runtime-wiring.log`) |
| root `ctest --test-dir target/qt6-build` | 19/19 | **20/20 passed**, `ROOT-CTEST-EXIT=0` |
| `bash build/gates/typecheck.sh` | RED (external `src/syntax/provider` link error) | **PASS**, `TYPECHECK-EXIT=0` (`13-typecheck.log`; the external issue was fixed by the owning lane, no change from this task) |

Note: `ctest` reports `Not Run` for a configured test until its executable is
built; `vedit-workspace-vcs-selftest` was built explicitly
(`VCS-TEST-BUILD-EXIT=0`) before the 20/20 run, as the other lane tests are built
by the normal full build.

## 3. Synchronous-call boundary (documented, not changed)

`14-sync-call-boundary.md`:

- `vd_vcs_status` is an async job (`vd_vcs_poll`).
- `vd_vcs_info` and `vd_vcs_diff` are **synchronous C calls** that spawn one short
  local `git`/`svn` subprocess on the caller's thread; the panel calls them on the
  GUI thread.
- AC-S2.5 (read-only) is satisfied; C2 is not fully met for those two calls (a
  slow/network working tree can stall the UI). The note includes a follow-up plan:
  add `vd_vcs_info_start/poll` and `vd_vcs_diff_start/poll` job arms in
  `vd-ffi/src/vcs.rs` + `service.rs`, let `FfiVcsSource` start/poll with a QTimer,
  extend the lane selftest with a non-blocking timing check. Not done in T95.

## 4. Scope / honesty

- Changed: the two authorized files + the new lane integration pair and one
  conditional line in the lane CMake; evidence under `evidence/slice2-vcs/`.
- No tests or gates were edited; no `src/app/app_shell.cpp` change; no
  `src/syntax/**`/`src/editor/**` change; no commit/push.
- The full build's typecheck is green at the time of this run; the earlier
  `TreeSitterSyntaxProvider::highlightSpans` failure is gone (the owning lane
  landed it) and is recorded as external in `06-typecheck.log`/`13-typecheck.log`.

## 5. Evidence

| file | content |
|---|---|
| `10-wiring-before.log` | 19 tests, 0 VCS symbols, 0 VCS strings |
| `11-wiring-after.log` | 20 tests, 73 symbols, UTF-16 objectNames, 20/20 ctest |
| `12-runtime-wiring.log` | `vcs: installVcs …` at normal startup |
| `13-typecheck.log` | typecheck PASS after wiring |
| `14-sync-call-boundary.md` | sync info/diff boundary + follow-up plan |
| `15-report.md` | this report |
