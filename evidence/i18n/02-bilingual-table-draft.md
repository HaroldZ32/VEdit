# T136 — 中英对照表（草案 v0，待用户审）

collected_at: 2026-11-02T11:26:00+08:00
git_head: 见 `01-string-inventory.log`
## 状态与方法

- 全仓候选 466 条（`01-string-inventory.log`，含 file:line）。
- 本表先从 **`src/app/*.cpp` 中能直接验证出处的界面串**开始（5 条），其余为产品已知面板/菜单术语，
  标 `[待核出处]`，等 `tr()` 包装时逐条对照代码确认后再定稿；**定稿前不落代码**。

## 已验证出处（src/app）

| 英文（源串） | 中文 | 处理/备注 | 出处 |
|---|---|---|---|
| Saved | 已保存 | | app_shell.cpp:549 |
| Unsaved changes | 未保存的更改 | | app_shell.cpp:76 |
| Session saved (%1) | 会话已保存（%1） | 保留占位符 | app_shell.cpp:336 |
| Project: %1 | 项目：%1 | | app_shell.cpp:621 |
| Theme: %1 | 主题：%1 | | app_shell.cpp:625 |

## 面板 / 菜单术语（`[待核出处]`，先讨论译法）

| 英文 | 中文 | 备注 |
|---|---|---|
| Project | 项目 | |
| Files / File tree | 文件 / 文件树 | |
| Search | 搜索 | |
| Diagnostics | 诊断 | |
| Outline | 大纲 | 保留“大纲”，不要译成“轮廓” |
| VCS | 版本控制 | 面板标题用“版本控制”；缩写 VCS 保留在代码/日志 |
| Terminal | 终端 | |
| Editor | 编辑器 | |
| Open | 打开 | |
| Save | 保存 | |
| Save As | 另存为 | |
| Close | 关闭 | |
| Undo / Redo | 撤销 / 重做 | |
| Find / Replace | 查找 / 替换 | |
| Settings / Preferences | 设置 | 统一用“设置” |
| Interface language | 界面语言 | |
| Auto / English / 简体中文 | 自动 / English / 简体中文 | 语言名按惯例保留各自母语写法 |
| Restart to apply | 重启后生效 | |
| atomic save | atomic save | **保留英文**（专业术语） |
| lint | lint | **保留英文** |
| LSP | LSP | **保留英文** |
| outline | outline（面板名译“大纲”） | 术语保留英文，UI 标题用中文 |
| filelist | filelist | **保留英文** |
| Verilog / SystemVerilog | Verilog / SystemVerilog | 不译 |

## 待办

1. 完成 `tr()` 包装后，从 `.ts` 导出**完整**中英表（300–350 条，分域：菜单/面板/对话框/状态与错误），
   交用户审后再定稿 `.ts`；
2. 术语表以本表为准，`atomic save`/`lint`/`LSP`/`filelist` 保留英文。
