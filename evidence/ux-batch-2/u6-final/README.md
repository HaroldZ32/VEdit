# U6 — lint wiring: the transport-pending knobs reach the argv; the discovered rules file becomes the fallback

- **Task:** T6 (`U6 lint wiring`), handover `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U6,
  contract `.mpd/plans/ux-batch-2/00-plan.md` §5.1 step 2 (the ABI wiring closure) and §0.1 **R1**
  (BINDING: ONE Lint page, TWO independent persisted configurations; a knob an engine does not
  support is never silently sent as a no-op).
- **Write scope actually touched:** `src/workspace/settings/**` (4 files) + this directory, plus the
  Lead-granted extension: `src/workspace/diagnostics/diagnostics_controller.cpp`
  (**`FfiProjectLintBackend::start()` only**), `src/workspace/diagnostics/diagnostics_integration.cpp`
  (`ProjectFileSetPoller::poll()` + the `followProjectRoot()` clear) and
  `src/workspace/diagnostics/ffi_backends.cpp`. Nothing else.
- **Result in one line:** five of the six knobs now reach the project pass's
  `VdLintBatchOptions` → `vd-deeplint` argv (six acceptance assertions incl. the autofix negative),
  defaults send no flag, the two configurations stay independent, the rules precedence is
  `lint.verible.rules_config → tools.verible_rules → VdProjectInfo.rules_path → no flag`, and
  `lint.verible.autofix` stays truthfully `TransportPending` (no ABI field — see §0.ii).

## 0. Two corrections to the task card (recorded here per the Lead's ruling #4)

**(i) The batch options are composed in `diagnostics_controller.cpp`, not `ffi_backends.cpp`.**
The project pass's `VdLintBatchOptions` is built by `FfiProjectLintBackend::start(const
LintPassRequest &)` in `src/workspace/diagnostics/diagnostics_controller.cpp`. The
`buildLintJobOptions()` call in `src/workspace/diagnostics/ffi_backends.cpp` belongs to
`FfiSlangBackend::startLint()` — the **per-file lower layer**, which calls `vd_deeplint_lint_v2`
(single-file ABI; only a `rules_path` argument, no knob fields). A4's report
(`evidence/ux-batch-2/a4/README.md` §4.1) says ffi_backends.cpp "determines
`VdLintBatchOptions.rules_path`"; **that is inaccurate** and is not edited here (the correction
belongs to this report and to the captain's acceptance matrix).

**(ii) `VdLintBatchOptions` has no autofix field.** Its fields are exactly `ruleset`, `rules`,
`waiver_files`, `slang_suppressions`, `error_limit` (plus engine/binaries/rules_path/lists/
timeouts). There is no autofix field anywhere in the Rust ABI (`grep -rn autofix crates/` finds
only `vd-settings`' stored value set). Wiring it would need `crates/vd-ffi` + `crates/vd-deeplint`
changes (header regeneration + Rust rebuild), which the contract puts out of lane scope. **Lead
ruling: keep it `TransportPending`, never send it, and prove that.** The real one-click autofix
path (the hover 「修复」 button) is a different, already-working mechanism.

## 1. What changed

### `src/workspace/settings/lint_options.h` (the ONE composition seam, header-only)

- `LintJobOptions` now carries the ABI's five fields:
  `ruleset` (0 default / 1 all / 2 none), `rules` (verbatim), `waiverFiles` (one path per entry),
  `slangWarnings` (each a `-W…` token), `slangErrorLimit` (0 = no flag). `autofix` is deliberately
  **absent** (§0.ii).
- `buildLintJobOptions()` composes **only the selected engine's knobs**; the other engine's fields
  stay empty (R1: the ABI's no-ops are never sent). Rules precedence unchanged in spirit and now
  complete: `lint.verible.rules_config` → `tools.verible_rules` → the discovered
  `VdProjectInfo.rules_path` → no flag.
- `lintKnobAbiGroup()` / `lintUnsupportedKnobGroups()` / `lintKnobBackendUnsupported()`: the
  grey-out decision now comes from the **real C ABI table** `vd_lint_unsupported_knobs(engine)`
  (with the same list as the fallback for a build without `vd_ffi.h`). The two verible-only TOOL
  options the ABI does not model (`rules_config`, `autofix`) are added explicitly, because
  vd-deeplint's verible arm emits them and the slang arm never does.
- `fillLintBatchOptions()` + `LintBatchAbiStorage`: the ONE mapping from `LintJobOptions` to
  `VdLintBatchOptions` (string storage owned by the caller for the duration of the call).
- `currentDiscoveredRulesPath()` / setter: the process-wide seam (same pattern as
  `currentLintValues()`) through which the project loader's discovered `.rules.verible_lint`
  reaches the lint job.

### `src/workspace/settings/lint_options.cpp`

`lintKnobState()`: `BackendUnsupported` follows the ABI table (+ the two tool options above);
`Ruleset/Rules/RulesConfig/WaiverFiles` (verible) and `SlangWarnings/SlangErrorLimit` (slang) are
`Supported`; **`Autofix` (verible) is the only `TransportPending`**, with the reason
"…the lint job's C ABI has no field for it (transport pending)".

### `src/workspace/settings/settings_panel.cpp`

Truthful status text: the verible page says `--ruleset/--rules/--rules_config/--waiver_files` are
applied and `--autofix` is stored only (no ABI field); the slang page says `-W`/`--error-limit` are
applied. The pending hint now lists only `--autofix` and says the values are never sent as a silent
no-op.

### `src/workspace/settings/settings_selftest.cpp`

The two assertions that encoded the **pre-wiring** state were updated (explicitly, §4), and the
acceptance checks below were added (panel values → the **real** `vd_lint_batch_command_line` echo,
through the same `buildLintJobOptions` + `fillLintBatchOptions` the product calls).

### `src/workspace/diagnostics/diagnostics_controller.cpp` — `FfiProjectLintBackend::start()` only

Composes `LintJobOptions` from `currentLintValues()`, `request.rulesPath` (the legacy
`tools.verible_rules` path) and `currentDiscoveredRulesPath()`, then fills the
`VdLintBatchOptions` through `fillLintBatchOptions()` before `vd_deeplint_batch_start()`.
*(Collision note: the i18n lane had just converted the two Chinese status strings in the same file
to `translate("VEdit", …)`; the file was re-read immediately before this edit, the converted
strings are intact, and only the `start()` body was touched — no reformatting.)*

### `src/workspace/diagnostics/diagnostics_integration.cpp`

`ProjectFileSetPoller::poll()` reads `VdProjectInfo.rules_path` before freeing the info and
publishes it through `settings::setCurrentDiscoveredRulesPath()`; `followProjectRoot()` clears it
so an old project's rules file can never lint the new root. `ffi_backends.cpp`'s per-file
`FfiSlangBackend::startLint()` now also feeds the discovered path into the same composition (its
ABI has only `rules_path`).

## 2. Acceptance evidence

### (1) Per-knob assertion: panel value → argv — SIX (`logs/04-settings-selftest.log`)

Each line is the REAL echo `vd_lint_batch_command_line()` of the struct the product backend hands
to `vd_deeplint_batch_start()` (`tool=`/`program=` elided as `…` for width; the `" | <engine knobs
note>"` suffix trimmed):

```
CHECK settings.a4.lint.argv.ruleset PASS engine=verible … --ruleset=all f.sv
CHECK settings.a4.lint.argv.rules PASS engine=verible … --rules=line-length,no-tabs f.sv
CHECK settings.a4.lint.argv.waiver-files PASS engine=verible … --waiver_files=/p/a.waiver,/p/b.waiver f.sv
CHECK settings.a4.lint.argv.autofix-not-sent PASS engine=verible … f.sv          # NO --autofix, knob still pending
CHECK settings.a4.lint.argv.slang-warnings PASS engine=slang … -Weverything -Wno-unused-variable f.sv
CHECK settings.a4.lint.argv.slang-error-limit PASS engine=slang … --error-limit=25 f.sv
```

The sixth is the Lead-approved autofix form: setting `lint.verible.autofix = "patch"` produces **no
`--autofix` argv** and `lintKnobState(Verible, Autofix).pending() == true` — R1's "never silently
send a no-op" is asserted, not assumed. `logs/04` also carries
`CHECK settings.a4.lint.state.autofix-pending PASS --autofix is stored, but the lint job's C ABI
has no field for it (transport pending).`

### (2) Defaults send NO flag

```
CHECK settings.a4.lint.argv.defaults-no-flag PASS
  engine=verible … f.sv ||
  engine=slang … --lint-only -Weverything --diag-column-unit byte --diag-json <diagnostics.json> f.sv
```

An untouched page carries no `--ruleset=`, `--rules=`, `--rules_config=`, `--waiver_files=`,
`--autofix`; the slang default has only the engine's own built-in `-Weverything` and no
`--error-limit=`.

### (3) The two configurations do not overwrite each other

Store level (pre-existing assertions, still green): `settings.a4.lint.two-sets`,
`settings.a4.lint.switch-back-slang`, `settings.a4.lint.switch-back-verible`,
`settings.a4.lint.no-crosswrite`. Argv level (new): one `LintValues` holding BOTH sets produces a
verible argv with only verible flags and a slang argv with only slang flags:

```
CHECK settings.a4.lint.argv.two-sets PASS
  engine=verible … --rules_config=/page/.rules --ruleset=all --rules=line-length --waiver_files=/p/a.waiver f.sv ||
  engine=slang … -Wno-unused-variable --error-limit=25 f.sv
```

### Rules precedence (page → legacy tools → discovered → none)

```
CHECK settings.a4.lint.argv.discovered-rules-path PASS engine=verible … --rules_config=/proj/.rules.verible_lint f.sv
CHECK settings.a4.lint.argv.hand-typed-wins PASS engine=verible … --rules_config=/page/.rules … f.sv
```

plus the pre-existing `settings.a4.lint.options.{page-wins,legacy-second,discovered-third,none}`
checks (all green). The discovered path is now REAL: `Poller` captures `VdProjectInfo.rules_path`.

### The grey-out table is the ABI's own

```
CHECK settings.a4.lint.state.abi-table PASS ruleset,rules,waiver_files || slang_suppressions,error_limit,include_dirs,defines
CHECK settings.a4.lint.state.wired PASS
CHECK settings.a4.lint.state.backend-unsupported PASS
```

### (4) Targeted suites — observed counts

| suite | command | result | log |
|---|---|---|---|
| settings | `ctest -R '^vedit-workspace-settings-selftest$'` | **1/1 Passed** (1.32 s); direct `SETTINGS-SELFTEST ok=1 checks=240 failures=0` (was 228 checks before this task) | `logs/03-ctest-…settings…`, `logs/04-settings-selftest.log` |
| diagnostics wiring E2E | `ctest -R '^vedit-b3-wiring-e2e$'` | **1/1 Passed** (6.87 s); direct `checks=71 failures=0 skips=0` (unchanged) | `logs/03-ctest-…wiring…`, `logs/05-wiring.log` |
| lint experience | `ctest -R '^vedit-lint-experience-selftest$'` | **1/1 Passed** (0.49 s); direct `checks=98 failures=0 skips=0` | `logs/03-…`, `logs/06-lint-experience.log` |
| rename E2E | `ctest -R '^vedit-rename-e2e$'` | **1/1 Passed** (1.18 s); direct `checks=21 failures=0 skips=0 not-claimed=1` | `logs/03-…`, `logs/07-rename.log` |
| project lint E2E (the REAL batch backend) | `ctest -R '^vedit-project-lint-e2e$'` | **1/1 Passed** (1.58 s); direct `checks=11 failures=0 skips=0` | `logs/03-…`, `logs/08-project-lint-e2e.log` |

## 3. Proof that the product (not just the test) uses the composition

The project pass's translation unit was preprocessed with its own recorded compile command
(`logs/09-preprocessed-tu-check.log`):

```
VEDIT_LINT_OPTIONS_HAVE_ABI -> #define VEDIT_LINT_OPTIONS_HAVE_ABI 1
VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS -> #define VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS 1
present: True
        settings::LintBatchAbiStorage abiStrings;
        const settings::LintJobOptions jobOptions = settings::buildLintJobOptions(
            settings::currentLintValues(),
            request.engine == 1 ? settings::LintBackend::Verible : settings::LintBackend::Slang,
            request.rulesPath, settings::currentDiscoveredRulesPath());
        settings::fillLintBatchOptions(jobOptions, &options, &abiStrings);
```

i.e. the composed branch — not the `#else` fallback — is compiled into
`FfiProjectLintBackend::start()`, the backend the real `vedit-project-lint-e2e` drives (11/11 green
above). The acceptance assertions use that same `buildLintJobOptions` + `fillLintBatchOptions`
pairing, so no test-local parallel mapping exists.

## 4. Test changes, stated explicitly

- `settings.a4.lint.state.transport-pending` (asserted `SlangWarnings` was pending) **was replaced**
  by `settings.a4.lint.state.wired` (the five wired knobs are `Supported`) and a new
  `settings.a4.lint.state.autofix-pending`. The old assertion encoded exactly the defect this task
  fixes; it was not weakened, it was inverted and split.
- `settings.a4.lint.verible.ruleset-pending` (asserted a "transport pending" reason) **was renamed**
  to `settings.a4.lint.verible.ruleset-wired` (reason empty = applied).
- Net: 228 → 240 checks (+12), no check asserts "pending" for a wired knob any more.

## 5. Bounds — what is NOT proven

- **No full gate / no relink of `vedit-app`** (captain's rule). The settings and diagnostics test
  binaries were rebuilt; the product binary was not relinked, so the panel texts are asserted
  through the panel widget/state, not through a screenshot of the running app.
- **The argv assertions spawn nothing.** `vd_lint_batch_command_line` is the ABI's own echo (its
  contract says "spawns nothing"); that the REAL tools accept these flags was proven in A1
  (`evidence/ux-batch-2/a1/README.md` §2.5: verible with `--ruleset=all --rules=+always-comb
  --waiver_files=<file>`, slang with `-Wno-unused-variable --error-limit=5`, both accepted by the
  real binaries). This task does not re-run the tools.
- **The five knobs reach the PROJECT pass only.** The per-file lower-layer ABI
  (`vd_deeplint_lint_v2`) has only a `rules_path` argument, so `FfiSlangBackend::startLint()` can
  carry the rules precedence but not the other five fields; that is an ABI-shape limit, not a
  choice made here.
- **The poller → settings-holder handoff** (`VdProjectInfo.rules_path` → the backend) is proven by
  the captured line in `poll()`, the compiled call site above, and the composition assertions —
  **not** by an E2E that puts a real `.rules.verible_lint` on disk and reads it out of a spawned
  argv (the batch backend does not echo its argv). Disclosed gap for the captain's matrix.
- **Invalid panel values fail loudly, not silently**: the ABI rejects an invalid ruleset code /
  rules list / `-W` token (`vd_lint_batch_command_line` returns null, `vd_deeplint_batch_start`
  refuses with the ABI's reason), so a corrupted settings.toml surfaces an error instead of being
  dropped. The product path for that failure is not re-tested here (A1's tests cover the ABI side).
- **Waiver paths cannot contain a comma** (the A1 comma-separated ABI form; documented limitation).
  A waiver path with a comma would be split by the ABI; no mitigation is invented here.
- **`-W` values are stored bare** ("everything", "no-unused-variable" — the store's documented
  form) and prefixed to `-W…` tokens here; a user typing the prefix already is accepted verbatim.
  "everything" therefore duplicates the engine's built-in `-Weverything` (harmless).
- **Shared build tree contention**: one `ninja: warning: premature end of file; recovering` was
  observed during a rebuild (another lane regenerating the shared `target/qt6-build`); it recovered
  and the build completed, and every suite above was then re-run green. No generated file was
  hand-edited.
- The i18n lane edited the same `diagnostics_controller.cpp` earlier (status strings). My edit is
  confined to `FfiProjectLintBackend::start()`; the converted `translate(...)` strings are intact
  (11 occurrences), and the file was re-read from disk immediately before editing.

## 6. Evidence index (`evidence/ux-batch-2/u6-final/`)

| path | content |
|---|---|
| `logs/01-settings-selftest-first.log` | first run: the one failure the defaults assertion had because the ABI's knobs note names flags (fixed by trimming the note in the test) |
| `logs/02-settings-selftest.log` | the 239-check run before the last `state.abi-table` assertion was added (`ok=1 checks=239 failures=0`) |
| `logs/04-settings-selftest.log` | FINAL settings self-test (`ok=1 checks=240 failures=0`) incl. all acceptance assertions |
| `logs/03-ctest-<suite>.log` | official ctest runs of the five suites (1/1 Passed each) |
| `logs/05-wiring.log`, `06-lint-experience.log`, `07-rename.log`, `08-project-lint-e2e.log` | direct runs with their observed counts |
| `logs/09-preprocessed-tu-check.log` | proof the composed branch is compiled into `FfiProjectLintBackend::start()` |

Changed files (sha256 at delivery): `lint_options.h` `64299662…f76c80`,
`lint_options.cpp` `c04d415d…9946bd`, `settings_panel.cpp` `065b2215…e15831`,
`settings_selftest.cpp` `61aedb22…a894af`, `ffi_backends.cpp` `b3921948…fb91b70`,
`diagnostics_controller.cpp` `8ba18515…8acc1`, `diagnostics_integration.cpp` `6013c0bb…2178f`.
