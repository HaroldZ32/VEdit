# 批 3b · 三栏 UI 基线 + 自动刷新 + i18n 证据（AC-3.4 / AC-3.5 / AC-3.7）

任务：`vcs-ui-git-a`（board **T13**，cell `cell-20261103T123639841Z`）。
契约：`.mpd/cellwork/cell-20261103T123639841Z/contracts/vcs-ui-git-a.md`。
写者：Senior Engineer（成员 `Senior Engineer 2`）。写范围：`src/workspace/vcs/`、`translations/vedit_zh_CN.ts`。
Git 写操作的证据在 `evidence/vcs-write/batch3b/`。

本文件只记录**命令、环境、退出码与关键观测**；实现细节在代码里。

## 0. 一屏结论

| AC | 观测 |
|---|---|
| AC-3.4 三栏 + 操作条 | `vcs-column-refs` / `vcs-column-changes` / `vcs-column-diff` 三列 + `vcs-actions` 操作条（Refresh / Stage / Unstage / Discard… / Commit…）由 lane 断言；1500x1000 **xcb** 截图 6 张（`screenshots/`，`--require-xcb`） |
| AC-3.4 四态 | empty / loading / error / ready 四态各有独立断言 + 独立截图；文案全部走 `QCoreApplication::translate("VEdit", …)` |
| AC-3.4 关闭重开不崩 | 3 轮 show/refresh/close/destroy + 1 轮「状态 job 在飞时销毁」均无崩溃 |
| AC-3.5 自动刷新 | 保存文件（QFileSystemWatcher，文件+目录）、VCS 操作完成、窗口/应用激活三路触发；探针记录刷新次数与前后状态（diff 文本 before/after、文件计数） |
| AC-3.5 防刷新风暴 | 尾沿合并窗口 1000 ms：探针 5 次触发 → **1** 次刷新（`merged=4`） |
| AC-3.7 i18n | 新增 55 条消息（47 条面板 + 8 条后端/变更类型名），`lrelease` **662 finished / 0 unfinished**；运行时用编译后的 `vedit_zh_CN.qm` 断言实际译出（暂存 / 提交… / 丢弃改动？ / 加载中…） |
| 七道门禁 | 全绿（§5）：build-qt exit 0、ctest 38/38、selfcheck 290/0、clippy 0、fmt 0、docs 0、lrelease 0 unfinished |
| 断言只增不减 | lane selftest **58 → 144 checks / 0 failures**；`--selfcheck` 290/0 不变 |

## 1. 三栏布局（AC-3.4）

左→右：`vcs-column-refs`（Branches and tags）／`vcs-column-changes`（Changes 上、Staged 下）／
`vcs-column-diff`；列宽比例 1:2:3，列不可折叠；底部操作条 `vcs-actions`。

- 行文本：`<path>  <kind>`（kind 走 i18n：新增/修改/删除/重命名/冲突/未知）。
- 操作条按钮 objectName：`vcs-action-refresh|stage|unstage|discard|commit`；
  Stage/Unstage/Discard 需要 Git 后端 + 选中行 + 无写操作在飞；Commit 需要 Git 后端 + 无写操作在飞。
- 选中行同步右侧 diff；刷新后按路径恢复选中，diff 重新拉取（前后状态因此可观测）。

### 四态（各自有截图与断言）

| 态 | 判据 | 截图 |
|---|---|---|
| empty | `Status.backend == None && !error` → 空态文案可见、三列隐藏 | `12-vcs-empty-state-1500x1000.png` |
| loading | 状态 job 在飞（`loading_` 可见），表头清空（未知工作副本时不谎称 "No repository"） | `13-vcs-loading-1500x1000.png`（PATH 前置 sleep 1.5s 的假 `git`，真实 loading 窗口） |
| error | `Status.error == true` → 红色错误行带真实原因、空态隐藏 | `14-vcs-error-state-1500x1000.png` |
| ready | 后端 Git/SVN → 三列可见 + 表头 `Git <branch> @ <rev> · N changed file(s)` | `10-vcs-three-columns-1500x1000.png`、`11-vcs-staged-1500x1000.png` |

提交对话框独立截图：`15-vcs-commit-dialog-1500x1000.png`（message / author / amend 三项，按钮文案 i18n）。

## 2. 自动刷新（AC-3.5）

三路触发，全部汇入同一个尾沿合并器：

1. **保存文件**：`QFileSystemWatcher` 监视工作树根 + 变更文件本身 + 变更文件父目录（上限 200 个变更项，
   避免耗尽 inotify）；`fileChanged` 触发 `noteFileSaved()`，并在文件被 rename 式保存（QSaveFile）后重新挂监视。
2. **VCS 操作完成**：写 job 成功/失败后 `scheduleRefresh("write"|"write-failed")`（失败也重读，不假设半应用状态）。
3. **窗口/应用激活**：面板对顶层窗口装 `WindowActivate` 事件过滤器 + `QGuiApplication::applicationStateChanged`。

合并规则：`scheduleRefresh()` 只在空闲时启动一个 1000 ms 单次定时器；窗口内的后续触发只累加
`mergedRefreshCount`，到点执行**一次** `refresh()`。Refresh 按钮是用户显式请求，直接刷新并记
`lastRefreshReason()=manual`。

探针记录（`logs/10-selftest.log` 的 `vcs.refresh.*`）：

```
vcs.refresh.initial           PASS backend=1 files=1
vcs.refresh.save-detected     PASS refreshes 1 -> 2 (file-saved)
vcs.refresh.save-state-after  PASS diff 文本由 change-1 变为 change-2
vcs.refresh.new-file-detected PASS files=1 -> 2
vcs.refresh.activation-detected PASS refreshes -> +1 (activated)
vcs.refresh.write-detected    PASS refreshes -> +1 (write)
vcs.refresh.coalesce-requests PASS requests=5
vcs.refresh.coalesce-merged   PASS merged=4
vcs.refresh.coalesce-no-storm PASS 5 triggers -> 1 refresh(es) in 1000 ms
```

## 3. i18n（AC-3.7）

- 新增用户可见字符串全部经 `QCoreApplication::translate("VEdit", …)`（面板 47 条 + 后端名/变更类型名 8 条）。
- `translations/vedit_zh_CN.ts`：607 → **662** 条消息，`type="unfinished"` = **0**。
- 提取来源：`lupdate`（`/usr/lib/qt6/bin/lupdate`，Qt 6.10.2）覆盖 `src/workspace/vcs/*.cpp` 得到的
  54 条中 47 条为新增；8 条运行时拼装名（Git/Subversion/added/…）手工补入（`backendName()` /
  `changeKindName()` 是运行期查表，lupdate 抽取不到，条目按同一 VEdit 上下文登记）。
- 运行时断言（比 lrelease 更强）：lane selftest 载入**构建产物** `vedit_zh_CN.qm`，断言
  `Stage→暂存`、`Commit…→提交…`、`Discard changes?→丢弃改动？`、`Loading…→加载中…` 真的译出（`vcs.i18n.*`）。
- 门禁 `lrelease`：`Generated 662 translation(s) (662 finished and 0 unfinished)`（`logs/gate-lrelease.log`）。

## 4. 边界（诚实登记）

1. **分支/标签列**：批 3a 的读 ABI（`VdVcsInfo`）只报当前分支，**没有标签列表**。左列因此展示
   当前分支 + 标签区的显式空态「此后端未报告标签」（i18n），不假装仓库没有标签。
   全量 ref 列举与分支/标签操作属批 4a（契约 `git-remote-refs`，board T15），其写范围是
   `crates/vd-vcs` + `crates/vd-ffi`，不在本任务写范围内。
2. **暂存列**：读 ABI 的 `Status.changes` 只有 `path + kind`，不区分 index/worktree 两侧
   （`from_git_pair` 把 `M `/` M`/`MM` 都映射为 Modified）。面板因此维护一个**会话内暂存覆盖**：
   Stage 把行移入 Staged、Unstage/Discard/Commit 清除，行**立即**移动，随后由合并刷新回读后端事实。
   在面板外（终端里 `git add`）产生的暂存状态不会显示在 Staged 列——真正的两侧拆分需要 status ABI
   增加 index 侧字段，属批 4 的 crates 写范围。
3. **diff 列**：显示工作树 diff（`git diff -- <path>`）。只有暂存侧改动时工作树 diff 为空，
   与读 ABI 暴露的数据一致。
4. **E2E 只在临时 fixture**：lane selftest 的仓库全部由 `QTemporaryDir`/`mktemp` 在 `/tmp` 下创建，
   并断言自身 cwd 不是工作树（`vcs.write.e2e.not-worktree`、脚本开头的 cwd 检查）。
5. **无界面确认**：`QT_QPA_PLATFORM=offscreen` 且未注入确认回调时，Discard/Amend **拒绝**（不静默毁数据），
   并在面板提示「无界面环境无法确认，未写入任何内容。」

## 5. 七道门禁（`logs/gate-summary.log`）

| 门 | 命令 | 观测 |
|---|---|---|
| 1 build-qt | `bash scripts/build-qt.sh` | exit 0；`[qt6] PASS`（Rust 层 + configure + build + ctest + xcb/软件渲染 smoke） |
| 2 ctest | 同上（ctest 段） | `100% tests passed, 0 tests failed out of 38` |
| 3 selfcheck | `vedit-app --selfcheck`（image, offscreen） | `APP-SHELL-SELFTEST ok=1 checks=290 failures=0` |
| 4 clippy | `vedit-build:local` 内 `cargo clippy --workspace --all-targets -- -D warnings` | exit 0（宿主缺 GTK3 开发文件，按 `scripts/gates.sh` 的同一回退在钉住镜像内跑） |
| 5 fmt | `cargo fmt --all -- --check` | exit 0 |
| 6 docs | `bash build/gates/docs.sh` | `[docs] PASS` |
| 7 lrelease | `/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts` | `662 finished and 0 unfinished` |

宿主机直接 `cargo clippy --workspace` 会在 `gdk-pixbuf-sys` / `cairo-sys-rs` 的 `pkg-config` 处失败——
这是**宿主缺 GTK3 开发文件**的环境限制（`scripts/gates.sh` 对此有明确回退），不是产品缺陷；
镜像内全工作区 clippy 通过。

## 6. 复现

```bash
bash scripts/build-qt.sh                      # 构建 + ctest + smoke（含本 lane）
bash evidence/vcs-ui/run-selftest.sh          # lane 自测 144 checks（offscreen）
bash evidence/vcs-ui/screenshots.sh           # 1500x1000 xcb 截图 6 张
bash evidence/vcs-write/batch3b/run-write-e2e.sh   # AC-3.3 真仓库写操作
bash evidence/vcs-ui/run-gates.sh --no-build-qt    # 其余六道门禁
```

日志：`logs/10-selftest.log`、`logs/20-screenshots.log`、`logs/gate-*.log`、`logs/gate-summary.log`。
