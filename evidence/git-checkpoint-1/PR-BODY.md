## ⚠️ 这是 WIP checkpoint，不是完成态

用户直令：「记得及时传GIT，你目前在的服务器没接屏幕看不见GUI」。

此 PR 把 `feat/qt6-ktexteditor-migration` 的当前成果**首次落到远端**，消除「100+ 项改动只存在于一台无屏开发机」的单点丢失风险。多个成员在同一棵工作树上并行写，本轮只纳入 **经 sha256 稳定性探针确认未被并发改写** 的文件；仍在被写入的文件故意跳过。

**不合并不打 tag，不发 Release。** 后续提交追加到本 PR，不重建。真正可验收的状态要等人工停点 1（六份独立评审 PASS）才更新，并由用户在有屏机器上 pull 验收。

- base: `dev` @ `eebbfd14a2311cd4bca533fe98d457a01e46e2e6`
- head: `feat/qt6-ktexteditor-migration` @ `8219324`
- 本轮 `9957f5f..8219324` 共 **18 个提交**
- 采集证据：`evidence/git-checkpoint-1/`

---

## ✅ 已完成（有独立证据闭环）

| 子系 | 证据 |
|------|------|
| **Slice 0：Rust 服务层 + C ABI 纵向切片** | `crates/vd-ffi`（cbindgen 头、28 个导出符号、30 个单测）、`crates/vd-{search,settings,vcs}`、`cmake/VEditRustFfi.cmake`；证据 `evidence/ffi-abi/`（含 ctest 10/10 与证伪红→恢复绿） |
| **T5 独立评审（盲审）** | `evidence/verdicts/ffi/verdict.md` —— **PASS（附带 Medium findings）**；findings 集中在门禁基础设施与证据交付，不阻塞 |
| **仓库内 fixture + 可复跑验收脚本** | `tests/fixtures/verilog-project/`（21 文件）、`scripts/verify-fixture.sh`、`evidence/fixture/` + `evidence/verdicts/fixture/verdict.md` |
| **AMENDMENTS rev2 + 许可登记** | `docs/handover/appendix/amendments-rev2.md`、`THIRD_PARTY_NOTICES.md`、`docs/decisions/r2-license.md`；证据 `evidence/docs-amendments/`、`evidence/verdicts/docs/verdict.md` |
| **T39 filetree 双击打开修复** | `src/workspace/filetree/**`（含 `tools/rootcheck/**`）；证据 `evidence/fix-filetree-open/`：before RED `ok=0 failures=6` → after GREEN `ok=1 failures=0`，根 gate `100% tests passed, 0 failed out of 10` |
| **C9 证据头批量** | `evidence/verdicts/{fix-evidence-c9,syn-grammars,term-panel,ws-filetree,ws-palette,ws-search}/verdict.md` |

## 🚧 在飞（已部分入库，未完成，不代表完成态）

| 子系 | 已入库部分 | 状态 |
|------|-----------|------|
| **Slice 1 · 工程模型 + `.f`（T6）** | `crates/vd-config`（project/render/filelist + fixture 测试） | lane 仍在写，会继续追加 |
| **Slice 1 · Verible LSP（T8）** | `crates/vd-lsp`（session + file-set/degradation 测试） | lane 仍在写 |
| **Slice 1 · 编辑内核（T10）** | — | 尚未落文件 |
| **Slice 1 · 语法高亮（T12）** | `src/syntax/product/`（C++ 产品接缝）、`src/syntax/provider/CMakeLists.txt`、`src/syntax/grammars/CMakeLists.txt`、`evidence/highlight-v2/00-before.txt` | 证据 `evidence/highlight-v2/` 正在生成，未入库 |

## ⏳ 未开始

- **Slice 2**：tree-sitter/KSH overlay、折叠/面包屑、命令面板、VCS 只读面板
- **Slice 3**：设置 UI、FIM、A4 残项、最终打包与 AppImage

---

## 📌 关于证据与门禁的四条口径（请评审/验收时按此读）

1. **HEAD 戳漂移属如实记录，不是伪造**：`evidence/ffi-abi/00-collect.txt` 记录 `HEAD=9957f5f`，那是**采集时刻**的真实 HEAD，早于本 PR 的 checkpoint 提交（`f349342`/`3cdbbe9`/`0904a1d`…）。不篡改原始采集值；一致的 C9 重采留到人工停点 1 冻结树时统一做。
2. **易失构建日志**：`evidence/ffi-abi/logs/19–23,26,27` 与 `evidence/m0-build/build-logs/31–34,37` 会被并发构建反复重写。已在 `0904a1d` **整体提交过一次**（本轮构建的真实输出），其后的重写视为噪声，不再逐条补交。
3. **`typecheck` 门禁红是已知基础设施缺陷（T54），不是 lane 失败**：`build/gates/typecheck.sh` 的 Rust 半段在宿主跑 `cargo`，而宿主 pinned 1.99.0 toolchain 缺 `cargo/clippy` → 该门禁在本宿主必红；同一份日志的 Qt6 半段为 PASS。评审在镜像内重跑的等价 Rust 命令全绿。
4. **本 checkpoint 未跑完整 gate**：提交内容是并行 lane 的快照，**不代表可构建状态**。

## ⚠️ 待决问题

`filetree-testdata/` 现有 **10,007 个文件 / 376K**（其中 10,006 个 0 字节），是 filetree 性能 lane 的生成数据。本轮**未提交**，请裁定：入库为 fixture，还是加 `.gitignore` 由脚本生成。

## 🔍 验证边界（诚实标注）

- **无头可证**：构建、单测、gates、Xvfb 无头 selftest、诊断日志、ELF/GLIBC 检查、AppImage 产物与 SHA256。
- **只能由用户在有屏机器上验收**：一切 GUI 现象（窗口交互、高亮观感、折叠/面包屑、面板手感）。执行服务器**无显示服务器**；「截图看起来对」「我认为它能打开」都不构成人工验收。
- 已做的机器检查：`Cargo.toml` 的 11 个 workspace member 在提交树中全部存在（`git cat-file -e HEAD:<member>/Cargo.toml` 全 OK）。

## 🚫 未做的事（红线）

未合并 PR、未 push `dev`/`main`、未打 tag、未发 Release、未 amend/rebase/force-push、未修改任何产品代码。
