# A4 independent review — settings: font / scale / 23 format knobs / two-backend lint page (T30)

Reviewer: **Reviewer C** (read-only lane; this review edited no file under `src/**`,
`crates/**`, `translations/**` or `docs/**` — it wrote only this report).
Frozen contract: `.mpd/plans/ux-batch-2/00-plan.md` §3.A4 and the BINDING user ruling
§0.1 R1, plus `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §1 rows 6/8 and
§3.U6 (the lint-wiring close-out). Requirement numbers 6 and 8 are the handover §1
matrix numbering.
Scope: A4's user-facing claims — requirement 6 (format options and LINT options made
into settings), requirement 8 (optional UI font size, instant; interface scale,
restart-effective).
Task: **T30**. This is the independent A4 pass the whole-batch review asked for
(`evidence/ux-batch-2/review-batch-acceptance/REPORT.md`, lines 49–50 and 120–126;
`review-batch-acceptance-r2/CHECKLIST.md` line 46 names this review as the verdict
of record).

**Verdict: PASS — approve (A4 lane).**
**Risk level: Medium** — the A4 lane's own delivery is complete and verified:
23 format knobs drive the real `verible-verilog-format` argv with "default = no
flag", the font applies immediately, the scale is a genuine restart switch wired
before `QApplication`, and the Lint page is one page with two independent
configurations and the two distinct disabled/pending reasons. The one **High is
carried and currently being closed by U6** (T6 in progress, T13 review): at A4
delivery six lint knobs and the discovered `.rules.verible_lint` did not reach the
lint job. That is disclosed in the A4 README and explicitly permitted by plan
§5.1.2 ("mark them transport-pending, never claim they are applied"), so it does
not send A4 back for repair — but requirement 6 must **not** be signed off as
complete until T13 passes.

## 0. Assessment (one paragraph)

A4 delivered the settings work as a coherent, test-pinned whole rather than four
independent screens. The Format page is driven by one table (`kFormatKnobs`,
`src/workspace/settings/format_options.h:65-148`) that feeds both the widgets and
`formatArguments()` (`:165-177`); an untouched page emits **no** flag, and the
`settings.a4.format.*` checks assert the one-to-one mapping and, crucially, that the
**real process** receives the same 23 flags (a fake formatter echoes its argv:
`evidence/ux-batch-2/a4/logs/20-format-argv-before-after.txt`, checks
`transport-runs` / `transport-argv`). The application seam is real:
`toolchain.cpp:491-513` appends those arguments in front of the stdin marker `-`.
The Appearance page applies `QApplication::setFont` on the spin box's
`valueChanged` (`settings_panel.cpp:441-448` → `ui_scale::applyFontPointSize`,
`ui_scale.cpp:135-162`), and the scale is genuinely read and exported **before**
`QApplication` is constructed (`main.cpp:1708` applies `QT_SCALE_FACTOR`, `:1712`
constructs the app), with 150→1.5 / 200→2 / 100→no variable verified by the real
binary's `--ui-scale-echo` line. The Lint page is one page over two persisted
halves (`LintValues`, `lint_options.h`), the engine switch keeps the other half
(`settings.a4.lint.no-crosswrite`, `switch-back-verible/slang`), and the page keeps
the two "not available" reasons apart: `BackendUnsupported` greys the control with
a reason, `TransportPending` keeps it editable and lists what is stored but not yet
sent (`screenshot 05-lint.png`). I independently ran the existing
settings-selftest binary (built 2026-11-05 17:08) on the host: **213 checks,
0 failures, 1 SKIP** (`settings.a4.scale.product-binary`, because that binary tree
has no product binary beside it) — the A4 log records 228 including the
real-product checks. What keeps this at Medium is evidence currency: the A4 README
explains the pending lint knobs as "the C ABI has no fields yet", but A1 has since
delivered `VdLintBatchOptions.ruleset/rules/waiver_files/slang_suppressions/
error_limit` (`vd_ffi.h:589-651`) and `VdProjectInfo.rules_path` (`:1376-1385`), and
during this review the U6 lane was actively rewriting
`lint_options.{h,cpp}`/`settings_panel.cpp` (mtimes 10:48–10:51) while
`ffi_backends.cpp` still passed an empty discovered path. The product gap is real
but it now belongs to T6/T13, not to A4.

## 1. What I read and ran

Read: the contract and handover sections above; `evidence/ux-batch-2/a4/README.md`,
`ASSERTIONS.md`, the logs `10`, `11`, `13`, `14`, `16`, `20`, and screenshots
`04-appearance.png`, `05-lint.png`, `06-format.png` (opened with the image reader).
Read the implementation: `src/workspace/settings/format_options.{h,cpp}`,
`lint_options.{h,cpp}` (snapshot hashes below), `ui_scale.{h,cpp}`,
`settings_panel.cpp` (appearance/font/lint/format/engine-switch blocks),
`settings_selftest.cpp` (assertion ids and the product-binary seam),
`src/app/main.cpp` (scale/env ordering, font application, `--ui-scale-echo`),
`src/workspace/diagnostics/toolchain.cpp` (`formatVerilog`),
`src/workspace/diagnostics/ffi_backends.cpp` (lint options seam),
`crates/vd-ffi/include/vd_ffi.h` (`VdLintBatchOptions`, `VdProjectInfo.rules_path`).

Ran:

```sh
# existing settings selftest binary, host, offscreen (no rebuild)
LD_LIBRARY_PATH=<docker-image libdir for libtree-sitter.so.0.25> \
QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-a4 \
  target/qt6-build/src/app/workspace-lane-settings/vedit-workspace-settings-selftest --selfcheck
#   ... SETTINGS-SELFTEST ok=1 checks=213 failures=0   (one SKIP: product-binary)
```

Snapshot (10:52:41 +08:00; the tree was being edited by the U6 lane during this
review):

| path | sha256 (first 16) | note |
|---|---|---|
| `settings/lint_options.h` | `64299662e236d5a9` | U6 added `ruleset/rules/waiverFiles/slangWarnings/slangErrorLimit`, `currentDiscoveredRulesPath` |
| `settings/lint_options.cpp` | `c04d415d49889911` | U6 |
| `settings/settings_panel.cpp` | `065b2215e8957238` | U6 updated the lint status strings at 10:51 |
| `settings/format_options.h` | `4800ff7647db9faa` | A4 delivery (unchanged) |
| `settings/ui_scale.cpp` | `e7f041dbdda44911` | A4 delivery (unchanged) |
| `diagnostics/ffi_backends.cpp` | (mtime 11-05 05:19) | **still** passes an empty `discoveredRulesPath` |

## 2. Requirement verification

### Requirement 6 — format options and LINT options as settings

**Format half: PASS.** 23 knobs (table count and `settings.a4.format.argv-count
PASS 23`); one table or the widgets and `formatArguments()`; empty value = no flag
(`default-argv PASS`, `transport-default PASS -`); configured page → exactly 23
flags in table order, and the fake formatter process echoes the same argv plus the
stdin `-` (`transport-runs` / `transport-argv`); "Restore defaults" removes every
key (`restored`, `restored-argv`, `restored-flush`); persistence is idempotent
(`idempotent`, `idempotent-flush`); old settings survive (`legacy.preserved`,
`legacy.no-new-keys`, `legacy.no-diagnostics`). `toolchain.cpp:491-513` consumes the
holder and appends the arguments before `-`, so the page → argv path has one owner.

**Lint half: PASS for the page contract; the transport is carried (H1).**
One page, two independent halves (`LintValues` holds verible + slang; both are read
and written always; `writeLintEntries` never touches the other half); engine switch
keeps the other set (`settings.a4.lint.no-crosswrite PASS`,
`switch-back-verible/slang PASS`); the two "not available" states are distinct
(`state.backend-unsupported`, `state.transport-pending`; `lintKnobState()` in
`lint_options.cpp:61-109`); `rules_config` is supported and routed through
`buildLintJobOptions()`; the page shows the pending list
(`settings_panel.cpp:2193-2218`, screenshot `05-lint.png`). In the tree I reviewed
at A4 delivery, the six engine-supported knobs were `TransportPending` and the
discovered rules path was never passed (`ffi_backends.cpp:602`, empty variable) —
this is H1 below, honestly disclosed in the A4 README §4.1/§4.2 and in the plan's
permitted `transport-pending` state.

**`.rules.verible_lint` discovery:** at A4 delivery the A4 lane stopped at the
`crates/vd-ffi` boundary and reported it (the contract told it to stop, not to edit
the FFI). A1 has since published `VdProjectInfo.rules_path` (`vd_ffi.h:1376-1385`);
the remaining consumer wiring is U6/T6 (`ffi_backends.cpp` still empty at the
snapshot).

### Requirement 8 — font instant, scale on restart: **PASS**

* Font: `fontPointSizeSpin_::valueChanged` → `applyFontPointSize(value)` and persist,
  with the `updating_` guard (`settings_panel.cpp:441-448`); the implementation uses
  `QApplication::setFont` and repaints top-level windows (`ui_scale.cpp:135-162`);
  startup calls `rememberStartupFont()` + `applyFontPointSize(...)` before the shell
  is built; setting 0 restores the captured startup font. Checks:
  `settings.a4.font.applied PASS 9`, `font.qapplication`, `font.product-line
  PASS ... font_point_size=19` (the real product process), `font.system`,
  `font.stored`, `font.restart-*`.
* Scale: `main.cpp:1708` calls `applyScaleEnvironment(readValues(...).scalePercent)`
  **before** `QApplication app(argc, argv)` at `:1712`; the mapping is
  125→`1.25`, 150→`1.5`, 200→`2`, and 100 → **no** override (an ambient HiDPI value
  is left alone) (`ui_scale.cpp:74-86, 115-124`); the page states the restart
  requirement (`04-appearance.png`: "Scale 150 %: the next start sets
  QT_SCALE_FACTOR=1.5 — restart VEdit to apply it."). The real binary's echo line
  proves the restart path: `UI-SCALE scale_percent=150 qt_scale_factor=1.5`,
  `...=200 qt_scale_factor=2`, `...=100 qt_scale_factor=<unset>` (logs `10`/`11`);
  `vedit-ui-scale-restart` passes (`logs/11`, 2/2).

### Settings-schema health

`crates/vd-settings` schema: the A4 log records `cargo test -p vd-settings` 24
passed and `fmt`/`clippy` green (`logs/13`); the A4 selftest's legacy checks show an
old `settings.toml` keeps its values and gains no new keys. I did not rerun the Rust
tests (bounds).

## 3. Findings (ranked)

### H1 — High (carried; closure in flight under U6/T6, review T13) — six lint knobs and the discovered rules file do not reach the lint job in the snapshot

**What/where:** at A4 delivery `LintJobOptions` had only `backend` + `rulesPath`,
`buildLintJobOptions()` composed only the rules path, and `ffi_backends.cpp:601-610`
passed a hard-coded empty `QString discoveredRulesPath;` into it. The six stored-
but-unsent knobs are `lint.verible.{ruleset,rules,waiver_files,autofix}` and
`lint.slang.{warnings,error_limit}` (the A4 README §4.1 lists them; the page shows
them as `TransportPending`). At my snapshot (10:52) the U6 lane had already added
five of the six to `LintJobOptions` in `lint_options.h`
(`ruleset/rules/waiverFiles/slangWarnings/slangErrorLimit`, plus
`currentDiscoveredRulesPath`), and `settings_panel.cpp` now says ruleset/rules/
rules_config/waiver_files are applied while `autofix` is the only one stored-only —
but `ffi_backends.cpp` (mtime 11-05 05:19) still passed the empty discovered path
and only `rulesPath` into `vd_deeplint_lint_v2`, so the **product path** could not
yet send them. `autofix` genuinely has no `VdLintBatchOptions` field
(`vd_ffi.h:589-651`), so it legitimately stays pending; the other five are an
ABI→consumer wiring step now owned by T6.

**Why it matters:** requirement 6's LINT half is "make the LINT options into
settings" — a settings page whose values are not applied is a half-delivery. The
A4 lane disclosed it (per plan §5.1.2) and must not be blamed for the missing ABI,
but the acceptance matrix must not mark item 6 complete until T13 records the
wiring. The whole-batch review reached the same conclusion
(`review-batch-acceptance/REPORT.md` "High 3 — item 6 (A4/U6)").

**Direction of fix (described only; T6's scope):** U6 fills
`LintJobOptions` (already started), extends `buildLintJobOptions`, passes
`currentDiscoveredRulesPath()` / the ABI `VdProjectInfo.rules_path` into it, and
fills `VdLintBatchOptions` in `ffi_backends.cpp`; T13 must assert the page value →
argv for each of the five and the discovered-path fallback, plus that `autofix`
stays unsent with its reason. No A4 repair.

### M1 — Medium — A4's rationale and acceptance wording are stale after A1 landed

**What/where:** the A4 README §4.1/§4.2 and the pre-U6 `lint_options.h` comments
say the six values cannot be sent because "the C ABI has no fields" and
`VdProjectInfo` "does not expose" the discovered rules path. As of the current
tree, A1 has published typed fields for five of the six
(`VdLintBatchOptions.ruleset/rules/waiver_files/slang_suppressions/error_limit`,
`vd_ffi.h:589-651`) and `VdProjectInfo.rules_path` (`:1376-1385`), plus the
`vd_lint_batch_command_line` / `vd_lint_unsupported_knobs` seams (`:2246`, `:2262`).
Only `autofix` has no field. So the remaining reason is "U6 has not wired the
consumer yet", not "the ABI lacks it". The A4 README is a delivery-time record and
should be superseded in the acceptance matrix by T6/T13 plus the current ABI, not
quoted as current. (The user-facing page strings were updated by U6 at 10:51; the
old wording "the lint job's C ABI does not carry them" existed at my 10:47 read and
must not survive into the final package.)
**Direction:** cite U6/T13 for the lint wiring; keep the A4 README as the
delivery-time record with a superseded note; ensure the final UI text says what is
actually applied vs stored.

### N1 — Nit — ctest counts in A4's evidence are from the earlier suite

`logs/16-full-ctest-sequential.log` = **42/42**; the current suite is **50**
(handover §2). The A4-specific tests still exist and pass in the current tree, and
my independent run of the settings selftest binary was 213/0/1-SKIP (the A4 log's
228 includes the real-product checks that the subdirectory binary cannot run). No
defect; do not quote 42 as the current total.

### N2 — Nit — a green test for a path the product did not take

`settings.a4.lint.options.discovered-third` proves `buildLintJobOptions()`'s third
precedence (discovered path) with a synthetic argument, while the only product
caller passed an empty one (`ffi_backends.cpp:602`). The test is correct but was
test-green/dead-path until U6; the README discloses it (§4.2). After U6 the caller
should be asserted too (T13).

### N3 — Nit — A4 README §6.7's two-candidate settings path is a real edge case

The startup reader looks for `settings.toml` in `AppConfigLocation(pre-QApplication)`
then `$XDG_CONFIG_HOME/VEdit/vedit-app/`, first existing wins; the font/format page
reads the post-construction store. If a user has both files, the scale can come from
one and font/format from the other. The writer disclosed it (`startupSettingsCandidates`
comment, README §6.7); I did not reproduce it. A one-line acceptance carry, not a
blocker.

## 4. Declined to judge (reason on the same line)

- The final U6 wiring and its verdict — T6 in progress / T13 review; my snapshot was
  mid-edit (lint_options and settings_panel changed while I read them).
- The Rust `crates/vd-settings` schema and `crates/vd-deeplint` argv builder beyond
  the seams I read — their own tests; I read the A4 log (24 passed) and the C ABI
  header, I did not rerun them.
- Real DPI/scaling perception on a desktop — no real GUI; the scale evidence is the
  environment/`QT_SCALE_FACTOR` value, and the A4 README correctly discloses that Qt
  applies it.
- The editor's (KTextEditor) own font — explicitly out of A4's scope and disclosed in
  the page text (`04-appearance.png`) and README §6.1.
- Whether the chosen 23-flag set is the ideal subset of verible's options — the plan
  named the major ones (F3); I verified wiring, defaults and argv, not formatter taste.
- Individual audit of all 228 A4 assertions — I ran the existing binary (213/0/1 SKIP)
  and read the assertion ids and logs.

## 5. Bounds of this review

- **No build.** Per the shared-build-tree notice I did not run `cmake --build`,
  `build-qt.sh`, `scripts/gates.sh` or any gate. The tree was mid-U6 during the
  review (`lint_options.{h,cpp}` 10:48–10:50, `settings_panel.cpp` 10:51); I did not
  treat that in-flight state as a defect and I did not rebuild.
- I ran the **existing** `vedit-workspace-settings-selftest` binary (built
  2026-11-05 17:08) on the host: **213 checks / 0 failures / 1 SKIP**
  (`settings.a4.scale.product-binary`, no product binary in that build dir). It
  predates the U6 edits, so it verifies the A4 delivery, not the in-flight U6 state.
- I did **not** run `vedit-app --ui-scale-echo` myself; the scale evidence comes
  from the A4 logs (real product: 150→1.5, 200→2, 100→<unset>) and from the code
  ordering in `main.cpp` (env at :1708, `QApplication` at :1712).
- **No real desktop GUI.** The screenshots (`04/05/06`) are offscreen artefacts
  inspected with the image reader; I did not resize a real window or observe font
  application on a live desktop.
- I did not rerun `cargo test -p vd-settings` (log says 24 passed) or the full
  50-test ctest suite.
- Snapshot hashes are recorded in §1; the U6 lane's files changed during the review,
  so a later reader should re-check `lint_options.h` / `ffi_backends.cpp` rather than
  treating my 10:52 snapshot as final.
- Nothing in this review edited `src/**`, `crates/**`, `translations/**`, `docs/**`
  or the build tree; only `evidence/ux-batch-2/review-a4-final/` was written.

## 6. What would change the verdict

- A Blocker/High in A4's own delivery would appear if the format argv were not
  one-to-one (it is asserted both in-process and through a real child), if the font
  were not applied immediately (it is, and the real product reports 19), or if the
  scale env were set after `QApplication` (it is set before, at `main.cpp:1708`).
  None of those holds.
- The carried H1 turns into a requirement failure only if T13/T6 do not land before
  acceptance; that is a batch-level decision, not an A4 repair.
- If the final package still shows the pre-U6 wording ("the C ABI does not carry
  them"), that is M1 — a stale user-facing claim — and must be fixed with U6.

**Verdict: PASS / approve — A4 lane (risk Medium; one High carried under T6/T13).**
A4's settings delivery is complete and test-pinned; requirement 6's lint transport
and the discovered-rules consumer are U6's close-out, in flight at review time, and
the acceptance matrix must cite T13 for them.
