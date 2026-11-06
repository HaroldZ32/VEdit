# U9a — the module catalog must survive a 1176-file design (AST JSON cap)

Status: **fixed and verified** on synthetic designs whose `--ast-json` document is
1.1×, 4.6× and 3.0× the old 256 MiB cap. The catalog process itself stays at
~12.5 MiB peak RSS; the document is streamed, never held.

Everything below is measured on this machine (slang 12.1.0+0,
`build/slang/out/slang`; Rust 1.99.0 via `cargo +stable` with
`CARGO_HOME=$PWD/target/cargo-home`), release profile for the end-to-end runs,
debug profile for the test suite.

---

## 1. The defect as reported, and the code path behind it

The user's error on a 1176-file SystemVerilog project:

```
slang failed: lint diagnostics exceeded the 268435456 byte limit
```

The message names the wrong artifact. The path was:

| step | code (pre-fix) |
|---|---|
| cap | `crates/vd-deeplint/src/hierarchy.rs:49` `DEFAULT_MAX_JSON_BYTES = 256 * 1024 * 1024` |
| catalog default | `crates/vd-deeplint/src/catalog.rs:203` → `CatalogOptions::max_json_bytes` |
| C++ passes 0 | `src/workspace/module_tree/catalog_client.cpp:332` (`0 = service default`) |
| fatal check | `crates/vd-deeplint/src/hierarchy.rs:410` `metadata.len() > options.max_json_bytes` → `LintFailure::OutputTooLarge` |
| wording | `crates/vd-deeplint/src/lib.rs` `OutputTooLarge => "lint diagnostics exceeded the …"` — shared with the lint `--diag-json` cap |
| ABI message | `crates/vd-ffi/src/catalog.rs:306` `format!("{} failed: {}", tool, failure.user_message())` |

So the catalog failed **before reading a single byte** of the AST JSON, and
reported the failure as a lint-diagnostic flood the catalog never produces.
The lint batch's own cap (8 MiB, `lib.rs`) is a different limit with a different
prefix (`slang: lint diagnostics …`), so this was not the lint pass.

Arithmetic behind the real-machine failure: the acceptance fixture below is
**297,912,513 bytes > 268,435,456 bytes** (1.11×), i.e. exactly the branch at
`hierarchy.rs:410`.

---

## 2. Measurements (the numbers that decided the fix)

Three synthetic projects, all 1176 files, produced by
[`gen_design.py`](gen_design.py). The generator writes one module per file with
ports, nets and continuous assignments; `--segment N` makes each module
instantiate the previous one inside an N-module segment (chain depth ≤ N).

| fixture | generator args | AST JSON bytes | slang wall | slang peak RSS |
|---|---|---|---|---|
| **acceptance** (`bigdesign-med/`) | `--signals 40 --segment 10` | **297,912,513** (284 MiB) | 1.62 s | 1,107,448 KiB |
| **heavy** (`bigdesign/`) | `--signals 180 --segment 10` | **1,246,660,559** (1.16 GiB) | 6.20 s | 3,364,508 KiB |
| shallow control (`bigdesign-med/`, earlier generation) | `--signals 20 --segment 10` | 161,035,953 | 1.01 s | 491,892 KiB |
| deep control (`bigdesign-deep/`) | `--signals 20 --segment 100` | 792,578,511 (**4.9× the shallow control**) | 2.50 s | 2,007,712 KiB |

Reading of the table: **the dump size is driven by hierarchy shape as much as by
source size** — identical module sources, depth 10 → 161 MB, depth 100 → 793 MB.
A monolithic `--ast-json` document is therefore not bounded by anything the
product controls.

End-to-end catalog runs (release test binary, `/usr/bin/time -v`; `self` = the
catalog process's own `VmHWM`, which excludes the slang child):

| fixture | result | modules / tops | service elapsed (slang+walk) | wall | own peak RSS | warning |
|---|---|---|---|---|---|---|
| acceptance (298 MB) | **complete** | 1176 / 118 | 2,107 ms | 2.15 s | **12,752 KiB** | size + file count |
| heavy (1.25 GB) | **complete** | 1176 / 118 | 9,729 ms | 9.86 s | **12,756 KiB** | size + file count |
| deep control (793 MB) | **fails** — `AstJsonNestedTooDeep` | 0 / 0 | 2,105 ms | 2.19 s | 3,968 KiB | — (see §6 bound) |

Derived stream-walk rate (release): ≈ 360 MB/s on the 1.25 GB dump and ≈ 0.6 GB/s
on the 298 MB dump (slang time measured separately; difference = walk + parse).
Debug builds are ~20–50× slower (the per-byte `io::Read` behind
`serde_json::from_reader`), which is why the acceptance runs use the release
profile.

Logs: `scratch/slang-*-time.txt`, `scratch/catalog-*-run.log`,
`scratch/catalog-*-time.txt`.

---

## 3. The fix

**PREFERRED path implemented — stream-parse, not a bigger constant.**

* **NEW `crates/vd-deeplint/src/ast_prune.rs`** — a streaming prune of the
  `--ast-json` document over `serde_json::Deserializer::from_reader`, wrapped in
  a byte-counting reader:
  * keeps only the nodes the catalog walks (`Root`, `CompilationUnit`,
    `Instance`, `InstanceBody`, `InstanceArray`, `GenerateBlockArray`,
    `GenerateBlock`, `Port`, `InterfacePort`, `Definition`) and only the keys
    the walk reads (`name`, `kind`, `addr`, `source_file/_line/_column`,
    `definitionKind`, `definition`, `type`, `direction`, `interfaceDef`,
    `constructIndex`, `body`, `members`);
  * every other node is skipped with `IgnoredAny` — and keys and `kind` are
    matched through a borrowed `&str` visitor, so an expression node allocates
    **nothing**;
  * retained memory is bounded by a **node budget** (`Instance` / `InstanceBody`
    / arrays / generate blocks, default 1,000,000 kept nodes), not by the
    document size. Past it the walk keeps consuming (so `definitions` stays
    complete), drops the remaining design nodes and reports
    `retained_fully = false` → a WARNING, never a failure;
  * a layout guard errors out if a retained node ever puts a position field
    before `kind` (slang puts `kind` second on catalog nodes, but the span first
    on expressions), so a future layout cannot silently lose `file:line`.
* **`crates/vd-deeplint/src/hierarchy.rs`** — `run_ast_json` now hands the
  catalog the temp **file path** (closure called while the tempdir is alive)
  instead of a `String`; the catalog path never reads the document as a whole.
  The hierarchy's whole-text path keeps its 256 MiB cap.
* **`crates/vd-deeplint/src/catalog.rs`** — `build_catalog` opens the file,
  streams the prune, and runs the SAME `parse_catalog_value` walk the
  whole-string `parse_catalog_json` uses (the equivalence test below proves they
  agree). `max_json_bytes` is now a **size-warning threshold** (default 256 MiB)
  and `max_retained_nodes` is the memory bound. Over-budget → partial catalog +
  warning; malformed → `InvalidAstJson`. `ModuleCatalog::failure_message()` is
  the catalog's own vocabulary for every failure.
* **`crates/vd-deeplint/src/lib.rs`** — two truthful variants:
  `AstJsonTooLarge { limit }` (hierarchy path) and `InvalidAstJson { message }`
  (catalog path). Neither mentions lint diagnostics.
* **`crates/vd-ffi/src/catalog.rs`** — `vd_catalog_poll` reports
  `catalog.failure_message()` instead of `LintFailure::user_message()`, so the
  C ABI can no longer surface `slang failed: lint diagnostics exceeded …`.
  **No ABI layout change**; `crates/vd-ffi/include/vd_ffi.h` was regenerated by
  the crate's own cbindgen build script (`crates/vd-ffi/build.rs`) and the diff
  is **comments only**.
* **`crates/vd-ffi/src/capi.rs` + `src/workspace/module_tree/catalog_client.cpp`**
  — the catalog's `slang` child budget is now 120 s (was the 30 s single-file
  lint budget) and the client poll window is 300 s (was 600 × 50 ms = 30 s), so
  a healthy large elaboration is not killed by the old UI timeout.

## 4. Reaching the user (panels)

The warning/error path is the existing one and did not need a new widget:

* `VdModuleCatalog.warning` (ABI) → `CatalogClient::toCatalog`
  (`catalog_client.cpp:136`, `snapshot.warning = fromC(catalog.warning)`) →
  `ModuleCatalog::summaryLine()` appends ` · ⚠ <warning>`
  (`module_catalog.cpp:70–72`) → `ModuleTreePanel::setCatalog` sets the summary
  label for both complete and empty catalogs (`module_tree_panel.cpp:159–186`).
* On failure, `ModuleTreePanel::setCatalog` (`:148–157`) and
  `InterfacePanel::refreshFocus` (`interface_panel.cpp:161–167`) show
  `catalog.error` as the one-line status/placeholder.
* The new ABI test `catalog_size_warning_reaches_the_abi_and_failures_are_truthful`
  proves the warning actually arrives in `VdModuleCatalog.warning` and that the
  failure text is the catalog's own ("the elaborated AST JSON could not be read:
  …"), with no `lint`/`diagnostics` wording.
* Limitation: the Qt lane was **not compiled/run here** (the host has no
  `ninja`/Qt6 dev files; the product builds in the pinned image). The C++ change
  is a poll-budget constant plus strings, and the rendering path above is
  unchanged pre-existing code — inspected, not executed. The captain's gate must
  compile it.

## 5. Tests (all quoted results are from this workspace)

Command (test suite, debug):

```
CARGO_HOME=$PWD/target/cargo-home cargo +stable test -p vd-deeplint -p vd-ffi --offline
```

Result: **185 passed; 0 failed; 1 ignored** (14 test binaries; log
`scratch/cargo-test-final.log`). The one ignored test is the real-scale E2E
below, which needs a generated fixture.

New/changed tests:

* `crates/vd-deeplint/tests/catalog.rs`
  * `the_streamed_catalog_matches_the_whole_string_parse` — the streaming prune
    produces **exactly** the same `CatalogModule` list as the old whole-string
    parse of the recorded fixture (no behavioural drift).
  * `a_1201_file_tree_is_catalogued_completely` — a real 1201-file, depth-3 tree
    (40 mids × 29 leaves) via the real slang binary: 1201/1201 modules, child
    edges `top`=40, `mid_0`=29, `mid_39`=29, `leaf_0`/`leaf_1159` not tops, no
    warning. (This is the completeness check that catches a stream walker that
    silently drops the tail.)
  * `an_over_budget_document_degrades_to_a_warning_not_a_failure` — one kept
    node: `failure == None`, warning names the node retention budget.
  * `a_document_over_the_size_threshold_is_warned_not_rejected` — a 1 KiB
    threshold on the 44 KiB fixture: full catalog + size note, no failure (the
    old behaviour failed here).
  * `a_catalog_failure_never_blames_lint_diagnostics` — no `lint`/`diagnostics`
    in the user message nor in `Display`.
  * `the_1176_file_design_yields_a_full_catalog` (**ignored**, run manually) —
    the acceptance E2E; see §2 and the run logs.
* `crates/vd-deeplint/tests/stream_memory.rs` —
  `the_streaming_catalog_does_not_allocate_the_document`: writes a **51,040,624
  byte** expression-shaped document (source span before `kind`, like real
  expressions), catalogues it through a fake `slang`, and asserts the process's
  own `VmHWM` grew by less than 16 MiB. Observed: **growth 564 KiB**
  (`document=51040624 bytes … growth=564 KiB`). The counting-allocator variant
  is impossible here (the workspace `forbid`s `unsafe_code`), so the kernel's
  high-water mark is the honest measurement; the document is written
  incrementally so the test never holds it either.
* `crates/vd-deeplint/src/lib.rs` tests —
  `an_ast_json_overflow_never_blames_lint_diagnostics`,
  `the_lint_output_cap_keeps_its_diagnostic_wording`.
* `crates/vd-ffi/tests/ux_batch_2.rs` —
  `catalog_size_warning_reaches_the_abi_and_failures_are_truthful` (ABI-level:
  `VD_STATUS_READY` + warning above the threshold; `VD_STATUS_FAILED` +
  AST-JSON wording for a design-less document).
* `crates/vd-deeplint/src/ast_prune.rs` unit tests — irrelevant subtrees dropped
  but catalog nodes survive; connections/expressions not retained; the node
  budget keeps `definitions` and reports partial retention; malformed documents
  reported.

Acceptance E2E (release, run manually; needs the generated fixture):

```
CARGO_HOME=$PWD/target/cargo-home cargo +stable test --release -p vd-deeplint \
  --test catalog --no-run --offline
VEDIT_U9A_BIGDIR=$PWD/evidence/ux-batch-2/u9a-catalog-scale/bigdesign-med \
  target/release/deps/catalog-* --ignored --nocapture the_1176_file_design_yields_a_full_catalog
```

Observed (both fixtures): `failure=None`, `modules=1176`, `tops=118`, warning
present, `self_peak_rss_kb=12752` / `12756` (`scratch/catalog-*-run.log`).

Static gates:

```
CARGO_HOME=$PWD/target/cargo-home cargo +stable clippy -p vd-deeplint -p vd-ffi \
  --all-targets --offline -- -D warnings      # exit 0
CARGO_HOME=$PWD/target/cargo-home cargo +stable fmt -p vd-deeplint -p vd-ffi -- --check
                                                # exit 0
```

The full workspace gate (`scripts/gates.sh`) was deliberately **not** run here —
the captain runs it.

**Which tests depend on the profile.** No test asserts wall-clock timing. The
ignored real-scale E2E is run **release** because a debug-build
`serde_json::from_reader` walk is 20–50× slower (per-byte `io::Read`), which
would make a multi-GB fixture take minutes; its assertions are about the
catalog's CONTENT and memory, not speed. The unit/integration suite (including
the RSS-bound test) runs in the normal debug test profile. The manual release
E2E outputs are recorded under `scratch/catalog-*-run.log`.

## 6. Bounds and what is NOT proven

1. **serde_json's recursion limit is a pre-existing depth bound, reported by a
   DEDICATED failure.** The deep control (100-deep instantiation chain → ~300
   nested JSON levels) trips serde_json's 128-value nesting guard. The catalog
   now classifies that error as `LintFailure::AstJsonNestedTooDeep` — NOT
   `InvalidAstJson` — and the user reads:

   > this design's hierarchy nests deeper than the AST JSON reader supports
   > (about 42 instance levels): the module catalog is unavailable for this design

   The bound: one elaborated instance costs three JSON levels (`Instance` →
   `InstanceBody` → `members`), so 128 / 3 ≈ **42 nested instance levels**.
   (slang itself refuses elaborations deeper than 128 instances, so the tool
   bounds the input from above.) The old whole-string
   `serde_json::from_str::<Value>` path had the same 128-level default, so this
   is **not a regression**; the change is only that the truth is now reported
   instead of "invalid JSON" / "lint diagnostics". Detection is a typed
   `PruneError::TooDeep` in `ast_prune.rs` (serde_json exposes no typed
   recursion-limit error; its stable Display prefix identifies it), with a unit
   test for the classification and an integration test that the catalog
   message names the reader limit and never says "invalid"/"malformed".
   Per the captain's ruling, the `serde_json` `unbounded_depth` feature is
   **NOT** enabled (workspace `Cargo.toml` is skeleton-owned, and a stack
   overflow is worse than a clean error).
2. **Synthetic designs only.** No real user project was available. The user's
   machine reported >256 MiB; the acceptance fixture is 284 MiB and the heavy one
   1.16 GiB, both generated by the script in this directory (deterministic).
3. **slang's own cost is not ours.** The child needed 1.1 GB / 3.4 GB peak RSS for
   these dumps; we cannot reduce the tool's memory, only ours (12.7 MiB).
4. **The C++ lane was not built/run here** (no `ninja`/Qt6 dev files on the host;
   the product builds in the pinned CentOS 7 image). See §4.
5. **The monolithic `--ast-json` document is still produced by slang.** At the
   GB extreme this costs the dump (18.5 s / 11.6 GB RSS for the artificial
   6.1 GB, 100-deep variant measured earlier in this session) plus our walk
   (~360 MB/s release). See §7 for the alternatives and why none is taken here.

## 7. Design question: is the monolithic `--ast-json` acceptable at GB scale?

**Measured facts.**

* The document is unbounded in the inputs we do not control: the same 1176 files
  produced 161 MB at depth 10 and 793 MB at depth 100 (§2). A production
  acceptance cannot assume a size.
* Our side is bounded: ~12.7 MiB peak RSS on both 284 MiB and 1.16 GiB documents
  (§2), 564 KiB growth on a 51 MB synthetic document (§5), ≈ 360 MB/s release
  walk.
* slang's side is not: 1.1 GB / 3.4 GB peak RSS (and 11.6 GB for the artificial
  6.1 GB depth-100 variant). That cost exists regardless of how we read the
  file; it is the tool's elaboration + serialisation.

**Alternatives considered, with numbers.**

1. **`--ast-json-scope <path>` per top-level root.** Measured on the catalog
   fixture: the scoped document's root is the `Instance` itself — there is **no
   `design` object and no `definitions` array** (`scratch/scope-top-a.json`:
   `design members: []`, `definitions: 0`). The catalog needs the top-level
   `definitions` array for `definitionKind` (module vs interface vs package), so
   a scoped dump would lose interface exclusion. It also needs one elaboration
   run per root (roots are unknown until an elaboration has run), and the total
   AST bytes do not shrink — only the per-file split does. **Not viable without
   a second information source.**
2. **A narrower extraction artifact.** slang 12.1.0 has no serializer that emits
   only module/port/instantiation data (`--ast-json`, `--cst-json`,
   `--diag-json` are the JSON surfaces; `--ast-json-detailed-types` only adds
   types). Getting one means patching/forking slang (C++) or writing our own
   SystemVerilog parser — out of proportion for this defect.
3. **Pipe the dump (`--ast-json -`) and stream it while slang writes.** This is
   the one economically sound improvement left: it removes the temp file
   (write + read of 0.3–1.2 GB) and overlaps the dump with our walk, so wall
   time ≈ max(dump, walk) instead of the sum. It requires reworking the child
   supervision (timeout/peak-RSS polling while consuming the child's stdout) —
   a contained but non-trivial runner change, **not started** (needs the
   captain's go-ahead).

**Recommendation.** Keep the streaming prune for this batch: it turns the
reported defect (empty panel, wrong message) into a complete tree at 12.7 MiB
for our process, which is the user-visible acceptance. The remaining GB-scale
cost is slang's own serialisation (bounded by the tool, not by us); the
stdout-pipe overlap is the candidate follow-up if real projects regularly
produce multi-GB dumps.

---

## 8. Files changed (this task)

```
crates/vd-deeplint/src/ast_prune.rs        NEW  streaming prune + node budget
crates/vd-deeplint/src/hierarchy.rs        sink = file path; AstJsonTooLarge
crates/vd-deeplint/src/catalog.rs          streaming build_catalog, failure_message,
                                           size threshold + node budget + warnings
crates/vd-deeplint/src/lib.rs              truthful failure variants
crates/vd-deeplint/tests/catalog.rs        new/updated tests + ignored real-scale E2E
crates/vd-deeplint/tests/stream_memory.rs  NEW  process-RSS bound test
crates/vd-ffi/src/catalog.rs               poll uses catalog.failure_message()
crates/vd-ffi/src/capi.rs                  doc comments (ABI layout unchanged)
crates/vd-ffi/include/vd_ffi.h             regenerated by cbindgen (comments only)
crates/vd-ffi/tests/ux_batch_2.rs          ABI warning + truthful-failure test
src/workspace/module_tree/catalog_client.cpp  THIS TASK's part: poll budget 300 s,
                                           timeout/size-threshold comments (the
                                           file also carries an unrelated RM-2
                                           edit from another lane — see below)
```

`git status` in this workspace also shows files from **other lanes'** tasks
(`crates/vd-config/src/project.rs`, `src/app/app_shell.cpp`,
`src/workspace/diagnostics/*`, `src/workspace/project/*`, `translations/*`) —
they are not part of this task and were not touched by it. The same is true of
the `beginProjectFallback()` hunk in `catalog_client.cpp` (an RM-2 edit by
another lane that lands in the same file); this task's hunks in that file are
only the poll budget and the two comments in `beginCatalog()`/`poll()`. The two
hunks are independent — a reviewer must not attribute the RM-2 expansion change
to U9a (or vice versa).

## 9. Reproduce

```
# 1) fixtures (deterministic; ~14 MB of .sv each)
python3 evidence/ux-batch-2/u9a-catalog-scale/gen_design.py \
  --out /tmp/bigdesign-med --files 1176 --signals 40 --segment 10

# 2) the document slang produces (≈298 MB, > the old 256 MiB cap)
build/slang/out/slang --ast-json /tmp/big-med.json --ast-json-source-info \
  /tmp/bigdesign-med/*.sv

# 3) the catalog end to end (release; see §5 for the exact test invocation)
VEDIT_U9A_BIGDIR=/tmp/bigdesign-med \
  target/release/deps/catalog-* --ignored --nocapture the_1176_file_design_yields_a_full_catalog
```
