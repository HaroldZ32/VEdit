# i18n 候选清单差异报告（01 → 可重现抽取器）

引用：**ADDENDUM A-16**（T45 / i18n 前哨）。

- 旧清单：`evidence/i18n/01-string-inventory.log`（git_head `af85225`，启发式抽取，无脚本）
- 新清单：`evidence/i18n/05-extracted-current.log`（`extract_i18n_candidates.py --profile strict`）
- 对齐方式：先按 `(path, string)` 精确对齐，再按 `string` 单独对齐（行号漂移不算消失）。

## 1. 计数

| 指标 | 旧清单 | 新清单 |
|---|---|---|
| 行数（occurrences） | 466 | 894 |
| 去重字符串 | 466 | 780 |

- 两边 `(path,string)` 完全一致：**422**
- 旧行位置变了但字符串还在新清单里（移动/重号）：**0**
- 旧行在新清单里消失：**44**（其中字符串仍在树里、只是不再满足规则：29；字符串已从树里消失：15）
- 新清单新增：**382**
- 新清单重复字符串：**79** 个串，多出 **114** 次出现（旧清单是「一字符串一行」，新清单是「每个字面量一行」）

## 2. 新增（added）按区域

`in_old_list=false` 的区域 = 旧启发式整片没扫到（例如 `src/terminal/`）。

| 区域 | 新增条数 | 旧清单同区域条数 | 旧清单覆盖过？ |
|---|---|---|---|
| `src/workspace/diagnostics` | 85 | 62 | 是 |
| `src/workspace/module_tree` | 74 | 0 | **否** |
| `src/workspace/project` | 55 | 26 | 是 |
| `src/editor/kernel` | 44 | 62 | 是 |
| `src/workspace/settings` | 28 | 32 | 是 |
| `src/terminal/terminal_selftest.cpp` | 18 | 0 | **否** |
| `src/app/app_shell.cpp` | 17 | 33 | 是 |
| `src/terminal/terminal_panel.cpp` | 11 | 0 | **否** |
| `src/app/editor_area.cpp` | 10 | 14 | 是 |
| `src/terminal/pty_session.cpp` | 10 | 0 | **否** |
| `src/syntax/kde` | 9 | 7 | 是 |
| `src/app/main.cpp` | 4 | 20 | 是 |
| `src/editor/probes` | 3 | 35 | 是 |
| `src/workspace/filetree` | 3 | 55 | 是 |
| `src/workspace/palette` | 3 | 22 | 是 |
| `src/workspace/search` | 3 | 25 | 是 |
| `src/app/crash_reporter.cpp` | 1 | 1 | 是 |
| `src/syntax/folding` | 1 | 10 | 是 |
| `src/syntax/product` | 1 | 1 | 是 |
| `src/terminal/terminal_widget.cpp` | 1 | 0 | **否** |
| `src/workspace/breadcrumb` | 1 | 3 | 是 |

## 3. 消失（gone）逐条

`in_tree` = 该字符串在当前树里是否仍存在；`reason` = 不再成为候选的原因。

| 旧出处 | 字符串 | in_tree | 现路径 | reason |
|---|---|---|---|---|
| `src/app/editor_area.cpp:338` | `    ◀ active` | False | — | 字符串已从当前树删除 |
| `src/workspace/breadcrumb/breadcrumb_model.cpp:41` | ` > ` | True | `src/syntax/grammars/tests/test_syn_grammars.cpp`, `src/workspace/breadcrumb/breadcrumb_model.cpp`, `src/workspace/breadcrumb/breadcrumb_model.h` | 形状规则淘汰：C4-format-only×4 |
| `src/editor/fim/fim_selftest.cpp:242` | ` async-suggestion` | True | `src/editor/fim/fim_selftest.cpp` | 形状规则淘汰：C5-lowercase-token×1 |
| `src/app/main.cpp:1331` | ` built ` | True | `src/app/main.cpp` | 形状规则淘汰：C5-lowercase-token×1 |
| `src/workspace/settings/settings_panel.cpp:488` | ` \| ` | True | `src/syntax/kde/tests/syntax_directory_equivalence.cpp`, `src/syntax/overlay/overlay_selftest.cpp`, `src/syntax/product/tests/test_syntax_product.cpp`, `src/workspace/diagnostics/diagnostics_ffi_selftest.cpp`, `src/workspace/diagnostics/toolchain.cpp` | 形状规则淘汰：C4-format-only×9 |
| `src/workspace/project/project_selftest.cpp:132` | `7 files` | False | — | 字符串已从当前树删除 |
| `src/workspace/settings/settings_panel.cpp:55` | `Applied immediately and stored as `theme` by the Rust service.` | True | `src/workspace/settings/settings_panel.cpp` | 已被 tr() 包装（已翻译），不再属于 QStringLiteral 候选族 |
| `src/workspace/settings/settings_panel.cpp:76` | `Assign to selected command` | True | `src/workspace/settings/settings_panel.cpp` | 已被 tr() 包装（已翻译），不再属于 QStringLiteral 候选族 |
| `src/workspace/search/search_types.cpp:14` | `CMakeFiles` | True | `src/workspace/search/search_types.cpp` | 形状规则淘汰：C10-no-whitespace×1 |
| `src/editor/probes/tx_probe.cpp:40` | `KTextEditor` | True | `src/editor/probes/tx_probe.cpp`, `src/syntax/product/tests/test_syntax_product.cpp` | 形状规则淘汰：C10-no-whitespace×2 |
| `src/workspace/settings/settings_panel.cpp:60` | `Key bindings` | False | — | 字符串已从当前树删除 |
| `src/workspace/settings/settings_panel.cpp:80` | `New shortcut:` | True | `src/workspace/settings/settings_panel.cpp` | 已被 tr() 包装（已翻译），不再属于 QStringLiteral 候选族 |
| `src/workspace/project/project_panel.cpp:38` | `Open a project with --project <dir>; the expanded file list appears here.` | False | — | 字符串已从当前树删除 |
| `src/app/app_shell.cpp:629` | `Panes: %1 · active: %2` | False | — | 字符串已从当前树删除 |
| `src/workspace/diagnostics/diagnostics_integration.cpp:409` | `Re&parse Project (Verible + SLANG)` | False | — | 字符串已从当前树删除 |
| `src/workspace/settings/settings_panel.cpp:104` | `Reload from disk` | True | `src/workspace/settings/settings_panel.cpp` | 已被 tr() 包装（已翻译），不再属于 QStringLiteral 候选族 |
| `src/app/main.cpp:923` | `SystemVerilog` | True | `src/app/main.cpp`, `src/editor/kernel/edit_core_selftest.cpp`, `src/editor/kernel/highlight_diagnosis.cpp`, `src/editor/kernel/highlight_mode.cpp`, `src/syntax/folding/tests/fold_native_gui_probe.cpp` | 形状规则淘汰：C10-no-whitespace×20 |
| `src/workspace/diagnostics/toolchain.cpp:334` | `TOOLCHAIN %1 local=[%2]` | False | — | 字符串已从当前树删除 |
| `src/workspace/diagnostics/diagnostics_selftest.cpp:279` | `UndeclaredIdentifier` | True | `src/workspace/diagnostics/diagnostics_selftest.cpp` | 形状规则淘汰：C10-no-whitespace×1 |
| `src/app/app_shell.cpp:635` | `VEdit — %1` | False | — | 字符串已从当前树删除 |
| `src/app/app_shell.cpp:634` | `VEdit — no project` | False | — | 字符串已从当前树删除 |
| `src/editor/kernel/fold_breadcrumb.cpp:21` | `breadcrumbDebounce` | True | `src/editor/kernel/fold_breadcrumb.cpp` | 形状规则淘汰：C10-no-whitespace×1 |
| `src/workspace/project/project_state.cpp:50` | `capturedAtUtc` | True | `src/workspace/project/project_state.cpp` | 形状规则淘汰：C10-no-whitespace×2 |
| `src/app/main.cpp:788` | `ctrlSAuthorityGuard` | True | `src/app/editor_area.cpp`, `src/app/main.cpp` | 形状规则淘汰：C10-no-whitespace×2 |
| `src/workspace/diagnostics/diagnostics_panel.cpp:40` | `diagnosticsEmptyState` | True | `src/workspace/diagnostics/diagnostics_panel.cpp` | 形状规则淘汰：C10-no-whitespace×1 |
| `src/workspace/diagnostics/diagnostics_panel.cpp:31` | `diagnosticsOutline` | True | `src/workspace/diagnostics/diagnostics_panel.cpp` | 形状规则淘汰：C10-no-whitespace×1 |
| `src/workspace/diagnostics/diagnostics_panel.cpp:25` | `diagnosticsStatus` | True | `src/workspace/diagnostics/diagnostics_panel.cpp` | 形状规则淘汰：C10-no-whitespace×1 |
| `src/app/app_shell.cpp:472` | `dockState` | True | `src/app/session_store.cpp` | 形状规则淘汰：C10-no-whitespace×2 |
| `src/app/app_shell.cpp:477` | `editorLayout` | True | `src/app/session_store.cpp` | 形状规则淘汰：C10-no-whitespace×2 |
| `src/workspace/filetree/file_tree_widget.cpp:105` | `fileTreeEmptyHint` | True | `src/workspace/filetree/file_tree_widget.cpp` | 形状规则淘汰：C10-no-whitespace×1 |
| `src/workspace/filetree/file_tree_widget.cpp:117` | `fileTreeEmptyState` | True | `src/workspace/filetree/file_tree_widget.cpp` | 形状规则淘汰：C10-no-whitespace×1 |
| `src/workspace/filetree/file_tree_widget.cpp:129` | `fileTreeStatus` | True | `src/workspace/filetree/file_tree_widget.cpp` | 形状规则淘汰：C10-no-whitespace×1 |
| `src/workspace/filetree/file_tree_widget.cpp:46` | `fileTreeToolBar` | True | `src/workspace/filetree/file_tree_widget.cpp` | 形状规则淘汰：C10-no-whitespace×1 |
| `src/workspace/filetree/file_tree_widget.cpp:92` | `fileTreeView` | True | `src/workspace/filetree/file_tree_widget.cpp` | 形状规则淘汰：C10-no-whitespace×1 |
| `src/workspace/project/project_state.cpp:52` | `includeDirs` | True | `src/workspace/project/project_state.cpp` | 形状规则淘汰：C10-no-whitespace×2 |
| `src/app/editor_area.cpp:853` | `nextPane` | True | `src/app/editor_area.cpp` | 形状规则淘汰：C10-no-whitespace×2 |
| `src/app/editor_area.cpp:336` | `no provider` | False | — | 字符串已从当前树删除 |
| `src/workspace/project/project_state.cpp:49` | `projectRoot` | True | `src/workspace/project/project_state.cpp` | 形状规则淘汰：C10-no-whitespace×2 |
| `src/workspace/project/project_selftest.cpp:155` | `restored from last session` | False | — | 字符串已从当前树删除 |
| `src/app/editor_area.cpp:261` | `save: the pane has no editor provider` | False | — | 字符串已从当前树删除 |
| `src/workspace/diagnostics/ffi_backends.cpp:361` | `slang (FFI)` | False | — | 字符串已从当前树删除 |
| `src/workspace/diagnostics/ffi_backends.cpp:362` | `slang (FFI): %1` | False | — | 字符串已从当前树删除 |
| `src/syntax/folding/ksyntax_fold_source.cpp:104` | `veditFold` | True | `src/syntax/folding/ksyntax_fold_source.cpp` | 形状规则淘汰：C10-no-whitespace×2 |
| `src/workspace/project/project_selftest.cpp:190` | `{ this is not json` | False | — | 字符串已从当前树删除 |

## 4. 移动（moved）逐条

| 旧出处 | 字符串 | 新出处 |
|---|---|---|

## 5. 重号（duplicates）

同一字符串在新清单里出现多次（旧清单按字符串去重，所以这些在 01 里只有一行）。

| 次数 | 字符串 | 出处 |
|---|---|---|
| 6 | `Open a folder first.` | `src/workspace/filetree/file_tree_widget.cpp:217`, `src/workspace/filetree/file_tree_widget.cpp:219`, `src/workspace/filetree/file_tree_widget.cpp:249`, `src/workspace/filetree/file_tree_widget.cpp:251`, `src/workspace/filetree/file_tree_widget.cpp:436`, `src/workspace/filetree/file_tree_widget.cpp:455` |
| 6 | `Select a file or folder first.` | `src/workspace/filetree/file_tree_widget.cpp:276`, `src/workspace/filetree/file_tree_widget.cpp:278`, `src/workspace/filetree/file_tree_widget.cpp:305`, `src/workspace/filetree/file_tree_widget.cpp:307`, `src/workspace/filetree/file_tree_widget.cpp:474`, `src/workspace/filetree/file_tree_widget.cpp:493` |
| 5 | `Save failed: %1` | `src/app/app_shell.cpp:84`, `src/app/app_shell.cpp:85`, `src/app/app_shell.cpp:657`, `src/app/app_shell.cpp:842`, `src/app/app_shell.cpp:843` |
| 4 | `no project` | `src/app/app_shell.cpp:551`, `src/app/app_shell.cpp:780`, `src/app/main.cpp:286`, `src/app/main.cpp:288` |
| 4 | `FIM 未启用（默认关闭）` | `src/editor/fim/fim_inline_completion.cpp:18`, `src/editor/fim/fim_inline_completion.cpp:28`, `src/editor/fim/fim_inline_completion.cpp:111`, `src/editor/fim/fim_inline_completion.cpp:153` |
| 4 | `no document` | `src/editor/kernel/edit_core_selftest.cpp:187`, `src/editor/kernel/edit_core_selftest.cpp:201`, `src/editor/kernel/edit_core_selftest.cpp:254`, `src/editor/kernel/edit_core_selftest.cpp:334` |
| 4 | `Diagnostics unavailable` | `src/workspace/diagnostics/diagnostics_selftest.cpp:496`, `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:160`, `src/workspace/diagnostics/toolchain_selftest.cpp:152`, `src/workspace/diagnostics/toolchain_ux_selftest.cpp:124` |
| 3 | `Close blocked: %1 has unsaved changes` | `src/app/app_shell.cpp:99`, `src/app/app_shell.cpp:797`, `src/app/app_shell.cpp:854` |
| 3 | `cannot open %1: not a regular file` | `src/app/app_shell.cpp:202`, `src/editor/kernel/document_io.cpp:28`, `src/editor/kernel/ktexteditor_provider.cpp:135` |
| 3 | `theme: %1` | `src/app/app_shell.cpp:562`, `src/app/app_shell.cpp:593`, `src/app/app_shell.cpp:598` |
| 3 | `m0 > inc` | `src/app/main.cpp:1024`, `src/workspace/breadcrumb/tests/breadcrumb_tests.cpp:102`, `src/workspace/breadcrumb/tests/breadcrumb_tests.cpp:187` |
| 3 | `FIM 不可用（未链接 Rust 服务）` | `src/editor/fim/fim_inline_completion.cpp:19`, `src/editor/fim/fim_inline_completion.cpp:29`, `src/editor/fim/fim_inline_completion.cpp:97` |
| 3 | `module m;` | `src/editor/fim/fim_selftest.cpp:312`, `src/editor/fim/fim_selftest.cpp:426`, `src/editor/fim/fim_selftest.cpp:500` |
| 3 | `alpha alpha` | `src/editor/kernel/find_replace_selftest.cpp:240`, `src/editor/kernel/find_replace_selftest.cpp:244`, `src/editor/kernel/find_replace_selftest.cpp:246` |
| 3 | `multi-cursor: no positions given` | `src/editor/kernel/multi_cursor.cpp:159`, `src/editor/kernel/multi_cursor.cpp:193`, `src/editor/kernel/multi_cursor.cpp:236` |
| 3 | `no provider for grammar '%1'` | `src/syntax/folding/syntax_fold_source.cpp:45`, `src/workspace/breadcrumb/syntax_breadcrumb_source.cpp:25`, `src/workspace/breadcrumb/syntax_breadcrumb_source.cpp:49` |
| 3 | `the Rust lint service is unavailable` | `src/workspace/diagnostics/diagnostics_controller.cpp:861`, `src/workspace/diagnostics/diagnostics_controller.cpp:894`, `src/workspace/diagnostics/diagnostics_controller.cpp:920` |
| 3 | `not saved` | `src/workspace/diagnostics/diagnostics_selftest.cpp:546`, `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:180`, `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:192` |
| 3 | `the Rust service handle could not be created` | `src/workspace/diagnostics/ffi_backends.cpp:195`, `src/workspace/diagnostics/ffi_backends.cpp:551`, `src/workspace/module_tree/hierarchy_client.cpp:214` |
| 3 | `no toolchain` | `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:162`, `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:163`, `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:164` |
| 3 | `A file or folder named %1 already exists` | `src/workspace/filetree/file_ops.cpp:96`, `src/workspace/filetree/file_ops.cpp:123`, `src/workspace/filetree/file_ops.cpp:154` |
| 3 | `permission denied` | `src/workspace/filetree/tests/filetree_tests.cpp:196`, `src/workspace/filetree/tests/filetree_tests.cpp:544`, `src/workspace/filetree/tests/filetree_tests.cpp:572` |
| 3 | `11 files` | `src/workspace/project/project_selftest.cpp:248`, `src/workspace/project/project_selftest.cpp:274`, `src/workspace/project/project_selftest.cpp:278` |
| 2 | `cannot open %1` | `src/app/app_shell.cpp:227`, `src/workspace/search/search_jump_target.cpp:67` |
| 2 | `unknown error` | `src/app/editor_area.cpp:344`, `src/workspace/module_tree/hierarchy_client.cpp:397` |
| 2 | `Save failed` | `src/app/main.cpp:937`, `src/app/main.cpp:940` |
| 2 | `module:m0 > function:inc` | `src/app/main.cpp:1021`, `src/workspace/breadcrumb/tests/breadcrumb_tests.cpp:104` |
| 2 | `FIM 请求未被 Rust 服务接受` | `src/editor/fim/fim_backend.cpp:50`, `src/editor/fim/fim_inline_completion.cpp:79` |
| 2 | `FIM 超时（%1 ms 无结果）` | `src/editor/fim/fim_backend.cpp:70`, `src/editor/fim/fim_inline_completion.cpp:48` |
| 2 | `FIM 建议就绪` | `src/editor/fim/fim_inline_completion.cpp:108`, `src/editor/fim/fim_inline_completion.cpp:150` |
| 2 | `FIM 返回空建议` | `src/editor/fim/fim_inline_completion.cpp:108`, `src/editor/fim/fim_inline_completion.cpp:150` |
| 2 | `FIM 失败（无详情）` | `src/editor/fim/fim_inline_completion.cpp:114`, `src/editor/fim/fim_inline_completion.cpp:156` |
| 2 | `FIM 已取消` | `src/editor/fim/fim_inline_completion.cpp:196`, `src/editor/fim/fim_inline_completion.cpp:202` |
| 2 | `FIM 未启用` | `src/editor/fim/fim_selftest.cpp:310`, `src/editor/fim/fim_selftest.cpp:315` |
| 2 | `%1 failed: %2` | `src/editor/kernel/atomic_save.cpp:39`, `src/workspace/diagnostics/toolchain.cpp:515` |
| 2 | `cannot read %1: %2` | `src/editor/kernel/document_io.cpp:35`, `src/workspace/project/project_state.cpp:122` |
| 2 | `no KTextEditor view` | `src/editor/kernel/edit_core_selftest.cpp:234`, `src/editor/kernel/edit_core_selftest.cpp:308` |
| 2 | `before=%1 after=%2` | `src/editor/kernel/edit_core_selftest.cpp:409`, `src/editor/kernel/edit_core_selftest.cpp:415` |
| 2 | `one two one two` | `src/editor/kernel/find_replace_selftest.cpp:263`, `src/editor/kernel/find_replace_selftest.cpp:278` |
| 2 | `multi-cursor: no view` | `src/editor/kernel/multi_cursor.cpp:155`, `src/editor/kernel/multi_cursor.cpp:324` |
| 2 | `multi-cursor: no document` | `src/editor/kernel/multi_cursor.cpp:189`, `src/editor/kernel/multi_cursor.cpp:232` |
| 2 | `foo bar foo` | `src/editor/probes/replace_probe.cpp:82`, `src/editor/probes/replace_probe2.cpp:109` |
| 2 | `foobarbaz bar foobarbaz` | `src/editor/probes/replace_probe.cpp:84`, `src/editor/probes/replace_probe2.cpp:111` |
| 2 | `alpha beta alpha` | `src/editor/probes/replace_probe.cpp:86`, `src/editor/probes/replace_probe2.cpp:112` |
| 2 | `a beta a` | `src/editor/probes/replace_probe.cpp:87`, `src/editor/probes/replace_probe2.cpp:113` |
| 2 | `parse returned no tree` | `src/syntax/folding/syntax_fold_source.cpp:54`, `src/workspace/breadcrumb/syntax_breadcrumb_source.cpp:56` |
| 2 | `exited with code 7` | `src/terminal/terminal_selftest.cpp:277`, `src/terminal/terminal_selftest.cpp:278` |
| 2 | `no LSP backend in this build` | `src/workspace/diagnostics/diagnostics_controller.cpp:276`, `src/workspace/diagnostics/ffi_backends.cpp:767` |
| 2 | `%1:%2:%3 — %4` | `src/workspace/diagnostics/diagnostics_panel.cpp:59`, `src/workspace/diagnostics/diagnostics_panel.cpp:419` |
| 2 | `fake session starting` | `src/workspace/diagnostics/diagnostics_selftest.cpp:85`, `src/workspace/diagnostics/diagnostics_selftest.cpp:108` |
| 2 | `missing=[verible, slang]` | `src/workspace/diagnostics/diagnostics_selftest.cpp:493`, `src/workspace/diagnostics/toolchain_selftest.cpp:149` |
| 2 | `no LSP session` | `src/workspace/diagnostics/ffi_backends.cpp:246`, `src/workspace/diagnostics/ffi_backends.cpp:314` |
| 2 | `'%1' no longer exists` | `src/workspace/filetree/file_ops.cpp:145`, `src/workspace/filetree/file_ops.cpp:180` |
| 2 | `The folder '%1' cannot be read (permission denied).` | `src/workspace/filetree/file_tree_model.cpp:90`, `src/workspace/filetree/file_tree_model.cpp:423` |
| 2 | `Cannot delete the folder that is open. Close it first.` | `src/workspace/filetree/file_tree_widget.cpp:312`, `src/workspace/filetree/file_tree_widget.cpp:497` |
| 2 | `Cannot open %1: no active editor pane` | `src/workspace/filetree/filetree_integration.cpp:40`, `src/workspace/palette/palette_integration.cpp:183` |
| 2 | `Cannot open %1` | `src/workspace/filetree/filetree_integration.cpp:44`, `src/workspace/palette/palette_integration.cpp:189` |
| 2 | `Opened %1` | `src/workspace/filetree/filetree_integration.cpp:46`, `src/workspace/palette/palette_integration.cpp:187` |
| 2 | `does not exist` | `src/workspace/filetree/tests/filetree_tests.cpp:164`, `src/workspace/project/project_selftest.cpp:384` |
| 2 | `%1 ms` | `src/workspace/filetree/tests/filetree_tests.cpp:345`, `src/workspace/filetree/tests/filetree_tests.cpp:360` |
| 2 | `hierarchy: %1` | `src/workspace/module_tree/hierarchy_types.cpp:42`, `src/workspace/module_tree/module_tree_integration.cpp:124` |
| 2 | `No hierarchy yet: open a project and press Refresh.` | `src/workspace/module_tree/module_tree_panel.cpp:137`, `src/workspace/module_tree/module_tree_panel.cpp:147` |
| 2 | `put the cursor on a signal name in the editor first` | `src/workspace/module_tree/module_tree_panel.cpp:295`, `src/workspace/module_tree/module_tree_panel.cpp:316` |
| 2 | `%1 · %2` | `src/workspace/module_tree/module_tree_panel.cpp:309`, `src/workspace/module_tree/module_tree_panel.cpp:330` |
| 2 | `assign source` | `src/workspace/module_tree/module_tree_selftest.cpp:144`, `src/workspace/module_tree/module_tree_selftest.cpp:165` |
| 2 | `port clk (in)` | `src/workspace/module_tree/module_tree_selftest.cpp:149`, `src/workspace/module_tree/module_tree_selftest.cpp:151` |
| 2 | `no driver` | `src/workspace/module_tree/module_tree_selftest.cpp:331`, `src/workspace/module_tree/module_tree_selftest.cpp:423` |
| 2 | `no project root: open a folder` | `src/workspace/module_tree/module_tree_selftest.cpp:434`, `src/workspace/module_tree/module_tree_selftest.cpp:436` |
| 2 | `No project` | `src/workspace/palette/command_palette.cpp:432`, `src/workspace/palette/palette_selftest.cpp:226` |
| 2 | `cannot create %1` | `src/workspace/palette/palette_bench.cpp:100`, `src/workspace/project/project_state.cpp:154` |
| 2 | `No project open` | `src/workspace/project/project_model.cpp:347`, `src/workspace/project/project_selftest.cpp:234` |
| 2 | `No project model is attached` | `src/workspace/project/project_panel.cpp:209`, `src/workspace/project/project_panel.cpp:228` |
| 2 | `no state directory is available` | `src/workspace/project/project_state.cpp:115`, `src/workspace/project/project_state.cpp:148` |
| 2 | `Enter a query` | `src/workspace/search/search_selftest.cpp:309`, `src/workspace/search/search_selftest.cpp:388` |
| 2 | `this build has no vd-ffi settings ABI` | `src/workspace/settings/settings_client.cpp:126`, `src/workspace/settings/settings_client.cpp:166` |
| 2 | `Stored by the Rust service in %1` | `src/workspace/settings/settings_panel.cpp:798`, `src/workspace/settings/settings_panel.cpp:863` |
| 2 | `color: #a33; font-weight: bold;` | `src/workspace/settings/settings_panel.cpp:1185`, `src/workspace/settings/settings_panel.cpp:1340` |
| 2 | `theme = "%1"` | `src/workspace/settings/settings_selftest.cpp:226`, `src/workspace/settings/settings_selftest.cpp:308` |
| 2 | `invalid TOML` | `src/workspace/settings/settings_selftest.cpp:293`, `src/workspace/settings/settings_selftest.cpp:296` |

## 6. 新增（added）全量

| 出处 | 字符串 |
|---|---|
| `src/app/app_shell.cpp:99` | `Close blocked: %1 has unsaved changes` |
| `src/app/app_shell.cpp:186` | `cannot open a file: the editor area is not available` |
| `src/app/app_shell.cpp:196` | `cannot open %1: no such file or directory` |
| `src/app/app_shell.cpp:218` | `cannot open %1: no editor pane is available` |
| `src/app/app_shell.cpp:238` | `Opened %1 file(s)` |
| `src/app/app_shell.cpp:297` | `color: #e8a33d; font-weight: 600;` |
| `src/app/app_shell.cpp:451` | `Open File` |
| `src/app/app_shell.cpp:452` | `Verilog/SystemVerilog (*.v *.sv *.svh *.vh);;` |
| `src/app/app_shell.cpp:753` | `● 未保存的更改` |
| `src/app/app_shell.cpp:755` | `unsaved document` |
| `src/app/app_shell.cpp:766` | `Panes: %1 · active: %2 · tabs: %3` |
| `src/app/app_shell.cpp:783` | `VEdit — %1[*]` |
| `src/app/app_shell.cpp:794` | `close refused (headless): %1 has unsaved changes` |
| `src/app/app_shell.cpp:797` | `Close blocked: %1 has unsaved changes` |
| `src/app/app_shell.cpp:804` | `%1 has unsaved changes.` |
| `src/app/app_shell.cpp:807` | `This document has no file path yet, so it cannot be saved here.` |
| `src/app/app_shell.cpp:854` | `Close blocked: %1 has unsaved changes` |
| `src/app/crash_reporter.cpp:369` | `%1 (%2 bytes)` |
| `src/app/editor_area.cpp:342` | `session: cannot reopen %1: %2` |
| `src/app/editor_area.cpp:344` | `unknown error` |
| `src/app/editor_area.cpp:407` | `Close File` |
| `src/app/editor_area.cpp:570` | `● %1` |
| `src/app/editor_area.cpp:606` | `QTabBar { background: %6; }` |
| `src/app/editor_area.cpp:622` | `color: %1; font-weight: 600;` |
| `src/app/editor_area.cpp:766` | `open: the pane could not create a tab` |
| `src/app/editor_area.cpp:813` | `save: the pane holds no such document` |
| `src/app/editor_area.cpp:1560` | `session: no document of pane %1 could be reopened (%2 archived)` |
| `src/app/editor_area.cpp:1630` | `session layout version %1 is newer than %2: only the ` |
| `src/app/main.cpp:563` | `no such file` |
| `src/app/main.cpp:1383` | `File(s) to open (each additional file gets a split).` |
| `src/app/main.cpp:1395` | `Run the Rust C ABI checks against the vd-ffi smoke fixture ` |
| `src/app/main.cpp:1409` | `Raise a signal after installing the crash reporter ` |
| `src/editor/kernel/atomic_save.cpp:39` | `%1 failed: %2` |
| `src/editor/kernel/document_io.cpp:71` | `KTextEditor::Editor::instance() is null ` |
| `src/editor/kernel/fim_wiring.cpp:29` | ` \u21b5` |
| `src/editor/kernel/fim_wiring.cpp:49` | `color: gray; background-color: rgba(160, 160, 160, 40); border-radius: 2px;` |
| `src/editor/kernel/fim_wiring.cpp:56` | `FIM ghost text: previews the first line only; Tab inserts the full suggestion.` |
| `src/editor/kernel/find_replace.cpp:123` | `find: invalid regular expression: %1` |
| `src/editor/kernel/find_replace.cpp:169` | `find: more than %1 matches; narrow the pattern or use a ` |
| `src/editor/kernel/find_replace.cpp:256` | `replace: no document` |
| `src/editor/kernel/find_replace.cpp:260` | `replace: the match range is invalid` |
| `src/editor/kernel/find_replace.cpp:267` | `replace: the document refused the replacement` |
| `src/editor/kernel/find_replace_selftest.cpp:182` | `invalid regular expression` |
| `src/editor/kernel/find_replace_selftest.cpp:238` | `X X` |
| `src/editor/kernel/find_replace_selftest.cpp:240` | `alpha alpha` |
| `src/editor/kernel/find_replace_selftest.cpp:244` | `alpha alpha` |
| `src/editor/kernel/find_replace_selftest.cpp:246` | `alpha alpha` |
| `src/editor/kernel/find_replace_selftest.cpp:258` | `1 two one two` |
| `src/editor/kernel/find_replace_selftest.cpp:263` | `one two one two` |
| `src/editor/kernel/find_replace_selftest.cpp:271` | `1 two 1 two` |
| `src/editor/kernel/find_replace_selftest.cpp:274` | `Replaced 2 of 2` |
| `src/editor/kernel/find_replace_selftest.cpp:278` | `one two one two` |
| `src/editor/kernel/find_replace_selftest.cpp:292` | `line0='%1' line1='%2'` |
| `src/editor/kernel/find_replace_selftest.cpp:296` | `alpha x alpha` |
| `src/editor/kernel/find_replace_selftest.cpp:308` | `Type to search` |
| `src/editor/kernel/find_replace_selftest.cpp:309` | `line0='%1' status='%2'` |
| `src/editor/kernel/highlight_diagnosis.cpp:100` | `%1: url='%2' mode='%3' highlightingMode='%4' availableModes=%5` |
| `src/editor/kernel/highlight_diagnosis.cpp:106` | `%1: got '%2', expected '%3'` |
| `src/editor/kernel/highlight_diagnosis.cpp:111` | `%1: resolved %2 -> formats=%3 begins=%4 ends=%5` |
| `src/editor/kernel/highlight_diagnosis.cpp:120` | `%1: formats=%2` |
| `src/editor/kernel/highlight_diagnosis.cpp:123` | `%1: begins=%2 ends=%3` |
| `src/editor/kernel/highlight_mode.cpp:117` | `the editor repository has no '%1' definition; ` |
| `src/editor/kernel/highlight_mode.cpp:124` | `'%1' resolves to author '%2' (not VEdit); the delivered ` |
| `src/editor/kernel/multi_cursor.cpp:311` | `multi-cursor: deleted %1 of %2 ranges` |
| `src/editor/kernel/multi_cursor_selftest.cpp:354` | `if (x) {` |
| `src/editor/kernel/multi_cursor_selftest.cpp:373` | `line='%1' cursor=%2,%3 cursors=%4` |
| `src/editor/kernel/multi_cursor_selftest.cpp:395` | `cursor=%1,%2 cursors=%3 text='%4'` |
| `src/editor/kernel/multi_cursor_selftest.cpp:411` | `text='%1' cursor=%2,%3` |
| `src/editor/kernel/multi_cursor_selftest.cpp:426` | `    // code;` |
| `src/editor/kernel/multi_cursor_selftest.cpp:430` | `    code;` |
| `src/editor/kernel/multi_cursor_selftest.cpp:475` | `ABC DEF` |
| `src/editor/kernel/multi_cursor_selftest.cpp:479` | `abc def` |
| `src/editor/kernel/multi_cursor_selftest.cpp:544` | `pixels=%1 point=%2,%3` |
| `src/editor/kernel/undo_ops.cpp:27` | `%1: no document` |
| `src/editor/kernel/undo_ops.cpp:34` | `%1: the runtime document exposes no %2 slot ` |
| `src/editor/kernel/undo_ops.cpp:45` | `%1: invoking %2 failed` |
| `src/editor/probes/replace_probe.cpp:58` | `REPLACE case=%1 matches=%2 expect_matches=%3 text_sha=%4 expect_sha=%5 ` |
| `src/editor/probes/replace_probe2.cpp:88` | `REPLACE2 case=%1 found=%2 replaced=%3 expect=%4 text_sha=%5 expect_sha=%6 ` |
| `src/editor/probes/tx_steps_probe2.cpp:59` | `STEPS2 %1 case=one_tx_per_edit len_before=%2 steps_to_stable=%3 ` |
| `src/syntax/folding/ksyntax_fold_source.cpp:110` | `%1 declares no keyword-list fold region` |
| `src/syntax/kde/ksyntax_data.cpp:91` | `Repository::addCustomSearchPath(private_dir) at startup; XDG_DATA_DIRS is not ` |
| `src/syntax/kde/ksyntax_data.cpp:147` | `forbidden KDE syntax definition present in the private data ` |
| `src/syntax/kde/ksyntax_data.cpp:153` | `no private syntax directory configured; falling back to standard ` |
| `src/syntax/kde/ksyntax_data.cpp:189` | `Verilog syntax definition is not available; .v files fall back to plain text` |
| `src/syntax/kde/ksyntax_data.cpp:193` | `SystemVerilog definition exists only inside the syntax library (KDE build) and is ` |
| `src/syntax/kde/ksyntax_data.cpp:198` | `SystemVerilog definition not shipped (C7 pending): .sv files degrade to the Verilog ` |
| `src/syntax/kde/ksyntax_data.cpp:204` | `.v resolves to definition '%1' by file name (the built-in 'V'/vlang ` |
| `src/syntax/kde/ksyntax_data.cpp:212` | `.sv resolves to definition '%1' by file name; use ` |
| `src/syntax/kde/ksyntax_data.cpp:238` | `%1: SystemVerilog definition missing (C7) -> falling back to ` |
| `src/syntax/product/syntax_product.cpp:95` | `KTextEditor::Editor::instance() returned null: the editing kernel ` |
| `src/terminal/pty_session.cpp:108` | `a shell session is already running (pid %1)` |
| `src/terminal/pty_session.cpp:113` | `shell is not executable: %1` |
| `src/terminal/pty_session.cpp:147` | `posix_openpt failed: %1` |
| `src/terminal/pty_session.cpp:150` | `grantpt/unlockpt failed: %1` |
| `src/terminal/pty_session.cpp:156` | `ptsname failed: %1` |
| `src/terminal/pty_session.cpp:169` | `fork failed: %1` |
| `src/terminal/pty_session.cpp:415` | `signal %1` |
| `src/terminal/pty_session.cpp:428` | `exit status unknown` |
| `src/terminal/pty_session.cpp:431` | `killed by signal %1 (%2)` |
| `src/terminal/pty_session.cpp:433` | `exited with code %1` |
| `src/terminal/terminal_panel.cpp:72` | `Close the current shell and open a new session` |
| `src/terminal/terminal_panel.cpp:74` | `Clear the scrollback of this panel` |
| `src/terminal/terminal_panel.cpp:76` | `Close the terminal panel` |
| `src/terminal/terminal_panel.cpp:85` | `background: #5a2b2b; color: #f2dede; padding: 3px 8px;` |
| `src/terminal/terminal_panel.cpp:189` | `Shell %1. Press Enter or Restart to start a new session.` |
| `src/terminal/terminal_panel.cpp:199` | `Shell: %1` |
| `src/terminal/terminal_panel.cpp:202` | `Not started` |
| `src/terminal/terminal_panel.cpp:206` | `Running · pid %1` |
| `src/terminal/terminal_panel.cpp:210` | `Killed · signal %1 (%2)` |
| `src/terminal/terminal_panel.cpp:216` | `Exited · code %1` |
| `src/terminal/terminal_panel.cpp:219` | `Stopped · %1` |
| `src/terminal/terminal_selftest.cpp:266` | `printf hello; exit 7` |
| `src/terminal/terminal_selftest.cpp:277` | `exited with code 7` |
| `src/terminal/terminal_selftest.cpp:278` | `exited with code 7` |
| `src/terminal/terminal_selftest.cpp:286` | `kill -TERM $$` |
| `src/terminal/terminal_selftest.cpp:386` | `sleep 20 & ls -l /proc/$!/fd; kill $!` |
| `src/terminal/terminal_selftest.cpp:416` | `head -c %1 /dev/zero \| tr '\0' x` |
| `src/terminal/terminal_selftest.cpp:426` | `delivered=%1 produced=%2` |
| `src/terminal/terminal_selftest.cpp:429` | `max=%1 limit=%2` |
| `src/terminal/terminal_selftest.cpp:489` | `printf done; exit 42` |
| `src/terminal/terminal_selftest.cpp:527` | `exit 5` |
| `src/terminal/terminal_selftest.cpp:717` | `VEdit — Terminal panel` |
| `src/terminal/terminal_selftest.cpp:718` | `QDockWidget::title { background: #2b2b2b; color: #d4d4d4;` |
| `src/terminal/terminal_selftest.cpp:723` | `Editor area — owned by src/app (application shell)` |
| `src/terminal/terminal_selftest.cpp:725` | `background: #232323; color: #6f6f6f;` |
| `src/terminal/terminal_selftest.cpp:778` | `printf 'work done; exiting\n'; exit 42` |
| `src/terminal/terminal_selftest.cpp:799` | `Terminal panel self-check (T35)` |
| `src/terminal/terminal_selftest.cpp:801` | `Run the headless checks.` |
| `src/terminal/terminal_selftest.cpp:803` | `Capture the panel screenshots into <dir>.` |
| `src/terminal/terminal_widget.cpp:93` | `DejaVu Sans Mono` |
| `src/workspace/breadcrumb/breadcrumb_bar.cpp:146` | `%1: %2 (line %3)` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:140` | `已取消 — %1/%2 文件` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:490` | `large pass: %1 files — cancel is available (%2/%3)` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:516` | `%1/%2 文件已扫描 — %3 条 CLI 诊断` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:562` | `%1: lsp %2/%3 file(s), diagnostics %4` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:647` | `a project lint pass is already running` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:660` | `no lint backend is installed` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:671` | `%1: the lint tool could not be resolved` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:695` | `no files to lint (every file has an open LSP session)` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:713` | `%1: the pass could not be started` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:732` | `large pass: %1 files — cancel is available` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:741` | `no project lint pass has run yet` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:861` | `the Rust lint service is unavailable` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:894` | `the Rust lint service is unavailable` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:920` | `the Rust lint service is unavailable` |
| `src/workspace/diagnostics/diagnostics_controller.cpp:932` | `the lint job is unknown to the service` |
| `src/workspace/diagnostics/diagnostics_ffi_selftest.cpp:187` | `%1 outline entries` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:520` | `Selected LSP backend and CLI lint engine` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:645` | `Backend switch failed: %1` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:711` | `%1 (%2) — %3 action(s)` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:712` | `no table` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:714` | `unknown basis` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:739` | `New name:` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:768` | `Formatting failed: %1` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:785` | `Formatted with verible-verilog-format` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:786` | `Already formatted (verible-verilog-format)` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:796` | `No project file set — open a folder first` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:815` | `Project lint did not start: %1` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:827` | `Linting project with %1…` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:878` | `%1 needs a KTextEditor view` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:917` | `Rename preview: %1 edit(s) from %2` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:925` | `No definition found` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:946` | `%1 reference(s) from %2` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:958` | `LSP backend=%1 · lint=%2 · %3` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:1104` | `Go to &Definition` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:1107` | `Find &References` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:1111` | `&Rename Symbol...` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:1115` | `&Format Document` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:1119` | `&Lint Project` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:1122` | `Show Backend &Capabilities` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:1125` | `Reload Backend Settings` |
| `src/workspace/diagnostics/diagnostics_integration.cpp:1172` | `Re&parse Project` |
| `src/workspace/diagnostics/diagnostics_outline_model.cpp:39` | `%1 (kind %2) at line %3:%4` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:59` | `%1:%2:%3 — %4` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:238` | `no problems reported for %1 — edit or save it to run the ` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:242` | `open a file to start the analysis, or open a project folder ` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:246` | `No diagnostics — the diagnostics backend is missing (%1); %2.` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:249` | `No diagnostics — %1.` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:296` | `%1 — analyzed %2 (%3): %4 diagnostic(s)` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:305` | `%1 — %2 diagnostic(s)` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:316` | `No symbols — the outline backend is missing (%1). ` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:321` | `No symbols yet for %1 — analysis is starting, ` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:326` | `No symbols yet — open a file; analysis starts ` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:382` | `Errors: %1 · Warnings: %2 · Information: %3` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:388` | `<span style='color:%1'><b>Errors: %2</b></span>` |
| `src/workspace/diagnostics/diagnostics_panel.cpp:419` | `%1:%2:%3 — %4` |
| `src/workspace/diagnostics/ffi_backends.cpp:271` | `backend=%1 · %2` |
| `src/workspace/diagnostics/ffi_backends.cpp:388` | `unknown LSP backend` |
| `src/workspace/diagnostics/ffi_backends.cpp:519` | `%1 (FFI)` |
| `src/workspace/diagnostics/ffi_backends.cpp:520` | `%1 (FFI): %2` |
| `src/workspace/diagnostics/ffi_backends.cpp:704` | `formatting always uses verible-verilog-format, independent of ` |
| `src/workspace/diagnostics/ffi_backends.cpp:706` | `verible-verilog-format not found (bundled, local checkout, PATH ` |
| `src/workspace/diagnostics/ffi_backends.cpp:719` | `editor-synthesised (syntax-level, not backend-provided)` |
| `src/workspace/diagnostics/ffi_backends.cpp:720` | `editor-synthesised by the editor (T16); absent until it lands` |
| `src/workspace/diagnostics/ffi_backends.cpp:729` | `no capability row for '%1' in the %2 table` |
| `src/workspace/diagnostics/ffi_backends.cpp:730` | `selected backend` |
| `src/workspace/diagnostics/ffi_backends.cpp:740` | `%1 does not provide %2` |
| `src/workspace/diagnostics/ffi_backends.cpp:747` | `%1 is not connected yet - connect to know` |
| `src/workspace/diagnostics/ffi_backends.cpp:848` | `backend=%1 lint=%2 lsp=%3 engine=%4` |
| `src/workspace/diagnostics/lint_progress.cpp:110` | `no request to re-run: the last pass had no files` |
| `src/workspace/diagnostics/lint_progress.cpp:175` | `cannot re-run this pass` |
| `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:231` | `%1 (%2x%3)` |
| `src/workspace/diagnostics/toolchain.cpp:184` | `%1: missing (probed %2)` |
| `src/workspace/diagnostics/toolchain.cpp:233` | `aux source=slang-server:%1 verible-format:%2 verible-lint:%3 missing=[%4]` |
| `src/workspace/diagnostics/toolchain.cpp:255` | `Verible diagnostics, navigation and the outline ` |
| `src/workspace/diagnostics/toolchain.cpp:269` | `Fix: bundle `verible-verilog-ls`/`slang` in the product's bin/ ` |
| `src/workspace/diagnostics/toolchain.cpp:300` | `%1 not found (bundled, local checkout, PATH and env all empty)` |
| `src/workspace/diagnostics/toolchain.cpp:311` | `slang-server probed: %1` |
| `src/workspace/diagnostics/toolchain.cpp:315` | `verible-verilog-format probed: %1` |
| `src/workspace/diagnostics/toolchain.cpp:319` | `verible-verilog-lint probed: %1` |
| `src/workspace/diagnostics/toolchain.cpp:340` | `Diagnostics unavailable: %1 not found (bundled, local checkout, ` |
| `src/workspace/diagnostics/toolchain.cpp:430` | `TOOLCHAIN %1 %2 local=[%3]` |
| `src/workspace/diagnostics/toolchain.cpp:491` | `verible-verilog-format is missing` |
| `src/workspace/diagnostics/toolchain.cpp:499` | `cannot start %1: %2` |
| `src/workspace/diagnostics/toolchain.cpp:509` | `%1 timed out` |
| `src/workspace/diagnostics/toolchain.cpp:515` | `%1 failed: %2` |
| `src/workspace/filetree/file_ops.cpp:195` | `%1 (the folder is not empty)` |
| `src/workspace/filetree/tests/filetree_tests.cpp:345` | `%1 ms` |
| `src/workspace/filetree/tests/filetree_tests.cpp:360` | `%1 ms` |
| `src/workspace/module_tree/hierarchy_client.cpp:209` | `this build has no Rust service layer (vd-ffi), so the hierarchy ` |
| `src/workspace/module_tree/hierarchy_client.cpp:231` | `project model (%1)` |
| `src/workspace/module_tree/hierarchy_client.cpp:242` | `no project root: open a folder or start vedit-app with --project <dir>` |
| `src/workspace/module_tree/hierarchy_client.cpp:259` | `project expansion was rejected: %1` |
| `src/workspace/module_tree/hierarchy_client.cpp:264` | `expanding the project file set…` |
| `src/workspace/module_tree/hierarchy_client.cpp:303` | `elaboration was rejected: %1` |
| `src/workspace/module_tree/hierarchy_client.cpp:307` | `elaborating %1 project file(s)…` |
| `src/workspace/module_tree/hierarchy_client.cpp:330` | `the hierarchy service did not answer within 30 s` |
| `src/workspace/module_tree/hierarchy_client.cpp:342` | `project expansion failed: %1` |
| `src/workspace/module_tree/hierarchy_client.cpp:362` | `elaboration failed: %1` |
| `src/workspace/module_tree/hierarchy_client.cpp:397` | `unknown error` |
| `src/workspace/module_tree/hierarchy_tree.cpp:56` | `no instantiation position was reported for %1` |
| `src/workspace/module_tree/hierarchy_tree.cpp:67` | `%1 has no driver in the elaborated design` |
| `src/workspace/module_tree/hierarchy_tree.cpp:72` | `source of %1: %2 · %3` |
| `src/workspace/module_tree/hierarchy_tree.cpp:76` | `%1 has a driver (%2) but no usable position` |
| `src/workspace/module_tree/hierarchy_tree.cpp:87` | `%1 has no load in the elaborated design` |
| `src/workspace/module_tree/hierarchy_tree.cpp:91` | `target of %1: %2 · %3` |
| `src/workspace/module_tree/hierarchy_tree.cpp:95` | `%1 has a load (%2) but no usable position` |
| `src/workspace/module_tree/hierarchy_tree.cpp:108` | `declaration of %1` |
| `src/workspace/module_tree/hierarchy_tree.cpp:120` | `%1 has no declaration/driver; using its first load` |
| `src/workspace/module_tree/hierarchy_tree.cpp:125` | `%1 has no declaration, driver or load position` |
| `src/workspace/module_tree/hierarchy_tree.cpp:135` | `no identifier under the cursor` |
| `src/workspace/module_tree/hierarchy_tree.cpp:146` | `no net named '%1' in the elaborated design` |
| `src/workspace/module_tree/hierarchy_tree.cpp:185` | `net %1` |
| `src/workspace/module_tree/hierarchy_tree.cpp:187` | `%1 nets named '%2'; chose %3` |
| `src/workspace/module_tree/hierarchy_tree.cpp:285` | `Nets without a reported instance` |
| `src/workspace/module_tree/hierarchy_tree.cpp:286` | `%1 net(s)` |
| `src/workspace/module_tree/hierarchy_tree.cpp:309` | `The elaboration reported no instances` |
| `src/workspace/module_tree/hierarchy_tree.cpp:310` | `No hierarchy yet` |
| `src/workspace/module_tree/hierarchy_tree.cpp:311` | `Run Refresh once a project is ` |
| `src/workspace/module_tree/hierarchy_types.cpp:42` | `hierarchy: %1` |
| `src/workspace/module_tree/hierarchy_types.cpp:45` | `no hierarchy yet` |
| `src/workspace/module_tree/hierarchy_types.cpp:47` | `%1 · %2 file(s) · %3 instance(s) · %4 net(s) · %5 ms · %6 KiB peak` |
| `src/workspace/module_tree/hierarchy_types.cpp:55` | ` · ⚠ ` |
| `src/workspace/module_tree/module_tree_integration.cpp:124` | `hierarchy: %1` |
| `src/workspace/module_tree/module_tree_integration.cpp:134` | `hierarchy: retrying after the project ` |
| `src/workspace/module_tree/module_tree_integration.cpp:170` | `No hierarchy yet: open a project and press ` |
| `src/workspace/module_tree/module_tree_integration.cpp:243` | `Module Tree` |
| `src/workspace/module_tree/module_tree_integration.cpp:248` | `Empty state: no elaborated project yet. Open a project and press ` |
| `src/workspace/module_tree/module_tree_panel.cpp:44` | `%1 · no file:line reported` |
| `src/workspace/module_tree/module_tree_panel.cpp:79` | `Re-elaborate the whole project` |
| `src/workspace/module_tree/module_tree_panel.cpp:83` | `Jump to source` |
| `src/workspace/module_tree/module_tree_panel.cpp:85` | `Put the cursor on a signal in the editor, then jump to the construct that drives it` |
| `src/workspace/module_tree/module_tree_panel.cpp:89` | `Jump to target` |
| `src/workspace/module_tree/module_tree_panel.cpp:91` | `Put the cursor on a signal in the editor, then jump to the construct that reads it` |
| `src/workspace/module_tree/module_tree_panel.cpp:95` | `Filter instance / net / file…` |
| `src/workspace/module_tree/module_tree_panel.cpp:103` | `Instance / Net` |
| `src/workspace/module_tree/module_tree_panel.cpp:127` | `nothing to refresh: no hierarchy client` |
| `src/workspace/module_tree/module_tree_panel.cpp:137` | `No hierarchy yet: open a project and press Refresh.` |
| `src/workspace/module_tree/module_tree_panel.cpp:147` | `No hierarchy yet: open a project and press Refresh.` |
| `src/workspace/module_tree/module_tree_panel.cpp:157` | `Hierarchy ready — double-click an instance, a net or a driver/load.` |
| `src/workspace/module_tree/module_tree_panel.cpp:255` | `%1 → %2` |
| `src/workspace/module_tree/module_tree_panel.cpp:258` | `%1: no jump target is registered (the editor lane is not ` |
| `src/workspace/module_tree/module_tree_panel.cpp:263` | `%1: the editor rejected the jump to %2` |
| `src/workspace/module_tree/module_tree_panel.cpp:267` | `%1: the jump request (%2) was refused` |
| `src/workspace/module_tree/module_tree_panel.cpp:285` | `%1 has no file:line in the elaborated design` |
| `src/workspace/module_tree/module_tree_panel.cpp:295` | `put the cursor on a signal name in the editor first` |
| `src/workspace/module_tree/module_tree_panel.cpp:309` | `%1 · %2` |
| `src/workspace/module_tree/module_tree_panel.cpp:316` | `put the cursor on a signal name in the editor first` |
| `src/workspace/module_tree/module_tree_panel.cpp:330` | `%1 · %2` |
| `src/workspace/module_tree/module_tree_selftest.cpp:139` | `port q (out)` |
| `src/workspace/module_tree/module_tree_selftest.cpp:142` | `port d (in)` |
| `src/workspace/module_tree/module_tree_selftest.cpp:144` | `assign source` |
| `src/workspace/module_tree/module_tree_selftest.cpp:149` | `port clk (in)` |
| `src/workspace/module_tree/module_tree_selftest.cpp:151` | `port clk (in)` |
| `src/workspace/module_tree/module_tree_selftest.cpp:165` | `assign source` |
| `src/workspace/module_tree/module_tree_selftest.cpp:331` | `no driver` |
| `src/workspace/module_tree/module_tree_selftest.cpp:356` | `3 instance` |
| `src/workspace/module_tree/module_tree_selftest.cpp:357` | `6 net` |
| `src/workspace/module_tree/module_tree_selftest.cpp:411` | `calls=%1 status=%2` |
| `src/workspace/module_tree/module_tree_selftest.cpp:423` | `no driver` |
| `src/workspace/module_tree/module_tree_selftest.cpp:432` | `No hierarchy` |
| `src/workspace/module_tree/module_tree_selftest.cpp:434` | `no project root: open a folder` |
| `src/workspace/module_tree/module_tree_selftest.cpp:436` | `no project root: open a folder` |
| `src/workspace/palette/command_palette.cpp:126` | `QDialog#command-palette { background: #1f2430; border: 1px solid #3b4252; }` |
| `src/workspace/palette/command_palette.cpp:437` | `%1 files · type to filter · Enter to open` |
| `src/workspace/palette/command_palette.cpp:439` | `%1 of %2 files` |
| `src/workspace/project/project_model.cpp:135` | `open a folder before importing a filelist` |
| `src/workspace/project/project_model.cpp:144` | `%1 is not a readable filelist` |
| `src/workspace/project/project_model.cpp:357` | ` · %1 filelist%2` |
| `src/workspace/project/project_model.cpp:363` | ` · %1 variable%2` |
| `src/workspace/project/project_model.cpp:368` | ` · %1 line%2 skipped` |
| `src/workspace/project/project_model.cpp:373` | ` · %1 unresolved` |
| `src/workspace/project/project_panel.cpp:73` | `Import Filelist…` |
| `src/workspace/project/project_panel.cpp:76` | `Add one or more .f filelists to the project file set` |
| `src/workspace/project/project_panel.cpp:83` | `Open a folder from the File Tree (or pass --project <dir>); the expanded ` |
| `src/workspace/project/project_panel.cpp:90` | `Filelists (0)` |
| `src/workspace/project/project_panel.cpp:97` | `Imported .f filelists; double-click to open one` |
| `src/workspace/project/project_panel.cpp:101` | `Remove Selected Filelist` |
| `src/workspace/project/project_panel.cpp:107` | `Files (0)` |
| `src/workspace/project/project_panel.cpp:113` | `Double-click a file to open it (AC-7.4)` |
| `src/workspace/project/project_panel.cpp:116` | `Include dirs (-I) (0)` |
| `src/workspace/project/project_panel.cpp:125` | `Defines (-D) (0)` |
| `src/workspace/project/project_panel.cpp:134` | `Variables (0)` |
| `src/workspace/project/project_panel.cpp:141` | `Project variable table; wins over the environment` |
| `src/workspace/project/project_panel.cpp:145` | `Edit Variables…` |
| `src/workspace/project/project_panel.cpp:209` | `No project model is attached` |
| `src/workspace/project/project_panel.cpp:217` | `Imported %1 filelist%2` |
| `src/workspace/project/project_panel.cpp:228` | `No project model is attached` |
| `src/workspace/project/project_panel.cpp:232` | `%1 is not an imported filelist` |
| `src/workspace/project/project_panel.cpp:235` | `Removed %1; the file set is being re-expanded` |
| `src/workspace/project/project_panel.cpp:270` | `%1 unresolved filelist variable(s): %2` |
| `src/workspace/project/project_panel.cpp:279` | `Import completed with %1 unresolved variable(s); the affected ` |
| `src/workspace/project/project_panel.cpp:288` | `Saved %1 variable(s); re-expanding` |
| `src/workspace/project/project_panel.cpp:297` | `Open a folder before editing variables` |
| `src/workspace/project/project_panel.cpp:311` | `No variables are used by this project yet` |
| `src/workspace/project/project_panel.cpp:321` | `Project variables updated; re-expanding` |
| `src/workspace/project/project_panel.cpp:354` | `Cannot open %1: no editor jump target is registered` |
| `src/workspace/project/project_panel.cpp:360` | `Cannot open %1: this build has no jump seam` |
| `src/workspace/project/project_panel.cpp:409` | `⚠ %1` |
| `src/workspace/project/project_panel.cpp:410` | `color: #c0392b;` |
| `src/workspace/project/project_panel.cpp:419` | `Open a folder before importing a filelist` |
| `src/workspace/project/project_panel.cpp:423` | `Import Filelist` |
| `src/workspace/project/project_panel.cpp:424` | `Verilog filelists (*.f *.F *.flist);;All files (*)` |
| `src/workspace/project/project_panel.cpp:447` | `Filelists (%1)` |
| `src/workspace/project/project_panel.cpp:456` | `Files (%1)` |
| `src/workspace/project/project_panel.cpp:465` | `Include dirs (-I) (%1)` |
| `src/workspace/project/project_panel.cpp:470` | `Defines (-D) (%1)` |
| `src/workspace/project/project_panel.cpp:475` | `Variables (%1)` |
| `src/workspace/project/project_selftest.cpp:248` | `11 files` |
| `src/workspace/project/project_selftest.cpp:274` | `11 files` |
| `src/workspace/project/project_selftest.cpp:278` | `11 files` |
| `src/workspace/project/project_selftest.cpp:382` | `include directory` |
| `src/workspace/project/project_selftest.cpp:383` | `unsupported filelist token` |
| `src/workspace/project/project_selftest.cpp:435` | `nested -f cycle` |
| `src/workspace/project/project_selftest.cpp:436` | `cycle_a.f -> ` |
| `src/workspace/project/project_selftest.cpp:496` | `this is not json` |
| `src/workspace/project/project_state.cpp:127` | `%1 is not a project state file: %2` |
| `src/workspace/project/project_state.cpp:132` | `%1 carries no project root` |
| `src/workspace/project/project_variables_dialog.cpp:20` | `Filelist variables` |
| `src/workspace/project/project_variables_dialog.cpp:27` | `These variables are used by the imported .f filelists but are not ` |
| `src/workspace/project/project_variables_dialog.cpp:39` | `value for %1` |
| `src/workspace/search/global_search.cpp:223` | `the Rust search service is not linked in this build; the Qt side does not ` |
| `src/workspace/search/search_jump_target.cpp:75` | `the active editor pane is not a KTextEditor view ` |
| `src/workspace/search/search_selftest.cpp:1154` | `Internal: slow-storage lifecycle probe (child of ` |
| `src/workspace/settings/settings_panel.cpp:163` | `cannot create the temporary key file` |
| `src/workspace/settings/settings_panel.cpp:171` | `the created file is not owner-only` |
| `src/workspace/settings/settings_panel.cpp:190` | `short write` |
| `src/workspace/settings/settings_panel.cpp:217` | `the key file is not owner-only after the atomic replace` |
| `src/workspace/settings/settings_panel.cpp:538` | `OpenAI-compatible (/v1/completions)` |
| `src/workspace/settings/settings_panel.cpp:540` | `llama.cpp (/infill)` |
| `src/workspace/settings/settings_panel.cpp:639` | `Settings loaded from %1 with %2 warning(s); the built-in ` |
| `src/workspace/settings/settings_panel.cpp:666` | `Theme '%1' is invalid; the default theme is ` |
| `src/workspace/settings/settings_panel.cpp:734` | `Shortcut for '%1' set to %2 and stored by the Rust ` |
| `src/workspace/settings/settings_panel.cpp:774` | `Preview — press Apply or OK to store it, ` |
| `src/workspace/settings/settings_panel.cpp:790` | `the Rust settings store rejected ` |
| `src/workspace/settings/settings_panel.cpp:793` | `The settings change was applied but could not ` |
| `src/workspace/settings/settings_panel.cpp:900` | `%1 configuration problem(s) — the built-in defaults were kept ` |
| `src/workspace/settings/settings_selftest.cpp:124` | `Re-run Project Lint` |
| `src/workspace/settings/settings_selftest.cpp:132` | `Fill in the Middle` |
| `src/workspace/settings/settings_selftest.cpp:204` | `%1 theme(s)` |
| `src/workspace/settings/settings_selftest.cpp:212` | `lint.rerun / fim.trigger must be listed` |
| `src/workspace/settings/settings_selftest.cpp:226` | `theme = "%1"` |
| `src/workspace/settings/settings_selftest.cpp:308` | `theme = "%1"` |
| `src/workspace/settings/settings_selftest.cpp:490` | `<no tab widget>` |
| `src/workspace/settings/settings_selftest.cpp:532` | `"tools.lsp_backend" = "slang"` |
| `src/workspace/settings/settings_selftest.cpp:533` | `"tools.lint_engine" = "verible"` |
| `src/workspace/settings/settings_selftest.cpp:555` | `"fim.enabled" = "true"` |
| `src/workspace/settings/settings_selftest.cpp:556` | `"fim.protocol" = "llama_cpp"` |
| `src/workspace/settings/settings_selftest.cpp:558` | `"fim.endpoint" = "http://127.0.0.1:8080"` |
| `src/workspace/settings/settings_selftest.cpp:559` | `"fim.timeout_ms" = "2500"` |
| `src/workspace/settings/settings_selftest.cpp:560` | `"fim.ghost_text" = "true"` |
| `src/workspace/settings/settings_selftest.cpp:586` | `mode=%1 umask=%2` |
