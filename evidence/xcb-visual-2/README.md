# 批 7c 乙 — xcb 视觉评审证据（AC-7.5）

任务 **T31** · cell `cell-20261103T123639841Z` · 冻结契约
`.mpd/cellwork/cell-20261103T123639841Z/contracts/delivery-evidence.md` · 由 requirements rev3 治理
（随包 git 2.53.0 / svn 1.14.5）。**本任务只采证与评审，不改产品代码**；发现的问题在 §5 如实列出，
按边界只记录、不修。

结论：**视觉评审 PASS**（28 张截图、独立断言 28/28、与既有基线逐像素对比无回归；
8 条观察项/缺陷候选见 §5，均非阻断，交由 captain 决定归属）。

---

## 1. 采集环境与命令（全部实测）

宿主：Linux，工作树 `git head=4e2d0a5`；真实 X 服务器只有容器里的 **Xvfb**（宿主没有 Xvfb/xcb 工具），
故所有截图都在 pinned 镜像 `vedit-qt6-build:local` 内用 Xvfb + `QT_QPA_PLATFORM=xcb`（软件渲染）产生。
`--require-xcb` 让平台不符直接 FAIL，绝不静默退化成 offscreen。

| 组 | 命令 | 退出码 | 关键观测 | 日志 |
|---|---|---|---|---|
| panels（4 张） | `bash evidence/xcb-visual-2/run-panels.sh` | **0** | `BATCH1-SHELL-PROBE ok=1 checks=42 failures=0`、`BATCH1B-PROBE ok=1 checks=44 failures=0`；4 张 PNG 均落盘 | `logs/10-probe-build.log`、`logs/11-probe-screenshots.log`、`logs/12-probe-panels.log` |
| vcs（20 张） | `bash evidence/xcb-visual-2/run-vcs-shots.sh` | **0** | `WORKSPACE-VCS-SELFTEST screenshots ok=1 checks=70 failures=0`；`svn=1.14.5`、`git version 2.53.0` | `logs/20-vcs-screenshots.log` |
| product（4 张） | `bash evidence/xcb-visual-2/run-product-shots.sh` | **0** | `APP-SHELL-SCREENSHOTS ok=1 dir=…/shots/product`；4 张 `SCREENSHOT OK` | `logs/30-product-screenshots.log` |
| 独立断言 | `python3 evidence/xcb-visual-2/tools/assert-shots.py` | **0** | `checked=28 failures=0 RESULT PASS`（字节/尺寸/sha256/采样颜色数/空白率） | `logs/40-shot-assertions.txt` |
| 与既有截图对比 | 内联 PIL 脚本（见报告 §4） | 0 | 逐像素 identical share | `logs/50-existing-vs-new.txt` |

Xvfb 显示器：panels `:71`、vcs `:83`、product `:85`，均 `1500x1000x24`。
panels 组的探针是 `evidence/batch1-shell/probe/main.cpp` 的**逐字节副本**（只把构建/输出路径指到本目录，
`probe/build.sh` 从产品自己的 ninja 编译/链接行取 flag，不重编 `src/`），因此它运行的是**真实产品 shell +
四个真实 lane**（Files/Search/VCS/Module Tree），不是替身。

## 2. 截图清单（28 张）

### 2.1 `shots/panels/` — 真实产品 shell，四个 lane 全装（1500x1000 请求尺寸）

| 文件 | 尺寸 | 内容 |
|---|---|---|
| `10-ordinary-start-root-a.png` | 1500x1180 | 普通启动（seeded session，**无 `--project`**），根=`alpha-project`，四栏 + 状态栏 |
| `20-after-switch-root-b.png` | 1500x1198 | 切根后=`beta-project`，四栏全部跟随、旧 token 0 命中 |
| `30-view-menu-panels.png` | 1500x1000 | View 菜单插入/隐藏两个 dock 的入口 |
| `31-panels-restored.png` | 1498x974 | 重启后 dock/面板恢复（3 pane、Outline+Diagnostics 空态提示） |

尺寸说明（`40-shot-assertions.txt` 的 NOTE，非失败）：`10/20` 的高度 1180/1198 受 **shell 自身最小高度**
约束（同批 1b 基线为 1050/1068，本批 VCS History 页签等使最小高度变大）；`31` 为 1498x974（与批 1b 基线完全一致）。

### 2.2 `shots/vcs/` — VCS lane 自测（**恰好 1500x1000**，除模态对话框）

`10-vcs-three-columns`、`11-vcs-staged`、`12-vcs-empty-state`、`13-vcs-loading`、`14-vcs-error-state`、
`15-vcs-commit-dialog`（900x360，模态框按自身尺寸）、`20-vcs-refs-and-remotes`、`21-vcs-remote-action-row`、
`22-vcs-credential-dialog`、`23-vcs-branch-dialog`、`24-vcs-tag-dialog`、`25-vcs-remote-dialog`、
`30-vcs-history-commits`、`31-vcs-history-blame`、`32-vcs-history-empty`、`33-vcs-history-error`、
`40-vcs-conflict-state`、`50-vcs-workspace-dialog`、`60-vcs-svn-panel`、`61-vcs-svn-real`
（文件名统一后缀 `-1500x1000.png`）。其中 `61` 是**真实 SVN 工作副本**（`svnadmin create` + 随包
`svn 1.14.5` checkout/add/commit，再由产品路径读 status/info/diff）；`30` 是 GitHub 风格 40 条提交历史。

### 2.3 `shots/product/` — 交付二进制 `vedit-app --screenshots`（1440x900，产品自定尺寸）

`10-horizontal-split`、`11-vertical-split`、`12-panels-and-docks`、`13-empty-state-no-project`。
注意：该入口**不安装 lane**，dock 里是 shell 占位文案（见 §5 第 6 条），所以“成品面板”的证据以 §2.1/§2.2 为准。

## 3. 独立断言（不依赖采集 harness 的自证）

`logs/40-shot-assertions.txt`：每张 PNG 记录 字节数 / 精确尺寸 / sha256 / 5px 采样网格的 distinct colours /
最常见采样颜色的占比。规则：≥4096 B、≥8 色、非空白（modal ≤ 0.995）、尺寸=1500x1000 或 1440x900
（模态对话框按其自身尺寸；top-level shell 抓取允许“最小尺寸撑高”并记 NOTE）。
**结果：checked=28 failures=0 RESULT PASS**，3 条 NOTE（见 §2.1）。

## 4. 与既有截图（冻结基线）的逐像素对比

`logs/50-existing-vs-new.txt`（同尺寸下 PIL `ImageChops.difference`）：

| 对 | identical share | mean abs diff | 结论 |
|---|---|---|---|
| `evidence/vcs-ui/.../10-vcs-three-columns` | 0.9329 | 2.10 | 仅临时仓库 hash/文件名不同，布局一致 |
| `evidence/vcs-ui/.../11-vcs-staged` | 0.9209 | 3.32 | 同上 |
| `evidence/vcs-ui/.../15-vcs-commit-dialog` | 1.0000 | 0.00 | **逐字节一致** |
| `evidence/svn-e2e/.../30-vcs-history-commits` | 1.0000 | 0.00 | **逐字节一致** |
| `evidence/svn-e2e/.../50-vcs-workspace-dialog` | 0.9969 | 0.23 | 仅临时路径文字 |
| `evidence/svn-e2e/.../60-vcs-svn-panel` | 1.0000 | 0.00 | **逐字节一致** |
| `evidence/svn-e2e/.../61-vcs-svn-real` | 1.0000 | 0.00 | **逐字节一致** |

即：当前树的 VCS/SVN UI 与批 6b/批 4a 的冻结基线相比**没有视觉回归**（差异只来自测试用临时仓库）。

**既有全屏截图的人工复核**（不只看像素差）：`evidence/batch1-shell/shots/10-ordinary-start-root-a.png`
（批 1b 基线，1500x1050）与本次 `panels/10`（1500x1180）逐项目视对比——四栏布局、Project/Files/Search/
Module Tree 的内容与位置完全同构；差异只有**批 6b 的 VCS 面板升级**（新增 `Working tree | History` 页签、
三栏 Branches/Changes/Diff、动作按钮两行与能力行），这也解释了 shell 最小高度 1050→1180。
旧图里根本没有 Diff 列，所以 §5 第 2 条的“Diff 空”不是本批引入的回归，而是“面板扩成三栏后首次进入
不自动选中第一行”的新可见状态——仍建议评审定级。另复核了既有 zh_CN 截图
`evidence/i18n-l2/shots-t27/10-shell-status-zh_CN.png`（1500x950）：中文渲染完整、无乱码/截断。

## 5. 视觉评审（人工看片，逐张）

### 5.1 看起来正确的地方

- **恢复的项目根确实到达四栏**（`panels/10`）：Project 面板 `Project: alpha-project — 2 files (live)`；
  Files 列出 `rtl/second.v`、`rtl/top.sv` 且 `Opened 'alpha-project'`；Search 根标签
  `In /tmp/…/alpha-project`、`1 hits in 1 of 2 files`；Source Control header
  `Git alpha-project @ 38307e1 · 1 changed file(s)`；Module Tree 显示 `alpha_second / alpha_top /
  alpha_top.u_child` 且 footer `root: …/alpha-project`；状态栏 `Project: alpha-project`。
- **切根后四栏一致跟随**（`panels/20`）：beta-project / 1 file、0 hits（旧 token 不再命中）、
  `Git beta-project @ 647cc06`、`beta_top / beta_top.u_child`、`Project: beta-project`。
- **VCS 三栏在 1500x1000 下完整可用**（`vcs/10`、`vcs/40`、`vcs/60`、`vcs/61`）：Branches/Changes/Diff
  三栏 + Staged 分区；diff 是真实的 `git diff` / `svn diff` 文本；冲突态有黄色横幅
  “Merge in progress: 1 conflicted file(s)…” 且 Continue/Abort 转可用；SVN 视图 footer 明确写出
  “Subversion: add, commit, revert, remove and update are available. Staging, fetches, pushes, branches,
  tags, remotes, stashes, worktrees and conflict continuation are not — hover a disabled action for its reason.”，
  按钮禁用与能力表一致（Unstage/Fetch/Push/Branch/Tag/Remote/Workspace/Continue/Abort 灰）。
- **History 页签**（`vcs/30`、`vcs/31`）：40 条提交（Commit/Date/Author/Subject）+ 右侧
  Commit/File history/Blame/Compare 四页签；提交详情含 hash/author/date/subject/Files/+1 -1 与 diff；
  Blame 显示 行号/commit/作者/文本。全屏 shell 里也可见 `Working tree | History` 页签（`panels/10`）。
- **Workspace 对话框**（`vcs/50`）：Stashes 列表 + stash message + Save/Pop/Apply/Drop；Worktrees 列出
  主工作树与 linked worktree（[master] (main) / [feature/ui]）、Worktree path/Branch 输入、
  Add/Remove worktree；Submodules 两个按钮；底部 `1 stash(es), 2 worktree(s)`。
- **没有错位/重叠/黑块**；中英文渲染正常；按钮禁用态与 footer 的“理由行”自洽。

### 5.2 问题清单（按严重度；**均未在本次修复**，属产品/UI lane 的写域）

| # | 级别 | 位置（截图） | 现象 | 备注/证据 |
|---|---|---|---|---|
| 1 | Minor（UX，截断） | `panels/10`、`panels/20`（1500 宽、四栏同时打开） | VCS 左栏窄（约 100px），分支名与空态被省略号截断：`alpha-proj…`、`No remote…`、`No remote…`；Module Tree 工具条按钮 `Filter instance / n…` | 非阻断；该列可设最小宽度或缩短标签。截图为证 |
| 2 | Minor（UX，待定级） | `panels/10`、`panels/20` | 变更列表有一行呈选中高亮，但 **Diff 列是空的**；独立 VCS 面板截图（`vcs/10` 等，显式 `selectFile()`）有 diff | `src/workspace/vcs/vcs_panel.cpp::renderChanges()` 首次渲染 `keep` 为空时不会 `setCurrentRow`，diff 仅在用户点行后请求；截图里的高亮更像焦点/current 样式。**建议评审确认**是否要首次加载自动选中第一行；本任务不改代码 |
| 3 | Minor（UX/信息架构） | `vcs/60`、`vcs/61`（SVN） | SVN 模式左栏仍出现 Git 词汇：`Tags`→`No tags`、`Remote branches`→`No remote branches`、`Remotes`→`No remotes configured`；而能力表明确 SVN 无 remote 概念 | 与 batch 6b 的“能力表与 UI 一致”目标有轻微张力；非阻断 |
| 4 | Minor（UX/表述） | `vcs/61` | header `Subversion repo @ 0` 与 diff 内 `(revision 1)` 并排，读起来矛盾 | 忠实于 svn：`@0` 是 WC 根 revision，`revision 1` 是文件 base revision（fixture 先 checkout r0 再 commit）。可考虑 header 改用 max item revision |
| 5 | Info | `vcs/13`（加载态） | 只有纯文本 `Loading…`，无转圈/进度条；全部按钮禁用（正确） | 稀疏状态，非缺陷；仅记录观感 |
| 6 | Info（证据边界） | `product/12`、`product/13` | `vedit-app --screenshots` 不装 lane，dock 显示 shell 占位文案（“This dock is the shell's placeholder…”）；尺寸 1440x900 为产品自定 | 成品面板视觉证据以 `panels/`、`vcs/` 为准；此处不当作“成品 UI”证据 |
| 7 | Info（harness 产物） | `vcs/50` | Workspace 对话框被统一 resize 到 1500x1000，底部大片灰白 | 真实产品里它是模态框；非布局缺陷 |
| 8 | Info（i18n） | 既有 `evidence/i18n-l2/shots-t27/10-shell-status-zh_CN.png` | 中文完整；编辑器状态记号仍英文（`INSERT`、`Normal`、`Soft Tabs: 4`、`UTF-8`） | lrelease 门禁 0 unfinished（见 `evidence/packaging-2/gates/07-lrelease.log`），故不是未翻译；属技术记号 |

未发现：错位/重叠、文字溢出容器、黑块/未绘制区域、禁用按钮与理由不符、CJK 缺字（随包 CJK 字体）。

## 6. 文件索引

```
evidence/xcb-visual-2/
├── README.md                  # 本文件（评审结论 + 问题清单）
├── run-panels.sh              # panels 组入口（宿主）
├── run-vcs-shots.sh           # vcs 组入口（宿主）
├── run-product-shots.sh       # product 组入口（宿主）
├── probe/                     # batch1-shell 探针副本 + 构建/运行脚本（容器内）
│   ├── main.cpp  build.sh  container-run.sh
├── tools/assert-shots.py      # 独立断言（PIL）
├── shots/panels/*.png (4)     # 真实 shell + 四 lane
├── shots/vcs/*.png (20)       # VCS 3 栏 / History / Workspace / SVN
├── shots/product/*.png (4)    # 交付二进制 --screenshots
└── logs/
    ├── 10-probe-build.log  11-probe-screenshots.log  12-probe-panels.log
    ├── 20-vcs-screenshots.log  30-product-screenshots.log
    ├── 40-shot-assertions.txt  50-existing-vs-new.txt
    └── run-*-console.log     # 三组入口的完整控制台输出
```
