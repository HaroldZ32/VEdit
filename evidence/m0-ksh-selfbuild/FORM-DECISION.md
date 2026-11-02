# T61 定案 — 白名单语法定义的注入形态：**F2（XDG 隔离）**

- cell `cell-20261031T014507403Z` · task **T61** · 席位：Qt Build Engineer · 日期 2026-10-31
- 上游实验数据：`form-experiment-logs/`（本目录）· 复跑：`bash scripts/run-ksh-form-experiment.sh`
- 契约说明：cell 记录里 T61 无 split-key 映射，`cellwork_status` 未提供 T61 的冻结契约/loop；
  验收条款取自 captain 的派活消息（T61 原文）。
- 写域：`scripts/`、`cmake/`、`evidence/m0-ksh-selfbuild/`（实验/证据均在此内）。

## 0. 决定

**选 F2**：`QRC_SYNTAX=OFF` + `NO_STANDARD_PATHS=OFF`，由 **AppRun 设置 `XDG_DATA_DIRS` 与
`XDG_DATA_HOME`**，把可见语法定义收窄到 AppDir 内的白名单。

**F1（`NO_STANDARD_PATHS=ON` + const_cast）不作为定案基础**：它唯一的注入路径是
`const_cast<Repository&>(Editor::repository())`，而 KTextEditor 6.24 **没有公开注入 API**（§2 阴性
证据）。F1 的隔离是构造性的、确定性更好，但它建立在"违反公开只读契约 + 依赖实现细节"之上，
不能作为 T11 收包的形态基础。

### F2 成立必须同时满足的条件（T11 的 AppRun 按此实现，缺一不可）

| # | 条件 | 依据 |
|---|---|---|
| C1 | 库用 `QRC_SYNTAX=OFF` 构建（库内 0 定义） | T60：G1 exit 0 / G2 xml=0 |
| C2 | 库用 `NO_STANDARD_PATHS=OFF` 构建（`scripts/versions-ksh.env` 已定 `KSH_FORM="F2"`） | §1 实测：只有 OFF 时库才会按 XDG 找定义 |
| C3 | `XDG_DATA_DIRS="$APPDIR/usr/share"`（**只含 AppDir**，覆盖宿主值；不要追加 `/usr/share`） | §1 `f2-override`：追加回 `/usr/share` 立刻泄漏 402 个定义 |
| C4 | `XDG_DATA_HOME="$APPDIR/usr/share"`（或 AppDir 内其它目录；**不能**保留默认 `$HOME/.local/share`） | §1 `f2-xdgdirs-only`：只设 C3 时用户目录的定义仍可见 |
| C5 | AppDir 内**只有白名单**：`$APPDIR/usr/share/org.kde.syntax-highlighting/syntax/*.xml`；**不得**存在 `syntax-bundled/`（压缩定义会被 `loadSyntaxFolderFromIndex` 自动加载） | 上游 `src/lib/repository.cpp` 的加载顺序 |
| C6 | AppRun 在 exec 前**自己设置** C3/C4（不依赖继承的用户环境） | §3 确定性 |
| C7 | 运行时守卫（T11/T13）：启动时枚举 `Repository::definitions()` 与 `KTextEditor::Editor::instance()->repository()`，任何不在白名单内的名字 → 告警并记录（建议同时记录 `QStandardPaths::standardLocations(GenericDataLocation)`） | 把"环境被绕过"变成可观测事件 |
| C8 | 依赖宿主图标/主题/字体的地方自备（`XDG_DATA_DIRS` 收窄后宿主数据目录不可见） | 工程副作用，见 §4 |

## 1. 问题 1 — F2 会泄漏宿主定义吗？**会，除非 C3+C4 同时成立**（实测）

实验设置（`form-experiment-logs/01-fixtures.txt`）：容器里**装着发行版 ksyntaxhighlighting**
（其定义只以嵌入形式存在，Ubuntu 包里没有外部 XML 文件——T56 §1.4 已核实），因此"宿主装了 KDE"
的**文件泄漏面**用模拟目录验证（402 个上游明文 XML；48 个生成变体不在源目录，故不是 450）。
被检进程一律用 `LD_LIBRARY_PATH` 覆盖成对应形态的自建库（否则发行版库的嵌入定义会污染结果）。
模拟宿主 KDE 安装
`/usr/share/org.kde.syntax-highlighting/syntax/`（402 个上游明文 XML，含 `systemverilog.xml`）+
唯一标记 `HostLeakProbe`；模拟用户数据 `~/.local/share/.../user-leak-probe.xml`（`UserLeakProbe`）；
AppDir 白名单只有 `verilog.xml`。

| 场景 | 环境 | defs | 可见内容 | 结论 |
|---|---|---|---|---|
| `f2-default` | 无 XDG 覆盖（默认包含 `~/.local/share`、`/usr/local/share`、`/usr/share`） | **403** | 宿主 402 + 用户 1 | **宿主泄漏**（含 `SystemVerilog`） |
| `f2-xdgdirs-only` | 仅 `XDG_DATA_DIRS=$APPDIR/usr/share` | **2** | `Verilog` + `UserLeakProbe` | 宿主被挡住，但 **XDG_DATA_HOME 仍泄漏用户定义** |
| `f2-isolated` | `XDG_DATA_DIRS` **和** `XDG_DATA_HOME` 都指向 AppDir | **1** | 只有 `Verilog` | **隔离成立** |
| `f2-editor-isolated` | 同 `f2-isolated`，另查 KTextEditor 自己的 repository | **1** | 只有 `Verilog` | **KTextEditor 无需注入即看到白名单** |
| `f2-override` | `XDG_DATA_DIRS=$APPDIR/usr/share:/usr/share` | **402** | 宿主定义回归 | **失败模式**：把宿主路径加回就泄漏 |
| `f1-default` | F1 库（`NO_STANDARD_PATHS=ON`），无 XDG 覆盖 | **0** | 无 | 构造性隔离（但注入需 const_cast） |
| `f1-editor-inject` | F1 + `const_cast` 注入 | 0 / editor 1 | editor 侧 `Verilog` | F1 的唯一路径 |

来源归因（逐条名字 + 路径）在每个场景日志里：`KSH-ENV loc dir=... xml_files=N`、
`KSH-ENV source name="..." path="..."`、`KSH-ENV def name="..."`。例如 `30-f2-default.log`
里可见 `SystemVerilog ← /usr/share/org.kde.syntax-highlighting/syntax/systemverilog.xml`；
完整 403 个名字在该日志中逐个列出；`32-f2-isolated.log` 只有 `Verilog` 一个。

## 2. 问题 2 — F1 的 const_cast 与"没有别的公开 API"的阴性证据

**头文件搜索（阴性）** —— 原始输出 `form-experiment-logs/60-no-public-api.txt`：

```sh
grep -rnE "addCustomSearchPath|setRepository|setSyntaxRepository|setHighlightingRepository" \
  /usr/include/KF6/KTextEditor/          # -> 无匹配（空）
grep -n repository /usr/include/KF6/KTextEditor/ktexteditor/editor.h
# -> 只有：const KSyntaxHighlighting::Repository &repository() const;   (editor.h:280)
#           void repositoryReloaded(KTextEditor::Editor *editor);        (信号)
ls /usr/include/KF6/KTextEditor/ktexteditor/ | grep -iE "private|p\.h"   # -> 无私有头
grep -nE "static|defaultRepository" /usr/include/KF6/KSyntaxHighlighting/.../repository.h  # -> 无全局实例
```

**编译期证明（同一文件里四条）**：

| 尝试 | 结果 |
|---|---|
| `editor->repository().addCustomSearchPath("x")` | error: passing `const KSyntaxHighlighting::Repository` as `this` discards qualifiers |
| `editor->setRepository(nullptr)` | error: no member named `setRepository` |
| `editor2->repository() = nullptr` | error: incomplete type / const 不可赋值 |
| `KSyntaxHighlighting::Repository repo; repo.addCustomSearchPath("x")`（独立仓库） | 编译通过（正控：方法存在、只是 editor 的实例只读） |

**结论**：KTextEditor 6.24 **没有公开注入 API**；F1 只能 `const_cast`。

**const_cast 的准确定性**（不夸大、也不一笔带过）：`const_cast` 本身不是 UB；UB 只发生在对
**真正 const 的对象**执行写操作时。`Editor::repository()` 返回的引用指向 KTextEditor 内部一个
非 const 成员，因此当前实现下该写操作是可用的（实测 F1 注入成功）。真正的风险是：
(1) 它违反头文件写明的 "read-only access" 契约；(2) 依赖 KTextEditor 的实现细节，跨版本无保证；
(3) 若上游未来把该对象改为真正 const 或改为返回值，行为/编译会变。因此**不作为形态基础**。
——如果将来上游提供公开 setter，F1（`NO_STANDARD_PATHS=ON`）可以重新评估，届时的最干净形态
是把"库内 0 定义"和"注入方式"分开决定。

## 3. 问题 3 — 确定性差异与可观测判据

判据（探针输出）：`KSH-ENV defs=<n> hash=<sha256(sorted visible definition names)>`。同一源码 +
同一库 + 同一环境 → 同一 hash；宿主定义变化若改变 hash，就说明结果不确定。

| 配置 | 宿主安装存在 | 宿主安装移除 | 结论 |
|---|---|---|---|
| F1（`f1-default`） | defs=0, hash=`e3b0c442…`（空串） | defs=0, hash=`e3b0c442…` | **恒定**（构造性） |
| F2 + C3+C4（`f2-isolated`） | defs=1, hash=`25d9678d…` | defs=1, hash=`25d9678d…` | **恒定**（条件成立时） |
| F2 默认环境 | defs=403, hash=`60d98554…` | defs=1, hash=`88331192…` | **随宿主变化 → 不确定** |

可观测判据给 T12/T43：对同一份 AppDir，切换宿主语法数据（有/无）各跑一次探针，比较
`defs=`/`hash=`；F2 成立条件即"两次相同且等于白名单集合"。运行时守卫（C7）用同一判据。

## 4. 法律 vs 工程（分开写，避免后人误读）

- **法律（分销）**：用户 T56/B′ 的裁决针对「**分销**」——我们不得随包分发 GPL 语法定义。
  F2 的 C1+C5 保证随包内容只有白名单；宿主机器上已有的 KDE 定义被**使用**不构成我们的分销。
- **工程（确定性）**：F2 的高亮结果依赖启动环境。C3/C4 缺失或被人为覆盖时，同一份 AppImage
  在不同宿主上会显示不同定义集合（实测 403 vs 1）——这是**工程质量问题**，不是法律问题。
  C6+C7 把它变成"受控且可观测"的状态，而不是默认假设。

## 5. 与 T60 的关系与证据位置

- T60 按当时指令交付了 **F1** 的构建与验收（G1/G2/G3/ABI/行为）；本任务改选 **F2** 并把默认值写入
  `scripts/versions-ksh.env`（`KSH_FORM="F2"`、`KSH_NO_STANDARD_PATHS="OFF"`）。
- 证据并存（同一构建脚本，`--form` 区分）：
  - `build-logs/` = **选定形态 F2** 的完整构建+验收（NO_STANDARD_PATHS=OFF）；
  - `build-logs-form-f1/` = F1 的对照构建+验收（NO_STANDARD_PATHS=ON，T60 结论仍有效）；
  - `form-experiment-logs/` = 本任务的三问实验、API 阴性证据、确定性表。
- G1/G2（库内 0 定义、无 `systemverilog.xml`）与形态无关，两个构建都通过；G3 的期望值随形态变化
  （F2: `NO_STANDARD_PATHS:BOOL=OFF`）。

## 6. 复跑

```sh
bash scripts/run-ksh-form-experiment.sh          # 构建 F1+F2 并跑全部场景（写入 form-experiment-logs/）
bash scripts/build-ksh-selfbuild.sh              # 选定形态 F2：构建+G1/G2/G3/ABI/行为
bash scripts/build-ksh-selfbuild.sh --form F1    # 对照形态 F1（写入 build-logs-form-f1/）
```

T11 收包后的复验（按 C1–C7）：见 `README-T11-packaging.md`，并把启动环境换成 C3+C4 后重跑其中的
`vedit-ksh-env-probe --list` 步骤——期望 `defs=1`（只有白名单 Verilog）、`forbid SystemVerilog present=0`。
