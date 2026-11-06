# T21 — 466 候选逐条对账报告（AC-4.5）

Cell `cell-20261102T100600176Z` · task **T21** (`i18n-t21`) · member **se-i18n**

语料（唯一输入，未自造）：`07-triage-466.json`（466 行分类）、`02-bilingual-table-draft.md`、`03-glossary.md`；译文落点：`09-ui-translations.json`、`12-ui-blocks.md`。

## 1. 计数核对

| 分类 | 行数 | 期望 | 结果 |
|---|---|---|---|
| UI | 215 | 215 | OK |
| non-UI | 195 | 195 | OK |
| already-translated | 31 | 31 | OK |
| obsolete | 15 | 15 | OK |
| uncertain | 10 | 10 | OK |
| **合计** | **466** | **466** | OK |

## 2. 决策分布

| decision | 条数 |
|---|---|
| 译 | 213 |
| 不译 | 197 |
| 已译 | 31 |
| 删除 | 15 |
| 待定→保留英文 | 10 |

`uncertain` 的 10 条按合同默认口径处理：**保留英文 + 登记理由**（decision 记 `待定→保留英文`）。

## 3. 逐条表（466 行）

| id | source_01 | classification | decision | string | reason |
|---|---|---|---|---|---|
| T001 | `src/app/editor_area.cpp:338` | obsolete | **删除** | `    ◀ active` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/app/editor_area.cpp；当前树中已无该字面量，无需代码动作 |
| T002 | `src/workspace/project/project_model.cpp:156` | UI | **译** | ` (expanding…)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T003 | `src/workspace/project/project_model.cpp:162` | UI | **译** | ` (live)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T004 | `src/workspace/project/project_model.cpp:158` | UI | **译** | ` (refresh failed)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T005 | `src/workspace/project/project_model.cpp:160` | UI | **译** | ` (restored from last session)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T006 | `src/workspace/breadcrumb/breadcrumb_model.cpp:41` | non-UI | **不译** | ` > ` | R14 无任何单词字符（纯格式/标点）（非 UI 表面，不进入 .ts） |
| T007 | `src/editor/kernel/fim_wiring_selftest.cpp:258` | non-UI | **不译** | ` assign y = 2;` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T008 | `src/editor/fim/fim_selftest.cpp:242` | non-UI | **不译** | ` async-suggestion` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T009 | `src/app/main.cpp:1331` | non-UI | **不译** | ` built ` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T010 | `src/workspace/filetree/tools/rootcheck/seamcheck.cpp:212` | non-UI | **不译** | ` bytes on disk` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T011 | `src/workspace/diagnostics/diagnostics_controller.cpp:205` | non-UI | **不译** | ` is unavailable` | R11 Verilog/SystemVerilog 代码片段或实例化文本（测试夹具/示例）（非 UI 表面，不进入 .ts） |
| T012 | `src/workspace/settings/settings_panel.cpp:488` | non-UI | **不译** | ` \| ` | R14 无任何单词字符（纯格式/标点）（非 UI 表面，不进入 .ts） |
| T013 | `src/syntax/kde/tests/validate_syntax_data.cpp:93` | non-UI | **不译** | ` … (+%1)` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T014 | `src/app/app_shell.cpp:322` | UI | **译** | `&About VEdit` | R15 出现在 Qt UI 调用点（ui_action）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T015 | `src/app/app_shell.cpp:268` | UI | **译** | `&Close Pane` | R15 出现在 Qt UI 调用点（ui_action）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T016 | `src/app/app_shell.cpp:278` | UI | **译** | `&Focus Next Pane` | R15 出现在 Qt UI 调用点（ui_action）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T017 | `src/workspace/palette/palette_integration.cpp:91` | UI | **译** | `&Go to File…` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T018 | `src/app/app_shell.cpp:280` | UI | **译** | `&Mixed Split (demo)` | R15 出现在 Qt UI 调用点（ui_action）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T019 | `src/app/app_shell.cpp:256` | UI | **译** | `&New Empty Layout` | R15 出现在 Qt UI 调用点（ui_action）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T020 | `src/app/app_shell.cpp:282` | UI | **译** | `&Reset Layout` | R15 出现在 Qt UI 调用点（ui_action）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T021 | `src/app/app_shell.cpp:264` | UI | **译** | `&Save Session Now` | R15 出现在 Qt UI 调用点（ui_action）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T022 | `src/workspace/filetree/file_ops.cpp:69` | UI | **译** | `'%1' is a reserved name` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T023 | `src/workspace/filetree/file_tree_model.cpp:84` | already-translated | **已译** | `'%1' is not a folder.` | R01 已被 tr()/QObject::tr() 直接包装，无需再包；英文源串已补中文（10-ui-translations-supplementary.json） |
| T024 | `src/workspace/filetree/file_ops.cpp:145` | UI | **译** | `'%1' no longer exists` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T025 | `src/workspace/vcs/vcs_selftest.cpp:152` | non-UI | **不译** | `+module top;` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T026 | `src/workspace/palette/palette_bench.cpp:122` | non-UI | **不译** | `10k-file Ctrl+P palette benchmark (VEdit T22 / A2b).` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T027 | `src/editor/kernel/fim_wiring_selftest.cpp:439` | non-UI | **不译** | `20 ms timer fired at %1 ms after scheduling` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T028 | `src/workspace/project/project_selftest.cpp:132` | obsolete | **删除** | `7 files` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/workspace/project/project_selftest.cpp；当前树中已无该字面量，无需代码动作 |
| T029 | `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:183` | UI | **译** | `: 0 diagnostic(s)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T030 | `src/app/crash_reporter.cpp:234` | non-UI | **不译** | `<too large to hash>` | R21 代码符号/数据片段（以符号开头或含 ->）（非 UI 表面，不进入 .ts） |
| T031 | `src/workspace/filetree/file_ops.cpp:96` | UI | **译** | `A file or folder named %1 already exists` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T032 | `src/app/app_shell.cpp:370` | UI | **译** | `About VEdit` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T033 | `src/workspace/settings/settings_panel.cpp:55` | already-translated | **已译** | `Applied immediately and stored as `theme` by the Rust service.` | R01 已被 tr()/QObject::tr() 直接包装，无需再包；tr() 已包装，中文条目随本次 .ts 合并 |
| T034 | `src/workspace/settings/settings_panel.cpp:76` | already-translated | **已译** | `Assign to selected command` | R01 已被 tr()/QObject::tr() 直接包装，无需再包；英文源串已补中文（10-ui-translations-supplementary.json） |
| T035 | `src/editor/fim/fim_selftest.cpp:478` | non-UI | **不译** | `Authorization: Bearer <redacted>` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T036 | `src/app/main.cpp:1196` | UI | **不译** | `Breeze Dark` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；主题标识为 KSyntaxHighlighting 的库内名称，按术语表技术名词保留英文 |
| T037 | `src/app/theme_manager.cpp:42` | UI | **不译** | `Breeze Light` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；同 T036：主题标识符保留英文 |
| T038 | `src/workspace/search/search_types.cpp:14` | non-UI | **不译** | `CMakeFiles` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T039 | `src/workspace/palette/palette_bench.cpp:124` | non-UI | **不译** | `Candidate file count.` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T040 | `src/workspace/filetree/file_tree_widget.cpp:312` | UI | **译** | `Cannot delete the folder that is open. Close it first.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T041 | `src/workspace/filetree/filetree_integration.cpp:44` | UI | **译** | `Cannot open %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T042 | `src/workspace/filetree/filetree_integration.cpp:40` | UI | **译** | `Cannot open %1: no active editor pane` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T043 | `src/workspace/filetree/file_tree_widget.cpp:151` | UI | **译** | `Cannot read '%1': %2` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T044 | `src/workspace/search/search_selftest.cpp:1151` | non-UI | **不译** | `Capture screenshots into this directory and exit.` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T045 | `src/app/main.cpp:1311` | UI | **译** | `Capture split-state screenshots into this directory and exit.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T046 | `src/workspace/settings/settings_panel.cpp:400` | UI | **译** | `Changes discarded; nothing was stored.` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T047 | `src/app/editor_area.cpp:125` | UI | **译** | `Close Pane` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T048 | `src/workspace/filetree/file_tree_widget.cpp:77` | UI | **译** | `Collapse every expanded folder` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T049 | `src/workspace/settings/settings_panel.cpp:487` | UI | **译** | `Conflicting shortcuts: %1` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T050 | `src/workspace/filetree/file_ops.cpp:107` | UI | **译** | `Could not create file %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T051 | `src/workspace/filetree/file_ops.cpp:130` | UI | **译** | `Could not create folder %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T052 | `src/workspace/filetree/file_ops.cpp:202` | UI | **译** | `Could not delete %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T053 | `src/workspace/filetree/file_ops.cpp:168` | UI | **译** | `Could not rename %1 to %2` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T054 | `src/workspace/filetree/file_tree_widget.cpp:57` | UI | **译** | `Create a file here (Ctrl+N)` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T055 | `src/workspace/filetree/file_tree_widget.cpp:61` | UI | **译** | `Create a folder here (Ctrl+Shift+N)` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T056 | `src/workspace/palette/palette_bench.cpp:131` | non-UI | **不译** | `Create/scan a real file tree under this directory.` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T057 | `src/workspace/filetree/tests/filetree_tests.cpp:398` | non-UI | **不译** | `Created file` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T058 | `src/workspace/filetree/file_tree_widget.cpp:234` | UI | **译** | `Created file '%1' (%2)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T059 | `src/workspace/filetree/file_tree_widget.cpp:266` | UI | **译** | `Created folder '%1'` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T060 | `src/workspace/palette/palette_integration_selftest.cpp:109` | non-UI | **不译** | `Ctrl+P palette integration selftest inside the VEdit shell.` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T061 | `src/workspace/filetree/file_tree_widget.cpp:69` | UI | **译** | `Delete the selection (Del)` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T062 | `src/workspace/filetree/file_tree_widget.cpp:333` | uncertain | **待定→保留英文** | `Deleted '%1'` | R25 无决定信号：既不像代码/键，也没有 UI 调用点；保留英文（AC-4.5 默认口径：未判定 → keep-en + 登记理由） |
| T063 | `src/workspace/project/project_panel.cpp:130` | UI | **译** | `Diagnostics (%1)` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T064 | `src/workspace/project/project_panel.cpp:55` | UI | **译** | `Diagnostics (0)` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T065 | `src/workspace/diagnostics/toolchain_ux_selftest.cpp:124` | UI | **译** | `Diagnostics unavailable` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T066 | `src/app/main.cpp:1298` | UI | **译** | `Directory for session.json and shortcuts.json.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T067 | `src/app/panel_registry.cpp:48` | UI | **译** | `Empty state: no project / no data.` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T068 | `src/app/app_shell.cpp:216` | UI | **译** | `Empty state: no project open. This panel stays usable and empty.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T069 | `src/app/app_shell.cpp:217` | UI | **译** | `Empty state: nothing to show for this project yet.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T070 | `src/workspace/search/search_selftest.cpp:309` | non-UI | **不译** | `Enter a query` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T071 | `src/editor/fim/fim_inline_completion.cpp:19` | already-translated | **已译** | `FIM 不可用（未链接 Rust 服务）` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T072 | `src/editor/fim/fim_inline_completion.cpp:65` | already-translated | **已译** | `FIM 不可用（未链接 Rust 服务；未发出请求）` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T073 | `src/editor/fim/fim_inline_completion.cpp:114` | already-translated | **已译** | `FIM 失败（无详情）` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T074 | `src/editor/fim/fim_backend.cpp:126` | already-translated | **已译** | `FIM 失败：%1` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T075 | `src/editor/fim/fim_inline_completion.cpp:196` | already-translated | **已译** | `FIM 已取消` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T076 | `src/editor/fim/fim_inline_completion.cpp:184` | already-translated | **已译** | `FIM 已接受建议` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T077 | `src/editor/fim/fim_inline_completion.cpp:108` | already-translated | **已译** | `FIM 建议就绪` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T078 | `src/editor/fim/fim_inline_completion.cpp:173` | already-translated | **已译** | `FIM 建议已作废（继续输入）` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T079 | `src/editor/fim/fim_backend.cpp:78` | already-translated | **已译** | `FIM 服务不可用（无法创建 Rust 句柄）` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T080 | `src/editor/fim/fim_selftest.cpp:310` | already-translated | **已译** | `FIM 未启用` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T081 | `src/editor/fim/fim_inline_completion.cpp:18` | already-translated | **已译** | `FIM 未启用（默认关闭）` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T082 | `src/editor/fim/fim_inline_completion.cpp:159` | already-translated | **已译** | `FIM 未知状态` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T083 | `src/editor/fim/fim_inline_completion.cpp:83` | already-translated | **已译** | `FIM 请求中…` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T084 | `src/editor/fim/fim_inline_completion.cpp:135` | already-translated | **已译** | `FIM 请求已取消` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T085 | `src/editor/fim/fim_inline_completion.cpp:117` | already-translated | **已译** | `FIM 请求已失效` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T086 | `src/editor/fim/fim_backend.cpp:50` | already-translated | **已译** | `FIM 请求未被 Rust 服务接受` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T087 | `src/editor/fim/fim_backend.cpp:90` | already-translated | **已译** | `FIM 请求未被 Rust 服务接受：%1` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T088 | `src/editor/fim/fim_selftest.cpp:355` | already-translated | **已译** | `FIM 超时` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T089 | `src/editor/fim/fim_backend.cpp:70` | already-translated | **已译** | `FIM 超时（%1 ms 无结果）` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T090 | `src/editor/kernel/fim_wiring.cpp:281` | already-translated | **已译** | `FIM 超时（%1 ms 无结果，已取消）` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T091 | `src/editor/fim/fim_selftest.cpp:351` | already-translated | **已译** | `FIM 超时（3000ms）` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T092 | `src/editor/fim/fim_backend.cpp:129` | already-translated | **已译** | `FIM 轮询异常（state %1）` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T093 | `src/editor/fim/fim_inline_completion.cpp:108` | already-translated | **已译** | `FIM 返回空建议` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
| T094 | `src/workspace/palette/palette_bench.cpp:136` | non-UI | **不译** | `Fail when the overall palette p95 exceeds this budget.` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T095 | `src/workspace/filetree/file_kind.cpp:53` | UI | **译** | `File list` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T096 | `src/workspace/palette/command_palette.cpp:147` | UI | **译** | `Go to file…` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T097 | `src/app/main.cpp:1314` | UI | **译** | `Ignore an existing session file at startup.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T098 | `src/app/main.cpp:1302` | UI | **译** | `KSyntaxHighlighting theme name.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T099 | `src/editor/probes/tx_probe.cpp:40` | non-UI | **不译** | `KTextEditor` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T100 | `src/syntax/product/syntax_product.cpp:92` | uncertain | **待定→保留英文** | `KTextEditor %1` | R25 无决定信号：既不像代码/键，也没有 UI 调用点；保留英文（AC-4.5 默认口径：未判定 → keep-en + 登记理由） |
| T101 | `src/editor/kernel/ktexteditor_provider.cpp:66` | UI | **译** | `KTextEditor (edit-core)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T102 | `src/editor/kernel/highlight_mode.cpp:130` | UI | **译** | `KTextEditor::Document::setHighlightingMode('%1') failed` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T103 | `src/editor/kernel/document_io.cpp:109` | UI | **译** | `KTextEditor::Document::setText() failed` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T104 | `src/editor/kernel/document_io.cpp:48` | UI | **译** | `KTextEditor::Document::setText() failed for %1` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T105 | `src/editor/kernel/document_io.cpp:79` | UI | **译** | `KTextEditor::Editor::createDocument() returned null` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T106 | `src/app/main.cpp:1315` | UI | **译** | `Keep the window for this many ms.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T107 | `src/workspace/settings/settings_panel.cpp:60` | obsolete | **删除** | `Key bindings` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/workspace/settings/settings_panel.cpp；当前树中已无该字面量，无需代码动作 |
| T108 | `src/workspace/diagnostics/diagnostics_controller.cpp:180` | UI | **译** | `LSP backend is not configured (no workspace root)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T109 | `src/workspace/settings/settings_panel.cpp:140` | UI | **译** | `Loading settings from %1 …` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T110 | `src/workspace/filetree/file_ops.cpp:78` | UI | **译** | `Name is longer than the 255-byte filesystem limit` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T111 | `src/workspace/filetree/file_ops.cpp:66` | UI | **译** | `Name must not be empty` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T112 | `src/workspace/filetree/file_ops.cpp:75` | UI | **译** | `Name must not contain a NUL character` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T113 | `src/workspace/filetree/file_ops.cpp:72` | UI | **译** | `Name must not contain a path separator` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T114 | `src/workspace/filetree/file_tree_widget.cpp:56` | already-translated | **已译** | `New File` | R01 已被 tr()/QObject::tr() 直接包装，无需再包；英文源串已补中文（10-ui-translations-supplementary.json） |
| T115 | `src/workspace/filetree/file_tree_widget.cpp:60` | already-translated | **已译** | `New Folder` | R01 已被 tr()/QObject::tr() 直接包装，无需再包；英文源串已补中文（10-ui-translations-supplementary.json） |
| T116 | `src/workspace/settings/settings_panel.cpp:80` | already-translated | **已译** | `New shortcut:` | R01 已被 tr()/QObject::tr() 直接包装，无需再包；英文源串已补中文（10-ui-translations-supplementary.json） |
| T117 | `src/workspace/vcs/vcs_panel.cpp:133` | UI | **译** | `No VCS backend available.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T118 | `src/workspace/settings/settings_panel.cpp:411` | UI | **译** | `No configuration problems reported by the Rust store.` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T119 | `src/workspace/settings/settings_panel.cpp:483` | UI | **译** | `No conflicting shortcuts.` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T120 | `src/app/editor_area.cpp:148` | UI | **译** | `No editor provider connected.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T121 | `src/workspace/palette/command_palette.cpp:419` | UI | **译** | `No files indexed for this project yet.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T122 | `src/workspace/filetree/tests/filetree_tests.cpp:155` | non-UI | **不译** | `No folder open` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T123 | `src/workspace/palette/command_palette.cpp:320` | UI | **译** | `No matching files` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T124 | `src/workspace/palette/command_palette.cpp:432` | UI | **译** | `No project` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T125 | `src/workspace/project/project_model.cpp:147` | UI | **译** | `No project open` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T126 | `src/workspace/palette/command_palette.cpp:417` | UI | **译** | `No project open — open a project root to jump between files.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T127 | `src/workspace/search/search_selftest.cpp:377` | non-UI | **不译** | `No project root` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T128 | `src/workspace/vcs/vcs_selftest.cpp:108` | UI | **译** | `No repository` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T129 | `src/workspace/search/search_selftest.cpp:404` | non-UI | **不译** | `No results` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T130 | `src/workspace/diagnostics/toolchain_ux_selftest.cpp:165` | non-UI | **不译** | `No symbols` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T131 | `src/workspace/filetree/file_tree_widget.cpp:53` | UI | **译** | `Open a folder` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T132 | `src/workspace/filetree/file_tree_widget.cpp:217` | UI | **译** | `Open a folder first.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T133 | `src/workspace/project/project_panel.cpp:38` | obsolete | **删除** | `Open a project with --project <dir>; the expanded file list appears here.` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/workspace/project/project_panel.cpp；当前树中已无该字面量，无需代码动作 |
| T134 | `src/workspace/filetree/filetree_integration.cpp:46` | uncertain | **待定→保留英文** | `Opened %1` | R25 无决定信号：既不像代码/键，也没有 UI 调用点；保留英文（AC-4.5 默认口径：未判定 → keep-en + 登记理由） |
| T135 | `src/workspace/search/search_jump_target.cpp:107` | UI | **译** | `Opened %1 line %2` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T136 | `src/workspace/filetree/file_tree_widget.cpp:181` | uncertain | **待定→保留英文** | `Opened '%1'` | R25 无决定信号：既不像代码/键，也没有 UI 调用点；保留英文（AC-4.5 默认口径：未判定 → keep-en + 登记理由） |
| T137 | `src/editor/fim/fim_selftest.cpp:485` | non-UI | **不译** | `POST /v1/completions ` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T138 | `src/app/app_shell.cpp:629` | obsolete | **删除** | `Panes: %1 · active: %2` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/app/app_shell.cpp；当前树中已无该字面量，无需代码动作 |
| T139 | `src/app/main.cpp:1300` | non-UI | **不译** | `Project root to display (optional).` | R13 命令行（CLI）帮助/用法文本，不走 Qt 翻译管线（非 UI 表面，不进入 .ts） |
| T140 | `src/app/app_shell.cpp:621` | UI | **译** | `Project: %1` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T141 | `src/workspace/project/project_model.cpp:151` | uncertain | **待定→保留英文** | `Project: %1 — %2 %3` | R25 无决定信号：既不像代码/键，也没有 UI 调用点；保留英文（AC-4.5 默认口径：未判定 → keep-en + 登记理由） |
| T142 | `src/editor/probes/replace_probe.cpp:71` | non-UI | **不译** | `REPLACE editor=null FAIL` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T143 | `src/editor/probes/replace_probe2.cpp:101` | non-UI | **不译** | `REPLACE2 editor=null FAIL` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T144 | `src/workspace/diagnostics/diagnostics_integration.cpp:409` | obsolete | **删除** | `Re&parse Project (Verible + SLANG)` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/workspace/diagnostics/diagnostics_integration.cpp；当前树中已无该字面量，无需代码动作 |
| T145 | `src/workspace/filetree/file_tree_widget.cpp:73` | UI | **译** | `Re-read the folder from disk (F5)` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T146 | `src/workspace/filetree/file_tree_widget.cpp:527` | uncertain | **待定→保留英文** | `Refreshed '%1'` | R25 无决定信号：既不像代码/键，也没有 UI 调用点；保留英文（AC-4.5 默认口径：未判定 → keep-en + 登记理由） |
| T147 | `src/workspace/settings/settings_panel.cpp:104` | already-translated | **已译** | `Reload from disk` | R01 已被 tr()/QObject::tr() 直接包装，无需再包；tr() 已包装，中文条目随本次 .ts 合并 |
| T148 | `src/workspace/filetree/file_tree_widget.cpp:65` | UI | **译** | `Rename the selection (F2)` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T149 | `src/workspace/filetree/file_tree_widget.cpp:295` | UI | **译** | `Renamed '%1' to '%2'` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T150 | `src/app/main.cpp:1305` | non-UI | **不译** | `Run non-interactive layout/theme/persistence checks and exit.` | R16 断言/校验模板（测试期望文本）（非 UI 表面，不进入 .ts） |
| T151 | `src/workspace/search/search_selftest.cpp:1149` | non-UI | **不译** | `Run the global search self-checks and exit.` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T152 | `src/editor/fim/fim_backend.cpp:175` | non-UI | **不译** | `Rust service (vd_fim_complete -> vd-fim)` | R21 代码符号/数据片段（以符号开头或含 ->）（非 UI 表面，不进入 .ts） |
| T153 | `src/workspace/diagnostics/toolchain.cpp:214` | non-UI | **不译** | `SLANG deep lint is unavailable` | R22 只出现在自检断言/输出调用点（self-check expectation）（非 UI 表面，不进入 .ts） |
| T154 | `src/editor/probes/tx_steps_probe.cpp:76` | non-UI | **不译** | `STEPS editor=null FAIL` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T155 | `src/editor/probes/tx_steps_probe.cpp:85` | non-UI | **不译** | `STEPS meta undo()=%1 redo()=%2 undoCount()=%3` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T156 | `src/editor/probes/tx_steps_probe.cpp:65` | non-UI | **不译** | `STEPS mode=%1 len_before=%2 undoCount=%3 steps_to_empty=%4 verdict=%5` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T157 | `src/editor/probes/tx_steps_probe2.cpp:85` | non-UI | **不译** | `STEPS2 %1 case=one_tx_100_edits steps=%2 verdict=%3` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T158 | `src/editor/probes/tx_steps_probe2.cpp:70` | non-UI | **不译** | `STEPS2 %1 case=redo_all restored_ok=%2 verdict=%3` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T159 | `src/editor/probes/tx_steps_probe2.cpp:100` | non-UI | **不译** | `STEPS2 editor=null FAIL` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T160 | `src/editor/probes/tx_steps_probe3.cpp:34` | non-UI | **不译** | `STEPS3 editor=null FAIL` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T161 | `src/editor/probes/tx_steps_probe3.cpp:67` | non-UI | **不译** | `STEPS3 keyevents chars=%1 steps_to_stable=%2 verdict=%3` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T162 | `src/editor/probes/tx_steps_probe3.cpp:72` | non-UI | **不译** | `STEPS3 keyevents redo_all restored_ok=%1` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T163 | `src/editor/probes/tx_steps_probe3.cpp:39` | non-UI | **不译** | `STEPS3 mergeFlag indexOfMethod=%1 invokable=%2` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T164 | `src/editor/probes/tx_steps_probe3.cpp:50` | non-UI | **不译** | `STEPS3 mergeFlag=off case=3_tx steps=%1 verdict=%2` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T165 | `src/editor/probes/tx_steps_probe4.cpp:34` | non-UI | **不译** | `STEPS4 editor=null FAIL` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T166 | `src/editor/probes/tx_steps_probe4.cpp:51` | non-UI | **不译** | `STEPS4 keys chars=%1 steps_to_stable=%2 verdict=%3` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T167 | `src/editor/probes/tx_steps_probe4.cpp:64` | non-UI | **不译** | `STEPS4 prog_with_events chars=%1 steps=%2 verdict=%3` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T168 | `src/editor/probes/tx_steps_probe5.cpp:54` | non-UI | **不译** | `STEPS5 editor=null FAIL` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T169 | `src/editor/probes/tx_steps_probe5.cpp:33` | non-UI | **不译** | `STEPS5 insert_lines=100 steps=%1 verdict=%2` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T170 | `src/editor/probes/tx_steps_probe5.cpp:46` | non-UI | **不译** | `STEPS5 replace_text=100 steps=%1 verdict=%2` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T171 | `src/workspace/palette/palette_bench.cpp:126` | non-UI | **不译** | `Samples per query.` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T172 | `src/app/main.cpp:864` | UI | **译** | `Save failed` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T173 | `src/app/app_shell.cpp:532` | UI | **译** | `Save failed: %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T174 | `src/app/app_shell.cpp:550` | uncertain | **待定→保留英文** | `Saved %1` | R25 无决定信号：既不像代码/键，也没有 UI 调用点；保留英文（AC-4.5 默认口径：未判定 → keep-en + 登记理由） |
| T175 | `src/workspace/settings/settings_panel.cpp:280` | UI | **译** | `Select a command in the table first.` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T176 | `src/workspace/filetree/file_tree_widget.cpp:276` | UI | **译** | `Select a file or folder first.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T177 | `src/workspace/palette/palette_selftest.cpp:310` | non-UI | **不译** | `Selftest of the VEdit Ctrl+P file palette lane (T22 / A2b).` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T178 | `src/app/app_shell.cpp:336` | UI | **译** | `Session saved (%1)` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T179 | `src/workspace/settings/settings_panel.cpp:159` | UI | **译** | `Settings loaded from %1` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T180 | `src/workspace/settings/settings_panel.cpp:146` | UI | **译** | `Settings unavailable; showing the built-in defaults.` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T181 | `src/workspace/settings/settings_panel.cpp:271` | UI | **译** | `Shortcut for '%1' cleared (default restored).` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T182 | `src/app/app_shell.cpp:222` | UI | **译** | `Source Control` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T183 | `src/workspace/settings/settings_selftest.cpp:87` | non-UI | **不译** | `Split Horizontally` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T184 | `src/app/app_shell.cpp:274` | UI | **译** | `Split Pane &Horizontally` | R15 出现在 Qt UI 调用点（ui_action）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T185 | `src/app/app_shell.cpp:276` | UI | **译** | `Split Pane &Vertically` | R15 出现在 Qt UI 调用点（ui_action）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T186 | `src/workspace/settings/settings_selftest.cpp:167` | non-UI | **不译** | `Stored by the Rust service` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T187 | `src/workspace/settings/settings_panel.cpp:317` | UI | **译** | `Stored by the Rust service in %1` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T188 | `src/app/main.cpp:923` | non-UI | **不译** | `SystemVerilog` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T189 | `src/syntax/kde/tests/validate_syntax_data.cpp:176` | non-UI | **不译** | `SystemVerilog definition not shipped` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T190 | `src/workspace/diagnostics/toolchain.cpp:334` | obsolete | **删除** | `TOOLCHAIN %1 local=[%2]` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/workspace/diagnostics/toolchain.cpp；当前树中已无该字面量，无需代码动作 |
| T191 | `src/editor/probes/tx_probe.cpp:115` | non-UI | **不译** | `TX %1 case=plain100_insert len=%2 after_undo_len=%3 one_step=%4 verdict=%5` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T192 | `src/editor/probes/tx_probe.cpp:72` | non-UI | **不译** | `TX %1 case=plain3_insert text3=%2 after_undo=%3 verdict=%4` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T193 | `src/editor/probes/tx_probe.cpp:122` | non-UI | **不译** | `TX %1 case=redo_after_plain_undo len=%2 verdict=%3` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T194 | `src/editor/probes/tx_probe.cpp:102` | non-UI | **不译** | `TX %1 case=tx100_insert len=%2 after_undo_len=%3 empty=%4 verdict=%5` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T195 | `src/editor/probes/tx_probe.cpp:87` | non-UI | **不译** | `TX %1 case=tx3_insert text=%2 after_undo=%3 empty=%4 verdict=%5` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T196 | `src/editor/probes/tx_probe.cpp:54` | non-UI | **不译** | `TX %1 metaClass=%2 undoIndex=%3 redoIndex=%4` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T197 | `src/editor/probes/tx_probe.cpp:59` | non-UI | **不译** | `TX %1 undo_unreachable=1` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T198 | `src/editor/probes/tx_probe.cpp:45` | non-UI | **不译** | `TX doc=null FAIL view=%1` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T199 | `src/editor/probes/tx_probe.cpp:37` | non-UI | **不译** | `TX editor=null FAIL` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T200 | `src/editor/probes/tx_probe.cpp:40` | non-UI | **不译** | `TX editor=ok class=%1` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T201 | `src/workspace/filetree/file_tree_model.cpp:90` | UI | **译** | `The folder '%1' cannot be read (permission denied).` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T202 | `src/workspace/filetree/file_tree_model.cpp:78` | UI | **译** | `The folder '%1' does not exist.` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T203 | `src/workspace/settings/settings_panel.cpp:234` | UI | **译** | `Theme '%1' applied and stored by the Rust service.` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T204 | `src/workspace/settings/settings_panel.cpp:227` | UI | **译** | `Theme '%1' could not be applied.` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T205 | `src/app/app_shell.cpp:625` | UI | **译** | `Theme: %1` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T206 | `src/workspace/settings/settings_selftest.cpp:95` | non-UI | **不译** | `Toggle Settings Panel` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T207 | `src/workspace/diagnostics/diagnostics_selftest.cpp:279` | non-UI | **不译** | `UndeclaredIdentifier` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T208 | `src/app/app_shell.cpp:76` | UI | **译** | `Unsaved changes` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T209 | `src/app/main.cpp:1388` | UI | **译** | `Use &tree-sitter parsing` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T210 | `src/app/main.cpp:1294` | UI | **译** | `VEdit application shell (Qt6/KTextEditor rewrite).` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T211 | `src/workspace/search/search_selftest.cpp:1145` | non-UI | **不译** | `VEdit global search self-check (task T23)` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T212 | `src/app/app_shell.cpp:635` | obsolete | **删除** | `VEdit — %1` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/app/app_shell.cpp；当前树中已无该字面量，无需代码动作 |
| T213 | `src/app/app_shell.cpp:634` | obsolete | **删除** | `VEdit — no project` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/app/app_shell.cpp；当前树中已无该字面量，无需代码动作 |
| T214 | `src/workspace/palette/palette_bench.cpp:128` | non-UI | **不译** | `Visible results per query.` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T215 | `src/workspace/palette/palette_bench.cpp:133` | non-UI | **不译** | `Write machine-readable results here.` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T216 | `src/workspace/palette/palette_selftest.cpp:313` | non-UI | **不译** | `Write palette screenshots into this directory.` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T217 | `src/workspace/palette/palette_integration_selftest.cpp:112` | non-UI | **不译** | `Write shell/palette screenshots into this directory.` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T218 | `src/editor/probes/replace_probe2.cpp:113` | non-UI | **不译** | `a beta a` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T219 | `src/app/main.cpp:1100` | non-UI | **不译** | `actual=%1 files=[%2] (C ABI fixture expects 3)` | R09 机器可读诊断模板（key=%1 …），供日志/门禁解析（非 UI 表面，不进入 .ts） |
| T220 | `src/editor/kernel/fim_wiring_selftest.cpp:454` | non-UI | **不译** | `after %1 ms with a 300 ms budget` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T221 | `src/editor/probes/replace_probe2.cpp:112` | non-UI | **不译** | `alpha beta alpha` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T222 | `src/workspace/diagnostics/diagnostics_ffi_selftest.cpp:194` | non-UI | **不译** | `alu u_alu` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T223 | `src/workspace/diagnostics/diagnostics_selftest.cpp:530` | non-UI | **不译** | `analysis count=%1` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T224 | `src/workspace/diagnostics/diagnostics_controller.cpp:155` | uncertain | **待定→保留英文** | `analyzing %1` | R25 无决定信号：既不像代码/键，也没有 UI 调用点；保留英文（AC-4.5 默认口径：未判定 → keep-en + 登记理由） |
| T225 | `src/editor/fim/fim_selftest.cpp:461` | non-UI | **不译** | `assign x = 1;` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T226 | `src/editor/kernel/atomic_save.cpp:136` | non-UI | **不译** | `atomic save: injected directory fsync failure` | R16 断言/校验模板（测试期望文本）（非 UI 表面，不进入 .ts） |
| T227 | `src/editor/kernel/atomic_save.cpp:114` | non-UI | **不译** | `atomic save: injected fsync failure` | R16 断言/校验模板（测试期望文本）（非 UI 表面，不进入 .ts） |
| T228 | `src/editor/kernel/atomic_save.cpp:124` | non-UI | **不译** | `atomic save: injected rename failure` | R16 断言/校验模板（测试期望文本）（非 UI 表面，不进入 .ts） |
| T229 | `src/editor/kernel/atomic_save.cpp:107` | non-UI | **不译** | `atomic save: injected write failure` | R16 断言/校验模板（测试期望文本）（非 UI 表面，不进入 .ts） |
| T230 | `src/editor/fim/fim_selftest.cpp:479` | non-UI | **不译** | `authorization: bearer <redacted>` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T231 | `src/editor/kernel/edit_core_selftest.cpp:409` | non-UI | **不译** | `before=%1 after=%2` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T232 | `src/editor/kernel/edit_core_selftest.cpp:478` | non-UI | **不译** | `begins=%1 ends=%2` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T233 | `src/workspace/search/search_selftest.cpp:322` | non-UI | **不译** | `binary skipped` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T234 | `src/editor/kernel/fold_breadcrumb.cpp:21` | non-UI | **不译** | `breadcrumbDebounce` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T235 | `src/workspace/diagnostics/toolchain_ux_selftest.cpp:283` | non-UI | **不译** | `build/slang/out/slang not built here` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T236 | `src/editor/kernel/edit_core_selftest.cpp:511` | non-UI | **不译** | `built without the src/app EditorProvider seam` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T237 | `src/app/crash_reporter_selftest.cpp:110` | non-UI | **不译** | `canary segv-canary-42` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T238 | `src/workspace/project/project_state.cpp:164` | UI | **译** | `cannot commit %1: %2` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T239 | `src/app/session_store.cpp:73` | UI | **译** | `cannot commit session %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T240 | `src/app/command_registry.cpp:106` | UI | **译** | `cannot commit shortcut store %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T241 | `src/workspace/project/project_state.cpp:150` | UI | **译** | `cannot create %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T242 | `src/editor/kernel/edit_core_selftest.cpp:493` | non-UI | **不译** | `cannot create a temporary directory` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T243 | `src/workspace/palette/palette_bench.cpp:96` | non-UI | **不译** | `cannot create directory for %1` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T244 | `src/app/session_store.cpp:59` | UI | **译** | `cannot create session directory %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T245 | `src/app/command_registry.cpp:84` | UI | **译** | `cannot create shortcut store directory %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T246 | `src/workspace/search/global_search.cpp:142` | UI | **译** | `cannot create the Rust search service` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T247 | `src/syntax/folding/tests/syntax_directory_tests.cpp:84` | non-UI | **不译** | `cannot create the temporary directory` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T248 | `src/editor/kernel/atomic_save.cpp:93` | UI | **译** | `cannot create the temporary file %1: %2` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T249 | `src/workspace/search/search_jump_target.cpp:67` | UI | **译** | `cannot open %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T250 | `src/syntax/folding/ksyntax_fold_source.cpp:51` | UI | **译** | `cannot open %1: %2` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T251 | `src/editor/kernel/document_io.cpp:42` | UI | **译** | `cannot open %1: it looks like a binary file` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T252 | `src/editor/kernel/ktexteditor_provider.cpp:122` | UI | **译** | `cannot open %1: not a regular file` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T253 | `src/editor/kernel/atomic_save.cpp:153` | UI | **译** | `cannot open the directory for fsync: %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T254 | `src/syntax/folding/ksyntax_fold_source.cpp:90` | UI | **译** | `cannot parse %1: %2` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T255 | `src/editor/kernel/document_io.cpp:35` | UI | **译** | `cannot read %1: %2` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T256 | `src/app/session_store.cpp:39` | UI | **译** | `cannot read session %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T257 | `src/app/command_registry.cpp:34` | UI | **译** | `cannot read shortcut store %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T258 | `src/editor/kernel/atomic_save.cpp:68` | UI | **译** | `cannot save %1: directory %2 does not exist` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T259 | `src/editor/kernel/atomic_save.cpp:72` | UI | **译** | `cannot save %1: directory %2 is not writable` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T260 | `src/workspace/project/project_state.cpp:157` | UI | **译** | `cannot write %1: %2` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T261 | `src/app/session_store.cpp:66` | UI | **译** | `cannot write session %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T262 | `src/app/command_registry.cpp:99` | UI | **译** | `cannot write shortcut store %1` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T263 | `src/workspace/project/project_state.cpp:50` | non-UI | **不译** | `capturedAtUtc` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T264 | `src/workspace/settings/settings_panel.cpp:486` | non-UI | **不译** | `color: #a33; font-weight: bold;` | R08 Qt 样式表片段（CSS 属性）（非 UI 表面，不进入 .ts） |
| T265 | `src/workspace/filetree/file_tree_widget.cpp:653` | non-UI | **不译** | `color: #b3261e;` | R08 Qt 样式表片段（CSS 属性）（非 UI 表面，不进入 .ts） |
| T266 | `src/app/main.cpp:788` | non-UI | **不译** | `ctrlSAuthorityGuard` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T267 | `src/editor/kernel/highlight_diagnosis.cpp:162` | UI | **译** | `definitionForFileName(top.sv): %1` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T268 | `src/editor/kernel/highlight_diagnosis.cpp:160` | UI | **译** | `definitionForFileName(top.v): %1` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T269 | `src/editor/kernel/highlight_diagnosis.cpp:159` | UI | **译** | `definitionForName(SystemVerilog): %1` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T270 | `src/editor/kernel/highlight_diagnosis.cpp:158` | UI | **译** | `definitionForName(Verilog): %1` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T271 | `src/app/editor_provider.cpp:23` | UI | **译** | `demo editor (shell placeholder)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T272 | `src/app/main.cpp:959` | non-UI | **不译** | `describe='%1' bar='%2'` | R09 机器可读诊断模板（key=%1 …），供日志/门禁解析（非 UI 表面，不进入 .ts） |
| T273 | `src/syntax/kde/ksyntax_data.cpp:159` | non-UI | **不译** | `development fallback syntax path added: %1` | R20 开发/故障注入诊断文本（fault-injection、selftest、probe）（非 UI 表面，不进入 .ts） |
| T274 | `src/workspace/diagnostics/diagnostics_panel.cpp:40` | non-UI | **不译** | `diagnosticsEmptyState` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T275 | `src/workspace/diagnostics/diagnostics_panel.cpp:31` | non-UI | **不译** | `diagnosticsOutline` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T276 | `src/workspace/diagnostics/diagnostics_panel.cpp:25` | non-UI | **不译** | `diagnosticsStatus` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T277 | `src/editor/kernel/atomic_save.cpp:142` | UI | **译** | `directory fsync not supported here (%1); the rename is complete` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T278 | `src/editor/kernel/atomic_save.cpp:138` | non-UI | **不译** | `directory fsync skipped by fault injection` | R20 开发/故障注入诊断文本（fault-injection、selftest、probe）（非 UI 表面，不进入 .ts） |
| T279 | `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:202` | non-UI | **不译** | `disk=%1 bytes` | R09 机器可读诊断模板（key=%1 …），供日志/门禁解析（非 UI 表面，不进入 .ts） |
| T280 | `src/workspace/diagnostics/diagnostics_ffi_selftest.cpp:277` | non-UI | **不译** | `disk=%1 bytes, buffer=%2 bytes` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T281 | `src/app/app_shell.cpp:474` | UI | **译** | `dock layout could not be restored (KTextEditor/version mismatch?)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T282 | `src/app/app_shell.cpp:472` | non-UI | **不译** | `dockState` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T283 | `src/workspace/filetree/tests/filetree_tests.cpp:164` | non-UI | **不译** | `does not exist` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T284 | `src/workspace/diagnostics/diagnostics_panel.cpp:116` | UI | **译** | `editor buffer, not saved yet` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T285 | `src/app/app_shell.cpp:481` | UI | **译** | `editor layout: %1` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T286 | `src/app/app_shell.cpp:477` | non-UI | **不译** | `editorLayout` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T287 | `src/editor/kernel/highlight_diagnosis.cpp:149` | non-UI | **不译** | `editor_repository_definitions=%1 custom_paths=%2` | R09 机器可读诊断模板（key=%1 …），供日志/门禁解析（非 UI 表面，不进入 .ts） |
| T288 | `src/workspace/search/global_search.cpp:135` | non-UI | **不译** | `empty query` | R11 Verilog/SystemVerilog 代码片段或实例化文本（测试夹具/示例）（非 UI 表面，不进入 .ts） |
| T289 | `src/workspace/settings/settings_selftest.cpp:312` | non-UI | **不译** | `entries=%1 delivered=%2` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T290 | `src/workspace/diagnostics/diagnostics_ffi_selftest.cpp:134` | non-UI | **不译** | `expected/files.txt -> %1 file(s)` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T291 | `src/workspace/diagnostics/diagnostics_selftest.cpp:163` | non-UI | **不译** | `fake LSP cannot follow buffers` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T292 | `src/workspace/diagnostics/diagnostics_selftest.cpp:222` | non-UI | **不译** | `fake LSP diagnostic` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T293 | `src/workspace/diagnostics/diagnostics_selftest.cpp:88` | non-UI | **不译** | `fake LSP refused to start (missing binary)` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T294 | `src/workspace/diagnostics/diagnostics_selftest.cpp:105` | non-UI | **不译** | `fake session ready` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T295 | `src/workspace/diagnostics/diagnostics_selftest.cpp:85` | non-UI | **不译** | `fake session starting` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T296 | `src/workspace/diagnostics/diagnostics_selftest.cpp:242` | non-UI | **不译** | `fake slang` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T297 | `src/workspace/diagnostics/diagnostics_selftest.cpp:254` | non-UI | **不译** | `fake slang binary is missing` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T298 | `src/workspace/diagnostics/diagnostics_selftest.cpp:280` | non-UI | **不译** | `fake slang diagnostic` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T299 | `src/workspace/diagnostics/diagnostics_selftest.cpp:248` | non-UI | **不译** | `fake slang is unavailable` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T300 | `src/syntax/kde/tests/validate_syntax_data.cpp:180` | non-UI | **不译** | `falling back to Verilog` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T301 | `src/workspace/filetree/file_tree_widget.cpp:105` | non-UI | **不译** | `fileTreeEmptyHint` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T302 | `src/workspace/filetree/file_tree_widget.cpp:117` | non-UI | **不译** | `fileTreeEmptyState` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T303 | `src/workspace/filetree/file_tree_widget.cpp:129` | non-UI | **不译** | `fileTreeStatus` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T304 | `src/workspace/filetree/file_tree_widget.cpp:46` | non-UI | **不译** | `fileTreeToolBar` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T305 | `src/workspace/filetree/file_tree_widget.cpp:92` | non-UI | **不译** | `fileTreeView` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T306 | `src/syntax/kde/tests/validate_syntax_data.cpp:140` | non-UI | **不译** | `filename probe -> %1 (expected the known V/vlang trap); explicit name mapping is used instead` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T307 | `src/editor/kernel/find_replace.cpp:41` | UI | **译** | `find: no document` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T308 | `src/editor/kernel/find_replace.cpp:45` | UI | **译** | `find: the pattern is empty` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T309 | `src/syntax/folding/ksyntax_fold_source.cpp:117` | UI | **译** | `fold lists of region %1 are empty` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T310 | `src/editor/probes/replace_probe2.cpp:109` | non-UI | **不译** | `foo bar foo` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T311 | `src/editor/probes/replace_probe2.cpp:111` | non-UI | **不译** | `foobarbaz bar foobarbaz` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T312 | `src/editor/kernel/edit_core_selftest.cpp:216` | non-UI | **不译** | `found=%1 replaced=%2` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T313 | `src/editor/kernel/goto_line.cpp:37` | UI | **译** | `goto-line: cursor landed at %1:%2 instead of %3:0` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T314 | `src/editor/kernel/goto_line.cpp:12` | UI | **译** | `goto-line: no view/document` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T315 | `src/editor/kernel/goto_line.cpp:31` | UI | **译** | `goto-line: setCursorPosition(%1) failed` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T316 | `src/editor/kernel/goto_line.cpp:18` | UI | **译** | `goto-line: the document has no lines` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T317 | `src/syntax/folding/syntax_fold_source.cpp:38` | UI | **译** | `grammar '%1' is not registered` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T318 | `src/workspace/project/project_state.cpp:52` | non-UI | **不译** | `includeDirs` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T319 | `src/workspace/settings/settings_selftest.cpp:238` | non-UI | **不译** | `invalid TOML` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T320 | `src/app/session_store.cpp:47` | UI | **译** | `invalid session %1: %2` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T321 | `src/app/command_registry.cpp:42` | UI | **译** | `invalid shortcut store %1: %2` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T322 | `src/app/editor_area.cpp:864` | UI | **译** | `layout has no root object` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T323 | `src/app/editor_area.cpp:877` | UI | **译** | `layout parse failed` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T324 | `src/app/editor_area.cpp:796` | UI | **译** | `leaf node without pane id` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T325 | `src/syntax/folding/ksyntax_fold_source.cpp:121` | non-UI | **不译** | `loaded %1 (%2) region=%3 begin=%4 end=%5` | R09 机器可读诊断模板（key=%1 …），供日志/门禁解析（非 UI 表面，不进入 .ts） |
| T326 | `src/workspace/diagnostics/diagnostics_selftest.cpp:341` | non-UI | **不译** | `lsp=%1 slang=%2/%3` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T327 | `src/app/main.cpp:951` | non-UI | **不译** | `m0 > inc` | R22 只出现在自检断言/输出调用点（self-check expectation）（非 UI 表面，不进入 .ts） |
| T328 | `src/editor/kernel/edit_core_selftest.cpp:289` | non-UI | **不译** | `mismatches=%1 error=%2` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T329 | `src/workspace/diagnostics/diagnostics_selftest.cpp:493` | non-UI | **不译** | `missing=[verible, slang]` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T330 | `src/editor/kernel/edit_core_selftest.cpp:464` | non-UI | **不译** | `mode='%1' expected='%2'` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T331 | `src/editor/fim/fim_selftest.cpp:312` | non-UI | **不译** | `module m;` | R11 Verilog/SystemVerilog 代码片段或实例化文本（测试夹具/示例）（非 UI 表面，不进入 .ts） |
| T332 | `src/app/main.cpp:948` | non-UI | **不译** | `module:m0 > function:inc` | R11 Verilog/SystemVerilog 代码片段或实例化文本（测试夹具/示例）（非 UI 表面，不进入 .ts） |
| T333 | `src/editor/kernel/multi_cursor.cpp:105` | UI | **译** | `multi-cursor: View::insertText() failed` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T334 | `src/editor/kernel/multi_cursor.cpp:49` | UI | **译** | `multi-cursor: asked for %1 cursors, the view reports %2` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T335 | `src/editor/kernel/multi_cursor.cpp:87` | UI | **译** | `multi-cursor: inserted at %1 of %2 cursors` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T336 | `src/editor/kernel/multi_cursor.cpp:69` | UI | **译** | `multi-cursor: no document` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T337 | `src/editor/kernel/multi_cursor.cpp:42` | UI | **译** | `multi-cursor: no positions given` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T338 | `src/editor/kernel/multi_cursor.cpp:38` | UI | **译** | `multi-cursor: no view` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T339 | `src/editor/kernel/highlight_diagnosis.cpp:65` | non-UI | **不译** | `name='%1' author='%2' section='%3' version=%4 valid=%5` | R09 机器可读诊断模板（key=%1 …），供日志/门禁解析（非 UI 表面，不进入 .ts） |
| T340 | `src/app/editor_area.cpp:853` | non-UI | **不译** | `nextPane` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T341 | `src/editor/kernel/edit_core_selftest.cpp:390` | non-UI | **不译** | `no C ABI hooks in this build` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T342 | `src/editor/kernel/edit_core_selftest.cpp:234` | non-UI | **不译** | `no KTextEditor view` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T343 | `src/workspace/diagnostics/diagnostics_controller.cpp:170` | UI | **译** | `no LSP backend in this build` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T344 | `src/workspace/diagnostics/ffi_backends.cpp:280` | UI | **译** | `no LSP request to poll` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T345 | `src/workspace/diagnostics/ffi_backends.cpp:200` | UI | **译** | `no LSP session` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T346 | `src/workspace/diagnostics/diagnostics_controller.cpp:302` | UI | **译** | `no SLANG backend` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T347 | `src/workspace/diagnostics/diagnostics_controller.cpp:206` | UI | **译** | `no SLANG backend in this build` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T348 | `src/workspace/diagnostics/ffi_backends.cpp:396` | UI | **译** | `no SLANG ticket to poll` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T349 | `src/app/app_shell.cpp:539` | UI | **译** | `no active editor pane` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T350 | `src/workspace/search/search_jump_target.cpp:43` | non-UI | **不译** | `no application shell` | R22 只出现在自检断言/输出调用点（self-check expectation）（非 UI 表面，不进入 .ts） |
| T351 | `src/workspace/diagnostics/diagnostics_panel.cpp:96` | UI | **译** | `no diagnostics controller` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T352 | `src/editor/kernel/edit_core_selftest.cpp:187` | non-UI | **不译** | `no document` | R11 Verilog/SystemVerilog 代码片段或实例化文本（测试夹具/示例）（非 UI 表面，不进入 .ts） |
| T353 | `src/editor/kernel/highlight_mode.cpp:102` | UI | **译** | `no document to highlight` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T354 | `src/workspace/search/search_jump_target.cpp:51` | non-UI | **不译** | `no edit-core provider: the search jump needs the KTextEditor editor lane` | R22 只出现在自检断言/输出调用点（self-check expectation）（非 UI 表面，不进入 .ts） |
| T355 | `src/workspace/diagnostics/diagnostics_controller.cpp:95` | UI | **译** | `no effective file set` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T356 | `src/workspace/diagnostics/toolchain_ux_selftest.cpp:312` | non-UI | **不译** | `no local verible-verilog-ls / fixture` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T357 | `src/workspace/diagnostics/diagnostics_controller.cpp:444` | UI | **译** | `no navigation request in flight` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T358 | `src/editor/kernel/edit_core_selftest.cpp:417` | non-UI | **不译** | `no positive-control hook` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T359 | `src/app/app_shell.cpp:414` | non-UI | **不译** | `no project` | R11 Verilog/SystemVerilog 代码片段或实例化文本（测试夹具/示例）（非 UI 表面，不进入 .ts） |
| T360 | `src/workspace/project/project_model.cpp:81` | UI | **译** | `no project is open` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T361 | `src/app/editor_area.cpp:336` | obsolete | **删除** | `no provider` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/app/editor_area.cpp；当前树中已无该字面量，无需代码动作 |
| T362 | `src/workspace/breadcrumb/syntax_breadcrumb_source.cpp:25` | UI | **译** | `no provider for grammar '%1'` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T363 | `src/app/command_registry.cpp:253` | UI | **译** | `no shortcut store configured` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T364 | `src/workspace/project/project_state.cpp:111` | UI | **译** | `no state directory is available` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T365 | `src/editor/kernel/highlight_mode.cpp:109` | UI | **译** | `no syntax definition for %1` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T366 | `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:109` | UI | **译** | `no temporary directory` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T367 | `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp:164` | non-UI | **不译** | `no toolchain` | R11 Verilog/SystemVerilog 代码片段或实例化文本（测试夹具/示例）（非 UI 表面，不进入 .ts） |
| T368 | `src/workspace/palette/file_index.cpp:82` | UI | **译** | `no workspace root configured` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T369 | `src/workspace/vcs/vcs_selftest.cpp:111` | UI | **译** | `not a Git or Subversion working tree` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T370 | `src/workspace/filetree/tests/filetree_tests.cpp:172` | non-UI | **不译** | `not a folder` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T371 | `src/workspace/search/search_selftest.cpp:428` | non-UI | **不译** | `not readable` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T372 | `src/workspace/diagnostics/diagnostics_selftest.cpp:546` | non-UI | **不译** | `not saved` | R11 Verilog/SystemVerilog 代码片段或实例化文本（测试夹具/示例）（非 UI 表面，不进入 .ts） |
| T373 | `src/app/editor_area.cpp:239` | UI | **译** | `open: the pane has no editor provider` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T374 | `src/editor/kernel/document_io.cpp:21` | UI | **译** | `openFile: no document` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T375 | `src/workspace/filetree/filetree_integration.cpp:52` | UI | **译** | `openFileInEditor: the application shell is not part of this build` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T376 | `src/workspace/palette/palette_integration.cpp:151` | non-UI | **不译** | `palette: cannot index workspace: %1` | R05 只出现在 qDebug/qInfo/qWarning/printf 日志调用点（非 UI 表面，不进入 .ts） |
| T377 | `src/workspace/palette/palette_integration.cpp:155` | non-UI | **不译** | `palette: indexed %1 files under %2 in %3 ms` | R05 只出现在 qDebug/qInfo/qWarning/printf 日志调用点（非 UI 表面，不进入 .ts） |
| T378 | `src/app/editor_area.cpp:802` | non-UI | **不译** | `pane %1 was saved by provider '%2', restored with '%3'` | R22 只出现在自检断言/输出调用点（self-check expectation）（非 UI 表面，不进入 .ts） |
| T379 | `src/workspace/breadcrumb/syntax_breadcrumb_source.cpp:56` | UI | **译** | `parse returned no tree` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T380 | `src/workspace/filetree/tests/filetree_tests.cpp:196` | non-UI | **不译** | `permission denied` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T381 | `src/workspace/filetree/file_tree_model.cpp:420` | UI | **译** | `permission denied: %1` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T382 | `src/syntax/kde/tests/syntax_directory_equivalence.cpp:94` | non-UI | **不译** | `private syntax directory missing` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T383 | `src/syntax/kde/ksyntax_data.cpp:111` | UI | **译** | `private syntax directory missing: %1/syntax (set VEDIT_SYNTAX_DIR)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T384 | `src/workspace/project/project_state.cpp:49` | non-UI | **不译** | `projectRoot` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T385 | `src/syntax/folding/syntax_fold_source.cpp:63` | UI | **译** | `provider '%1' parsed %2 fold ranges` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T386 | `src/workspace/diagnostics/diagnostics_ffi_selftest.cpp:293` | non-UI | **不译** | `remaining errors=%1` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T387 | `src/app/main.cpp:784` | non-UI | **不译** | `removed=%1 bindings=%2` | R09 机器可读诊断模板（key=%1 …），供日志/门禁解析（非 UI 表面，不进入 .ts） |
| T388 | `src/workspace/diagnostics/diagnostics_controller.cpp:101` | UI | **译** | `reparsing %1 file(s)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T389 | `src/editor/kernel/find_replace.cpp:121` | UI | **译** | `replace-all replaced %1 of %2 matches` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T390 | `src/editor/kernel/highlight_diagnosis.cpp:153` | UI | **译** | `repository-definition %1` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T391 | `src/workspace/diagnostics/diagnostics_ffi_selftest.cpp:161` | non-UI | **不译** | `requested=%1 completed=%2 failures=%3` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T392 | `src/workspace/project/project_selftest.cpp:155` | obsolete | **删除** | `restored from last session` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/workspace/project/project_selftest.cpp；当前树中已无该字面量，无需代码动作 |
| T393 | `src/workspace/search/search_selftest.cpp:483` | non-UI | **不译** | `result limit 3 reached` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T394 | `src/workspace/diagnostics/toolchain_ux_selftest.cpp:291` | non-UI | **不译** | `run scripts/fetch-verible.sh in a checkout to enable this` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T395 | `src/editor/kernel/ktexteditor_provider.cpp:211` | UI | **译** | `save: the document has no file path yet` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T396 | `src/app/editor_area.cpp:261` | obsolete | **删除** | `save: the pane has no editor provider` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/app/editor_area.cpp；当前树中已无该字面量，无需代码动作 |
| T397 | `src/editor/kernel/document_io.cpp:170` | UI | **译** | `saveAtomic: the document has no file path` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T398 | `src/workspace/diagnostics/diagnostics_panel.cpp:117` | non-UI | **不译** | `saved file` | R11 Verilog/SystemVerilog 代码片段或实例化文本（测试夹具/示例）（非 UI 表面，不进入 .ts） |
| T399 | `src/app/main.cpp:610` | UI | **译** | `search panel from a registered factory` | R15 出现在 Qt UI 调用点（ui_text）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T400 | `src/workspace/search/global_search.cpp:126` | UI | **译** | `search root does not exist or is not a directory: %1` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T401 | `src/workspace/search/global_search.cpp:131` | UI | **译** | `search root is not readable: %1` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T402 | `src/workspace/project/project_model.cpp:139` | non-UI | **不译** | `service: %1` | R22 只出现在自检断言/输出调用点（self-check expectation）（非 UI 表面，不进入 .ts） |
| T403 | `src/app/app_shell.cpp:406` | UI | **译** | `session mentions unknown panel '%1'` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T404 | `src/workspace/diagnostics/diagnostics_controller.cpp:188` | UI | **译** | `session started over %1 file(s)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T405 | `src/workspace/search/search_jump_target.cpp:89` | UI | **译** | `setCursorPosition(%1:%2) failed` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T406 | `src/editor/kernel/document_io.cpp:103` | UI | **译** | `setText: no document` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T407 | `src/workspace/settings/settings_panel.cpp:143` | UI | **译** | `settings could not be loaded` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T408 | `src/workspace/settings/settings_client.cpp:198` | UI | **译** | `settings load failed (state %1)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T409 | `src/workspace/settings/settings_client.cpp:222` | UI | **译** | `settings save failed (state %1)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T410 | `src/app/crash_reporter_selftest.cpp:104` | non-UI | **不译** | `signal=11 (SIGSEGV)` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T411 | `src/workspace/diagnostics/ffi_backends.cpp:361` | obsolete | **删除** | `slang (FFI)` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/workspace/diagnostics/ffi_backends.cpp；当前树中已无该字面量，无需代码动作 |
| T412 | `src/workspace/diagnostics/ffi_backends.cpp:362` | obsolete | **删除** | `slang (FFI): %1` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/workspace/diagnostics/ffi_backends.cpp；当前树中已无该字面量，无需代码动作 |
| T413 | `src/workspace/diagnostics/toolchain.cpp:249` | non-UI | **不译** | `slang probed: %1` | R20 开发/故障注入诊断文本（fault-injection、selftest、probe）（非 UI 表面，不进入 .ts） |
| T414 | `src/syntax/folding/ksyntax_fold_source.cpp:137` | UI | **译** | `source is not loaded: %1` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T415 | `src/workspace/diagnostics/toolchain.cpp:199` | non-UI | **不译** | `source=verible:%1 slang:%2 missing=%3` | R09 机器可读诊断模板（key=%1 …），供日志/门禁解析（非 UI 表面，不进入 .ts） |
| T416 | `src/app/editor_area.cpp:824` | UI | **译** | `split node needs two object children` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T417 | `src/app/editor_area.cpp:816` | UI | **译** | `split node without a valid orientation` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T418 | `src/editor/kernel/fim_wiring_selftest.cpp:399` | non-UI | **不译** | `start took %1 ms (the endpoint sleeps 400 ms)` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T419 | `src/workspace/settings/settings_selftest.cpp:292` | non-UI | **不译** | `start took %1 ms for a %2 KB store` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T420 | `src/workspace/vcs/vcs_selftest.cpp:318` | non-UI | **不译** | `start took %1 ms while the fake git sleeps 400 ms` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T421 | `src/workspace/project/project_model.cpp:136` | non-UI | **不译** | `state: %1` | R22 只出现在自检断言/输出调用点（self-check expectation）（非 UI 表面，不进入 .ts） |
| T422 | `src/workspace/vcs/vcs_selftest.cpp:158` | non-UI | **不译** | `stub has no diff` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T423 | `src/workspace/filetree/file_ops.cpp:155` | uncertain | **待定→保留英文** | `target: %1` | R25 无决定信号：既不像代码/键，也没有 UI 调用点；保留英文（AC-4.5 默认口径：未判定 → keep-en + 登记理由） |
| T424 | `src/workspace/diagnostics/diagnostics_controller.cpp:283` | UI | **译** | `the LSP session did not start` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T425 | `src/workspace/project/project_service.cpp:89` | UI | **译** | `the Rust project service is unavailable` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T426 | `src/workspace/search/global_search.cpp:28` | UI | **译** | `the Rust search service did not report a reason` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T427 | `src/workspace/diagnostics/ffi_backends.cpp:163` | UI | **译** | `the Rust service handle could not be created` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T428 | `src/editor/fim/fim_backend.cpp:24` | UI | **译** | `the Rust service reported no detail` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T429 | `src/workspace/settings/settings_client.cpp:125` | UI | **译** | `the Rust settings store rejected the path` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T430 | `src/workspace/settings/settings_client.cpp:165` | UI | **译** | `the Rust settings store rejected the write` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T431 | `src/workspace/diagnostics/ffi_backends.cpp:386` | UI | **译** | `the SLANG job id does not fit the backend ticket type` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T432 | `src/app/crash_reporter_selftest.cpp:114` | non-UI | **不译** | `the backtrace must name the crash handler` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T433 | `src/workspace/search/search_jump_target.cpp:98` | non-UI | **不译** | `the cursor landed at %1:%2 instead of %3:%4` | R22 只出现在自检断言/输出调用点（self-check expectation）（非 UI 表面，不进入 .ts） |
| T434 | `src/editor/kernel/highlight_mode.cpp:137` | UI | **译** | `the document reports highlighting mode '%1' instead of '%2'` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T435 | `src/app/app_shell.cpp:545` | UI | **译** | `the editor provider refused the save` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T436 | `src/workspace/filetree/file_ops.cpp:25` | non-UI | **不译** | `the entry` | R11 Verilog/SystemVerilog 代码片段或实例化文本（测试夹具/示例）（非 UI 表面，不进入 .ts） |
| T437 | `src/workspace/filetree/file_ops.cpp:33` | UI | **译** | `the folder '%1' is not writable` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T438 | `src/workspace/filetree/file_ops.cpp:194` | UI | **译** | `the folder is not empty` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T439 | `src/workspace/diagnostics/diagnostics_ffi_selftest.cpp:107` | non-UI | **不译** | `the navigation request did not settle in 20 s` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T440 | `src/workspace/search/search_jump_target.cpp:82` | non-UI | **不译** | `the opened document has no lines` | R22 只出现在自检断言/输出调用点（self-check expectation）（非 UI 表面，不进入 .ts） |
| T441 | `src/workspace/project/project_model.cpp:228` | UI | **译** | `the project job was rejected by the service` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T442 | `src/workspace/project/project_model.cpp:206` | UI | **译** | `the project service did not answer in %1 ms` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T443 | `src/workspace/project/project_model.cpp:254` | UI | **译** | `the project service failed` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T444 | `src/workspace/search/search_jump_target.cpp:56` | non-UI | **不译** | `the shell has no active editor pane` | R22 只出现在自检断言/输出调用点（self-check expectation）（非 UI 表面，不进入 .ts） |
| T445 | `src/app/app_shell.cpp:428` | UI | **译** | `theme fallback: %1` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T446 | `src/app/app_shell.cpp:425` | uncertain | **待定→保留英文** | `theme: %1` | R25 无决定信号：既不像代码/键，也没有 UI 调用点；保留英文（AC-4.5 默认口径：未判定 → keep-en + 登记理由） |
| T447 | `src/workspace/diagnostics/toolchain_ux_selftest.cpp:342` | non-UI | **不译** | `this build has no Rust service layer (FFI)` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T448 | `src/workspace/settings/settings_client.cpp:126` | UI | **译** | `this build has no vd-ffi settings ABI` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T449 | `src/workspace/settings/settings_selftest.cpp:307` | non-UI | **不译** | `ticks=%1 poles=%2` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T450 | `src/workspace/vcs/vcs_selftest.cpp:356` | non-UI | **不译** | `timer fired at %1 ms after scheduling (expected late)` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T451 | `src/workspace/search/search_selftest.cpp:323` | non-UI | **不译** | `too large skipped` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T452 | `src/syntax/folding/syntax_fold_source.cpp:31` | UI | **译** | `tree-sitter switch is off` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T453 | `src/editor/fim/fim_backend.cpp:177` | UI | **译** | `unavailable (no Rust service in this build)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T454 | `src/workspace/diagnostics/diagnostics_selftest.cpp:181` | non-UI | **不译** | `unknown fake request id` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T455 | `src/workspace/diagnostics/diagnostics_selftest.cpp:266` | non-UI | **不译** | `unknown fake slang ticket` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T456 | `src/app/editor_area.cpp:844` | UI | **译** | `unknown layout node type '%1'` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T457 | `src/app/theme_manager.cpp:55` | UI | **译** | `unknown theme '%1' (or not loaded by KSyntaxHighlighting)` | R17 人类可读的消息/标签，位于交付源文件（间接显示或由上层弹出）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T458 | `src/workspace/search/search_selftest.cpp:459` | non-UI | **不译** | `unreadable skipped` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T459 | `src/syntax/folding/ksyntax_fold_source.cpp:104` | non-UI | **不译** | `veditFold` | R06 无空白的标识符形状：Qt objectName / JSON 键 / 命令 id（非 UI 表面，不进入 .ts） |
| T460 | `src/workspace/diagnostics/toolchain.cpp:247` | non-UI | **不译** | `verible-verilog-ls probed: %1` | R20 开发/故障注入诊断文本（fault-injection、selftest、probe）（非 UI 表面，不进入 .ts） |
| T461 | `src/editor/probes/tx_steps_probe5.cpp:40` | non-UI | **不译** | `w%1 ` | R04 只出现在开发自检/探针/基准文件里（_selftest.cpp、tests/、probes/），不是交付 UI 表面（非 UI 表面，不进入 .ts） |
| T462 | `src/app/app_shell.cpp:494` | UI | **译** | `window geometry could not be restored` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T463 | `src/workspace/palette/file_index.cpp:89` | UI | **译** | `workspace root is not a directory: %1` | R23 英文消息/状态文本（服务层以 message/error 字段传到界面，显示点未逐条证明）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T464 | `src/workspace/project/project_selftest.cpp:190` | obsolete | **删除** | `{ this is not json` | R03 当前树里已不存在该字面量（注释中残留或已删除/改写）；01 出处 src/workspace/project/project_selftest.cpp；当前树中已无该字面量，无需代码动作 |
| T465 | `src/workspace/vcs/vcs_panel.cpp:109` | UI | **译** | `· %1 changed file(s)` | R18 括号/省略号修饰的界面标签（附加在状态文本上）；译文按 02-bilingual-table-draft.md + 03-glossary.md 口径，落点见 12-ui-blocks.md |
| T466 | `src/editor/fim/fim_selftest.cpp:302` | already-translated | **已译** | `未链接 Rust 服务` | R02 字符串本身已是中文（CJK 文本），不需要翻译；源串本身是中文（另一 lane 已落），英文界面下仍显示中文 —— 登记为已知缺陷，不在 AC-4.5/4.6 写域内 |
