# Independent verification — issue #1/#2 fix, commit `7acd487`

Reviewer (T2) · 2026-10-30 · verdict: **approve**

| item | value |
|---|---|
| reviewed commit | `7acd48755b7ff1a4894c84a3299d99eb23277feb` (parent `c5216c1`) |
| patch | `crates/vd-app/src/{app.rs,document_tab.rs,selftest.rs}`, +357 / −29 |
| patch sha256 | `ba64bcce4e04ccf7cd88df86203e8c013c6da8e7f43f8e53c0b9db4e38a606d8` (`patch-7acd487.diff`) |
| tree at review | product paths clean (`git status --porcelain -- crates/` empty). The working tree also carries an unrelated, uncommitted edit to `evidence/wiring/README.md` (documents the 51→58 check count; not part of `7acd487`, not written by the reviewer) and the untracked `evidence/github/` |
| binary reviewed | `target/release/vd-app` sha256 `82477a6aa65de1b1061b0df8a0e85c1ca87c0d75e4fc7497bdf89a864a4f9e52` |
| environment | pinned image `vedit-build:local` (CentOS 7.9.2009, cargo 1.99.0), host 16 cores |
| frozen contract | `.mpd/plans/issue-1-2-fix-contract.md` (AC-1..AC-8 text unchanged; §1 protocol update checked, no criterion weakened) |

Method: contract read first, baselines frozen **before** the diff existed (`baseline-checks.txt`,
`00-instrument-audit.md`), then the diff, then independent re-execution of every gate and probe.
Nothing here is taken from the implementer's report.

---

## 1. AC verdicts

### AC-1 — editor owns keyboard focus at startup · **PASS**
- Selftest check `AC-1 the current tab's editor view has the keyboard focus at startup`
  → `focus_is_editor=true, focused=Some("GtkSourceView"), current=Some("Untitled")`.
- The assertion is a real **object-identity** comparison, not "non-null":
  `crates/vd-app/src/selftest.rs: focus_is_editor` upcasts the focus widget and `tab.view` to
  `glib::Object` and compares raw pointers with `std::ptr::eq`; it returns `false` when nothing is focused.
- Black-box corroboration (AC-1's user-visible effect): XTest injection + Ctrl+S.
  - before, binary `41d02dca…`: `xtype_exit=0` → `RESULT: TYPING_BROKEN`, file unchanged;
  - after, binary `82477a6a…`: `xtype_exit=0` → `RESULT: TYPING_WORKS`.
  Control (`probe-typing-control.sh`, minimal focused `GtkTextView`): `CONTROL_WORKS` in both runs,
  so the injector path itself is proven and `TYPING_BROKEN` is a property of the pre-fix app.
- Command: `xvfb-run -a target/release/vd-app --selftest all` (exit 0) and
  `APP_BIN=/vedit-build/target/release/vd-app bash evidence/github/repro/probe-typing.sh` (exit 0).
- Evidence: `20-selftest-all.json`, `21-typing-after.txt`, `10-typing-before-reproduced.txt`,
  `25-typing-keystroke-position.txt`, `evidence/github/repro/typing-before-fix.txt`.

### AC-2 — focus follows open / page switch · **PASS**
- Checks `…after opening a file` (`focus_is_editor=true, expected="format_me.sv"`) and
  `…after a page switch` (`focus_is_editor=true`), both identity-based as AC-1.
  The page-switch check switches to page 0 and compares against the **new** current tab's view, so a
  missing `focus_editor()` in the `switch_page` handler would fail it.
- Implementation: `crates/vd-app/src/app.rs: Ui::focus_editor` is called from the notebook
  `connect_switch_page` closure; opening a file adds a page and switches, so both entry points are covered.
- Command/exit/evidence: same `--selftest all` run (`20-selftest-all.json`).

### AC-3 — ghost overlay never eats pointer events · **PASS**
- Check `AC-3 the ghost-text overlay is pass-through` → `overlay_pass_through=true`, read back through
  the getter `gtk::Overlay::is_overlay_pass_through(&ghost_fixed)` (not a comment or a flag).
- Implementation: `crates/vd-app/src/app.rs: build_ui` calls
  `overlay.set_overlay_pass_through(&ghost_fixed, true)`; `Ui::overlay` is stored so the getter is reachable.
- Evidence: `20-selftest-all.json`.

### AC-4 — status text bounded, LSP at most once, display-free assertion · **PASS**
- Smoke check `AC-4 120 background polls keep the status text byte-identical` →
  `polls=120, len=177, lsp_occurrences=1, len_after=177`.
- **Display-free** regression test exists and was executed with `DISPLAY` **and** `VEDIT_*` unset:
  `cargo test -p vd-app --bin vd-app "app::tests::repeated_lsp_status_refreshes_keep_the_status_text_bounded" -- --exact`
  → `1 passed`, exit 0 (`24-testgate-repoenv.txt`). 200 refreshes, byte-identical, LSP count exactly 1.
- Implementation: `crates/vd-app/src/app.rs: StatusModel::set_lsp` **replaces** the live status;
  `Ui::status_text` composes it at render time and `Ui::sync_lsp_status` never appends to the message.
  No device or display is involved (`StatusModel` is pure data).

### AC-5 — gutter marks do not churn when diagnostics are unchanged · **PASS**
- Check `AC-5 unchanged diagnostics keep the same gutter mark objects` →
  `marks_before=1, marks_after=1, same_objects=true, worst={0: Error}` — 50 `refresh_status()` calls,
  mark identity compared by `GObject` pointer, non-empty guard present (`!marks_before.is_empty()`).
- Implementation: `crates/vd-app/src/document_tab.rs: Tab::marks_key` caches the `(line → worst severity)`
  map; `crates/vd-app/src/app.rs: Ui::refresh_gutter_marks_inner` returns before touching marks when the
  key is unchanged. Rebuild-on-change still works (pre-existing check `gutter mark is placed at the
  diagnostic line` passes with `all_marks=1`; the `slang` suite's `gutter marks the SLANG line` also passes).
- Evidence: `20-selftest-all.json`.

### AC-6 — other backends polled while `state.lsp` is `None` · **PASS**
- Check `AC-6 poll_background consumes a SLANG report while the LSP bridge is absent` →
  `sent=true, before=false, after=true, lsp_present=false` — the real `Ui::poll_background` path consumes a
  real `vd_deeplint::LintReport` through the production conversion.
- Code: `crates/vd-app/src/app.rs: Ui::poll_background` no longer contains the early `return`; the LSP
  event drain is guarded by `if let Some(bridge)`, and the SLANG and FIM blocks are reached unconditionally.
  The removed `return` was the only statement that could skip them, so the **FIM half is structurally
  satisfied**; see finding F3 for the assertion-coverage gap.
- Evidence: `20-selftest-all.json`.

### AC-7 — main-loop cost bounded (black box) · **PASS**
Same script, same image, explicit `APP_BIN`, quiet host, `host_nproc`/`host_loadavg` recorded.

| window (app age 63 s) | before `41d02dca…` | after `82477a6a…` |
|---|---|---|
| first window (3 s) | 36.3 % | 0.3 % |
| 5 s | 49.4 % | 0.2 % |
| 10 s | 77.5 % | 0.2 % |
| 20 s | 96.5 % | 0.3 % |
| **last 20 s** | **98.0 %** | **0.2 %** |
| growth (last − first) | **+61.7 pp** | **−0.1 pp** |
| host | 16 cores, loadavg 3.24 | 16 cores, loadavg 0.70 |

- last 20 s ≤ 5 %: **0.2 %** ✓ · growth ≤ 2 pp: **−0.1 pp** ✓ · runtime ≥ 60 s: `app_age=63s` ✓
- Command: `APP_BIN=/vedit-build/target/release/vd-app bash evidence/github/repro/probe-cpu.sh`
  → `SUMMARY: app_age=63s first_window_pct=0.3 last_window_20s_pct=0.2 growth_pp=-0.1`,
  `RESULT: SAMPLED`, exit 0. (Judged from `SUMMARY`; `RESULT: SAMPLED` is not a verdict token — as agreed.)
- Evidence: `22-cpu-after.txt`; before column `evidence/github/repro/cpu-before-fix-60s.txt`
  (raw capture; the reviewer also independently reproduced the pre-fix column at 97.9 % / +60.6 pp —
  `11-cpu-before-reproduced.txt`).

### AC-8 — gates green, not one existing assertion lost · **PASS**
All four contract gates re-run independently in the pinned image at `7acd487`
(`20-run-gates-after.sh` → `20-gates-after.log`, `24-testgate-repoenv.txt`):

| gate | exit | observed |
|---|---|---|
| `cargo build --release --workspace` | 0 | binary sha256 `82477a6a…` |
| `cargo fmt --all -- --check` | 0 | no diff |
| `cargo clippy --workspace --all-targets -- -D warnings` | 0 | no warnings |
| `cargo test --workspace` | 0 | **223 passed, 0 failed, 0 ignored** (baseline 221 → +2) |
| `xvfb-run -a target/release/vd-app --selftest all` | 0 | **58 checks, 58 passed** (baseline 51 → +7) |

No assertion removed, skipped or weakened:
- `git diff --numstat c5216c1 7acd487` → `selftest.rs 201 0`: **zero deleted lines in any test**;
  no `#[ignore]`/skip introduced.
- All **51 baseline check names present and passing**; the 7 additions are exactly AC-1, AC-2 ×2, AC-3,
  AC-4, AC-5, AC-6 (`20-selftest-all.json`, compared against `baseline-checks.txt`).
- Unit tests 221 → 223: the two additions are the AC-4 regression tests; no existing test disappeared.
- Existing check details remain meaningful (e.g. `gutter mark is placed at the diagnostic line`,
  `status bar reports the cursor`, `F12 definition jumps…` all still assert concrete values).

Caveat recorded honestly: a first gate run with `VEDIT_*` exported showed 2 failures in
`crates/vd-deeplint/tests/fake_binary.rs` (`missing_binary_lists_every_attempt`,
`non_executable_binary_is_not_found`). This is **pre-existing environment coupling, not this change**:
the same unchanged test binary passes 13/13 with `VEDIT_*` unset (repo gate env), and the failure is
`assertion failed: !report.is_success()` — the env-provided binary makes the lint succeed. See F4.

### Cross-cutting checks requested by the captain

1. **Existing assertions deleted / skipped / weakened:** none. `git diff --numstat c5216c1 7acd487`
   → `selftest.rs 201/0` (zero deleted lines in the test file; the only deletions anywhere are production
   lines in `app.rs` 28 and `document_tab.rs` 1). No `#[ignore]`, no skip, no filter introduced.
   All 51 baseline check names are present and passing; unit tests 221 → 223 with no removal.
2. **AC-1/AC-2 compare object identity, not "non-null":** yes — `selftest.rs: focus_is_editor` upcasts
   `window.focused_widget()` and `tab.view` to `glib::Object` and compares `as_ptr()` with `std::ptr::eq`,
   returning `false` when nothing is focused; AC-2's switch check compares against the **new** current tab.
3. **AC-4 has a display-independent assertion:** yes — `app::tests::repeated_lsp_status_refreshes_keep_the_status_text_bounded`,
   executed with `DISPLAY` unset and `VEDIT_*` unset → `1 passed` (`24-testgate-repoenv.txt`).
4. **Other per-tick GTK writes / unbounded growth:** one 50 ms timer (unchanged by the diff) →
   `Ui::poll_background`. Status bar writes are gated (`Ui::refresh_status` writes only when the composed
   text changed) and gutter marks are keyed (`Ui::refresh_gutter_marks_inner`), so the tick no longer writes
   GTK in the measured scenario. The only remaining per-tick GTK call is `Ui::position_ghost` while a FIM
   ghost is pending (F5, pre-existing, FIM-disabled by default). No append-to-state path survives — every
   `push`/`push_str` left builds a local `String`/`Vec` — so no unbounded-growth path was found.
5. **Scope:** exactly 3 files, all under `crates/vd-app/src/**`; no `Cargo.toml` / `Cargo.lock` /
   `rust-toolchain.toml`, no README, no other crate.

---

## 2. Findings (severity-ranked; none blocks approval)

### F1 · Medium · pre-existing · advisory — FIM reason duplicated in the status bar
- **What:** when FIM is unavailable, the bar shows `FIM: FIM 未启用  |  FIM 未启用` — the reason twice.
- **Where:** `crates/vd-app/src/app.rs: Ui::status_text` pushes `FIM: {reason}` into the parts, while
  `Ui::request_fim` (and the `Err` arm of `Ui::poll_background`) call `set_info(reason)`, which is then
  rendered as the message too.
- **Provenance:** **not a regression.** Reproduced on the **pre-fix binary** `41d02dca…` with its own
  suite: `--selftest fim` → `… | FIM: FIM 未启用  |  FIM 未启用  Verible LSP: generation 0`
  (`23-prefix-fim-suite.txt`). The two statements are unchanged by the commit (only the storage type moved
  from `State::info` to `StatusModel`).
- **Why it matters:** minor UI noise; it was previously masked in some runs when an LSP status event
  overwrote the message, so the fix can make it *more persistent*, but the composition is pre-existing.
- **Direction:** emit the FIM reason only once — either drop the `FIM: {reason}` part from `Ui::status_text`
  and keep the message, or keep the part and stop calling `set_info` with the same reason.

### F2 · Medium · pre-existing · advisory — opening a file leaves the caret at the end of the document
- **What:** after opening a file the caret is at EOF; the first keystroke appends there. Observed with the
  captain's probe unchanged: the saved file diffs as `+a` after `endmodule` (no trailing newline),
  i.e. the injected key landed at the very end (`25-typing-keystroke-position.txt`).
- **Where:** `crates/vd-editor/src/document.rs: Document::open` (and `from_text`) call
  `buffer.set_text(...)` and never place the cursor; `GtkTextBuffer` leaves the insert mark at the end of
  the inserted text. No cursor-setting code exists on the open path
  (`crates/vd-app/src/document_tab.rs: Tab::open`, `crates/vd-app/src/app.rs: Ui::open_path`).
- **Provenance:** **not a regression.** The commit touches no cursor code (`vd-editor` is untouched;
  `Ui::open_path` and `Tab::open` are untouched); the only new UI call is `Ui::focus_editor` →
  `grab_focus()`, which does not move the insert mark. It merely became **visible/usable** because the
  view now owns focus (GTK draws the caret only for a focused view).
- **Why it matters:** now that typing works, "click nothing, start typing" silently edits the end of the
  file — the most user-visible pre-existing wart this fix exposes.
- **Direction:** place the cursor at `Position(0,0)` in `Ui::open_path` after a successful open (or restore
  the last session position); out of this contract's scope.

### F3 · Low · evidence coverage — AC-6 asserts the SLANG half only
- **What/Where:** the AC-6 selftest check (`crates/vd-app/src/selftest.rs: smoke`) proves SLANG consumption
  with no LSP bridge, but nothing asserts the FIM half that AC-6's text also names.
- **Why it matters:** the behaviour is satisfied — `Ui::poll_background` has no early return left, so the
  FIM block is reached unconditionally — but a future edit could regress the FIM half silently.
- **Direction:** add a check that seeds a ghost FIM result with `state.lsp = None` and asserts it is polled
  (same shape as the SLANG probe).

### F4 · Low · pre-existing · gate robustness — `cargo test --workspace` fails when `VEDIT_*` is exported
- **What:** with `VEDIT_SLANG`/`VEDIT_VERIBLE_*` exported (the environment the GTK selftest needs),
  `crates/vd-deeplint/tests/fake_binary.rs::missing_binary_lists_every_attempt` and
  `::non_executable_binary_is_not_found` fail with `assertion failed: !report.is_success()`.
- **Why:** the resolver falls back to the environment binary, so a deliberately missing/non-executable
  explicit path still yields a successful lint.
- **Provenance:** not this change — `vd-deeplint` is untouched and the same test binary passes 13/13 with
  `VEDIT_*` unset (`24-testgate-repoenv.txt`; baseline clean-tree run at `d9f6911` also 13/13).
- **Why it matters:** a future verifier who runs the gates with the selftest environment exported will see
  red tests that have nothing to do with the code, and may misread them as a regression.
- **Direction:** make those two tests clear `VEDIT_*` for their child resolution (or document that
  `cargo test` runs without them); do not "fix" it by weakening the assertions.

### F5 · Low · pre-existing residual — one per-tick GTK call is left: `position_ghost`
- **What/Where:** while a FIM ghost is **pending**, `crates/vd-app/src/app.rs: Ui::poll_background` calls
  `Ui::position_ghost` on every 50 ms tick (`gtk::Fixed::move_`), so an on-screen ghost keeps a 20 Hz
  widget move.
- **Why it matters:** it is outside the AC-7 scenario (FIM is disabled by default, so the measured
  scenario never enters it) and pre-dates the commit; but it is the only remaining per-tick GTK write in
  the poll path and would resurface as "churn" if FIM is enabled.
- **Direction:** reposition only when the cursor/viewport actually changed.

### F6 · Nit — per-tick allocation churn in `sync_lsp_status`
- **What/Where:** `crates/vd-app/src/app.rs: Ui::sync_lsp_status` formats a `String` and recomposes the whole
  status text every 50 ms even when nothing changed.
- **Why it matters:** no GTK call and no growth; measured cost 0.2 % of a core, so acceptable. Only worth
  doing when touching this code again.

### F7 · Nit — `marks_key` is committed even if a mark could not be created
- **What/Where:** `crates/vd-app/src/app.rs: Ui::refresh_gutter_marks_inner` stores `worst` as the new key
  after the loop even when a `line_start_position`/`iter_at_position` lookup `continue`d, so that line would
  never be retried until the diagnostics change.
- **Why it matters:** unreachable for well-formed diagnostics (a valid line always has a start position);
  defensive hardening only.

---

## 3. Self-reported contract-external defects — regression judgement (asked by the captain)

| # | defect | regression of `7acd487`? | basis |
|---|---|---|---|
| ① | startup status bar shows the FIM reason twice | **No** | reproduced on the **pre-fix binary** `41d02dca…` (`23-prefix-fim-suite.txt`): `FIM: FIM 未启用  |  FIM 未启用` already present; the two contributing statements are unchanged by the commit |
| ② | caret at end of document after opening a file | **No** | `Document::open` performs `buffer.set_text` only and is untouched; `Tab::open`/`Ui::open_path` untouched; `grab_focus()` does not move the insert mark. Newly *visible* because the caret is drawn only for a focused view |

Both are correctly classified by the implementer as out of contract scope and unfixed. They do not block,
but F1/F2 are the two follow-ups I would queue next.

---

## 4. Declined to judge (with the reason on the same line)

- Whether the FIM reason should appear at all in the status bar — product/UX decision, not an AC.
- AC-6's FIM half judged PASS on code structure rather than on an assertion — the early return was the only
  skip path and it is gone; recorded as F3 rather than a FAIL so coverage is not mistaken for behaviour.
- Contract §1's protocol/baseline rewrite — captain-owned; I re-read AC-1..AC-8 and they are unchanged, so
  no acceptance criterion was weakened.
- `README.md` / docs — the contract assigns that to the captain at integration; correctly absent from the commit.
- `probe-cpu.sh` measures only the app process, not the `verible-verilog-ls`/`slang` children — the before
  column used the same scope, so the AC-7 comparison is apples-to-apples.
- Per-suite cross-contamination of the shared `Ui` in `--selftest all` — pre-existing harness design; the
  status counts and mark details in the run show no leakage.
- Pre-existing `fatal: Invalid pathspec magic 'exclude'` lines in `tests/e2e/gates-frozen-run.txt` —
  unrelated capture-script defect, outside this commit.
- Whether the fix's ~20 allocations/50 ms are worth removing — measured 0.2 % CPU; not worth touching here.

---

## 5. Risk assessment

**Residual risk: low.** The patch is small (3 files, +357/−29) and confined to the app shell; no dependency,
`Cargo.toml`/`Cargo.lock`/`rust-toolchain` change; no public API change outside `crates/vd-app`; the
scheduling, LSP, editor and syntax crates are untouched. The two behaviours most likely to disappoint a
user after this fix (F1 duplicate FIM text, F2 caret at EOF) are proven pre-existing and are documented
follow-ups, not regressions. The one reproducibility trap (F4) is environmental and now recorded.

## 6. Verdict

**approve** — all eight acceptance criteria are independently evidenced, the status/LSP path is bounded
(98.0 % → 0.2 % of one core, growth +61.7 pp → −0.1 pp), and not a single existing assertion was removed,
skipped or weakened (51→58 selftest checks, 221→223 tests, zero deletions in test files). The findings are
pre-existing defects, one evidence-coverage gap and two nits; none is a regression of this commit.
