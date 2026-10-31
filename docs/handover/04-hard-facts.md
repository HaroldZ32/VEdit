# 04 · 硬事实：环境、许可、API 限制、不可信判据

> 这一份里的每一条都是**踩出来的**，不是查文档抄的。设计任何方案之前先扫一遍，能省掉一整轮返工。

## 1. 版本与环境（全部锁定）

| 组件 | 版本 | 说明 |
| --- | --- | --- |
| Qt | 6.10.2 | 固定镜像内构建 |
| KDE Frameworks | 6.24.0 | |
| KTextEditor | 6.24.0 | 同一套 KF6 |
| KSyntaxHighlighting | 6.24.0 | **自建**，`QRC_SYNTAX=OFF`（见 §3） |
| tree-sitter | 0.25.9 | |
| 语言 | C++20 | CMake + Ninja |
| 构建镜像 | `vedit-qt6-build:local` | 22 个 Debian 包按 sha256 锁定，组合指纹 `c97a2566…` |
| 目标下限 | **glibc 2.17 / CentOS 7.9**，min kernel 3.2.0 | 与新版系统共用同一个产物 |

锁定文件：`scripts/versions-qt.env`、`scripts/versions-ksh.env`、`scripts/versions-grammar.env`、
`scripts/docker/qt6-locked-packages.txt`。

## 2. 私有运行时（AppImage）的三个关键点

1. **自带 loader**：`AppRun` 里 `exec "$APPDIR/ld-linux-x86-64.so.2" --library-path "$APPDIR/lib" …`，
   一共 171 个库。`libc` 必须从包内解析，这点有断言守着。
2. **`LD_LIBRARY_PATH` 只能在 `exec` 之前导出**。早期把它导出得太早，导致宿主自己的 `/bin/cat` 也加载了我们的
   glibc 2.43，守卫读到的是**空白名单**，于是"空 == 空"被误判成 PASS。现在守卫在空白名单上 `exit 2`（fail-closed）。
3. **F2 注入形态**：`QRC_SYNTAX=OFF` + `NO_STANDARD_PATHS=OFF`，`AppRun` 必须**同时**设
   `XDG_DATA_DIRS="$APPDIR/usr/share"` 和 `XDG_DATA_HOME="$APPDIR/usr/share"`——**只设一个会漏**
   （宿主的定义会漏进来）。

## 3. 许可链（已闭环，别重开）

- grammar：`tree-sitter/tree-sitter-verilog@4457145e795b363f072463e697dfe2f6973c9a52`，**MIT**，LICENSE 随包。
- KDE 的 `systemverilog.xml` 是 **GPL-3.0-or-later**（sha256 `3361d8b4…`），`verilog.xml` 无许可（`7199e367…`）→ **两份都按 sha256 拉黑**。
- 随包的是自家 **LGPL-2.1+** 移植定义，且带 `<folding>` 区域（A4a）。
- 送达构建：`builtin_valid_defs=2`、`KSH-GUARD PASS whitelist=SystemVerilog,Verilog`；反向对照 `defs=404`。
- **QTermWidget 是 GPL-2.0+，本仓 MIT** → 链进去等于整包换许可。所以终端是自己写的 pty + VT（D7）。**不要推翻这个决定。**

## 4. KTextEditor 6.24.0 的硬限制（探针实测，不是听说）

### 4.1 已知限制

| 限制 | 后果 / 应对 |
| --- | --- |
| 每行属性（attribute）区间上限 **1024** | 1100 个区间会让**整行被静默丢弃**（不报错） |
| **没有文档级区间枚举**（`movingRanges()` 不存在） | overlay 必须自己维护活动区间表 |
| **没有公开的语义高亮 API** | `setDefaultStyle` 是死的；颜色要从 `Theme::textColor()` 解出来 |
| 公开头文件里**没有折叠 API** | `unfold` 只能按光标行 |
| 在折叠区内插入会**丢折叠状态** | 编辑时要主动保护 |
| `fold`/`tfold` 传非法 range 会**回退成 `selectionRange()`** | **会静默折叠用户选区**。任何折叠命令必须先自己校验 range（AMENDMENTS A1） |

### 4.2 探针新发现（2026-10-31，`evidence/edit-core/00-probe.md`）

| 发现 | 含义 |
| --- | --- |
| 公开头文件**没有** `undo()/redo()` | 只能走运行期 metaobject 槽（index 70/71）+ `invokeMethod`。**脆弱点**：Qt 版本变了槽号可能挪，实现必须带"槽不存在就断言失败"，不许静默降级 |
| `searchText` **一次只返回 1 个匹配** | 查找替换必须迭代 + 零长前进 + 自下而上 |
| `EditingTransaction` 把组内操作合并成**一条** undo（有/无 view 一致） | 多光标、全部替换各包一个事务即可 |
| KTextEditor 会把**连续键入 / 整行插入合并**成一条 undo | `replaceText`×100 → 101 步；`insertLine`×100 / 单字符×100 → **1 步**。这是框架应有的行为，**不要 fork 去改** |
| 目录 `fsync` 返回 `EINVAL` 非致命 | 原子保存按 `tmp→fsync(file)→rename→fsync(dir)`，`EINVAL` 吞掉 |

**A1b「≥100 步撤销」的验收口径（已定）**：按**逻辑操作**算，不按按键算。
多光标一次编辑 = 1 条 undo；全部替换 = 1 条 undo；100 个逻辑操作可逐次 undo/redo 与原文逐字节比对。

### 4.3 语言识别

- **`.v` 用 `definitionForFileName` 会解析成 "V"（vlang）**，不是 Verilog。必须用 `definitionForName("Verilog")`。
- 自家有一张 `file_kind` 表，绕过 KSyntaxHighlighting 的识别，别混用。

## 5. 不可作为身份判据的东西

| 不要用 | 因为 | 用什么 |
| --- | --- | --- |
| 二进制 sha256 当产物身份 | 同一份源码、不同构建树 ⇒ 哈希不同（DWARF 里的路径进哈希）。已观测到同一 smoke 的三个哈希：`f0f53bfd…`、`d629a1ad…`、`cc9f6778…` | 版本矩阵 + 冒烟输出的 key=value + 采集时间 |
| docker image id 当身份 | 同一 Dockerfile 每次构建 id 都不同（apt 时间戳） | 锁文件 + 22 个 deb 的 sha256 + 指纹 `c97a2566…` |
| `/tmp` 当备份位置 | **每条命令的 `/tmp` 是隔离的**，下一条命令就没了 | 备份一律放工作区内 |
| 宿主 shell 直接删容器产物 | 容器会以 `nobody:nogroup 0555` 写文件，宿主没有 sudo（`no new privileges`） | `docker run --rm -v "$PWD:/w" … rm -rf /w/...` |

## 6. 两条被事故教出来的规则

1. **凡是"构建通过"，必须贴出实际编译到的 target 清单**，并**证伪一次**（往 lane 里放 `#error` 让它红）。
   这条规则的由来：T63 之前 `src/syntax/provider` 编译了 **0 个文件**，而门禁全绿。
2. **证据必须带采集时间 + 树状态**；每个里程碑结束要在**冻结的树**上重采一次。
   否则会出现"证据比代码老"，或者拿一份红色日志当编译证据（`cmake/smoke/34-qt6-elf.txt`、`32-qt6-build.log` 就这么被误用过）。
