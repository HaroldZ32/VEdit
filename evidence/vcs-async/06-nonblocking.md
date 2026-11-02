# T99 N-1 — 非阻塞回归断言（慢子进程时事件循环仍在跳）

位置：`src/workspace/vcs/vcs_selftest.cpp` 的 `checkNonBlocking()`（ctest 条目
`vedit-workspace-vcs-selftest`，基准 24 checks → 35 checks）。

## 装置

1. 写一个假的 `git` 脚本并放到 **PATH 最前面**：`#!/bin/sh` → `sleep 0.4` → `exec <真实 git> "$@"`；
2. 临时目录里放一个 `.git` 目录标记，使 vd-vcs 的 backend 探测判定为 Git
   （否则它只做 marker walk，不 spawn 任何进程，job 会瞬间完成——第一版测试就是这样失败的，
   因此断言本身证明了「必须有真子进程」这一前提）；
3. `FfiVcsSource::requestInfo()` 之后**立即**用 `QElapsedTimer` 量 start 的耗时，
   再用 20 ms 的 `QTimer` 在 250 ms 窗口内数 tick，同时记录回调是否已经到达。

## 通过的断言（最终运行，`03-vcs-selftest.log`）

| check | 结果 |
|---|---|
| `vcs.nonblocking.start-returns-fast` | **0 ms**（假 git 会睡 400 ms） |
| `vcs.nonblocking.event-loop-ticks` | **12 ticks** / 250 ms（事件循环在跳） |
| `vcs.nonblocking.job-still-pending` | 回调**未到达**，已轮询 24 次（job 仍在 PENDING） |
| `vcs.nonblocking.result-delivered` | 慢 git 结束后结果经 QTimer 送达（note=`git repository has no commit yet`） |
| `vcs.nonblocking.metric-detects-block` | 同一 20 ms 定时器 + 线程阻塞 300 ms → **300 ms 才触发**（迟到） |

最后一条是**灵敏度证明**：如果实现是同步的（像旧的 `vd_vcs_info` 那样在调用线程等 git），
定时器就会在阻塞结束后才触发，`start-returns-fast` 与 `event-loop-ticks` 必然红。
所以这条断言不是「改了个壳」——它能抓住同步实现。
