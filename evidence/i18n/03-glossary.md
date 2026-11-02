# T136 — 统一术语表（用户裁定 A：操作词翻中文、技术名词留英文）

collected_at: 2026-11-02T11:31:00+08:00
git_head: 见 `01-string-inventory.log`
判断标准：**用户要做的动作 → 中文；用户要搜的关键词 → 英文**。

## 操作词 / 界面元素 / 状态（翻中文）

| 英文 | 中文 |
|---|---|
| Open / Open File / Open Folder | 打开 / 打开文件 / 打开文件夹 |
| Save / Save As / Save All | 保存 / 另存为 / 全部保存 |
| Close / Close Pane / Close Tab | 关闭 / 关闭窗格 / 关闭标签页 |
| New / New File | 新建 / 新建文件 |
| Undo / Redo | 撤销 / 重做 |
| Cut / Copy / Paste | 剪切 / 复制 / 粘贴 |
| Find / Find Next / Find Previous / Replace | 查找 / 查找下一个 / 查找上一个 / 替换 |
| Go to Line / Go to Definition | 跳转到行 / 跳转到定义 |
| Split / Split Horizontal / Split Vertical | 分屏 / 水平分屏 / 垂直分屏 |
| Settings / Preferences | 设置 |
| Interface language | 界面语言 |
| Auto / English / 简体中文 | 自动 / English / 简体中文 |
| Restart to apply | 重启后生效 |
| Saved / Unsaved changes | 已保存 / 未保存的更改 |
| Session saved | 会话已保存 |
| No folder open | 未打开文件夹 |
| analysis starts automatically | 自动开始分析 |
| Start Analysis / Re-run Analysis | 开始分析 / 重新分析 |
| Reload / Refresh | 重新加载 / 刷新 |
| Show / Hide | 显示 / 隐藏 |
| Search in files | 在文件中搜索 |
| No results / No matches | 无结果 / 无匹配 |
| Loading… | 加载中… |
| Running… | 运行中… |
| Failed / Error / Warning | 失败 / 错误 / 警告 |

## 面板标题（翻中文）

| 英文 | 中文 |
|---|---|
| File Tree / Files | 文件树 / 文件 |
| Search | 搜索 |
| Diagnostics | 诊断 |
| Outline | 大纲 |
| Version Control (VCS) | 版本控制 |
| Terminal | 终端 |
| Problems | 问题 |
| Settings | 设置 |
| Editor | 编辑器 |

## 技术名词（保留英文原文）

`lint`、`LSP`、`filelist`、`atomic save`、`outline`（作技术概念时）、`Verilog`、`SystemVerilog`、
`verible`、`verible-verilog-ls`、`slang`、`KTextEditor`、`tree-sitter`、`KSyntaxHighlighting`、
`TOML`、`Git`、`SVN`、`AppImage`、`JSON`、`UTF-8`；文件名/路径/代码片段原样；
**诊断消息体（verible/slang 原文）不翻**。

## 边界示例（定稿口径）

| 源串 | 译法 |
|---|---|
| Run lint | 运行 lint |
| LSP server not available | LSP 服务器不可用 |
| Atomic save failed | atomic save 失败 |
| Show outline | 显示大纲 |
| Large file (56 MB): syntax-aware features limited | 大型文件（56 MB）：语法感知功能受限 |
| Opening big.sv (56 MB)… | 正在打开 big.sv（56 MB）… |
| No folder open — analysis starts automatically | 未打开文件夹 —— 自动开始分析 |

## 说明

- 本表是**术语与口径**；完整 300–350 条界面文本表要等 `tr()` 包装后从 `.ts` 导出再逐条定稿（下一步）。
- 拿不准的条目在完整表中标 **待定**，不猜测翻译。
