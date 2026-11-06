# U9d — a deep design must yield a catalog (JSON recursion bound) + the warning must reach the panel

Status: **fixed and verified**. A design nested past serde_json's default
128-value guard (the reviewer's depth-100 fixture, 793 MB) now yields a FULL
catalog (1176 modules) instead of 0 modules; a document past the safe cap is
refused with its measured depth; the module-tree panel renders the warning; both
READMEs document the shipped limits; the legacy `OutputTooLarge` message has no
shipped constructor.

---

## 1. H1 — the measured bound (why the old reader refused a NORMAL design)

Reproduced the reviewer's shape on my own fixture (1176 files, 20 signals,
`--segment 100` = a 100-level instantiation chain):

```
red-before: files=1176 modules=0 tops=0 failure=Some(AstJsonNestedTooDeep)   (scratch/red-before-deep-design.log)
```

Measured JSON nesting with an iterative byte scanner (`target/u9d_depthscan`,
string/escape aware, early-exit capable):

| fixture | instance levels | AST JSON bytes | **max JSON nesting** |
|---|---|---|---|
| depth-100 chain (1176 files) | 100 | 792,578,511 | **308** |
| shallow control (1176 files, `--segment 10`) | 10 | 297,912,513 | **38** |
| boundary chain (42 files, seeded for this task) | 42 | 3,417,770 | **134** |

`(308 − 38) / (100 − 10) = 3.0`: **one instance level costs exactly three JSON
levels** (`Instance` object → `body` object → `members` array), plus a base of
≈8. serde_json's default guard is **128 levels**, so it trips from
**≈42 instance levels** — a normal design depth, not a pathological one. The
boundary fixture above is the smallest real design that used to fail (134 > 128).

Arithmetic of a valid design, measured at both ends:

* slang itself refuses elaborations deeper than **128 instance levels**
  (measured: `fatal error: module instantiation exceeded maximum depth of 128`);
* therefore the instance-driven ceiling of a VALID design is
  `≈ 8 + 3 × 128 = 392 JSON levels`;
* expression/statement nesting inside a body is NOT bounded by that instance
  cap — which is why the fix measures the document instead of assuming a bound.

## 2. The chosen mechanism (approved `unbounded_depth`, gated per document)

**(a) The feature, minimal and proven.** `crates/vd-deeplint/Cargo.toml` now
declares:

```toml
serde_json = { workspace = true, features = ["unbounded_depth"] }
```

Proof that this stays inside the lane rule:
* `git status --porcelain Cargo.toml Cargo.lock rust-toolchain.toml` → **empty**
  (the skeleton-owned files are untouched);
* `cargo tree -p vd-deeplint | wc -l` → **27**, the same count as before the
  change (no crate is added; `unbounded_depth = []` in serde_json's manifest
  declares no dependency);
* feature unification makes `Deserializer::disable_recursion_limit()` visible
  crate-wide, but the ONLY caller is `crates/vd-deeplint/src/ast_prune.rs`
  (`prune_json`), i.e. the catalog's own deserializer.

**(b) The per-document gate — never a blanket unlock.**
1. `ast_prune::measure_nesting(reader, limit)` scans the document first:
   iterative, 1 MiB buffer, O(1) memory, string/escape aware, and it
   **early-exits above the cap** (measured 0.74 s over the 793 MB document);
2. `AST_JSON_MAX_NESTING = 10_000` (`crates/vd-deeplint/src/lib.rs`) is the
   named cap with the justification in its comment: the 392-level instance
   ceiling plus expression headroom ≈ **25×** the valid-design bound; a valid
   design cannot reach it;
3. `measured > cap` → `LintFailure::AstJsonNestedTooDeep { measured: Some(cap + 1) }`
   — the typed failure names the measured lower bound and the cap
   ("nests at least 10001 levels deep, past the catalog's 10000-level limit"),
   never an empty tree without a reason;
4. `measured ≤ cap` → `prune_json` calls `disable_recursion_limit()` and the
   parse runs on a thread with a **64 MiB stack** (`WALK_STACK_BYTES`).

**(c) The whole walk runs on that thread, not just the parse.** Measurement
during development found that a 3000-level chain overflowed the default 8 MiB
thread AFTER a successful parse: `collect_bodies` / `collect_instance_paths`,
the RM-4 path rewrite (`absolutize_ast`) and the deep `serde_json::Value`'s own
recursive `Drop` all recurse once per level. `build_catalog` now runs
scan-gated parse → path rewrite → catalog collection → **drop of the deep
value** inside the big-stack thread and returns only shallow results
(`Vec<CatalogModule>` + counters).

## 3. Red → green (the evidence)

| case | before U9d | after |
|---|---|---|
| boundary chain 42 (134 levels, real slang) | rejected (guard) | **42 modules / 1 top, failure=None** (`scratch/green-boundary.log`) |
| depth-100 (793 MB, 308 levels, real slang) | **0 modules / 0 tops** (`scratch/red-before-deep-design.log`) | **1176 modules / 12 tops, failure=None**, 6.7 s, own peak RSS 19,376 KiB (`scratch/green-deep-100.log`) |
| 298 MB full tree (1176 files) | already green (U9a) | **1176 / 118, failure=None**, 2.7 s, own peak 21,944 KiB (`scratch/green-med-298mb.log`) |
| 1.16 GB full tree | already green (U9a) | **1176 / 118, failure=None**, 12.1 s, own peak 45,416 KiB (`scratch/green-heavy-1.16gb.log`) |
| synthetic chain 3000 (≈9003 levels, just below cap) | n/a | **3000 modules**, walk thread did NOT die (in-suite test) |
| synthetic chain 3400 (≈10,203 levels, above cap) | n/a | `AstJsonNestedTooDeep { measured: Some(10001) }`, process survives (in-suite test) |

The two synthetic cases are in `crates/vd-deeplint/tests/catalog.rs`
(`a_chain_just_below_the_nesting_cap_completes_on_the_walk_thread`,
`a_document_above_the_nesting_cap_is_refused_with_its_depth`,
`a_chain_just_past_serde_json_s_default_guard_is_catalogued`).

## 4. Proof conditions (a)–(e) from the captain

| # | demand | evidence |
|---|---|---|
| a | boundary case just past 128 yields a FULL catalog | §1 fixture + §3 row 1 (134 levels → 42/42 modules) |
| b | depth-100 (308) yields a FULL catalog | §3 row 2 (793 MB → 1176 modules, no warning about partial) |
| c | >cap yields the typed failure AND the walk does not die at the cap | §3 rows 5–6, in-suite; both run in the normal `cargo test` |
| d | memory bound holds + honest stack accounting | §5 |
| e | node budget + existing tests stay green | §8 (209 passed / 0 failed / 3 ignored); node budget code untouched |

## 5. Memory bound accounting (condition d)

* The catalog's retained structures are still bounded by the **node budget**
  (`DEFAULT_MAX_RETAINED_NODES = 1,000,000`); nothing about that changed.
* Own peak RSS (`VmHWM`, measured inside the test) on the deep documents:
  **19,376 KiB** (793 MB), 21,944 KiB (298 MB), 45,416 KiB (1.16 GB).
* **Honest accounting:** the growth above U9a's 12.7 MiB baseline is NOT the
  document and NOT the stack. It is the per-file byte→char cache added by
  **RM-4b** (`Columns` in `tool_paths.rs` keeps every source line's bytes): it
  scales with total SOURCE LINES — ≈4 MB for the 20-signal fixture, ≈6 MB for
  40 signals, ≈27 MB for 180 signals. That is another lane's feature; reported
  here as a cross-lane finding, not changed by this task.
* **Stack, reserved vs touched (measured):** running the 298 MB case with the
  walk thread's stack forced to **8 MiB vs 64 MiB** gave
  `self_peak_rss_kb = 21,960 vs 21,956` — a 4 KiB difference, i.e. the 64 MiB
  reservation is virtual only and the recursion's touched pages are noise at
  real depths (308 levels ≪ the cap).
* The scanner adds a constant 1 MiB buffer, independent of the document.

## 6. M1 — the warning reaches the module-tree panel (Qt selftest, executed)

`src/workspace/module_tree/module_tree_selftest.cpp` gained two checks; the lane
selftest was rebuilt and run inside the pinned Qt6 image (ctest id unchanged):

```
ok   panel.summary.shows-warning — slang · 6 module(s) · 3 top(s) · 5 ms ·
     19592 KiB peak · ⚠ the elaborated AST JSON is 274 MiB (over the 256 MiB size threshold)
ok   panel.summary.no-warning-on-clean-catalog — slang · 6 module(s) · 3 top(s) ·
     5 ms · 19592 KiB peak
PASS: 78 check(s), 0 failure(s)
ctest -R ^vedit-module-tree-selftest$ → Passed 0.04 s
```

The second check is the negative control: an error-free catalog renders no `⚠`
and no warning text. Log: `scratch/module-tree-selftest.log`.

## 7. M3 — the shipped limits are documented where a maintainer looks

* `src/workspace/module_tree/README.md` — new "Service caps (U9a/U9d)" block next
  to the catalog client: 1,000,000-node retention budget → PARTIAL + warning
  rendered in the panel's summary; 256 MiB size threshold → note only, never a
  cap; 10,000-level nesting cap with the typed refusal; 64 MiB stack (reserved
  virtual). The pre-existing 20,000-row / depth-64 panel guards stay as they
  were.
* `src/workspace/diagnostics/README.md` — new section "Lint volume limits (U9b):
  count bounds, not byte caps": streamed `--diag-json` (no 8 MiB byte cap),
  de-duplication key (file, line, column, code, message) with repeat counts,
  **500 distinct findings per file**, **20,000 distinct findings per pass**
  (`EngineOptions.max_pass_diagnostics`), and the pass summary wording
  (`N repeat(s) collapsed`, `N dropped`, "at least N" when a file failed) plus
  the named failed files.

## 8. N1 — `LintFailure::OutputTooLarge`

Audit (`grep -rn OutputTooLarge crates/ src/`): the variant is defined in
`lib.rs`, rendered by `user_message`/`Display`, mapped defensively in the
catalog's `failure_message`, and referenced by one lib unit test. **There is no
constructor anywhere in the tree** — the lint path's byte check was removed by
U9b and the catalog never produced it after U9a. So no shipped path can construct
it for the catalog (or lint), and the legacy "lint diagnostics" wording cannot
surface. Kept for API compatibility; nothing to kill.

## 9. Test-robustness fix found while proving (no assertion weakened)

The extra catalog tests made a pre-existing race reachable: a fake `slang`
script written by one test thread and exec'd by another fails with
`Spawn { "Text file busy" }` when a fork inherited the script's write
descriptor. `crates/vd-deeplint/tests/catalog.rs` now serializes its spawning
tests (`SPAWN_SERIAL`), documented in the test file; 3 consecutive catalog-suite
runs are green (`run1/2/3: 13 passed, 2 ignored`).

## 10. Gates (quoted)

```
CARGO_HOME=$PWD/target/cargo-home cargo +stable test -p vd-deeplint -p vd-ffi --offline
# 209 passed; 0 failed; 3 ignored   (scratch/cargo-test.log)
CARGO_HOME=$PWD/target/cargo-home cargo +stable clippy -p vd-deeplint -p vd-ffi \
  --all-targets --offline -- -D warnings          # exit 0   (scratch/clippy.log)
CARGO_HOME=$PWD/target/cargo-home cargo +stable fmt -p vd-deeplint -p vd-ffi -- --check
                                                  # exit 0
```

The full workspace gate was not run (captain's job). Only the
`vedit-module-tree-selftest` target was built in the Qt lane (the M1 case); the
rest of the C++ lane was not rebuilt by this task.

## 11. Bounds / not proven

1. The deep fixtures are generated (1176 files at 20/40/180 signals; the 42-file
   boundary chain; the synthetic 3000/3400 chains), not a real customer design.
   The reviewer's own depth-100 shape is reproduced; the byte size and nesting
   are measured, not assumed.
2. The >cap refusal is exercised by a synthetic 10,203-level document; a valid
   design cannot reach it (392-level instance ceiling), so this path is a guard,
   not a product workflow.
3. Expression nesting is not bounded by slang's instance cap; the per-document
   scan is what bounds it. A document of ≤10,000 levels is proven safe on the
   64 MiB thread by the 3000-instance test; a deeper-but-under-cap expression
   nesting is covered by the same bound, not by a second fixture.
4. The own-RSS growth above 12.7 MiB comes from RM-4b's per-file column cache
   (§5), not from U9d; fixing that is RM-4b's call.
5. The `Columns` cache also means the catalog's own peak scales with total source
   LINES (≈45 MiB at 180 signals/module). It is still document-content-sized,
   not document-byte-sized.

## 12. Reproduce

```
# boundary fixture + green run
python3 evidence/ux-batch-2/u9a-catalog-scale/gen_design.py \
  --out evidence/ux-batch-2/u9d-depth/boundary --files 42 --signals 2 --segment 42
VEDIT_U9D_BOUNDARY_DIR=$PWD/evidence/ux-batch-2/u9d-depth/boundary \
  target/release/deps/catalog-* --ignored --nocapture \
  the_boundary_depth_design_below_the_default_guard_is_catalogued

# depth-100 / 298 MB / 1.16 GB (fixtures from the U9a evidence directory)
VEDIT_U9A_BIGDIR=$PWD/evidence/ux-batch-2/u9a-catalog-scale/bigdesign-deep \
  target/release/deps/catalog-* --ignored --nocapture \
  the_1176_file_design_yields_a_full_catalog

# panel warning (pinned image)
ctest --test-dir target/qt6-build -R '^vedit-module-tree-selftest$' --output-on-failure
```
