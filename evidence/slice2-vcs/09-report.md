# T23 — read-only VCS panel (Git / SVN)

C9 header

- collected_at / HEAD / host / image ids: `00-context.txt`; every log repeats its
  own header.
- All checks run headless (`offscreen`); GUI interaction is not claimed.

## 1. Deliverable

| layer | file(s) | responsibility |
|---|---|---|
| Rust backend | `crates/vd-vcs/src/lib.rs`, `runner.rs`, `service.rs` | Git/SVN detection, porcelain/`svn status` parsing, branch + revision, **read-only gate**, async status job, per-file diff |
| C ABI | `crates/vd-ffi/src/vcs.rs` | `vd_vcs_status`/`vd_vcs_poll` (async), `vd_vcs_info` (branch/revision), `vd_vcs_diff`; only read entry points exist |
| Qt panel | `src/workspace/vcs/vcs_source.{h,cpp}`, `vcs_panel.{h,cpp}` | consumes `vd_vcs_*` only; branch/revision header, changed-file table (path + kind), single-file diff view, explicit non-repository empty state |
| stub seam | `StubVcsService` + `RecordingRunner` | the tests drive the panel with canned data and no process; production and stub share the same read-only gate |

## 2. Read-only proof (AC-S2.5)

- Every write subcommand is enumerated in `GIT_WRITE` / `SVN_WRITE` and refused by
  `ensure_read_only()` **before any process is spawned**; the allow lists are
  disjoint from the write lists (`allow_lists_are_disjoint_from_write_lists`).
- The production runner (`SystemRunner`) and the recording stub both call the same
  gate, so a permissive fake cannot pass the test.
- `02-readonly-proof.txt` records the lists, the gate call sites and the observed
  test results:
  - `every_git_write_subcommand_is_refused_before_spawn` — ok
  - `every_svn_write_subcommand_is_refused_before_spawn` — ok
  - `production_runner_gate_refuses_writes_without_spawning` — ok
  - `git_session_uses_only_read_only_subcommands` — ok
  - `svn_session_reads_status_info_and_diff` — ok
  - `stub_service_drives_the_panel_without_processes` — ok
- Qt contains **no process spawn and no git/svn invocation**; it calls only the
  C ABI (`07-ffi-ownership.txt`: `NO-HITS` for `QProcess`/`system`, `vd_vcs_*`
  call sites, no write entry point in the header).

## 3. Panel acceptance (AC-S2.5)

`04-selftest.log` — `WORKSPACE-VCS-SELFTEST ok=1 checks=24 failures=0`, including:

- `vcs.panel.empty-state.*` — non-repository empty state (backend `None`, note
  “not a Git or Subversion working tree”, no files, no write attempt);
- `vcs.panel.git.branch-and-revision` / `changed-file-count` / `file-row` /
  `added-row` — `Git feature/vcs-panel @ abc1234 · 2 changed file(s)`,
  `rtl/top.sv modified`, `rtl/added.sv added`;
- `vcs.panel.svn.backend-and-revision` / `file-count` — `Subversion trunk @ 42 · 1 changed file(s)`;
- `vcs.panel.diff.shown` / `diff.read-only` / `missing-is-a-note` — per-file diff
  rendered, a missing diff is a visible note (no crash);
- `vcs.panel.refresh.*` — refresh keeps the empty state / populates after a stub
  change;
- `vcs.ffi.info-empty-state` / `diff-empty-state` / `read-only-surface` — the C ABI
  itself reports the empty state and exposes no write call.

## 4. Gates and observed exits

| command | observed |
|---|---|
| `cargo test -p vd-vcs` | 6 lib tests + 9 read-only tests passed (`01-cargo-test.log`) |
| `cargo test --workspace` | **WORKSPACE-TEST-EXIT=0** (`08-cargo-workspace.log`) |
| `cargo clippy -p vd-vcs -p vd-ffi --all-targets -- -D warnings` | `CLIPPY-EXIT=0` (the 7 earlier errors are fixed: unused imports, `type_complexity`, `expect()` in the stub/test) |
| `cargo fmt -p vd-vcs -p vd-ffi -- --check` | `FMT-EXIT=0` |
| lane configure/build (`target/vcs-build`) | `CONFIGURE-EXIT=0`, `BUILD-EXIT=0` (`03-lane-build.log`) |
| `ctest --test-dir target/vcs-build` | 1/1 passed, `CTEST-EXIT=0` |
| `bash build/gates/typecheck.sh` | **RED — external**: the C++/Qt6 lane fails to link `src/syntax/provider/vedit_syntax_provider_tests` / `vedit_syntax_product_tests` with `undefined reference to TreeSitterSyntaxProvider::highlightSpans(...)` — in-flight tree-sitter provider work in `src/syntax/provider`, outside T23's write scope. The Rust clippy portion now compiles `vd-vcs`/`vd-ffi` cleanly (the blockers T21 reported are fixed). |

The typecheck gate is therefore **not claimed PASS**; `06-typecheck.log` keeps the
full output and `00-context.txt` shows the touched paths. This lane is not the
cause (no changes under `src/syntax/**`).

## 5. Honest notes / out of scope

- The lane is **not yet added** to `VEDIT_WORKSPACE_LANES` in `src/app/CMakeLists.txt`
  and `src/workspace/integration.cpp` has no `installVcs` call: both files are
  outside T23's declared write scope. The lane builds standalone, is ctest-green
  and is FFI-wired; the product wiring is a one-line list entry + integration
  call for the Lead/integration task.
- `vd_vcs_info` and `vd_vcs_diff` are synchronous C calls (they spawn a short
  local `git`/`svn` process); `vd_vcs_status` is an async job. This is within
  AC-S2.5 but is the one C2-sensitive area a reviewer may want to improve later
  (make info/diff jobs too).
- No commit/push; no files outside the T23 scopes were edited by this task
  (`crates/vd-ffi/include/vd_ffi.h` is the cbindgen-generated header, rewritten by
  `build.rs`, not hand-edited).

## 6. Evidence index

| file | content |
|---|---|
| `00-context.txt` | HEAD, host/image, changed paths |
| `01-cargo-test.log` | `cargo test -p vd-vcs` + clippy + fmt |
| `02-readonly-proof.txt` | write/allow lists, gate call sites, observed tests |
| `03-lane-build.log` | standalone lane configure/build + selftest + ctest |
| `04-selftest.log` | 24-check panel/stub/FFI selftest |
| `05-ctest.log` | lane ctest |
| `06-typecheck.log` | repository typecheck output incl. the external syntax-provider link error |
| `07-ffi-ownership.txt` | Qt uses `vd_vcs_*` only, no Qt spawn, header exports, backend abstraction |
| `08-cargo-workspace.log` | `cargo test --workspace` exit 0 |
| `09-report.md` | this report |
