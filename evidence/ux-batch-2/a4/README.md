# A4 — 设置：界面字体/缩放 + Format 可配项 + Lint 双后端统一面板

契约：`.mpd/plans/ux-batch-2/00-plan.md` §3.A4 + §0.1（R1/R2/R3 为用户裁定，BINDING）。
写作用域：`src/workspace/settings/**`、`crates/vd-settings/**`、`src/app/main.cpp`、
`src/workspace/diagnostics/toolchain.cpp`、`src/workspace/diagnostics/ffi_backends.cpp`。
（`src/workspace/CMakeLists.txt` 在这一棵树里**不存在**；lane 的源文件清单在
`src/workspace/settings/CMakeLists.txt`，已加三个新源文件。）

状态：**实现完成并通过本 lane 的全部 ctest**。有两处交付物**尚未真正生效**，都属于
A1 的 ABI 补齐范围，已在下面 §4 明确列出（不掩盖）：
1. `VdLintBatchOptions` 只透传 `rules_path`，其余 lint 旋钮是 `transport-pending`（存起来、还没送出去）；
2. 工程里发现的 `.rules.verible_lint` 仍**没有**默认值——`VdProjectInfo` 还不暴露它。

## 1. 新增的 settings key（Rust `crates/vd-settings/src/schema.rs` 是参考实现）

| key | 类型 | 默认 | 生效时机 |
|---|---|---|---|
| `ui.font_point_size` | 整数（6..72；空=跟随系统） | 空 | **立即**（`QApplication::setFont`），启动时也应用 |
| `ui.scale_percent` | 100/125/150/200 | 空（=100） | **下一次启动**（`QT_SCALE_FACTOR`，在构造 QApplication 之前） |
| `format.indentation_spaces` | 整数 1..16 | 空 = verible 默认 2，不传 flag | 下一次 format |
| `format.column_limit` | 整数 1..1000 | 空 = verible 默认 100 | 同上 |
| `format.wrap_spaces` | 整数 1..16 | 空 = verible 默认 4 | 同上 |
| `format.line_terminator` | `CRLF`,`LF`,`auto` | 空 = auto | 同上 |
| `format.try_wrap_long_lines` | `true`/`false` | 空 = verible 默认 false | 同上 |
| `format.compact_indexing_and_selections` | `true`/`false` | 空 = verible 默认 true | 同上 |
| `format.<alignment>` ×11 | `align`,`flush-left`,`preserve`,`infer` | 空 = infer | 同上 |
| `format.<indentation>` ×4 | `indent`,`wrap` | 空 = wrap | 同上 |
| `format.port_declarations_right_align_{packed,unpacked}_dimensions` | `true`/`false` | 空 = false | 同上 |
| `lint.verible.ruleset` | `default`,`all`,`none` | 空 = 不传 | 见 §4 |
| `lint.verible.rules` | 逗号分隔的规则名（原样） | 空 | 见 §4 |
| `lint.verible.rules_config` | 路径 | 空 | **真的生效**（`VdLintBatchOptions.rules_path`） |
| `lint.verible.waiver_files` | 一行一个路径 | 空 | 见 §4 |
| `lint.verible.autofix` | `no`,`patch`,`patch-interactive`,`inplace`,`inplace-interactive`,`generate-waiver` | 空 = 不传 | 见 §4 |
| `lint.slang.warnings` | 一行一个 `-W` 值 | 空 | 见 §4 |
| `lint.slang.error_limit` | 整数 0..1000000（0=slang 的"不限"） | 空 | 见 §4 |

一律遵循"**默认 = 不写键 / 不传 flag**"：`Restore defaults`、把字体选回"跟随系统"、把缩放选回 100 %
都会**删除**对应的键，所以旧 `settings.toml` 只会多出用户真正改过的键；`settings.a4.legacy.*`
断言旧键（`theme`、`tools.*`）保持原样、且新页默认值不会写出任何新键。

## 2. 三个新页面

- **Appearance**：字体（0=跟随系统，用 `QSpinBox` 的 special value 表达）+ 缩放（100/125/150/200），
  下面一行说明"缩放下次启动生效，届时设置 `QT_SCALE_FACTOR=1.5`"。
- **Lint**（§0.1 R1）：**一个页面 + 两套独立持久化**。verible 组 5 个旋钮、slang 组 2 个旋钮，
  按当前 `tools.lint_engine` 逐旋钮启用/置灰，并有两类**分开的**原因：
  - `BackendUnsupported`（该引擎语义上没有）→ **置灰** + tooltip + 组内可见说明；
  - `TransportPending`（引擎支持、C ABI 还没透传）→ **不置灰、不丢值**，页面底部有一行列出
    "已保存、还没送出"的旋钮。
- **Format**：4 个主旋钮 + 可折叠 `Advanced options`（19 个高级旋钮）+ `Restore defaults`。
  全部来自 `format_options.h` 的同一张表（store key / verible flag / 控件类型 / 默认值文案），
  因此"页面控件"和"argv"不可能各改一半。

## 3. 代码落点

| 文件 | 作用 |
|---|---|
| `src/workspace/settings/ui_scale.{h,cpp}`（新） | 启动用的极小 store 读取器（只读两个数值键）、`QT_SCALE_FACTOR` 映射、`QApplication::setFont` |
| `src/workspace/settings/format_options.{h,cpp}`（新） | 23 个 format 旋钮表 + `formatArguments()`（纯函数，header-only 的 holder 供 diagnostics lane 用） |
| `src/workspace/settings/lint_options.{h,cpp}`（新） | 两套 lint 配置 + `lintKnobState()`（两类原因）+ **唯一的** `buildLintJobOptions()` 组合点 |
| `src/workspace/settings/settings_keys.h` | 全部新 key 常量（与 Rust `schema::keys` 一一对应） |
| `src/workspace/settings/settings_panel.{h,cpp}` | 三个页面 + 持久化 + 字体即时应用 + 引擎切换不串写 |
| `src/workspace/settings/settings_selftest.cpp` | 121 条 `settings.a4.*` 断言 |
| `src/app/main.cpp` | 构造 `QApplication` **之前**设 `QT_SCALE_FACTOR`；之后应用字体、把两个 holder 灌进 store 值；`--ui-scale-echo` 测试出口 |
| `src/workspace/diagnostics/toolchain.cpp` | `formatVerilog` 把 `formatArguments()` 放到 `-` 之前 |
| `src/workspace/diagnostics/ffi_backends.cpp` | 用 `buildLintJobOptions()` 决定 `VdLintBatchOptions.rules_path` |
| `crates/vd-settings/src/schema.rs` | `UiSettings` / `FormatSettings` / `LintSettings`（含值集校验、默认值、降级）+ 5 个新测试 |

## 4. 两处**尚未真正生效**的交付物（诚实声明）

### 4.1 lint 旋钮：`transport-pending`（引擎支持，C ABI 没有字段）
现在**真的送到 lint job** 的只有 `lint.verible.rules_config` → `VdLintBatchOptions.rules_path`。
下面这些值会**持久化、通过校验、页面可编辑**，但 lint job 收不到（页面底部与 tooltip 明确写出）：

`lint.verible.ruleset`、`lint.verible.rules`、`lint.verible.waiver_files`、`lint.verible.autofix`、
`lint.slang.warnings`、`lint.slang.error_limit`。

接上它们的唯一位置是 `buildLintJobOptions()`（`lint_options.h`）+ `FfiSlangBackend::startLint`
（`ffi_backends.cpp`，已调用它）。A1 把 `VdLintBatchOptions` 补齐后，只需给 `LintJobOptions`
加字段并在这一个函数里填，不需要第二处改动。

### 4.2 `.rules.verible_lint`：**仍未**作为默认值生效
`buildLintJobOptions()` 的优先级已按裁定实现并可测：
`lint.verible.rules_config`（页面手填） → `tools.verible_rules`（旧字段） → **发现路径** → 不传。
第三级今天**永远为空**：`crates/vd-ffi` 的 `VdProjectInfo` 没有 rules 路径字段
（`vd_ffi.h:1117-1161`；`VdLintBatchOptions.rules_path` 是输入，不是输出），按契约我没有改
`crates/vd-ffi`。调用点已留一个参数位（`QString discoveredRulesPath;` + 注释），A1 补
`VdProjectInfo.rules_path` 后把它传进去即可。**所以"GUI 消费工程里发现的规则文件"这条缺陷
到本次交付仍未修复**，当前默认 = 未发现路径（与旧行为相同）。

## 5. 验证（命令 + 观测结果）

复现（容器内，仓库根目录）：

```bash
bash scripts/build-qt.sh                      # 整树 + ctest + xcb smoke（pinned 镜像）
cd target/qt6-build
QT_QPA_PLATFORM=offscreen ctest -j1 -R "vedit-workspace-settings-selftest|vedit-ui-scale-restart" --output-on-failure
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-build:local cargo test -p vd-settings
```

**key 拼写的双向钉住**：C++ 侧是 `src/workspace/settings/settings_keys.h`（每个字面量都被
`settings.a4.*` 的文件断言检查），Rust 侧是 `schema::keys`（`key_contract_is_stable` 断言同一批
字面量）。两边各有测试钉住同一个字符串，改一边不改另一边会被其中一侧的测试抓住；但**没有**
编译期/生成式的一致性检查（与既有 `tools.*` key 的做法一致）。


| 命令 | 结果 |
|---|---|
| `bash scripts/build-qt.sh --no-test`（整树，pinned 镜像） | **EXIT=0**：Rust 层 + configure + 整树 build（warnings-as-errors）全绿，含 `vedit-app` 链接。`logs/15a-build-qt-notest.log` |
| `vedit-workspace-settings-selftest --selfcheck`（offscreen） | `ok=1 checks=228 failures=0`（`settings.a4.*` 121 条）。`logs/10-settings-selftest.log` |
| `ctest -R "vedit-ui-scale-restart|vedit-workspace-settings-selftest"` | 2/2 Passed（1.42 s / 0.24 s）。`logs/11-ui-scale-restart.log` |
| `bash scripts/build-qt.sh`（整树 + ctest + xcb smoke，pinned 镜像；**最终源码、跑完不再改动**） | **EXIT=0，ctest 42/42 Passed**，含 `vedit-workspace-settings-selftest` 与 `vedit-ui-scale-restart`，以及 xcb+软件渲染窗口 smoke。`logs/15-build-qt-full.log` |
| `ctest`（全树 42 行，**串行** `-j1`；另一次并行跑出现 1 个既有 SIGPIPE 抖动，见限制 7） | **42/42 Passed，EXIT=0**。`logs/16-full-ctest-sequential.log`；并行那次见 `logs/14-full-ctest.log` |
| `cargo test -p vd-settings` | 24 passed（含 5 个新测试）。`logs/13-cargo-vd-settings.log` |
| `cargo fmt -p vd-settings -- --check` / `cargo clippy -p vd-settings --all-targets -- -D warnings` | 均绿，同上日志 |

截图（offscreen，`settings --screenshot`）：`screenshots/01-shortcuts.png` … `06-format.png`。
Format 页那张是**展开高级区 + 真实值**的状态；Lint 页那张显示"active / not selected"两组与
"已保存未生效"清单。

## 6. 没证明什么 / 已知限制

1. **字体只证到 `QApplication::font().pointSize()`**。`applyFontPointSize` 用
   `QApplication::setFont`（Qt 会对既有 widget 发 `ApplicationFontChange`）+ 顶层窗口
   `updateGeometry/update`；本仓库里**没有**逐个 widget 的字体覆盖审计，若某个 lane 给自己的
   widget 设过字体，它不会跟随。**编辑器（KTextEditor）自己的字体不在此列**——那是 editor lane
   的设置，本任务没有改（Appearance 页已写明）。
2. **缩放的"视觉"效果没测**。证的是"下一次启动的环境里 `QT_SCALE_FACTOR` 正确"（真实二进制：
   150→1.5、200→2、100→无变量），Qt 是否真的按它缩放属于 Qt 契约；offscreen 下无法像素级验证。
   真机观感仍需用户验收（计划 §2 已声明）。
3. **lint 的 6 个 transport-pending 旋钮没有 argv 断言**（因为根本还没发出去）。`rules_config`
   的链路是"面板值 → 组合函数 → `VdLintBatchOptions.rules_path`"，最后一步的 argv 拼装属于既有
   `crates/vd-deeplint`（其自身测试断言 `--rules_config=<path>` 进 argv）。
4. **`.rules.verible_lint` 发现路径未接入**（§4.2）。
5. **QIntValidator 只是 UI 挡板**：手改 `settings.toml` 的非法值由 Rust schema + 面板兜底
   （`settings.a4.read.degrades` 断言降级），不保证手工文本一定被拒。
6. **并发环境**：本任务运行期间 A2 正在改 `src/app/app_shell.cpp`/菜单与 `src/terminal/**`，
   期间出现过一次整树链接失败（`TerminalWidget::event` 未定义，属对方半成品状态，重跑即绿）
   与 A2 的 `a2.f1.*` 断言红；本 lane 的测试行始终为绿。
7. **全树并行 ctest 的一个既有隐患（非本 lane）**：`vedit-diagnostics-ffi-selftest` 在**并行**跑时
   偶发 `SIGPIPE`（单独跑 3/3 通过，整树**串行** 42/42 通过）。这与 AGENT.md 已记录的"lane 自测
   直接 spawn 真实 LSP 子进程且 Rust 静态链接进 C++ 宿主，SIGPIPE 默认处置未被忽略"一致：
   是那个 lane 自测的既有隐患，与 A4 的改动无关（A4 两行在并行/串行两种模式下都通过）。
7. **无 `--state-dir` 时的路径解析角落**：构造 QApplication 之前拿不到 org/app 名，所以启动读
   的候选顺序是 `AppConfigLocation(构造前)` → `$XDG_CONFIG_HOME/VEdit/vedit-app/settings.toml`，
   取第一个**存在**的。若用户恰好在那两个位置各有一个 `settings.toml`，缩放可能读到构造前那个
   而字体/format 读构造后那个——罕见，已在 `startupSettingsCandidates()` 注释里写明。
8. **截图是 offscreen 渲染**，样式取自容器内可用主题（Breeze Light）；置灰控件在扁平样式下
   对比度低，所以每个 Lint 组额外用一行文字说明状态（不是装饰）。
