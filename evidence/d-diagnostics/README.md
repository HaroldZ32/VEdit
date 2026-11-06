# diagnostics-presentation — evidence (cell `cell-20261102T050548232Z`, task `T9`)

Frozen contract: `.mpd/cellwork/cell-20261102T050548232Z/contracts/diagnostics-presentation.md`.
Deliverable: the diagnostics panel's error/warning split with counts + colors + sorting +
empty state, double-click jumps through the one jump registry, and the editor's inline
wavy underline (MovingRange + Attribute).

## What changed (all inside the frozen write scopes)

| path | change |
|---|---|
| `src/workspace/diagnostics/diagnostics_panel.h/.cpp` | diagnostics section rebuilt as **severity groups** (Errors / Warnings / Information) with per-group counts, the editor's severity palette, `file:line` sorting, explicit empty state, and double-click handlers that call `vedit::workspace::jump::jumpTo` (outline row: file:line:column; diagnostics row: file:line) |
| `src/workspace/diagnostics/diagnostics_outline_model.h/.cpp` | `symbolAt(row)` for the outline double-click; tooltip now carries line:column |
| `src/editor/kernel/diagnostics_annotations.h/.cpp` | inline squiggle: one `KTextEditor::MovingRange` per diagnostic span with a `SpellCheckUnderline` `Attribute`, error red `#c62828` / warning orange `#b26a00`, z-depth `-90000`; gutter glyph model kept; ranges follow edits, are deleted on replace / document moving-interface invalidation / view destruction; per-line cap 1024 with merge; observable accessors for the evidence |

Nothing outside those paths was modified by T9.

## How the evidence was produced

The probes compile the **same product sources** the lane builds (panel + outline model +
controller + toolchain + shared jump registry; annotations + the real KTextEditor) and
drive them headless under the pinned Qt6 image:

```
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local bash -lc '
  cmake -S evidence/d-diagnostics/harness -B target/d-harness -G Ninja
  cmake --build target/d-harness
  QT_QPA_PLATFORM=offscreen target/d-harness/panel-probe evidence/d-diagnostics
  QT_QPA_PLATFORM=offscreen target/d-harness/annotations-probe evidence/d-diagnostics
  QT_QPA_PLATFORM=offscreen ctest --test-dir target/qt6-build -R "(diagnostics|toolchain-ux|edit-core|native-fold)"'
```

The product seam test additionally runs the real `verible-verilog-ls` through the C ABI
with `VEDIT_SEAM_SCREENSHOT` set, giving a full application screenshot.

## Evidence

| file | observed result |
|---|---|
| `panel-probe.log` | `SUMMARY checks=37 failures=0`, exit 0. Counts `Errors: 2 · Warnings: 4 · Information: 2`, group titles `Errors (2)` / `Warnings (4)` / `Information (2)`, group colors `#c62828` / `#b26a00` equal to the editor palette, every row's severity never crosses its group, rows sorted `first.sv:11…` then `second.sv:11…` and warnings in ascending line order. Outline double click → `OUTLINE-DOUBLE-CLICK file='/tmp/vedit-fixture/rtl/top.sv' line=5 column=8`; diagnostics double click → `DIAGNOSTIC-DOUBLE-CLICK … line=6 column=4`; with no target registered the double clicks are a no-op (no crash). Empty state: `No diagnostics — open a file to start the analysis, or open a project folder and run Lint project.` |
| `annotations-probe.log` | `SUMMARY checks=38 failures=0`, exit 0. `SpellCheckUnderline` on error and warning ranges, colors `#c62828` / `#b26a00`, `z=-90000`, exact span text `b;`, gutter `annotated-lines=3` and tooltip preserved. After inserting a line: `start(3,13)=1 text='b;'` (MovingRange followed the edit). After clearing entries: 0 ranges. Per-line cap: `cap ranges=1024 per-line=1024 merged=476`. Large file: 10k lines / 500 spans, scrolled to line 9000, still 500 ranges. After the view is DESTROYED: `after-close live-ranges=0` (checked after teardown on purpose). |
| `underline-error-warning.png`, `underline-zoom.png` | the rendered KTextEditor view with red error / orange warning squiggles visible under the spans (zoom is a 3× crop). |
| `underline-large-file.png` | the 10k-line document after scrolling; ranges intact. |
| `panel-error-warning-groups.png` | the panel: counts line with two colored groups, group headers, sorted rows. |
| `panel-empty-state.png` | the empty state with the next step. |
| `product-seam-inline-diagnostics.png` | the FULL product window: editor with the red inline squiggle + gutter icon and the diagnostics dock showing `Errors: 1 Warnings: 0 Information: 0` / `Errors (1)` in red. |
| `product-seam.log` | real-backend seam: `seam.annotation_lines=1 tooltip='syntax error at "b" [verible]'`, `seam.screenshot-saved`, exit 0. |
| `ctest-diagnostics.log` | `vedit-workspace-diagnostics-selftest`, `vedit-diagnostics-ffi-selftest`, `vedit-diagnostics-product-seam`, `vedit-toolchain-ux-selftest`, `vedit-edit-core-selftest`, `vedit-native-fold-tests` — 6/6 passed, exit 0. |
| `product-selfcheck.log` | `vedit-app --selfcheck`: `APP-SHELL-SELFTEST ok=1 checks=245 failures=0`, `EDIT-CORE-SELFTEST ok=1 checks=67 failures=0`, exit 0. |

Pixel proof of the squiggle (`annotations-probe.log`): the view is grabbed twice with the
same annotation border and the pixel diff is restricted to the text area —
`PIXELS text-area changed=168 bbox=(62,27)-(169,69) red-ish=48 orange-ish=2 bands(error=15
warning=80 control=0)`: the error line and the warning line are repainted, a control line
without a diagnostic is untouched (0 pixels), so the wavy underline really is painted and
not merely attached as an object.

## AC mapping

- **AC-5.1 / 5.2** — `SpellCheckUnderline` ranges, red for errors and orange for warnings
  (the same values as the gutter glyph palette); the gutter annotation model is kept.
  Screenshots + pixel proof.
  **Scope of that claim, precisely:** the *exact-span* squiggle is proven at the
  annotation layer (the probe drives the real editor). The product path still feeds the
  annotation with `line` only until T11 wires `column`/`endLine`/`endColumn` in
  `diagnostics_integration.cpp`, so the **shipped product today draws a whole-line
  underline**. Item 5 ("下划线画在代码下方") is therefore NOT fully delivered in the
  visible product yet; the remaining piece is handoff 1 below (T11's acceptance).
- **AC-5.3** — replacing the entries removes the ranges; a `MovingRange` follows inserted
  text; destroying the view leaves `activeRangeCount()==0`.
- **AC-5.4** — a line is capped at 1024 ranges, the overflow is merged (476 of 1500), the
  line keeps its underline, and a 10k-line document scrolls without a crash.
- **AC-6.1 / 6.2** — two (plus information) groups with their own counts and colors from
  one shared palette; a severity never lands in the wrong group.
- **AC-6.3** — rows sorted by file then line then column; double click jumps.
- **AC-6.4 / AC-10.3.3** — an explicit empty state naming the next step.
- **AC-7.1 / 7.2** — outline and diagnostics double clicks go through
  `vedit::workspace::jump::jumpTo` (the process-wide registry), with no panel-local file
  opening; a missing target is a no-op.

## Gaps / handoffs (reported to the captain, not silently absorbed)

1. **Exact underline spans in the PRODUCT need one small plumbing change outside T9's
   write scope — it is T11's (backend-switch) work, and it is the acceptance for "item 5
   visible in product".** `DiagnosticAnnotation` now carries
   `column`/`endLine`/`endColumn`, but the product's bridge
   (`src/workspace/diagnostics/diagnostics_integration.cpp`, T11 write scope) still fills
   only `line`, so the product screenshot shows a WHOLE-LINE squiggle. The editor half
   already renders exact spans (probe). Fix is three assignments after
   `annotation.line = entry.line;`:
   `annotation.column = entry.column; annotation.endLine = entry.endLine;
   annotation.endColumn = entry.endColumn;`
   T11 must then prove it end-to-end on the PRODUCT path (a diagnostic on a
   column-selected token underlines only that span, not the whole line), reusing the
   pixel-diff technique above. Until then AC-5.1/5.2 hold at line granularity in the
   shipped product — materially different from what the user asked for, and not to be
   reported as fully delivered.
2. **The panel's diagnostics section and the outline share the dock.** The counts/groups
   are a new section below the outline; no layout/behavior regression in the existing
   tests (`toolchain_ux_selftest` still sees the same single-line status and outline empty
   state).
3. **Teardown-only signal checked explicitly**: the annotations probe asserts range
   cleanup AFTER destroying the view (not only while alive), and the product `--selfcheck`
   (245 checks) runs the full shell including the new range cleanup — both green.

## Verified vs uncertain

- Verified: the six ctest entries above, both probes, the real-backend product seam, the
  product `--selfcheck`, the screenshots listed.
- Uncertain: nothing in this task's scopes; the only open item is the exact-span plumbing
  in handoff 1 (T11's write scope) — the shipped product underlines the whole line, so
  item 5 is not fully delivered until T11 lands.
