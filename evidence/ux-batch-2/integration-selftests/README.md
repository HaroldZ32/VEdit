# T30 — two stale selftest assertions after RM-2 (toolbar order) and RM-8 (manual title)

Status: **fixed, built and re-run green in the pinned Qt6 image.** Both failures
were stale ASSERTIONS, not product defects: the product truth is correct and the
tests now pin it exactly.

Source of the finding: `evidence/ux-batch-2/u8-final/ctest-failing4.log`
(the captain's first compile+ctest checkpoint on the merged tree, 58 tests,
2 real failures). I did not touch product code.

## 1. What was changed (each with its reason)

All changes are in `src/app/ux_selftest.cpp`.

### 1a. `a2.toolbar.action-order` — RM-2 added `file.switch-project`

* Before: `CHECK a2.toolbar.action-order FAIL action-file.new,action-file.open,action-file.open-folder,action-file.switch-project,...`
  — the assertion's `kToolbarOrder` still listed the pre-RM-2 order.
* Product truth: `src/app/app_shell.cpp:921-928` builds
  `new, open, open-folder, SWITCH PROJECT, save, save-all, undo, redo | separator | search, lint, format`.
  RM-2 added `file.switch-project` next to Open Folder by explicit user request
  (comment "RM-2: `file.switch-project` below is the same reset path under the
  name the user asked for mid-session").
* Change: `kToolbarOrder` gained `"file.switch-project"` in that position.
* **Kept as an EXACT ORDERED comparison** (`actualOrder == expectedOrderNames`);
  no subset check. The risk this test guards is that the toolbar order is
  intentional and stable (a missing/extra/reordered entry is a real regression),
  and the replacement guards exactly that with the new entry included.

### 1b. `a2.help.manual-has-title` and `a2.help.viewer-renders-title` — RM-8 rewrote the manual

* Before: `CHECK a2.help.manual-has-title FAIL VEdit 用户手册` and
  `CHECK a2.help.viewer-renders-title FAIL` — both assertions still looked for
  the old `用户指南` substring.
* Product truth: the shipped manual's H1 is `# VEdit 用户手册`
  (`docs/USER_GUIDE.md:1`), and `HelpViewer::manualTitle()`
  (`src/app/help_viewer.cpp:62`) returns that first H1 line verbatim.
* Change: one shared constant
  `const QString kManualTitle = QStringLiteral("VEdit 用户手册");` feeds both
  assertions, so the header accessor and the rendered view cannot drift apart:
  * `a2.help.manual-has-title`: `HelpViewer::manualTitle() == kManualTitle`
    (exact H1, stronger than the old substring);
  * `a2.help.viewer-renders-title`: the viewer's plain text
    `contains(kManualTitle)` (the rendered document must show the shipped title).
* Risk guarded, same as before: "the manual has the shipped title AND the viewer
  really renders it". The accessor check is now exact, not a substring, so a
  manual whose H1 changes again fails loudly in one place.

## 2. Sweep for the same class of staleness

### (a) RM-5b shortcut changes (`Ctrl+Shift+H`→`Ctrl+Alt+H`, `Ctrl+Alt+V`→`Ctrl+Alt+C`, save-as, Ctrl+P palette, new bindings)

Every hard-coded key sequence in the selftests was compared with the product
defaults. **No assertion needed changing** — none pins a changed binding:

| selftest | sequence(s) | verdict |
|---|---|---|
| `src/app/shortcut_key_path_test.cpp:130` | `Ctrl+Alt+H` (split default), `Ctrl+Shift+V` (vertical) | already the RM-5b truth (`src/app/app_shell.cpp:622,624`); the old `Ctrl+Shift+H` survives only in comments explaining why it moved |
| `src/workspace/settings/settings_selftest.cpp` | `Ctrl+S`, `Ctrl+Shift+J`, `Ctrl+Alt+P`, `Ctrl+Shift+L`, `Alt+\` | test-local `CommandSpec` fixtures (`addCommands`), not product defaults — unchanged |
| `src/workspace/palette/palette_integration_selftest.cpp:152` | `Ctrl+P` | matches the product palette default (`src/workspace/palette/palette_integration.cpp:101`) |
| `…palette_integration_selftest.cpp:229-236` | `Ctrl+Shift+P` | a test-local REBINDING the test performs itself, then restores Ctrl+P — unchanged |
| `src/editor/kernel/find_replace_selftest.cpp:120` | `Ctrl+F`, `Ctrl+H`, `F3`, `Shift+F3` | KTextEditor/kernel defaults, not RM-5b's commands — unchanged |
| `src/editor/kernel/multi_cursor_selftest.cpp:94` | `Ctrl+/`, `Alt+Up`, … | kernel multi-cursor defaults — unchanged |
| `src/editor/kernel/fim_wiring_selftest.cpp:315` | `Alt+\` | matches product (`src/editor/kernel/fim_wiring.cpp:63`) |
| `src/app/ux_selftest.cpp` | `F1` (manual) | matches product (`src/app/app_shell.cpp:690`) |

No selftest asserts the old `Ctrl+Shift+H` split binding or the old `Ctrl+Alt+V`
VCS-panel binding; the only remaining mentions are explanatory comments in
product code and in the key-path test.

### (b) RM-8 manual renames

Grepped selftests for `manualMarkdown`, `manualTitle`, `USER_GUIDE`,
`用户手册`, `用户指南`: the only pinned manual text was the two assertions fixed
above. The `docs` gate (`build/gates/docs.sh`) checks file existence and a
non-empty-line count — it does not pin the title, so it is not stale.
**Deliberately left:** the comment at `src/app/help_viewer.h:30` still says the
heading is `"VEdit 用户指南"`; that is product code (comment only) and outside
this task's write scope — reported, not edited.

### (c) RM-2 added commands (`file.switch-project`, toolbar entry)

Grepped selftests for command-id / action-name lists:
* `src/app/ux_selftest.cpp` `kToolbarOrder` — the only exhaustive action list;
  fixed in 1a.
* `palette_integration_selftest.cpp` checks `ids().contains(...)` and File-menu
  MEMBERSHIP only (`actionInFileMenu`) — not exhaustive, unaffected.
* No selftest pins the File menu's exact action order or a command count.
* RM-2's own coverage (`src/workspace/diagnostics/selftests/rm2_project_switch_e2e.cpp`)
  already exercises `file.switch-project`.
**Deliberately left:** `src/app/main.cpp`'s command-id array is product code, not
a selftest list.

## 3. Re-run (pinned Qt6 image, incremental build of the touched target)

Build (container, workspace at `/vedit-build`):

```
cmake --build target/qt6-build --target vedit-app-ux-selftest -- -j8
[70/74] Linking CXX static library src/app/editor-lane/libvedit_editor_core.a
[74/74] Linking CXX executable src/app/vedit-app-ux-selftest
```

Affected ctest ids (`scratch/ctest-rerun.log`):

```
ctest --test-dir target/qt6-build -R "vedit-app-ux-selftest|vedit-shortcut-key-path-tests" --output-on-failure
1/2 Test #15: vedit-app-ux-selftest ............   Passed    0.15 sec
2/2 Test #19: vedit-shortcut-key-path-tests ....   Passed    0.67 sec
100% tests passed, 0 tests failed out of 2
```

The three previously failing checks, observed directly
(`scratch/ux-selftest-direct.log`, exit 0):

```
CHECK a2.toolbar.action-order PASS action-file.new,action-file.open,action-file.open-folder,action-file.switch-project,action-file.save,action-file.save-all,action-edit.undo,action-edit.redo,---,action-search.focus,action-project.lint,action-diagnostics.format
CHECK a2.help.manual-has-title PASS VEdit 用户手册
CHECK a2.help.viewer-renders-title PASS
A2-UX-SELFTEST failures=0
```

`vedit-shortcut-key-path-tests` was re-run as the sweep's neighbour (it pins the
RM-5b split shortcut) and stays green.

## 4. Scope notes

* No product code was changed. The stale comment in `help_viewer.h:30` is
  reported above instead of edited.
* Only the two affected ctest ids (plus the sweep neighbour) were run; the full
  gate was not.
* `translations/vedit_zh_CN.ts` was **not** touched by this lane.
