# T9 — editor baseline data layer: report

Cell `cell-20261030T020634155Z` · task **T9** · owner **Senior Engineer**
Write scope used: `crates/vd-editor/**` (implementation + unit tests + evidence),
plus the T7-REPORT disclosure fix the captain asked for
(`build/evidence/T7-REPORT.md`). No UI code: the AC-04 UI half belongs to T21.
Evidence directory: **`crates/vd-editor/evidence/`** (path segment `evidence`,
so a verifier can read it without reading implementation).

## 1. Approach: GtkSourceBuffer as the single storage, native GTK undo, no rope

The task text mentions a "rope buffer", and the captain's constraint forbids
hand-writing one. On branch A (GTK owns the text) the storage already exists, so
this crate keeps **`GtkSourceBuffer` as the single source of truth** and uses the
buffer's **native `GtkSourceUndoManager`** for undo/redo. The only hand-written
code is the glue the task allows: LSP position model, per-line line-ending
preservation, atomic save, find/replace matching, jump-to-line data and dirty
tracking. There is no rope, no second model and therefore nothing for T21 to keep
in sync.

Two runtime probes decided this (raw commands + output in
`evidence/headless-gtk.txt`):

- a `GtkTextBuffer` can be created, filled and read **with no `DISPLAY`**
  (`no-display buffer ok: hello`, exit 0) — so the unit tests run headless, in CI,
  with no Xvfb;
- the `GtkSourceBuffer` undo manager works headless and restores text
  (`manager=… can_undo=1` → `after undo text=[] can_undo=0 can_redo=1` →
  `after redo text=[hello]`) — so undo/redo is upstream behaviour, not ours.

The buffer also records the initial text load as one undo action; `Document`
deliberately exposes **only its own edits** through `can_undo`/`can_redo`, so
undoing a freshly opened file cannot empty it (`freshly_loaded_document_cannot_undo_the_load`).

### Dependencies used (no lane-side dependency changes)

The GTK stack was pre-installed by the skeleton task (captain ruling ①); this
lane added no dependency:

| crate | version | license |
|---|---|---|
| `gtk` (gtk-rs bindings) | 0.18.2 | MIT |
| `glib` (glib-rs bindings) | 0.18.5 | MIT |
| `sourceview4` (GtkSourceView 4 bindings) | 0.5.0 | MIT |
| GtkSourceView C library (runtime, from the T7 image) | 4.6.1 | LGPL-2.1-or-later |
| GTK3 C library (runtime, from the T7 image) | 3.22.30 | LGPL-2.1-or-later |

## 2. What the data layer delivers (contract success criteria)

| criterion | implementation | tests (all in `cargo test -p vd-editor`) |
|---|---|---|
| edit / undo / redo round trip | typed insert/delete/replace over the buffer, one undo step per user action; grouping of multi-step operations (`replace_all` = one step) | `edit_undo_redo_round_trip`, `replace_is_one_undo_step`, `find_and_replace` (undo of `replace_all`), `empty_insert_does_not_create_an_undo_step`, `freshly_loaded_document_cannot_undo_the_load` |
| CRLF file saves with unchanged line endings | every line break's ending is remembered and reconstructed on save (`LineEnding::{detect,assemble}` + edit bookkeeping) | `crlf_file_saves_with_crlf_line_endings` (byte-level assert: no lone `\n`), `mixed_line_endings_are_preserved_line_by_line` (a `\r\n`/`\n` mix stays exactly per line), `undo_after_an_edit_keeps_the_original_line_endings` |
| atomic save failure never damages the original file | temp file in the target directory → write → fsync → (hook) → rename → directory fsync; cleanup on failure | `failure_while_writing_the_temp_file_keeps_the_original`, `crash_before_rename_keeps_the_original`, `permission_denied_at_rename_keeps_the_original`, `missing_parent_directory_is_rejected_without_touching_anything`, plus `atomic_save_failure_keeps_the_file_and_the_dirty_flag` at the Document level — each asserts the original bytes are unchanged and no temp file is left behind |
| LSP positions on Chinese/emoji files | `Position`/`Range` are 0-based with `character` in **UTF-16 code units** (LSP default); conversions between position, whole-document character offset and `GtkTextIter` | `chinese_and_emoji_utf16_offsets`, `positions_round_trip_on_mixed_text`, `lsp_positions_on_chinese_and_emoji` (asserts exact offsets and rejects a position inside the emoji's surrogate pair with `Error::SplitSurrogate`) |

Additional behaviour the task lists:

- **incremental editing**: `insert`, `delete`, `replace` by LSP `Range`; each call
  is one undo step and keeps the line-ending bookkeeping in sync.
- **find / replace matching**: built on GTK's own text search
  (`TextIter::forward_search` / `backward_search`), with optional
  case-insensitivity: `find_next`, `find_previous`, `replace_next`, `replace_all`
  (the latter is a single undo step).
- **go to line data**: `line_count`, `line_text`, `line_start_position`,
  `line_range`, plus `iter_at_position` for the UI to scroll/select.
- **unsaved-changes / close confirmation data**: `is_modified`,
  `needs_close_confirmation`, `mark_saved` (T21 renders the dialog).
- **open/save/save-as**: `open`, `save`, `save_as` with line-ending restoration
  and the atomic writer.
- **T21 seam**: `Document::from_buffer` wraps the `sourceview4::Buffer` the app
  already owns.

## 3. Gates (observed)

`crates/vd-editor/evidence/gates-vd-editor.txt` — scoped to this crate because the
workspace is currently being edited by other lanes:

```
$ cargo fmt -p vd-editor -- --check                      exit=0
$ cargo clippy -p vd-editor --all-targets -- -D warnings exit=0
$ cargo test -p vd-editor                                exit=0
  running 36 tests
  test result: ok. 36 passed; 0 failed
```

Full log: `evidence/cargo-test-vd-editor.log` (`exit=0`).

**Honest workspace state:** `cargo test --workspace` is currently **red, and not
because of this crate**: `vd-syntax` (T11) is being written concurrently and one
of its in-progress tests fails (`detect::tests::supported_table_is_exactly_the_contract_table`,
a sort-order assertion in `crates/vd-syntax/src/detect.rs`), and `vd-config` (T13)
has uncommitted in-flight files. The captured workspace run is
`evidence/cargo-test-workspace.log` (`exit=101`) for the reviewer to inspect; this
lane did not touch those crates and did not "fix" their tests. The full repo gate
should be re-run once the lanes commit.

## 4. Design decisions and bounds

- **One writer**: line-ending bookkeeping is maintained by `Document`; T21 must
  mutate text through `Document`'s methods, not by writing to the buffer directly,
  or the bookkeeping (and the per-line endings) can drift.
- **Headless tests**: `sourceview4::Buffer::new` asserts GTK was initialised on
  the main thread. The buffer itself is display-free (probe above), so
  `Document`'s private `new_buffer()` uses the normal constructor when GTK is
  initialised and `glib::Object::new::<Buffer>()` otherwise. Application code
  always takes the normal path after `gtk::init`.
- **GTK is not thread-safe**: the document tests serialise buffer creation and
  use with a test-only mutex (36 parallel tests pass; without the lock the test
  binary segfaulted, recorded in the T9 work log, not shipped).
- **Mixed line endings**: unchanged lines keep their ending byte-for-byte; a line
  break *introduced* by an edit keeps the ending style of the inserted text (a
  plain `\n` stays `\n`), and the `LineEnding::Cr` legacy-Mac style is supported
  as well.
- **Atomic save "read-only target"**: the container runs as namespace root, which
  bypasses file mode bits, so the permission-denied case is injected as the same
  `io::ErrorKind::PermissionDenied` the rename would return; the assertion (target
  untouched, no temp file) is identical.
- **LSP encoding**: UTF-16 code units are implemented (LSP's default). If a
  server negotiates UTF-8 `positionEncoding`, the glue needs an extension; noted
  for T15.
- **Search** is GTK's search, so matching semantics are upstream; the crate does
  not implement its own text matching engine.
- The **full workspace gate** is red only because of concurrently edited crates
  (see §3); this is a coordination state, not a T9 defect.

## 5. How to reproduce

```sh
# inside the pinned image (the host has no GTK development files):
docker run --rm -v "$PWD:/vedit-build" -w /vedit-build vedit-build:local \
  bash -c 'cd /vedit-build && cargo test -p vd-editor'
# or through the repo gates once the other lanes are committed:
bash scripts/gates.sh
```

Evidence: `crates/vd-editor/evidence/`
(`headless-gtk.txt`, `gates-vd-editor.txt`, `cargo-test-vd-editor.log`,
`cargo-test-workspace.log`).
