# T26 独立评审结论 — T25 `settings-ui`：设置 UI + Rust 设置存储（AC-S3.1）

- **评审者**：Reviewer 4（cell `cell-20261031T085543161Z`；verifier ≠ writer `Senior Engineer (instance 2)`）
- **被评审任务**：board `T25`（`settings-ui`，Slice 3）；冻结契约 `.mpd/cellwork/cell-20261031T085543161Z/contracts/settings-ui.md`
- **验证环**：`loop-20261031T095153-67be87`（`mpd_verify_seat` 已绑定）
- **HEAD sha（评审时）**：`48569602d0c86fb402dc88155593d1dc06445c90`（`feat/qt6-ktexteditor-migration`）
- **结论**：**PASS**（bound seat）。AC-S3.1 四项全部由我自己的复跑 + 源码审计成立；设置在 Rust 侧存储、Qt 只消费
  （Qt 无第二套 TOML/文件存储）；无阻塞 finding，附 1 条 Low（写域授权未回写契约）与 4 条 Info。

---

## 1. 我实际跑的命令与观测

```text
# 设置 lane 自检（根构建，offscreen）
$ …/workspace-lane-settings/vedit-workspace-settings-selftest --selfcheck
BUILD-EXIT=0 ; SELFTEST-EXIT=0 ; CHECK 行 36 条、FAIL 0
WORKSPACE-SETTINGS-SELFTEST ok=1 checks=36 failures=0

# 根 ctest
$ ctest --test-dir target/qt6-build --output-on-failure
23/23 Test #23: vedit-workspace-settings-selftest ... Passed 0.32 sec
100% tests passed, 0 tests failed out of 23            ROOT-CTEST-EXIT=0

# 仓库门禁
$ mpd_verify_evidence {kind:"gate", gate:"typecheck"}
gate typecheck exit 0 -> .mpd/verify/evidence/ev-20261101T071340-072758.log

# Rust 设置存储本身（pinned vedit-build:local）
$ cargo test -p vd-settings
test result: ok. 6 passed; 0 failed
  （missing_file_is_the_default_state / broken_toml_degrades_to_a_warning /
    non_string_values_are_reported_and_skipped / round_trip_preserves_every_pair /
    save_replaces_atomically_and_leaves_no_temporary_file / failed_rename_leaves_the_original_file_intact）
```

## 2. AC-S3.1 逐条（断言是真实 UI/存储状态，不是 stub 或信号）

| 判据 | 证据（selftest 断言 + 我读的实现） | 结果 |
|---|---|---|
| 主题切换**即时生效** | `settings.theme.applied`：`panel->setTheme("Atom One Dark")` 后 `ThemeManager::currentTheme()` 立即相等；`ThemeManager::applyTheme()` 用 KSyntaxHighlighting Theme 计算 **QApplication::setPalette()** 并记录 current_（真实应用，不是只存名字） | ✅ |
| 并**持久化** | `settings.theme.persisted`：Rust store 写出的 TOML 含 `theme = "Atom One Dark"` | ✅ |
| **重启后仍生效** | `settings.theme.restart`：新的 ThemeManager + 新 registry + 新 panel 读**同一文件** → `Atom One Dark` | ✅ |
| 键位可在 UI 修改 | `settings.keys.applied`：`view.split.horizontal → Ctrl+Alt+J` 后断言 `splitAction->shortcut() == QKeySequence("Ctrl+Alt+J")`（真实 QAction 状态） | ✅ |
| 并**持久化** | `settings.keys.persisted`：Rust store 写入 `"keymap.view.split.horizontal" = "Ctrl+Alt+J"` | ✅ |
| **重启后仍生效** | `settings.keys.restart`：**空的 shortcuts.json** + Rust store → 恢复 `Ctrl+Alt+J`（证明权威存储在 Rust）；`keys.mirror-restart`：shell 启动加载的 mirror 也已是新键位 | ✅ |
| **冲突键位有提示** | `settings.conflict.detected/names/visible`：`Ctrl+S` 同时绑 `file.save` 与 `view.toggle-settings` → 可见加粗警告 `Conflicting shortcuts: Ctrl+S: file.save, view.toggle-settings`；`refreshConflicts()` 从真实 `commands_->ids()/action()` 计算（不是硬编码） | ✅ |
| **坏配置可诊断** | `settings.badconfig.warning/visible`：写 `theme = \n[unclosed` → 面板诊断区显示 Rust store 的 `invalid TOML … line 2, column 10`，不崩溃；Rust 测试 `broken_toml_degrades_to_a_warning` 同证 | ✅ |
| **坏配置可回退** | `settings.badconfig.theme-valid`（仍保持可用主题）+ `recovered`/`file-valid`（在 UI 里再改一次 → 原子重写为合法 TOML → 重载后 diagnostics 清空，**真从磁盘读回**） | ✅ |
| 设置在 Rust、Qt 只消费 | `settings_client.{h,cpp}` 只调 `vd_settings_load/save`（异步 job + poll + `vd_settings_free`），头注释明写 "This client never touches the file"；客户端/面板无 QFile/TOML 解析（QFile 只出现在 selftest 的 fixture 写入）；Rust 存储的原子写/坏配置/无 panic 由 `cargo test -p vd-settings` 6 条覆盖 | ✅ |
| GUI 线程不阻塞（C2） | `settings.nonblocking.*`：1.5 MB / 12 万条目 → `load()` start **0 ms**、解析期间 `ticks=7`、`entries=120000 delivered`；客户端只用 job + QTimer | ✅ |

## 3. 写域与授权

- T25 的改动：`src/workspace/settings/**`（新）、`evidence/slice3-settings/**` —— 在契约写域内。
- 另两处：`src/app/CMakeLists.txt`（lane 列表加 `settings`）与 `src/workspace/integration.cpp`（guarded include + `installSettings`）——
  **不在冻结契约的写域列表里**，但我找到了 **captain 的明确书面授权**（团队邮箱 `2026-11-01T06:55:05Z`：
  "T25 裁定 (A)：授权你改 CMakeLists 的 lane 列表 + integration.cpp 的 settings guarded 块（与 vcs 同款）"，精确到行）。
  → 判为"有授权、契约未回写"（**N-1 Low**，与 T6/T95 同一模式，建议 captain 补记）。
- `crates/vd-settings/`、`crates/vd-ffi/src/settings.rs`、`theme_manager.*`、`session_store.*`：`git status` 干净（未改，报告也这么说）✓。

## 4. Findings / Notes

| ID | 严重度 | 内容 | 处理建议 |
|---|---|---|---|
| **N-1** | **Low** | 两处共享文件改动（`src/app/CMakeLists.txt`、`src/workspace/integration.cpp`）在冻结写域之外；授权存在（邮箱 06:55Z）但契约文本未同步 | captain 在契约/记录里补记该豁免（同 T6/T95 的处理） |
| N-2 | Info | 面板除写 Rust store 外，还把键位镜像进 shell 的 `shortcuts.json`（`commands_->saveShortcuts`）——这是既有 shell 键位 mirror，**权威仍是 Rust store**（`keys.restart` 用空 mirror 仍恢复即为证）。报告 §4 已披露 | 无；保持 mirror 为派生缓存即可 |
| N-3 | Info | "即时生效"验证到 `ThemeManager::applyTheme` 的 QApplication 调色板应用（同步）；屏幕观感无显示服务器，不认证 | 停点由用户在真机看 |
| N-4 | Info | 报告记录共享树里 `vedit-folding-tests`（T94 lane）曾 flaky；我这次根 ctest **23/23** 无 flake | 无 |
| N-5 | Info | 未测"设置文件在运行期被外部进程并发改写"（报告已声明）；属竞态边界，非 AC-S3.1 判据 | 可另开小项 |

## 5. 未能验证的部分

1. **GUI 视觉/交互**：无显示服务器；主题即时生效只验证到调色板应用与对象状态，不认证屏幕观感。
2. **真实并发外部改写设置文件**：未测（声明）。
3. **重启**是测试内的"新对象 + 同一文件"模拟，不是真正进程重启后的人工操作；但 `keys.restart` 用空 mirror 证明 Rust store 是权威，语义等价。

## 6. 结论

**PASS**。AC-S3.1 的四项（主题即时生效+持久化+重启仍生效、键位 UI 修改+持久化+重启仍生效、冲突可见提示、坏配置诊断+回退+从磁盘恢复）
都由真实对象状态断言覆盖，且我实跑 36/36、根 ctest 23/23、typecheck exit 0、`cargo test -p vd-settings` 6/6 全绿。
设置在 Rust 侧（`crates/vd-settings` + `vd-ffi/src/settings.rs` + 异步 job），Qt 客户端只调 C ABI、无第二套存储；
`shortcuts.json` 是派生 mirror（报告已披露）。唯一 Low 是契约写域未回写 captain 的接线授权。
