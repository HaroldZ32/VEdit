# T40 — F3 claim audit (generated)

Source: `evidence/i18n-l2/logs/20-t40-claim-audit.csv` (produced by `tools/make_reconciliation.py`).

| kind | rows |
|---|---|
| A · claim-flip（already_wrapped 改了方向） | 74 |
| B · decision-change（声称不变，翻译决定改了） | 11 |
| C · supplement-added（T38 的 UI-sink 过滤没发现的 T40 行） | 15 |
| D · reason-verified（只有理由文本改成可验证说法） | 219 |

## A · claim-flip（74）

| # | path:line | string | 判定（tools/verify_claims.py） | 旧 → 新 |
|---|---|---|---|---|
| 1 | `src/app/app_shell.cpp:148` | `Session could not be restored (%1) — starting clean` | own-call+ts | no / 不译 → yes / 译（本批） |
| 2 | `src/app/app_shell.cpp:246` | `open.paths: opened %lld file(s): %s` | no-wrap | yes / 译（已完成） → no / 不译 |
| 3 | `src/app/app_shell.cpp:687` | `Saved %1` | own-call+ts | no / 不译 → yes / 译（本批） |
| 4 | `src/editor/kernel/editor_advanced.cpp:343` | `%1 — available (%2)` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 5 | `src/editor/kernel/editor_advanced.cpp:353` | `Module skeleton` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 6 | `src/editor/kernel/editor_advanced.cpp:354` | `always_ff block` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 7 | `src/editor/kernel/editor_advanced.cpp:355` | `case statement` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 8 | `src/editor/kernel/editor_context_menu.cpp:199` | `Language Server` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 9 | `src/editor/kernel/fim_wiring.cpp:57` | `FIM ghost text: previews the first line only; Tab inserts the full suggestion.` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 10 | `src/editor/kernel/lsp_actions.cpp:547` | `No diagnostics in this file` | own-call+ts | no / 不译 → yes / 译（本批） |
| 11 | `src/workspace/diagnostics/diagnostics_integration.cpp:655` | `Backend switch failed: %1` | own-call+ts | no / 不译 → yes / 译（本批） |
| 12 | `src/workspace/diagnostics/diagnostics_integration.cpp:722` | `%1 (%2) — %3 action(s)` | own-call+ts | no / 不译 → yes / 译（本批） |
| 13 | `src/workspace/diagnostics/diagnostics_integration.cpp:724` | `no table` | own-call+ts | no / 不译 → yes / 译（本批） |
| 14 | `src/workspace/diagnostics/diagnostics_integration.cpp:727` | `unknown basis` | own-call+ts | no / 不译 → yes / 译（本批） |
| 15 | `src/workspace/diagnostics/diagnostics_integration.cpp:754` | `Hover needs a KTextEditor view` | own-call+ts | no / 不译 → yes / 译（本批） |
| 16 | `src/workspace/diagnostics/diagnostics_integration.cpp:769` | `Hover request refused by %1` | own-call+ts | no / 不译 → yes / 译（本批） |
| 17 | `src/workspace/diagnostics/diagnostics_integration.cpp:870` | `Formatting failed: %1` | own-call+ts | no / 不译 → yes / 译（本批） |
| 18 | `src/workspace/diagnostics/diagnostics_integration.cpp:886` | `Formatted with verible-verilog-format` | own-call+ts | no / 不译 → yes / 译（本批） |
| 19 | `src/workspace/diagnostics/diagnostics_integration.cpp:888` | `Already formatted (verible-verilog-format)` | own-call+ts | no / 不译 → yes / 译（本批） |
| 20 | `src/workspace/diagnostics/diagnostics_integration.cpp:898` | `No project file set — open a folder first` | own-call+ts | no / 不译 → yes / 译（本批） |
| 21 | `src/workspace/diagnostics/diagnostics_integration.cpp:918` | `Project lint did not start: %1` | own-call+ts | no / 不译 → yes / 译（本批） |
| 22 | `src/workspace/diagnostics/diagnostics_integration.cpp:931` | `Linting project with %1…` | own-call+ts | no / 不译 → yes / 译（本批） |
| 23 | `src/workspace/diagnostics/diagnostics_integration.cpp:984` | `%1 needs a KTextEditor view` | own-call+ts | no / 不译 → yes / 译（本批） |
| 24 | `src/workspace/diagnostics/diagnostics_integration.cpp:1027` | `No hover from %1 here` | own-call+ts | no / 不译 → yes / 译（本批） |
| 25 | `src/workspace/diagnostics/diagnostics_integration.cpp:1042` | `Rename preview: %1 edit(s) from %2` | own-call+ts | no / 不译 → yes / 译（本批） |
| 26 | `src/workspace/diagnostics/diagnostics_integration.cpp:1051` | `No definition found` | own-call+ts | no / 不译 → yes / 译（本批） |
| 27 | `src/workspace/diagnostics/diagnostics_integration.cpp:1072` | `%1 reference(s) from %2` | own-call+ts | no / 不译 → yes / 译（本批） |
| 28 | `src/workspace/diagnostics/diagnostics_integration.cpp:1084` | `LSP backend=%1 · lint=%2 · %3` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 29 | `src/workspace/diagnostics/diagnostics_panel.cpp:297` | `%1 — analyzed %2 (%3): %4 diagnostic(s)` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 30 | `src/workspace/diagnostics/diagnostics_panel.cpp:317` | `No symbols — the outline backend is missing (%1). ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 31 | `src/workspace/diagnostics/diagnostics_panel.cpp:318` | `See the status line above for the one-line fix.` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 32 | `src/workspace/diagnostics/diagnostics_panel.cpp:322` | `No symbols yet for %1 — analysis is starting, ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 33 | `src/workspace/diagnostics/diagnostics_panel.cpp:323` | `or this file has no structure.` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 34 | `src/workspace/diagnostics/diagnostics_panel.cpp:327` | `No symbols yet — open a file; analysis starts ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 35 | `src/workspace/diagnostics/diagnostics_panel.cpp:420` | `%1:%2:%3 — %4` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 36 | `src/workspace/diagnostics/lint_progress.cpp:111` | `no request to re-run: the last pass had no files` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 37 | `src/workspace/diagnostics/lint_progress.cpp:176` | `cannot re-run this pass` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 38 | `src/workspace/filetree/file_tree_widget.cpp:182` | `Opened '%1'` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 39 | `src/workspace/filetree/file_tree_widget.cpp:334` | `Deleted '%1'` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 40 | `src/workspace/filetree/file_tree_widget.cpp:528` | `Refreshed '%1'` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 41 | `src/workspace/module_tree/module_tree_integration.cpp:135` | `hierarchy: retrying after the project ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 42 | `src/workspace/module_tree/module_tree_integration.cpp:171` | `No hierarchy yet: open a project and press ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 43 | `src/workspace/module_tree/module_tree_integration.cpp:249` | `Empty state: no elaborated project yet. Open a project and press ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 44 | `src/workspace/module_tree/module_tree_panel.cpp:45` | `%1 · no file:line reported` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 45 | `src/workspace/module_tree/module_tree_panel.cpp:138` | `No hierarchy yet: open a project and press Refresh.` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 46 | `src/workspace/module_tree/module_tree_panel.cpp:148` | `No hierarchy yet: open a project and press Refresh.` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 47 | `src/workspace/module_tree/module_tree_panel.cpp:158` | `Hierarchy ready — double-click an instance, a net or a driver/load.` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 48 | `src/workspace/palette/command_palette.cpp:438` | `%1 files · type to filter · Enter to open` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 49 | `src/workspace/palette/command_palette.cpp:440` | `%1 of %2 files` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 50 | `src/workspace/project/project_panel.cpp:84` | `Open a folder from the File Tree (or pass --project <dir>); the expanded ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 51 | `src/workspace/project/project_panel.cpp:85` | `file list, include dirs and defines appear here.` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 52 | `src/workspace/project/project_panel.cpp:218` | `Imported %1 filelist%2` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 53 | `src/workspace/project/project_panel.cpp:271` | `%1 unresolved filelist variable(s): %2` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 54 | `src/workspace/project/project_panel.cpp:280` | `Import completed with %1 unresolved variable(s); the affected ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 55 | `src/workspace/project/project_panel.cpp:281` | `.f lines stay listed below` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 56 | `src/workspace/project/project_panel.cpp:289` | `Saved %1 variable(s); re-expanding` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 57 | `src/workspace/project/project_panel.cpp:312` | `No variables are used by this project yet` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 58 | `src/workspace/project/project_panel.cpp:322` | `Project variables updated; re-expanding` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 59 | `src/workspace/project/project_panel.cpp:355` | `Cannot open %1: no editor jump target is registered` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 60 | `src/workspace/project/project_panel.cpp:361` | `Cannot open %1: this build has no jump seam` | own-call+ts | no / 译（本批） → yes / 译（本批） |
| 61 | `src/workspace/project/project_variables_dialog.cpp:28` | `These variables are used by the imported .f filelists but are not ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 62 | `src/workspace/project/project_variables_dialog.cpp:29` | `resolved by the project table or the environment. Fill them in once.` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 63 | `src/workspace/search/search_jump_target.cpp:122` | `search: jump opened '%s' line=%d column=%d (cursor=%d:%d)` | no-wrap | yes / 译（已完成） → no / 不译 |
| 64 | `src/workspace/settings/settings_panel.cpp:704` | `Theme '%1' is invalid; the default theme is ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 65 | `src/workspace/settings/settings_panel.cpp:705` | `applied instead.` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 66 | `src/workspace/settings/settings_panel.cpp:772` | `Shortcut for '%1' set to %2 and stored by the Rust ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 67 | `src/workspace/settings/settings_panel.cpp:812` | `Preview — press Apply or OK to store it, ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 68 | `src/workspace/settings/settings_panel.cpp:813` | `Cancel to discard.` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 69 | `src/workspace/settings/settings_panel.cpp:828` | `the Rust settings store rejected ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 70 | `src/workspace/settings/settings_panel.cpp:829` | `the write` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 71 | `src/workspace/settings/settings_panel.cpp:831` | `The settings change was applied but could not ` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 72 | `src/workspace/settings/settings_panel.cpp:832` | `be stored; see the diagnostics.` | own-call+ts | no / 译（片段） → yes / 译（片段） |
| 73 | `src/workspace/settings/settings_panel.cpp:1347` | `color: #a33; font-weight: bold;` | no-wrap | yes / 译（已完成） → no / 不译 |
| 74 | `src/workspace/settings/settings_panel.cpp:1347` | `color: #a33; font-weight: bold;` | no-wrap | yes / 译（已完成） → no / 不译 |

## B · decision-change（11）

| # | path:line | string | 判定（tools/verify_claims.py） | 旧 → 新 |
|---|---|---|---|---|
| 1 | `src/app/app_shell.cpp:792` | `no project` | own-call+ts | yes / 译（已完成） → yes / 译（本批） |
| 2 | `src/workspace/diagnostics/diagnostics_panel.cpp:303` | `saved file` | own-call+ts | yes / 译（已完成） → yes / 译（本批） |
| 3 | `src/workspace/diagnostics/diagnostics_panel.cpp:306` | `%1 — %2 diagnostic(s)` | own-call+ts | yes / 译（已完成） → yes / 译（本批） |
| 4 | `src/workspace/filetree/filetree_integration.cpp:48` | `Opened %1` | own-call+ts | yes / 译（已完成） → yes / 译（本批） |
| 5 | `src/workspace/project/project_panel.cpp:77` | `Add one or more .f filelists to the project file set` | own-call+ts | yes / 译（已完成） → yes / 译（本批） |
| 6 | `src/workspace/project/project_panel.cpp:236` | `Removed %1; the file set is being re-expanded` | own-call+ts | yes / 译（已完成） → yes / 译（本批） |
| 7 | `src/workspace/settings/settings_panel.cpp:674` | `Settings loaded from %1` | own-call+ts | yes / 译（已完成） → yes / 译（本批） |
| 8 | `src/workspace/settings/settings_panel.cpp:677` | `Settings loaded from %1 with %2 warning(s); the built-in ` | own-call+ts | yes / 译（已完成） → yes / 译（本批） |
| 9 | `src/workspace/settings/settings_panel.cpp:678` | `defaults were kept where the file is invalid.` | own-call+ts | yes / 译（已完成） → yes / 译（本批） |
| 10 | `src/workspace/settings/settings_panel.cpp:939` | `%1 configuration problem(s) — the built-in defaults were kept ` | own-call+ts | yes / 译（已完成） → yes / 译（本批） |
| 11 | `src/workspace/settings/settings_panel.cpp:940` | `where needed:` | own-call+ts | yes / 译（已完成） → yes / 译（本批） |

## C · supplement-added（15）

| # | path:line | string | 判定（tools/verify_claims.py） | 旧 → 新 |
|---|---|---|---|---|
| 1 | `src/app/app_shell.cpp:686` | `Saved` | own-call+ts | — / — → yes / 译（本批） |
| 2 | `src/workspace/diagnostics/diagnostics_panel.cpp:300` | `pending` | own-call+ts | — / — → yes / 译（本批） |
| 3 | `src/workspace/project/project_integration.cpp:58` | `Project` | own-call+ts | — / — → yes / 译（本批） |
| 4 | `src/workspace/project/project_model.cpp:352` | `Project: %1 — %2 %3` | own-call+ts | — / — → yes / 译（本批） |
| 5 | `src/workspace/project/project_model.cpp:356` | `file` | own-call+ts | — / — → yes / 译（本批） |
| 6 | `src/workspace/project/project_model.cpp:357` | `files` | own-call+ts | — / — → yes / 译（本批） |
| 7 | `src/workspace/project/project_model.cpp:361` | ` · %1 filelist` | own-call+ts | — / — → yes / 译（本批） |
| 8 | `src/workspace/project/project_model.cpp:361` | ` · %1 filelists` | own-call+ts | — / — → yes / 译（本批） |
| 9 | `src/workspace/project/project_model.cpp:367` | ` · %1 variable` | own-call+ts | — / — → yes / 译（本批） |
| 10 | `src/workspace/project/project_model.cpp:367` | ` · %1 variables` | own-call+ts | — / — → yes / 译（本批） |
| 11 | `src/workspace/project/project_model.cpp:372` | ` · %1 line skipped` | own-call+ts | — / — → yes / 译（本批） |
| 12 | `src/workspace/project/project_model.cpp:372` | ` · %1 lines skipped` | own-call+ts | — / — → yes / 译（本批） |
| 13 | `src/workspace/project/project_model.cpp:376` | ` · %1 unresolved` | own-call+ts | — / — → yes / 译（本批） |
| 14 | `src/workspace/settings/settings_panel.cpp:677` | `Settings loaded from %1 with %2 warning(s); the built-in defaults were kept wher` | own-call+ts | — / — → yes / 译（本批） |
| 15 | `src/workspace/settings/settings_panel.cpp:939` | `%1 configuration problem(s) — the built-in defaults were kept where needed:` | own-call+ts | — / — → yes / 译（本批） |

## D · reason-verified

219 行的 `already_wrapped` 与 decision 均未变；理由文本从 T38 的无条件断言改为「tools/verify_claims.py 已验证」的说法。逐行内容见 CSV 的 `change=reason-verified` 行。

