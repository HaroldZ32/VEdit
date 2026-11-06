# U9a / U9b final independent review — catalog and lint at 1176-file scale

- Task: T3 (independent verification of U9a + U9b), read-only.
- Reviewer: `reviewer-3` (Reviewer).
- Date: 2026-11-06 (Asia/Shanghai).
- **Verdict: FAIL (request changes). Risk: HIGH.**
- Basis: the RM-1 card in `evidence/ux-batch-2/REAL-MACHINE-FINDINGS.md` (the frozen
  acceptance: a ~1176-file design must yield a catalog — full, or partial with an explicit
  warning; an empty tree with a nicer error is a FAIL; the lint pass must never fail on
  diagnostic volume); the writers' evidence `u9a-catalog-scale/` (README sha256 `bac09e65…`)
  and `u9b-lint-flood/` (README sha256 `acd99d23…`); the code.
- Reviewed tree hashes (`05-provenance.txt`): `ast_prune.rs` `ba23ec67…`, `catalog.rs`
  `47d47c37…`, `hierarchy.rs` `432a1f18…`, `lib.rs` `03e6355b…`, `diagnostic.rs` `336ccb64…`,
  `batch.rs` `0602323f…`, `service.rs` `00689372…`, `vd-ffi/src/catalog.rs` `84318bc4…`,
  `catalog_client.cpp` `b09f3b21…`, `module_catalog.cpp` `b6384d76…`.

I ran the Rust side of the acceptance myself in the workspace: the 1176-file catalog at three
shapes, the 40,000-warning flood, the catalog/stream-memory tests, and the ABI message tests.
The Qt lane was not built here (see M1); the C++ warning/rendering path was inspected.

---

## 1. What I re-derived (commands and observed output; logs in this directory)

### 1.1 Catalog at 1176 files — the acceptance and the heavy/full case pass

`VEDIT_U9A_BIGDIR=…/u9a-catalog-scale/bigdesign-med target/release/deps/catalog-* --ignored
--nocapture the_1176_file_design_yields_a_full_catalog` (`01-catalog-med-rerun.log`):

```
big-catalog: files=1176 modules=1176 tops=118 service_elapsed_ms=3727
  slang_peak_rss_kb=1015608 self_peak_rss_kb=12756 wall_ms=3767
  warning=Some("the elaborated AST JSON is 274 MiB (over the 256 MiB size threshold);
  the project has 1176 files (limit 1000): the module tree may be slow") failure=None
test ... ok
```

The heavy fixture (`bigdesign/`, 1.16 GiB AST; `04-catalog-heavy-rerun.log`): **1176 modules /
118 tops**, `self_peak_rss_kb=12740`, warning present, `failure=None`, 12.18 s wall. Both are
real full trees, not empty shells, and both return the size/file-count warning.

### 1.2 Catalog at 1176 files — the deep/hierarchical case FAILS (the finding)

The same test on `bigdesign-deep/` (1176 files, depth-100 instantiation chain;
`05-catalog-deep-rerun.log`):

```
thread 'the_1176_file_design_yields_a_full_catalog' panicked at crates/vd-deeplint/tests/catalog.rs:543:5:
a real-scale catalog must not fail: Some(AstJsonNestedTooDeep)
test result: FAILED. 0 passed; 1 failed
```

`big-catalog: files=1176 modules=0 tops=0 … failure=Some(AstJsonNestedTooDeep)`. The panel gets
**no tree at all** — an empty module catalog — with the dedicated message "this design's
hierarchy nests deeper than the AST JSON reader supports (about 42 instance levels): the module
catalog is unavailable for this design". The writer's own real-scale test fails on this fixture;
the writer's README §6 calls it a bound. The task's #1 says an empty tree with a nicer error
message is a **FAIL**, and this is a 1176-file design.

### 1.3 Memory — bounded, not a raised constant with a blob

- `the_streaming_catalog_does_not_allocate_the_document` (`03-catalog-tests-rerun.log`):
  a >40 MiB pruned document, process peak-RSS growth **< 16 MiB**, then a full catalog; 1 passed
  (4.46 s). The test writes the document incrementally, then a fake slang copies it, so the test
  process never holds it.
- The catalog's own peak RSS on the 298 MB and 1.16 GB documents is **12.7 MiB** in both writer
  logs and in my reruns; the writer's `self_peak_rss_kb` excludes the slang child.
- The code is a stream prune (`ast_prune.rs`: `serde_json::Deserializer::from_reader`, a
  counting reader, `IgnoredAny` for non-catalog nodes, a `Budget`/`max_retained_nodes` (default
  1,000,000 kept nodes), `retained_fully=false` past the budget). The document is never a
  `Value`; only the catalog nodes are retained. The node budget bounds the retained set; the
  document size only bounds disk/stream time. **The chosen approach is bounded.**
- The slang child's own 1.1–3.4 GB peak is the tool's, not the service's; the writer discloses
  it, and the dump's disk cost (0.3–1.2 GB) remains. That is a genuine, disclosed bound, not a
  hidden blob in the service.

### 1.4 Lint flood — no failure, counted/deduped, child reaped

`VEDIT_U9B_FLOOD_FILE=…/flood.sv cargo +stable test -p vd-deeplint --test engine_batch
--offline -- --ignored --nocapture` (`02-lint-flood-rerun.log`):

```
real-flood: phase=Finished files=1/1 total_findings=40000 distinct=500 dropped=39500
  collapsed=0 elapsed_ms=2136 failures=0
  summary=slang — 1/1 files, 500 distinct diagnostic(s), 39500 dropped
```

The real slang `--diag-json` document is 28,292,247 bytes (3.4× the old 8 MiB cap); the pass
finishes, counts every finding, retains 500 distinct rows, reports 39,500 dropped, and fails
nothing. The de-dupe key is `file / line / column / code / message` in both the per-file
stream (`diagnostic.rs::dedupe_key`) and the pass (`batch.rs::batch_dedupe_key`); the code and
message are part of the key, and the tests cover "same location, different code/message are
never merged". The batch key uses the diagnostic's own reported file when it has one (an
include), else the linted file. `service.rs` kills and reaps every path
(`try_wait` loop, `terminate()` → `kill()` + `wait()`); the `engine_batch` flood test records the
child PID and asserts it was reaped after the flood. I did not find a merge of distinct
findings.

### 1.5 Normal behaviour and the rest of the narrow suites

- `cargo +stable test -p vd-deeplint --test catalog --test stream_memory --offline`
  (`03-catalog-tests-rerun.log`): **11 passed, 0 failed, 1 ignored** (the ignored one is the
  1176 E2E), including `the_streamed_catalog_matches_the_whole_string_parse` and
  `a_1201_file_tree_is_catalogued_completely` (the real 1201-file depth-3 tree, 1201/1201
  modules); the stream-memory test 1 passed.
- The writer's debug suite log (`u9a-catalog-scale/scratch/cargo-test-final.log`) is
  **185 passed, 0 failed, 1 ignored**; `clippy`/`fmt` exit 0 per the README.
- The catalog warning/error text is truthful: `catalog.rs::failure_message` maps
  `OutputTooLarge | AstJsonTooLarge` to "the elaborated AST JSON exceeded …", and the ABI poll
  uses `catalog.failure_message()`; the hierarchy path uses `LintFailure::AstJsonTooLarge`; the
  lint path no longer has a byte cap. The C++ `ModuleCatalog::summaryLine()` appends
  ` · ⚠ <warning>`, and `ModuleTreePanel::setCatalog` sets that summary for complete and empty
  catalogs (inspected; the Qt lane was not run — M1).
- Limits: the warning text names the size threshold (256 MiB) and the file count (1000); the
  lint summary names the distinct/dropped counts; the evidence READMEs document the streaming
  and the node/count budgets. The module-tree lane README still names only its 20,000 row
  budget and the lint lane README does not mention the 500/20,000/dropped limits (M3).

---

## 2. The six checks

| # | Check | Result |
|---|---|---|
| 1 | ~1176-file design yields a module catalog; full or partial+warning; no empty tree for a nicer error | **FAIL.** The acceptance (`bigdesign-med`, 298 MB) and the heavy (`bigdesign`, 1.16 GB) yield full trees (1176 modules / 118 tops) with the size/file-count warning. But `bigdesign-deep` (1176 files, depth 100) yields **0 modules / 0 tops** with `AstJsonNestedTooDeep`: an empty tree with a dedicated message, which the task explicitly calls a FAIL. The writer's own real-scale test fails on it; the writer calls it a pre-existing serde recursion bound. The user's real shape (depth 3, the 1201-file acceptance) completes, but the deep 1176-file design does not. |
| 2 | Memory honesty; bounded approach | **PASS.** Streaming prune + node budget; own peak 12.7 MiB on both 298 MB and 1.16 GB documents; the process-RSS test shows < 16 MiB growth on a >40 MiB document; the document is never a Value. The slang child's 1.1–3.4 GB and the dump's disk cost are the tool's, disclosed. |
| 3 | The message is truthful everywhere | **PASS with a Nit.** The catalog maps AST overflows to AST-JSON wording (`catalog.rs::failure_message`; the ABI poll uses it), the hierarchy uses `AstJsonTooLarge`, the lint path has no byte cap. `LintFailure::OutputTooLarge`'s legacy "lint diagnostics exceeded … 8388608" string still exists for API compatibility and is unit-tested (N1); no product path constructs it for the catalog, and the catalog maps it to the AST-JSON wording. |
| 4 | Lint flood: no failure, de-dupe+repeat, different findings separate, collapsed/dropped visible, child reaped | **PASS with a Low.** 40,000 findings → 500 distinct, 39,500 dropped, 0 failures; the key is (file, line, column, code, message) in both layers; different codes/messages at the same location are not merged (tests); the pass summary carries distinct/dropped (and collapsed); the child is killed+reaped and the test checks the PID. The per-row `repeat` exists in the model but is not in the C ABI; the pass-level collapsed count is what reaches the UI (M2). |
| 5 | Normal project behaviour unchanged; existing suites green | **PASS with the Qt bound.** My narrow Rust reruns: catalog 11/0 (1 ignored), stream-memory 1/0; the writer's full debug suite 185/0 (1 ignored); clippy/fmt exit 0. The Qt lane was not built/run (M1); the C++ changes are a poll budget and comments, and the existing rendering path is unchanged. |
| 6 | Limits documented where a user/maintainer looks | **PASS with a Low.** The messages name the thresholds; the evidence READMEs document the streaming/node/count budgets; the module-tree lane README names only its 20,000 row budget and the diagnostics README does not mention the new 500/20,000/dropped limits (M3). |

---

## 3. Findings (ranked)

### H1 — HIGH — a deep 1176-file design still yields an EMPTY catalog (the task's #1 FAIL)

**What.** `bigdesign-deep` (1176 files, depth-100 instantiation chain, 793 MB AST) produces
`modules=0`, `tops=0`, `failure=Some(AstJsonNestedTooDeep)`; the writer's own real-scale test
`the_1176_file_design_yields_a_full_catalog` FAILS on it (`05-catalog-deep-rerun.log`). The user
reads a dedicated message, but the module tree/interface panel are empty. The writer's README §6
classifies this as a pre-existing `serde_json` 128-value nesting guard (~42 instance levels) and
says it is not a regression — true, but the task's #1 says "an empty tree with a nicer error
message is a FAIL", and this is a 1176-file design. The user's reported real shape (depth 3)
completes, and the writer's 1201-file depth-3 test passes, but the acceptance is written for
"a ~1176-file design" and the deep one is one.
**Where.** `crates/vd-deeplint/src/ast_prune.rs` (`PruneError::TooDeep`, `from_reader`),
`crates/vd-deeplint/src/catalog.rs` (`AstJsonNestedTooDeep` mapping),
`crates/vd-deeplint/src/lib.rs` (`AstJsonNestedTooDeep` message);
`evidence/ux-batch-2/u9a-catalog-scale/bigdesign-deep/` (the fixture);
README §6 bound 1.
**Why it matters.** The task's frozen acceptance says a ~1176-file design must produce a tree
(full or partial+warning); the deep case produces neither. The user's real design depth is not
proven to be ≤42 by the writer (the 1201 depth-3 tree is a synthetic shape, not the user's actual
files). If the user's actual 1176 files nest deeper than ~42 instance levels, the module catalog
is unavailable — exactly the class of empty-panel failure RM-1 reported.
**Direction (described, not applied).** Either prove the user's actual design depth is within the
reader limit (a real user-project measurement, not a synthetic 1201), or make the deep case a
**partial catalog with the warning** (the task's allowed second outcome) — e.g. a depth-aware
iterative walker that retains the shallow nodes and reports the dropped deep ones, rather than
aborting the whole walk. A nicer message on an empty tree does not meet #1.

### M1 — MEDIUM — the warning path was not executed in the Qt lane; the writer's own bound

**What.** The ABI warning (`VdModuleCatalog.warning`) and the C++ rendering path
(`ModuleCatalog::summaryLine` → `ModuleTreePanel::setCatalog`) are correct by inspection, and the
Rust ABI test proves the warning arrives. But the writer states the Qt lane was **not**
compiled/run here; the product's module-tree panel has not displayed this warning in a built app.
The task's #1 says the warning must reach the module tree panel.
**Where.** `src/workspace/module_tree/catalog_client.cpp` (`snapshot.warning = fromC(...)`),
`src/workspace/module_tree/module_catalog.cpp` (`summaryLine`), `src/workspace/module_tree/module_tree_panel.cpp`
(`setCatalog`); README §4/§6 bound 4.
**Why it matters.** The last mile (the actual label) is asserted by code inspection, not by a
running Qt panel; a build-time or layout regression in that path would not be caught by the Rust
tests.
**Direction.** Build the pinned Qt image and assert the warning text on the module-tree summary
label for the bigdesign-med catalog (or a fixture with a synthetic warning), so the ABI-to-panel
path is executed, not inferred.

### M2 — LOW — the per-row repeat count is not in the C ABI

**What.** `Diagnostic.repeat` and the batch `collapsed` count exist; the pass summary
("N repeat(s) collapsed", "N dropped") reaches the UI, but a single retained row does not carry
its repetition count through the C ABI. The task's #4 asks for de-duplication with a repeat
count; the pass-level collapsed count is visible, the per-row count is not.
**Where.** `crates/vd-deeplint/src/diagnostic.rs` (`repeat`), `batch.rs` (`collapsed`),
`crates/vd-ffi/src` (the ABI rows); README §6 bound 2.
**Why it matters.** The user sees "39,500 dropped" and can see the pass summary; a row that
represents 500 instances of the same warning does not say "×500" in the UI.
**Direction.** Either add the field to the ABI and render it, or state the bound in the lint
README/user guide.

### M3 — LOW — the new limits are not in the module-tree/lint lane READMEs

**What.** The evidence READMEs document the streaming/node/count budgets and the message text
names the thresholds, but `src/workspace/module_tree/README.md` still names only its 20,000-row
tree budget and `src/workspace/diagnostics/README.md` does not mention the 500/file,
20,000/pass or dropped limits. A maintainer reading the lane READMEs does not learn the new
limits.
**Where.** `src/workspace/module_tree/README.md`; `src/workspace/diagnostics/README.md`; the
evidence READMEs.
**Why it matters.** The task's #6 asks for the limits where a user/maintainer looks; the message
and evidence cover the user, the lane READMEs do not cover the maintainer.
**Direction.** Add the size/node/500/20,000/dropped limits (and the deep serde bound) to the two
lane READMEs, pointing at the evidence.

### N1 — NIT — the legacy `OutputTooLarge` message still says "lint diagnostics"

**What.** `crates/vd-deeplint/src/lib.rs` keeps
`LintFailure::OutputTooLarge => "lint diagnostics exceeded the {limit} byte limit"` for API
compatibility (unit-tested); the lint path no longer produces it, the catalog maps it to the AST
JSON wording, and the hierarchy uses `AstJsonTooLarge`. A future caller constructing
`OutputTooLarge` would still show the old wording.
**Where.** `crates/vd-deeplint/src/lib.rs` (`LintFailure::user_message`), the `OutputTooLarge`
variant; `catalog.rs::failure_message` (the mapping).
**Why it matters.** The task's #3 is met in the shipped paths; the legacy variant is a latent
mislabel if reused.
**Direction.** Mark it deprecated with the AST-JSON wording, or remove it once the ABI callers no
longer reference it.

### N2 — NIT — synthetic designs only; the user's actual 1176 files were not available

**What.** The writer states no real user project was available; the acceptance/heavy/deep
fixtures are generated by `gen_design.py`. I reproduced the med/heavy/deep on the writer's
fixtures; I cannot see the user's actual 1176 files.
**Where.** `evidence/ux-batch-2/u9a-catalog-scale/gen_design.py`; README §6 bound 2.
**Why it matters.** The real design's hierarchy shape/size may differ; the deep bound is exactly
the kind of shape difference the synthetic control exposes.
**Direction.** If the user's real project can be shared (file list only), run the acceptance on
it; otherwise keep the synthetic + the deep control as the stated bound.

---

## 4. Declined to judge (reason on the same line)

- **The 6.1 GB / 100-deep artificial variant** (the writer's §6.5: 18.5 s / 11.6 GB slang RSS) — not re-run here; the writer's bound; the GB cost is slang's serialisation, not the service's.
- **The `--ast-json-scope` and narrower-extraction alternatives** — the writer's §7 analysis (the scoped dump loses `definitions`/`definitionKind`; no narrower serializer in slang 12.1.0); not a code defect of this fix.
- **The stdout-pipe (`--ast-json -`) follow-up** — the writer records it as a future optimisation; not started; not required by the acceptance as written.
- **`serde_json` `unbounded_depth`/stacker** — the captain's ruling not to enable it; the writer's bound; not re-litigated.
- **The 500/file and 20,000/pass constants** — product judgement, documented rationale, one tunable; not measured as optimal; not a defect.
- **The legacy GTK `vd-app` build** — the writer updated its three literals; the host cannot build GTK; the workspace gate will catch; the writer's bound.
- **The full workspace gate / full ctest** — the captain runs it; I ran the narrow Rust suites, the two E2E real-scale cases, and the stream-memory test.
- **The Qt lane** — M1; the writer's bound; I inspected the C++ path, did not build it.
- **The per-row repeat in the UI** — M2; the pass-level summary is implemented.
- **The lane README limits** — M3.
- **The legacy `OutputTooLarge` wording** — N1.
- **The user's actual 1176 files / real desktop** — N2; the synthetic fixtures are the writer's and mine.

---

## 5. Bounds of this review (what I could not reproduce at full scale)

- I reproduced the **1176-file** catalog acceptance on the writer's **synthetic** `bigdesign-med`
  (298 MB) and `bigdesign` (1.16 GB) and the failure on `bigdesign-deep` (793 MB), with the
  writer's release test binary. I did **not** have the user's actual 1176 files; the fixtures are
  generated by `gen_design.py` (synthetic modules, 0–180 signals, depth 10/100), so the user's
  real hierarchy shape/size is approximated, not measured.
- I ran the **real slang** 40,000-warning flood (`flood.sv`, 28.3 MB `--diag-json`), the
  catalog/stream-memory tests, and the catalog tests; I did **not** rebuild the whole workspace
  (the writer's debug suite 185/0 is the full suite evidence; my narrow reruns match).
- I did **not** build/run the Qt lane; the warning-to-panel path is inspected (ABI warning test +
  `summaryLine`/`setCatalog` code), not executed (M1).
- I did **not** run the full gate (`scripts/gates.sh`), the legacy GTK crate, the 6.1 GB
  artificial variant, or the pipe follow-up.
- The deep case's 0/0 is the writer's own fixture and test; I re-ran it and it fails the same
  way; the user's real design depth is not available.
- No product file was edited. The only writes are this report and `01`–`05` under
  `evidence/ux-batch-2/review-u9-final/`.

---

## 6. Verdict

**FAIL — request changes. Risk HIGH.**

The core of U9a is a real fix, not a message change: the 298 MB and 1.16 GB AST documents are
stream-pruned into **full 1176-module trees** with the catalog process at **12.7 MiB own peak
RSS**, the size/file-count warning is produced and wired to the module-tree summary, and the
process-RSS test proves the document is never held. The lint side is also real: the 8 MiB cap is
gone, the 40,000-warning flood finishes with 40,000 counted, 500 distinct retained, 39,500
dropped and 0 failures; the de-dupe key includes code and message so distinct findings stay
separate; the child is killed and reaped; the normal Rust suites are green in my narrow rerun.
Those parts I would sign off.

What fails the frozen acceptance is the **deep 1176-file design**: the writer's own real-scale
test yields **0 modules / 0 tops** with `AstJsonNestedTooDeep` — an empty module tree with a
nicer error message, which the task's #1 explicitly calls a FAIL. The writer classifies it as a
pre-existing serde recursion bound (~42 instance levels) and the user's reported depth-3 shape
completes, but no proof is offered that the user's actual 1176 files stay within the limit, and
the allowed second outcome (a partial catalog with the explicit warning) is not produced for
this case. The C++/Qt warning-to-panel path (M1) and the lane-README limits (M3) are still
inspection-only. Close the deep case with either a real user-project depth proof or a partial
tree, and execute the warning path in the pinned Qt image; the rest of the evidence is strong.
