# Qt 诊断会话恢复

基线为 `dev` 的 `752c8ab8e2e22528f4e863da8318d81e177203e8`。在主检出的 `fix/diagnostics-recovery` 上，RED 使用这版 Qt 产品源码和新断言，链接分支上已完成的 Rust 静态库。GREEN 复跑同一回归，另经真实 Rust FFI 驱动受控 LSP 子进程。[冻结契约](../../../.mpd/workspace-audits/cleanup-20261127/contract.md) · [运行日志节选](raw.log)

| 场景 | RED | GREEN |
| --- | --- | --- |
| 服务静默 | 空面板已解释静默，状态行仍显示 `0 diagnostic(s)`。 | 状态行与面板都解释没有分析结果。 |
| 服务显式发布空集合 | 显示零条诊断并产生干净判定。 | 原有干净判定通过。 |
| 空闲时 Ready → Failed | 面板定时轮询后仍显示已分析、零条诊断和干净状态。 | 定时轮询显示失败及服务 stderr，先前的干净判定失效。 |
| 分析轮次读到 Failed | `failed: lsp 1/1 file(s), diagnostics 0` 覆盖了具体原因。 | 保留服务自己的失败原因。 |
| 新进程就绪 | 受控后端的上下文推送总数保持在 `1`。 | 初次和恢复各一次。新进程收到未保存文本，磁盘仍为原文。 |
| 中间模块范围 | 此处添加精确断言以保护现有行为。 | 实时分析集合与发出的诊断请求集合均为 `{sub.sv, leaf.sv}`。`sibling.sv` 仍在会话集合，原正控通过。 |

面板继续每 300 ms 轮询活动会话。空闲退出后，下一次显式文件分析把当前编辑缓冲区提交给 Rust，触发 Rust 的惰性恢复。新进程就绪时，Qt 才重新推送运行期 `.f` 和请求诊断。受控流程在失败及恢复后分别观察了 650 ms 空闲期。Rust 单独的协议对照观察到自主补推 0 次和调用者补推 1 次。[Rust 协议对照](../rust/README.md)

## 复跑

从主检出仓库根目录执行。缓存路径沿用上一次完全同源的 Qt 6.10.2 / KF 6.24.0 Ninja 构建。容器将主检出设为 `/src`，将旧检出下的缓存挂载到 `/src/target/outline-qa`。RED 配置后，用 `ninja -t clean` 清除两个回归目标及其依赖，再从主检出重编。现成 HDL 工具的来源见先前构建记录。[工具链和原构建](../../outline-known-module/implementation/README.md)

```bash
repo=$PWD
cache=$repo/target/outline-context-pr/target/outline-qa
out=$repo/target/diagnostics-recovery-qt
mkdir -p "$out/snapshots"

docker run --rm --network none -v "$repo:/src" -v "$cache:/src/target/outline-qa" \
  -w /src vedit-build:local bash -ec '
  source scripts/versions.env
  export RUSTUP_TOOLCHAIN="$RUST_TOOLCHAIN" RUSTFLAGS="$RELEASE_RUSTFLAGS"
  export CARGO_HOME=/src/target/cargo-home CARGO_TARGET_DIR=/src/target/outline-qa/rust CARGO_INCREMENTAL=0
  cargo --offline clean -p vd-lsp -p vd-ffi --release
  cargo build --offline --locked --release -p vd-ffi
'

# 可复用 Rust 回归生成的 session_health 可执行文件。否则先编译，再从 Cargo 输出取路径。
CARGO_HOME="$repo/target/cargo-home" cargo +stable test --locked --offline -p vd-lsp --test session_health --no-run
fixture=$repo/target/debug/deps/session_health-2ac3201dffcf8dc9
printf '%s\n' '#!/bin/sh' 'export VEDIT_TEST_SESSION_HEALTH_MODE=live' \
  'exec 3>&1 1>/dev/null' \
  'exec /tools/session_health --exact startup_failure_includes_child_stderr --nocapture' \
  > "$out/health-server"
chmod +x "$out/health-server"

docker run --rm --network none -v "$repo:/src" -v "$cache:/src/target/outline-qa" \
  -v "$repo/.mpd/recon/verible/bin:/tools/verible:ro" \
  -v "$fixture:/tools/session_health:ro" -w /src vedit-qt6-build:local bash -ec '
  b=/src/target/outline-qa/qt
  lane=$b/src/app/workspace-lane-diagnostics
  out=/src/target/diagnostics-recovery-qt
  export LC_ALL=C.UTF-8 VEDIT_NO_ENV_AUTOCAPTURE=1 VEDIT_SLANG=/src/build/slang/out/slang
  export VEDIT_SLANG_SERVER=/src/target/outline-qa/bin/slang-server
  export VEDIT_VERIBLE_LS=/tools/verible/verible-verilog-ls VEDIT_VERIBLE_BIN_DIR=/tools/verible
  export VEDIT_VERIBLE_FORMAT=/tools/verible/verible-verilog-format VEDIT_VERIBLE_LINT=/tools/verible/verible-verilog-lint
  cmake -S /src -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DVEDIT_RUST_FFI_LIB=/src/target/outline-qa/rust/release/libvd_ffi.a \
    -DVEDIT_RUST_FFI_INCLUDE=/src/crates/vd-ffi/include
  cmake --build "$b" --parallel 9 --target VEdit_lrelease \
    vedit-lint-experience-selftest vedit-realtime-depth-switch-e2e \
    vedit-workspace-diagnostics-selftest vedit-realtime-context-e2e vedit-filelist-authority-e2e \
    vedit-toolchain-ux-selftest vedit-slang-build-context-e2e vedit-verdict-retention-selftest \
    vedit-diagnostics-product-seam
  VEDIT_RECOVERY_SNAPSHOT_DIR="$out/snapshots" QT_QPA_PLATFORM=xcb xvfb-run -a \
    -s "-screen 0 1500x1080x24 -nolisten tcp" "$lane/vedit-lint-experience-selftest" \
    --verible-lint /tools/verible/verible-verilog-lint --health-server "$out/health-server"
  QT_QPA_PLATFORM=offscreen ctest --test-dir "$b" -V --output-on-failure -j 2 \
    -R "^vedit-(lint-experience-selftest|realtime-depth-switch-e2e|workspace-diagnostics-selftest|realtime-context-e2e|filelist-authority-e2e|toolchain-ux-selftest|slang-build-context-e2e|verdict-retention-selftest)$"
  env -u VEDIT_SLANG_SERVER QT_QPA_PLATFORM=offscreen ctest --test-dir "$b" -V --output-on-failure \
    -R "^vedit-diagnostics-product-seam$"
'
```

RED 在上述构建前执行 `ninja -C "$b" -t clean vedit-lint-experience-selftest vedit-realtime-depth-switch-e2e`，保留 Qt 基线产品源码和新增面板断言，xcb 命令只传 `--verible-lint`。RED 为 121 项中 6 失败，退出码 1。最终 xcb 为 135 项通过，8 项 focused CTest 全通过，两者退出码均为 0。

产品 seam 使用 slang-server 时仍在既有的提示词断言失败，CTest 退出码 8。提示中实际有 `unused-but-set-variable`。Verible 回退重跑通过，退出码 0。Qt 翻译生成 1,894 条，未完成 0 条。[既有 seam 对照](../../outline-known-module/verification/qt-checks.log)

验证窗口为实际 `DiagnosticsPanel` QWidget。就绪、失败及恢复三张 720 × 460 快照留在忽略目录 `target/diagnostics-recovery-qt/snapshots/`，已逐张打开确认状态行可读。本次范围为诊断 focused CTest，没有构建发布包或运行全仓 CTest。
