# r2-fold 决策：KTextEditor 折叠能否被外部 range 驱动（Open Q3）

**结论：有条件可用（conditional）——「精确折叠」可以“叠加”实现，但只能在“注入”方向，
不能做“查询/按 range 删除/每帧增量更新”。**

- 公共 KTextEditor API **没有**折叠模型（安装头文件里零折叠类/接口）；折叠实现在
  KatePart 私有的 `Kate::TextFolding`。
- 但公开的 **Editor 命令接口** `KTextEditor::Editor::commands()` 暴露了 KatePart 注册的
  `fold` / `tfold` / `unfold`：`KTextEditor::Command::exec(view, cmd, msg, range)` 接受
  **调用方显式给出的 Range**。实测该 range 与语法定义无关（无语法定义也能折叠）。
- 因此 M1 的精确折叠建议采用「应用侧 registry + 命令注入」的**叠加**方案；
  “替换折叠视图”只在需要可编程查询/批量增删/每帧更新时才值得（量级 L，见 §5）。

- Board task: `T9` · 契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/r2-fold-probe.md`
- 探针源码: `probes/fold-interface/`（与着色探针完全分开，未合成一个 probe）
- 证据目录: `evidence/r2-fold/` · 镜像 `vprobe-qt6kf6:1`（Qt 6.10.2 / KTextEditor 6.24.0）

## 1. 复现命令

```bash
docker run --rm -e HOME=/tmp -v "$PWD":/work -w /work/probes/fold-interface \
  vprobe-qt6kf6:1 bash scripts/run-probe.sh /work/evidence/r2-fold
# 观察到的结果：02-run.log 结尾 RESULT=PASS failures=0；09-result.txt = failures=0
```

**Gate / 退出码：**本任务只新增 `probes/fold-interface/` 与 `evidence/r2-fold/`，未改
`src/`/产品源码；仓库级 gate 不受影响。探针 gate = 构建 + Xvfb 运行 + 断言，
实测退出码 `0`、`09-result.txt` = `failures=0`。

## 2. (a) KTextEditor 折叠相关 API 梳理（以镜像内安装头文件 + 6.24.0 源码为准）

### 2.1 公共 API：没有折叠模型

```text
$ grep -ril 'fold' /usr/include/KF6/KTextEditor/ktexteditor/
  view.h            # 只出现在配置键文档：folding-bar / folding-preview / folding-marker-color
  movingrange.h     # 只出现在 zDepth 文档注释（"Folding Hover == -100.0"）
$ grep -rn -E 'class [A-Za-z_:]*Fold|FoldingModel|CodeFoldingModel' /usr/include/KF6/KTextEditor/
  NONE
```

即：公共 API 只能配置折叠栏显隐/颜色，**没有**“提供 fold ranges / 查询折叠状态 /
折叠/展开指定 range”的接口。`View::firstDisplayedLine(RealLine|VisibleLine)` 与
`verticalScrollBar()` 是仅有的可用来**间接观测**折叠效果的公共入口。

### 2.2 内部实现（未安装，仅用于解释行为与评估替换成本）

| 位置（ktexteditor 6.24.0 源码） | 作用 |
|---|---|
| `src/buffer/katetextfolding.h:32` | `class Kate::TextFolding`：唯一的折叠模型 |
| `src/buffer/katetextfolding.cpp:76` | `newFoldingRange(Range, FoldingRangeFlags)` |
| `src/view/kateview.h:827` | `KateView::textFolding()`（内部访问器） |
| `src/document/katebuffer.cpp:546/559/593/610` | 从 KSyntaxHighlighting 的 fold region 生成折叠区域 |
| `src/view/kateviewinternal.cpp:2429` | 折叠展开/收起的行布局计算 |
| `src/utils/katecmds.cpp:258` | 命令 `fold`：`newFoldingRange(range, Persistent \| Folded)` |
| `src/utils/katecmds.cpp:261` | 命令 `tfold`：`newFoldingRange(range, Folded)` |
| `src/utils/katecmds.cpp:264` | 命令 `unfold`：只对**光标所在行**起始的折叠执行 `unfoldRange` |

结论：折叠模型单一（TextFolding），内置折叠由语法定义驱动，外部只能通过**命令**间接操作。

## 3. (b) 最小 probe：外部 range 驱动折叠的实测结果

探针：`probes/fold-interface/src/main.cpp`；输出 `02-run.log`。

### 3.1 接口可得性

```text
INFO command_unfold_supports_range=0 (unfold is cursor-based by design)
CHECK command.fold.registered PASS
CHECK command.unfold.registered PASS
CHECK command.fold.supportsRange PASS     # fold 接受显式 range
CHECK command.unfold.is.cursor.based PASS
INFO fold_command_registrations=28        # 每个存活 view 注册一份命令对象
```

### 3.2 外部 range 折叠：成功且可观测

Verilog 模式（语法定义存在）下，对**非定义区域**的 `Range(2,0,10,0)` 调 `fold`：

```text
SNAPSHOT baseline_verilog   last_real=53 last_visible=53 scroll_max=397
SNAPSHOT after_fold_2_10    last_real=61 last_visible=53 scroll_max=389
CHECK fold.external.range.returned.true PASS
CHECK fold.external.range.observable PASS     # 视口内隐藏行数 0 -> 8
```

`scroll_max` 恰好减少 8（该 range 隐藏第 3–10 行共 8 行），视口能容纳的真实行数从 53 增至 61。
截图 `10-fold-external-range.png`（行号跳跃 + 折叠起始行下划线）。

**关键：不依赖语法定义。** 在 `highlightingMode=None` 的文档上折叠 `Range(4,0,12,0)`：

```text
INFO fold_without_definition ok=1
CHECK fold.works.without.syntax.definition PASS
```

→ 折叠由**调用方给出的 range** 驱动，而不是由 KSyntaxHighlighting 的 fold region 驱动。

### 3.3 展开、编辑、重叠、非法输入

```text
# 展开：把光标放到折叠起始行，执行 unfold
SNAPSHOT after_unfold_line_2  scroll_max=397      # 完全恢复
CHECK unfold.returned.true PASS
CHECK unfold.restores.visibility PASS

# 在折叠区间上方插入一行：折叠随文本移动（仍隐藏 8 行）
INFO edit_above_fold hidden_before=8 hidden_after=8
SNAPSHOT after_insert_above scroll_max=390        # 文档 +1 行，折叠整体下移

# 在折叠区间内部插入一行：折叠被自动展开（状态丢失）
INFO edit_inside_fold hidden_after=0
SNAPSHOT after_insert_inside scroll_max=399
CHECK edit.inside.fold.no.crash PASS
CHECK edit.inside.fold.unfolds.range PASS

# 重叠折叠：两次都返回 true，无崩溃（嵌套语义不可查询）
INFO overlapping_folds a=1 b=1
CHECK overlapping.folds.no.crash PASS

# 非法输入：空 range=false、倒置 range=true（无效果）、越界 range=false
INFO empty_range_ok=0 inverted_range_ok=1 beyond_end_ok=0
```

### 3.4 注入成本（4001 行文档，60 次）

```text
MEASURE fold_command_exec   n=60 p50_us=680.50  p95_us=1081.92  mean_us=748.22
MEASURE unfold_command_exec n=60 p50_us=719.22  p95_us=1042.20  mean_us=757.96
```

单次 ~0.7 ms（含布局/滚动条更新）。**可以交互级使用，不能“每帧对全文档重建”**：
100 个折叠 ≈ 70 ms。适合“打开文件 / 切换开关 / 重解析完成后”批量注入，并按 diff 只处理变化项。

### 3.5 能否增量、能否共存、能否查询

- **增量创建**：可以（每次 `fold` 只影响一个 range，代价固定 ~0.7 ms）；
  **增量修改/删除**：不可以——没有“按 range 展开/删除”的 API，`unfold` 只能作用于
  **光标所在行起始**的折叠（`supportsRange=false`）；要删掉任意一条只能移动光标再 unfold。
- **共存**：折叠模型只有一份（`Kate::TextFolding`），命令注入与语法定义驱动的折叠共享它，
  因此“共存”是**真**的（不会有两套模型打架）；但**无法查询已有折叠**，应用侧的记账表
  无法与用户手动折叠保持一致。冲突策略只能是：我们只在开关/重解析时批量重建，
  平时不触碰用户折叠。
- **查询**：`INFO state_query_api=absent` —— 没有公共 API 返回折叠 ranges 或折叠状态，
  只能用 `firstDisplayedLine/lastDisplayedLine(RealLine|VisibleLine)` 与
  `verticalScrollBar()->maximum()` 间接观测。

## 4. 三态结论

| 维度 | 结论 |
|---|---|
| 能否由外部 range 提供 fold ranges | **能**（`Command::exec(..., range)`，与语法定义无关） |
| 能否增量更新 | **部分**：可增量“新增”，不能按 range 删除/移动/查询；编辑内部会自动展开 |
| 能否与自带折叠共存 | **能**（同一模型），但无法仲裁/查询，冲突靠应用侧策略规避 |
| 能否精确控制（查询、批量增删、每帧重建） | **不能**（公共 API 不提供） |

→ **“精确折叠”可以叠加实现（注入方向），但不能完全接管折叠（管理方向）。**

## 5. 若仍需“替换折叠视图”：接口边界与工作量量级

（本任务不实现替换，仅给边界与量级。）

**必须由替换方拥有的层：**

1. **折叠数据源**：tree-sitter fold query 或 KSyntaxHighlighting folding region → `FoldRange` 列表；
2. **折叠状态存储**：folded ids、展开/收起、与文本编辑的跟随（插入/删除后的重映射）；
3. **可见行映射**：real line ↔ visible line，贯穿光标、滚动条、搜索/替换、选区、点击坐标；
4. **渲染**：行号栏折叠标记、隐藏行、折叠预览；需要接入（或重写）`KateRenderer`
   与 `KateViewInternal` 的布局；
5. **交互**：点击折叠栏、快捷键、全展开/全折叠、以及 A1f 要求的“随 tree-sitter 开关切换”。

**公共 API 完全无法控制行可见性与折叠栏绘制**，因此只有两条路：

| 路线 | 代价 | 量级 |
|---|---|---|
| 命令注入（本决策推荐） | 只写应用侧 registry + 增量同步；不碰 KTextEditor 内部 | **M：约 2–4 人日**（含开关切换、观测与测试） |
| 替换折叠视图（fork KatePart 私有层：TextFolding/KateRenderer/KateViewInternal） | 触及私有 API，需长期跟随上游升级重做 | **L：≥5k LOC，4–8 人周**（且每次 KTextEditor 升级都要重新验证） |
| 完全自绘文本视图（放弃 KTextEditor::View） | 等同重写编辑器 | **XL：不建议** |

## 6. 对 R2 验收（A1c/A1f/C17 相关）的影响

- **A1c（tree-sitter fold ranges 落地）**：可满足——tree-sitter 产出的 ranges 经
  `fold` 命令注入即可观测折叠；`Range` 由我们给出，不依赖语法定义。
- **A1f（随 tree-sitter 开关可观测变化）**：可满足，但切换时必须**重建**：
  先逐条 `unfold`（光标移到每条起始行）清空我们的折叠，再按新来源重新 `fold`；
  50 条 ≈ 35 ms，属于“开关级”操作，不能每帧做。切换后折叠可见性变化可由
  `lastDisplayedLine(RealLine/VisibleLine)` 的差值断言（本探针已在用该判据）。
- **Open Q3（折叠是叠加还是替换）**：**叠加**（命令注入）。替换只在“需要可编程查询/批量增删/每帧更新”
  时才需要，量级 L，不建议放进 M1。

## 7. 本次未证明 / 局限

- 折叠状态无法被查询，本探针的“隐藏行数”判据是间接观测（视口内 real/visible 行数差与滚动上限）；
  极端布局（自动换行、缩放）下该判据的稳定性未测。
- 未测多视图/多文档共享同一 document 时的折叠模型共享与命令路由。
- 未测折叠与搜索/替换、光标映射、KTextEditor 批注（annotation/inline note）的交互。
- 未测大量折叠（1000+）的注入总耗时与内存；未单独测 `tfold`（临时折叠）。
- `inverted_range_ok=1` 的语义（返回成功但无可见效果）来自 KatePart 内部容错，未进一步追查。

## 8. 证据清单

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 镜像/Qt 版本、公共头文件折叠 API 扫描（含零 API 结论）、内部实现文件行号 |
| `00-image-id.txt` | 探针镜像 id/digest |
| `01-build.log` | CMake/ninja 构建输出 |
| `02-run.log` | 全部 SNAPSHOT / MEASURE / CHECK / INFO（含三态结论所需观测） |
| `03-artifacts.txt` | 截图路径与 sha256 |
| `09-result.txt` | `failures=0` |
| `10-fold-external-range.png` | 外部 range 折叠后的截图（行号跳跃） |
| `11-bigdoc-fold-series.png` | 4001 行文档 60 次 fold/unfold 后 |
| `12-folded-view.png` | 6 个折叠同时存在的视图（行号 1,2,10,11,…） |
