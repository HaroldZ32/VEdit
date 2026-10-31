# T6 — Slice 1：工程模型 + `.f`（Rust 服务，可诊断、可持久）— 交付与证据报告

collected_at: 2026-10-31T18:58:01+08:00
git_head: 1f47e56421bbeaab64f8082df3cfc104a6a2c2ef
git_status_porcelain（本任务的显式路径）:

```
 M crates/vd-config/tests/project_model_fixture.rs
 M src/app/CMakeLists.txt
 M src/workspace/integration.cpp
?? evidence/project-model/
?? src/workspace/project/
```

（其余 `crates/vd-config/src/*`、`crates/vd-ffi/src/project.rs` 已由 Lead 的 checkpoint 提交收录；本次只在其上做了 clippy 修复。）

无头声明：本机没有显示服务器。下文所有结论均为无头可证（构建、单测、ctest、`--selfcheck`、nm/ELF、持久化文件内容）；**没有任何一条**声称 GUI 看起来正确或用户可见效果。

---

## 1. 交付了什么

| 半 | 路径 | 内容 |
|---|---|---|
| Rust 服务 | `crates/vd-config/` | 工程根发现新增 `.f` 标记（`RootSource::Filelist`）；`.f` 诊断改为指向**具体 .f 文件 + 行号**；嵌套 `-f` 的 `..` 路径做词法归一化；新增 fixture 真服务路径测试与反例测试 |
| Rust 服务 | `crates/vd-ffi/src/project.rs` | 服务契约测试（3 个）：fixture 展开与 `expected/` 逐项一致、坏条目诊断字符串带 `文件:行:`、两次展开确定性 |
| Qt 面板 | `src/workspace/project/` | 新 lane：`ProjectState`（原子 JSON 持久化）、`ProjectService`（唯一触碰 `vd_ffi.h` 的文件）、`ProjectModel`（QTimer 异步轮询、restore/refresh/save）、`ProjectPanel`（摘要/文件列表/诊断/Refresh）、`project_integration.cpp`（`installProject`）、`project_selftest.cpp`、CMakeLists、README |
| 集成（**captain 授权**，见 §8） | `src/app/CMakeLists.txt`、`src/workspace/integration.cpp` | lane 列表加 `project`；`VEDIT_WORKSPACE_HAVE_PROJECT` 下 include + `installProject(shell, options)` |
| 证据/复跑 | `evidence/project-model/` | 见 §6，`run-standalone.sh` 可复跑 |

**归属诚实说明**：Rust 服务主体（`Loader`/`ProjectContext`、`vd_project_parse/poll/info_free` 的 C ABI、cbindgen 头、服务句柄与任务注册表）由 **T4（ffi-seam）** 交付并在 T5 评审通过；本任务在其上补齐验收所需的行为与证明，并新增 Qt 侧。C1/C4 的“Qt 不得有第二套实现”成立：`src/workspace/project/` 里没有任何 `.f` 解析代码，全部数据来自 `vd_project_*`。

## 2. 关键修正与依据

### 2.1 工程根：目录直接含 `*.f` 即为工程根（`RootSource::Filelist`）

- 缺陷：T14 的验收 fixture `tests/fixtures/verilog-project` 位于本仓库（`.git` 在父目录）内，而旧 `discover_root` 只认 `vedit.toml`/`.git` → 以 fixture 目录为起点会被判成 VEdit 仓库根 → 根目录没有 `.f` → 展开 **0 文件**。AC-S1.3「UI 显示展开后文件数」在真实服务路径上必然失败（T14 的 `verify-fixture.sh` 自己在 bash 里展开，没走 `vd-config`，所以此前未暴露）。
- 依据：fixture 自己的 `README.md` 明说「the project root is this directory」；AC-S1.3（文件数/列表）与 AC-S1.4（同一文件集喂 SLANG/Verible）要求**每个服务解析出同一个根**。
- 实现：`discover_root` 在每个祖先目录按 `vedit.toml > .git > 直接含 *.f` 判定；同目录既有 `.git` 又有 `.f` 时，根相同、`root_source` 报 `git_dir`（更强的标记）。
- **反例测试**（captain 要求的“根发现反例”）：`crates/vd-config/src/project.rs::tests::a_direct_filelist_roots_its_own_directory_but_never_the_outer_checkout`
  - 外层 checkout 根加载 → `RootSource::GitDir`，根仍是外层（嵌套的 `.f` 不会把根拽下来）；
  - 该子目录加载 → `RootSource::Filelist`，根是子目录；
  - 同目录同时有 `.git` + `*.f` → 根不变，source 报 `git_dir`。
- 已知文档影响（**不在我的写域，已按 captain 安排交给 T52**）：`docs/USER_GUIDE.md:57,176` 的「工程根 = 最近含 `vedit.toml` 或 `.git` 的祖先」。建议 T52 用的原话：
  > 工程根是最近的包含 `vedit.toml` 或 `.git` 的祖先目录；若某个目录**直接含有 `*.f` 清单**，则该目录本身即为工程根（即使它位于更大的 git 仓库内部）。例如 `tests/fixtures/verilog-project`。

### 2.2 坏条目诊断必须能指到“哪一行”

- 缺陷：`filelist.rs` 的行级警告此前用 `.with_file(base)`，`base` 是**所在目录**，于是 UI 收到 `filelist.unknown_token: <目录>:22: …` —— 有行号却没有文件名，用户无法定位是哪个 `.f`。
- 修正：`consume_line/consume_defines/consume_nested` 传入当前 filelist 文件路径，行级警告一律 `.with_file(file).with_line(line)`。模块文档写明该约定。
- 证据：`04-selftest-selfcheck.log` 的 36 项 CHECK 与 `06/09` 里的真实诊断字符串，例如
  `filelist.define_invalid: /tmp/bad-project-t6/project.f:1: malformed +define+ token; skipped`。

### 2.3 嵌套 `-f` 的相对路径归一化

- 缺陷：`filelists/../tb/tb_top.sv` 这样的路径不归一化时，① 同一文件两种写法会绕过去重（跨 filelist 的重复检测失效），② UI 显示 `..`；T14 的 `expected/files.txt` 也是归一化后的形式。
- 修正：`filelist::resolve` 做**词法**归一化（不碰文件系统、不解析 symlink），新增单测 `parent_components_are_normalized`。

## 3. 观测结果（原样摘录）

### 3.1 Rust（pinned CentOS 7 镜像 `vedit-build:local`，rustc/cargo 1.99.0）

`cargo test -p vd-config -p vd-ffi` → 全绿，包含：

```
test project::tests::fixture_expansion_matches_the_service_contract ... ok
test project::tests::bad_entries_render_actionable_diagnostics ... ok
test project::tests::expansion_is_deterministic ... ok
test result: ok. 3 passed; 0 failed
test the_repository_fixture_expands_to_the_checked_in_expectations ... ok
test bad_entries_name_the_filelist_file_and_line ... ok
test result: ok. 2 passed; 0 failed
test result: ok. 12 passed; 0 failed   (vd-ffi 的 ABI 测试，含 project_parse 的既有检查)
```

`cargo fmt --all -- --check` → `FMT-EXIT=0`
`cargo clippy -p vd-config -p vd-ffi --all-targets -- -D warnings` → `SCOPED-CLIPPY-EXIT=0`

完整日志：`16-rust-gates.log`。

### 3.2 Qt lane standalone（pinned Qt6 镜像，Qt 6.10.2）

`cmake -S src/workspace/project … && cmake --build … && ctest`：

```
100% tests passed, 0 tests failed out of 1
PROJECT-MODEL-SELFTEST ok=1 checks=36 failures=0
```

日志：`01-standalone-configure.log`、`02-standalone-build.log`、`03-standalone-ctest.log`、`04-selftest-selfcheck.log`。

36 项 CHECK 覆盖：服务启动非阻塞（<250 ms）与 pending、7 个成员且与 fixture `expected/files.txt` 完全一致、4 个 incdir、4 个 define、重复条目与未知 token 诊断、诊断带 `project.f:`、摘要含 “7 files”、面板实际渲染的文件数与列表/诊断与模型一致、空状态、损坏 state 文件降级、进程内持久化回环。

### 3.3 重启持久化（**两个进程**，真重启）

```
SEED    phase=seed    ok=1 files=7 root=/vedit-build/tests/fixtures/verilog-project state=…/project-model.json
RESTORE phase=restore ok=1 files=7 root=/vedit-build/tests/fixtures/verilog-project error=
```

第二个进程只拿到 state 目录（没有 `--project`），仍列出 7 个成员。持久化文件内容见 `06-persisted-state.json`。日志：`05-restart-seed.log`、`07-restart-restore.log`。

### 3.4 面板真的进了产品二进制（before / after）

| | BEFORE（集成前，sha256 `df7f3d74…`） | AFTER（集成后，sha256 `4513e96d…`） |
|---|---|---|
| `nm -C vedit-app \| grep -c installProject` | `0` | `2` |
| `nm -C \| grep workspace::project::Project(Model\|Panel)` | 0 | 有：`installProject`、`ProjectModel::*`、`ProjectPanel::*`、两个 `staticMetaObject` |
| `strings -e l \| grep dock-project`（QStringLiteral 是 UTF-16） | `0` | `1` |
| `ctest -N` 里的工程面板测试 | 无 | `Test #14: vedit-workspace-project-selftest` |

证据：`12-before-after-symbols.txt`（before）、`20-product-binary-proof.log`（after + 该测试运行 `100% tests passed`）。

根构建目录 `target/qt6-build` 由 `build/gates/typecheck.sh` 的 C++ 半段重新配置/编译（构建日志 `17-typecheck-gate.log` 第 505–524 行：`[30/50] … vedit-workspace-project-selftest` … `[50/50] Linking CXX executable src/app/vedit-app`）。

根 ctest（`19-root-ctest.log`）：`13/14` 通过，**我的 `#14 vedit-workspace-project-selftest` 通过**；唯一失败 `#10 vedit-edit-core-selftest (Not Run)` 是 **T10 编辑内核 lane 在飞**（其测试二进制未产出），不是本 lane。

## 4. 门禁现状（如实区分“我这条”与“别人的”）

- `bash build/gates/typecheck.sh` → **`TYPECHECK-EXIT=1`**（`17-typecheck-gate.log`）。原因分两块，均不在本任务代码：
  1. **C++ 半段：基础设施 bug（真红，但不是代码缺陷）**。`scripts/build-qt-in-container.sh:90` 用
     `if ! nm -C "$APP" | grep -qE ' T vd_abi_version$'`，而脚本自身 `set -o pipefail`。`grep -q` 命中即退出 → `nm` 收到 SIGPIPE（141）→ 管道判非零 → 打印 `FATAL: vedit-app does not export vd_abi_version`。**但同一脚本上一条命令的 nm 列表里明明有 `T vd_abi_version`、`## count 36`。** 容器内复现：
     ```
     pipefail+grep-q   status=141
     pipefail+grep-no-q status=0
     ```
     建议修复（属 build-infra 写域，一行）：把该行改成 `nm -C "$APP" | grep -E ' T vd_abi_version$' > /dev/null`（去掉 `-q`，或先把 nm 输出落文件再 grep）。这是潜伏 bug：它被本 lane 增加的符号量顶过 64 KiB 管道缓冲后才稳定触发，此前 T39 的 `build-qt.sh` 是绿的。
  2. **Rust 半段：`cargo clippy --workspace --all-targets -- -D warnings` 剩余的 3 处错全在 `crates/vd-lsp/tests/session_file_set.rs`（T8 lane）**：`needless_question_mark`、`manual_contains`。本任务 scope 的文件已清：`cargo clippy -p vd-config -p vd-ffi --all-targets -- -D warnings` → 0。我没有改 T8 的文件。
- `cargo fmt --all -- --check` → `FMT-EXIT=0`。
- 因此 `bash scripts/build-qt.sh` 目前也会在那条 ABI 断言处失败（同一脚本），这不是本 lane 能修的，见上。

## 5. 已知限制（声明，不隐藏）

1. **View 菜单没有 project 面板 toggle**：面板用 `QMainWindow::addDockWidget` 直接插入（objectName `dock-project`），**未注册** `PanelRegistry`（注册表在 `src/app/app_shell.cpp`，本任务只获授权改两处共享文件）。代价：用户不能从菜单/快捷键开关该面板；有工程根时它出现在左侧，并随 `QMainWindow::saveState` 的 dock 布局一起保存。若需要 toggle，是一处新的 app-shell 改动（panel spec + command），应单独评审。
2. **无显示服务器**：面板外观、交互手感、真实窗口里的中文/字体/DPI 表现**无头不可证**；AC-S1.6 的用户手工验收未做。
3. **根构建的一命令全集**（`bash scripts/build-qt.sh` 到 Xvfb/xcb smoke）被上述 infra bug 拦住；本报告给出的是“目标级根构建 + 产品二进制符号 + 根 ctest”的可复跑证明。
4. `target/` 下 `libvd_ffi.a` 由容器重建（root 所有，gitignored），不影响源码树。

## 6. 证据清单（`evidence/project-model/`）

| 文件 | 内容 |
|---|---|
| `run-standalone.sh` | 可复跑：容器内 configure/build/ctest、36 项 selftest、seed→restore 两进程、坏条目、fixture 期望值 |
| `01..04` | standalone configure/build/ctest/selfcheck 日志（带 C9 头） |
| `05,06,07` | seed 进程、持久化 JSON、restore 进程 |
| `08` | fixture 文件清单与 `expected/*.txt` |
| `09` | 坏条目诊断样例（真实字符串：`文件:行: 原因`） |
| `11` | 首次全量 `scripts/build-qt.sh`（当时被 T41/T10 在飞改动打断；引用其 `BUILD-QT-EXIT=1` 与外部原因） |
| `12` | BEFORE 二进制事实（sha256、installProject=0、dock-project=0） |
| `13` | 目标级根构建（`vedit-app` + `vedit-workspace-project-selftest`） |
| `16` | pinned 镜像内 rustc/cargo 版本、fmt/clippy/测试输出 |
| `17` | 权威 `build/gates/typecheck.sh` 全日志（含 §4 的 FATAL 与 nm 证据） |
| `19` | 根 ctest 全量（13/14，我的 #14 通过；#10 为 T10 在飞） |
| `20` | AFTER 产品二进制符号证明 + 面板 ctest 通过 |

## 7. 复跑

```sh
# Rust 服务（容器，pinned 1.99.0）
docker run --rm -e HOME=/vedit-build/target/home -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust -e RUSTUP_TOOLCHAIN=1.99.0 -v "$PWD":/vedit-build -w /vedit-build \
  vedit-build:local bash -lc 'cargo test -p vd-config -p vd-ffi'

# Qt lane + 两进程重启持久化（容器，pinned Qt 6.10.2；无需显示器）
bash evidence/project-model/run-standalone.sh
```

## 8. 写域与豁免记录

- 本任务写域：`crates/vd-config/`、`crates/vd-ffi/src/project.rs`、`src/workspace/project/`、`evidence/project-model/`。
- **豁免来源**：captain 于 2026-10-31T10:33Z 前后经 cellwork 消息授权我改 `src/app/CMakeLists.txt`（lane 列表加 `project`）与 `src/workspace/integration.cpp`（include + `installProject` 调用）；授权理由是 T39 已完成、`integration.cpp` 写入窗口已空。两处改动均为最小 diff（见 `git diff`）。
- 未动：`crates/vd-ffi` 共享文件（lib.rs/Cargo.toml/build.rs/cbindgen/头文件）、根 `Cargo.toml`/`Cargo.lock`/`rust-toolchain.toml`、`src/app/main.cpp` 的 `--selfcheck-ffi`、`vd-deeplint --test real_slang`、`docs/**`、`resources/syntax/**`、T12 的 grammar。
- 未 commit / 未 push（按 captain 纪律，Git 由 Lead 统一处理）。
