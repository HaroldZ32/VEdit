# 更正（追加，不改历史）—— a27a4ee 的「同步 / 15s 冻结」已被 T106 解决

appended_by: Lead（T31 打包基线提交时）
generated: 2026-11-01T07:57:14Z
resolves_commit: a27a4ee（T104：把 FIM 接进编辑器内核）
resolution_commit: e6f55e5918051064264b72ca75c1ceba4a01b802（T106：FIM 异步化）

## 被更正的原文（a27a4ee 提交正文，一字未改）

> 已知代价（T104 如实上报）：`InlineCompletion::request()` 仍是同步，端点不可达时 GUI 最长冻结 15s —— T106 改成 job+轮询。

## 更正

该代价**已由 T106 消除**（`e6f55e5918051064264b72ca75c1ceba4a01b802`）：

- `Backend` 新增异步 seam：`start()` 只调 `vd_fim_complete` 并**立即返回 token**；`poll()` 只调**一次** `vd_fim_poll`、**绝不等待**；另有 `cancel()`。
- `fim_wiring` 用 **15 ms QTimer** 轮询，`Alt+\\` 立即返回。
- **超时 = 取消 + 状态句**（`FIM 超时（300 ms 无结果，已取消）`），不再是"界面卡 15 秒"。
- 同一时刻**只有一个 job**；新请求 / 打字 / 移动光标都会取消在飞 job。

## 实测（非阻塞断言 50 checks / 0 failures）

慢端点 400 ms + 黑洞端点永不响应：`start` **0 ms** 返回、`in-flight=1`、**`ticks=12` / 250 ms**、`still-pending`、`polls-ran=16`、`suggestion-arrives-late`、`typing-cancels-inflight`、`cancelled-never-arrives`；灵敏度对照组 `metric-detects-block`（人为阻塞 300 ms → 同一 20 ms 定时器 300 ms 才触发）；**超时 301 ms 触发**。

## 口径

历史提交正文**保持原样**（git 历史不可改，也不该改）；本文件是**追加式更正**，与 T103 处理 verdict 的方式一致。
判读规则：看到 `a27a4ee` 的"同步 / 15s"字样时，以本文件为准。
