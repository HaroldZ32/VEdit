# 批 6b · SVN 后端读写 E2E + 面板对齐 Git（AC-6.2 / 6.3 / 6.4 / 6.5 / 6.6 / 6.7）

任务：`svn-backend-panel`（board **T25**，cell `cell-20261103T123639841Z`）。
冻结契约：`.mpd/cellwork/cell-20261103T123639841Z/contracts/svn-backend-panel.md`（split rev1）。
**由 requirements rev3 治理**（captain 书面裁定）：随包 **subversion 1.14.5** + **git 2.53.0**；
`svn info --show-item` 允许；svnadmin 仅 build-only（`build/vcs-tools/`，不随包）。

## 0. 一屏结论

| 判据 | 观测（证据行号/文件） |
|---|---|
| 真实 SVN 读（status/branch/revision/changed files/diff） | 经产品路径 `vd_vcs::status` / `ProcessVcsService::info` / `file_diff`，捆绑 svn 1.14.5；`logs/30-rust-svn-e2e.log`、`logs/10-selftest.log` 的 `vcs.svn.e2e.*` |
| 真实 SVN 写（add/commit/revert/update/remove） | 经 `vd_vcs::write` + 面板 `VcsPanel` 请求路径；`vcs.svn.e2e.add-ok / commit-ok / revert-ok / update-ok / remove-ok`，`vcs.svn.write.*` |
| `.svn` 工程不再报 cannot run svn | `vcs.svn.e2e.no-cannot-run` PASS；工具链 `vcs.toolchain.svn-version PASS …/bin/svn -> 1.14.5` |
| 面板对齐 Git（同一三栏 UI） | `vcs.svn.panel.three-columns`、`vcs.svn.panel.add-label=Revert`、`vcs.svn.panel.update-label=Update`、`vcs.svn.panel.staged-column-hidden`；截图 `screenshots/60/61-*` |
| 能力表置灰 + 真实理由 | `vcs.capability.svn.*`（11 项 unsupported + 每项 reason）、`vcs.svn.panel.*-reason`；§2 表 |
| 异步 job（不在 GUI 线程跑 svn） | 复用 `FfiVcsSource` 的 job+poll；E2E 由 `waitUntil` 驱动，面板无同步 subprocess |
| 破坏性操作双重确认 | `vcs.svn.write.revert-double-confirmed` / `remove-double-confirmed`（首步取消 = 零写入） |
| 子进程在 AppDir loader/env 下可运行 | `logs/20-toolchain-capabilities.log`（readelf/ldd/loader --list）+ `logs/40-clean-container-cycle.log`（CentOS 7.9 glibc 2.17 容器内全流程） |
| 凭据/密码不进日志 | §6 的扫描：0 命中 |
| worktree E2E on 捆绑 git 2.53（AC-6.7） | `evidence/vcs-git-workspace/logs/45-worktree-e2e-bundled.log` |
| 七道门禁 | §7：全绿（lrelease 917/0） |

## 1. 捆绑工具与本次运行环境（实测）

```
$ packaging/runtime/out/AppDir/bin/svn --version --quiet        -> 1.14.5
$ build/vcs-tools/svnadmin --version --quiet                    -> 1.14.5
$ packaging/runtime/out/AppDir/bin/git --version                -> git version 2.53.0
```

- **宿主与三个镜像都没有 `svn`**：真实 E2E 用 AppDir 的 `bin/svn`（loader 包装脚本）与
  build-only 的 `build/vcs-tools/svnadmin`；Rust 测试通过 `VEDIT_SVN_BIN` / `VEDIT_SVNADMIN`
  环境变量启用（未设置时 SKIP，`cargo test` 门禁保持绿），产品进程通过
  `toolchain.rs` 的 `VEDIT_SVN`（bundled > PATH > env 的 env 末位）解析到**同一个**客户端。
- lane 的 `VCS-TOOLCHAIN` 行实测：
  `VCS-TOOLCHAIN git=/usr/bin/git version=2.53.0 svn=/vedit-build/packaging/runtime/out/AppDir/bin/svn version=1.14.5 source=mixed git_source=path svn_source=env`
  （`vcs.toolchain.svn-version PASS`）。

## 2. Subversion 1.14.5 能力表（AC-6.5，与面板能力表逐项一致）

`tools/probe-toolchain.sh` 用 `svn help <sub>` 逐项实测（exit 0 = 该客户端有）：
**status / info / diff / commit / add / remove / revert / update / log / copy / merge 全部 exit 0**；
`svn diff --internal-diff`、`--non-interactive`、`--no-auth-cache` 均存在。

| 能力 | 捆绑 svn 1.14.5 | 面板（本批） | 面板给用户的理由 |
|---|---|---|---|
| status | 有 | ✅ 读 | — |
| info（revision / URL→branch） | 有（`--xml`；`--show-item` 也可用） | ✅ 读 | — |
| diff（单文件） | 有（`--internal-diff`） | ✅ 读 | — |
| add | 有 | ✅ 写（按钮 **Add**） | — |
| remove | 有 | ✅ 写（按钮 **Remove…**，双重确认） | — |
| revert | 有 | ✅ 写（按钮 **Revert…**，双重确认） | — |
| update | 有 | ✅ 写（按钮 **Update**） | — |
| commit | 有 | ✅ 写（按钮 **Commit…**） | — |
| staging area | **SVN 没有** | ❌ 置灰（Unstage） | "Subversion has no staging area…"（真实机制） |
| fetch / pull / push | SVN 无对应概念 | ❌ 置灰 | "no remote-tracking branches to fetch" / "updates in place; use Update" / "no push: commit publishes directly" |
| branch / tag 创建 | `svn copy` 可用 | ❌ 置灰 | 仓库端拷贝，不在本构建接受的 SVN 功能面；理由明说 |
| remote add/rm | SVN 工作副本绑定单一 URL | ❌ 置灰 | "bound to one URL; there is no remote set…" |
| stash / worktree | SVN 无此概念 | ❌ 置灰 | "Git concepts; a Subversion working copy is a single directory." |
| merge/rebase continue / abort | SVN 无多步操作 | ❌ 置灰 | "no multi-step merge or rebase to continue/abort; revert restores…" |
| history（`svn log`） | **客户端有**（`svn log --xml` exit 0） | ❌ 置灰 | "Subversion's log (available in svn 1.14.5) is outside this build's accepted read set." |

**能力表与实现一致性的机器可读形式**：`capabilityReport(Backend::Subversion)`（面板
`capabilityReportText()`）输出
`svn refresh=yes add=yes unstage=no(…) revert=yes commit=yes remove=yes fetch=no(…) pull=no(…) update=yes push=no(…) branch=no(…) tag=no(…) remote=no(…) workspace=no(…) continue=no(…) abort=no(…) history=no(…)`，
lane 检查 `vcs.capability.svn.report` 断言之；`disabledActionReasons()` 给出 11 条 `action: reason`。

**没有静默降级**：`svn log` 在 1.14.5 可用但面板不提供（历史视图是 Git 的），这一点写在
能力表里、写在按钮的 tooltip 与面板能力行里，并作为"tool 有 / 面板不提供"的显式条目记录。

## 3. 真实 SVN 读写 E2E（AC-6.2）

### 3.1 构建镜像内的完整周期（命令 + 输出）

`tools/clean-container-cycle.sh` → `logs/40-clean-container-cycle.log`：在
`vedit-build:local`（CentOS 7.9.2009，glibc 2.17）容器里用 AppDir 的 svn + build-only
svnadmin 跑完：`svnadmin create` → `checkout` → `add` → `commit`（r1）→ `status`（M）→
`info --xml` → `diff --internal-diff` → `revert` → 第二个工作副本 `add`+`commit`（r2）→
`update` → `remove` → `commit`（r3）→ `log -q`。每一步都打印命令与 exit code，`result=PASS`。

### 3.2 经产品代码路径（Rust，env-gated）

`crates/vd-vcs/tests/svn_e2e.rs` → `logs/30-rust-svn-e2e.log`（`cargo test -p vd-vcs --test svn_e2e`）：
`svnadmin create` + `svn checkout` 造 fixture，然后**只通过产品 API**做：

| 步骤 | 产品调用 | 断言（用真实 svn 命令复核） |
|---|---|---|
| 检测 | `vd_vcs::detect` | backend=Subversion，root 是含 `.svn` 的目录 |
| add | `write::write(SvnAdd)` | `svn status` 显示 `A a.sv` |
| commit | `write::write(SvnCommit)` | `svn log --xml <url>` 恰好 1 个 `<logentry>`（r1） |
| 读 status | `vd_vcs::status` | 返回**工作副本相对路径** `a.sv`（与 Git 面板同形状；见 §5 修复项） |
| 读 info | `ProcessVcsService::info` | revision=0（WC 根基准版本，见 §5）、branch=repo（URL 末段） |
| 读 diff | `ProcessVcsService::file_diff` | 含 `// changed` |
| revert | `write::write(SvnRevert)` | 文件内容恢复为提交版；`svn status` 干净 |
| 第二工作副本提交 | fixture 的 `svn` 命令 | r2 |
| update | `write::write(SvnUpdate)` | `b.sv` 出现；info revision=2 |
| remove | `write::write(SvnRemove)` | `svn status` 显示 `D b.sv`；commit 后 r3 |
| 失败原因 | `write::write(SvnRemove)` on 缺失路径 | `success=false` + svn 真实 stderr（不是参数拒绝） |

### 3.3 经面板请求路径（C++ lane，真实 svn，同一三栏）

`evidence/svn-e2e/run-selftest.sh` → `logs/10-selftest.log`（`ok=1 checks=673 failures=0`（558 基线 +115，只增不减））：
`vcs.svn.e2e.*` 用 `FfiVcsSource`（异步 job）驱动真实工作副本：backend/header/changed-row/
add-ok/commit-ok（r1）/diff（含 `// svn change`）/revert-ok/revert-restored/update-ok/
update-file/update-row（header `@ 2`）/remove-ok/remove-committed（r3）/no-cannot-run。
面板**没有**在 GUI 线程跑任何 subprocess：所有读写走 `request*` + QTimer poll。

## 4. 面板对齐 Git（AC-6.3）与截图

同一个 `VcsPanel`、同一个三栏布局、同一个 action bar、同一套 `issueWrite`/`confirmTwice`：

| 槽位 | Git | Subversion |
|---|---|---|
| Stage 按钮 | Stage（`git add`） | **Add**（`svn add`） |
| Unstage 按钮 | Unstage | 置灰 + 真实理由（无暂存区） |
| Discard 按钮 | Discard…（`git checkout --`，双重确认） | **Revert…**（`svn revert`，双重确认） |
| Commit 按钮 | Commit…（需消息） | Commit…（需消息；amend 被拒绝并说明） |
| Pull 按钮 | Pull（`git pull --ff-only`） | **Update**（`svn update`） |
| 新增 Remove 按钮 | 置灰 + 真实理由（Git 靠暂存删除记录删除） | **Remove…**（`svn remove`，双重确认） |
| Fetch/Push/Branch/Tag/Remote/Workspace/Continue/Abort | 可用 | 置灰 + 各自真实理由 |
| Staged 列 | 可见 | 隐藏（无索引），能力行说明 |
| 能力行 | `Git: staging, commits, …` | `Subversion: add, commit, revert, remove and update are available. …` |

xcb 截图（1500×1000，Xvfb :83，`--require-xcb`，`logs/20-screenshots.log` 70 checks / 0 failures）：

| 文件 | 内容 | 大小 |
|---|---|---|
| `screenshots/60-vcs-svn-panel-1500x1000.png` | SVN 后端（stub 数据）同一三栏 + 能力行 + 置灰按钮 | 60622 B |
| `screenshots/61-vcs-svn-real-1500x1000.png` | **真实** svn 工作副本：header `Subversion repo @ 0 · 1 changed file(s)`、Changes `rtl/a.sv  modified`、真实 diff（`Index: rtl/a.sv`）、能力行、Add/Revert…/Commit…/Remove… 可用、Unstage/Fetch/Push/… 置灰 | 62266 B |

`60` 的能力行直接列出"哪些可用 / 哪些不可用 + hover 看原因"；`61` 的同一行来自真实后端。

## 5. 本批在真实 SVN 上发现并修复的两个问题（如实记录）

1. **changed-file 路径变成绝对路径**（修复：`crates/vd-vcs/src/lib.rs`）。原先 `svn status <绝对
   working_tree>` 让 svn 原样回显绝对路径（实测 `M /tmp/.../wc/a.sv`），面板的 Changes 列因此显示
   绝对路径、与 Git 面板不一致。改为不带路径参数的 `svn status`（cwd=WC 根），svn 输出**相对路径**
   （`M       a.sv`）。E2E 断言 `a.sv` 而不是绝对路径。
2. **网络能力操作的交互与凭据缓存**（修复：`crates/vd-vcs/src/write.rs`）。`svn commit` /
   `svn update` 现在带 `--non-interactive --no-auth-cache`：不会等待终端提示、不会把凭据缓存到
   磁盘（与 Git 侧 `GIT_TERMINAL_PROMPT=0` 的既有策略一致）。精确 argv 断言在
   `crates/vd-vcs/tests/write.rs::accepted_operations_build_exactly_one_documented_invocation`。

**SVN 的 revision 语义（记录，不是缺陷）**：`svn info` 报的是**该路径的基准版本**；一个
"只提交了文件、没 update 过"的工作副本根仍是 r0（混合版本工作副本），`svn update` 后变为 r2。
产品如实转述 `svn info` 的答案；E2E 对 r0 → r2 的迁移都有断言（`vcs.svn.e2e.header`/`update-row`）。
捆绑内没有 `svnversion`，因此没有用"最大值"改写这一语义。

## 6. 子进程隔离与凭据（AC-6.4）

- **AppDir loader/env**（`logs/20-toolchain-capabilities.log`）：
  `file libexec/svn` → ELF（interpreter `/lib64/ld-linux-x86-64.so.2`），包装脚本
  `exec <appdir>/ld-linux-x86-64.so.2 --library-path <appdir>/usr/lib/vcs/lib:<appdir>/lib …`；
  `readelf -d libexec/svn` 的 NEEDED（libsvn_client/wc/ra/delta/diff/subr、libapr、libc）；
  `<loader> --library-path … --list libexec/svn` 显示 **libsvn*/libapr 全部解析到
  `<appdir>/usr/lib/vcs/lib`**，glibc 走系统；`SONAME=ld-linux-x86-64.so.2`。
- **干净容器实测**：`logs/40-clean-container-cycle.log`（CentOS 7.9.2009 / glibc 2.17）跑完整
  读写周期；产品 runner 在 spawn 前 `env_remove("LD_LIBRARY_PATH")`（T23 起），确保包装脚本的
  `/bin/sh` 用宿主 loader 启动、再由脚本切到 AppDir loader，不与产品 bundle 的库冲突。
- **凭据/密码**：SVN 路径**从不**接受或传递凭据（无 `--username/--password`；commit/update 带
  `--non-interactive --no-auth-cache`）。扫描（§6 末）对 `--password`、`password=`、
  `Authorization: Basic`、URL userinfo（`scheme://user:pass@`）在 `evidence/svn-e2e/` 与
  本批日志中 **0 命中**。

## 7. 七道门禁与复跑

`bash evidence/svn-e2e/run-all.sh` 依次执行：工具链/能力探针 → 干净容器周期 →
Rust svn E2E → Rust worktree E2E（捆绑 git，日志落 `evidence/vcs-git-workspace/logs/`）→
lane selftest → xcb 截图 → 七道门禁 → 凭据扫描。

```bash
bash evidence/svn-e2e/run-all.sh            # 全量（会重建 Qt6 产物）
bash evidence/svn-e2e/run-all.sh --no-build # 复用现有构建
```

日志：`logs/gate-build-qt.log`、`gate-ctest.log`、`gate-selfcheck.log`、`gate-clippy.log`、
`gate-fmt.log`、`gate-docs.log`、`gate-lrelease.log`、`gate-summary.log`、`gate-ac62-evidence.log`。

## 8. 边界（如实声明）

- **真实网络 SVN（https/ssh）未自动化**：本批 E2E 全部是 `file://` 临时仓库（/tmp
  fixture，`svnadmin` 造库）；需求 open question 已把真实网络认证列为人工验证。
- **history 视图对 SVN 不提供**：`svn log` 在 1.14.5 存在，但历史面板是 Git 实现；按
  "能力表置灰 + 真实理由"处理并记录（§2 最后一行）。
- **merge/conflict 的 SVN 流程**（`svn resolve --accept` 等）不在 AC-6.2 的读写集内，
  面板对 SVN 不提供 merge/continue/abort（理由见能力表）。
- **refs 列对 SVN 的含义**：SVN 没有本地分支 ref，三栏左侧对 SVN 显示的是 `svn info` 的
  URL 末段（工作副本绑定位置，如 `repo`），Tags/Remote branches/Remotes 均为空（"No tags" /
  "No remote branches" / "No remotes configured"）。这不是伪造的分支列表：Branch/Tag/Remote
  创建在能力表中明确"不提供 + 真实理由"。
- **svnadmin 不随包**：只在 `build/vcs-tools/`（1.14.5）供测试造库；AppDir 内断言缺席。
- 旧契约的 "subversion 1.7.14" 口径已被 rev3 取代；本批所有实测都基于**捆绑 1.14.5**。
