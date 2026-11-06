# W6 — hierarchy / signal service (T7, AC-3.1 / 3.2 / 3.4 data side, AC-3.5)

Cell `cell-20261102T050548232Z`, board task **T7**; contract
`.mpd/cellwork/cell-20261102T050548232Z/contracts/hierarchy-service.md`.
Paths: `crates/vd-deeplint/src/hierarchy.rs` (service),
`crates/vd-ffi/src/{capi.rs,deeplint.rs}` + `include/vd_ffi.h` (ABI),
`crates/vd-ffi/tests/qt_abi_guard.{cpp,rs}` **ADDENDUM A-11**.

## What exists

* `vd_deeplint::elaborate(files, &HierarchyOptions) -> HierarchyReport` — runs
  `slang --top <top> --ast-json … --ast-json-source-info [-I…] [-D…] <files>` with
  **cwd = the project root**, parses the elaborated design and returns
  `instances: Vec<InstanceNode>` (path, module, file, 0-based line/column of the
  **instantiation site**) and `signals: Vec<SignalInfo>` (path, kind,
  declaration, `drivers`, `loads`, `chain`), plus `elapsed`, `peak_rss_kb` and
  `warning` (AC-3.5 oversize notice).
* C ABI (append-only): `vd_hier_start` / `vd_hier_poll` / `vd_hier_release` /
  `vd_hierarchy_free`, with `VdHierarchy`, `VdHierInstance(List)`,
  `VdHierSignal(List)`, `VdHierEndpoint(List)`, `VdHierOptions`.
  Jobs run on a worker thread (GUI never waits) and their ids live in their own
  range (`1 << 48`), disjoint from lint jobs (`1 << 32`) and service jobs (1).

## The actual data a module tree will consume (not a description)

From `01-c-abi-end-to-end.txt` (C ABI, real `slang` on the AC-3.0 fixture):

```json
instances: [{"path":"top","module":"top","file":"…/top.sv","line":2,"column":7},
            {"path":"top.u0","module":"mid","file":"…/top.sv","line":10,"column":6},
            {"path":"top.u1","module":"mid","file":"…/top.sv","line":11,"column":6}]
top.link: drivers: [{"path":"top.u0.q","file":"…/top.sv","line":10,"column":37,"kind":0}]
          loads:   [{"path":"top.u1.d","file":"…/top.sv","line":11,"column":24,"kind":1},
                    {"path":"top.link","file":"…/top.sv","line":15,"column":9,"kind":2}]
          chain:   [{"path":"top.u0.q","line":10,"kind":0,"detail":"port q (out)"},
                    {"path":"top.u0.q","line":12,"kind":2,"detail":"assign"},
                    {"path":"top.u0.state","line":9,"kind":3,"detail":"AlwaysFF"}]
```

`kind` codes: `0` instance-output-port, `1` instance-input-port,
`2` continuous-assign, `3` procedural. Positions are **0-based** (the editor's
model); the driver chain continues into the child instance, which is what
"jump to source" needs (AC-3.2). The same shape comes out of the service directly
(`03-service-report.txt`, `VD_HIER_REPORT_JSON=…`).

## Measurements (AC-3.5 bound: 30 s / 1 GiB)

| path | elapsed | peak RSS | evidence |
| --- | --- | --- | --- |
| service (`elaborate`, real slang) | **18 ms** | **19,720 KiB ≈ 19.3 MB** | `03-service-report.txt` |
| C ABI (`vd_hier_start` → `vd_hier_poll`) | **5 ms** | **19,592 KiB ≈ 19.1 MB** | `01-c-abi-end-to-end.txt` |

Peak RSS is sampled from `/proc/<pid>/status` `VmHWM` while the child runs (1 ms
poll), so it is the tool's own high-water mark, not the GUI process's.

Oversize behaviour (AC-3.5): above `max_files` / `max_instances` the run still
completes but `HierarchyReport::warning` (and `VdHierarchy.warning`) carries the
reason; `an_oversized_project_sets_the_warning_instead_of_failing_silently`
covers it.

## The Qt macro collision this ABI caused (fixed here)

The first generated header exposed `VdHierarchy.signals`, and `signals` is
`#define signals public` in Qt — every Qt TU including the header failed to
compile. Fixed by renaming to **`signal_list`**; see
`02-qt-macro-guard.txt` (real Qt6 headers: guard compiles, negative control
fails) and `04-a11-qt-guard.txt`. Two guards keep the class away:

* `crates/vd-ffi/src/capi.rs` unit tests — scan the generated header for field /
  parameter names that are Qt/Windows macros (`signals slots emit foreach
  interface near far small IN OUT min max`) with a synthetic negative control;
* `crates/vd-ffi/tests/qt_abi_guard.{cpp,rs}` (**ADDENDUM A-11**) — compile the
  tree's header together with `<QObject>`; the Rust wrapper prints a loud `SKIP`
  (with the command it would run) when Qt6 dev files are absent, so it can never
  green itself silently.

## Gates (quoted)

* `cargo test -p vd-deeplint -p vd-ffi` → **exit 0**, 11× `test result: ok`
  (`05-gate-tests.txt`).
* `cargo clippy -p vd-deeplint -p vd-ffi --all-targets -- -D warnings` → **exit 0**
  (`06-gate-clippy.txt`).
* `cargo clippy --workspace --all-targets -- -D warnings` in the CentOS 7 GTK
  build image → **exit 0** (`07-workspace-clippy.txt`; the workspace needs GTK,
  which the host lacks — the repository runs that gate in the pinned image). The
  file is now present: the original run's log had not been copied into evidence,
  and the new owner re-ran and re-captured it.
* Owner re-runs after the task was reassigned (new owner, same tree):
  `cargo test -p vd-deeplint -p vd-ffi` → `TEST_EXIT=0`,
  `cargo clippy -p vd-deeplint -p vd-ffi --all-targets -- -D warnings` →
  `CLIPPY_EXIT=0`, `rustfmt --check` on the touched files → `FMT_EXIT=0`;
  raw log `08-owner-verification.log`, analysis `08-owner-verification.md`.
* Every file this task touches is rustfmt-clean (`cargo fmt --all -- --check`
  still flags `crates/vd-config/src/{filelist,project}.rs` and
  `crates/vd-ffi/src/project.rs`, which belong to the in-flight T6/A-9 work, not
  to this diff).

## Bounds that are NOT proven

1. **Scale**: measured on the AC-3.0 fixture (2 files, 3 instances). The
   1k files / 10k instances bound (AC-2.14 / AC-3.5) is **not** exercised here;
   the service has a timeout + a JSON size cap + the oversize warning, but the
   large-fixture numbers are still owed by the integration/bound task.
2. **Packaging**: the `slang` binary already ships (T17 build); the hierarchy
   service adds no new artifact.
3. The elaboration uses the **CLI/AST-JSON** route proven by the spike. The
   slang-server LSP route (custom `slang.getScope` / `getDriversWithLocation`
   commands, evidence `w2-spike/06-…`) is **not** wired in this service; it stays
   a documented alternative for a future live-session backend.
4. Positions are whatever slang reports; a file whose content changed on disk
   between elaboration and the jump can be stale (the caller must re-run the job
   after an edit, which the module tree's refresh policy owns).
5. `vd_ffi.h` changed (new structs + the `signal_list` rename) — that is header
   drift by design; the file in the tree is the generated one.

## Owner verification after the reassignment (T7 → se-shell)

`08-owner-verification.md` records what the new owner re-checked independently:
the gates above, a mechanical append-only comparison of the generated header
(0 structs and 0 functions removed; the only pre-existing-struct field additions
are T3's), the guards re-run (Qt PASS, negative control FAIL, loud Rust SKIP), the
real data re-observed (3 instances / 14 nets, `top.link` driver/load with exact
file:line), and the flake record below.

**Intermittent timeout test — RECORDED PERMANENTLY; if it recurs, start here.**
Exact test: `a_hanging_elaboration_times_out_and_is_reaped`
(`crates/vd-deeplint/tests/hierarchy.rs`). The original failure was **one** run in
parallel, with the truncated panic `expected a timeout, got …`; **the failure
variant was never captured**, so the cause is not proven. On this tree it did
**not** reproduce: 100 runs at `--test-threads=8` and 100 runs under 4× concurrent
stress (`--test-threads=16`) — 0 failures. The
crate already documents the class for this pattern (`tests/fake_binary.rs`:
parallel write+exec of a fake binary can hit ETXTBSY), so the three
process-spawning test bodies now take the same `SERIAL` guard, and a non-timeout
outcome panics with the variant **plus** tool/elapsed/peak-RSS so a recurrence is
diagnosable. No retry, no weaker assertion: a run that does not time out still
fails. Post-change: 100 runs → 0 failures. This is a documented-class mitigation
plus a measured rate, **not** a proven root cause.

**Input half added.** `the_elaboration_command_passes_the_files_include_dirs_and_defines`
(a fake `slang` records its argv): asserts `-I<dir>`, `-DWIDTH=8`, `-DSIM`,
`--ast-json-source-info`, `--top top` and the project file set reach the tool — the
contract's "输入为工程文件集与 -I/-D" now has a direct test.

## Interim state worth recording (honest)

While this task was being finished, `crates/vd-ffi/src/project.rs` (T6/A-9,
se-project's in-flight file) did not compile its **test** target, so
`cargo test -p vd-ffi` could not run for a window. It was **not** touched here;
once A-9 landed the full gate ran green and the numbers above were re-taken
against that tree (`01-…`, `05-…`). The owner pass re-confirmed the full gate on
the current tree (`08-owner-verification.log`).
