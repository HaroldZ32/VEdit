# T21 — command palette 10k budget + Rust-backed global search

C9 header

- collected_at / HEAD / host CPU / memory / image ids: `00-context.txt`.
- All commands were run inside the pinned images (`vedit-qt6-build:local` for the
  product/benchmark, `vedit-build:local` for Rust). No GUI display is used
  (offscreen); the screenshots are the headless capture of the real shell.

## 1. AC-S2.3 — 10,000-file palette filter ≤ 200 ms

`01-palette-bench.log` (reproducible command recorded there):

```
cmake -S src/workspace/palette -B target/ws-palette-bench-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build target/ws-palette-bench-build --target vedit-workspace-palette-bench
QT_QPA_PLATFORM=offscreen target/ws-palette-bench-build/vedit-workspace-palette-bench \
  --files 10000 --iterations 200 --limit 100 \
  --fixture target/ws-palette-bench-fixture \
  --json evidence/slice2-find/palette-bench.json --assert-p95-ms 200
```

Observed (machine: see `00-context.txt`):

| metric | median | p95 | max |
|---|---|---|---|
| core `FuzzyMatcher::filter` (10k candidates) | 0.763 ms | **1.346 ms** | 1.702 ms |
| palette keystroke path (filter + 100-row list) | 0.880 ms | **1.471 ms** | 1.953 ms |
| index cold scan (10k files, once) | — | — | 18 ms |

`BENCH VERDICT PASS p95_ms=1.471 budget_ms=200.000 files=10000 source=disk`,
`BENCH-EXIT=0`; machine-readable result in `palette-bench.json`. The same tree is
reused on later runs, so the numbers are cheap to reproduce.

## 2. AC-S2.4 — global search via the Rust index service

### Ownership (single implementation)

- `crates/vd-search/src/lib.rs::search_tree` owns the **walk, pruning, skip
  classification, literal matching, limits and time budget**, and returns hits
  plus an observable `TreeSummary` (files scanned/matched, skips, truncated,
  timed out, elapsed).
- `crates/vd-ffi/src/search.rs` projects it into the C ABI:
  `vd_search_tree_query` starts the job, `vd_search_tree_poll` returns
  `VdSearchResults` + `VdSearchSummary`; the service worker performs the scan
  (no UI-thread blocking, no per-keystroke call).
- `src/workspace/search/global_search.cpp` is now a **pure consumer**:
  `vd_search_tree_query` → poll → map to Qt hits/summary. It contains **no
  `QDirIterator`, no `QFile` read, no line matcher** (`04-search-ffi-rust.txt`
  records the grep; the only hits are the two FFI calls and the results free).
  A build without the Rust layer reports a typed Failed state instead of
  growing a Qt search backend.
- `src/workspace/search/README.md`, `global_search.h` and the palette README
  were updated to this contract; the palette table carries this run's numbers.

### UI contract (hits, clickable jump, empty state)

`03-search-selftest.log` (non-root, so the unreadable-path checks are real):
`WORKSPACE-SEARCH-SELFTEST ok=1 checks=111 failures=0 skipped=0`, including:

- `panel.summary.state / files / files-scanned`, `panel.summary.skipped-binary`,
  `panel.summary.skipped-too-large` — the summary now comes from Rust;
- `empty.no-project.visible / text`, `panel.empty.enter-query` — explicit empty
  states (no-result and no-project);
- `jump.hit-row-exists`, `jump.click.file / line / column`, `jump.activate`,
  `jump.file-row.no-jump` — clicking a hit requests the right file/line/column
  and the product jump target is invoked;
- `large.skipped-by-default`, `large.cancel.state`, `large.timeout.flag / bounded`,
  and the stalled-read lifecycle checks.

Fresh screenshots from this build: `screenshots/01-search-results.png`,
`02-search-empty-state.png`, `03-search-jump.png`, `04-search-no-project.png`
(`03b-search-screenshots.log`; the unreadable-root screenshot needs the
non-root selftest, which is covered by the log above).

## 3. Gates and observed exits

| command | observed |
|---|---|
| palette benchmark (above) | `BENCH-EXIT=0`, p95 1.471 ms < 200 ms |
| search selftest (non-root) | `SELFTEST-EXIT=0`, 111 checks / 0 failures |
| `cmake --build target/qt6-build --target vedit-app` | `APP-BUILD-EXIT=0` |
| `ctest --test-dir target/qt6-build` | **19/19 passed**, `CTEST-EXIT=0` |
| `cargo test -p vd-search` | 10 passed, 0 failed (`07-rust-checks.log`) |
| `bash build/gates/typecheck.sh` | **RED — external**: `crates/vd-vcs` (T23's in-flight VCS lane) has 7 compile errors; the Qt lane itself printed `[qt6] PASS`. Not a T21 change, and not repaired here (outside this task's write scope). |
| `cargo clippy -p vd-search -p vd-ffi` / `cargo fmt … --check` | blocked by the same in-flight `crates/vd-ffi/src/vcs.rs`; the only reported fmt diffs are in `vcs.rs`, not in T21 files. |

Because the repository-wide typecheck gate is currently red for another lane's
work, this task does **not** claim it PASS; the ownership evidence is
`06-typecheck.log` + `09-scope-check.txt` (`crates/vd-vcs/*` = OTHER-LANE).

## 4. Changed paths / honest notes

- In scope: `crates/vd-search/**`, `crates/vd-ffi/src/search.rs`,
  `src/workspace/search/**`, `src/workspace/palette/README.md`,
  `evidence/slice2-find/**`.
- `crates/vd-ffi/include/vd_ffi.h` changed as a **generated artifact**:
  `crates/vd-ffi/build.rs` runs cbindgen on every build, so the new ABI exports
  and `VdSearchSummary` appear there automatically (not hand-edited; the same
  situation T8 recorded). No other path was touched; `resources/syntax`,
  `src/editor` and the gates are untouched.
- **Streaming**: the Rust job returns one complete result; the Qt side delivers
  it in 200-hit batches after the job finishes (not live per-file streaming).
  The `slowlifecycle` checks still pass, but the old "batch before the stalled
  read" effect is no longer a real streaming guarantee — recorded here rather
  than overclaimed.
- GUI interaction was not manually exercised (no display); the evidence is the
  headless selftest + offscreen screenshots.

## 5. Evidence index

| file | content |
|---|---|
| `00-context.txt` | HEAD, host CPU/memory, image ids, T21 status |
| `01-palette-bench.log` | reproducible 10k benchmark + machine + verdict |
| `palette-bench.json` | machine-readable benchmark samples |
| `03-search-selftest.log` | 111-check non-root selftest (hits, empty, jump, limits, lifecycle) |
| `03b-search-screenshots.log` + `screenshots/` | fresh offscreen captures |
| `04-search-ffi-rust.txt` | Rust ownership, FFI symbols, no Qt walker, Rust tests |
| `05-root-build-ctest.log` | product build + 19/19 ctest |
| `06-typecheck.log` | full typecheck output incl. the external `vd-vcs` errors |
| `07-rust-checks.log` | `cargo test -p vd-search` and the scoped clippy/fmt attempts |
| `08-report.md` | this report |
| `09-scope-check.txt` | changed-path classification |
