# 最终验收矩阵 — cell `cell-20261103T123639841Z`（requirements rev3）

> **判定口径（写给 verifier）**：本矩阵的每一行都以「冻结契约 + 本文档 + `evidence/` 下的证据」为准，
> **不**采信写者的过程叙述；行内指针给出 record id（`.mpd/verify/records/<id>.json`）、证据目录/文件、
> 或门禁 evidence id（`.mpd/verify/evidence/<id>.log`）。旧矩阵
> `evidence/final-acceptance/AC-MATRIX.md`（**46 个不同 AC id / 47 个 `| AC-` 表格行**，其中 AC-8.1 出现两行；
> mtime 仍是 2026-11-03 01:38）**原样保留、未改写**，本轮不复用其文件（AC-0.3）。
>
> 任务：T33 / cell `cell-20261103T123639841Z` · 契约 `docs-final-matrix.md`（loop `loop-20261103T131047-664c9f`）
> · Owner Junior Engineer · Verifier **Architect**（T34）
> 验收树：HEAD `4e2d0a57ca50db9568db5a5b040cc66f04c9a05f`（`4e2d0a5`，main，**零 git 动作**）

## 0. 一句话结论

本轮 47 条 requirements AC（AC-0.1…0.5 / 1.1…1.5 / 2.1…2.4 / 3.0…3.7 / 4.1…4.5 / 5.1…5.4 /
6.1…6.7 / 7.1…7.9）**逐条 PASS，未验 = 0**；旧 46 行矩阵**未改写**且仍为 PASS；七道门禁在终态树上
全绿（§7 贴命令与退出码）；交付物为 Qt6 AppImage（§1）。

## 1. 交付形态与工具链（AC-7.4 / AC-3.0 / AC-6.1 的终态汇总）

| 项 | 值 | 证据 |
|---|---|---|
| 交付物 | **AppImage 130,181,624 B**，sha256 `6076ff87f4ec60b54005790295e62c308b309b1df50bc50928fc934182e89edd` | `evidence/packaging-2/README.md` §2、`50-appimage.log` |
| AppDir | **347** 个文件；`bin/translations/` 两个 `.qm` 进 `runtime-manifest.txt` | `evidence/packaging-2/appdir-files.txt`、§2 |
| 打包链 | `bash packaging/runtime/build.sh` 全链 **exit 0 / `steps_failed=0`**（AppDir→自检→AppImage→隔离→CentOS 7 探测） | `evidence/packaging-2/00-run.log` |
| 随包 VCS 工具链 | `git 2.53.0` + `subversion 1.14.5` 在 `usr/lib/vcs/`（57 库 ldd 闭包，与产品 `lib/` 互不遮蔽）；解析顺序 **bundled > PATH > env**；可观测行 `VCS-TOOLCHAIN git=… version=2.53.0 svn=… version=1.14.5 source=bundled`；`svnadmin` **不随包**（build-only） | `evidence/vcs-toolchain/README.md` §1–§3、`evidence/packaging-2/60-selfcheck-appimage.log` |
| 验证环境 | 干净 **CentOS 7.9.2009 / glibc 2.17** 容器：`AppRun --selfcheck` = `ok=1 checks=290 failures=0`，`git 2.53.0`、`svn 1.14.5` | **`evidence/packaging-2/72-clean-container-e2e.log`**（脚本 `evidence/packaging-2/scripts/clean-container-e2e.sh`）；另 `evidence/packaging-2/60-selfcheck-appimage.log` |
| 许可 | 产品 **MIT**；捆绑 **git GPL-2.0-only**；**Subversion/APR/serf Apache-2.0**；**utf8proc MIT**；Noto/Slang 各自许可 —— 均登记并给出源码获取方式 | `THIRD_PARTY_NOTICES.md` §10–§11、`evidence/vcs-toolchain/README.md` §5、`evidence/packaging-2/RUNTIME-NOTICES.txt` |

## 2. 携带豁免（**不得记为缺陷**）

| 项 | 事实 | 依据 |
|---|---|---|
| MPD 门禁 `typecheck` exit 1 | 因 `check_ffi_header_drift`：生成的 `crates/vd-ffi/include/vd_ffi.h` 与 HEAD 不同；该文件在**本 cell 开工前**即已脏（`evidence/baseline-2/00-head.txt` + 基线 porcelain），若 git 交付重新裁定则转绿 | captain 书面豁免；`evidence/captain-exemptions.md` |
| MPD 门禁 `tests` argv `bun test packages` | 该路径在本仓库不存在，脚本自身已写明这是 Rust 工作区的另一套入口；C++ 侧由 ctest 覆盖 | 同上；`package.json` 注释 |
| `ctest 38 → 37` | 批 7b（T29）删除死代码 lane 时少了 2 个 `add_test`、重新注册 1 个（`vedit-fold-gutter-click` 同名保留），净 -1。**已评审通过**：等价覆盖仍在（`vedit-syntax-directory-equivalence`），真实 gutter 点击判据仍在运行（`hidden 0→38→0`） | `evidence/dead-code/README.md` §5–§6、`16-check-ids-after-repair.txt`、`rec-20261104T062309-c7a2db`（PASS） |

## 3. 批次状态（一行一批；record id 在 `.mpd/verify/records/`）

| 批 | 内容 | 判定 | record id | 证据目录 |
|---|---|---|---|---|
| 0 | 开工基线快照（porcelain + -uall） | PASS | `rec-20261103T131426-fd3e6e` | `evidence/baseline-2/` |
| 1a | 主壳地基：根传播 / 运行时切换 / 面板恢复 | PASS | `rec-20261103T134429-b22908` | `evidence/batch1-shell/REPORT.md` |
| 1b | Save As + 启动根持久化 | PASS | `rec-20261103T140857-e4b166` | `evidence/batch1-shell/REPORT-1b.md` |
| 2 | 编辑器导航（rename / references / 不回归） | PASS | `rec-20261103T143915-3a28d4` | `evidence/batch2-editor/REPORT.md` |
| 3a | VCS 写入口地基：读拒绝写 + 写白名单 + 异步 job | PASS | `rec-20261103T150508-986cb4` | `evidence/vcs-write/` |
| 3a-fix | 读路径兼容修复（AC-3.0 的读面） | PASS | `rec-20261103T153055-16afb8` | `evidence/vcs-read-compat/` |
| 3b | 三栏 UI 基线 + 自动刷新 | PASS | `rec-20261103T161032-85da54` | `evidence/vcs-ui/` |
| 4a | 远端同步 + 分支/标签 + 凭据对话框 | **FAIL → 修复 → PASS** | FAIL `rec-20261103T170216-cb85a4` → PASS `rec-20261103T172347-16150f` | `evidence/vcs-git-remote/` |
| 4b | 历史查看（列表/详情/单文件/blame/比较） | PASS | `rec-20261104T013854-7e67b0` | `evidence/vcs-git-history/` |
| 5a | 历史操作（merge/rebase/cherry-pick/revert/reset） | PASS | `rec-20261104T022330-508c00` | `evidence/vcs-git-ops/` |
| 5b | 工作区（stash / worktree / submodule） | PASS | `rec-20261104T032832-b5dc45` | `evidence/vcs-git-workspace/` |
| 6a | VCS 工具链捆绑（git 2.53 + svn 1.14.5） | PASS | `rec-20261104T041304-a0e2a1` | `evidence/vcs-toolchain/` |
| 6b | SVN 读写 E2E + 面板对齐 + 版本能力 | PASS | `rec-20261104T050053-d56d4f` | `evidence/svn-e2e/`、`evidence/vcs-toolchain/` |
| 7a | i18n 收口（F8 状态栏 + G7 报告 + verify_claims） | **FAIL → 修复 → PASS** | FAIL `rec-20261104T053841-f60d47` → PASS `rec-20261104T055201-94c638`（repair rep-…e7ab5b） | `evidence/i18n-l2/` |
| 7b | 死代码删除（`vedit_syntax_folding` 等） | **FAIL → 修复 → PASS** | FAIL `rec-20261104T055834-63ba3a` → PASS `rec-20261104T062309-c7a2db`（repair rep-…f7d666） | `evidence/dead-code/` |
| 7c | 打包端到端 + xcb 视觉评审 | PASS | `rec-20261104T064001-226f83` | `evidence/packaging-2/`、`evidence/xcb-visual-2/` |
| 7-cleanup | 收尾清扫（typed job payload 等） | PASS | `rec-20261104T063243-c53ee6` | `evidence/vcs-cleanup/` |
| 跨批修复 | `.ts` 绝对行重建 + 对账恢复 | PASS | `rec-20261104T063449-ab43fc` | `evidence/i18n-l2/logs/51…59-t51-*.log` |

## 4. AC 逐条覆盖（47 行，未验 = 0）

| AC | 判据 | 判定 | 证据指针 |
|---|---|---|---|
| AC-0.1 | 七道门禁全绿 | PASS | §7 门禁表（终态树）。**`ctest` 是 37 而不是 38：携带豁免**——批 7b 的评审通过记录 `rec-20261104T062309-c7a2db`、等价覆盖 `vedit-syntax-directory-equivalence` 与 `evidence/dead-code/16-check-ids-after-repair.txt`，见 §2 第三行 |
| AC-0.2 | 每个 work 有不同 agent 的 PASS verdict | PASS | §3 表的 record id（全部 `verdict:"PASS"`，verifier ≠ writer）。**范围说明**：本 cell 的规划/管理行 **T35–T38 / T43 / T44** 是 cell 自身的 planning/管理任务，不是产品 AC，不计入 AC-0.2 的 work-task 集合；与产品 AC 对应的是 §3 表里各批的 work 行 |
| AC-0.3 | 旧矩阵全 PASS 且本轮新矩阵 0 未验 | PASS | `evidence/final-acceptance/AC-MATRIX.md`（46 个 AC id / 47 行，未改写，mtime 2026-11-03 01:38）+ 本文件 §4（47 行，未验 0） |
| AC-0.4 | VCS E2E 全在临时 fixture，工作树零写 | PASS | `evidence/vcs-write/README.md` §4、`evidence/vcs-git-remote/README.md` §6（probe 断言 cwd ≠ 工作树） |
| AC-0.5 | 开工基线落 `evidence/baseline-2/` 并与终态对账 | PASS | `evidence/baseline-2/00-head.txt`…`03-accounting.txt`、`rec-20261103T131426-fd3e6e` |
| AC-1.1 | 普通启动根传播（四面板） | PASS | `evidence/batch1-shell/REPORT.md`（AC-1.1 节）、`rec-20261103T134429-b22908` |
| AC-1.2 | 运行时切换根（Files/Search/VCS/文件集） | PASS | `evidence/batch1-shell/REPORT.md`（AC-1.2 节） |
| AC-1.3 | 面板可恢复（View 菜单 + saveState/restoreState） | PASS | `evidence/batch1-shell/REPORT-1b.md` |
| AC-1.4 | Save As（落盘/标题/会话/取消/覆盖确认） | PASS | `evidence/batch1-shell/REPORT-1b.md` |
| AC-1.5 | ctest 或 CHECK 数增加且断言不减 | PASS | `evidence/batch1-shell/REPORT-1b.md` §断言 |
| AC-2.1 | F2 Rename 真落盘 + undo + 真实原因 | PASS | `evidence/batch2-editor/REPORT.md` |
| AC-2.2 | Shift+F12 References 列表 + 跳转 + 空态 | PASS | 同上 |
| AC-2.3 | 既有 LSP 动作不回归 | PASS | 同上（重跑记录） |
| AC-2.4 | 新增断言只增不减 | PASS | 同上 |
| AC-3.0 | 随包工具链解析 + 真实操作 + 干净容器版本（**在 6a 结清**，见 §5 注） | PASS | `evidence/vcs-toolchain/README.md`、`evidence/packaging-2/60-selfcheck-appimage.log`、`rec-20261104T041304-a0e2a1`、`rec-20261103T153055-16afb8` |
| AC-3.1 | 读拒绝写 + 写白名单 + 正反测试 | PASS | `evidence/vcs-write/README.md` §2–§3、`crates/vd-vcs/tests/readonly.rs`、`rec-20261103T150508-986cb4` |
| AC-3.2 | 异步写 job 三件套 + ABI 一致 + 事件循环 tick | PASS | `evidence/vcs-write/README.md` §1、`evidence/vcs-read-compat/README.md`、`rec-20261103T150508-986cb4` |
| AC-3.3 | Git 基础写（stage/unstage/discard/commit/amend）+ 确认 | PASS | `evidence/vcs-write/README.md` §5、`evidence/vcs-ui/README.md` |
| AC-3.4 | 三栏 UI 四态 + 1500x1000 截图 + 关闭重开 | PASS | `evidence/vcs-ui/README.md`、`evidence/xcb-visual-2/README.md` |
| AC-3.5 | 自动刷新（保存/操作/激活，同秒合并） | PASS | `evidence/vcs-ui/README.md`（刷新计数节） |
| AC-3.6 | 两处旧断言替换为更强断言 | PASS | `evidence/vcs-write/README.md` §3（35→48 checks） |
| AC-3.7 | 新增字符串 i18n，lrelease 0 unfinished | PASS | `evidence/vcs-ui/README.md`、`evidence/i18n-l2/logs/56-t51-lrelease.log` |
| AC-4.1 | 远端同步对本地裸仓库真实跑通 + 失败给真实 stderr | PASS | `evidence/vcs-git-remote/README.md` §1–§4、`rec-20261103T172347-16150f` |
| AC-4.2 | 分支/标签 CRUD + 危险项确认 | PASS | `evidence/vcs-git-remote/README.md` §5 |
| AC-4.3 | 历史查看（分页/搜索/diff/单文件/blame/比较，≥1000 commits 异步） | PASS | `evidence/vcs-git-history/README.md`、`rec-20261104T013854-7e67b0` |
| AC-4.4 | 凭据对话框：不落盘/不进日志/取消中止 | PASS | `evidence/vcs-git-remote/README.md` §7（敏感串 grep = 0） |
| AC-4.5 | 新增字符串 i18n + 断言只增不减 | PASS | `evidence/vcs-git-remote/README.md` §8 |
| AC-5.1 | 历史操作真实执行 + 冲突态 + 确认预览 | PASS | `evidence/vcs-git-ops/README.md`、`rec-20261104T022330-508c00` |
| AC-5.2 | stash / worktree / submodule | PASS | `evidence/vcs-git-workspace/README.md`、`rec-20261104T032832-b5dc45` |
| AC-5.3 | 不可逆操作双重确认，取消零写入 | PASS | `evidence/vcs-git-ops/README.md` §confirmTwice、`evidence/vcs-git-workspace/README.md` |
| AC-5.4 | 新增字符串 i18n + 断言只增不减 | PASS | 同上两目录的 i18n 节 |
| AC-6.1 | 工具链捆绑 + 干净容器版本 + 许可登记 | PASS | `evidence/vcs-toolchain/README.md`、`evidence/packaging-2/60-selfcheck-appimage.log`、`THIRD_PARTY_NOTICES.md` §10 |
| AC-6.2 | 真实 SVN E2E（svnadmin + checkout + 面板读写） | PASS | `evidence/svn-e2e/README.md`、`rec-20261104T050053-d56d4f` |
| AC-6.3 | SVN 面板对齐 + 能力表置灰 + 截图 | PASS | `evidence/svn-e2e/README.md`、`evidence/xcb-visual-2/README.md` §5 #3（记录为 UX 观察） |
| AC-6.4 | 子进程隔离（loader/ldd + 干净容器） | PASS | `evidence/svn-e2e/README.md` §隔离、`evidence/vcs-toolchain/README.md` §3 |
| AC-6.5 | 捆绑 svn 能力表与实际一致（11 条不支持逐条列理由） | PASS | `evidence/svn-e2e/README.md` §capability |
| AC-6.6 | 新增字符串 i18n + 断言只增不减 | PASS | `evidence/svn-e2e/README.md` §i18n |
| AC-6.7 | worktree E2E 在捆绑 git 上跑通 | PASS | `evidence/vcs-git-workspace/README.md`、`rec-20261104T032832-b5dc45` |
| AC-7.1 | F8 状态栏 i18n + 真实 shell 截图 | PASS | `evidence/i18n-l2/logs/38-t27-status-probe-run.log`（10/0）、`shots-t27/11-status-bar-zh_CN.png`、`rec-20261104T055201-94c638` |
| AC-7.2 | G7 报告 §2 = 319/205·73·23·18；§1.1 补 3 条(43)/13→10；无自相矛盾 | PASS | `evidence/i18n-l2/50-i18n-l2-report.md` §1.1/§2、`logs/54-t51-reconciliation.log`、`rec-20261104T055201-94c638` |
| AC-7.3 | verify_claims fragment 仅在 own-call 判 yes + 负向自测 | PASS | `evidence/i18n-l2/tools/verify_claims_selftest.py`（4/0）、`logs/55-t51-selftest.log`、`03-b-reconciliation.csv`（319 行） |
| AC-7.4 | 打包端到端 exit 0 + AppImage 大小/sha256 | PASS | `evidence/packaging-2/README.md`、`00-run.log`、`50-appimage.log`、`rec-20261104T064001-226f83` |
| AC-7.5 | xcb 视觉评审（PASS + 8 条非阻断观察） | PASS | `evidence/xcb-visual-2/README.md` §5（8 条，均非阻断） |
| AC-7.6 | T26 文档瑕疵（§5.1 路径 / H1 / AC-0.7 说明）+ docs 门禁 | PASS | `docs/ACCEPTANCE-MATRIX.md`（H1、`evidence/final-acceptance/03-baseline-accounting.txt`、AC-0.7 范围说明）；docs 门禁 exit 0，evidence id `ev-20261104T064802-4886da`（T33 定稿后）/ `ev-20261104T070012-79d899`（T53 指针修正后）——两次都是 exit 0；矩阵文件本身不是 docs 门禁的输入（`gates/05-docs.log`） |
| AC-7.7 | 死代码删除 + 3 测试登记 + 覆盖不减 | PASS | `evidence/dead-code/README.md` §2/§5/§9、`rec-20261104T062309-c7a2db` |
| AC-7.8 | AGENT.md 写边界 + §7 证据 + SVN 状态 | PASS | `AGENT.md`（"VCS panel: asynchronous reads AND writes…" 一节、§7 末两条、§9/§10 更新） |
| AC-7.9 | 本矩阵（0 未验）+ 46 旧行不变 + 七门禁 + verdict 齐 + 交付说明 | PASS | 本文件 §3/§4/§7/§8；旧矩阵未改写 |

## 5. 已知限制 / 未验（**不是缺陷，但不软化**）

1. **没有真实网络传输**：远端只覆盖本地裸仓库（`file://`/本地路径）与一个本地 401 认证传输；
   **没有** HTTPS / SSH 端到端。
2. **SVN 能力表**：11 项不支持（逐条给真实机制原因，如 `svn log` 客户端可用但不在本轮接受的读面内）；
   面板对这些项置灰并给原因。
3. **上限未压测**：`MAX_REFS=2000`、每页 200 提交、blame ≤ 20000 行、文本 ≤ 2 MiB —— 有上限断言，
   但没有在超限仓库上做压力测试。
4. **性能未承诺**：非阻塞证据是单机测量（事件循环 tick 计数 + 计时日志），不是 SLA。
5. **8 条视觉观察**（`evidence/xcb-visual-2/README.md` §5）：① VCS 左栏 1500 宽下被省略号截断；
   ② 首次加载 Diff 列为空（选中行像焦点样式）；③ SVN 模式左栏仍出现 Git 词汇
   （Tags/Remote branches/Remotes）；④ SVN header `@0` 与 diff `(revision 1)` 并排易误读；
   ⑤ 加载态只有 `Loading…` 文本；⑥ `--screenshots` 不装 lane 时的占位 dock 不算成品 UI 证据；
   ⑦ Workspace 对话框被 harfness 统一 resize；⑧ 编辑器状态记号 `INSERT`/`Normal`/`Soft Tabs: 4`
   等为技术记号（未翻译 ≠ 遗漏）。其中 **②③ 记录为 UX 后续项，本轮未修**（不改产品代码）。
6. **AC-3.0 跨批结清**：它的判据依赖 6a 的捆绑工具链，因此最终在 6a 的证据上结清（3a-fix 结清读面），
   不是遗漏。

## 6. 过程事故与规则（给下一次的经验）

1. **`translations/vedit_zh_CN.ts` 是共享产物**：本轮**两次**被用 lupdate 默认选项重建为**相对 location**
   形式（批 7a 一次、跨批 14:05 一次），每次都让 `make_reconciliation.py` 变红（47 行退化为 wrap-only、
   17 行变待定）。**规则**：单一 owner；永远 `lupdate -locations absolute -recursive src` →
   `add_t40_messages.py` → `add_t27_messages.py` → `make_reconciliation.py`（期望 319 行 /
   205·73·23·18 / supplement 15 / 待定 0 / exit 0）→ 重建 `.qm`。
2. **契约模板缺陷（5 次）**：`writeScopes` 多次漏掉 `evidence/`（证据无处可写），一次漏掉
   `src/app/main.cpp`（唯一 selftest 源）。建议修 splitter 模板：证据目录与 selftest 源默认进 scope。
3. **并行 lane 的门禁噪声**：`fmt` 曾因另一条 lane 的在途 Rust 文件未格式化而红；
   并发构建出现过一次 `ninja: warning: premature end of file; recovering`。两者都**不是**产品缺陷，
   最终交付构建应单线串行跑。
4. **成员替换 / fenced 成员**由 captain 重新分派处理；handoff 文档在
   `.mpd/cellwork/cell-20261103T123639841Z/handoff/`。

## 7. 如何复验（命令 + 期望）

### 7.1 终态门禁实测（本矩阵定稿时的树，日志在 `evidence/final-acceptance-2/gates/`）

| 门禁 | 命令 | 观测 | 日志 |
|---|---|---|---|
| 构建 + ctest | `bash scripts/build-qt.sh` | **exit 0**；`100% tests passed, 0 tests failed out of 37`；`M0-SMOKE ok=1` | `gates/01-build-qt.log` |
| selfcheck | `QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/sc` | **exit 0**；`EDIT-CORE-SELFTEST ok=1 checks=99 failures=0`、`APP-SHELL-SELFTEST ok=1 checks=290 failures=0` | `gates/02-selfcheck.log` |
| clippy | `cargo clippy --workspace --all-targets -- -D warnings`（`vedit-build:local`） | **exit 0** | `gates/03-clippy.log` |
| fmt | `cargo fmt --all -- --check`（RUSTUP_TOOLCHAIN=stable） | **exit 0**（无输出） | `gates/04-fmt.log` |
| docs | `bash build/gates/docs.sh` | **exit 0**；`[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md` | `gates/05-docs.log` |
| lrelease | `lrelease translations/vedit_zh_CN.ts -qm /tmp/final2.qm` | **exit 0**；`Generated 936 translation(s) (936 finished and 0 unfinished)` | `gates/06-lrelease.log` |
| 对账（i18n 交叉） | `python3 evidence/i18n-l2/tools/make_reconciliation.py` | **exit 0**；`findings=319 rows=319 supplement=15`、`205/73/23/18`、`待定 0` | `gates/07-reconciliation.log` |
| verify_claims 自测 | `python3 evidence/i18n-l2/tools/verify_claims_selftest.py` | **exit 0**；`4 cases, 0 failure(s)` | `gates/08-verify-claims-selftest.log` |
| 真实 gutter 点击 | `ctest -R '^vedit-fold-gutter-click$' -V` | **Passed**；`hidden 0→38→0`、`scroll_max 939→901→939`、`result ok=1` | `gates/09-fold-gutter-click.log` |

`ctest` 是 **37** 而不是 38：批 7b 删死代码时的评审通过口径（§2 第三行，record `rec-20261104T062309-c7a2db`），
不是缺测；`vedit-fold-gutter-click` 仍然在跑并且真实双向。

### 7.2 详细命令（手工复验用）

```bash
# 七道门禁（终态树，单线串行）
bash scripts/build-qt.sh                                  # exit 0；ctest 37/37；M0-SMOKE ok=1
QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/sc   # exit 0；checks 290/0
docker run --rm -e HOME=/vedit-build/target/home -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust -e RUSTUP_TOOLCHAIN=1.99.0 -v "$PWD":/vedit-build -w /vedit-build \
  vedit-build:local cargo clippy --workspace --all-targets -- -D warnings       # exit 0
RUSTUP_TOOLCHAIN=stable cargo fmt --all -- --check                              # exit 0
bash build/gates/docs.sh                                                        # exit 0
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash -lc '/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/v.qm'  # 936 finished / 0 unfinished

# 打包（会重建 AppImage；想沿用交付哈希就只跑下面的校验）
bash packaging/runtime/build.sh                                                 # exit 0 / steps_failed=0
# 干净容器
bash evidence/packaging-2/scripts/clean-container-e2e.sh                         # 干净 CentOS 7.9.2009 容器：AppRun --selfcheck + git/svn 版本
#   日志：evidence/packaging-2/72-clean-container-e2e.log（打包链自身的 7/7 探测也在 00-run.log 里）

# i18n 对账与自证
python3 evidence/i18n-l2/tools/make_reconciliation.py   # 319 行 / 205·73·23·18 / exit 0
python3 evidence/i18n-l2/tools/verify_claims_selftest.py # 4 cases / 0

# 真实点击与探针
ctest --test-dir target/qt6-build -R '^vedit-fold-gutter-click$' -V   # NATIVE-GUI result ok=1
bash evidence/i18n-l2/probe/build-i18n.sh && bash evidence/i18n-l2/probe/run-i18n.sh   # 43 checks / 0
bash evidence/i18n-l2/probe/build-status-i18n.sh && bash evidence/i18n-l2/probe/run-status-i18n.sh  # 10 / 0
```

## 8. 交付说明（本轮改了哪些路径 / 怎么复现）

| 路径 | 动作 | 关注点 |
|---|---|---|
| `AGENT.md` | 改 | VCS 面板一节从「read-only / SVN stub-only」改为「读拒绝写 + 写白名单 + 异步 + 双确认 + 凭据不落盘」；§7 补证据与最终矩阵入口；§9 去掉已删类的消费者；§10 折叠 lane 删除与探针迁移的现状 |
| `docs/ACCEPTANCE-MATRIX.md` | 改 | 补 H1；§5.1 基线路径改为 `evidence/final-acceptance/03-baseline-accounting.txt`；AC-0.7 补分流程行 T27/T28/T31/T36/T37 的范围说明 |
| `docs/USER_GUIDE.md` | 改 | 新增「打开另一个文件夹（File → Open Folder…）」；新增 §10 VCS 面板（随包工具链、能做什么、安全边界、未承诺项） |
| `src/workspace/breadcrumb/{CMakeLists.txt,breadcrumb_model.h,README.md}` | 改（仅注释） | 去掉指向已删 `src/syntax/folding` 的路径 |
| `src/syntax/common/CMakeLists.txt` | 改（仅注释） | 同上 |
| `evidence/final-acceptance-2/AC-MATRIX.md` | 新增 | **本文件**（权威矩阵） |
| `evidence/final-acceptance-2/gates/` | 新增 | §7 七道门禁的终态日志 |
| `docs/FINAL-ACCEPTANCE-MATRIX.md` | 新增 | 本矩阵的**公开镜像**（docs 入口）；`docs/ACCEPTANCE-MATRIX.md` 顶部加了指向它的说明，旧 46-AC 镜像的判定未改写 |
| `evidence/final-matrix/README.md` | 新增 | 指向本文件的说明（captain 口述路径 vs requirements/契约路径的差异登记） |

复现：本文件 §7 的命令按顺序跑即可；所有证据路径都相对于仓库根。
旧证据（`evidence/final-acceptance/`、`evidence/baseline*/`、各批目录）**未被改写**。
