# T4 — F1 (FIM reason duplication) and F2 (cursor at EOF) — evidence

Contract: `.mpd/plans/ci-and-followups-contract.md` §B (AC-B1..AC-B4).
Write scope used: `crates/vd-app/src/`, `crates/vd-editor/src/`. No git commit.
No existing assertion was removed, skipped or weakened.

## F1 — the status bar rendered the FIM reason twice (`FIM: FIM …`)

Changed `crates/vd-app/src/app.rs`:

- `Ui::status_text`: the FIM column now pushes the reason **verbatim** (it already
  carries its `FIM` tag). The old `format!("FIM: {reason}")` printed
  `FIM: FIM 未启用`; the column is now the **single** producer of the reason.
- `Ui::request_fim`: the disabled path no longer calls `set_info(reason)`
  (it wrote the same sentence into the message as well); a transient rejection
  (e.g. "FIM is already generating") is still shown unless it equals the
  persistent reason the column already renders.
- `build_ui`: the block that appended `"FIM: {reason}"` to the message is gone.
- `StatusModel::info()` became dead after the change and was removed. The AC-4
  unit test still asserts the message **by equality**: it splits
  `model.text(&parts)` on the `"  |  "` separator and requires one segment to be
  exactly `"LSP running (pid 7), restarts 0 (running: true, restarts: 0)"`
  (`assert!(composed.split("  |  ").any(|part| part == message))`), not a
  substring match; the 200-iteration byte-identical assertion is unchanged.
  (Review R1: the first version used `contains`, which was weaker than the
  original `assert_eq!` on the stored message.)

Behaviour (observed in `--selftest all`):

| point | before the fix (mutation, `03-f1-mutation-selftest.json`) | after the fix (`05-selftest-all.json`) |
|---|---|---|
| startup | `… | FIM: FIM 未启用 | configuration warnings: 1 | …` → failed on `FIM: FIM` | `… | FIM 未启用 | configuration warnings: 1 | …`, occurrences=1 |
| after a rejected `request_fim()` | `… | FIM: FIM 未启用 | FIM 未启用 | …` occurrences=2 | `… | FIM 未启用 | LSP running (…) | …`, occurrences=1 |

The rejection assertion reads the status text **synchronously** after
`request_fim()`: in this image a pending LSP `Status` event on the next 50 ms
poll overwrites the message and would mask a duplicate instead of proving its
absence (the first version of the check was masked exactly that way).

## F2 — the cursor landed at the end of the file on open

Changed `crates/vd-editor/src/document.rs`:

- `Document::open` places the insert mark at `start_iter()` **after**
  `Self::from_buffer(&buffer)` (which re-`set_text`s a CRLF file while
  normalising it), so the placement survives the normalisation.
- New tests (already present from the earlier partial work, kept):
  `opening_an_lf_file_puts_the_cursor_at_the_start`,
  `opening_a_crlf_file_puts_the_cursor_at_the_start`, sharing
  `assert_insert_mark_at_start` (uses `buffer.get_insert()` /
  `iter_at_mark` / `start_iter()`).

Mutation evidence (`01-f2-mutation-check.txt`): with the `place_cursor` line
removed the two tests fail with `lf: insert mark is at offset 32` and
`crlf: insert mark is at offset 34` (both at EOF), `2 failed`, exit 101.

## New assertions (AC-B3)

| Suite | Check name |
|---|---|
| smoke | `AC-B1 the FIM reason appears exactly once at startup` |
| smoke | `AC-B2 opening a file puts the cursor at 1:1` (earlier partial work) |
| smoke | `AC-B2 the status bar reports 1:1 after opening a file` (earlier partial work) |
| fim | `AC-B1 the FIM reason appears exactly once after a rejected request` |
| vd-editor tests | `document::tests::opening_an_lf_file_puts_the_cursor_at_the_start`, `document::tests::opening_a_crlf_file_puts_the_cursor_at_the_start` |

## Commands, exits and counts

| Command | Exit | Observed |
|---|---|---|
| `bash scripts/gates.sh` (lock policy + `cargo fmt --all -- --check` + `cargo clippy --workspace --all-targets -- -D warnings` + `cargo test --workspace`) | 0 | `GATES PASS (local)` / `GATES PASS (host: … in the pinned image)`; **225 tests passed, 0 failed** (baseline 223 → +2) |
| `cargo build --release --workspace --offline` | 0 | `Fresh vd-app` for the delivered sources; `target/release/vd-app` rebuilt from them |
| `xvfb-run -a target/release/vd-app --selftest all` | 0 | `"ok": true`, **62 checks** (baseline 58 → +4: 2 AC-B2 + 2 AC-B1); smoke 30, highlight 8, config 6, lsp 6, slang 6, fim 6 |
| same, second run | 0 | `06-selftest-all-run2.json`, `"ok": true`, 62 checks |

AC-B4 check-name superset: every one of the 58 checks in
`evidence/github/fix-impl/selftest-all.json` is present in the 62 (missing = []).
`cargo test --workspace` totals come from `04-gates.txt` (21 test binaries,
225 passed / 0 failed).

Mutation of the old F1 behaviour turned **exactly** the two AC-B1 checks red and
nothing else (`03-f1-mutation-selftest.json`: 2 failed checks, both AC-B1), then
the sources were restored byte-for-byte (sha256 verified in the run log).

## Not proven / notes

- The old `build_ui` info-injection is not separately observable in this image:
  `startup_messages` overwrites the message right after `build_ui`. The startup
  assertion therefore fails on the old **label** duplication (`FIM: FIM`) and on
  any duplicate present when smoke runs; the injection removal is verified by
  reading the diff, not by a distinct assertion.
- `request_fim`'s "already generating" branch (FIM enabled with an in-flight
  request) is not exercised: FIM is disabled by default in the image, so the
  same-reason guard is code-verified only.
- `poll_background`'s runtime FIM error message (`set_info(error.reason())`) is
  intentionally kept (it is a transient failure, not the persistent reason) and
  is not asserted separately.
- `docs/BUILD.md` and `build/evidence/12-gates-image-build.log` also appear
  modified in the working tree; they are **not** part of this task (`docs/` is
  the captain's, the log is rewritten by `scripts/gates.sh`). The tracked log was
  restored with `git checkout --` at the end.
- The T3 `scripts/`/`packaging/` changes and the captain's `.github/` were not
  touched.

## Source hashes (delivered tree)

```
9fb8cada22b5a5d404574c62e6f0d87e40ae5e2f3b7338eb7fd7902966183c06  crates/vd-app/src/app.rs
0ab67496366985c07aa034073f665cb3016985435b310950bf9ff1f8412a2904  crates/vd-app/src/selftest.rs
77bbea84dfa8a5c6650f154b1a6224b6c5b8b26052ab364a54987a5b39248f62  crates/vd-editor/src/document.rs
```

`04-gates.txt`, `05-selftest-all.json` and `06-selftest-all-run2.json` are the
post-R1 runs (the earlier F1 mutation evidence `03-…` predates only the unit-test
assertion tightening; the behaviour it mutates is identical).

## Evidence files

`evidence/github/f1f2-impl/`: `01-f2-mutation-check.txt`,
`02-f1-mutation-log.txt`, `03-f1-mutation-selftest.json`,
`04-gates.txt`, `05-selftest-all.json` (+ `.err` empty),
`05-selftest-console.txt`, `06-selftest-all-run2.json`,
`06-selftest-run2-console.txt`.
