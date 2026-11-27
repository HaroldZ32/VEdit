# Rust 诊断恢复复核修复

主检出分支为 `fix/diagnostics-recovery`，复现基线为 `45b018f`。修复后的测试针对同一检出的 Rust 源码，交付核对时 `HEAD` 为 `dc78d8eb99572d5692d42c1329c9f984aeb9383e`，`crates/vd-lsp/` 与该提交一致。Rust 补丁的 SHA-256 为 `acee858b530969ebe75fba14090bb8d88abc0e4f610dc1fbda6577b44d6876d6`（`git diff 45b018f dc78d8e -- crates/vd-lsp | sha256sum`）。

## 三项边界

| 场景 | 修复 | 验证 |
| --- | --- | --- |
| 新进程静默 | 诊断来源校验、替换和清理共享同一个 watch 写锁。传输结束作废它的空集和非空集 publication。若旧写入或清理迟到，当前来源的条目及代次均保留。 | 两种旧 publication 都得到 `generation 1 → 2 → 3`。新进程静默时 `published=false`，首次显式空发布后 `published=true`。单元测试直接交错新旧发布者。 |
| 工程索引与相同缓冲区 | 只有已初始化且协议循环仍活动的会话可以跳过重复发送或报告 Ready。 | 健康时两次提交相同文本只发送一次。耗尽后两次重送保持 `Failed / PID=None / restarts=3`，总共启动四次。 |
| 协议输出关闭且子进程仍活着 | 检查并在需要时终止会话持有的 `Child`，有界等待回收完成后才允许启动替代进程。保留退出子进程的 stderr，正常关停先完成 LSP 握手。 | 旧 PID 1772 从 `/proc` 消失，替代 PID 1807 回复符号请求。初始化拒绝保留 stderr，初始化超时在返回错误前回收实际 PID，真实 Verible 正常退出通过。 |

## 日志

- 精简回归在修复前退出 `101`。三项失败和既有四项通过均有记录。[red.log](red.log)
- 修复后 `vd-lsp` 72 项通过，使用真实 Verible 和 slang-server，无跳过。初始化超时单独运行也通过。`check --all-targets`、`clippy --all-targets -- -D warnings`、`fmt --check` 和 `git diff --check` 都退出 `0`。[rust.log](rust.log)
- 初次整包运行暴露了本轮初始化回收断言的错误。既有 `wait_gone` 的零时间窗口会跳过检查并直接失败，后续改用即时 `/proc` 检查，再重新运行整包及门禁。

本目录记录实施侧验证。独立 Reviewer 的原探针及 Qt 产品流程另行核验。
