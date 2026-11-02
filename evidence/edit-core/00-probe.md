# T14 探针报告 — EditingTransaction / replaceText / fsync 的真实语义

- 时间：2026-10-31（UTC 08:22）· 环境钉：`evidence/edit-core/00-pins.txt`
  （`vedit-qt6-build:local`，Ubuntu 26.04.1 / glibc 2.43 / KTextEditor **6.24.0** / Qt **6.10.2** / gcc 15.2）
- 探针源码：`src/editor/probes/*.cpp`（独立 CMake 工程，**不进产品构建**）
- 原始日志：`00-build-probe.log`、`00-probe-{tx,tx-steps,tx-steps2,tx-steps3,tx-steps4,replace,replace2,fsync,fsync-container-tmp}.log`
- 复跑：
  ```bash
  docker run --rm -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -v "$PWD:/vedit-build" -w /vedit-build \
    vedit-qt6-build:local bash -lc '
      cmake -S src/editor/probes -B /vedit-build/evidence/edit-core/.work/pb -DCMAKE_BUILD_TYPE=Release &&
      cmake --build /vedit-build/evidence/edit-core/.work/pb -j4 &&
      /vedit-build/evidence/edit-core/.work/pb/tx_probe &&
      /vedit-build/evidence/edit-core/.work/pb/replace_probe2 &&
      /vedit-build/evidence/edit-core/.work/pb/fsync_probe /vedit-build/evidence/edit-core/.work/fs'
  ```

## 0. 判定摘要（决定实现路线）

| 探针 | 判定 | 关键观测 | 对实现的决定 |
|---|---|---|---|
| **A. EditingTransaction 合并** | **PASS** | 3 次插入在 1 个事务内 → 1 次 undo 回空；100 次插入在 1 个事务内 → 1 次 undo 回空（noview/view 同结论） | 多光标 N 处插入、全部替换：**各包一个 `EditingTransaction`**，一次撤销即全回滚 |
| **A′. undo/redo 可达性** | **PASS（需绕行）** | 公开头文件里 `KTextEditor::Document` **没有** `undo()`/`redo()` 声明；运行期 `metaObject` 有槽：`undo()`=70、`redo()`=71，`QMetaObject::invokeMethod` 可用 | 编辑内核封装 `documentUndo()/documentRedo()`，内部走 metaobject；**不 include 私有头、不做 const_cast** |
| **A″. ≥100 步撤销** | **PASS（按操作类型）** | `replaceText` ×100（不同区间）→ **101 个 undo 步**；`insertLine` ×100 / 单字符 `insertText` ×100 → **1 步**（KTextEditor 把「连续键入/连续整行插入」合并为一条 undo，这是其键入合并语义） | 验收「≥100 步」用**逻辑操作**（`replaceText` 类）构造并逐步断言；**不为逐字符撤销去对抗框架语义**（并写进文档） |
| **B. replaceText 批量偏移** | **PASS（算法修正后）** | `searchText` **每次只返回 1 个匹配**（相邻 4 个只拿到 1 个）；改为「迭代 searchText、零长匹配前进 1 字符、自下而上替换、单事务」后：相邻 4/4、零长锚点 3/3、变长 2/2、变短 2/2 全部文本精确，且**一次 undo 回原文** | `find_replace` 按迭代算法实现；搜索区间终点用 `(lines-1, lineLength(last))` 以消除 `line 1 is not within interval` 噪声 |
| **C. 原子保存语义** | **PASS** | 工作区文件系统与容器 `/tmp`：`open_tmp/write/fsync_file/rename/open_dir/fsync_dir` 全部 `rc=0 errno=0`，`stat size=14` | A1e 的故障注入矩阵可做；但**仍把 `fsync(dir)==EINVAL` 当非致命**（NFS/FUSE 存在该返回），记录告警而非失败 |

**结论：没有「能力缺失」型阻塞。** 两个 FAIL 是**我们原先的假设错了**，修正后通过：
1. `searchText` 不是「一次返回全部匹配」——必须迭代；
2. undo 粒度不是「每次插入一条」——KTextEditor 合并连续键入/整行插入（原生行为），
   而 `replaceText` 类操作是逐步的（实测 101 步）。

## 1. 探针 A 原始输出（节选）

```
TX noview metaClass=KTextEditor::DocumentPrivate undoIndex=70 redoIndex=71
TX noview case=tx3_insert   text=cd2eb083… after_undo=e3b0c442… empty=1 verdict=PASS
TX noview case=tx100_insert len=100 after_undo_len=0 empty=1 verdict=PASS
TX noview case=plain3_insert  after_undo=e3b0c442…（空）   ← 无事务也合并
TX noview case=plain100_insert len=100 after_undo_len=0  ← 同上
TX view  （与 noview 完全一致的结论）
STEPS5 insert_lines=100 steps=1 verdict=FAIL
STEPS5 replace_text=100 steps=101 verdict=PASS
```

- `EditingTransaction` 的文档语义（`document.h:130-131`、`:610-650`）被实测证实：**事务内 = 一条 undo**。
- 负控（无事务）没有出现「一次只退一步」，因为 KTextEditor 对**连续键入/整行插入**有合并策略；
  `replaceText` 不受影响（101 步）——这就是「合并来自操作类型，不是 undo 栈本身怪异」。

## 2. 探针 B 原始输出（修正后）

```
REPLACE2 case=adjacent            found=4 replaced=4 expect=4 text_ok=1 count_ok=1 one_undo_ok=1 PASS
REPLACE2 case=zero_length_anchor  found=3 replaced=3 expect=3 text_ok=1 count_ok=1 one_undo_ok=1 PASS
REPLACE2 case=longer              found=2 replaced=2 expect=2 text_ok=1 count_ok=1 one_undo_ok=1 PASS
REPLACE2 case=shorter             found=2 replaced=2 expect=2 text_ok=1 count_ok=1 one_undo_ok=1 PASS
```

修正前（`00-probe-replace.log`）四个用例都只找到 1 个匹配 → 证明「一次 searchText 只回一个」。

## 3. 探针 C 原始输出

```
FSYNC step=open_tmp rc=0 errno=0 (ok)
FSYNC step=write rc=0 errno=0 (ok)
FSYNC step=fsync_file rc=0 errno=0 (ok)
FSYNC step=rename rc=0 errno=0 (ok)
FSYNC step=open_dir rc=0 errno=0 (ok)
FSYNC step=fsync_dir rc=0 errno=0 (ok)
FSYNC stat rc=0 size=14
FSYNC verdict=PASS
```

（容器 `/tmp` 结果相同，见 `00-probe-fsync-container-tmp.log`。）

## 4. 未决/边界（写进实现文档，不隐藏）

1. **按键级撤销粒度**：offscreen 下 `QTest::keyClicks` 没有向视图写入字符（`chars=0`），因此
   「真实按键是否每键一条 undo」本次**未能测**。已知的 KTextEditor 行为是连续键入会被合并。
   产品若要求「每次按键一条 undo」，那是**用户决策**，不是实现层可以硬改的；实现按「逻辑操作」
   粒度保证一次撤销回滚整组，并在 README 写明。
2. **`undoCount` 不可达**（不是槽），因此测试用「反复 undo 直到文本稳定」计数，而不是读计数。
3. **`fsync(dir)` 的 `EINVAL`**：本次环境不出现；实现仍按非致命处理（告警 + 继续），否则 NFS/FUSE
   上保存会整体失败。
4. **`EditingTransaction` 不允许跨事件循环**（头文件明示）；实现里事务只包住同步编辑，不跨越异步等待。

---

## 5. 硬约束（交接必读，实现不得偏离）

以下四条由探针直接得出，**不是建议**；任何实现若偏离，A1b/A1d/A1e 的判据都不成立。

### 约束 1 — `undo()` / `redo()` 只能经运行期 metaobject 调用（脆弱点）
- 事实：公开头文件 `ktexteditor/document.h`（sha256 `950f0c56…`）**没有** `undo()`/`redo()` 声明；
  运行期 `doc->metaObject()` 的槽表里 `undo()`=`70`、`redo()`=`71`（`metaClass=KTextEditor::DocumentPrivate`）。
- 实现要求：封装 `documentUndo(document)` / `documentRedo(document)`，内部：
  `indexOfMethod("undo()") >= 0` **先断言**，再用 `QMetaObject::invokeMethod`；**槽不存在时必须失败
  （断言/错误返回），禁止静默降级**。
- 为什么：Qt/KF6 升级可能改变槽号（不要硬编码 70/71），甚至移除槽。用名字查、失败即报，
  才能把「框架变了」变成一条红断言，而不是一个悄悄失效的撤销按钮。
- 自检要求：`edit-core` 的自检第一条就是 `undoMeta = indexOfMethod("undo()") >= 0`，并把它写进证据。

### 约束 2 — `searchText` 一次只返回一个匹配
- 事实：相邻 4 个 `a`，一次 `searchText` 只回 1 个 range（修正前 `00-probe-replace.log` 四例全部 `found=1`）。
- 实现要求：**迭代**——每次以上一个匹配的 `end()` 作为下次起点；**零长匹配必须前进 1 个字符**
  （列 +1，到行尾则下一行行首，末行末列则终止），否则死循环；收集完再**自下而上**替换（避免偏移失效）。
- 搜索区间终点用 `(lines-1, lineLength(last))`：用 `(lines(), 0)` 会产生
  `kf.texteditor: line 1 is not within interval` 噪声（功能无碍，但会污染日志、干扰排障）。

### 约束 3 — 目录 `fsync` 的 `EINVAL` 非致命
- 事实：本环境 `fsync(dirfd)` `rc=0 errno=0`（工作区 FS 与容器 `/tmp` 都是），但 NFS/FUSE 上可能返回 `EINVAL`。
- 实现要求：`fsync(dir)` 失败时：`EINVAL`/`ENOTSUP` → **告警并继续**（rename 已完成，数据已落盘）；
  其它 errno → 作为保存失败上报。文档与测试都要覆盖「EINVAL 不导致保存失败」。

### 约束 4 — `EditingTransaction` 不跨事件循环
- 头文件明示：「Your application should not return control to the event loop while it has an
  unterminated editing sequence」。实现里事务只包同步编辑；**不得**在事务内 `await`/`processEvents`/弹窗。

## 6. A1b 的验收口径（已与 captain 确认，写清理由防误读）

**A1b 按「逻辑操作」验证，不对抗 KTextEditor 的连续键入合并。** 理由：把连续按键合并成一条 undo 是
编辑器应有的行为（VS Code / Lapce / Kate 一致），要改成「每次按键一条」必须 fork KTextEditor；
为一条验收条款 fork 框架是本末倒置。

由此，A1b 的判据钉成三条，**全部要有可重跑断言**：

1. **多光标一次编辑 = 一条 undo**：N（3/10/50）个光标同时插入 → 一次 `documentUndo()` 后全文
   逐字节回到插入前（不是部分回滚），再 `documentRedo()` 全部恢复。
2. **全部替换 = 一条 undo**：200 处匹配一次替换 → 文本等于期望，一次 `documentUndo()` 回到原文，
   `documentRedo()` 回到替换后。
3. **≥100 步撤销**：100 个**逻辑操作**（实测用 `replaceText` 类操作得到 **101** 步）逐次
   `documentUndo()` 回退、再 `documentRedo()` 重做，每一步与「该步之前的全文 sha256」比对。
   证据必须写明：**单字符 `insertText`/`insertLine` 会被框架合并成 1 步（原生语义，不视为缺陷）**，
   因此 100 步用逻辑操作构造，而不是 100 次逐字符键入。

## 7. 两条被推翻的原假设（探针的直接产出）

1. **「`searchText` 一次返回全部匹配」→ 错**：它一次只回一个，必须迭代。
   （原设计里的「先收集全部 ranges」若照旧写，会把 200 处替换变成 1 处，A1d 必挂。）
2. **「每次 `insertText` 是一条 undo」→ 错**：连续键入/连续整行插入被 KTextEditor 合并成一条 undo；
   `replaceText` 类操作则是逐步的。这直接改变了 A1b 的构造方式（见 §6）。

## 8. 交接状态

- 探针源码：`src/editor/probes/`（独立 CMake 工程，**不进产品构建**），重跑说明见 `src/editor/probes/README.md`。
- 本目录产物：`00-pins.txt`、`00-probe.md`、`00-build-probe.log`、`00-probe-*.log`；`.work/` 已 gitignore。
- **`src/editor/` 的产品代码尚未开始**（按 captain 指令停手，避免与下一轮 cellwork 写域撞车）。
