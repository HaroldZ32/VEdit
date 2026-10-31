## 🛑 人工停点 1 — 待用户在有屏机器上验收（未合并 / 未打 tag / 未发 Release）

本 PR 交付 **v2 迁移的 Slice 1**。此刻分支处于**冻结状态**：工作树 clean，门禁全绿，等待用户 `git pull` 后亲手验收。
**未合并、未打 tag、未发 Release**；交付出口就是这个 PR。

### 门禁（冻结树 `09907ae`，真实输出）
| 门禁 | 命令 | 结果 |
|---|---|---|
| typecheck | `bash build/gates/typecheck.sh` | **`TYPECHECK-EXIT=0`** → `[typecheck] PASS (pinned image vedit-build:local)`（真的在 CentOS 7 / rustc 1.99.0 容器里跑，容器横幅可查） |
| 构建 + 测试 + 冒烟 | `bash scripts/build-qt.sh` | **`BUILD-QT-EXIT=0`**，ctest **`100% tests passed, 0 tests failed out of 16`**，xcb/软件渲染 `M0-SMOKE ok=1` |

> 诚实记录：本停点第一次跑门禁时是红的（`BUILD-QT-EXIT=8`，ctest 15/16，唯一失败 `vedit-workspace-search-selftest` 7 条 `jump.product.*`）。根因是**构建竞态**：构建窗口 `19:51:37→19:52:17`，而 T69 的 `search_selftest.cpp` mtime 是 `19:52:24` —— 编译进来的是一份中间态。等 T69 收敛后重跑即 16/16。两次记录都在 `evidence/slice1-stop/{12-build-qt.log,13-ctest-red.txt,22-build-qt-final.log,23-gates-summary.txt}`。

### Slice 1 交付内容（按 lane）
| lane | 交付物 | 独立评审 |
|---|---|---|
| **T10 编辑内核** | `src/editor/kernel/**` + `src/editor/CMakeLists.txt`，ctest `vedit-edit-core-selftest` | `evidence/verdicts/edit-core/` |
| **T47 终端 + 会话恢复** | `src/terminal/**`，ctest `vedit-terminal-selftest` | `evidence/verdicts/fix-shell-terminal/` |
| **T71 保存（Ctrl+S / 菜单）** | `src/app/app_shell.{h,cpp}`、`main.cpp`（与 T47 双 lane 共写，见豁免表） | `evidence/verdicts/save-binding/` |
| **T6 工程模型 + `.f`** | `crates/vd-config/**`、`src/workspace/project/**` | `evidence/verdicts/project-model/` |
| **T8 Verible LSP** | `crates/vd-lsp/**`、`src/workspace/lsp` 接缝 | `evidence/verdicts/lsp/{t67-verdict.md,t73-verdict.md}` |
| **T12 语法高亮** | `src/syntax/**`、`resources/syntax/vedit-*.xml` | `evidence/verdicts/syntax/` |
| **T69 全局搜索 + 跳转** | `src/workspace/search/**` | `evidence/verdicts/search-uaf/`、`evidence/verdicts/fix-search-jump/` |
| **T59 诊断** | `src/workspace/diagnostics/**` | `evidence/verdicts/`（diagnostics） |
| **T39/T40 filetree** | `src/workspace/filetree/**` | `evidence/verdicts/fix-filetree-open/`、`fix-filetree-provider/` |
| **T54/T55 门禁基础设施** | `build/gates/{typecheck,gates}.sh` | `evidence/verdicts/gate-infra/verdict.md` |
| **T57 仓库卫生** | `.gitignore`、`src/workspace/filetree/tools/generate-testdata.sh` | `evidence/verdicts/repo-hygiene/verdict.md` |

### 证据索引（都在本 PR 里）
- `evidence/slice1-stop/` —— 停点 1 专属：冻结树 C9 头、两次门禁原始输出、ctest 红/绿对照、未验证项
- `evidence/verdicts/**` —— 每个 lane 的独立评审 verdict
- `evidence/captain-exemptions.md` —— **7 条写域豁免登记** + `src/app/{app_shell.h,app_shell.cpp,main.cpp}` 的 T47+T71 双 lane 共写提示，verifier 判读前请先读这张表
- `evidence/{ffi-abi,highlight-v2,docs-amendments,fix-filetree-open}/` 里的 `*-recollect-*` / `*-backfill-*` —— C9 补记。**原始 `collected_at` / `git_rev_parse_HEAD` 一格未改**，补记是独立文件
- `evidence/ffi-abi/**`、`evidence/m0-build/build-logs/**` —— Slice 0 与构建的原始日志（会被后续构建重写，本 PR 里是冻结时刻的内容）

### 用户验收步骤（只有他能做）
```bash
git pull                      # 在本分支
# 构建（见 AGENT.md §4 / docs/BUILD.md）
bash scripts/build-qt.sh
# 打开一个真实工程目录
vedit-app --project <真实工程目录>
```
然后：**双击打开 3 个文件** → **改一行** → **Ctrl+S** → **看诊断是否出现**。
执行服务器**没有显示服务器**，以上没有任何一步能被 agent 代劳；本 PR 里的所有"绿"都只覆盖**无头可证**的部分。

### 明确的边界
- **GUI 一律未验收**：窗口交互、高亮观感、折叠/面包屑、面板手感。
- `M0-SMOKE ok=1` 是 **xcb + 软件渲染**的无头冒烟，不等于人眼验收。
- 未跑全量 MPD 门禁（`rows`/`comments` 是仓库声明式 N/A→RED，与本 PR 无关）。

### 本轮提交（`95c496e..09907ae5e69fcff6a646823619ce80b506d7549b`，共 13 个）

```
5f22bd3 feat(editor): land the KTextEditor editing kernel lane
22e5ef8 feat(terminal): land the pty terminal lane and its selftest
e7445f6 feat(app): wire the shell for session restore and save bindings
a058995 feat(workspace): land the project, diagnostics, search and filetree lanes
1ed2f9f feat(crates): land the rust service crates and the cbindgen header
0584d1e feat(syntax): land the vedit verilog/systemverilog grammar data
8fa1819 build(packaging): land the appdir, tarball and clean-room packaging scripts
5098bbf build(gates): land the gate rewrite and the container build drivers
00128af docs: land the user guide split, license decision and amendments
c86ce6c docs(evidence): land the independent verdict set
cfe708b docs(evidence): record captain exemptions and the two reconstructed PASS verdicts
e8c67a2 docs(evidence): land the remaining lane evidence
09907ae docs(evidence): backfill C9 headers on the frozen tree and record the gate results
```
