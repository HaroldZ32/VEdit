# T37 复现台：T46 修复后的 module-tree §C/D 重跑（prep-t37 / 看板 T9）

Cell `cell-20261102T100600176Z` · 任务 **T9 / prep-t37** · 成员 **se-shell**（Senior Engineer）
契约：`.mpd/cellwork/cell-20261102T100600176Z/contracts/prep-t37.md`
本文件只为 **T37 完整性复核** 准备【当前树】证据，不落 verdict、不改产品代码/测试/门禁。

**结论（本任务范围）**：T46 walker 修复在当前树生效；T17 module tree 在 **真实产品壳**里
渲染出 `top.u_mid.g_leaf[0]`、`top.u_arr[3]`、`g_if.u_if` 三个路径段；对这三行做**真实鼠标双击**
（QTest 事件投递到树 viewport，不是直接调用面板方法），跳转落在实例化点的精确
`file:line:column`。lane §D 的 generate 观测从旧证据的 **2/5** 变为 **5/5**。

---

## 1. 树状态与新鲜度（禁止旧二进制冒充当前树）

- HEAD `4e2d0a57ca50db9568db5a5b040cc66f04c9a05f`；运行时刻 worktree dirty=159
  （含其他 lane 的在飞改动，例如 `crates/vd-config/src/filelist.rs`；本任务**未改任何产品文件**）。
- 固定镜像 `vedit-qt6-build:local` = `sha256:ad6557e3f5b796e5b928487d87800531bf6c167f362f9a434b93ca7a6542e18f`。
- 本任务链条（同一窗口，先生成再运行）：

| 对象 | mtime（CST） | sha256 |
|---|---|---|
| `src/workspace/module_tree/module_tree_panel.cpp`（lane 源码，最晚） | 2026-11-02 17:28:56 | — |
| `src/workspace/module_tree/module_tree_selftest.cpp` | 2026-11-02 17:23:35 | — |
| `crates/vd-deeplint/src/hierarchy.rs`（T46 修复） | 2026-11-02 17:07:34 | — |
| `target/qt6-build/src/app/workspace-lane-module_tree/vedit-module-tree-selftest` | 2026-11-02 19:19:53 | `989b7c96…073376` |
| `target/qt6-build/src/app/vedit-app` | 2026-11-02 19:19:59 | `5a6b9203…e3e640` |
| `target/t37-probe/t37-module-tree-probe`（本任务探针） | 2026-11-02 19:20:14 | `ac9935b5…29de6f` |
| `evidence/m-tree/probe/t37_module_tree_probe.cpp`（探针源码） | 2026-11-02 19:18:03 | `4a070b10…d13eeb5` |

  **源码 mtime 全部早于二进制 mtime**（最晚源码 17:28:56 < 19:19:53）。原始 mtime/sha 清单见
  `50-t37-mtimes.txt`（同一文件里也保留了 19:09 第一次门禁构建与 19:16 另一 agent
  并发重建的记录，未删改）。

## 2. 命令与退出码（全部在 `vedit-qt6-build:local` 内；`QT_QPA_PLATFORM=offscreen`）

| # | 命令 | 观察结果 / exit | 日志 |
|---|---|---|---|
| 1 | `bash scripts/build-qt.sh --no-test` | `[qt6] PASS: Rust service layer + configure + build (warnings-as-errors) inside vedit-qt6-build:local; tests skipped (--no-test)`，**exit 0** | `54-t37-build-rerun-20261102.log`（`BUILD2_EXIT=0`；同命令 19:08 的第一次运行见 `50-t37-build-20261102.log`，`BUILD_EXIT=0`） |
| 2 | lane §C/§D 重跑：`vedit-module-tree-selftest probes/slang-spike/fixture build/slang/out/slang "" evidence/m-tree/fixture-generate` | **51 checks, 0 failure(s)**，`SELFTEST_EXIT=0` | `51-t37-lane-selftest-20261102.log` |
| 3 | 探针构建：`docker run … bash -lc 'bash evidence/m-tree/probe/build-t37.sh'` | `PROBE_BUILD_OK binary=…/t37-module-tree-probe`，**exit 0** | `53-t37-probe-build-20261102.log` |
| 4 | 探针运行：`t37-module-tree-probe evidence/m-tree/fixture-t37 evidence/m-tree/t37-module-tree.png`（`VEDIT_SLANG=/vedit-build/build/slang/out/slang`，offscreen） | **18 checks, 0 failure(s)**，`PROBE_EXIT=0` | `52-t37-probe-20261102.log` |

探针属于**真实 shell**路径：`ApplicationShell` + `installModuleTree()`（产品自己在面板登记时调用的同一个入口）
+ 真实 `QTreeWidget`；`--selfcheck` **没有**被用来充当本证据（按契约）。

## 3. 三种路径段的断言（真实双击 → 精确 file:line:column）

fixture：`evidence/m-tree/fixture-t37/{top,mid,leaf}.sv`（本任务新增，只放在证据目录）：
`top` 内实例化 `mid u_mid` 与实例数组 `leaf u_arr [3:0]`；`mid` 内有
`for (g…) begin : g_leaf leaf u (…)`（6 次）与 `if (1) begin : g_if leaf u_if (…)`。
真实服务（`slang 12.1.0+0`）返回 **3 file(s) · 13 instance(s) · 41 net(s)**，模块树 133 行。

期望位置不是从 ABI 抄的：探针**读 fixture 源文件**，找到实例化语句行与实例名 token 的 1-based 列
（`t37.ground-truth.*`），再与双击后 `IJumpTarget` 收到的 `file:line:column` 比较。

| 路径段（契约要求） | 树中的行标签 | 行内 `Location` | 双击跳转（观察 = 期望） | 检查名 |
|---|---|---|---|---|
| `top.u_mid.g_leaf[0]` | `top.u_mid.g_leaf[0].u` | `mid.sv:6:12` | `mid.sv:6:12` | `t37.row/segment/jump.g_leaf0` |
| `g_if.u_if` | `top.u_mid.g_if.u_if` | `mid.sv:10:10` | `mid.sv:10:10` | `t37.row/segment/jump.g_if` |
| `top.u_arr[3]` | `top.u_arr[3]` | `top.sv:10:8` | `top.sv:10:8` | `t37.row/segment/jump.u_arr3` |

附加观察（同一日志）：`top`、`top.u_arr[0..3]`、`top.u_mid.g_leaf[0..5].u`、`top.u_mid.g_if.u_if`
共 13 个实例行全部在树中；每个 `t37.segment.*` 断言行文本确实含要求的路径段
（`g_leaf[0]` / `g_if.u_if` / `u_arr[3]`）；`t37.screenshot` 保存成功。

截图：`t37-module-tree.png`（420×779，真实 shell 的 module-tree dock；最后一行状态栏为
`top.u_arr[3] (top.sv:10:8) → top.sv:10:8`）。截图前把信号子树折叠、展开 `top`，使 13 个实例行
与三段的 `Location` 同屏。（lane 面板截图：`t37-lane-module-tree.png`。）

## 4. 与旧证据的差异（T46 之前 → 现在）

旧证据 `evidence/m-tree/10-selftest.log`（17:04，T46 修复前）§D：

```
generate fixture: slang · 2 file(s) · 2 instance(s) · 6 net(s) · 34 ms · 19688 KiB peak
observed instance paths: gen_top, gen_top.u_bank
observed 2/5 ground-truth instance(s); missing: gen_top.u_bank.g[0].u_leaf, gen_top.u_bank.g[1].u_leaf, other_top
```

现在（`51-t37-lane-selftest-20261102.log`，同一自检、同一 fixture）：

```
generate fixture: slang · 2 file(s) · 5 instance(s) · 14 net(s) · 5 ms · 19348 KiB peak
observed instance paths: gen_top, gen_top.u_bank, gen_top.u_bank.g[0].u_leaf, gen_top.u_bank.g[1].u_leaf, other_top
observed 5/5 ground-truth instance(s); missing:
```

即：旧证据里“第二个 root 丢失 + 不进 GenerateBlockArray”的两个缺口均已消失；
旧 module tree 从不出现任何 `[i]` 路径段，本任务新探针首次在**真实 shell** 中验证
`g_leaf[0]`/`g_if.u_if`/`u_arr[3]` 的显示与双击精度。

## 5. 诚实限制（本证据不证明什么）

1. **真实编辑器 target 未接入**：探针在同一个进程级 `jump::Registry` 里装的是**录制型**
   `IJumpTarget`（真实编辑器 target 是 T20 的契约）。因此本节证明“面板交付了精确
   file:line:column”，不证明“产品里真的把文件打开到该行”；也没有第二条开文件路径。
2. **平台**：`QT_QPA_PLATFORM=offscreen`。鼠标事件是 QTest 投递到树 viewport 的真实事件序列
   （press + dblclick + release，与 T49 证据一致），不是直接调用面板方法；但本任务没有在
   xcb 下重复该点击（T49 的 fail-first 证据覆盖事件接线本身）。
3. **规模**：只测 fixture 规模（3 文件 / 13 实例 / 41 net / ~6 ms）。1k 文件、10k 实例的完整性
   仍归 T44 的测量，本任务不主张。
4. **树是脏的**：dirty=159，含其他 lane 在飞改动；本次门禁与探针都在**该状态**下构建并运行，
   若之后有人继续改产品代码，二进制需按本文命令重建后再复核。
5. **T37 verdict 不在这里落**：本文件是 writer 侧复现台；T37 的 PASS/FAIL 由独立 Reviewer 在
   `loop-20261102T063252-202730`（writer `se-tree`）上 `mpd_verify_record` 给出。

## 6. 复现命令（供 verifier 独立复核）

```bash
cd /home/haroldzhao/MyProj/DshProj/VEdit
# ① 门禁（固定镜像内 Rust 层 + cmake/ninja；--no-test 跳过 ctest/xcb smoke）
bash scripts/build-qt.sh --no-test
# ② lane §C/§D（真实 hierarchy 服务；spike fixture + generate fixture）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e QT_QPA_PLATFORM=offscreen \
  -e VEDIT_MODULE_TREE_SHOT=/vedit-build/evidence/m-tree/t37-lane-module-tree.png \
  vedit-qt6-build:local bash -lc './target/qt6-build/src/app/workspace-lane-module_tree/vedit-module-tree-selftest \
    probes/slang-spike/fixture build/slang/out/slang "" evidence/m-tree/fixture-generate'
# ③ 探针构建（用 ninja 自己的 vedit-app 编译/链接命令；产物进 target/t37-probe/）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash -lc 'bash evidence/m-tree/probe/build-t37.sh'
# ④ 探针运行（真实 shell + 三段路径断言 + 截图）
bash evidence/m-tree/probe/run-t37.sh
# 或等价的显式命令：
# docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e QT_QPA_PLATFORM=offscreen \
#   -e VEDIT_SLANG=/vedit-build/build/slang/out/slang vedit-qt6-build:local \
#   bash -lc './target/t37-probe/t37-module-tree-probe evidence/m-tree/fixture-t37 evidence/m-tree/t37-module-tree.png'
```

期望：① exit 0 且出现 `[qt6] PASS:`；② `51 checks, 0 failure(s)` / `SELFTEST_EXIT=0`；
③ `PROBE_BUILD_OK`；④ `18 checks, 0 failure(s)` / `PROBE_EXIT=0`（路径段缺失或跳转偏移会直接
FAIL 并打印 observed/expected；契约要求“路径段不可见 → FAIL，交回 captain”）。

## 7. 证据索引（本任务新增/更新，全部在 `evidence/m-tree/`）

| 文件 | 内容 | sha256 |
|---|---|---|
| `50-t37-recheck.md` | 本文件 | — |
| `50-t37-mtimes.txt` | 源码/二进制 mtime、sha、镜像 id、树状态（含并发构建记录） | — |
| `50-t37-build-20261102.log` / `54-t37-build-rerun-20261102.log` | 门禁构建，exit 0 | `5482b079…` / `f8c14d1b…` |
| `51-t37-lane-selftest-20261102.log` | lane §C/§D 重跑，51/0，exit 0 | `02f67b71…` |
| `52-t37-probe-20261102.log` | 真实 shell 探针，18/0，exit 0 | `143fc328…` |
| `53-t37-probe-build-20261102.log` | 探针构建，exit 0 | `fd770e3b…` |
| `t37-module-tree.png` | 真实 shell module-tree 截图（三段同屏） | `968d3840…` |
| `t37-lane-module-tree.png` | lane 面板截图（§C 真实服务） | `85059ad6…` |
| `fixture-t37/{top,mid,leaf}.sv` | 三种路径形状的 fixture | 见 `50-t37-mtimes.txt` |
| `probe/t37_module_tree_probe.cpp` | 真实 shell 探针源码（新） | `4a070b10…` |
| `probe/build-t37.sh`、`probe/run-t37.sh` | 探针构建/运行脚本（新） | — |

原有旧证据（`10-selftest.log`、`REPORT.md` 等）**未改动**。

探针二进制是构建产物，按仓库探针惯例放在 `target/t37-probe/`（`evidence/` 只放日志与截图，
见交接 §6.1）；它不是交付物，verifier 可用 `probe/build-t37.sh` 重建后核对 sha。
本任务对工作树的写入只有 `evidence/m-tree/` 下的文件（`git status` 里表现为未跟踪的
`evidence/m-tree/`，产品源码/测试/门禁零改动）。
