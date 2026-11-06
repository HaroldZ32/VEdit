# T21 — 215 条 UI 候选的可核对块（AC-4.6）

每个块 = 一个源文件。列：triage id、源字符串、中文译文、代码中的**落点**（包装调用 file:line + 翻译上下文）、.ts 状态。

- UI 候选总数：**215**（期望 215）

- 未进 .ts 的译项：**0**；无包装落点的译项：**0**


## `src/workspace/project/project_model.cpp`（9 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T002 | ` (expanding…)` | （展开中…） | src/workspace/project/project_model.cpp:377 | VEdit | OK |
| T003 | ` (live)` | （实时） | src/workspace/project/project_model.cpp:383 | VEdit | OK |
| T004 | ` (refresh failed)` | （刷新失败） | src/workspace/project/project_model.cpp:379 | VEdit | OK |
| T005 | ` (restored from last session)` | （已从上次会话恢复） | src/workspace/project/project_model.cpp:381 | VEdit | OK |
| T125 | `No project open` | 未打开工程 | src/workspace/project/project_model.cpp:348<br>src/workspace/project/project_selftest.cpp:234 | VEdit | OK |
| T360 | `no project is open` | 未打开工程 | src/workspace/project/project_model.cpp:126 | VEdit | OK |
| T441 | `the project job was rejected by the service` | 项目作业被服务拒绝 | src/workspace/project/project_model.cpp:529 | VEdit | OK |
| T442 | `the project service did not answer in %1 ms` | 项目服务未在 %1 毫秒内响应 | src/workspace/project/project_model.cpp:503 | VEdit | OK |
| T443 | `the project service failed` | 项目服务失败 | src/workspace/project/project_model.cpp:552 | VEdit | OK |

## `src/app/app_shell.cpp`（25 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T014 | `&About VEdit` | 关于 VEdit(&A) | src/app/app_shell.cpp:451 | class tr() | OK |
| T015 | `&Close Pane` | 关闭窗格(&C) | src/app/app_shell.cpp:397 | class tr() | OK |
| T016 | `&Focus Next Pane` | 聚焦下一个窗格(&F) | src/app/app_shell.cpp:407 | class tr() | OK |
| T018 | `&Mixed Split (demo)` | 混合分屏(&M)（演示） | src/app/app_shell.cpp:409 | class tr() | OK |
| T019 | `&New Empty Layout` | 新建空布局(&N) | src/app/app_shell.cpp:385 | class tr() | OK |
| T020 | `&Reset Layout` | 重置布局(&R) | src/app/app_shell.cpp:411 | class tr() | OK |
| T021 | `&Save Session Now` | 立即保存会话(&S) | src/app/app_shell.cpp:393 | class tr() | OK |
| T032 | `About VEdit` | 关于 VEdit | src/app/app_shell.cpp:517 | VEdit | OK |
| T068 | `Empty state: no project open. This panel stays usable and empty.` | 空状态：未打开工程。此面板保持可用但为空。 | src/app/app_shell.cpp:343 | VEdit | OK |
| T069 | `Empty state: nothing to show for this project yet.` | 空状态：此工程暂无可显示内容。 | src/app/app_shell.cpp:344 | VEdit | OK |
| T140 | `Project: %1` | 项目：%1 | src/app/app_shell.cpp:768 | class tr() | OK |
| T173 | `Save failed: %1` | 保存失败：%1 | src/app/app_shell.cpp:86<br>src/app/app_shell.cpp:87<br>src/app/app_shell.cpp:667<br>src/app/app_shell.cpp:855<br>src/app/app_shell.cpp:856<br>src/app/app_shell.cpp:939<br>src/app/app_shell.cpp:940 | VEdit, class tr() | OK |
| T178 | `Session saved (%1)` | 会话已保存（%1） | src/app/app_shell.cpp:483 | class tr() | OK |
| T182 | `Source Control` | 版本控制 | src/app/app_shell.cpp:349 | VEdit | OK |
| T184 | `Split Pane &Horizontally` | 水平分屏(&H) | src/app/app_shell.cpp:403 | class tr() | OK |
| T185 | `Split Pane &Vertically` | 垂直分屏(&V) | src/app/app_shell.cpp:405 | class tr() | OK |
| T205 | `Theme: %1` | 主题：%1 | src/app/app_shell.cpp:772 | class tr() | OK |
| T208 | `Unsaved changes` | 未保存的更改 | src/app/app_shell.cpp:812<br>src/app/app_shell.cpp:974 | class tr() | OK |
| T281 | `dock layout could not be restored (KTextEditor/version mismatch?)` | 无法恢复停靠布局（KTextEditor/版本不匹配？） | src/app/app_shell.cpp:616 | VEdit | OK |
| T285 | `editor layout: %1` | 编辑器布局：%1 | src/app/app_shell.cpp:622 | VEdit | OK |
| T349 | `no active editor pane` | 没有活动的编辑器窗格 | src/app/app_shell.cpp:674 | VEdit | OK |
| T403 | `session mentions unknown panel '%1'` | 会话提到未知面板“%1” | src/app/app_shell.cpp:553 | VEdit | OK |
| T435 | `the editor provider refused the save` | 编辑器 provider 拒绝了保存 | src/app/app_shell.cpp:680 | VEdit | OK |
| T445 | `theme fallback: %1` | 主题回退：%1 | src/app/app_shell.cpp:575 | VEdit | OK |
| T462 | `window geometry could not be restored` | 无法恢复窗口几何 | src/app/app_shell.cpp:633 | VEdit | OK |

## `src/workspace/palette/palette_integration.cpp`（1 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T017 | `&Go to File…` | 转到文件(&G)… | src/workspace/palette/palette_integration.cpp:92 | VEdit | OK |

## `src/workspace/filetree/file_ops.cpp`（13 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T022 | `'%1' is a reserved name` | “%1”是保留名称 | src/workspace/filetree/file_ops.cpp:70 | VEdit | OK |
| T024 | `'%1' no longer exists` | “%1”已不存在 | src/workspace/filetree/file_ops.cpp:146<br>src/workspace/filetree/file_ops.cpp:181 | VEdit | OK |
| T031 | `A file or folder named %1 already exists` | 名为 %1 的文件或文件夹已存在 | src/workspace/filetree/file_ops.cpp:97<br>src/workspace/filetree/file_ops.cpp:124<br>src/workspace/filetree/file_ops.cpp:155 | VEdit | OK |
| T050 | `Could not create file %1` | 无法创建文件 %1 | src/workspace/filetree/file_ops.cpp:108 | VEdit | OK |
| T051 | `Could not create folder %1` | 无法创建文件夹 %1 | src/workspace/filetree/file_ops.cpp:131 | VEdit | OK |
| T052 | `Could not delete %1` | 无法删除 %1 | src/workspace/filetree/file_ops.cpp:203 | VEdit | OK |
| T053 | `Could not rename %1 to %2` | 无法把 %1 重命名为 %2 | src/workspace/filetree/file_ops.cpp:169 | VEdit | OK |
| T110 | `Name is longer than the 255-byte filesystem limit` | 名称超过文件系统的 255 字节限制 | src/workspace/filetree/file_ops.cpp:79 | VEdit | OK |
| T111 | `Name must not be empty` | 名称不能为空 | src/workspace/filetree/file_ops.cpp:67 | VEdit | OK |
| T112 | `Name must not contain a NUL character` | 名称不能包含 NUL 字符 | src/workspace/filetree/file_ops.cpp:76 | VEdit | OK |
| T113 | `Name must not contain a path separator` | 名称不能包含路径分隔符 | src/workspace/filetree/file_ops.cpp:73 | VEdit | OK |
| T437 | `the folder '%1' is not writable` | 文件夹“%1”不可写 | src/workspace/filetree/file_ops.cpp:34 | VEdit | OK |
| T438 | `the folder is not empty` | 文件夹不为空 | src/workspace/filetree/file_ops.cpp:195 | VEdit | OK |

## `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp`（2 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T029 | `: 0 diagnostic(s)` | ：0 条诊断 | src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:181 | VEdit | OK |
| T366 | `no temporary directory` | 没有临时目录 | src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:103 | VEdit | OK |

## `src/app/main.cpp`（10 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T036 | `Breeze Dark` | —（不译，见 11 报告） | — | — | OK |
| T045 | `Capture split-state screenshots into this directory and exit.` | 把分屏状态截图保存到该目录后退出。 | src/app/main.cpp:1403 | VEdit | OK |
| T066 | `Directory for session.json and shortcuts.json.` | session.json 与 shortcuts.json 的目录。 | src/app/main.cpp:1390 | VEdit | OK |
| T097 | `Ignore an existing session file at startup.` | 启动时忽略已有的会话文件。 | src/app/main.cpp:1406 | VEdit | OK |
| T098 | `KSyntaxHighlighting theme name.` | KSyntaxHighlighting 主题名称。 | src/app/main.cpp:1394 | VEdit | OK |
| T106 | `Keep the window for this many ms.` | 让窗口保持该毫秒数。 | src/app/main.cpp:1407 | VEdit | OK |
| T172 | `Save failed` | 保存失败 | src/app/main.cpp:941<br>src/app/main.cpp:944 | VEdit | OK |
| T209 | `Use &tree-sitter parsing` | 使用 &tree-sitter 解析 | src/app/main.cpp:1515 | VEdit | OK |
| T210 | `VEdit application shell (Qt6/KTextEditor rewrite).` | VEdit 应用外壳（Qt6/KTextEditor 重写版）。 | src/app/main.cpp:1383 | VEdit | OK |
| T399 | `search panel from a registered factory` | 来自已注册工厂的搜索面板 | src/app/main.cpp:687 | VEdit | OK |

## `src/app/theme_manager.cpp`（2 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T037 | `Breeze Light` | —（不译，见 11 报告） | — | — | OK |
| T457 | `unknown theme '%1' (or not loaded by KSyntaxHighlighting)` | 未知主题“%1”（或未被 KSyntaxHighlighting 加载） | src/app/theme_manager.cpp:56 | VEdit | OK |

## `src/workspace/filetree/file_tree_widget.cpp`（14 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T040 | `Cannot delete the folder that is open. Close it first.` | 无法删除已打开的文件夹。请先关闭它。 | src/workspace/filetree/file_tree_widget.cpp:313<br>src/workspace/filetree/file_tree_widget.cpp:498 | VEdit | OK |
| T043 | `Cannot read '%1': %2` | 无法读取“%1”：%2 | src/workspace/filetree/file_tree_widget.cpp:152 | VEdit | OK |
| T048 | `Collapse every expanded folder` | 折叠所有已展开的文件夹 | src/workspace/filetree/file_tree_widget.cpp:78 | VEdit | OK |
| T054 | `Create a file here (Ctrl+N)` | 在此创建文件 (Ctrl+N) | src/workspace/filetree/file_tree_widget.cpp:58 | VEdit | OK |
| T055 | `Create a folder here (Ctrl+Shift+N)` | 在此创建文件夹 (Ctrl+Shift+N) | src/workspace/filetree/file_tree_widget.cpp:62 | VEdit | OK |
| T058 | `Created file '%1' (%2)` | 已创建文件“%1”（%2） | src/workspace/filetree/file_tree_widget.cpp:235 | VEdit | OK |
| T059 | `Created folder '%1'` | 已创建文件夹“%1” | src/workspace/filetree/file_tree_widget.cpp:267 | VEdit | OK |
| T061 | `Delete the selection (Del)` | 删除所选内容 (Del) | src/workspace/filetree/file_tree_widget.cpp:70 | VEdit | OK |
| T131 | `Open a folder` | 打开文件夹 | src/workspace/filetree/file_tree_widget.cpp:54 | VEdit | OK |
| T132 | `Open a folder first.` | 请先打开文件夹。 | src/workspace/filetree/file_tree_widget.cpp:218<br>src/workspace/filetree/file_tree_widget.cpp:220<br>src/workspace/filetree/file_tree_widget.cpp:250<br>src/workspace/filetree/file_tree_widget.cpp:252<br>src/workspace/filetree/file_tree_widget.cpp:437<br>src/workspace/filetree/file_tree_widget.cpp:456 | VEdit | OK |
| T145 | `Re-read the folder from disk (F5)` | 从磁盘重新读取文件夹 (F5) | src/workspace/filetree/file_tree_widget.cpp:74 | VEdit | OK |
| T148 | `Rename the selection (F2)` | 重命名所选内容 (F2) | src/workspace/filetree/file_tree_widget.cpp:66 | VEdit | OK |
| T149 | `Renamed '%1' to '%2'` | 已将“%1”重命名为“%2” | src/workspace/filetree/file_tree_widget.cpp:296 | VEdit | OK |
| T176 | `Select a file or folder first.` | 请先选择文件或文件夹。 | src/workspace/filetree/file_tree_widget.cpp:277<br>src/workspace/filetree/file_tree_widget.cpp:279<br>src/workspace/filetree/file_tree_widget.cpp:306<br>src/workspace/filetree/file_tree_widget.cpp:308<br>src/workspace/filetree/file_tree_widget.cpp:475<br>src/workspace/filetree/file_tree_widget.cpp:494 | VEdit | OK |

## `src/workspace/filetree/filetree_integration.cpp`（3 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T041 | `Cannot open %1` | 无法打开 %1 | src/workspace/filetree/filetree_integration.cpp:45<br>src/workspace/palette/palette_integration.cpp:190 | VEdit | OK |
| T042 | `Cannot open %1: no active editor pane` | 无法打开 %1：没有活动的编辑器窗格 | src/workspace/filetree/filetree_integration.cpp:41<br>src/workspace/palette/palette_integration.cpp:184 | VEdit | OK |
| T375 | `openFileInEditor: the application shell is not part of this build` | openFileInEditor：此构建不包含应用外壳 | src/workspace/filetree/filetree_integration.cpp:53 | VEdit | OK |

## `src/workspace/settings/settings_panel.cpp`（13 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T046 | `Changes discarded; nothing was stored.` | 更改已丢弃；未存储任何内容。 | src/workspace/settings/settings_panel.cpp:922 | VEdit | OK |
| T049 | `Conflicting shortcuts: %1` | 快捷键冲突：%1 | src/workspace/settings/settings_panel.cpp:1499 | VEdit | OK |
| T109 | `Loading settings from %1 …` | 正在从 %1 加载设置… | src/workspace/settings/settings_panel.cpp:650 | VEdit | OK |
| T118 | `No configuration problems reported by the Rust store.` | Rust 存储未报告配置问题。 | src/workspace/settings/settings_panel.cpp:934 | VEdit | OK |
| T119 | `No conflicting shortcuts.` | 没有冲突的快捷键。 | src/workspace/settings/settings_panel.cpp:1495 | VEdit | OK |
| T175 | `Select a command in the table first.` | 请先在表中选择一个命令。 | src/workspace/settings/settings_panel.cpp:795 | VEdit | OK |
| T179 | `Settings loaded from %1` | 已从 %1 加载设置 | src/workspace/settings/settings_panel.cpp:674 | VEdit | OK |
| T180 | `Settings unavailable; showing the built-in defaults.` | 设置不可用；显示内置默认值。 | src/workspace/settings/settings_panel.cpp:656 | VEdit | OK |
| T181 | `Shortcut for '%1' cleared (default restored).` | 已清除“%1”的快捷键（恢复默认）。 | src/workspace/settings/settings_panel.cpp:786 | VEdit | OK |
| T187 | `Stored by the Rust service in %1` | 已由 Rust 服务存储到 %1 | src/workspace/settings/settings_panel.cpp:834<br>src/workspace/settings/settings_panel.cpp:898 | VEdit | OK |
| T203 | `Theme '%1' applied and stored by the Rust service.` | 主题“%1”已应用并由 Rust 服务存储。 | src/workspace/settings/settings_panel.cpp:749 | VEdit | OK |
| T204 | `Theme '%1' could not be applied.` | 无法应用主题“%1”。 | src/workspace/settings/settings_panel.cpp:742 | VEdit | OK |
| T407 | `settings could not be loaded` | 无法加载设置 | src/workspace/settings/settings_panel.cpp:653 | VEdit | OK |

## `src/app/editor_area.cpp`（9 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T047 | `Close Pane` | 关闭窗格 | src/app/editor_area.cpp:215 | VEdit | OK |
| T120 | `No editor provider connected.` | 未连接编辑器 provider。 | src/app/editor_area.cpp:367 | VEdit | OK |
| T322 | `layout has no root object` | 布局没有根对象 | src/app/editor_area.cpp:1653 | VEdit | OK |
| T323 | `layout parse failed` | 布局解析失败 | src/app/editor_area.cpp:1673 | VEdit | OK |
| T324 | `leaf node without pane id` | 叶子节点缺少窗格 id | src/app/editor_area.cpp:1562 | VEdit | OK |
| T373 | `open: the pane has no editor provider` | 打开：窗格没有编辑器 provider | src/app/editor_area.cpp:766 | VEdit | OK |
| T416 | `split node needs two object children` | 分割节点需要两个对象子节点 | src/app/editor_area.cpp:1609 | VEdit | OK |
| T417 | `split node without a valid orientation` | 分割节点缺少有效方向 | src/app/editor_area.cpp:1601 | VEdit | OK |
| T456 | `unknown layout node type '%1'` | 未知布局节点类型“%1” | src/app/editor_area.cpp:1629 | VEdit | OK |

## `src/workspace/project/project_panel.cpp`（2 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T063 | `Diagnostics (%1)` | 诊断（%1） | src/workspace/project/project_panel.cpp:484 | VEdit | OK |
| T064 | `Diagnostics (0)` | 诊断 (0) | src/workspace/project/project_panel.cpp:152 | VEdit | OK |

## `src/workspace/diagnostics/toolchain_ux_selftest.cpp`（1 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T065 | `Diagnostics unavailable` | 诊断不可用 | src/workspace/diagnostics/toolchain_ux_selftest.cpp:124<br>src/workspace/diagnostics/diagnostics_selftest.cpp:534<br>src/workspace/diagnostics/toolchain_selftest.cpp:152<br>src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:160 | VEdit | OK |

## `src/app/panel_registry.cpp`（1 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T067 | `Empty state: no project / no data.` | 空状态：无工程 / 无数据。 | src/app/panel_registry.cpp:49 | VEdit | OK |

## `src/workspace/filetree/file_kind.cpp`（1 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T095 | `File list` | 文件列表 | src/workspace/filetree/file_kind.cpp:54 | VEdit | OK |

## `src/workspace/palette/command_palette.cpp`（5 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T096 | `Go to file…` | 转到文件… | src/workspace/palette/command_palette.cpp:148 | VEdit | OK |
| T121 | `No files indexed for this project yet.` | 此工程尚未索引文件。 | src/workspace/palette/command_palette.cpp:420 | VEdit | OK |
| T123 | `No matching files` | 无匹配文件 | src/workspace/palette/palette_integration_selftest.cpp:10<br>src/workspace/palette/command_palette.cpp:321<br>src/workspace/palette/command_palette.h:69<br>src/workspace/palette/palette_selftest.cpp:5 | VEdit | OK |
| T124 | `No project` | 无工程 | src/workspace/palette/command_palette.cpp:433<br>src/workspace/palette/palette_selftest.cpp:227 | VEdit | OK |
| T126 | `No project open — open a project root to jump between files.` | 未打开工程——打开工程根目录即可在文件间跳转。 | src/workspace/palette/command_palette.cpp:418 | VEdit | OK |

## `src/editor/kernel/ktexteditor_provider.cpp`（3 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T101 | `KTextEditor (edit-core)` | KTextEditor（编辑内核） | src/editor/kernel/ktexteditor_provider.cpp:78 | VEdit | OK |
| T252 | `cannot open %1: not a regular file` | 无法打开 %1：不是常规文件 | src/app/app_shell.cpp:212<br>src/app/editor_provider.h:125<br>src/editor/kernel/ktexteditor_provider.cpp:156<br>src/editor/kernel/document_io.cpp:29 | VEdit | OK |
| T395 | `save: the document has no file path yet` | 保存：文档尚无文件路径 | src/editor/kernel/ktexteditor_provider.cpp:318 | VEdit | OK |

## `src/editor/kernel/highlight_mode.cpp`（4 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T102 | `KTextEditor::Document::setHighlightingMode('%1') failed` | KTextEditor::Document::setHighlightingMode('%1') 失败 | src/editor/kernel/highlight_mode.cpp:131 | VEdit | OK |
| T353 | `no document to highlight` | 没有要高亮的文档 | src/editor/kernel/highlight_mode.cpp:103 | VEdit | OK |
| T365 | `no syntax definition for %1` | %1 没有语法定义 | src/editor/kernel/highlight_mode.cpp:110 | VEdit | OK |
| T434 | `the document reports highlighting mode '%1' instead of '%2'` | 文档报告的高亮模式为“%1”，而非“%2” | src/editor/kernel/highlight_mode.cpp:138 | VEdit | OK |

## `src/editor/kernel/document_io.cpp`（8 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T103 | `KTextEditor::Document::setText() failed` | KTextEditor::Document::setText() 失败 | src/editor/kernel/document_io.cpp:110 | VEdit | OK |
| T104 | `KTextEditor::Document::setText() failed for %1` | KTextEditor::Document::setText() 对 %1 失败 | src/editor/kernel/document_io.cpp:49 | VEdit | OK |
| T105 | `KTextEditor::Editor::createDocument() returned null` | KTextEditor::Editor::createDocument() 返回空 | src/editor/kernel/document_io.cpp:80 | VEdit | OK |
| T251 | `cannot open %1: it looks like a binary file` | 无法打开 %1：它看起来是二进制文件 | src/editor/kernel/document_io.cpp:43 | VEdit | OK |
| T255 | `cannot read %1: %2` | 无法读取 %1：%2 | src/workspace/project/project_state.cpp:123<br>src/app/editor_provider.h:132<br>src/editor/kernel/document_io.cpp:36 | VEdit | OK |
| T374 | `openFile: no document` | openFile：没有文档 | src/editor/kernel/document_io.cpp:22 | VEdit | OK |
| T397 | `saveAtomic: the document has no file path` | saveAtomic：文档没有文件路径 | src/editor/kernel/document_io.cpp:171 | VEdit | OK |
| T406 | `setText: no document` | setText：没有文档 | src/editor/kernel/document_io.cpp:104 | VEdit | OK |

## `src/workspace/diagnostics/diagnostics_controller.cpp`（9 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T108 | `LSP backend is not configured (no workspace root)` | LSP 后端未配置（无工作区根目录） | src/workspace/diagnostics/diagnostics_controller.cpp:287 | VEdit | OK |
| T343 | `no LSP backend in this build` | 此构建没有 LSP 后端 | src/workspace/diagnostics/ffi_backends.cpp:798<br>src/workspace/diagnostics/diagnostics_controller.cpp:277 | VEdit | OK |
| T346 | `no SLANG backend` | 没有 SLANG 后端 | src/workspace/diagnostics/diagnostics_controller.cpp:412 | VEdit | OK |
| T347 | `no SLANG backend in this build` | 此构建没有 SLANG 后端 | src/workspace/diagnostics/diagnostics_controller.cpp:313 | VEdit | OK |
| T355 | `no effective file set` | 没有有效文件集 | src/workspace/diagnostics/diagnostics_controller.cpp:202 | VEdit | OK |
| T357 | `no navigation request in flight` | 没有进行中的导航请求 | src/workspace/diagnostics/diagnostics_controller.cpp:618 | VEdit | OK |
| T388 | `reparsing %1 file(s)` | 正在重新解析 %1 个文件 | src/workspace/diagnostics/diagnostics_controller.cpp:208 | VEdit | OK |
| T404 | `session started over %1 file(s)` | 会话已在 %1 个文件上启动 | src/workspace/diagnostics/diagnostics_controller.cpp:295 | VEdit | OK |
| T424 | `the LSP session did not start` | LSP 会话未启动 | src/workspace/diagnostics/diagnostics_controller.cpp:393 | VEdit | OK |

## `src/workspace/vcs/vcs_panel.cpp`（2 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T117 | `No VCS backend available.` | 没有可用的 VCS 后端。 | src/workspace/vcs/vcs_panel.cpp:134 | VEdit | OK |
| T465 | `· %1 changed file(s)` | · %1 个已更改文件 | src/workspace/vcs/vcs_panel.cpp:110 | VEdit | OK |

## `src/workspace/vcs/vcs_selftest.cpp`（2 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T128 | `No repository` | 无仓库 | src/workspace/vcs/vcs_selftest.cpp:109 | VEdit | OK |
| T369 | `not a Git or Subversion working tree` | 不是 Git 或 Subversion 工作树 | src/workspace/vcs/vcs_selftest.cpp:99<br>src/workspace/vcs/vcs_selftest.cpp:112<br>src/workspace/vcs/vcs_panel.cpp:124 | VEdit | OK |

## `src/workspace/search/search_jump_target.cpp`（3 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T135 | `Opened %1 line %2` | 已打开 %1 第 %2 行 | src/workspace/search/search_jump_target.cpp:120 | VEdit | OK |
| T249 | `cannot open %1` | 无法打开 %1 | src/workspace/search/search_jump_target.cpp:83<br>src/app/app_shell.cpp:237 | VEdit | OK |
| T405 | `setCursorPosition(%1:%2) failed` | setCursorPosition(%1:%2) 失败 | src/workspace/search/search_jump_target.cpp:102 | VEdit | OK |

## `src/workspace/filetree/file_tree_model.cpp`（3 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T201 | `The folder '%1' cannot be read (permission denied).` | 无法读取文件夹“%1”（权限被拒绝）。 | src/workspace/filetree/file_tree_model.cpp:91<br>src/workspace/filetree/file_tree_model.cpp:424 | VEdit | OK |
| T202 | `The folder '%1' does not exist.` | 文件夹“%1”不存在。 | src/workspace/filetree/file_tree_model.cpp:79 | VEdit | OK |
| T381 | `permission denied: %1` | 权限被拒绝：%1 | src/workspace/filetree/file_tree_model.cpp:421 | VEdit | OK |

## `src/workspace/project/project_state.cpp`（4 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T238 | `cannot commit %1: %2` | 无法提交 %1：%2 | src/workspace/project/project_state.cpp:169 | VEdit | OK |
| T241 | `cannot create %1` | 无法创建 %1 | src/workspace/project/project_state.cpp:155<br>src/workspace/palette/palette_bench.cpp:101 | VEdit | OK |
| T260 | `cannot write %1: %2` | 无法写入 %1：%2 | src/workspace/project/project_state.cpp:162<br>src/app/editor_provider.h:259 | VEdit | OK |
| T364 | `no state directory is available` | 没有可用的状态目录 | src/workspace/project/project_state.cpp:116<br>src/workspace/project/project_state.cpp:149 | VEdit | OK |

## `src/app/session_store.cpp`（5 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T239 | `cannot commit session %1` | 无法提交会话 %1 | src/app/session_store.cpp:261 | VEdit | OK |
| T244 | `cannot create session directory %1` | 无法创建会话目录 %1 | src/app/session_store.cpp:247 | VEdit | OK |
| T256 | `cannot read session %1` | 无法读取会话 %1 | src/app/session_store.cpp:227 | VEdit | OK |
| T261 | `cannot write session %1` | 无法写入会话 %1 | src/app/session_store.cpp:254 | VEdit | OK |
| T320 | `invalid session %1: %2` | 无效会话 %1：%2 | src/app/session_store.cpp:235 | VEdit | OK |

## `src/app/command_registry.cpp`（6 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T240 | `cannot commit shortcut store %1` | 无法提交快捷键存储 %1 | src/app/command_registry.cpp:107 | VEdit | OK |
| T245 | `cannot create shortcut store directory %1` | 无法创建快捷键存储目录 %1 | src/app/command_registry.cpp:85 | VEdit | OK |
| T257 | `cannot read shortcut store %1` | 无法读取快捷键存储 %1 | src/app/command_registry.cpp:35 | VEdit | OK |
| T262 | `cannot write shortcut store %1` | 无法写入快捷键存储 %1 | src/app/command_registry.cpp:100 | VEdit | OK |
| T321 | `invalid shortcut store %1: %2` | 无效快捷键存储 %1：%2 | src/app/command_registry.cpp:43 | VEdit | OK |
| T363 | `no shortcut store configured` | 未配置快捷键存储 | src/app/command_registry.cpp:254 | VEdit | OK |

## `src/workspace/search/global_search.cpp`（4 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T246 | `cannot create the Rust search service` | 无法创建 Rust 搜索服务 | src/workspace/search/global_search.cpp:145 | VEdit | OK |
| T400 | `search root does not exist or is not a directory: %1` | 搜索根目录不存在或不是目录：%1 | src/workspace/search/global_search.cpp:129 | VEdit | OK |
| T401 | `search root is not readable: %1` | 搜索根目录不可读：%1 | src/workspace/search/global_search.cpp:134 | VEdit | OK |
| T426 | `the Rust search service did not report a reason` | Rust 搜索服务未报告原因 | src/workspace/search/global_search.cpp:28 | VEdit | OK |

## `src/editor/kernel/atomic_save.cpp`（5 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T248 | `cannot create the temporary file %1: %2` | 无法创建临时文件 %1：%2 | src/editor/kernel/atomic_save.cpp:94 | VEdit | OK |
| T253 | `cannot open the directory for fsync: %1` | 无法打开目录以执行 fsync：%1 | src/editor/kernel/atomic_save.cpp:154 | VEdit | OK |
| T258 | `cannot save %1: directory %2 does not exist` | 无法保存 %1：目录 %2 不存在 | src/editor/kernel/atomic_save.cpp:69 | VEdit | OK |
| T259 | `cannot save %1: directory %2 is not writable` | 无法保存 %1：目录 %2 不可写 | src/editor/kernel/atomic_save.cpp:73 | VEdit | OK |
| T277 | `directory fsync not supported here (%1); the rename is complete` | 此处不支持目录 fsync（%1）；重命名已完成 | src/editor/kernel/atomic_save.cpp:143 | VEdit | OK |

## `src/syntax/folding/ksyntax_fold_source.cpp`（4 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T250 | `cannot open %1: %2` | 无法打开 %1：%2 | src/syntax/folding/ksyntax_fold_source.cpp:52 | VEdit | OK |
| T254 | `cannot parse %1: %2` | 无法解析 %1：%2 | src/syntax/folding/ksyntax_fold_source.cpp:91 | VEdit | OK |
| T309 | `fold lists of region %1 are empty` | 区域 %1 的折叠列表为空 | src/syntax/folding/ksyntax_fold_source.cpp:118 | VEdit | OK |
| T414 | `source is not loaded: %1` | 源码未加载：%1 | src/syntax/folding/ksyntax_fold_source.cpp:138 | VEdit | OK |

## `src/editor/kernel/highlight_diagnosis.cpp`（5 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T267 | `definitionForFileName(top.sv): %1` | definitionForFileName(top.sv)：%1 | src/editor/kernel/highlight_diagnosis.cpp:163 | VEdit | OK |
| T268 | `definitionForFileName(top.v): %1` | definitionForFileName(top.v)：%1 | src/editor/kernel/highlight_diagnosis.cpp:161 | VEdit | OK |
| T269 | `definitionForName(SystemVerilog): %1` | definitionForName(SystemVerilog)：%1 | src/editor/kernel/highlight_diagnosis.cpp:160 | VEdit | OK |
| T270 | `definitionForName(Verilog): %1` | definitionForName(Verilog)：%1 | src/editor/kernel/highlight_diagnosis.cpp:159 | VEdit | OK |
| T390 | `repository-definition %1` | 仓库定义 %1 | src/editor/kernel/highlight_diagnosis.cpp:154 | VEdit | OK |

## `src/app/editor_provider.cpp`（1 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T271 | `demo editor (shell placeholder)` | 演示编辑器（外壳占位） | src/app/editor_provider.cpp:24 | VEdit | OK |

## `src/workspace/diagnostics/diagnostics_panel.cpp`（2 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T284 | `editor buffer, not saved yet` | 编辑器缓冲区，尚未保存 | src/workspace/diagnostics/diagnostics_panel.cpp:302 | VEdit | OK |
| T351 | `no diagnostics controller` | 没有诊断控制器 | src/workspace/diagnostics/diagnostics_panel.cpp:282 | VEdit | OK |

## `src/editor/kernel/find_replace.cpp`（3 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T307 | `find: no document` | 查找：没有文档 | src/editor/kernel/find_replace.cpp:114 | VEdit | OK |
| T308 | `find: the pattern is empty` | 查找：模式为空 | src/editor/kernel/find_replace.cpp:118 | VEdit | OK |
| T389 | `replace-all replaced %1 of %2 matches` | 全部替换：已替换 %2 个匹配中的 %1 个 | src/editor/kernel/find_replace.cpp:235 | VEdit | OK |

## `src/editor/kernel/goto_line.cpp`（4 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T313 | `goto-line: cursor landed at %1:%2 instead of %3:0` | 跳转到行：光标落在 %1:%2，而非 %3:0 | src/editor/kernel/goto_line.cpp:38 | VEdit | OK |
| T314 | `goto-line: no view/document` | 跳转到行：没有视图/文档 | src/editor/kernel/goto_line.cpp:13 | VEdit | OK |
| T315 | `goto-line: setCursorPosition(%1) failed` | 跳转到行：setCursorPosition(%1) 失败 | src/editor/kernel/goto_line.cpp:32 | VEdit | OK |
| T316 | `goto-line: the document has no lines` | 跳转到行：文档没有行 | src/editor/kernel/goto_line.cpp:19 | VEdit | OK |

## `src/syntax/folding/syntax_fold_source.cpp`（3 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T317 | `grammar '%1' is not registered` | 语法“%1”未注册 | src/syntax/folding/syntax_fold_source.cpp:39 | VEdit | OK |
| T385 | `provider '%1' parsed %2 fold ranges` | provider“%1”解析出 %2 个折叠范围 | src/syntax/folding/syntax_fold_source.cpp:64 | VEdit | OK |
| T452 | `tree-sitter switch is off` | tree-sitter 开关已关闭 | src/syntax/folding/syntax_fold_source.cpp:32 | VEdit | OK |

## `src/editor/kernel/multi_cursor.cpp`（6 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T333 | `multi-cursor: View::insertText() failed` | 多光标：View::insertText() 失败 | src/editor/kernel/multi_cursor.cpp:330 | VEdit | OK |
| T334 | `multi-cursor: asked for %1 cursors, the view reports %2` | 多光标：请求 %1 个光标，视图报告 %2 个 | src/editor/kernel/multi_cursor.cpp:167 | VEdit | OK |
| T335 | `multi-cursor: inserted at %1 of %2 cursors` | 多光标：已在 %2 个光标中的 %1 个处插入 | src/editor/kernel/multi_cursor.cpp:215 | VEdit | OK |
| T336 | `multi-cursor: no document` | 多光标：没有文档 | src/editor/kernel/multi_cursor.cpp:190<br>src/editor/kernel/multi_cursor.cpp:233 | VEdit | OK |
| T337 | `multi-cursor: no positions given` | 多光标：未提供位置 | src/editor/kernel/multi_cursor.cpp:160<br>src/editor/kernel/multi_cursor.cpp:194<br>src/editor/kernel/multi_cursor.cpp:237 | VEdit | OK |
| T338 | `multi-cursor: no view` | 多光标：没有视图 | src/editor/kernel/multi_cursor.cpp:156<br>src/editor/kernel/multi_cursor.cpp:325 | VEdit | OK |

## `src/workspace/diagnostics/ffi_backends.cpp`（5 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T344 | `no LSP request to poll` | 没有可轮询的 LSP 请求 | src/workspace/diagnostics/ffi_backends.cpp:343 | VEdit | OK |
| T345 | `no LSP session` | 没有 LSP 会话 | src/workspace/diagnostics/ffi_backends.cpp:247<br>src/workspace/diagnostics/ffi_backends.cpp:323 | VEdit | OK |
| T348 | `no SLANG ticket to poll` | 没有可轮询的 SLANG 凭据 | src/workspace/diagnostics/ffi_backends.cpp:620 | VEdit | OK |
| T427 | `the Rust service handle could not be created` | 无法创建 Rust 服务句柄 | src/workspace/module_tree/hierarchy_client.cpp:215<br>src/workspace/diagnostics/ffi_backends.cpp:196<br>src/workspace/diagnostics/ffi_backends.cpp:582 | VEdit | OK |
| T431 | `the SLANG job id does not fit the backend ticket type` | SLANG 作业 id 不适合后端凭据类型 | src/workspace/diagnostics/ffi_backends.cpp:610 | VEdit | OK |

## `src/workspace/breadcrumb/syntax_breadcrumb_source.cpp`（2 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T362 | `no provider for grammar '%1'` | 语法“%1”没有 provider | src/workspace/breadcrumb/syntax_breadcrumb_source.cpp:26<br>src/workspace/breadcrumb/syntax_breadcrumb_source.cpp:50<br>src/syntax/folding/syntax_fold_source.cpp:46 | VEdit | OK |
| T379 | `parse returned no tree` | 解析未返回语法树 | src/workspace/breadcrumb/syntax_breadcrumb_source.cpp:57<br>src/syntax/folding/syntax_fold_source.cpp:55 | VEdit | OK |

## `src/workspace/palette/file_index.cpp`（2 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T368 | `no workspace root configured` | 未配置工作区根目录 | src/workspace/palette/file_index.cpp:83 | VEdit | OK |
| T463 | `workspace root is not a directory: %1` | 工作区根目录不是目录：%1 | src/workspace/palette/file_index.cpp:90 | VEdit | OK |

## `src/syntax/kde/ksyntax_data.cpp`（1 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T383 | `private syntax directory missing: %1/syntax (set VEDIT_SYNTAX_DIR)` | 私有语法目录缺失：%1/syntax（设置 VEDIT_SYNTAX_DIR） | src/syntax/kde/ksyntax_data.cpp:112 | VEdit | OK |

## `src/workspace/settings/settings_client.cpp`（5 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T408 | `settings load failed (state %1)` | 设置加载失败（状态 %1） | src/workspace/settings/settings_client.cpp:198 | VEdit | OK |
| T409 | `settings save failed (state %1)` | 设置保存失败（状态 %1） | src/workspace/settings/settings_client.cpp:222 | VEdit | OK |
| T429 | `the Rust settings store rejected the path` | Rust 设置存储拒绝了该路径 | src/workspace/settings/settings_client.cpp:125 | VEdit | OK |
| T430 | `the Rust settings store rejected the write` | Rust 设置存储拒绝了写入 | src/workspace/settings/settings_client.cpp:165 | VEdit | OK |
| T448 | `this build has no vd-ffi settings ABI` | 此构建没有 vd-ffi 设置 ABI | src/workspace/settings/settings_client.cpp:126<br>src/workspace/settings/settings_client.cpp:166 | VEdit | OK |

## `src/workspace/project/project_service.cpp`（1 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T425 | `the Rust project service is unavailable` | Rust 项目服务不可用 | src/workspace/project/project_service.cpp:124 | VEdit | OK |

## `src/editor/fim/fim_backend.cpp`（2 条）

| id | 源串 | 中文 | 落点 | 上下文 | .ts |
|---|---|---|---|---|---|
| T428 | `the Rust service reported no detail` | Rust 服务未报告细节 | src/editor/fim/fim_backend.cpp:25 | VEdit | OK |
| T453 | `unavailable (no Rust service in this build)` | 不可用（此构建没有 Rust 服务） | src/editor/fim/fim_backend.cpp:178 | VEdit | OK |
