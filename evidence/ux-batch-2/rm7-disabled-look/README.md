# RM-7 — every functionally disabled control must LOOK disabled, in every theme

Real-machine report (user, 2026-11-06): **“所有该显示灰色不可点击的地方都没有变灰”** —
every control that is functionally disabled still looks enabled. Two examples the
user gave:

1. **Lint page with engine = slang** — the verible-only knobs cannot be clicked
   but look exactly like configurable controls;
2. **the toolbar Undo/Redo** probably too.

Shape of the defect: the *disabling* works (“实际上没法点”), only the
*appearance* is wrong — which is why every functional test passed.

**Status: FIXED.** `src/app/theme_manager.{h,cpp}` only; `src/app/app_shell.cpp`
was **not touched** (per the Lead’s concurrency note it stays with the other
three lanes). New regression check: `vedit-app-disabled-look-selftest`
(`src/app/disabled_look_selftest.cpp`, ctest entry registered in
`src/app/CMakeLists.txt`).

---

## 1. Why it happened (file:line, pre-fix)

The product has **no `.qss` resources**: the shell applies the selected
KSyntaxHighlighting theme as a `QPalette` (`theme_manager.cpp`,
`ThemeManager::paletteForTheme`, then `ThemeManager::applyTheme` →
`QApplication::setPalette`). The palette was built only with

```cpp
QPalette palette;
palette.setColor(QPalette::Window, window);
palette.setColor(QPalette::WindowText, foreground);
palette.setColor(QPalette::Text, foreground);
palette.setColor(QPalette::ButtonText, foreground);
… 15 roles …
```

`QPalette::setColor(ColorRole role, const QColor &color)` **sets the colour for
all three groups** (Active, Inactive **and Disabled**). The pre-fix file
therefore contained **no `QPalette::Disabled` assignment at all**
(`git show HEAD:src/app/theme_manager.cpp | grep -c Disabled` → `0`), so the
Disabled group was a byte-for-byte copy of Active.

Qt styles pick the group from the widget state (`QStyle::drawItemText` uses the
Disabled group when `State_Enabled` is clear; Fusion/Breeze/Windows style all do
this). With Disabled == Active, a disabled widget was painted with exactly the
enabled colour — the palette *contract* was broken, not the wiring.

Measured pre-fix (all 30 themes, `logs/03-red-prefix-selftest.log`):

```
CHECK disabled.palette.text FAIL theme=Atom One Dark enabled=#abb2bf disabled=#abb2bf delta=0 luminance=0.000 drop=1.00
CHECK disabled.palette.text FAIL theme=Breeze Light enabled=#1f1c1b disabled=#1f1c1b delta=0 luminance=0.000 drop=1.00
…
CHECK disabled.palette.min-channel-delta FAIL min=0 theme=Atom One Dark floor=40
CHECK disabled.palette.min-luminance-delta FAIL min=0.000 theme=Atom One Dark floor=0.100
CHECK disabled.palette.min-contrast-drop FAIL min=1.00 theme=Atom One Dark floor=1.40
```

The two user-named cases are both standard widgets fed by this palette, so the
single palette defect explains both:

* **Lint page knobs** — `settings_panel.cpp:2183` disables exactly the
  unsupported knob widgets (`widget->setEnabled(state.enabled())`, driven by
  `lint_options.h:89`). They are real `QComboBox` / `QLineEdit` /
  `QPlainTextEdit` widgets; the style painted their Disabled text with the
  Active colour.
* **Toolbar Undo/Redo** — text-only `QToolButton`s that share the command
  registry’s `QAction`s (`app_shell.cpp:848-869`). The action **is** reachable in
  the disabled state: it starts greyed out (`app_shell.cpp:182`), and
  `syncEditorCommandStates()` (`app_shell.cpp:880-889`) follows the active
  editor’s own action, which is disabled when there is no undo step
  (`editor_area.cpp:858-869`). So the user’s second example is the **same**
  palette defect, not a separate reachability bug.

The other stylesheets in the tree are scoped and carry no colour rule for a
disabled control (`grep -rn setStyleSheet src` → the dirty-status label, the
editor tab bar, the command palette, VCS/terminal banners); none of them is the
mechanism here.

---

## 2. The fix

`src/app/theme_manager.cpp`:

* `blendToward(color, surface, percent)` (line 40) moves a colour a fixed
  fraction toward the surface it is painted on;
* after the Active roles are set, the **Disabled group is built explicitly**
  (lines 142-163): `WindowText`, `Text`, `ButtonText`, `ToolTipText`,
  `PlaceholderText`, `HighlightedText`, `Highlight`, `Link`, `LinkVisited`,
  `BrightText` — each `blendToward(<its normal colour>, <its surface>, 55%)`.

Why this shape:

* **Theme-derived, not a hard-coded grey** — light and dark themes both dim
  correctly, and a future theme needs no new constant;
* **Active group is untouched** — verified (`disabled.palette.active-intact`:
  every theme’s Active Text/Base are still the theme’s own colours). This is what
  keeps the *editable* state (the Lint page’s `TransportPending` knob, which
  stays **enabled**) distinguishable from the *unsupported* state
  (`BackendUnsupported`, which is disabled and now visibly dimmed). No opacity /
  whole-widget effect is used, so an enabled-but-pending knob is still fully
  saturated, and the unsupported knob keeps its enabled tooltip + reason label;
* **only foreground roles are dimmed**; Window/Base/Button backgrounds are left
  alone — that is what Qt’s own styles do, and it keeps the enabled rendering
  byte-identical.

55% is measured, not guessed: it gives a minimum channel delta of 72/255, a
minimum relative-luminance delta of 0.183 and a minimum contrast-ratio drop of
2.13× over all 30 themes (next section).

---

## 3. Numbers (all 30 themes the manager can apply)

The regression check asserts, **per theme, per role**, that Disabled differs
from Active by at least: channel delta 40/255, WCAG relative-luminance delta
0.10, enabled/disabled contrast-ratio drop 1.40× for the four primary text roles;
40 / 0.10 / 1.25 for `PlaceholderText`; 40 / 0.05 / 1.25 for `Link` /
`LinkVisited` (these are already de-emphasised by the theme itself).
Selection roles (`Highlight`, `HighlightedText`) only have to *differ* (floor
5/255) — see §6.

Worst role per theme after the fix (`logs/06-green-ctest-verbose.log`); **before
the fix every row is delta 0 / luminance 0.000 / drop 1.00**:

| theme | worst primary role | channel Δ | luminance Δ | contrast drop |
|---|---|---|---|---|
| Atom One Dark | button-text | 72 | 0.294 | 2.48 |
| Atom One Light | button-text | 96 | 0.276 | 3.98 |
| Breeze Dark | button-text | 92 | 0.442 | 2.96 |
| Breeze Light | button-text | 115 | 0.260 | 5.20 |
| Catppuccin Frappé | button-text | 91 | 0.423 | 2.60 |
| Catppuccin Latte | button-text | 80 | 0.264 | 3.00 |
| Catppuccin Macchiato | button-text | 98 | 0.467 | 2.94 |
| Catppuccin Mocha | button-text | 105 | 0.501 | 3.23 |
| Dracula | button-text | 111 | 0.680 | 3.23 |
| Falcon | button-text | 100 | 0.494 | 2.83 |
| GitHub Dark | button-text | 101 | 0.559 | 3.11 |
| GitHub Light | button-text | 110 | 0.274 | 4.82 |
| Homunculus | button-text | 100 | 0.256 | 4.22 |
| Monokai | button-text | 112 | 0.688 | 3.31 |
| Nord | button-text | 90 | 0.494 | 2.75 |
| Oblivion | button-text | 98 | 0.489 | 3.18 |
| Printing | button-text | 130 | 0.223 | 5.46 |
| Radical | button-text | 74 | 0.225 | 2.72 |
| Solarized Dark | window-text | 72 | 0.183 | 2.23 |
| Solarized Light | button-text | 73 | 0.265 | 2.13 |
| Tokyo Night | button-text | 93 | 0.325 | 2.88 |
| Tokyo Night Light | button-text | 80 | 0.199 | 3.08 |
| Tokyo Night Storm | button-text | 80 | 0.303 | 2.55 |
| VSCodium Dark | button-text | 97 | 0.487 | 3.20 |
| Vim Dark | window-text | 98 | 0.365 | 3.80 |
| ayu Dark | button-text | 92 | 0.343 | 3.32 |
| ayu Light | button-text | 79 | 0.287 | 2.78 |
| ayu Mirage | button-text | 92 | 0.432 | 2.99 |
| gruvbox Dark | button-text | 104 | 0.513 | 3.03 |
| gruvbox Light | button-text | 95 | 0.256 | 3.83 |

Per-role minima over the 30 themes (same log):

| role | min channel Δ | min luminance Δ | min contrast drop |
|---|---|---|---|
| window-text | 72 | 0.191 | 2.25 |
| text | 72 | 0.191 | 2.35 |
| button-text | 72 | 0.183 | 2.13 |
| tooltip-text | 72 | 0.185 | 2.26 |
| placeholder-text | 50 | 0.202 | 1.35 |
| link | 64 | 0.077 | 1.33 |
| link-visited | 64 | 0.158 | 1.55 |

Before vs after, real rendered text colours (offscreen widget renders, three
themes, `logs/04-after-renders.log` vs `logs/05-before-renders.log`):

```
BEFORE  breeze-light-toolbar-undo  enabled_ink=#271c1b disabled_ink=#271c1b ink_channel_delta=0
AFTER   breeze-light-toolbar-undo  enabled_ink=#271c1b disabled_ink=#948e8e ink_channel_delta=115

BEFORE  breeze-dark-combo-ruleset  enabled_ink=#cfcfc2 disabled_ink=#cfcfc2 ink_channel_delta=0
AFTER   breeze-dark-combo-ruleset  enabled_ink=#cfcfc2 disabled_ink=#737571 ink_channel_delta=92

BEFORE  nord-lineedit-rules        enabled_ink=#d8dee9 disabled_ink=#d8dee9 ink_channel_delta=0
AFTER   nord-lineedit-rules        enabled_ink=#d8dee9 disabled_ink=#7a818c ink_channel_delta=94

BEFORE  breeze-light-button-apply  enabled_ink=#1f1c1b disabled_ink=#1f1c1b ink_channel_delta=0
AFTER   breeze-light-button-apply  enabled_ink=#1f1c1b disabled_ink=#908e8e ink_channel_delta=115
```

The full PNG pairs (enabled/disabled × toolbar-action / combo / line-edit /
button × Breeze Light / Breeze Dark / Nord) are in `screens/` (after) and
`screens-prefix/` (before).

---

## 4. Red / green runs

Both runs execute the **same committed test source**
(`src/app/disabled_look_selftest.cpp`). The red run compiles it against
`prefix/theme_manager_prefix.cpp` — a verbatim copy of
`git show HEAD:src/app/theme_manager.cpp` (sha256
`ec8b69a5…efef69865`, zero `Disabled` assignments) — i.e. the pre-fix palette in
a scratch build; **no product file was temporarily modified**.

RED (pre-fix palette):

```
$ bash evidence/ux-batch-2/rm7-disabled-look/probe/run-selftest.sh \
      /vedit-build/evidence/ux-batch-2/rm7-disabled-look/prefix/theme_manager_prefix.cpp
…
CHECK disabled.palette.min-channel-delta FAIL min=0 theme=Atom One Dark floor=40
CHECK disabled.palette.min-luminance-delta FAIL min=0.000 theme=Atom One Dark floor=0.100
CHECK disabled.palette.min-contrast-drop FAIL min=1.00 theme=Atom One Dark floor=1.40
CHECK disabled.pixels.toolbar-action FAIL style=Fusion theme=Atom One Dark differing=2101
      enabled_hits=162 disabled_hits=153 enabled=#abb2bf disabled=#abb2bf delta=0 luminance=0.000
DISABLED-LOOK-SELFTEST failures=584       (584 × CHECK … FAIL)
exit code 1                                [logs/03-red-prefix-selftest.log]
```

GREEN (fixed palette, built through the product CMake/ctest):

```
$ docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp \
      vedit-qt6-build:local bash -lc \
      'cmake --build target/qt6-build --target vedit-app-disabled-look-selftest --parallel "$(nproc)"'
[40/40] Linking CXX executable src/app/vedit-app-disabled-look-selftest

$ docker run --rm -v "$PWD":/vedit-build -w /vedit-build/target/qt6-build \
      -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp/xdg-rm7 \
      vedit-qt6-build:local bash -lc 'ctest -R vedit-app-disabled-look-selftest -V'
16: CHECK disabled.palette.min-channel-delta PASS min=72 theme=Atom One Dark floor=40
16: CHECK disabled.palette.min-luminance-delta PASS min=0.183 theme=Solarized Dark floor=0.100
16: CHECK disabled.palette.min-contrast-drop PASS min=2.13 theme=Solarized Light floor=1.40
16: CHECK disabled.pixels.toolbar-action PASS style=Fusion themes=30 worst_theme=Atom One Dark
      channel_delta=72 luminance_delta=0.294 differing_px=2333
… (QPushButton / QComboBox / QLineEdit / QPlainTextEdit: PASS, both styles)
16: DISABLED-LOOK-SELFTEST failures=0
1/1 Test #16: vedit-app-disabled-look-selftest ...   Passed   0.27 sec
100% tests passed, 0 tests failed out of 1      exit code 0
                                                [logs/06-green-ctest-verbose.log]
```

What the pixel half of the check proves, per theme × style: a real
`QToolButton` fed by a `QAction` (the Undo/Redo wiring), a `QPushButton`, a
`QComboBox`, a `QLineEdit` and a `QPlainTextEdit` are rendered offscreen enabled
and disabled; the two renders must differ, and the **enabled render must contain
the palette’s Active text colour while the disabled render must contain the
palette’s Disabled text colour** (sampled with a large bold font so glyph-core
pixels exist; no hard-coded hex, the expected colours come from the sample’s own
effective palette). The app’s own style (Fusion) is held to the exact-colour
rule; the other style plugin present in the image (Windows) is held to “the two
renders differ” because it applies its own disabled effect.

---

Sources exercised by both runs (`logs/00-source-sha256.txt`):

```
3aad0537b7cfe98c891640408e0eba9dfb5339dc367935407dfaba801fea2670  src/app/theme_manager.cpp
1309c7c269203c74002a8cb341df86041c514b04a624de3de6affdd378666377  src/app/disabled_look_selftest.cpp
1315a94e82ad22b7f1bb58d3b4e2c18f7eb94da56aa38d8bf890c561a67002c6  src/app/CMakeLists.txt
ec8b69a5e8ac407a3dbfc7b4a513fd29a553bfc33b6eaa66c3ae292efef69865  prefix/theme_manager_prefix.cpp  (pre-fix, used only by the red run)
```

## 5. Files

| path | role |
|---|---|
| `src/app/theme_manager.cpp` | the fix (Disabled group derived from each theme) |
| `src/app/disabled_look_selftest.cpp` | new numeric regression check |
| `src/app/CMakeLists.txt` | target + ctest entry `vedit-app-disabled-look-selftest` |
| `evidence/ux-batch-2/rm7-disabled-look/probe/` | palette probe, render probe, run scripts |
| `evidence/ux-batch-2/rm7-disabled-look/prefix/` | verbatim pre-fix `theme_manager.cpp` used for the red run |
| `evidence/ux-batch-2/rm7-disabled-look/logs/` | red/green/measurement logs |
| `evidence/ux-batch-2/rm7-disabled-look/screens{,-prefix}/` | enabled/disabled PNG pairs (after / before) |

---

## 6. Bounds — what this does NOT prove

1. **Style coverage.** The pinned image offers only `Windows` and `Fusion`
   (`QStyleFactory::keys()`); the user’s KDE desktop may run the **Breeze**
   style, which is not in this image, so Breeze was not rendered here. The
   palette assertions are style-independent (all 30 themes); Breeze reads the
   same `QPalette` groups, but that is reasoning, not a measurement. The
   product’s own Xvfb+xcb screenshot harness (`vedit-app-ux-screenshots`) was
   not driven for this task; the pixel proof is offscreen widget rendering.
2. **Selection roles.** `Highlight` / `HighlightedText` are dimmed for
   completeness but the check only asserts they *differ* (min Δ 9/255 and
   60/255). On some themes the *enabled* `HighlightedText` is already
   low-contrast by theme construction (it is forced to white/black while some
   themes’ `TextSelection` is near-white), so no contrast-drop guarantee is
   meaningful there; e.g. Vim Dark shows a 0.60 “drop” (the disabled variant is
   relatively more visible than the enabled one). No disabled control in this
   product paints a selection.
3. **Frame/bevel roles untouched.** `Light`, `Midlight`, `Mid`, `Dark`,
   `Shadow` are still Qt’s default-palette values rather than theme-derived
   (pre-existing, affects enabled controls too). They were deliberately not
   changed so the enabled rendering stays byte-identical; a disabled control’s
   *text* is the signal this task fixes.
4. **Custom-painted widgets outside the write scope were inspected, not
   modified.** With no disabled paint in `app_shell.cpp`, the only hand-painted
   widget that could ignore `isEnabled()` in a disabled state is the file-tree
   status dot (`src/workspace/filetree/file_status_delegate.cpp:42-89` — the dot
   is drawn at full colour, only the text passes `State_Enabled` to
   `drawItemText`). Today that state is **unreachable**: tree rows are always
   `Qt::ItemIsEnabled` (`file_tree_model.cpp:276-281`) and the lane never calls
   `setEnabled()` (`grep` over `src/workspace/filetree` → none). Also
   out-of-scope but noted: the editor tab-bar stylesheet
   (`src/app/editor_area.cpp:648`) has a `QToolButton { color: … }` rule with no
   `:disabled` variant (only the tab close button is styled, never disabled),
   and the command palette is hard-coded dark (`command_palette.cpp:126`).
5. **Language/theme changes while a dialog is open** follow Qt’s normal
   `QApplication::setPalette` propagation; not separately exercised here.
