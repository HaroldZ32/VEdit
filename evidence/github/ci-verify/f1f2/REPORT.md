# Verification — F1/F2 follow-ups (T4) against contract v2, AC-B1..AC-B4

Reviewer (T6) · 2026-10-30 · verdict: **PASS** for AC-B1..AC-B3, **PASS with one recorded Low finding** for AC-B4

| item | value |
|---|---|
| reviewed state | **uncommitted working tree** on branch `docs/branching-flow`, HEAD `40d344d` (T4 had not committed at review time) |
| F1/F2 files & hashes | `crates/vd-app/src/app.rs` `3b09f034…a68d2` · `crates/vd-app/src/selftest.rs` `0ab67496…a2904` · `crates/vd-editor/src/document.rs` `77bbea84…48f62` |
| F1/F2 patch | `git diff -- crates/` → `f1f2.patch`, sha256 `95e1ef20868a8f48bf8979e826b860966e76493a4f91cd7ff5959de7fb443080` |
| binary verified | `target/release/vd-app` sha256 `ac830b04aa4ec38641ff1a20846ec3701851119f28767d68a44fd1339baaf6c7` |
| environment | pinned image `vedit-build:local`, host 16 cores |
| diff size | `app.rs` +21/−20, `selftest.rs` +43/−0, `document.rs` +42/−0 (test files: **0 deletions**) |

Honesty note: a first run (`20-verify-INVALID-concurrent-edits.log`) was **discarded** — T4 was still
editing `app.rs`/`selftest.rs` while it ran (file hashes changed mid-run, clippy saw a half-written tree).
Everything below comes from the clean run `21-verify.log`, whose start and end hashes are identical.

---

## AC-B1 — the FIM reason appears exactly once · **PASS**

Requirement: at startup and after a rejected/failed `request_fim`, the status text contains the reason
exactly once, never `FIM: FIM …`, and the LSP state + `set_info` messages stay visible.

Observed (selftest details, `21-selftest-all.json`):
- startup: `reason="FIM 未启用", occurrences=1, text="1:1 | systemverilog | verible 0 | slang 0 | FIM 未启用 | configuration warnings: 1 | Verible LSP: generation 0"`
- rejection path: `reason="FIM 未启用", occurrences=1`, text keeps `LSP running (pid 1194), restarts 0 (running: true, restarts: 0)` and `Verible LSP: generation 6`
- no `FIM: FIM` substring in either; `set_info` messages (`configuration warnings: 1`, `LSP running…`) and the
  live LSP column are both still rendered.

Implementation reviewed (`crates/vd-app/src/app.rs`):
- `Ui::status_text` pushes the **bare** reason (the reason text already carries its `FIM` tag), and it is now
  the **single** producer;
- the startup block that appended `"FIM: {reason}"` into the message is gone;
- `Ui::request_fim` no longer writes the persistent reason into the message (it used to be the second
  producer); a *transient* rejection still produces a message when it differs from the persistent reason,
  and FIM-enabled requests still get `"FIM: waiting for a completion (Esc cancels)"` (not `FIM: FIM`).
- `StatusModel::info()` was removed because nothing reads it any more (its only caller was a test — see AC-B4).

Commands/exit: `xvfb-run -a target/release/vd-app --selftest all` → exit **0**, 62/62 checks.
Evidence: `21-verify.log`, `21-selftest-all.json`.

## AC-B2 — cursor at the document start after `Document::open` · **PASS**

- `crates/vd-editor/src/document.rs: Document::open` now calls
  `document.buffer.place_cursor(&document.buffer.start_iter())` **after** `Self::from_buffer(...)` returns,
  i.e. after the CRLF normalisation re-`set_text` (confirmed: `from_buffer` re-sets the text when
  `LineEnding::detect` normalises — so the placement happens on the finished buffer).
- Unit tests (headless, `cargo test -p vd-editor opening_`) → exit **0**,
  `opening_an_lf_file_puts_the_cursor_at_the_start` and `opening_a_crlf_file_puts_the_cursor_at_the_start`
  both pass (2 passed, 42 filtered).
- Smoke: `AC-B2 opening a file puts the cursor at 1:1` → `cursor=Position { line: 0, character: 0 }`;
  `AC-B2 the status bar reports 1:1 after opening a file` → text starts with `1:1`.
- Black-box e2e (independent of the assertions): XTest types `a` into a freshly opened `a.sv`, then Ctrl+S.
  Result `TYPING_WORKS`, and the saved-file diff is `-module a_top;` / `+amodule a_top;` — the first keystroke
  now lands at **offset 0**. (Before this follow-up the same probe appended at EOF — see
  `evidence/github/fix-verify/25-typing-keystroke-position.txt`.) Control still `CONTROL_WORKS`.
  Evidence: `22-typing-cursor-start.txt`.

## AC-B3 — new regression assertions · **PASS**

All three required assertions exist, run and pass:
1. `crates/vd-editor/src/document.rs` `mod tests`: LF and CRLF samples, insert mark compared to
   `start_iter()` via a shared `assert_insert_mark_at_start` helper.
2. `crates/vd-app/src/selftest.rs: smoke`: after opening the fixture — cursor `Position(0,0)` **and** the
   status text starts with `1:1`.
3. `crates/vd-app/src/selftest.rs: fim`: after a rejected `request_fim`, the reason occurs exactly once and
   `FIM: FIM` is absent (read synchronously, before a poll could overwrite a transient message).

## AC-B4 — nothing weakened, counts only grow, gates green · **PASS (one Low finding, R1)**

| baseline (verified state after `7acd487`) | after F1/F2 | delta |
|---|---|---|
| `--selftest all` **58** (smoke 27, highlight 8, config 6, lsp 6, slang 6, fim 5) | **62** (smoke 30, highlight 8, config 6, lsp 6, slang 6, fim 6) | **+4**, all four are the new AC-B assertions |
| `cargo test --workspace` **223** | **225** | **+2** (the LF + CRLF document tests) |
| test-file deletions | 0 (`selftest.rs` +43/−0, `document.rs` +42/−0) | — |

Gates re-run in the pinned image (`20-run-verify.sh` → `21-verify.log`):
`cargo fmt --all -- --check` exit 0 · `cargo clippy --workspace --all-targets -- -D warnings` exit 0 ·
`cargo test --workspace` (repo gate env, `VEDIT_*` unset) exit 0, 225 passed ·
`cargo build --release --workspace` exit 0 · `xvfb-run -a target/release/vd-app --selftest all` exit 0, 62/62.
No `#[ignore]`, no skip, no check deleted; all 58 previous check names still present.

---

## Findings

### R1 · Low · an existing unit-test assertion was relaxed (not a coverage loss)
- **What/Where:** `crates/vd-app/src/app.rs` `mod tests`, test
  `repeated_lsp_status_refreshes_keep_the_status_text_bounded`: the closing assertion changed from
  `assert_eq!(model.info(), "<exact message>")` to `assert!(model.text(&parts).contains("<message>"))`
  because `StatusModel::info()` was removed.
- **Why it matters:** AC-B4 says existing checks must not be weakened; equality → substring is literally a
  relaxation. In practice the property is still enforced **byte-exactly** by the loop above it
  (`text == expected` for 200 polls, and `expected` contains the message), so the test as a whole is not
  weaker; the removed line was a redundant restatement.
- **Direction (not applied):** keep the exact form, e.g. assert the message segment of `model.text(&parts)`
  equals the expected message, or keep `info()` and its equality assertion.
- **Verdict impact:** recorded under AC-B4, does not block.

### R2 · Low · process: T4 left no evidence of its own, and the tree mixes two tasks
- **What:** at review time the F1/F2 changes were **uncommitted** on `docs/branching-flow`, sharing the
  working tree with T3's CI/scripts changes (`scripts/lib.sh`, `packaging/**`, `docs/BUILD.md`,
  `scripts/docker/**`), and T4 had produced no evidence directory.
- **Why it matters:** the reviewed state is reproducible only by file hash (`f1f2.patch` sha256), not by a
  commit; and committing all of `git status` at once would mix T3 and T4 in one commit.
- **Direction:** commit `crates/**` as the F1/F2 change (on a `fix/…` branch per the new AGENT.md flow) and
  keep the T3 script/packaging files in their own commit; the verifier evidence is supplied here.

### Declined to judge
- Whether the FIM reason should be persistent in the bar at all — AC-B1 explicitly requires it to be visible
  exactly once, so it is accepted as designed.
- The `None` arm of the AC-B1 startup check (it fails the check when there is no persistent reason) — the
  suite also asserts `FIM is disabled by default`; a config that enables FIM would legitimately change both.
- `Document::from_text` / `from_buffer` cursor placement — AC-B2 covers `Document::open` only, and an empty
  untitled buffer has its insert mark at the start anyway.
- The `matches("")` edge in the fim check when `reason` is `None` — guarded by `!reason.is_empty()` in the
  pass condition; no panic.

## Verdict

**PASS** for AC-B1, AC-B2, AC-B3 and AC-B4, with R1 recorded as a Low note (assertion relaxed but the
property still covered byte-exactly) and R2 as a process note for the captain. The F1/F2 behaviour is
verified both by the new assertions and black-box (status text shows the reason once; the first keystroke
after opening a file lands at offset 0 for LF; CRLF is covered by the unit test).
