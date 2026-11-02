# T25 报告 — Slice 3 设置 UI（AC-S3.1）+ Rust 设置存储

- Cell: `cell-20261031T085543161Z` · Board task: **T25** · 契约：`.mpd/cellwork/cell-20261031T085543161Z/contracts/settings-ui.md`
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-11-01 · 基线 `HEAD 4856960`
- 结论：**AC-S3.1 全部落地并在无头环境验证通过**：主题切换即时生效+持久化、键位 UI 修改+持久化、
  冲突键位可见提示、坏配置诊断+回退+从磁盘恢复；设置的存储是 **Rust（`crates/vd-settings` + `vd-ffi/src/settings.rs`）**，
  Qt 面板只消费。接线已按 captain 授权完成（2 个文件的最小 guarded 改动）。
- 门禁：**根 ctest 23/23 passed（exit 0）**；`typecheck` 的唯一失败是 **T27 的 `crates/vd-ffi/src/fim.rs:412`**
  （clippy `field_reassign_with_default`，不在 T25 写域、T25 未改任何 Rust 文件），已在交付说明中如实标注（见 §5）。

## 1. 交付物

| 路径 | 内容 |
|---|---|
| `src/workspace/settings/settings_client.{h,cpp}` | Rust 设置存储的 Qt 客户端：`vd_settings_load`/`vd_settings_save` **异步 job + QTimer 轮询**；GUI 线程不读文件、不做 TOML 解析。无 FFI 构建时给出诊断而不是假装能用。 |
| `src/workspace/settings/settings_panel.{h,cpp}` | 设置面板：主题下拉（30 个 KSyntaxHighlighting 主题，即时应用并写 `theme`）、键位表（每命令 默认/覆盖 + State 列）、`QKeySequenceEdit` + Assign/Clear、**冲突提示**（同一序列多个命令）、**诊断区**（Rust store 的 warnings + 应用/保存失败）、Reload、状态行。 |
| `src/workspace/settings/settings_selftest.cpp` | 36 条无头断言（见 §3），真 Rust store + 真 C ABI，不含 stub。 |
| `src/workspace/settings/settings_integration.{h,cpp}` | `installSettings(shell, options)`：设置 `settings` 面板工厂，settings 文件 = `<stateDir>/settings.toml`。 |
| `src/workspace/settings/CMakeLists.txt` | lane 目标 `vedit_workspace_settings` + ctest 条目；可选链接 vd-ffi（同 vcs lane 的解析顺序），独立构建时编译它消费的两个 shell 源。 |
| `src/app/CMakeLists.txt` | **captain 授权的 1 行**：lane 列表末尾加 `settings`。 |
| `src/workspace/integration.cpp` | **captain 授权的 guarded 块**：`VEDIT_WORKSPACE_HAVE_SETTINGS` 的 include + `installSettings(...)`（与 vcs 同款，未动其它块）。 |

`crates/vd-settings` 与 `crates/vd-ffi/src/settings.rs` **不需要改动**：Slice 0 已交付 TOML 存储
（原子写、坏配置 warnings、无 panic）与两个异步 job；T25 是它们的第一个真实消费者（面板 + selftest 直接走 C ABI）。

## 2. 接线前后对照（同命令，`01-wiring-before.log` / `02-wiring-after.log`）

| 观测 | 接线前 | 接线后 |
|---|---|---|
| `ctest --test-dir target/qt6-build -N` | **22** 项 | **23** 项（`Test #23 vedit-workspace-settings-selftest`） |
| `nm -C vedit-app \| grep -c 'vedit::workspace::settings'` | **0** | **123**（含 `installSettings(ApplicationShell&, IntegrationOptions const&)`、`SettingsPanel::qt_metacall`、`SettingsClient::load/save` …） |
| `strings -el vedit-app \| grep settings` | 0 | `settings-panel`、`settings-keys`、`settings-conflict` |
| 真实启动（`--state-dir /tmp/t25-state --project tests/fixtures/verilog-project --hold 300`） | 无 settings 行 | `settings: installSettings file='/tmp/t25-state/settings.toml'`，`APP-EXIT=0` |

未改 `app_shell.cpp`。

## 3. AC-S3.1 逐条 ↔ 断言（`03-selftest.log`，36 checks / 0 failures，exit 0）

| AC-S3.1 | 证据（selftest 断言） |
|---|---|
| 主题切换即时生效 | `settings.theme.applied`（选 `Atom One Dark` 后 `ThemeManager::currentTheme()` 立刻等于它） |
| 并持久化 | `settings.theme.persisted`（Rust store 写出的 TOML 含 `theme = "Atom One Dark"`） |
| 重启后仍然生效 | `settings.theme.restart`（新 ThemeManager + 新 registry + 新 panel 读同一文件 → `Atom One Dark`） |
| 键位可在 UI 修改 | `settings.keys.applied`（`view.split.horizontal` → `Ctrl+Alt+J`，QAction 立即变） |
| 并持久化 | `settings.keys.persisted`（`keymap.view.split.horizontal = "Ctrl+Alt+J"` 由 Rust store 写入） |
| 重启后仍然生效 | `settings.keys.restart`：**用空的 shortcuts.json**，仅靠 Rust store 恢复 `Ctrl+Alt+J`（证明权威存储在 Rust）；`settings.keys.mirror-restart`：shell 启动时加载的 mirror 文件也已经是新键位 |
| 冲突键位有提示 | `settings.conflict.detected/names/visible`：`Ctrl+S` 同时绑 `file.save` 与 `view.toggle-settings` → 可见警告 `Conflicting shortcuts: Ctrl+S: file.save, view.toggle-settings` |
| 坏配置可诊断 | `settings.badconfig.warning/visible`：写入 `theme = \n[unclosed` → 面板诊断区显示 Rust store 的 `invalid TOML … line 2, column 10`，不崩溃 |
| 坏配置可回退 | `settings.badconfig.theme-valid`（仍保持一个可用主题）、`settings.badconfig.recovered/file-valid`（在 UI 里再改一次 → 原子重写为合法 TOML → 重载后 diagnostics 清空，**真的从磁盘读回**） |
| GUI 线程不阻塞（C2） | `settings.nonblocking.*`：1.5 MB / 12 万条目 → `load()` **start 0 ms**、解析期间事件循环 `ticks=7`、最终 `entries=120000 delivered`；面板/客户端只有 job + QTimer，无直接文件 IO |

## 4. 语义说明（重启后「生效」的两条真实路径）

- **设置存储（权威）**：`<stateDir>/settings.toml`，由 Rust store 原子写入；`theme` 与 `keymap.<command id>`。
- **面板未打开时也生效**：面板把键位同时镜像进 `shortcuts.json`（shell 启动即加载），主题在退出时由 shell 把
  当前主题写进 `session.json`（既有行为）；因此重启后**即使不打开设置面板**，主题与键位也已是上次的值。
  打开面板时，面板再用 Rust store 的值覆盖/校正（一致，因为面板同时写两边）。
- 冲突键位**允许保存但显著告警**（用户可自行取舍）；不做静默覆盖。

## 5. 门禁（自跑，原样引用）

| 门 | 命令 | 结果 |
|---|---|---|
| 根 ctest | `ctest --test-dir target/qt6-build --output-on-failure` | **23/23 passed，ROOT-CTEST-EXIT=0**（`04-ctest.log`）。外部 flake 如实记录：`vedit-folding-tests`（T94 folding lane）在 4 次整树运行中红过 2 次、每次单独重跑都通过（`06-external-flake.log`），与 T25 无关。 |
| 设置 lane | `vedit-workspace-settings-selftest --selfcheck` | **36 checks / 0 failures，exit 0** |
| typecheck | `bash build/gates/typecheck.sh` | **TYPECHECK-EXIT=0（PASS）** —— 最终树通过（`05-typecheck.log`）。过程中曾被 T27 的 `crates/vd-ffi/src/fim.rs:412`（clippy `field_reassign_with_default`，T25 写域之外、且 T25 未改任何 Rust 文件）卡红；T27 修复后本任务重跑即绿，没有把外部的红写成自己的绿。 |

## 6. 范围 / 诚实声明

- 改动：`src/workspace/settings/**`（新）、`src/app/CMakeLists.txt`（1 行，captain 授权）、
  `src/workspace/integration.cpp`（guarded 块，captain 授权）、`evidence/slice3-settings/**`。
  `crates/vd-settings/**`、`crates/vd-ffi/src/settings.rs`、`theme_manager.*`、`session_store.*` **未改**（无需改）。
- 未改 `src/app/app_shell.cpp`；未 commit/push。
- 无 GUI 视觉检查（headless offscreen）：面板行为由对象级断言覆盖，没有「看起来对」的判断。
- 面板的 `Reload`/坏配置恢复路径测过；未测「设置文件在应用运行期间被外部进程并发改写」这一类竞态。
- typecheck 的绿取决于 T27 修复 `fim.rs`；本报告没有把它写成绿。

## 7. 证据清单

`00-pins.txt`、`01-wiring-before.log`、`02-wiring-after.log`、`03-selftest.log`（36 checks 全文，含非阻塞）、
`04-ctest.log`（23/23）、`05-typecheck.log`（含外部 `fim.rs` 失败原文）、本报告。
