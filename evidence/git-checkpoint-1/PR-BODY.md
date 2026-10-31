## ⚠️ 这是 WIP checkpoint，不是完成态

用户直令：「记得及时传GIT，你目前在的服务器没接屏幕看不见GUI」。

此 PR 只做一件事：把 `feat/qt6-ktexteditor-migration` 的当前成果**第一次落到远端**，消除「100+ 项改动只存在于一台无屏开发机」的单点丢失风险。多个成员正在同一棵工作树上并行写，本轮只提交 **mtime 已冷却（> 5 分钟）** 的文件，仍在飞的文件故意不提交，避免把半写状态固化。

**不合并不打 tag，不发 Release。** 后续提交追加到本 PR，不重建。真正可验收的状态要等人工停点 1（六份独立评审 PASS）才更新，并由用户在有屏机器上 pull 验收。

- base: `dev` @ `eebbfd14a2311cd4bca533fe98d457a01e46e2e6`
- head: `feat/qt6-ktexteditor-migration` @ `3cdbbe9`
- 本轮新增 7 个 checkpoint 提交：133 文件 / +12,841 行 / −12 行
- 采集证据：`evidence/git-checkpoint-1/`

### 本轮已提交（冷文件，落远端安全）

| # | commit | 子系 | 内容 |
|---|--------|------|------|
| 1 | `f349342` | Rust 服务层 + FFI 构建接线 | `Cargo.toml`/`Cargo.lock`/`CMakeLists.txt`、`cmake/VEditRustFfi.cmake`、`crates/vd-ffi`（22 文件：C ABI、cbindgen 头、abi 测试与 fixture）、`crates/vd-search`、`crates/vd-settings`、`crates/vd-vcs` |
| 2 | `6204865` | 仓库内 fixture + 验收脚本 | `tests/fixtures/verilog-project`（21 文件）、`scripts/verify-fixture.sh` |
| 3 | `057b6f1` | rootcheck cmake | `src/workspace/filetree/tools/rootcheck/CMakeLists.txt` |
| 4 | `4929488` | Qt6 构建脚本 | `scripts/build-qt.sh`、`scripts/build-qt-in-container.sh` |
| 5 | `e065c77` | 文档 + 第三方许可登记 | `docs/handover/README.md`、`docs/decisions/r2-license.md`、`THIRD_PARTY_NOTICES.md` |
| 6 | `a7ce7e9` | 各 lane 证据 | 66 文件：`evidence/syn-grammars/`、`evidence/term-panel/`、`evidence/ws-filetree/`、`evidence/ws-palette/`、`evidence/ws-search/`、`evidence/fixture/`、`evidence/highlight-v2/00-before.txt` |
| 7 | `3cdbbe9` | app shell 入口 | `src/app/CMakeLists.txt`、`src/app/main.cpp` |

### 仍在飞，本轮故意未提交（mtime < 5 分钟）

| 路径 | 状态 |
|------|------|
| `crates/vd-config/src/{project,lib,render}.rs` | 正在写 |
| `crates/vd-lsp/src/{session,lib}.rs` | 正在写 |
| `src/workspace/filetree/filetree_integration.{h,cpp}`、`tests/filetree_tests.cpp`、`tools/rootcheck/seamcheck.cpp` | 正在写 |
| `src/syntax/grammars/CMakeLists.txt`、`src/syntax/provider/CMakeLists.txt`、`src/syntax/product/` | 正在写 |
| `docs/handover/appendix/amendments-rev2.md` | 正在写（C15） |
| `evidence/m0-build/build-logs/{20,21,31,32,33,34,37}` | gate 运行中 |
| `evidence/verdicts/`、`evidence/ffi-abi/`、`evidence/docs-amendments/`、`evidence/fix-filetree-open/` | 采集进行中 |
| `filetree-testdata/` | 10,007 个生成文件，正在生成；是否入库待决策（见下） |

### 待决问题

`filetree-testdata/` 现有 **10,007 个文件 / 376K**，是 filetree 性能 lane 的生成数据。本 checkpoint **未提交**，建议船长裁定：入库为 fixture，还是加 `.gitignore` 由脚本生成。

### 尚未开始 / 未完成

T5 / T7 / T9 / T11 / T13 / T15 六份独立评审在看板上仍为 `pending`，因此本 PR **不宣称任何 Slice 1 子系完成**，也不宣称构建通过。

### 验证边界（诚实标注）

- **无头可证**：构建、单测、gates、Xvfb 无头 selftest、诊断日志、ELF/GLIBC 检查、AppImage 产物与 SHA256。
- **只能由用户在有屏机器上验收**：窗口交互、高亮观感、折叠/面包屑、命令面板手感等一切 GUI 现象。执行服务器**无显示服务器**，任何"截图看起来对 / 我认为能打开"都不构成人工验收。
- 本 PR 未运行完整 gate；提交内容是并行 lane 的快照，**不代表可构建状态**。
