# 连续退出后的构建上下文

在 `fix/diagnostics-recovery` 主检出验证，基线为 `ad9f41f2498e6c20fb72d8847ac82b31d42981da`。待交付的 `git diff -- crates/vd-lsp | sha256sum` 为 `ebcf1575dcca4e1bee5511ede12469f4eef0b58adde7ccba4f6f11fb71ab4244`。

Rust 会话在收到成功回复后记录实际接收进程的重启代次和完整 `.f` 正文。后续推送仅在进程健康、代次相同且正文相同时合并。旧补推仍能启动替代进程。拒绝会通过既有会话状态报告，相同输入可以重试。替换配置时，原临时文件按既有规则回收。

| 检查 | 观测 | 证据 |
| --- | --- | --- |
| `cargo +stable test --locked --offline -j 4 -p vd-lsp --test session_health recovery_coalesces_only_successful_identical_build_contexts -- --exact --nocapture` | 基线替代 PID `197` 收到 2 次同配置，退出 `101`。修复后替代 PID `471` 收到 1 次，退出 `0`。 | [RED](red.log) · [GREEN](green.log) |
| 同一聚焦回归的正对照 | 替代进程收到未保存文本、实际变化的 `WIDTH=32`、包含目录 `new-inc`，以及完整的 `top.sv` 会话源路径。`new-inc` 先被拒绝，原样重试获接受。显式还原旧宏和包含目录也发送到同一 PID。 | [GREEN](green.log) |
| `cargo +stable test --locked --offline -j 4 -p vd-lsp --test review_context_interleave -- --nocapture` | 两个归档源码逐字节相同，8/8，退出 `0`。交错替代 PID `366` 收到 1 次，普通恢复为 `1→0→1`。临时探针副本已移除。 | [原探针重放](independent-probes.log) |
| `cargo +stable test --locked --offline -j 4 -p vd-lsp -- --nocapture` | 73/73，使用真实 Verible 和 slang-server，无跳过。真实 slang-server 的源集合正对照解析到 `rtl/uart.v`。 | [Rust 门禁](rust-gates.log) |
| `cargo +stable fmt --all -- --check`、`cargo +stable clippy --locked --offline -j 4 -p vd-lsp --all-targets -- -D warnings`、`git diff --check` | 均退出 `0`。 | [Rust 门禁](rust-gates.log) |

验证复用了 `target/cargo-home`、`target/debug` 和现有语言服务二进制。默认版本名 `1.99.0` 对应的工具链缺少 Cargo，`+stable` 的 cargo 和 rustc 实测均为仓库指定的 `1.99.0`。公开 `SessionHandle` 在构造时固定会话文件集，聚焦回归直接观察同一 PID 上重复推送和新配置带着正确源路径。新文件集合由新会话接收，另由整包的源集合场景覆盖。实施侧验证运行 Rust 与受控协议进程，Qt 面板及 FFI 产品复核由独立验收负责。
