# 构建上下文超时后的恢复

基线为 `25b4dfaba740b2cba63460ce3bd74b7f1c34870b`，候选为 `fea860c0ed32d74cdbda514d3a0b66f0ecab7db6`。`git diff 25b4dfa fea860c -- crates/vd-lsp | sha256sum` 为 `c21e6dd9063f12bdad9871cdc7452c7cf7253213caca1d94a5d78dfe04ea61dd`。

| 检查 | 观测 | 证据 |
| --- | --- | --- |
| 新 health 回归对基线 | `101`。同一 PID 应用了 A、B 并省略 B 的回复，之后恢复 A 未到达对端。 | [RED](red.log) |
| 未修改的原始超时探针 | 两个源文件 `cmp` 均退出 `0`。探针退出 `0`，PID `1380` 收到 A→B（超时）→A，状态从 Failed 恢复到 Ready，重启次数 `0`。 | [原探针](original-probe.log) |
| `vd-lsp` 整包 | `0`，75/75。PID `676` 恢复 A，PID `762` 重发 B，成功后同正文未再次发送。PID `672` 对两次不同的超时请求复用候选路径，停止时仍可读，随后两个文件均回收。已有进程恢复场景保持 `1→0→1`，交错恢复的替代进程只收到一次同配置。真实 Verible 和 slang-server 测试均运行。 | [Rust 门禁](rust-gates.log) |
| `cargo +stable fmt --all -- --check`、`cargo +stable clippy --locked --offline -j 4 -p vd-lsp --all-targets -- -D warnings`、`git diff --check` | 均退出 `0`。 | [Rust 门禁](rust-gates.log) |

默认 `1.99.0` 工具链的 Cargo 入口缺失，所用 `+stable` 的 Cargo 和 rustc 实测均为 `1.99.0`。基线回归和原探针分别使用固定提交的 Git 归档和各自初始为空的 Cargo 目标目录。超时由受控协议进程省略回复触发。
