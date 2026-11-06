# RM-7 final independent review — every disabled control must LOOK disabled

- Task: T16 (independent verification of T15 / RM-7), read-only.
- Reviewer: `reviewer-3` (Reviewer).
- Date: 2026-11-06 (Asia/Shanghai).
- **Verdict: PASS. Risk: MEDIUM.**
- Reviewed tree: `/home/haroldzhao/MyProj/DshProj/VEdit` working tree.
  `src/app/theme_manager.cpp` sha256 `3aad0537b7cfe98c891640408e0eba9dfb5339dc367935407dfaba801fea2670`;
  `src/app/disabled_look_selftest.cpp` sha256 `1309c7c269203c74002a8cb341df86041c514b04a624de3de6affdd378666377`;
  the writer's pre-fix copy sha256 `ec8b69a5e8ac407a3dbfc7b4a513fd29a553bfc33b6eaa66c3ae292efef69865`,
  byte-identical to `git show HEAD:src/app/theme_manager.cpp`, with **0** `Disabled` assignments.
  Provenance: `05-provenance.txt`.
- **One provenance drift**: `src/app/CMakeLists.txt` has changed after the writer's evidence snapshot
  (another lane added the RM-5b shortcut-test block); current sha256 `cbcd2764…` vs the writer's
  `1315a94e…`. The RM-7 ctest block is still present and the reviewer's rerun on the current tree
  passes; the `theme_manager.cpp` and selftest hashes still match. Re-derive this hash at integration.

Basis: the RM-7 card in `evidence/ux-batch-2/REAL-MACHINE-FINDINGS.md` §RM-7, the writer's evidence
`evidence/ux-batch-2/rm7-disabled-look/` (README sha256 `3e32d4da…`), and the source. The reviewer
re-ran the writer's checks from byte-identical sources and wrote the raw logs in this directory.

---

## 1. What I re-derived (independent of the writer's narrative)

### 1.1 Green path — ctest in the pinned image

`docker run ... vedit-qt6-build:local ctest -R vedit-app-disabled-look-selftest -V`
(`01-ctest-rerun.log`):

```
CHECK disabled.palette.catalogue PASS 30 theme(s)
CHECK disabled.palette.themes-tested PASS 30/30
CHECK disabled.palette.active-intact PASS
CHECK disabled.palette.min-channel-delta PASS min=72   (Atom One Dark, floor 40)
CHECK disabled.palette.min-luminance-delta PASS min=0.183 (Solarized Dark, floor 0.100)
CHECK disabled.palette.min-contrast-drop PASS min=2.13 (Solarized Light, floor 1.40)
CHECK disabled.apply-theme PASS Breeze Light
CHECK disabled.pixels.styles PASS 2 style(s): Windows, Fusion
... PIXELS rows: 5 widget classes × 30 themes × 2 styles ...
DISABLED-LOOK-SELFTEST failures=0
1/1 Test #16: vedit-app-disabled-look-selftest ... Passed 0.27 sec
```

The 30 `PALETTE-THEME` rows and all 9 `PALETTE-ROLE-MIN` rows are **line-for-line identical** to the
writer's green log (`04-number-crosscheck.txt`). Re-derived per-role minima:
window-text 72 / 0.191 / 2.25; text 72 / 0.191 / 2.35; button-text 72 / 0.183 / 2.13;
tooltip-text 72 / 0.185 / 2.26; placeholder-text 50 / 0.202 / 1.35; link 64 / 0.077 / 1.33;
link-visited 64 / 0.158 / 1.55; highlight 9 / 0.007 / 1.09; highlighted-text 60 / 0.071 / 0.49
(the last two are "must differ" secondary roles).

Representative pixel rows (Fusion, the app style; `exact_colour=1`):
toolbar-action 72 / 0.294, differing 2333, enabled hits 162 / disabled hits 190;
push-button 72 / 0.294, differing 1047; combo-box 72 / 0.294, differing 1191;
line-edit 72 / 0.191, differing 2022; plain-text-edit 72 / 0.191, differing 850.
Windows style is held only to "the renders differ" (`exact_colour=0`).

### 1.2 Red path — the same test against the pre-fix palette

`bash .../probe/run-selftest.sh /vedit-build/.../prefix/theme_manager_prefix.cpp`
(`02-red-rerun.log`): **584 FAILs, `DISABLED-LOOK-SELFTEST failures=584`, exit 1**. The 638
CHECK/PALETTE/PIXELS lines are identical to the writer's red log, including the measured
`enabled=#abb2bf disabled=#abb2bf delta=0` pairs. The pre-fix copy is HEAD, 0 `Disabled`
assignments, so the red run really is the old palette (not a modified worktree).

### 1.3 Screenshots

- Pair comparison (`06-screenshot-pairs.log`): **12/12 enabled pairs byte-identical** before vs
  after; **12/12 disabled pairs differ**. That independently confirms the "enabled group untouched"
  claim for every rendered sample/theme pair.
- I looked at the images. In `screens/` (after), the disabled toolbar "Undo" is grey against the
  lighter/ darker surface while the enabled one is the theme's full text colour; `screens-prefix/`
  (before) shows the disabled "Undo", combo and line-edit in exactly the enabled ink colour.
  Concretely: Breeze Light toolbar enabled `#271c1b` vs after-disabled `#948e8e` (Δ115), before-disabled
  `#271c1b` (Δ0); Breeze Dark combo `#cfcfc2` vs `#737571` (Δ92) vs `#cfcfc2`; Nord line-edit
  `#d8dee9` vs `#7a818c` (Δ94) vs `#d8dee9`. The images are **offscreen sample widgets**, not the
  real application; see the residual risk.

### 1.4 Host-style observation (new, not in the writer's evidence)

On the review host, which exposes **4** styles (`Breeze, Oxygen, Windows, Fusion`), the same
selftest binary prints the palette block and `CHECK disabled.pixels.styles PASS 4 style(s)`, then
makes no further progress: 60 s and 240 s runs were killed with **exit 124**, with **0** `PIXELS`
lines (`03-host-style-timeout.log`). In the pinned image (2 styles) it passes in 0.27 s. So the pixel
half is not portable to a host that offers Breeze/Oxygen, and Breeze was **not** measured.

---

## 2. The seven checks

| # | Check | Result |
|---|---|---|
| 1 | Mechanism named with file:line; cause not one symptom | **PASS** — README §1 names `src/app/theme_manager.cpp` (`ThemeManager::paletteForTheme` / `applyTheme`), the `QPalette::setColor` all-groups behaviour and the 0-`Disabled` pre-fix tree. The diff moves the same values into locals and adds the `QPalette::Disabled` group; the enabled roles are byte-identical (12/12 screenshot pairs). This is the cause, not a message change. |
| 2 | Both user-named cases visibly different when disabled | **PASS** — Lint page: `SettingsPanel::refreshLintKnobStates` disables exactly the `BackendUnsupported` knobs (`LintKnobState::enabled()`), which are real `QComboBox`/`QLineEdit`/`QPlainTextEdit` widgets in `lintKnobWidgets_`; the palette's Disabled group now dims them. Undo/Redo: `ApplicationShell::buildToolBar` uses `Qt::ToolButtonTextOnly` shared command `QAction`s; `ApplicationShell::syncEditorCommandStates` follows `EditorArea::activeEditorActionEnabled` → `EditorPane::editorActionEnabled` → `findEditorAction` (KTE's standard actions live on the internal child widget; `editor_area.cpp` documents the walk), and the action starts disabled with no pane. The action is therefore genuinely reachable in the disabled state, and the style paints a disabled `QToolButton` from the Disabled group. I did not drive the real product toolbar; the selftest's `SampleKind::ToolBarAction` is a real `QToolButton` fed by a `QAction`, i.e. the same wiring shape. |
| 3 | Every theme the manager can produce, per-theme numbers | **PASS** — `ThemeManager::availableThemes()` returns the repository list (30), the test resolves each name and requires `tested == names.size()`; 30/30 pass; all 30 per-theme rows re-derived and identical to the writer's table. |
| 4 | Numeric assertion, fails pre-fix, no hard-coded hex | **PASS** — floors 40/255, 0.10 luminance and 1.40× contrast drop for the four primary text roles; 40/0.10/1.25 for placeholder; 40/0.05/1.25 for links; colours come from the sample's effective theme palette. The same committed test source against the HEAD palette produces 584 FAILs (reproduced), and against the fix 0. |
| 5 | Lint-page `BackendUnsupported` vs `TransportPending` distinguishable | **PASS** — `LintKnobState::enabled()` is false only for `BackendUnsupported` (dimmed, reason in tooltip and the visible `lintVeribleStatus_`/`lintSlangStatus_` label); `TransportPending` (verible `Autofix`) stays **enabled** and fully saturated, with its own reason. The two states are now visually distinct. The inactive-engine disabled knobs and the active-engine backend-unsupported knobs share the same grey, but the reason text differentiates them; the card's explicit pair is satisfied. |
| 6 | Custom-painted widgets honour `isEnabled()` | **PARTIAL — latent Medium M2.** The toolbar action is style-painted (palette-driven, honours). The file-status dot delegate does **not** check `State_Enabled` when it paints the dot, but the disabled row state is unreachable today (verified). No user-visible defect now; see M2 and the declared bound in §4. |
| 7 | Screenshots viewed; residual risk ranked | **PASS with Medium risk M1** — 12 after/12 before PNG pairs exist; I viewed representative pairs and confirmed the visible difference; enabled renders are byte-identical. They are offscreen sample renders, not the real app/desktop; Breeze style is unmeasured and the real product's Xvfb+xcb harness was not driven. |

---

## 3. Findings (ranked)

### M1 — Medium — the style on the user's real desktop (Breeze) is unmeasured; the pixel half of the test stalls on a host that offers it

**What.** The palette proof is style-independent and covers 30/30 themes, but the pixel proof covers
only the two styles present in the pinned image (Windows, Fusion). The user's KDE desktop may run
**Breeze**; the writer's bound 1 acknowledges this is reasoning, not measurement. On a host with
Breeze/Oxygen the same test binary prints the palette block and the style catalogue, then hangs
before the first `PIXELS` line (60 s / 240 s, exit 124; `03-host-style-timeout.log`).
**Where.** `src/app/disabled_look_selftest.cpp :: testPixels` (the `QStyleFactory::keys()` loop);
`evidence/ux-batch-2/rm7-disabled-look/README.md` §6.1.
**Why it matters.** The whole card is about what the user sees on the real machine. A style that
overrides a disabled colour, or applies a different disabled effect, would not be caught; the test
also turns a style the gate image does not ship into a hang instead of a clean skip. This is the
largest residual risk in an otherwise sound fix.
**Direction (described, not applied).** Render the pixel samples with the app's style (Fusion) as the
exact-colour proof, and add a bounded Breeze sample in an image that has Breeze (or a real
Xvfb+xcb screenshot of the Lint page + toolbar) so the user's style is measured; make the style loop
skip an unrenderable style with an explicit `SKIP`/reason rather than hanging.

### M2 — Medium (latent) — the hand-painted file-status dot ignores `State_Enabled`

**What.** `FileStatusDotDelegate::paint` passes `State_Enabled` to `drawItemText` for the row text,
but paints the dot with `dotColor(status)` at full colour, with no `State_Enabled` check. The writer's
claim that the disabled state is unreachable is **verified today**: `FileTreeModel::flags` always
returns `Qt::ItemIsEnabled | Qt::ItemIsSelectable`, and the filetree/project lanes never call
`setEnabled` on the tree. So no user can currently see a bright dot on a disabled row.
**Where.** `src/workspace/filetree/file_status_delegate.cpp :: FileStatusDotDelegate::paint`
(dot brush); `src/workspace/filetree/file_tree_model.cpp :: FileTreeModel::flags`;
`src/workspace/filetree/file_tree_widget.cpp :: FileTreeWidget` (no disable path).
**Why it matters.** The card's "self-painted controls" note and the review's check 6 name exactly this
delegate. If a disable path is ever added (e.g. disabling the tree during a project pass), the dot
would silently look enabled on a disabled row — the same class of defect the user reported, on a
hand-painted row that no palette fix can reach. Criterion 6 is met **in effect** (no reachable
disabled row) but not at the code level.
**Direction (described, not applied).** Gate the dot brush on `(opt.state & QStyle::State_Enabled)`
(dim it toward the row background, or use the disabled text colour), or record and test a hard
invariant that the tree is never disabled. One line, outside `theme_manager`; the Lead should decide
whether to schedule it now or carry it explicitly.

### N1 — Nit — the pixel-check wording proves less than it claims

**What.** The selftest header says the pixel half proves "the disabled ink is measurably closer to the
background than the enabled ink". `measurePixels` computes `channelDelta`/`luminanceDelta` from the
**palette roles**, and uses the rendered images only for the "differing" count and exact-colour
glyph hits; the direction "closer to the background" is not measured from pixel ink. The separate
`render_pixels` probe does compute `enabled_ink`/`disabled_ink`, but the committed selftest does not.
**Where.** `src/app/disabled_look_selftest.cpp :: measurePixels` and its header comment.
**Why it matters.** An evidence claim slightly stronger than the assertion; a future reader could
trust a guarantee the regression check does not make.
**Direction.** Measure ink-vs-surface from the render, or reword the header to "the disabled render
contains the palette's Disabled colour and the enabled/disabled renders differ".

### N2 — Nit — the shipping `applyTheme` path is only checked for one theme

**What.** `testPaletteGroups` checks `paletteForTheme` for all 30 themes, but the
`applyTheme` → `QApplication::setPalette` path once with `defaultThemeName()` (Breeze Light).
**Where.** `src/app/disabled_look_selftest.cpp :: testPaletteGroups` (the `disabled.apply-theme`
check).
**Why it matters.** Small gap: the claim "every theme the manager can produce" is proven through the
pure function; the shipping path is trivial but only 1/30.
**Direction.** Loop the 30 themes through `applyTheme` and assert the app palette's Disabled `Text`
each time (cheap), or state the bound.

### N3 — Nit (provenance/process) — the writer's source-hash list is stale for `CMakeLists.txt`

**What.** `src/app/CMakeLists.txt` is now `cbcd2764…`, while the writer's
`logs/00-source-sha256.txt` records `1315a94e…`; another lane added the RM-5b block after the RM-7
evidence. The RM-7 test block is intact, the `theme_manager.cpp` and selftest hashes still match, and
the reviewer's ctest rerun on the current tree passes.
**Where.** `evidence/ux-batch-2/rm7-disabled-look/logs/00-source-sha256.txt`;
`src/app/CMakeLists.txt`.
**Why it matters.** It is exactly the "which review covers the final tree" trap: the evidence snapshot
and the integration tree diverged after the review. Not an RM-7 code defect.
**Direction.** Re-derive the hash table at integration (or scope it to the RM-7 block) so the final
tree is what the evidence names.

---

## 4. Declined to judge (every item I weighed and set aside, with the reason on the same line)

- **Stale toolbar undo/redo enabled-state sync** — `EditCoreProvider::setModifiedListener` connects only `KTextEditor::Document::modifiedChanged`, so `ApplicationShell::syncEditorCommandStates` runs on modified flips, not every undo/redo; an undo within modified content can make the KTE redo available while the toolbar redo stays disabled (a working action that looks disabled). That is the U5/undo functional lane, not the RM-7 direction (disabled-looks-enabled); flagged for the Lead, not counted against RM-7.
- **Tab-bar `QToolButton` stylesheet without `:disabled`** — `EditorPane::applyTabStyle` colours the tab close button; no `setTabEnabled`/`setEnabled` path disables it, so the missing `:disabled` rule is unreachable; declined.
- **Command palette hard-coded dark style and `PaletteItemDelegate`** — it lists files, never disabled items; the delegate's lack of `State_Enabled` cannot manifest in this palette; declined as outside RM-7's reachable controls.
- **`Highlight` / `HighlightedText` disabled values** — only "must differ" (min Δ 9 / 60); on some dark themes the disabled value is lighter than the enabled black/white (writer's bound 2); no disabled control in this product paints a selection; declined.
- **`Light`/`Midlight`/`Mid`/`Dark`/`Shadow` frame roles** — still Qt defaults rather than theme-derived (pre-existing; affects enabled controls too); the task fixes text signalling; declined (writer's bound 3).
- **Tooltip on a disabled knob** — I did not empirically prove Qt shows a tooltip on a disabled widget; the visible `lintVeribleStatus_`/`lintSlangStatus_` labels carry a reason anyway; declined to judge the tooltip mechanism.
- **Theme catalogue in the packaged AppImage vs the build image** — the test enumerates the repository in the test image; I did not extract the AppImage's KF6 syntax data to prove the same 30 names; bound.
- **Real product Xvfb+xcb screenshots** — not driven; the writer's PNGs are offscreen sample widgets, so the actual Lint page and toolbar in the app were not photographed; bound (M1).
- **Language/theme change while a Preferences dialog is open** — not exercised (writer's bound 5); bound.
- **`QMenu` / `QToolTip` disabled rendering not in the pixel matrix** — the palette roles are set and the mechanism is the same group, but these surfaces were not rendered; bound.
- **Frame/bevel and selection-role guarantees** — not asserted beyond "must differ"; declined as above.

---

## 5. Bounds of this review (what I did not read or run)

- I did **not** run the product itself: no Xvfb+xcb full-app session, no live Lint page or toolbar
  screenshot; the product's own `vedit-app-ux-screenshots` harness was not driven.
- I did **not** measure the Breeze style: the pinned image has only Windows/Fusion, and the host run
  with Breeze/Oxygen stalls (exit 124, no `PIXELS`); the palette proof is style-independent in
  theory, the Breeze pixel proof is missing.
- I did **not** run the whole ctest suite or the u8 gate; only `vedit-app-disabled-look-selftest` in
  `vedit-qt6-build:local` (twice: green and red).
- I did **not** extract the packaged AppImage's theme/Style data to compare its theme catalogue with
  the build image's 30.
- I did **not** empirically test tooltip delivery to a disabled widget.
- I did **not** inspect the writer's 24 PNGs individually; I compared all 24 by sha256 and viewed
  5 representative images (Breeze Light toolbar, Breeze Dark combo, Nord line-edit; enabled,
  after-disabled and before-disabled where applicable).
- I did **not** edit any product file. The only writes are this report and the review logs in
  `evidence/ux-batch-2/review-rm7-final/`: `01-ctest-rerun.log`, `02-red-rerun.log`,
  `03-host-style-timeout.log`, `04-number-crosscheck.txt`, `05-provenance.txt`,
  `06-screenshot-pairs.log`.

---

## 6. Verdict

**PASS — risk MEDIUM.**

The root cause is correctly identified and fixed at the right seam: `QPalette::setColor` was filling
the Disabled group with the Active colours, and `ThemeManager::paletteForTheme` now builds a
theme-derived Disabled group while leaving the enabled group untouched. The reviewer independently
reproduced the numbers (30/30 themes, minima 72 / 0.183 / 2.13 over the primary text roles), the red
run (584 failures against the HEAD palette), the green ctest (0 failures, 1/1, 0.27 s), and the
screenshot facts (12/12 enabled pairs identical, 12/12 disabled pairs different). Both user-named
cases are reached: the Lint page's unsupported knobs are standard widgets fed by the new Disabled
group, and the toolbar Undo/Redo are text-only `QToolButton`s fed by shared `QAction`s that are
genuinely disabled when there is no pane or no editor undo step. The `BackendUnsupported` greyed state
and the editable `TransportPending` state remain distinguishable.

The Medium risk is the last mile the evidence cannot prove: the user's actual desktop style (Breeze)
was not rendered — and the selftest stalls when a host offers it — and the real app was not
screenshotted; and the hand-painted file-status dot does not honour `State_Enabled`, although that
state is unreachable today (verified) and is disclosed in the writer's bounds. Neither is a
user-visible defect in the current tree; both should be carried or closed explicitly (M1, M2) rather
than silently assumed.
