# U6 — lint knob wiring close-out — independent review (T13)

Reviewer: reviewer-1 (read-only lane; nothing was edited by this review). Repair
reviewed: T6, `evidence/ux-batch-2/u6-final/`. Contract:
`.mpd/plans/ux-batch-2/00-plan.md` §0.1 **R1** (BINDING: ONE Lint page, TWO independent
persisted configurations, an unsupported knob never silently sent) and §5.1 step 2 (the
ABI wiring closure); handover `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U6.

Risk level: **Low** — the five ABI-modelled knobs demonstrably reach the batch argv, the
defaults are neutral, the grey-out is truthful, the two configurations are independent and
the precedence is exactly as contracted; the one open point (autofix) is a frozen-ABI
limit, not a wiring defect.

## 1. Result

**PASS** against the frozen contract. Five of the six knobs (ruleset, rules,
waiver_files, slang warnings, slang error-limit) reach the real
`vd_lint_batch_command_line` echo; the sixth (autofix) has **no field in
`VdLintBatchOptions`** and is therefore correctly kept `TransportPending` and proven
*not sent* — the honest closure R1 demands. Defaults send no flag, the panel greys
exactly what the ABI says is a no-op (plus the two tool-level options the ABI does not
model), the two engine sets never overwrite each other, and the rules precedence is
`lint.verible.rules_config → tools.verible_rules → VdProjectInfo.rules_path → no flag`
in the composed order.

## 2. What I ran (independent re-derivation)

All runs in the pinned image `vedit-qt6-build:local`; logs in this directory.

| run | command | observed |
|---|---|---|
| settings self-test (the acceptance suite) | `vedit-workspace-settings-selftest --selfcheck` | `SETTINGS-SELFTEST ok=1 checks=240 failures=0`, exit 0 — `10-settings-selftest-full.log` |
| official ctest | `ctest -R '^(vedit-workspace-settings-selftest\|vedit-project-lint-e2e\|vedit-b3-wiring-e2e)$'` | **3/3 Passed** (1.40 s / 1.58 s / 6.89 s) — `12-ctest.log` |
| product lint E2E (real batch backend) | `vedit-project-lint-e2e` | `SUMMARY checks=11 failures=0 skips=0`, exit 0 — `13-project-lint-direct.log` |
| product-path compile proof | preprocess `diagnostics_controller.cpp` with its recorded compile command | `VEDIT_LINT_OPTIONS_HAVE_ABI 1` and `VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS 1`; `settings::buildLintJobOptions` and `settings::fillLintBatchOptions` present; the legacy `#else` body absent — `11-preprocess-check.sh/.log` |

The test binary (11:04) and the diagnostics binaries (11:02) are newer than the U6
sources (`lint_options.h` 10:50, `diagnostics_controller.cpp` 10:57), so these results
are on the post-U6 build. The settings self-test's checks are the product's own
functions, not a test-local mapper: its `batchArgv` helper calls
`buildLintJobOptions()` → `fillLintBatchOptions()` → the real
`vd_lint_batch_command_line()` on the same `VdLintBatchOptions` struct the product
backend fills (`FfiProjectLintBackend::start`).

## 3. Item 1 — the knobs reach the argv (six checks, observed)

Each non-default value was set on one knob (`LintValues`), composed and echoed; the
quoted text is my run, trimmed to the argv (`tool=`/`program=` kept as printed).

| # | knob (settings key) | non-default value | observed argv tail | check |
|---|---|---|---|---|
| 1 | `lint.verible.ruleset` | `all` | `--ruleset=all f.sv` | `settings.a4.lint.argv.ruleset` PASS |
| 2 | `lint.verible.rules` | `line-length,no-tabs` | `--rules=line-length,no-tabs f.sv` | `…argv.rules` PASS |
| 3 | `lint.verible.waiver_files` | `/p/a.waiver` + `/p/b.waiver` (one per line) | `--waiver_files=/p/a.waiver,/p/b.waiver f.sv` | `…argv.waiver-files` PASS |
| 4 | `lint.verible.autofix` | `patch` | **no `--autofix`** (see §4) | `…argv.autofix-not-sent` PASS + `…state.autofix-pending` PASS |
| 5 | `lint.slang.warnings` | `everything` / `no-unused-variable` | `… -Weverything -Wno-unused-variable f.sv` | `…argv.slang-warnings` PASS |
| 6 | `lint.slang.error_limit` | `25` | `… --error-limit=25 f.sv` | `…argv.slang-error-limit` PASS |

The Rust argv builder agrees by construction: `crates/vd-deeplint/src/engine.rs`
`command_args_for` emits `--rules_config=`, `--ruleset=` (only for `All`/`None`),
`--rules=`, `--waiver_files=` in the verible arm and the `-W…` tokens plus
`--error-limit=` in the slang arm; the other engine's fields are never read. The C++
composition (`buildLintJobOptions`) fills only the selected engine's fields, so a
verible pass cannot carry slang flags and vice versa.

## 4. Item 1, the autofix knob — a frozen-ABI limit, correctly closed

- `crates/vd-ffi/include/vd_ffi.h` `VdLintBatchOptions` has exactly `ruleset`, `rules`,
  `waiver_files`, `slang_suppressions`, `error_limit` (plus binaries/lists/timeouts); it
  has **no** autofix field, and `grep autofix crates/vd-ffi/... crates/vd-deeplint/...`
  finds none in the ABI or the argv builder. This matches plan §3 A1 (rev3), which lists
  exactly those five typed fields as the ABI closure — autofix was never one of them.
- The lane therefore keeps `lint.verible.autofix` `TransportPending`: the value is
  persisted (`lint.verible.autofix` survives a restart and the engine switch), the
  control stays usable, and the tooltip/status text say it is stored but the lint job's
  C ABI has no field for it. The negative assertion
  `settings.a4.lint.argv.autofix-not-sent` proves `--autofix` never appears in the argv,
  i.e. R1's "never send an unsupported parameter as a silent no-op" is asserted, not
  assumed. The product's real one-click fix is the hover button, a different mechanism.
- Consequence for the assignment: the task card's "six knobs change the argv" cannot be
  true for autofix without a new ABI field (`crates/vd-ffi` + `crates/vd-deeplint` +
  header regeneration), which is outside this lane's write scope and not part of the
  frozen §3 A1 contract. The delivered five-positive + one-negative is the correct
  close-out; if the Lead wants autofix literally in the batch argv, that is a new ABI
  task, not a U6 repair (see finding N2).

## 5. Item 2 — defaults send no flag

`settings.a4.lint.argv.defaults-no-flag` PASS, and the echo shows:

```
verible: <program> f.sv
slang:   <program> --lint-only -Weverything --diag-column-unit byte --diag-json <diagnostics.json> f.sv
```

No `--ruleset=`, `--rules=`, `--rules_config=`, `--waiver_files=`, `--autofix`,
`--error-limit=` is present; the slang `-Weverything` is the engine's own built-in
(the check counts exactly one `-W`). An untouched page therefore reproduces the
pre-U6 argv for the same inputs (the only pre-existing rules input,
`tools.verible_rules`, still flows through unchanged when set).

## 6. Item 3 — unsupported knobs are not sent, and the grey-out tells the truth

- The active engine's no-op list comes from the ABI itself:
  `vd_lint_unsupported_knobs(Slang)` = `ruleset,rules,waiver_files`;
  `vd_lint_unsupported_knobs(Verible)` =
  `slang_suppressions,error_limit,include_dirs,defines` (my run's
  `settings.a4.lint.state.abi-table` prints exactly that). The builder never emits the
  other engine's fields, so nothing listed is ever sent.
- The panel greys with the reason from `lintKnobState` (`refreshLintKnobStates` sets
  `setEnabled(state.enabled())` and the tooltip). My run confirms both directions:
  slang active → `slang.ruleset-disabled`, `slang.config-reason`, `slang.waiver-reason`,
  `slang.autofix-reason`, while `slang.warnings-enabled`; verible active →
  `verible.ruleset-enabled`, `verible.config-supported`, `verible.slang-disabled` +
  `verible.slang-reason`. No supported control is greyed and no unsupported one is
  enabled; the inactive engine's whole group is disabled with an explicit "not the
  selected engine … their values are kept" label, which is an engine-selection state,
  not a capability claim.
- Residual metadata gap (N1): `unsupported_knobs(Slang)` does not list `rules_config`
  even though the slang arm demonstrably ignores it; the C++ supplements the list with
  a hard-coded `rules_config`/`autofix` clause. The UI is still truthful because the
  supplement matches the builder, but the ABI list is not the complete single source
  the header comment claims.

## 7. Item 4 — the two configurations are independent

Store keys are disjoint (`lint.verible.{ruleset,rules,rules_config,waiver_files,autofix}`
vs `lint.slang.{warnings,error_limit}`), and `writeLintEntries` writes both halves on
every change while `lintValuesFromEntries` reads both. My run's checks:
`settings.a4.lint.two-sets` PASS (the store carries both engines' keys at once),
`switch-back-slang` / `switch-back-verible` PASS (both sets survive an engine switch),
`no-crosswrite` PASS (the lint section is byte-identical after the switch), plus
`restart-values` / `idempotent` PASS. At the argv level, `settings.a4.lint.argv.two-sets`
PASS shows one `LintValues` holding both sets producing a verible argv with only
verible flags and a slang argv with only slang flags.

## 8. Item 5 — rules precedence

`buildLintJobOptions` tries `lint.verible.rules_config`, then the legacy
`tools.verible_rules` (the `request.rulesPath` the product backend passes), then the
discovered `VdProjectInfo.rules_path`, then leaves it empty. My run:
`options.page-wins`, `options.legacy-second`, `options.discovered-third`,
`options.none`, `options.slang-never` all PASS; at the argv level
`argv.discovered-rules-path` shows `--rules_config=/proj/.rules.verible_lint` and
`argv.hand-typed-wins` shows the page path wins over both other levels.

The discovered path is wired end to end: `ProjectFileSetPoller::poll()` copies
`info.rules_path` before `vd_project_info_free`, publishes it through
`settings::setCurrentDiscoveredRulesPath`, and `followProjectRoot()` clears it on a root
change; `FfiProjectLintBackend::start()` reads it at pass start (and the per-file
`FfiSlangBackend::startLint()` uses the same composition for its `rules_path`
argument). The preprocessed-TU check (§2) proves the composed branch — not the legacy
`#else` — is what the product library compiles.

## 9. Findings

### N1 — Nit — `vd_lint_unsupported_knobs` is not the complete no-op table
`crates/vd-deeplint/src/engine.rs` `unsupported_knobs(Slang)` returns
`ruleset,rules,waiver_files` and omits `rules_config`, although `command_args_for`'s
slang arm never reads `rules_config` (the value is ignored for slang). The C++ panel
compensates with the hard-coded `rules_config`/`autofix` supplement in
`lintKnobBackendUnsupported`, so the UI is truthful, but the declared "the page and the
Rust argv builder cannot drift" property is only partial: a different consumer of the
ABI list would enable `rules_config` for slang and get a silent no-op. Direction: add
`rules_config` to the slang no-op list (a `crates/**` change, outside this lane's
scope) or document that the table covers only the five ABI-modelled knobs.
Where: `crates/vd-deeplint/src/engine.rs` `unsupported_knobs`;
`src/workspace/settings/lint_options.h` `lintKnobBackendUnsupported`.

### N2 — contract note, needs a ruling (not a wiring defect) — the sixth "argv" knob
The assignment asks for six knobs that change the argv, including
`lint.verible.autofix`; the frozen ABI has no autofix field (plan §3 A1 defines five
typed fields), so autofix cannot reach the batch argv without a new ABI task. The lane's
five-positive + one-negative (autofix stays `TransportPending`, never sent, value
persisted, tooltip/status truthful) is the correct closure of the frozen contract and of
R1. If "six argv-carrying knobs" is intended literally, the task card conflicts with
§3 A1 and needs a captain/Lead decision (an ABI lane), not a U6 repair.

## 10. Declined to judge

- **Waiver paths containing a comma** — the ABI's comma-separated `--waiver_files=` form
  cannot represent such a path; documented limitation in the A1/ABI contract, not a U6
  defect. (One path per line in the panel; a comma inside a path would be split.)
- **The `-Weverything` duplication when a user stores the bare `everything` warning** —
  the engine's built-in `-Weverything` plus the user's token; harmless and disclosed.
- **An unknown stored `ruleset` string or an unparsable `error_limit` degrades to "no
  flag"** (`lintRulesetCode` / `lintErrorLimitValue`) rather than surfacing an error —
  the panel's combo/spin only produce valid values; a hand-edited store falls back to
  the engine default and never sends an invalid argument. Acceptable.
- **The inactive engine's controls are all disabled** — an engine-selection state, not a
  capability claim; the status label says so and states the values are kept.
- **The real batch pass's spawned argv is not observable** — the `vd_lint_batch_command_line`
  echo and the spawn share `apply_lint_knobs` + `command_args_for` (Rust test
  `command_line_for_echoes_the_same_argv_the_run_uses`), and the product pass is covered
  by `vedit-project-lint-e2e` (11/11 here), but no E2E reads the spawned argv; disclosed
  by the writer.
- **The discovered-path handoff is not E2E-proven with a real `.rules.verible_lint`** —
  code-verified (`poll()` copies before free; the backend reads at start) and disclosed
  as a gap for the captain's matrix.
- **No full build/relink of `vedit-app`** — the captain's gate; the panel is asserted
  through widget/state, not a screenshot.

## 11. Verdict

**PASS** — five knobs demonstrably reach the batch argv with the contracted flags, the
defaults are neutral, the unsupported-knob/no-op rule holds in both directions (with a
truthful grey-out), the two persisted configurations are independent, and the rules
precedence is exactly page → legacy tools → discovered → none; autofix is an honest
`TransportPending` because the frozen ABI has no field for it.

## 12. Bounds of the review

- Ran only the already-built binaries/ctest in the shared tree (no full build, no
  `build/gates/*`). The argv evidence is the ABI's own echo; the real pass spawns the
  same options but does not echo them (writer-disclosed gap).
- The preprocess check reuses the recorded compile command from
  `target/qt6-build/compile_commands.json`; no generated file was edited.
- Did not re-run the real verible/slang tools against the produced flags (A1's own
  evidence covers that acceptance); did not relink/repackage the product.
- Raw evidence: `10-settings-selftest-full.log`, `11-preprocess-check.sh/.log`,
  `12-ctest.log`, `13-project-lint-direct.log` in this directory.
