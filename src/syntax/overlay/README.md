# src/syn-overlay — tree-sitter colouring overlay (T18)

One job: paint tree-sitter highlight spans on top of the KSyntaxHighlighting
base highlighter **without ever colouring the same bytes twice**.

| Requirement | Where it lives |
|---|---|
| C16 / A1g — single owner per range, no double colouring | every claim goes through `OwnerArbiter` (`src/syntax/provider`); `paint.cpp` turns the arbiter state into the clipping plan the editor paints from (overlay wins, base spans are clipped, never painted underneath) |
| C15 / A1h — runtime off switch, fallback, no crash | `OverlayController::setEnabled(false)` releases every claim synchronously; a disabled `SyntaxService`, a null provider or a failed parse all degrade to "base highlighter owns everything" (`State::Disabled` / `State::NoProvider`) |
| C12 — incremental overlay | the overlay owns exactly the injected visible window (`injectWindow`); one edit (`applyEdit`) syncs the provider, refreshes the span cache and re-injects only that window — scrolling replaces the claim set instead of accumulating it |
| Open Q2 budget | measured by the lane validator on a 1,051-line / 25 KB synthetic SystemVerilog document: 50 rebuilds ≈ 103–106 ms (budget 150 ms), single injection p95 ≈ 0.07 ms (budget 2 ms) — raw output in `evidence/slice2-overlay/03-perf-runs.log` |

## Files

- `overlay.h/.cpp` — `OverlayController`: document lifecycle, runtime switch,
  incremental edit + window injection, arbiter queries, timing counters.
- `paint.h/.cpp` — `buildPaintPlan()`: given the base spans and the overlay
  spans for a range, produces the single-engine partition plus the
  `raw_overlap_bytes` diagnostic (bytes the two raw inputs disagreed on, i.e.
  proof that the clipping path was exercised).
- `overlay_selftest.cpp` — the lane validator (`ctest`: `vedit-syntax-overlay-selftest`).

## Build

The root build owns the lane (it is listed in `VEDIT_LANE_DIRECTORIES`); a
standalone configure of this directory is refused on purpose because the
provider target brings the tree-sitter version/package discovery with it:

```sh
cmake --build target/qt6-build --target vedit-syntax-overlay-selftest
ctest --test-dir target/qt6-build -R vedit-syntax-overlay-selftest
```
