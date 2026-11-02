# Build registration — 三条 lane 真正进入根构建 + 终端面板接线（captain 直管任务）

- 日期：2026-10-31（Asia/Shanghai）· 分支 `feat/qt6-ktexteditor-migration` · HEAD `4775bee`
- 问题（与 T63 同类假绿）：根 `CMakeLists.txt` 的 `VEDIT_LANE_DIRECTORIES` 只有
  `src/syntax/provider` / `src/syntax/kde`，而 `src/terminal`（T35）、`src/syntax/grammars`（T17）
  在树里却从未被根构建编译；`src/syntax/overlay`（T18）尚未落地。`src/app` 也没有把终端面板工厂
  接上、没有链接 `vedit_terminal`。

## 改了什么（3 个文件，+14 行，无删除）

| 文件 | 改动 |
|---|---|
| `CMakeLists.txt` | `VEDIT_LANE_DIRECTORIES` 追加 `src/syntax/grammars`（T17）、`src/terminal`（T35）、`src/syntax/overlay`（T18，带 `if(EXISTS)` 守卫；目前不存在 → 配置期跳过）。原有两行**字节未动**（保住 T35 rehearsal 的 needle 前缀）。 |
| `src/app/CMakeLists.txt` | 在 `vedit-app` 的 `vedit_app_shell` 链接之后加 `if(TARGET vedit_terminal) target_link_libraries(vedit-app PRIVATE vedit_terminal) endif()`。 |
| `src/app/main.cpp` | 加 `#include "terminal_panel.h"`（不是 `terminal/terminal_panel.h`：`vedit_terminal` 发布的 include 目录就是 `src/terminal`）；在 `installPanels(...)` 之后、`shell.show()` 之前加 `shell.panels()->setFactory(QStringLiteral("terminal"), vedit::terminal::terminalPanelFactory());`（`shell` 是值，必须用 `.`；`setFactory` 签名见 `src/app/panel_registry.h:40`）。 |

没有改任何 lane 实现、没有 commit。

## 绿侧（`01-build.txt`）

`bash scripts/build-qt.sh` → **exit 0**，配置期输出：

```
-- VEdit: building lane directory src/syntax/provider
-- VEdit: building lane directory src/syntax/kde
-- VEdit: building lane directory src/syntax/grammars
-- VEdit: building lane directory src/terminal
-- VEdit: lane directory src/syntax/overlay not present yet; skipped
```

真实编译/链接（不是 skipped/空）：`libvedit_tree_sitter_verilog.a`（parser.c，C）、
`libvedit_syntax_grammars.a`、`libvedit_terminal.a`、`vedit_syn_grammars_tests`、
`vedit-terminal-selftest`；`ctest` **9/9 passed**
（含 `vedit-syn-grammars-tests`、`vedit-terminal-selftest`）。

## 目标清单（`02-targets.txt`）

`ninja -t targets all` 里的真实目标 + 磁盘产物（大小/时间）：

```
src/syntax/grammars/libvedit_tree_sitter_verilog.a   18,356,932 B
src/syntax/grammars/libvedit_syntax_grammars.a          461,916 B
src/syntax/grammars/vedit_syn_grammars_tests         21,454,768 B
src/terminal/libvedit_terminal.a                      7,824,886 B
src/terminal/vedit-terminal-selftest                  4,301,656 B
src/app/vedit-app                                    18,906,984 B
```

`ctest -N`：9 个测试全部注册（m0-smoke、syntax-provider、syntax-kde-data、syn-grammars、
terminal、app-shell、workspace-palette×2、workspace-search）。

## 链接证据（`03-link.txt`）

1. `ninja -t commands src/app/vedit-app` 的**真实链接命令行**包含
   `src/terminal/libvedit_terminal.a`（同时含 `libvedit_app_shell.a`、workspace 各库）；
2. `ninja -t query src/app/vedit-app` 的 inputs 列出 `src/terminal/libvedit_terminal.a`；
3. `nm -C target/qt6-build/src/app/vedit-app`：`T vedit::terminal::terminalPanelFactory()`，
   共 **267** 个 `vedit::terminal::` 符号；
4. `nm -C .../vedit-app.dir/main.cpp.o`：`U vedit::terminal::terminalPanelFactory()` 与
   `U vedit::app::PanelRegistry::setFactory(...)`（调用点真的编译进了 app）。

## 红侧反例（`00-counterexample.txt`，必做项）

隔离副本 `target/counterexample-registration`（同一脚本、同一固定镜像；**未动 live 树与三条 lane 实现**）：
给两个**已存在**的 lane 各追加一行 `#error`（`src/terminal/terminal_panel.cpp`、
`src/syntax/grammars/bindings.cpp`），并在副本里新建一个假的 `src/syntax/overlay` lane
（`#error`）来证明第三条 lane 条目在目录出现后会真正被编译。

- `bash scripts/build-qt.sh`（副本）→ **exit 1**；配置期五个 lane 目录全部出现
  （含 `src/syntax/overlay`），编译期 `#error` 失败，`ninja: build stopped: subcommand failed`。
- 同一副本 `cmake --build ... -- -k 0` → 三条 lane 的 `#error` **全部**报出
  （overlay / grammars / terminal）。
- 副本需要 `third_party/` 与 `resources/`（grammars lane 在配置期检查 vendored parser 与
  queries）；缺它们会先在配置期失败（也在日志里，属于环境缺件，不是覆盖证明）。

## 独立评审

Reviewer（只读）对 diff + 四份证据独立复核：**PASS**（risk Medium，无 Blocker/High），四项验收
（范围、真实编译+9/9 ctest、真实链接+符号、反例真实）逐条有证据。其非阻塞发现见下一节，
原文要点已并入。

## 遗留发现（不阻塞本任务；不是本任务边界内该修的东西）

1. **Medium — `VEDIT_SYNTAX_GRAMMAR` 缓存冲突**：`src/syntax/provider/CMakeLists.txt:25` 先以
   `none` 写入 CACHE，根构建里 `src/syntax/grammars/CMakeLists.txt:36` 的 `verilog` 默认值被忽略 →
   根构建实际是 `VEDIT_SYNTAX_GRAMMAR=none`（`31-qt6-configure.log` 打印
   `syn-grammars: compiled grammar id='none'`）。当前 app 未链接 grammars，无用户可见影响；
   一旦链接或消费该 id 就会变成功能错配。建议单一 owner + 硬编码期望的测试（T17/T15）。
2. **Medium — 会话恢复顺序**：`ApplicationShell` 构造函数先 `restoreSession()`（
   `src/app/app_shell.cpp:74` → `applyPanelsFromSession()` → `insert()`），此时 terminal factory 仍为空，
   恢复出来的终端 dock 是占位符；`main.cpp` 之后才 `setFactory`（只改 spec，不重建已插入 dock）。
   工作区面板同样受此顺序影响，修复应在 shell 侧（注册早于恢复 / 延迟插入 / setFactory 刷新已插入
   dock），属 T13/T35。
3. **Nit — 守卫只护链接**：`if(TARGET vedit_terminal)` 只包了链接，`main.cpp` 仍无条件 include/调用；
   没有 terminal lane 的部分树会在编译期失败，与注释里"missing lane => skip"的承诺只兑现一半。
   建议加 `VEDIT_HAVE_TERMINAL` 编译开关包住 include/注册，或明确 terminal 为必需。
4. **Nit — T35 的 root rehearsal needle 失效**：`evidence/term-panel/run-root-rehearsal.sh:34`
   硬编码了旧 lane 列表结尾，现在 `needle in text == False`，该脚本会 FATAL。属 T35 工具维护
   （本任务 3 文件写域之外）；且真实根构建已包含 terminal，该 rehearsal 可以退役。
5. **Nit — 6 个被 gate 重写的 tracked 日志**：`evidence/m0-build/build-logs/{20,21,31,32,33,34}-*`
   被本次绿构建重写（这是 `scripts/build-qt.sh` 的正常行为）。提交时请决定：单独作为证据提交，
   或排除在本次 registration 提交之外。
6. **Nit — 9/9 ctest 不覆盖注册本身**：`runSelfCheck()`/`captureScreenshots()` 在注册行之前
   return，因此 app selftest 走的是 placeholder 路径；注册的证明目前是链接/符号级。建议加一个
   走正常注册路径并断言 factory 的测试模式。

## 复跑

```sh
bash scripts/build-qt.sh                 # 绿：exit 0，9/9 ctest；日志落 evidence/m0-build/build-logs/
# 红（隔离副本，勿在 live 树加 #error）：
#   见 00-counterexample.txt 末尾 "reproduce" 段（复制 CMakeLists/cmake/scripts/src/third_party/resources，
#   注入三处 #error，再 bash <copy>/scripts/build-qt.sh 与 -k 0 全量收集）
```

证据：`00-counterexample.txt`（红，含复现命令）· `00b-counterexample-full-first-run.log` /
`00c-counterexample-k0-full.log`（红的未裁剪原始日志）· `01-build.txt`（绿）·
`02-targets.txt`（目标/产物/ctest）· `03-link.txt`（链接命令 + ninja query + nm 符号）。
