# Fold regression after RM-5a (`openUrl` load path) — diagnosis and fix

- **Trigger**: the captain's first integration checkpoint on the merged tree —
  `ctest -R vedit-native-fold-tests` failed with
  `CHECK native.fold.native-fold-applied FAIL 0` (`NATIVE-FOLD-TESTS ok=0 checks=12 failures=1`).
  Everything else in the probe passed (provider, editor, warmup, no-auto-folds, 10k open in 66 ms,
  second open).
- **Owner**: RM-5a (the load path moved from `Document::setText()` to
  `KParts::ReadOnlyPart::openUrl()`).
- **Verdict**: **not a product regression.** The probe lost a side effect of `setText()`: it
  observed folding on an **unlaid-out (never shown) view**. A laid-out view — which is what every
  product pane is — folds an `openUrl`-loaded document normally. The load path keeps every guarantee
  (loaded, mode applied, `modified=0`, undo floor at the opened text). The probe now sets up the
  product's view state, and the fold guarantee is additionally asserted **together with** the undo
  floor in the RM-5a regression, so the two can never trade again.

## 1. Measurements (probe `probe/fold_after_openurl_probe.cpp`, log `probe/fold-probe.log`)

One binary, the same 10,497-line fixture, one variable at a time. `hidden` = real displayed span
minus visible span; `fold` is KDE's own command on `Range(0,0,39,0)`.

| case | load | view state | mode | modified | fold exec | hidden before → after | verdict |
|---|---|---|---|---|---|---|---|
| **A** | `openUrl` (current product) | created, not shown | SystemVerilog | 0 | ok | 0 → 0 | fold NOT observable |
| **C** | `setText` (the reverted pre-RM-5a behavior) | created, not shown | SystemVerilog | 0 | ok | 0 → **-39** | the old probe's passing case |
| **B** | `openUrl` + mode clear/re-set | not shown | SystemVerilog | 0 | ok | 0 → 0 | a mode transition does not help |
| **D** | mode applied **before** `openUrl` | not shown | SystemVerilog | 0 | ok | 0 → 0 | order of mode/open does not help |
| **E** | `openUrl` + switch to `C++` and back | not shown | SystemVerilog | 0 | ok | 0 → 0 | a real mode change does not help |
| **I** | `openUrl` + `resize(900,700)` | not shown | SystemVerilog | 0 | ok | 0 → 0 | a size alone does not help |
| **H** | `openUrl` | **shown 900×700** | SystemVerilog | 0 | ok | 0 → **+39**, `y(40)` 561→15 | **folds (product condition)** |
| **C2** | `setText` | shown 900×700 (scrolled to end) | SystemVerilog | 0 | ok | 0 → 0 (`visible` 10449→10410) | the fold applies but `hidden` is 0: the metric is viewport-dependent |
| **G** | `openUrl` then `setText(same text)` | not shown | SystemVerilog | 0 | ok | 0 → -39 | restores folds **but adds an undo step**, and `undo()` then leaves the document at **10,498 lines** (text corrupted) — **not a valid fix** |

Conclusions:
1. **The regression is an unlaid-out-view artifact.** `A` (unshown) reports no layout at all
   (`firstReal=lastReal=0`), the fold command runs (`exec_ok=1`) but cannot change a viewport that
   does not exist; `C` (unshown, `setText`) produced a **garbage** line span (`firstReal=10495,
   lastReal=10496`, then `firstReal=10495, lastReal=0`); the probe's old `hidden() != 0` passed on
   that garbage.
2. **The product is unaffected**: `H` (the product's condition — a shown, laid-out pane) folds the
   `openUrl`-loaded document and the cursor mapping moves (line 40: y 561 → 15).
3. **No product-side workaround exists that keeps the Blocker fixed**: the only operation that
   restores the unlaid-out fold state is a text set, which re-adds the undo step (`G`) and whose undo
   corrupts the buffer (`10497 → 10498` lines) — precisely the RM-5a Blocker.

## 2. Fix

`src/editor/kernel/native_fold_tests.cpp` (the probe that actually applies folds):
- lay the view out after creating it — `resize(900,700); show(); pump();` — exactly what a pane
  does; comment records why (measured above);
- scroll to the top (`setCursorPosition(0,0)`) before folding and assert `hidden > 0` instead of
  `hidden != 0` — the old check could pass on an unlaid-out garbage span, the new one cannot (a
  shown view at the top reports the number of folded lines: 39).

Result: `NATIVE-FOLD-TESTS ok=1 checks=12 failures=0`, `native.fold.native-fold-applied PASS
hidden=39 firstReal=0 lastReal=86 firstVisible=0 lastVisible=47` (`ctest-targeted.log`).

## 3. Cross-lane assertion (the gap that let this happen)

`src/editor/kernel/undo_view_lifetime_test.cpp` (the RM-5a regression) now asserts **on the same
loaded document**:
- `cross.fold.command` / `cross.fold.applied` — KDE's own fold command collapses a range on the
  pane's laid-out view;
- `cross.fold.not-an-edit` — the document text is still the opened text;
- `cross.fold.no-undo-step` — the shell's undo action is still DISABLED after loading and folding,
  i.e. the undo floor is at the opened text (RM-5a's guarantee).

`vedit-undo-view-lifetime-tests` → `SUMMARY checks=69 failures=0`; `vedit-native-fold-tests` →
`ok=1 checks=12 failures=0` (`ctest-targeted.log`).

## 4. Note for the board

This was caught by the **captain's integration checkpoint**, not by either lane's suite: the fold
probe (owned by the folding lane) was the only fold assertion, it never asserted the undo floor, and
the undo lane's regression never asserted folding. Both guarantees are now in one test, so a future
load-path change cannot silently break either.

## 5. Bounds

- The link between view layout and KTextEditor's fold tree is internal to KTextEditor; there is no
  public "build fold regions" API (checked: the `Document`/`View` public headers expose no fold
  query/recompute call), so a headless probe must reproduce the product's laid-out view.
- The probes run offscreen; the physical desktop is the user's check.
- Files changed by this follow-up: `src/editor/kernel/native_fold_tests.cpp` (probe state + stronger
  assertion), `src/editor/kernel/undo_view_lifetime_test.cpp` (cross-lane checks). No product source
  changed — the product load path is unchanged and keeps the undo floor.
