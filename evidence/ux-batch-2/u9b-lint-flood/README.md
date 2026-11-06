# U9b — the project lint pass must never fail (or lose a file) on diagnostic volume

Status: **fixed and verified** with the REAL slang binary on a generated file
whose `--diag-json` document is **28,292,247 bytes — 3.4× the old 8 MiB cap**.
Before: the file vanished from the pass with zero diagnostics. After: 40,000
findings counted, 500 distinct retained, 39,500 dropped and reported, 0 failures.

---

## 1. The defect

`crates/vd-deeplint/src/lib.rs` had `DEFAULT_MAX_JSON_BYTES = 8 * 1024 * 1024`
(the LINT default; the catalog had its own 256 MiB constant). `EngineOptions`
carried it into `LintService::run_engine`, and `service.rs::read_json` checked the
`--diag-json` file's size BEFORE reading it:

```rust
if metadata.len() > options.max_json_bytes as u64 {
    report.failure = Some(LintFailure::OutputTooLarge { limit: options.max_json_bytes });
    return;
}
```

`batch.rs::run_batch` turns a per-file failure into a `BatchFileFailure` and keeps
going, so a project whose warnings exceed 8 MiB per file ends with:

* every affected file's diagnostics **silently lost** from the pass,
* one line of summary text: `N file(s) failed` (no file name, no reason in the
  ABI surface the UI reads),
* the typed reason (`lint diagnostics exceeded the 8388608 byte limit`) buried in
  the report the C ABI never exposes.

The 8 MiB cap existed to keep the whole document out of memory (the old parser
did `fs::read_to_string` + `serde_json::from_str::<Value>`). The fix removes the
byte policy and replaces it with **counting, streaming and de-duplication**, so
volume can no longer fail anything.

## 2. The real flood fixture and the numbers

`gen_flood.py` writes ONE SystemVerilog file; each `assign narrow_i = din;`
(8→4 bits) is one `WidthTruncate` warning:

| quantity | value |
|---|---|
| source file | `flood.sv`, 2,257,873 bytes, 40,000 warnings |
| `slang --lint-only -Weverything --diag-json …` | **28,292,247 bytes** |
| old 8 MiB cap (`8,388,608`) | **3.37× smaller than the document** |
| slang wall / peak RSS | 0.54 s / 185,248 KiB |

## 3. Red before / green after (both on the real fixture)

**Before** (pre-fix code at HEAD f1b8602, run in a temporary worktree; full log
`scratch/red-before.log`):

```
BEFORE: phase=Finished total_findings=0 distinct=0 failures=1 \
  summary=slang — 1/1 files, 0 diagnostic(s), 1 file(s) failed
```

40,000 findings became ZERO, and the file was not named. The pre-fix unit test
that encoded the contract (`oversized_json_is_rejected_before_parsing`, with
`max_json_bytes = 256` and a 4096-byte document) also ran green on that worktree.

**After** (this tree; `scratch/green-after-real-flood.log`):

```
real-flood: phase=Finished files=1/1 total_findings=40000 distinct=500 \
  dropped=39500 collapsed=0 elapsed_ms=2475 failures=0 \
  summary=slang — 1/1 files, 500 distinct diagnostic(s), 39500 dropped
```

No failure, the file is processed, every finding is counted, the retained set is
bounded by COUNT, and the dropped count reaches the UI in the pass message.

## 4. What changed

* **`crates/vd-deeplint/src/diagnostic.rs`** — `parse_diagnostics_reader(reader,
  max_unique)`: streams slang's `--diag-json` ARRAY from a `Read` (no
  whole-document buffer) and applies two policies while reading:
  * **de-duplication**: findings with the same (file, line, column, code,
    message) become ONE row; `ParsedDiagnostic`/`Diagnostic.repeat` counts the
    occurrences (a warning about an instantiated module's connection repeats per
    elaborated instance). Different rule codes or messages at the same location
    are NEVER merged (unit-tested).
  * **count bound**: at most `max_unique` DISTINCT rows are retained; further
    rows are counted in `total` and `dropped`, never kept. The element is still
    JSON-validated, so a malformed document is still a typed failure.
  `parse_diagnostics(&str)` stays as the whole-string path (test-only now) and
  shares `parse_wire_element` with the streaming path, so both agree
  (unit-tested).
* **`crates/vd-deeplint/src/service.rs`** — `read_json` no longer checks the file
  size and no longer reads the document into a String: it opens the file and
  streams it. `LintReport.dropped` is reported; `summary()` names the distinct /
  dropped counts.
* **`crates/vd-deeplint/src/lib.rs`** — the lint byte cap is GONE:
  `LintOptions.max_json_bytes`, `EngineOptions.max_json_bytes` and
  `DEFAULT_MAX_JSON_BYTES` are removed (the ABI never exposed them). Two count
  bounds replace them:
  * `DEFAULT_MAX_DIAGNOSTICS = 500` — DISTINCT findings retained per file (the
    existing per-file row bound, now counted on distinct rows, so repeats no
    longer consume it);
  * `DEFAULT_MAX_PASS_DIAGNOSTICS = 20_000` — DISTINCT findings retained by one
    pass, tunable through `EngineOptions.max_pass_diagnostics` (0 = default).
  `LintFailure::OutputTooLarge` is kept for API compatibility and documented as
  no longer produced by the lint path.
* **`crates/vd-deeplint/src/batch.rs`** — the pass de-duplicates across the whole
  run keyed on (the diagnostic's own file, line, column, code, message),
  `collapsed` counts the raw findings folded into a retained row
  (per-file repeats + pass-level merges), `dropped` counts distinct findings the
  bounds did not keep, and the summary now reads like
  `slang — 1176/1176 files, N distinct diagnostic(s), M repeat(s) collapsed,
  at least K dropped, 2 file(s) failed: <file> (<reason>); …`.
  A file that could not be processed is NAMED with its reason (first two; "and N
  more"), so no file is silently lost.
* **No C ABI change**: the counts ride the existing `VdLintProgress.message`,
  which `src/workspace/diagnostics/lint_progress.cpp:97-99` already renders. The
  C++ lane therefore needed no edit for this task.

### Why these bounds are sane

* 500 distinct findings per file is the pre-existing per-file UI bound; with
  de-duplication repeats no longer eat it, and a file that produces more
  reports the exact excess.
* 20,000 distinct findings per pass ("low tens of thousands"): a 1176-file
  project at the per-file bound would otherwise retain up to 588,000 rows; 20k
  rows is ~5 MB of model + C list and already far beyond what a human can act
  on. The bound is reported, never hidden.

## 5. Tests (commands + observed results)

```
CARGO_HOME=$PWD/target/cargo-home cargo +stable test -p vd-deeplint -p vd-ffi --offline
# 196 passed; 0 failed; 2 ignored   (scratch/cargo-test.log)
CARGO_HOME=$PWD/target/cargo-home cargo +stable clippy -p vd-deeplint -p vd-ffi \
  --all-targets --offline -- -D warnings          # exit 0
CARGO_HOME=$PWD/target/cargo-home cargo +stable fmt -p vd-deeplint -p vd-ffi -- --check
                                                  # exit 0
```

New/changed tests:

* `diagnostic.rs` units: whole-string vs streaming equality; `repeated_findings_
  collapse_with_a_repeat_count` (3 copies → 1 row, repeat 3); `different_findings_
  at_one_location_stay_separate` (same location, different code/message → 2
  rows); `the_count_bound_keeps_the_head_and_counts_the_tail`; non-array → typed
  error.
* `tests/fake_binary.rs`: `a_diagnostics_flood_is_bounded_by_count_and_never_
  fails_the_file` — 3000 valid diagnostics, `max_diagnostics = 10` → success,
  `total = 3000`, 10 retained, `dropped = 2990`, summary names the count. The
  old `oversized_json_is_rejected_before_parsing` (byte cap → `OutputTooLarge`)
  is deleted with the contract it encoded. `diagnostics_are_capped_and_the_total_
  is_reported` became `identical_findings_collapse_and_the_total_stays_truthful`.
* `tests/engine_batch.rs`:
  * `a_warning_flood_is_deduped_and_never_fails_the_pass` — 300 identical + 50
    distinct findings: phase Finished, no failure, 51 rows, the repeated row has
    `repeat = 300`, all 50 distinct findings preserved, `collapsed = 299`,
    `dropped = 0`, summary names the collapse, and the child PID is gone from
    `/proc` afterwards (no zombie, no hang).
  * `the_pass_count_bound_drops_but_reports_and_never_fails` — 60 distinct with
    `max_pass_diagnostics = 10` → 10 retained, `dropped = 50`, summary says
    "50 dropped", phase Finished.
  * `a_failed_file_is_named_with_its_reason_and_the_other_files_still_report` —
    one malformed-JSON file + one good file: the good file still reports, the bad
    one is recorded AND named in the summary with "malformed".
  * `a_real_diagnostics_flood_is_streamed_and_reported` (**ignored**, run
    manually) — the real 28 MB fixture above; output in
    `scratch/green-after-real-flood.log`.
* Existing batch/cancel/progress tests stay green (normal projects are
  unchanged: with no repeats and no bounds hit, every row has `repeat = 1`, the
  summary keeps its old shape, and the count of rows is the count of findings).

## 6. Bounds and what is NOT proven

1. **The child still writes to a temp file** (`--diag-json <file>`); we
   STREAM-PARSE that file, so the raw output is never held in memory. Streaming
   the child's stdout pipe instead (`--diag-json -`) would also remove the temp
   file, but it needs the same runner rework already recorded for the catalog
   (timeout + peak-RSS supervision while draining the pipe) — recorded as a
   future optimisation, not done here.
2. **Per-row repeat counts are not in the C ABI.** The model carries them
   (`Diagnostic.repeat`) and the pass-level `collapsed`/`dropped` totals reach the
   UI through `VdLintProgress.message`; adding a per-row count would be a C ABI
   field addition (header regeneration + C++ consumer) and was not needed for the
   truthful pass summary.
3. **The Qt lane was not compiled here** (host has no ninja/Qt6 dev files). This
   task changed no C++ file; it relies on the existing
   `VdLintProgress.message` rendering (`lint_progress.cpp:97-99`).
   The legacy GTK crate (`crates/vd-app`) could not be compiled either (its
   `gobject-sys`/`gdk-sys` build scripts need GTK dev files, and the pinned
   build image is not available on this host). Its three `LintReport` /
   `Diagnostic` literals were updated with the compiler-mandated new fields
   (`dropped: 0`, `repeat: 1`) — a required fallout fix, not a feature: the
   workspace gate must build `vd-app` and will catch any other site (none found
   by a workspace-wide grep).
4. **Synthetic designs only** — but the flood is produced by the REAL slang
   binary, not a fake, and the pre-fix behaviour was measured on the pre-fix code
   itself (worktree at HEAD f1b8602, `scratch/red-before.log`).
5. The 500/file and 20,000/pass numbers are a product judgement, not a measured
   optimum; they are constants with documented rationale and one of them is
   tunable (`EngineOptions.max_pass_diagnostics`).

## 7. Reproduce

```
python3 evidence/ux-batch-2/u9b-lint-flood/gen_flood.py \
  --out /tmp/flood.sv --warnings 40000
build/slang/out/slang --lint-only -Weverything --diag-column-unit byte \
  --diag-json /tmp/flood.json /tmp/flood.sv     # 28,292,247 bytes

VEDIT_U9B_FLOOD_FILE=/tmp/flood.sv \
  CARGO_HOME=$PWD/target/cargo-home cargo +stable test -p vd-deeplint \
  --test engine_batch --offline -- --ignored --nocapture
```
