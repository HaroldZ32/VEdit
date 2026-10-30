# issue #1/#2 fix — implementation evidence (T1)

Scope: `crates/vd-app/src/` only (contract `.mpd/plans/issue-1-2-fix-contract.md`,
AC-1..AC-8). No commit was made; the working tree carries the change.

- Product code changed: `crates/vd-app/src/app.rs`, `crates/vd-app/src/document_tab.rs`
- Selftest: `crates/vd-app/src/selftest.rs` (additions only: 201 added lines, 0 removed)
- Not touched: `README.md`, `Cargo.toml`, `Cargo.lock`, `rust-toolchain.toml`,
  gate scripts, existing assertions.
- Binary under test: `target/release/vd-app`
  sha256 `82477a6aa65de1b1061b0df8a0e85c1ca87c0d75e4fc7497bdf89a864a4f9e52`
  (see `binary-sha256.txt`), built from git HEAD `c5216c1` + this diff.

## What changed

| Root cause | Change |
|---|---|
| R1 focus | `Ui::focus_editor()` (`app.rs`); called after `window.show_all()` and from the notebook `switch_page` handler, so the current tab's `sourceview4::View` owns the keyboard focus at startup and after every tab change. |
| R2 status unbounded | New `StatusModel` (`app.rs`): the last `set_info` message plus the live LSP status. `sync_lsp_status()` now REPLACES the LSP status (`StatusModel::set_lsp`) instead of appending it to the message; `status_text()` composes message + one LSP state. `refresh_status()` skips `set_text` when the text is unchanged. |
| R2 gutter amplifier | `Tab::marks_key` (`document_tab.rs`) records the diagnostic set the marks were built from; `refresh_gutter_marks_inner()` returns early when `worst_severity_per_line()` is unchanged, so the 50 ms poll no longer deletes/recreates every mark (and no longer re-enters `refresh_status` through `mark-set`). |
| R3 early return | `poll_background()` no longer returns when `state.lsp` is `None`; it only skips the LSP event drain, so SLANG and FIM are still polled. |
| R4 overlay | `overlay.set_overlay_pass_through(&ghost_fixed, true)` in `build_ui`. |

## AC evidence

All commands ran in the pinned image `vedit-build:local` (root; host uid mapping),
with `HOME=/vedit-build/target/home`, `CARGO_HOME=/vedit-build/target/cargo-home`,
`RUSTUP_HOME=/opt/rust`, repo mounted at `/vedit-build`.

| AC | Command | Observed | File |
|---|---|---|---|
| AC-1 | `xvfb-run -a target/release/vd-app --selftest all` | check `AC-1 the current tab's editor view has the keyboard focus at startup` = true, detail `focus_is_editor=true, focused=Some("GtkSourceView"), current=Some("Untitled")` | `selftest-all.json` |
| AC-2 | same | `AC-2 … after opening a file` = true; `AC-2 … after a page switch` = true | `selftest-all.json` |
| AC-3 | same | `AC-3 the ghost-text overlay is pass-through` = true (`overlay_pass_through=true`; gtk-rs getter `is_overlay_pass_through` = C `gtk_overlay_get_overlay_pass_through`) | `selftest-all.json` |
| AC-4 | same + `cargo test --workspace` | selftest: `AC-4 120 background polls keep the status text byte-identical` = true, `polls=120, len=176, lsp_occurrences=1, len_after=176`; display-free unit tests `app::tests::repeated_lsp_status_refreshes_keep_the_status_text_bounded` and `status_text_renders_the_message_and_at_most_one_lsp_state` pass | `selftest-all.json`, `13-cargo-test-workspace.txt` |
| AC-5 | same | `AC-5 unchanged diagnostics keep the same gutter mark objects` = true, `marks_before=1, marks_after=1, same_objects=true, worst={0: Error}` | `selftest-all.json` |
| AC-6 | same | `AC-6 poll_background consumes a SLANG report while the LSP bridge is absent` = true, `sent=true, before=false, after=true, lsp_present=false` | `selftest-all.json` |
| AC-7 | `APP_BIN=/vedit-build/target/release/vd-app bash evidence/github/repro/probe-cpu.sh` (CPU_WINDOWS default `2 3 5 10 20 20`) | `SUMMARY: app_age=63s first_window_pct=0.0 last_window_20s_pct=0.2 growth_pp=0.2`, `RESULT: SAMPLED`, exit 0. Pre-fix baseline (`evidence/github/repro/cpu-before-fix-60s.txt`): `36.7% → 97.4%`, `growth_pp=60.7` | `cpu-after-fix-60s.txt` |
| AC-8 | `bash scripts/gates.sh` inside the image | `PASS[lock]`, fmt, clippy, test all pass, `GATES PASS (local)`, exit 0 | `14-gates-sh.txt` |
| AC-8 (selftest) | `xvfb-run -a target/release/vd-app --selftest all` | exit 0, `"ok": true` on two consecutive runs; smoke 27 checks (20 pre-existing all present + 7 new AC checks), highlight 8, config 6, lsp 6, slang 6, fim 5 = 58 total (51 before) — no pre-existing check missing | `selftest-all.json`, `selftest-all-run2.json` |

End-to-end issue #1 confirmation (not an AC by itself): the frozen injector probe
`evidence/github/repro/probe-typing.sh` reports `TYPING_WORKS` with the fixed
binary (`typing-after-fix.txt`), and `typing-diff.sh` shows the exact bytes the
keystroke added: `+a` at EOF (`typing-after-fix-diff.txt`, `xtype_exit=0`).
Pre-fix baseline: `TYPING_BROKEN` (`evidence/github/repro/typing-before-fix.txt`).

## Evidence files

- `10-release-build.txt` — `cargo build --release --workspace --offline` exit 0
- `12-selftest-console.txt`, `selftest-all.json`, `selftest-all.err` (empty)
- `12-selftest-run2-console.txt`, `selftest-all-run2.json`, `selftest-all-run2.err`
- `13-cargo-test-workspace.txt` — full `cargo test --workspace` log, exit 0
- `14-gates-sh.txt` — repository gate runner (fmt/clippy/test/lock policy), exit 0
- `cpu-after-fix-60s.txt` — 60 s CPU protocol, probe script sha256 recorded
- `cpu-after-fix-probe-script.txt` — first probe run (earlier probe revision)
- `typing-after-fix.txt`, `typing-after-fix-diff.txt`, `typing-diff.sh`
- `binary-sha256.txt`

## Not proven / observations (honest limits)

- AC-2 is verified by the selftest (focus identity) and AC-1 additionally by the
  XTest typing probe; no automated probe switches tabs via the mouse/window
  manager.
- AC-3 is verified through the overlay getter; no wheel-event probe exists to
  prove a scroll event reaches the `GtkSourceView` after the pass-through flag.
- The AC-6 check injects a SLANG report value into the real polling path; the
  FIM branch after it is not separately observed (a disabled FIM service always
  returns `None`). The early return it removes covered both producers.
- Observations outside the contract, NOT changed (out of scope):
  1. The status bar shows the FIM reason twice at startup (`build_ui` appends
     `FIM: …` to the message and `status_text()` also pushes the ghost reason).
     Pre-existing; `AC-4` is not affected.
  2. Opening a file leaves the cursor at the end of the document (the typed `a`
     in `typing-after-fix-diff.txt` appended at EOF). Not covered by the contract.
  3. The pre-existing smoke check `gutter mark is placed at the diagnostic line`
     depends on a 50 ms timer tick between `set_slang` and the assertion; it
     passed on both runs but the ordering was not changed (contract forbids
     touching existing checks).
- The contract's AC-7 mentions "同一脚本"; while T1 was in progress the probe
  script gained `CPU_WINDOWS` (default `2 3 5 10 20 20`, app age 63 s), so the
  same script now covers the >=60 s requirement. The revision used here is
  recorded as `probe_sha256=1293d32431c1e85cc79e0fad601910f89df3212e7916c308a0c29414284c3af4`
  at the top of `cpu-after-fix-60s.txt`.
