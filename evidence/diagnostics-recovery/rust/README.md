# Rust 语言服务恢复

基线为 `752c8ab8e2e22528f4e863da8318d81e177203e8`，验证对象为 `fix/diagnostics-recovery` 上的 Rust 工作树。[终端输出节选](raw.log)仅替换了临时目录和检出绝对路径，SHA-256 为 `9969df2a3d2f8f3b36b742152a641f0343197fb1f07c656ff53c6ad80ddf3121`。

## 实测

| 场景 | 基线加回归 | 修复后 |
| --- | --- | --- |
| 子进程在 Ready 后空闲退出 | 公共状态显示 `Failed`、`0/3` 和子进程 stderr，PID 仍是旧进程。恢复后实际新进程为 `255`，公共 PID 仍为 `231`。 | `Failed` 清除 PID。触发恢复的内存改动之后，观察到 `Ready`、`restarts=1`，公共 PID 和实际新进程均为 `473`。 |
| 耗尽重启次数 | 实际共启动四次，公开计数为 3，拒绝原因却显示 `4/3`。 | 实际启动数量为四次，计数与拒绝理由统一为 `3/3`，并带最后一个进程的 stderr。 |
| 初始化阶段退出 | `initialize failed: service stopped` 缺少子进程的错误文本。 | 同一错误附带 `server stderr: fixture rejected initialization`。 |
| 恢复未保存文本及运行期构建上下文 | 新进程收到包含未保存文本的 `didOpen`。线上的 `slang.setBuildFile` 次数在初次、重启后、调用者补推后依次为 `1 → 0 → 1`。 | 同样为 `1 → 0 → 1`。请求响应作为顺序屏障，夹具实际读取 `.f` 并检查 include 参数、含空格的定义值及会话文件。 |

`LspClient::ensure_session` 现在在尝试启动替代进程时递增次数。`Inner::note_success` 和失败路径发布同一客户端的 PID 与计数。诊断状态在启动或传输失败时给 stderr 读取任务最多 100 ms 完成，避免被进程或继承管道的后代拖住。消息沿用最多 5 行和 400 字符的摘要，每个新进程有独立的 stderr 缓冲。

[Rust 会话](../../../crates/vd-lsp/src/session.rs)的惰性恢复使用原启动配置并自动重开文档。[Qt 控制器](../../../src/workspace/diagnostics/diagnostics_controller.cpp)拥有运行期构建上下文，在 `Ready` 检测重启计数变化后应向新进程补推一次并重发相关请求。Rust 回归通过公共 `SessionHandle` 记录了协议次数，Qt 何时补推及面板如何展示由 Qt 任务验收。

## 复跑

主机 `cargo +stable` 与 `rustc +stable` 均为 1.99.0，符合 `rust-toolchain.toml`。主机名为 `1.99.0` 的 rustup 工具链条目缺 Cargo，默认注册缓存离线解析缺 `glib`。使用既有 `target/cargo-home` 缓存和默认 `target/debug` 即可解析锁文件并完成 `vd-lsp` 包验证。也确认已安装的 `vedit-build:local` 有 Cargo 1.99.0。

仅重跑四项新回归，从仓库根目录执行。

```sh
CARGO_HOME="$PWD/target/cargo-home" cargo +stable test --locked --offline -p vd-lsp --test session_health -- --nocapture
```

整包验证使用本地已缓存的 Verible 与 slang-server。

```sh
export CARGO_HOME="$PWD/target/cargo-home"
export VEDIT_VERIBLE_BIN_DIR="$PWD/.mpd/recon/verible/bin"
export VEDIT_SLANG_SERVER="$PWD/packaging/runtime/out/AppDir/usr/lib/lsp/slang-server"
cargo +stable test --locked --offline -p vd-lsp -- --nocapture
cargo +stable check --locked --offline -p vd-lsp --all-targets
cargo +stable clippy --locked --offline -p vd-lsp --all-targets -- -D warnings
cargo +stable fmt -p vd-lsp -- --check
git diff --check
```

原有包基线 64 项通过，退出码 0。新回归在产品未改动时为 1 通过、3 失败，退出码 101。最终整包 68 项通过，退出码 0，输出无 `SKIP`。类型检查、Clippy、格式及差异检查退出码均为 0。RED 后夹具另做了私有临时目录和完整日志行读取两项加固，断言场景一致。

新回归使用独立 Unix 子进程和私有目录，通过每个子进程自己的退出标记触发自退。轮询有超时，夹具不向任何 PID 发信号，也无需系统安装 HDL 工具。退出细节与运行期协议由测试子进程观测，已安装的真实 slang-server 另通过已有崩溃恢复和构建上下文测试。验证范围为 Rust `vd-lsp`，未运行全 workspace 或 Qt。
