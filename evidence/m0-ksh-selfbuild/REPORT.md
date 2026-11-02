# T60 证据报告 — B′：自建 `QRC_SYNTAX=OFF` 的 KSyntaxHighlighting（库内 0 定义）+ CLI 0.27.1 锁定

- cell `cell-20261031T014507403Z` · task **T60** · 席位：Qt Build Engineer（Senior Engineer 级）
- 触发：用户裁决 **B′** + `evidence/r2-ksh-embedded/report.md` Q2/Q5 + ADR §5
- 日期：2026-10-31（Asia/Shanghai）· 构建镜像：现有 M0 Qt6 镜像（未新增镜像、未新增 Qt）
- 契约说明：cell 记录里 T60 没有 split-key 映射（`split/2.json` 无此任务），`cellwork_status` 也
  没有为它冻结 loop/契约文件，因此本任务的验收条款取自 captain 的派活消息（T60 原文），逐条对应
  在 §3。
- 边界遵守：**未改任何上游源码**（只用上游 CMake 选项）；**未改/未关任何门禁**；没有 fork
  `data/CMakeLists.txt`；写域只有 `scripts/`、`cmake/`、`evidence/m0-ksh-selfbuild/`
  （`cmake/smoke/qt6_window_smoke.cpp` 为 T60 探针扩展，在写域内）。
- **后续（T61）**：注入形态最终定为 **F2**（`NO_STANDARD_PATHS=OFF` + AppRun 设置
  `XDG_DATA_DIRS`/`XDG_DATA_HOME`），见 `FORM-DECISION.md`。本报告记录的是 T60 当时交付的
  **F1**（`NO_STANDARD_PATHS=ON`）构建证据，仍然有效；两套构建与证据并存：
  `build-logs/` = 选定形态 **F2**，`build-logs-form-f1/` = F1 对照；安装路径形如
  `target/ksh-selfbuild/{F1,F2}/install/lib/x86_64-linux-gnu/`。G1/G2 与形态无关；G3 的期望值
  随形态变化（F1: `NO_STANDARD_PATHS:BOOL=ON`，F2: `:BOOL=OFF`）。

## 0. 结论（BLUF）

用上游官方选项在 **v6.24.0** 源码上重建 KSyntaxHighlighting：
`-DQRC_SYNTAX=OFF -DNO_STANDARD_PATHS=ON` → **库内语法定义数 = 0**，定义改为安装为文件；
与 **KTextEditor 6.24.0 / katepart** 的 ABI 实测兼容（KTextEditor+katepart 需要的 101 个符号
全部可解析，`ldd -r` 无未解析重定位，katepart 实际加载、文档创建成功）；白名单定义经
`addCustomSearchPath` 按**名字**加载后 Verilog 真实高亮（21 个格式区间）。

四项验收实测值：

| 验收 | 命令 | 实测 |
|---|---|---|
| **G1 主闸门** | `python3 evidence/r2-ksh-embedded/scan-embedded-defs.py --expect-clean <our-lib.so>` | **exit 0**（`strict-GPL definitions embedded: 0`）；同一命令跑发行版库 **exit 1**（36 个 GPL，含 `SystemVerilog GPLv3+`）→ 闸门仍有效 |
| **G2** | UTF-16 资源名表扫描（脚本内联，`30-g2-resource-names.txt`） | **`xml resource names: 0`**，`systemverilog.xml present: False`，exit 0 |
| **G3 构建来源** | `grep 'QRC_SYNTAX:BOOL=OFF' $BUILD/CMakeCache.txt` + `NO_STANDARD_PATHS` | **两行都在**：`QRC_SYNTAX:BOOL=OFF`、`NO_STANDARD_PATHS:BOOL=ON`（`20-cmakecache-keys.txt`） |
| **行为** | 冒烟程序 `--ksh-selfbuild --expect-empty-syntax [--custom-syntax-dir … --highlight-check]` | 空定义：`ok=1 builtin_valid_defs=0` 不崩溃；白名单：`whitelist_verilog=1 highlight_formats=21`，`kshlib` 解析到我们自己的 `.so`，`ok=1` |

**「库内定义数 = 0」可重跑命令**：

```sh
bash scripts/build-ksh-selfbuild.sh
python3 evidence/r2-ksh-embedded/scan-embedded-defs.py --expect-clean \
  target/ksh-selfbuild/F1/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0
# -> resource names (utf-16le): xml=0 theme=30 / zstd frames parsed as definitions: 0
# -> RESULT: PASS - no strict-GPL definition embedded        (exit 0)
# 负控（必须是 exit 1）：
python3 evidence/r2-ksh-embedded/scan-embedded-defs.py --expect-clean \
  /usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6
```

**主题开关结论**：上游**没有** `QRC_THEMES` 一类开关。`data/CMakeLists.txt:107` 无条件把
`themes/theme-data.qrc` 加进库，因此 30 个 `.theme` 资源与 `QRC_SYNTAX` 无关，仍留在 `.so` 里。
按要求**没有** fork `data/CMakeLists.txt`。补充清点（U11）：**30 个主题全部声明 MIT**
（`70-theme-embedding-investigation.txt`），即主题的许可是可归因、可清点的，不是未知。

## 1. 上游来源与开关（可归因）

| 项 | 值 |
|---|---|
| 来源 | 官方 KDE Frameworks 6.24 发布 tarball `syntax-highlighting-6.24.0.tar.xz` |
| URL | `https://download.kde.org/stable/frameworks/6.24/syntax-highlighting-6.24.0.tar.xz` |
| sha256 | `061c5946dc6170ecfa91b9c7d4faa5ec81eb53bb5f86eda861c2ce748a65486d`（与上游 `.sha256` 一致，构建时校验） |
| tag / commit | `v6.24.0` / `bbea2326fbd4fc720518efc43f1b018f8a7d0670`（commit 由 T56 核实） |
| 选项位置 | `CMakeLists.txt:66` `option(QRC_SYNTAX … ON)`；`CMakeLists.txt:72` `option(NO_STANDARD_PATHS … OFF)` |
| 是否改源码 | **否**。构建只用选项；`git status` 无上游文件改动（源码在 `target/ksh-selfbuild/src`，gitignored） |

锁定文件：`scripts/versions-ksh.env`（源码 URL/sha256/commit、两个开关、期望的 0 定义/30 主题）。

## 2. 构建与产物

```sh
bash scripts/build-ksh-selfbuild.sh        # 复用 M0 镜像；下载校验源码 → configure → build → install → 全部验收
```

configure 关键参数（`evidence/m0-ksh-selfbuild/build-logs/10-configure.log`）：

```
-DQRC_SYNTAX=OFF -DNO_STANDARD_PATHS=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
-DCMAKE_INSTALL_PREFIX=/vedit-build/target/ksh-selfbuild/install
-DCMAKE_SKIP_INSTALL_RPATH=ON      # 产物是打包输入，不能带构建树 RUNPATH
```

产物（`21-library-identity.txt`）：

```
libKF6SyntaxHighlighting.so.6.24.0
  size=696,328 B   sha256=2d19bd15849fa4f98601bb55f9ea3eca8a1c35d10058d37ae2ca71312ef750ce
  SONAME: libKF6SyntaxHighlighting.so.6
  RPATH/RUNPATH: (none)
  NEEDED: libQt6Gui.so.6, libQt6Network.so.6, libQt6Core.so.6, libstdc++.so.6, libm.so.6, libc.so.6
对照（发行版同名库）: 2,010,872 B（内嵌 450 定义）
```

两次独立全量构建（`10-configure.log`…`52-run-whitelist-verilog.log` 的两轮）产出**相同 sha256**
（`2d19bd15…`），即本环境内该配置是可复现的；跨环境请以 `scripts/versions-ksh.env` 的源码 pin +
选项为准，不要把 sha256 当跨环境常量。

## 3. 验收实测（逐条）

### G1 — 库内不再有 GPL 定义（主闸门）

`50-g1-scan-selfbuilt.log`（自建库）：`xml=0 theme=30`、`zstd frames parsed as definitions: 0`、
`strict-GPL definitions embedded: 0`、`RESULT: PASS`、**exit 0**。
`51-g1-scan-distro.log`（发行版库负控）：`strict-GPL …: 36`，含
`SystemVerilog license='GPLv3+'`、`Verilog license=''`、`RESULT: FAIL`、**exit 1**。
即：**自建库干净，且扫描闸门没有被"搞脏"**。

### G2 — 资源名表里没有 `systemverilog.xml`

`30-g2-resource-names.txt`：`xml resource names: 0 -> []`，`systemverilog.xml present: False`，
`RESULT: PASS`，exit 0。（主题名仍是 30 个，见 §4。）

### G3 — 干净来自构建配置

`20-cmakecache-keys.txt`：

```
BUILD_TESTING:BOOL=OFF
CMAKE_BUILD_TYPE:STRING=Release
CMAKE_INSTALL_PREFIX:PATH=/vedit-build/target/ksh-selfbuild/install
NO_STANDARD_PATHS:BOOL=ON
QRC_SYNTAX:BOOL=OFF
```

脚本用 `grep -q "^QRC_SYNTAX:BOOL=OFF$"`、`grep -q "^NO_STANDARD_PATHS:BOOL=ON$"` 断言，任一不符
即 `FATAL G3` 退出。

### ABI — KTextEditor 6.24.0 / katepart 实测（不是"只编译通过"）

- SONAME 与发行版一致：`libKF6SyntaxHighlighting.so.6`；
- **KTextEditor + katepart 实际需要的 101 个符号全部可解析**，缺失 0
  （`31-symbol-diff.txt`）；两库导出集差异只剩 C++ ABI 细节
  （`_ZTISt19_Sp_make_shared_tag` 等），已列明并说明不参与消费者解析；
- `LD_LIBRARY_PATH=<自建>/lib ldd -r libKF6TextEditor.so.6` 与 `ldd -r katepart.so` 均 **(none)**
  （`32-abi-loading.txt`）；
- 冒烟程序在 `LD_LIBRARY_PATH` 覆盖下运行：`ldd` 显示 `libKF6SyntaxHighlighting.so.6 => <自建>/lib/…`，
  程序自检字段 `kshlib=/vedit-build/target/ksh-selfbuild/F1/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0`；
  `editor=1`（katepart 加载成功）、`document=1`（文档往返）。

### 行为 — 空定义不崩溃；白名单按名字高亮

- `51-run-empty-definitions.log`：不提供任何自定义路径 →
  `M0-SMOKE ok=1 … builtin_valid_defs=0 … editor=1 document=1`（`definitions=1` 是引擎内置的无效
  "None" 条目，`builtin_valid_defs` 只数有效定义）→ **未崩溃**。
- `52-run-whitelist-verilog.log`：白名单目录（`<dir>/syntax/verilog.xml`，来自上游 `data/syntax/`，
  测试夹具，uuid+sha256 见 `50-whitelist.txt`）经 `addCustomSearchPath` 加载后：
  `whitelist_verilog=1 highlight_formats=21 definitions=2 ok=1`
  —— **按名字**取 `Verilog`（刻意不依赖 `.v` 扩展名，避开 T10 发现的 `.v`→"V"(vlang) 陷阱），
  且引擎真的产出 21 个格式区间。
  日志里 `Unable to resolve external include rule for definition "Comments"` 是单文件夹具缺少
  `comments.xml` 等被 include 的定义导致的（高亮仍工作）；T16 的白名单需一并带上依赖文件。

### KTextEditor 集成探针（重要发现，见 §5）

`M0-SMOKE ksh-selfbuild ktexteditor_repo_valid_defs_before=0 ktexteditor_repo_whitelist_visible=1
(const_cast probe only)`：KTextEditor 自己的 repository 在 `NO_STANDARD_PATHS=ON` 下同样是 0 个有效
定义；只有通过 `const_cast` 对 `Editor::repository()` 注入自定义路径后才可见（探针实测 =1）。

## 4. 主题：无开关，30 个仍内嵌；许可已清点为 MIT

- `grep -riE "option\(.*theme" CMakeLists.txt data/CMakeLists.txt` → **无此类开关**；
- `data/CMakeLists.txt:107` `target_sources(SyntaxHighlightingData PRIVATE …/themes/theme-data.qrc)`
  无条件执行 → 30 个 `.theme` 仍在 `.so` 里（`30-g2-resource-names.txt`: `theme resource names: 30`）；
- 30 个主题文件的 metadata 全部声明 **MIT**（逐文件清单在 `70-theme-embedding-investigation.txt`）。

结论：**没有独立开关**；按任务边界**没有 fork** `data/CMakeLists.txt`；如果 T16/T11 需要"库内连主题
也没有"，那只能改上游 `data/CMakeLists.txt`（需要用户/计划决定，不在本任务做）。

## 5. 关键集成发现（给 T11 / T16 决策）

`NO_STANDARD_PATHS=ON` 是**进程内全局**的：KTextEditor 与 katepart 用的 `Repository` 实例同样不再
查 QStandardPaths。而 `KTextEditor::Editor::repository()` 的公开签名是
`const KSyntaxHighlighting::Repository &repository() const;`（`editor.h:280`）——
**没有公开的注入接口**。实测有两条路：

1. **const_cast 注入**（本次探针用的）：`const_cast<Repository&>(editor->repository())` +
   `addCustomSearchPath(<AppDir>/share/syntax)`，实测可用（`ktexteditor_repo_whitelist_visible=1`），
   但这是代码异味，且依赖"对象本来不是 const"的假设；
2. **改构建配置**：`QRC_SYNTAX=OFF` **+ `NO_STANDARD_PATHS=OFF`**，把白名单装到
   `<AppDir>/share/org.kde.syntax-highlighting/syntax/`（纯 XML）或 `.../syntax-bundled/`
   （带 index），由 AppRun 设 `XDG_DATA_DIRS=<AppDir>/share`。这条正是需求 C6 的选项一
   （“用 XDG_DATA_DIRS 或 addCustomSearchPath”），KTextEditor 与 katepart 会自动加载，无需
   const_cast、无需 API hack。

**建议**（决策权在 captain/T16/T11）：B′ 的打包形态用配置 2（`QRC_SYNTAX=OFF` +
`NO_STANDARD_PATHS=OFF` + XDG_DATA_DIRS）更干净；本任务按 captain 指令构建并验证的是
`NO_STANDARD_PATHS=ON`，若最终改走配置 2，重跑命令与证据结构不变，只需把
`scripts/versions-ksh.env` 的 `KSH_NO_STANDARD_PATHS` 改为 `OFF` 并重跑本报告 §0 的命令。

## 6. tree-sitter CLI 0.27.1 纳入锁定（N5）

- `scripts/versions-grammar.env`：CLI 0.27.1 的两件产物都锁 sha256——
  npm 包 `tree-sitter-cli-0.27.1.tgz`（`4ece7204684483f281b5e10097caefc92afbd6362a948a12e755d711da9b15ba`）
  与真正执行的 `tree-sitter-cli-linux-x64.zip`
  （`c7e686aec16ba17053c2e6fd87600ccb10cf8ba3d055756b17e756d538e11114`，8,642,525 B）。
- `scripts/check-tree-sitter-cli.sh`：下载→sha256 校验→解压→断言
  `tree-sitter --version == "tree-sitter 0.27.1"`；**在镜像内**运行通过
  （`build-logs/60-tree-sitter-cli-pin.txt`：`PASS: tree-sitter CLI 0.27.1 …`，exit 0）。
- 明确**没有**把它混进 M0 运行时清单：运行时库仍是 `versions-qt.env` 里的 libtree-sitter 0.25.9
  （`versions-grammar.env` 头部写明了两者为什么分开）。

## 7. 边界与残余风险

- 本任务只证明**机制**：白名单夹具是上游 `verilog.xml`（`license=""`，T56 已知）；**随包**用哪份
  定义（ADR §5 的自有 LGPL 移植 / 接受 GPLv3+ / 只做 `.v`）不是 T60 的决定。
- 主题 30 个仍内嵌（无开关）；许可均 MIT，但"是否随包"仍属 T16/T11 的判断。
- 未处理 X11/字体/plugin 依赖：那是 T11 的收包范围；本报告只给"哪些文件必须/不得随包"的清单
  （`README-T11-packaging.md`）。
- 冒烟探针用 `const_cast` 仅用于测量，不代表产品实现。
- 与 T13 并行：`src/app` 正在落地，探针只构建 `--target vedit-m0-smoke`，不受其源码编译状态影响。

## 8. 复跑清单

```sh
bash scripts/build-ksh-selfbuild.sh                     # 构建 + G1/G2/G3 + ABI + 行为，全套
python3 evidence/r2-ksh-embedded/scan-embedded-defs.py --expect-clean \
  target/ksh-selfbuild/F1/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0   # G1
python3 evidence/r2-ksh-embedded/scan-embedded-defs.py --expect-clean \
  /usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6                                # G1 负控（exit 1）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash scripts/check-tree-sitter-cli.sh                                                   # CLI 0.27.1 镜像内断言
```

证据文件（`evidence/m0-ksh-selfbuild/`）：`README-T11-packaging.md`、本报告、
`build-logs/`（源码校验、configure、build、install、CMakeCache、库身份、G2、符号差异、ABI 加载、
白名单、两次行为运行、G1 正/负控、CLI 断言、最终运行）、`70-theme-embedding-investigation.txt`、
`upstream/`（构建时抓取的 v6.24.0 CMake/Repository 源码，用于归因）。
