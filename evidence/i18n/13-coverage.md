# T21 — 覆盖率/计数汇总（AC-4.5 / 4.6 / 4.7 自查）

## 466 对账

- 行数：466（期望 466）
- UI: 215（期望 215）
- non-UI: 195（期望 195）
- already-translated: 31（期望 31）
- obsolete: 15（期望 15）
- uncertain: 10（期望 10）
- 决策分布：{'删除': 15, '译': 213, '不译': 197, '已译': 31, '待定→保留英文': 10}

## 215 UI 译文落盘

- UI 候选：215；决定译：213；决定不译：2
- 译项全部在 .ts：OK
- 译项全部有包装落点：OK

## translations/vedit_zh_CN.ts

- messages：391；有译文：290；未译：101（未译条目是非 UI 候选或 AC-4.5 判为不译的串）

## 上下文（lupdate 实际产出）

- `QObject`：3 条，已译 0
- `VEdit`：200 条，已译 200
- `vedit::app::ApplicationShell`：52 条，已译 52
- `vedit::editor::FindReplaceBar`：23 条，已译 0
- `vedit::workspace::FileTreeWidget`：15 条，已译 3
- `vedit::workspace::search::SearchPanel`：27 条，已译 1
- `vedit::workspace::settings::SettingsPanel`：71 条，已译 34
