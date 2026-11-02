# T90/F-2 — 运行期快捷键重灌：选择选项 A（低成本运行期兜底 + 可无头断言的自动清除）

collected_at: 2026-11-01T10:42:00+08:00
git_head: 95c496e682e3841add1a081edd78d29c34957aac

## 结论（选择与理由）

**选 A**：不改判据、不静默放过，在 shell 层加一个**低成本运行期兜底**，并把
“枚举发现 >1 → 自动清除”写成无头断言。

理由：

- T80 的不变量只在 **pane 创建 / Show / FocusIn / 事件循环下一拍** 施加。KTextEditor
  （或 KDE 系统设置里的 KStandardShortcut 重新 apply）可以在**运行期**把编辑器自己的
  `KStandardAction::save`（Ctrl+S）重新挂回来；那之后又一次出现两个 Ctrl+S，用户在真实机器上
  会重新遇到 “Ambiguous shortcut overload”（两个都不触发）。这个窗口此前**没有任何测试**。
- 兜底成本很低：复用 T80 已有的 `clearEditorSaveShortcut()`（唯一实现，不新增第二套清除逻辑），
  只增加两个触发点 —— `QEvent::ApplicationActivate` / `QEvent::WindowActivate`（设置驱动的重灌
  被应用时最可能出现的时刻）与一个 2 秒的低频 `QTimer`（安全网）。两者调用**同一个入口**
  `EditorArea::enforceSingleCtrlSAuthority()`，返回清除条数。
- 不改快捷键判据本身：shell 的 `file.save` 仍是唯一权威，编辑器侧只移除重复的**按键绑定**，
  KTextEditor 自己的菜单动作仍然存在、可点（与 T80 的语义一致）。

## 改了什么（写域内 `src/app/**`）

| 文件 | 改动 |
|---|---|
| `src/app/editor_area.h` | `EditorPane::enforceSingleCtrlSAuthority()`、`EditorArea::enforceSingleCtrlSAuthority()`（返回清除条数）、`EditorArea::eventFilter` 覆盖、`QTimer *shortcutGuard_` |
| `src/app/editor_area.cpp` | `clearEditorSaveShortcut()` 改为返回清除条数（唯一清除实现）；`EditorArea` 构造时创建 `ctrlSAuthorityGuard` 定时器（2000 ms，`QObject::findChild` 可查，objectName 固定）并把 `EditorArea` 安装为 `QCoreApplication` 的应用级事件过滤器；`ApplicationActivate`/`WindowActivate` 时与定时器到点时都调用同一入口；析构时停表并移除过滤器 |
| `src/app/main.cpp` | `--selfcheck` 新增 6 条断言（见下）：运行期注入一个 Ctrl+S 绑定 → 枚举到 2 → 普通事件循环一拍**不会**被清（旧行为的暴露窗口）→ 发送 `ApplicationActivate` → 自动清除为 1 且权威仍是 shell 的 `file.save`；再注入一次 → 直接调用定时器所用的入口 → 清除 1；断言 `ctrlSAuthorityGuard` 定时器处于 active |

## 无头断言（红 → 绿，同一进程内可复跑）

`03-selfcheck-shortcut.log` 实测（`vedit-app --selfcheck`）：

```
CHECK save.shortcut.unique PASS bindings=1                       # T80 不变量仍成立
CHECK save.shortcut.authority PASS
CHECK save.shortcut.runtime-duplicate-enumerated PASS bindings=2 # 运行期注入成功（模拟重灌）
CHECK save.shortcut.runtime-unguarded-turn-keeps-duplicate PASS bindings=2
        ^ 红侧：没有新兜底时，事件循环一拍后重复绑定仍在 => 下一次 Ctrl+S 会再次歧义
CHECK save.shortcut.runtime-guard-clears PASS bindings=1         # 绿侧：ApplicationActivate 触发自动清除
CHECK save.shortcut.runtime-guard-authority PASS                # 唯一剩余 = shell 的 file.save
CHECK save.shortcut.runtime-guard-entry-point PASS removed=1 bindings=1
        ^ 定时器调用的同一入口确实清除了 1 条
CHECK save.shortcut.runtime-guard-timer PASS                    # 2 s 安全网定时器在跑
CHECK save.shortcut.unique.multi-pane PASS bindings=1
APP-SHELL-SELFTEST ok=1 checks=182 failures=0
```

说明：红侧不是“把实现删掉再跑”，而是**把旧行为的暴露窗口在同一测试里测出来**——
注入后仅跑一遍事件循环（不触发任何 T80 触发点、也还没到 2 s 定时器），重复绑定保持 2；随后
`ApplicationActivate` 一次即降为 1。这就是“重灌 → 歧义可复现”与“兜底 → 自动恢复”的对照。

## 已知边界（明写，不静默）

1. **最大暴露窗口**：事件驱动的清除在 `ApplicationActivate`/`WindowActivate` 时立即发生；
   2 s 定时器是安全网。若编辑器在**两次触发之间**（< 2 s）重灌并恰好此时按下 Ctrl+S，仍可能
   出现一次歧义；这是“低成本兜底”的边界（不做每按键轮询/不做全局 shortcut map 钩子）。
   该边界与 T80 的语义一致：**最终状态**始终收敛到唯一权威。
2. **只在 EditorArea 的 pane 上执行**：不触碰 KTextEditor 的内部动作集合删除（动作仍存在，
   仅移除按键绑定），也不改其它快捷键；KDE 的 `KStandardShortcut` 若重灌其它键，不在本任务范围。
3. 无显示服务器：以上为无头断言（枚举 QAction 的 shortcuts + 事件驱动清除），**真实键盘按下**
   的 GUI 观感仍属停点 1 的用户验收。
4. 复现/手工造重灌场景（给评审/用户）：运行产品 → 打开任意 `.v` → 在 KDE 系统设置里应用一次
   快捷键方案（或任何让 KTextEditor 重新 populate 动作集合的操作）→ 切回窗口
   （`ApplicationActivate`）→ 断言只会触发一次保存（`--selfcheck` 的注入断言就是这一行为的程序化版本）。
