# T7 — owner verification (new owner, after the dw-rust handoff)

The task was reassigned after `dw-rust` was fenced. This file records what the new
owner **re-ran and checked itself**, with the raw command log next to it:
`08-owner-verification.log` (gates, data, flake rate, guard skip) and
`07-workspace-clippy.txt` (the workspace gate file the original README already
referenced but that had not been copied into evidence).

The build itself is the handoff's: `crates/vd-deeplint/src/hierarchy.rs`,
`crates/vd-ffi/src/{capi.rs,deeplint.rs}` + `include/vd_ffi.h`, and the A-11 guard.
Nothing here changes the ABI; the only code change this pass made is in
`crates/vd-deeplint/tests/hierarchy.rs` (see §4).

## 1. Gates re-run on the current tree

```
cargo test -p vd-deeplint -p vd-ffi                          TEST_EXIT=0
  38 + 4 + 13 + 6 + 6 + 2 (vd-deeplint) + 43 + 12 + 1 (vd-ffi) passed, 0 failed
cargo clippy -p vd-deeplint -p vd-ffi --all-targets -- -D warnings
                                                             CLIPPY_EXIT=0
cargo clippy --workspace --all-targets -- -D warnings        WORKSPACE_CLIPPY_EXIT=0
rustfmt --check (hierarchy.rs, lib.rs, tests/hierarchy.rs,
                 capi.rs, deeplint.rs)                       FMT_EXIT=0
```

The task's `cargo test -p vd-ffi` half is green again; the earlier window in which
`crates/vd-ffi/src/project.rs` (T6/A-9, another lane) did not compile its test
target is over. That file was **not** touched by T7 at any point.

## 2. The ABI is append-only (checked mechanically, not by reading a diff by eye)

Comparing the declaration sets of `git show HEAD:crates/vd-ffi/include/vd_ffi.h`
with the tree header:

* typedef structs: 29 → 41, **0 removed**; the 9 new `VdHier*` are T7's, the 3
  others (`VdLintBatchOptions`, `VdLintProgress`, `VdLspCapabilities`,
  `VdLspCapability`) belong to T3's engine layer.
* exported functions: 44 → 62, **0 removed**; T7 adds `vd_hier_start`,
  `vd_hier_poll`, `vd_hier_release`, `vd_hierarchy_free`.
* pre-existing structs that gained fields: `VdDiagnostic` (`file`, `key`,
  `origin`), `VdLspStatus` (`backend`, `backend_name`), `VdLspDiagnostic`
  (`origin`), `VdProjectInfo` (`relative_files`, `missing_variables`,
  `skipped_lines`) — all **additions only**, and all from T3's dedup/backend/
  filelist work, not from T7. No T7 edit changes an existing field or function.

The `signals` → `signal_list` rename is inside T7's own new struct, so it is not
an ABI break for anything that existed before this task.

## 3. Real data, re-observed (not just "the struct compiles")

Re-ran both data paths on the AC-3.0 fixture (`top` instantiates `mid` twice; the
signal is driven in `mid`, loaded in `top`):

```
vd_deeplint real tool : elapsed_ms=7,  peak_rss_kb=19104
C ABI job             : 281474976710656, 3 instance(s), 14 net(s), 8 ms, 19052 KiB
```

Instance paths and the signal endpoints (0-based, from the same run):

| what | value |
| --- | --- |
| instances | `top` (top, top.sv:2:7), `top.u0` (mid, top.sv:10:6), `top.u1` (mid, top.sv:11:6) |
| `top.link` drivers | `top.u0.q` — top.sv:10:37, kind 0 (instance-output-port) |
| `top.link` loads | `top.u1.d` — top.sv:11:24, kind 1 (instance-input-port); continuous assign — top.sv:15:9, kind 2 |
| `top.link` chain | `top.u0.q` (port) → `top.u0.q` (assign, mid.sv:12) → `top.u0.state` (AlwaysFF, mid.sv:9) |

The exact JSON is in `01-c-abi-end-to-end.txt` (C ABI) and `03-service-report.txt`
(service). Run-to-run variance is milliseconds (5–33 ms across runs, same ballpark
RSS ≈ 19 MB); the 30 s / 1 GiB bound is not close on this fixture.

## 4. The intermittent timeout test — recorded, hardened, not retried

Test: `a_hanging_elaboration_times_out_and_is_reaped` (`crates/vd-deeplint/tests/hierarchy.rs`).
The handoff recorded **one** failure in a parallel run ("expected a timeout, got …");
the failure variant was not captured, so the cause is not proven.

Measured rate before any change, on this tree (the failure did **not** reproduce):

* 100 runs of the `hierarchy` test binary at `--test-threads=8`: **0 failures**
* 100 runs under stress (4 concurrent lanes × 25 runs, `--test-threads=16`): **0 failures**

The only non-timeout outcomes this test can see are a spawn failure or the child
exiting before the 200 ms deadline, and the crate already documents the class it
observed for the same pattern: `tests/fake_binary.rs` serialises its test bodies
because "running them in parallel can hit ETXTBSY (another thread still holds a
write descriptor when a child execs the file)". The hierarchy test writes an
executable script and execs it, in the same parallel-test arrangement.

Hardening applied (the only code change in this pass):

* the three process-spawning test bodies take the same `SERIAL` mutex pattern
  (static `Mutex` + poisoned-recovery guard), so the ETXTBSY class cannot occur;
* the duplicate `#[test]` attribute on the real-tool test was removed;
* a non-timeout outcome now panics with the variant **plus** the tool name, the
  elapsed time and the child's peak RSS, so a recurrence is diagnosable from the
  log instead of a bare variant name;
* **no retry and no weakened assertion**: a run that does not time out still fails.

After the change: 100 runs at `--test-threads=8` → **0 failures** (`08-…log`).
This is a mitigation of a documented class plus a measurement, **not** a proven
root cause — if it recurs, the new panic text identifies the variant.

Also added in this pass (`the_elaboration_command_passes_the_files_include_dirs_and_defines`):
the contract's input half had no direct test. A fake `slang` records its argv and
exits 1; the test asserts `-I<dir>`/`-DWIDTH=8`/`-DSIM` are single argv tokens,
`--ast-json-source-info` and `--top top` are passed, and the project file set
reaches the tool. The first version of this test used `iter().any(...)` where
clippy wanted `contains(&…)`; the gate caught it and it was fixed
(`CLIPPY_EXIT=101` during the pass, green afterwards).

## 5. The A-11 guard behaves as approved (re-run by the new owner)

* tree guard TU against the real generated header + `<QObject>` in
  `vedit-qt6-build:local`: `GUARD_EXIT=0`;
* negative control (a field named `signals`): `NEGATIVE_CONTROL_EXIT=1` — the
  guard is not vacuous;
* Rust wrapper where Qt dev files are absent: **loud SKIP with the exact command**
  (quoted in `08-owner-verification.log`, section 7), never a silent pass. It
  compiles the header **in the tree**, not a copy.

Limitation this pass does **not** fix: the guard is not wired into the Qt lane's
CMake, so a plain Qt build does not run it every time. Wiring it needs
`src/app/CMakeLists.txt`, which is outside T7's write scope (recorded for T17 /
shell-integration).

## 6. Bounds still not proven (unchanged from the handoff)

1. **Scale** — measured on the AC-3.0 fixture (2 files, 3 instances). The 1k
   files / 10k instances bound (AC-2.14 / AC-3.5) is not exercised; the service has
   a timeout, a JSON size cap and the oversize warning, but no large-fixture number.
2. **Packaging** — the `slang` binary is T17's build artifact; the hierarchy
   service adds no new artifact of its own.
3. **LSP route not wired** — the service uses the spike-proven CLI/AST-JSON route.
   The slang-server LSP commands (`slang.getDriversWithLocation` …) are proven by
   the spike but not consumed here; the module-tree UI consumes this service.
4. **UI consumer absent** — the C++ side that will read `signal_list` belongs to
   the module-tree lane; this task delivers the data seam only.
