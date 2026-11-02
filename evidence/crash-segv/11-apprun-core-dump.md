# T117 — core dump / AppRun 说明

- **App 内已做**（`src/app/crash_reporter.cpp::install`）：`setrlimit(RLIMIT_CORE, infinity)`，
  因此进程自身允许吐核；同时**无论 core 是否生成**，信号处理器一定写
  `$XDG_STATE_HOME/vedit/crash-<pid>-<unix-time>.log`（回落 `~/.local/state/vedit/`）。
- **AppRun 的 `ulimit -c unlimited` / `kernel.core_pattern`**：属于 `packaging/**`，
  按 T117 写域**未改动**。给打包负责人（T31/T113）的建议：
  1. AppRun 开头加 `ulimit -c unlimited 2>/dev/null || true`；
  2. 不要尝试改 `kernel.core_pattern`（非 root 无效）；依赖 app 的 crash log 即可；
  3. AppRun 可导出 `VEDIT_APPIMAGE_SHA256`（打包时算好），崩溃报告会直接带上，不必在启动时哈希 95MB。
- 用户侧取证步骤（写进用户指南时可用）：
  `ls -t ~/.local/state/vedit/crash-*.log | head` → 把最新文件发回，即可拿到信号/栈/当时打开的文件/最近事件。
