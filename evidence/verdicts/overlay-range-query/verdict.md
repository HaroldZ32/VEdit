# Verdict — T96: independent review of T92 (provider range query / 10k-line wall)

## Binding declaration

- **HEAD**: `48569602d0c86fb402dc88155593d1dc06445c90` (the clean worktree checked out exactly this commit;
  it is also the current shared-tree HEAD at review time).
- **T92 commit**: `3b1bb1e feat(syntax): land the provider range query and the folding lane`
  (the folding lane in the same commit is out of this review's scope).
- Provider sources at the binding (sha256, working tree == HEAD, `git status -- src/syntax` empty):
  `syntax_provider.h 79ac063fdbdc…`, `syntax_provider.cpp 5a9fd0645176…`,
  `tree_sitter_provider.cpp 3aefd6aa30ed…`, `tree_sitter_provider.h b5950e8fd8ec…`;
  overlay lane `eeccaa26…` selftest / `5d78c565…` overlay.cpp.
- Isolation: `git worktree add --detach target/t96-clean 4856960`; the shared tree's 41 in-flight
  entries never participated in any run (the T94 red the captain warned about does not appear in
  this clean tree). Worktree removed after the runs.
- If T100 (block-comment fix) later changes `src/syntax`, this verdict must be re-checked for the new
  sha; at this binding no post-T92 change to `src/syntax/provider/**` exists (`git log -- src/syntax/provider`
  ends at `3b1bb1e`).

## Verdict: PASS — T92 can be released

The range query does **not** loosen the semantics: the whole-tree contract is preserved exactly
(per-range multiset equality and union equality on my own constructed cases), the default
implementation leaves legacy providers unchanged, multi-line nodes crossing a window are neither
missed nor duplicated, and the “independent of document size” claim is true for a fixed window
(proved, not inferred from the sweep).

## 1. Default implementation — legacy providers unchanged (PASS)

`SyntaxProvider::highlightSpans(tree, Range)` is **non-pure** with the default implemented in
`syntax_provider.cpp` as `filter(highlightSpans(tree))` using the same intersection condition as
the contract (`span.end > range.start && range.end > span.start`). My legacy-provider harness
(a provider that implements only the whole-tree method) called through a base pointer:
`DEFAULT-IMPL exact=1 whole_tree_calls=1 got=2 expected=2` (exact interval multiset), and an
out-of-range query returns an empty vector (the same empty result the old provider gives for a
query with no matches; there is no error channel on this API and no new silent-empty path — the
only new caller is the overlay, which treats an empty result as “base owns everything”, its
existing fallback). Existing call sites (`grammars` tests, provider tests, `open()` full-cache)
still use the whole-tree method unchanged. The C++ overload is hidden on a derived type that
overrides only the one-arg method, but no existing caller calls the new overload through a derived
pointer; callers use the base interface.

## 2. Union of range queries == whole-tree, exact intervals (PASS)

My own harness (source saved as `harness.cpp`, raw output as `range-union-output.txt` in this
directory) on a constructed SystemVerilog document (9,876 B / 237 lines; long metadata comment,
block comment, nested `ifdef`, `class`/`package`, a ~4 KB single line, 40 always_ff blocks) with
1,108 whole-tree spans:

- **496 constructed cases**: byte partitions (step 97 and 512), 5-line windows, 250 deterministic
  random windows (1–4,000 B), 9 windows **deep inside the longest multi-line node**
  (10%…90% of it), 60 exact span-boundary windows, full range, beyond-end, zero-length, inverted.
  Result: **`failing=0 missing_total=0 extra_total=0`** — every query returned exactly the
  intersection-filtered whole-tree result as an interval multiset (start, end, capture).
- **Union over partitions**: step 97 (102 queries) → `full=1108 union=1108 missing=0 extra=0`;
  step 512 → equal; step 4096 → equal. Overlapping windows duplicate a crossing span **across
  different queries only** (42/8/0 cross-window dups — expected), never inside one query
  (per-query exact match proves no duplicates).
- **Full-range query** `{0,size}` == whole-tree, exact (`got=1108 full=1108`, equal=1).

## 3. Multi-line nodes crossing a window (PASS)

- The longest multi-line capture is the block comment `comment` span `[66,2024)` = **1,958 bytes**.
  Windows placed at 10%…90% inside it (i.e. starting > 1,500 bytes after the node start, far more
  than the “one line” expansion) **all return the whole comment span** — no miss.
- Boundary constructions pass exactly: window starting before the node and ending inside it,
  window starting inside and ending after it, exact node range, node tail crossing the end.
- Long single line (~4 KB of assignments) and nested `ifdef`/`define` blocks are in the document
  and covered by the byte/line partitions without misses.
- Edge semantics are consistent between the two implementations: a zero-length range inside a span
  returns that span in both the default filter and the tree-sitter override (`got=1 expected=1`);
  an inverted range returns the same (empty) result from both. (The override ignores the `false`
  return of `ts_query_cursor_set_byte_range` for start>end, so an inverted range performs a
  whole-tree scan and then filters everything out — correct result, wasted work, and no caller
  passes inverted ranges; observation only.)
- Note: tree-sitter documents `ts_query_cursor_set_byte_range` as **intersection** semantics
  (“a match may be returned even if some captures fall outside … as long as part of the match
  overlaps”), so the line-expansion is conservative rather than required; it does not cause misses
  or duplicates (proven above), it only widens the scan slightly.

## 4. “Independent of document size” is true for a fixed window (PASS)

Fixed windows at the middle of documents of 1,000 / 1,920 / 4,800 / 9,600 lines (perfDocument):

| fixed window | 1.0k lines | 1.9k | 4.8k | 9.6k |
| --- | --- | --- | --- | --- |
| 2 KB `range_2k_ms` | 0.144 | 0.148 | 0.143 | **0.137** |
| 16 KB `range_16k_ms` | 0.555* | 1.103 | 1.095 | **1.087** |
| 25 % of document | 0.283 | 0.544 | 1.335 | 2.754 |
| whole tree (`full_ms`) | 1.114 | 2.193 | 5.701 | 12.880 |

\* at 1k lines the whole document is only 16 KB, so the “16 KB” window covers it; for documents
larger than the window the cost is flat (~1.09–1.10 ms).

So the range query tracks the **window**, not the document: a fixed 2 KB window is flat 0.137–0.148 ms
across a 9.6× document-size range; a 25 %-of-document window grows with the window (0.28→2.75 ms);
the whole-tree query stays linear (1.1→12.9 ms). This rules out “it was only fast at 10k because it
queried a tiny range”: the writer's own sweep measures the **same 40-line window**
(`Range{lineStart(text,20), lineStart(text,60)}`, `overlay_selftest.cpp` checkPerformanceSweep) at
every size, and my independent fixed-window numbers confirm the claim.

## 5. Observability and budgets unchanged (PASS)

On the same clean tree: lane build `BUILD-EXIT=0`; `vedit-syntax-overlay-selftest --selfcheck` →
`SYN-OVERLAY-SELFTEST ok=1 checks=40 failures=0`, with A1g `single owner across 494 spans` /
`overlay spans=247`, A1h `claims-released before=181 after=0`, `incremental=812 full=812`, and the
T17-identical budget checks: 1,051-line 50-rebuild median **24.607 ms** (≤150), injection p95
**0.153 ms** (≤2); 10,081-line sweep rebuild **31.016 ms**, p95 **0.154 ms** — all PASS.

## Regression (clean worktree, HEAD 4856960)

- `bash build/gates/typecheck.sh` → **exit 0** (`[qt6] PASS … warnings-as-errors`;
  `[typecheck] PASS (pinned image vedit-build:local)`).
- root `ctest --test-dir target/qt6-build` → **`100% tests passed, 0 tests failed out of 20`**,
  `CTEST_EXIT=0` (A1g/A1h/incremental/lane entry included).
- `cargo test --workspace` → **exit 0** (all test binaries pass, including `real_slang` 6/6 and
  `verible_ls` 8/8), after provisioning the repo-relative external artifacts the tests document:
  `build/slang/out/slang` and `.mpd/recon/verible/bin/` (copied from the built artifacts; `VEDIT_*`
  env unset). In a bare worktree without those artifacts the tests **fail loudly by design**
  (“slang artifact not found … run `bash build/slang/build.sh`”); that is an environment
  prerequisite produced by the repo's build scripts, not a T92 regression.
- No test/gate weakened: T92 commit `3b1bb1e` = 1,944 insertions, **0 deletions**; the overlay
  commit (`1977c50`) deletes no existing assertion; the new checks are additive.

## Findings / observations (non-blocking)

| id | severity | observation |
| --- | --- | --- |
| O1 | INFO | The default implementation preserves the provider's order but does not sort; the header comment says “ordered by start byte”. The tree-sitter implementations sort; a legacy provider that returns unsorted spans yields an unsorted range result — no behaviour change vs its whole-tree result. Suggest “preserves the provider's order” in the comment. |
| O2 | LOW | Inverted ranges (start>end): `ts_query_cursor_set_byte_range` returns `false` and the code ignores it, leaving the cursor unbounded → a whole-tree scan then the filter drops everything. Result is still correct; no caller passes inverted ranges (overlay guards `start<end`). A guard would avoid the wasted scan. |
| O3 | INFO | The “expand by lines” widening is conservative, not required by tree-sitter's documented intersection semantics; proven harmless (no misses, no duplicates). |
| O4 | INFO | T92's evidence numbers were measured on the intermediate overlay selftest state (`8cb45b65…`); the final state (`eeccaa26…`, HEAD 4856960) re-run (T98 and this review) shows the same shape and budgets, so no re-review is required for release. |
| O5 | INFO | `cargo test --workspace` needs the external toolchain artifacts; the clean worktree requires provisioning them (documented above). Not a code defect. |

## Not verified / bounds

- The same synthetic-document methodology as T17 (fixtures are constructed, not real customer
  projects; the long-line/comment/ifdef shapes target the risk).
- No GUI/visual claim.
- The folding lane files in the same T92 commit (`src/syntax/folding/**`) are outside this review's
  scope (their own review exists/landed separately).

## Release statement

**T92 can be released.** The performance improvement does not widen the semantic contract, and the
default/legacy path, multi-line coverage, window-union equality and the size-independence claim are
all independently reproduced on the frozen tree. No repair task is opened by this review.
