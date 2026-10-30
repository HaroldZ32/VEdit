# T17 — SLANG deep-lint execution and diagnostic model

Cell `cell-20261030T020634155Z` · task **T17** · owner **Senior Engineer**
Write scope: `crates/vd-deeplint/**` (implementation, tests, evidence).
Evidence directory: `crates/vd-deeplint/evidence/` (path segment `evidence`).

## 0. Verdict

The contract's four success criteria are met:

| criterion | result | evidence |
|---|---|---|
| unit tests parse recorded JSON fixtures and malformed output | 25 lib tests + 13 fake-binary tests, all pass | `gates-vd-deeplint.txt`, `cargo-test-vd-deeplint.log` |
| integration test with the real artifact gives exact line:column on the undeclared-identifier fixture | real slang 12.1.0+0: one error, editor range `line 2, character 4..19` (0-based UTF-16) from raw byte `3:5..3:20` | `real_slang.rs` tests, `diag-table-unicode.txt` |
| cancellation leaves no orphan process | timeout and cancel both kill+reap; `/proc/<pid>` gone; fake binary uses `exec sleep` so the killed process is the sleeper | `runtime-timeout-cancel.txt` |
| no self-written SystemVerilog front end | none; everything comes from slang | — |

Scoped gates (the full workspace is currently red on other lanes' in-flight
crates, see §8):

```
cargo fmt -p vd-deeplint -- --check                     exit=0
cargo clippy -p vd-deeplint --all-targets -- -D warnings exit=0
cargo test -p vd-deeplint                              exit=0   (25 + 13 + 6 tests)
```

## 1. Invocation, and the one documented deviation

```text
slang --lint-only -Weverything --diag-column-unit byte --diag-json <tmp.json> <file>
```

`-Wall` is not a slang option (individual checks use `-W<name>`; `-Weverything`
enables all of them). Diagnostics still come only from slang.

**Documented deviation:** `--diag-column-unit byte` is added to the contract's
literal invocation, with the captain's explicit approval (2026-10-30). It changes
**only the unit of the reported columns**, not the diagnostic set: counts,
`code`, `severity`, `message`, `ranges` and `symbolPath` are identical between
the two modes. `Display` is slang's default for JSON
(`Driver.cpp: options.diagColumnUnit.value_or(ColumnUnit::Display)`), not its only
unit.

## 2. Column semantics — measured, not assumed

slang's **default** JSON columns are **display columns** (tab expanded to 8-column
tab stops, East-Asian Wide characters width 2, combining marks 0, emoji 1), which
cannot be converted back to text offsets without a width table identical to
slang's own. With `--diag-column-unit byte` the columns are exact 1-based UTF-8
byte offsets, and `ranges[].end` uses the **same** unit (verified: `end - start`
equals the identifier's byte length on every calibration row).

Three-way calibration (raw outputs and the independent Python computation in
`position-semantics.txt`; fixture `tests/fixtures/display_calibration.sv` covers
plain / tab / CJK×3 / emoji×3 / CJK+emoji / combining mark):

```
label             line byte_col display byte_indep char_idx  utf16_0 editor_char
undeclared_plain     3       17      17         16       16       16          16
undeclared_tab       4       18      21         17       17       17          17
undeclared_cjk1      5       15      14         14       12       12          12
undeclared_cjk2      6       18      16         17       13       13          13
undeclared_cjk3      7       21      18         20       14       14          14
undeclared_e1        8       16      13         15       12       13          13
undeclared_e2        9       20      14         19       13       15          15
undeclared_e3       10       24      15         23       14       17          17
undeclared_ce       11       19      15         18       13       14          14
undeclared_comb     12       19      19         18       18       18          18
```

- `byte_col == independent byte offset` and `editor_char == independent UTF-16
  offset` for all rows — **neither assertion uses slang's display numbers**.
- display differs from the 1-based UTF-16 column on **7 of 10** rows (tab, CJK and
  emoji lines), which is why display columns are not used.
- The conversion rejects a byte column that is not on a UTF-8 character boundary
  with a typed `ConvertError::NotCharBoundary` (never truncates silently).
- The CJK/emoji identifiers follow the same rule: `4:18..4:31 → 3:13..3:26`
  and `5:16..5:31 → 4:13..4:28` (raw byte → editor UTF-16).
- **Upgrade guard:** if slang is upgraded, re-verify that
  `--diag-column-unit` still exists and keeps the byte semantics, and re-run the
  10-case calibration fixture. The fixture is committed for exactly this reason.

## 3. Diagnostics without positions (real-world shapes)

Measured on slang 12.1 with `-Weverything`:

- `ImplicitNet` (`implicit net 'q' created`): has `location` + `optionName` +
  `symbolPath`, but **no `ranges`**;
- `TooManyErrors`: has only `code`/`message`/`severity` — **no location at all**.

The model therefore keeps:

- `location: Option<RawLocation>` and `ranges: Vec<RawRange>` (possibly empty);
- `raw: serde_json::Value` — the original JSON object, so unknown keys such as
  `optionName` survive;
- `editor_range: Option<EditorRange>` plus `conversion_note`/`parse_note` for
  degraded cases.

`ImplicitNet` converts its location into a zero-width editor range; a diagnostic
with no position is kept with `editor_range = None` and an explanatory note — no
panic, no dropped diagnostic. Adversarial tests cover both shapes
(`implicit_net_has_a_location_but_no_ranges_and_keeps_option_name`,
`a_diagnostic_without_location_or_ranges_is_kept`) and the real artifact is
tested too (`real_slang_implicit_net_has_no_ranges_but_keeps_its_location`).

## 4. Failure model (AC-11)

Typed `LintFailure` with a short `user_message()`:

`BinaryNotFound{attempts}` · `Spawn{message}` · `Timeout{after}` · `Cancelled` ·
`MissingOutput{exit_code, stderr}` · `InvalidJson{message, stderr}` ·
`OutputTooLarge{limit}` · `InvalidSourceEncoding{path}` · `Io{message}`.

A **nonzero exit with parseable JSON is a normal result** (slang exits 1 when it
reported errors). A nonzero exit without JSON, malformed JSON, an oversized JSON
file, a missing binary, an exec failure, a timeout or a cancellation each map to
one of the variants above; the UI sentence is derived from it.

## 5. Process management (timeout, cancellation, limits)

- every run polls the child every 5 ms; timeout/cancel → `kill()` + `wait()`
  (reaps the direct child; no zombie);
- **a new run cancels the previous one** (`LintService` holds the current cancel
  flag; the second `lint()` sets it and the first returns `Cancelled`);
- stdout/stderr are captured on threads with a channel and a **bounded 250 ms
  grace**: a grandchild that inherited the pipe cannot stall the result after the
  child was reaped (this is the `wait_with_output`-hangs-forever trap; the fake
  binaries use `exec sleep` so the killed process is the sleeper itself);
- `--diag-json` size is capped before parsing (`max_json_bytes`, default 8 MiB)
  and the diagnostics list is capped (`max_diagnostics`, default 500) while the
  total count is still reported (`truncated` flag);
- measured: timeout at 300 ms returned in 301 ms, cancel returned `Cancelled`,
  both with `reaped=true` (`runtime-timeout-cancel.txt`).

## 6. Binary resolution (never `PATH`)

Order: explicit `LintOptions::binary` (tests/packaging) → `VEDIT_SLANG` →
`exe_dir/../lib/vedit/bin/slang` → `exe_dir/../bin/slang` → `exe_dir/slang`.
Every attempt is recorded with its outcome (`found` / `not found` /
`not executable` / `unset`) and is present in every `LintReport`. Three captured
scenarios: env set (found), env unset (package paths probed, none found), env set
to a missing path (env not found + package paths). See
`binary-attempts-env-{found,unset,missing}.txt`.

## 7. Public surface for T21

```rust
LintOptions { timeout, max_json_bytes, max_diagnostics, binary }
lint(file, &options) -> LintReport
LintService::{new, lint, cancel}          // repeated edits cancel the old run
LintReport { diagnostics, total_diagnostics, truncated, binary,
             failure, exit_code, stdout, stderr, elapsed, child_pid }
Diagnostic { severity, message, code, location: Option<RawLocation>,
             ranges, symbol_path, raw, editor_range: Option<EditorRange>,
             conversion_note, parse_note }
EditorPosition { line, character }        // identical semantics to vd-editor (T9)
```

No UI code and no SystemVerilog parsing lives here.

## 8. Bounds and honest caveats

- Only slang **12.1.0+0** (the T3 artifact) was tested; the CLI/JSON contract of
  other versions is unverified (the upgrade guard in §2 covers the column unit).
- The full workspace gate is currently red on **other lanes' in-flight crates**
  (vd-syntax). This lane's scoped gates are green; the workspace run is not this
  crate's evidence and was not "fixed" by touching other lanes' tests.
- The no-zombie evidence covers the direct child; a grandchild that a *fake*
  binary deliberately leaks is outside our control (real slang does not fork),
  which is exactly why the reader wait is bounded.
- `InvalidSourceEncoding` is reported as a failure (positions cannot be mapped);
  a non-UTF-8 file is outside the editor's model.
- `unicode-width` was deliberately **not** added: byte columns make a width table
  unnecessary, and `unicode-width` disagrees with slang on emoji width
  (slang: 1, unicode-width: 2), which would systematically shift positions.

## 9. Evidence index (`crates/vd-deeplint/evidence/`)

| file | produced by | exit |
|---|---|---|
| `gates-vd-deeplint.txt` | `cargo fmt/clippy/test -p vd-deeplint` | 0 |
| `cargo-test-vd-deeplint.log` | `cargo test -p vd-deeplint` (25 + 13 + 6 tests) | 0 |
| `position-semantics.txt` | independent Python computation + `raw/calibration-*.json` | 0 |
| `raw/runs.txt`, `raw/<case>-{display,byte}.json` | direct slang runs on clean/undeclared/unicode/implicit/calibration fixtures, commands + exit codes | 1 for diagnostic runs, 0 for clean |
| `diag-table-unicode.txt`, `diag-table-calibration.txt` | `cargo run --example diag_table` against the real artifact | 0 |
| `runtime-timeout-cancel.txt` | `cargo run --example runtime_probe` (timeout 301 ms, cancel, `/proc` checks) | 0 |
| `binary-attempts-env-{found,unset,missing}.txt` | `cargo run --example binary_attempts` in three env scenarios | 0 |
