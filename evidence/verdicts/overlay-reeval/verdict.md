# Verdict — T98: re-review of T17 overlay evidence after T93 repair

## Binding declaration (the frozen tree this verdict is bound to)

- **HEAD**: `48569602d0c86fb402dc88155593d1dc06445c90` (snapshot taken by the Lead; the clean
  worktree checked out this exact commit). Evidence logs were collected at the earlier
  `31a7c2d…` (pre-snapshot) — declared below.
- **`src/syntax/overlay/overlay_selftest.cpp` sha256**:
  `eeccaa26368344072aca9216f78571479efd984eaaf6bef86dca64008ef8b777`
  (the final state named in the dispatch; the T93 evidence's `8cb45b65…` is the **intermediate**
  state, honestly disclosed by the writer — see F2/binding notes below).
- Other overlay sources at this binding (all clean at HEAD, hashes match the T93 evidence's
  `lane_sources` for the 6 unchanged files):
  CMakeLists `9594bd91…`, overlay.cpp `5d78c565…`, overlay.h `c01585d7…`,
  paint.cpp `f89da4da…`, paint.h `7f449eb1…`, README `e252aa29…`.
- Isolation: `git worktree add --detach target/t98-clean 4856960` gave a clean tree; the
  shared working tree's 33 in-flight entries (T94/T97 etc.) were **not** part of the runs.
  The worktree was removed after the runs.
- loop: no verification loop exists for T98 (hand-created task; none in
  `.mpd/verify/loops/`), so no `mpd_verify_record` was possible; this verdict is advisory.
- Work is read-only; the only write is this verdict file (write scope `evidence/verdicts/**`).

## Verdict: PASS

F1–F5 are all fixed and independently reproducible; the functional lane (T17 + T92
range-query version) is green on the frozen tree. No blocking findings.

## F1 — no future capture time (PASS)

- Independent scan (my own regex walk over every file in `evidence/slice2-overlay/` except the
  audit file itself, resolving `HH:MM(:SS)` against now): **`future_tokens = 0`**.
- README window (`14:08:16 – 14:09:38`) matches the logs: `01` header `14:08:16`;
  `04` full-ctest at `14:09:11` and lane ctest started `14:09:36`, `Passed 2.10 sec`
  → ≈`14:09:38`. `02`/`03` headers are `14:08:20` / `14:08:56`.
- File mtimes: `01` 14:08:20, `02` 14:08:23, `03`/`04` **14:12:28** — the later mtime of
  03/04 is the documented `## backfill:` binding-reconciliation edit, not a different
  collection time (see F2). `05-time-audit.txt` lists the same mtimes and the same zero-future
  conclusion; my scan reproduces it.
- The superseded 13:38 window is retained only as an explicit “invalid” record in
  `05-time-audit.txt` §6, with no future value repeated as if it were a capture time.

## F2 — C9 headers in 01/02/03/04 + honest backfill (PASS)

My own grep per file: `01`, `02`, `03`, `04` each contain `collected_at` (local + UTC),
`git_head`, `git_status_porcelain` (collection-time snapshot) and `lane_sources` (7 sha256s);
`03`/`04` additionally carry `## backfill: 2026-11-01T14:12:28+08:00 — binding reconciliation
(post-hoc note, NOT the original collection time)`. That note states exactly:
- the executed binary was built in the `01` step (14:08:16–14:08:20) from
  `overlay_selftest.cpp = 8cb45b65…` (01/02 headers record that hash);
- the `lane_sources` block in 03/04 was a later snapshot that had already drifted
  (the lane owner edited the untracked file during T93; `07-binding.txt` logs the edit
  sequence 14:04:36 / 14:09:21 / 14:10:04 / 14:11:15 and the binary rebuild at 14:12:49);
- the binary + 01/02 hash are authoritative for the numbers.

No original time is fabricated: the runs inside the logs (`run 1 at 14:08:56`, `FULL root
ctest at 14:09:11`, lane ctest at 14:09:36) are the header times; the later file mtime is
explained by the post-hoc note itself. This is an honest disclosure, not a masked mismatch.

**Binding note for the release decision**: the evidence numbers describe the intermediate
`8cb45b65…` selftest; the final committed state is `eeccaa26…`. My re-run on the final state is
recorded in this verdict (below) and passes with the same shape, so the evidence remains valid
for release; optionally the T93 evidence directory could append this final-state re-run.

## F3 — machine info present (PASS)

`03`/`04` headers (and `01`/`02`) record:
`AMD Ryzen 7 7840H w/ Radeon 780M Graphics | nproc=16 | kernel=7.0.0-38-generic |
mem=30496356 kB` plus `loadavg_at_collection: 5.27 5.44 4.77 …` and the pinned image id.
I verified the machine facts on the host: nproc=16, same CPU model, kernel 7.0.0-38-generic,
MemTotal 30496356 kB. The loadavg is a collection-time value and is correctly labelled as such.

## F4 — synthetic base declared, wording limited (PASS)

README §6 item 6 now says explicitly: `overlay_selftest.cpp`'s `baseLineSpans()` (`:120–136`,
used at `:201`) is a **synthetic one-span-per-non-empty-line base**, **not** KSyntaxHighlighting
spans; therefore `raw-overlap=663` proves **the clipping path was exercised and the paint plan
agrees with the arbiter**, and it does **not** prove the overlap under a real KSH span or that
the editor canvas paints the plan. The conclusion table row for A1g carries the same limit
("base 是测试内的合成替身（§6.6），不是真 KSyntaxHighlighting span"). Wording no longer
claims the stand-in result proves the real base.

## F5 — full root ctest recorded and reproduced (PASS)

`04-ctest.log` records, in order: **full root ctest** (`ctest --test-dir target/qt6-build
--output-on-failure` → `100% tests passed, 0 tests failed out of 20`, `FULL_CTEST_EXIT=0`)
and the lane entry (`1/1` passed, `LANE_CTEST_EXIT=0`). On the clean worktree I reproduced
the same: **20/20 pass, `CTEST_EXIT=0`** (test #8 lane and #20 vcs included).

## Functional re-run on the frozen binding (PASS)

`cmake --build target/qt6-build --target vedit-syntax-overlay-selftest` → `BUILD_EXIT=0`
(binary built 14:31 local from the clean tree). Three independent runs
(`QT_QPA_PLATFORM=offscreen … --selfcheck`):

| run | result | A1g | A1h | incremental | 1,051-line rebuild median | injection p95 | 10,081-line rebuild median | 10k injection p95 | range-query median |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `checks=40 failures=0` | single owner 494 spans, overlay 247 | claims 181→0 | 812 == 812 | 24.045 ms | 0.151 ms | 31.168 ms | 0.157 ms | 0.066 ms |
| 2 | `checks=40 failures=0` | 494 / 247 | 181→0 | 812 == 812 | 25.115 ms | 0.147 ms | 30.926 ms | 0.153 ms | 0.066 ms |
| 3 | `checks=40 failures=0` | 494 / 247 | 181→0 | 812 == 812 | 24.029 ms | 0.148 ms | 30.771 ms | 0.218 ms | 0.066 ms |

Budget comparison (same method as T17: 50 rebuilds median of 3 batches ≤150 ms; one-window
injection p95 ≤2 ms): all three runs pass with large margin (rebuild ≈24–25 ms at 1k and
≈31 ms at 10k; injection ≈0.15–0.22 ms). The range-query median stays 0.066 ms across
1k→10k — consistent with “query cost tracks the window, not the document”.

T92's range-query version is the one under test: at this binding `overlay.cpp` caches the
whole document only in `open()`/re-enable (`refreshSpans(0,size)`), `applyEdit()` refreshes
`window_start..window_end`, and `injectWindow()` re-queries when the window is outside the
cache. A1g 494/247 and incremental 812==812 hold unchanged under that version.

## Not verified / bounds

- **GUI visuals** (open/close overlay screenshots, real-window colour change, scroll feel):
  user-only with a display server; the lane declares them undone (§5) — unchanged bound.
- **Real KSyntaxHighlighting spans / editor canvas consumption**: outside this lane's write
  scope; the paint-plan evidence is the synthetic-base exercise declared in F4.
- The evidence directory's own numbers bind the intermediate `8cb45b65…` selftest; this verdict
  supplies the final-state (`eeccaa26…`, HEAD 4856960) re-run but does not rewrite T93's logs.
- Not run in this task: `typecheck` / `cargo test --workspace` — not part of T98's acceptance
  list (the task asks for lane build/selftest/perf/full ctest); the full ctest was run.

## Observations (non-blocking)

- The README phrase “`04-ctest.log` 落盘 14:09:38” is the original write time; the file mtime is
  14:12:28 because of the documented backfill note. The note explains it; a future edit could
  say “原始写入 14:09:38，backfill 于 14:12:28” to remove the apparent gap.
- The 03/04 `lane_sources` blocks are drifted snapshots by the writer's own description; the
  authoritative binding is the 01/02 hash + the binary, as stated. This is disclosed, not hidden.
