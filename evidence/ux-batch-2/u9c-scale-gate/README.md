# U9c — a real-project-scale gate for the module catalog and the lint pass

Status: **added, green, and proven able to go red.** One new cargo integration
test file runs in the repository's own test gate; it generates a 1176-file design
and a 40,000-warning flood at test time, measures the real documents, and asserts
the U9a/U9b acceptance instead of a proxy.

---

## 1. The case and where the gate runs it

`crates/vd-deeplint/tests/scale_gate.rs` — two stable test ids:

* `the_scale_gate_catalog_survives_a_1176_file_design`
* `the_scale_gate_lint_survives_a_diagnostic_flood`

**Gate registration.** The crate's neighbouring integration tests (`catalog.rs`,
`real_slang.rs`, `engine_batch.rs`) are plain cargo integration tests; the
repository's `tests` / `gates` gates run them through `cargo test --workspace`
(`build/gates/tests.sh` and `scripts/gates.sh` → `gate_test`). This file follows
that same registration — no CMake entry was added, because the neighbouring Rust
tests are not registered in CMake either and a ctest wrapper would only create a
second place for the id to drift. The gate id that now covers this case is the
Rust workspace test gate (`gates`, whose ledger-visible run folds the test suite
in; `tests` is the human-facing copy of the same command).

**Nothing is committed as a fixture.** The design and the flood are generated
into a `tempfile::TempDir` by the test itself, deterministically:
`write_scale_project` (1176 modules, 40 signals each, instantiation chains of ≤10
so slang's own 128-instance limit and serde_json's nesting guard stay far away)
and `write_flood_project` (one module with 40,000 width-truncation assignments).

## 2. The numbers (green run, `scratch/scale-gate.log`)

| quantity | observed |
|---|---|
| generated files | 1176 (generation 27 ms) |
| measured `--ast-json` document | **319,209,717 bytes (304 MiB)** — above the old 256 MiB (268,435,456) catalog cap |
| catalog result | **1176 modules / 118 tops**, `failure=None`, warning = size note + file-count note |
| catalog wall | **24.1 s** (debug profile — the gate runs debug; the same walk was 0.5 s release in U9a) |
| catalog process own peak RSS | 13,696 KiB (`VmHWM`; the slang child's RSS is separate) |
| flood file | 40,000 warnings → `--diag-json` **36,492,247 bytes (34.8 MiB)** — 4.35× the old 8 MiB (8,388,608) lint cap |
| lint pass result | `Finished`, 1/1 files processed, **40,000 findings counted, 500 distinct retained, 39,500 dropped**, 0 failures |
| lint pass wall | 2.34 s |
| `cargo test --test scale_gate` total | **29.0 s** (the two cases are serialized by a mutex so the log numbers stay clean) |

## 3. What the assertions catch (not proxies)

* Catalog: `failure.is_none()` + `modules >= 1176` + **no "partial" in the
  warning** — a regression to the old byte cap (error/empty tree) fails the first
  two; a regression to a truncating budget fails the third.
* Catalog: a document above the 256 MiB threshold must be catalogued in full AND
  carry the size note — the size handling is exercised, not skipped.
* Lint: phase `Finished`, `done == 1`, `total_diagnostics == 40_000` (every
  finding counted), `dropped > 0` (the truncation path is really exercised),
  `diagnostics.len() <= DEFAULT_MAX_DIAGNOSTICS`, and the summary must contain
  `dropped` (the truncation is REPORTED, never hidden).
* Both: `assert_no_children` reads `/proc/self/task/*/children` after the case
  and fails if any child survives — a zombie or leaked tool process is caught.

## 4. Negative controls (each demonstrated on this machine)

### (i) Catalog: force the retention budget low → the gate goes RED

```
VEDIT_U9C_FORCE_RETAINED_NODES=10 cargo test -p vd-deeplint --test scale_gate \
  --offline -- --nocapture the_scale_gate_catalog_survives_a_1176_file_design
```

Observed (full log `scratch/negative-catalog-budget-red.log`, exit 101):

```
u9c-catalog: files=1176 ast_json_bytes=319209717 generation_ms=16 catalog_ms=20608 \
  modules=5 tops=1 own_peak_rss_kb=4808 forced_nodes=10 \
  warning="the elaborated design exceeded the catalog's 10 node retention budget: \
  the module tree is partial (5 module(s) kept); the elaborated AST JSON is 274 MiB \
  (over the 256 MiB size threshold); the project has 1176 files (limit 1000): \
  the module tree may be slow" failure=None
thread 'the_scale_gate_catalog_survives_a_1176_file_design' panicked at ...:
the module tree is incomplete at real scale: 5 of 1176 module(s); warning="..."
test result: FAILED. 0 passed; 1 failed
```

So the completeness assertion is load-bearing: a truncated catalog (the shape a
regressed budget produces) fails the gate, while the normal run is green.

### (ii) Lint: force the pass bound low → the truncation is asserted, not ignored

```
VEDIT_U9C_MAX_PASS_DIAGNOSTICS=100 cargo test -p vd-deeplint --test scale_gate \
  --offline -- --nocapture the_scale_gate_lint_survives_a_diagnostic_flood
```

Observed (full log `scratch/negative-lint-bound.log`, exit 0 — the RED condition
is the bound trip; the test asserts the truthful truncated result):

```
u9c-lint: warnings=40000 diag_json_bytes=36492247 generation_ms=5 pass_ms=2574 \
  phase=Finished files=1/1 total_findings=40000 distinct=100 dropped=39900 \
  collapsed=0 failures=0 forced_pass=100 \
  summary=slang — 1/1 files, 100 distinct diagnostic(s), 39900 dropped
test result: ok. 1 passed
```

The old (pre-U9b) behaviour on the same flood is the red this guards against —
measured on the pre-fix code at HEAD f1b8602 in a worktree
(`evidence/ux-batch-2/u9b-lint-flood/scratch/red-before.log`):

```
BEFORE: phase=Finished total_findings=0 distinct=0 failures=1 \
  summary=slang — 1/1 files, 0 diagnostic(s), 1 file(s) failed
```

The new test would fail that shape on `done`/`total_diagnostics`/summary
assertions (the file produced 40,000 findings; the old code delivered 0).

## 5. Gate-time honesty

* Added wall time to `cargo test --workspace`: **24.6–29.0 s** for the whole
  `scale_gate` file across runs (24.1 s catalog + 2.3 s lint + generation and
  the one measuring slang run). It is a separate test id, not folded into an
  existing tightly-timed one.
* The crate's whole suite with the new file: **198 passed / 0 failed /
  2 ignored** (`scratch/crate-suite.log`; the two ignored ones are the U9a/U9b
  fixture-driven E2Es). Neighbours unchanged (`scratch/neighbours.log`):
  `catalog.rs` 9 passed / 1 ignored (0.73 s), `engine_batch.rs` 7 passed /
  1 ignored (0.52 s), `real_slang.rs` 6 passed (0.01 s).
* Note on one transient run during evidence collection: a doctest compile once
  failed with "can't find crate for serde" because several lanes were building
  in the shared `target/` dir at the same time; the same command succeeded on
  rerun (and the doctest set is empty). This is a shared-target race, not a
  property of the new test — the final suite log quoted above is exit 0.
* It needs the real slang artifact, exactly like `real_slang.rs`
  (`VEDIT_SLANG`, else `build/slang/out/slang`), and fails loudly with the
  remediation message when it is missing — it never skips silently.

## 6. Bounds and what is NOT proven

1. The gate runs in **debug** profile (`cargo test --workspace`), which is why the
   304 MiB stream walk costs ~24 s. Release is ~50× faster (0.5 s) but the gate
   does not choose the profile.
2. The catalog case measures the document by running slang itself once before the
   catalog run (one extra execution, ~2 s) so the log carries the REAL byte size
   instead of a proxy. The design is deterministic, so the number is stable.
3. The negative controls are exercised through documented env switches
   (`VEDIT_U9C_FORCE_RETAINED_NODES`, `VEDIT_U9C_MAX_PASS_DIAGNOSTICS`) and are
   never set in the gate; the red run was performed manually and is recorded here.
4. The C++/Qt6 ctest suite is not involved: this case is Rust-side (the catalog
   and lint APIs it drives are Rust). Per the write scope, "run the new test plus
   its neighbours" was done; the full gate was not run.
5. The synthetic design is generated, not a real customer project. Its measured
   document (304 MiB) is above the real user's reported threshold (>256 MiB), so
   it exercises the same failure class with a known-good expectation.

## 7. Reproduce

```
# green gate case (≈29 s)
CARGO_HOME=$PWD/target/cargo-home cargo +stable test -p vd-deeplint \
  --test scale_gate --offline -- --nocapture

# negative controls
VEDIT_U9C_FORCE_RETAINED_NODES=10 ... -- the_scale_gate_catalog_survives_a_1176_file_design  # RED
VEDIT_U9C_MAX_PASS_DIAGNOSTICS=100 ... -- the_scale_gate_lint_survives_a_diagnostic_flood     # green + truncation
```
