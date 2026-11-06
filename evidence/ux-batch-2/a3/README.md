# A3 证据 — VHDL / Markdown / Tcl 语法高亮（+ SDC/XDC → Tcl）

任务：team `ux-batch-2` / 行 A3；契约 `.mpd/plans/ux-batch-2/00-plan.md` §3.A3（冻结）。
本目录是验收证据：**跑过的命令、观察到的输出、没证明的部分**。

## 0. 一句话结论

5 份定义随包（Verilog/SystemVerilog/VHDL/Markdown/Tcl），6 类扩展名（`.v/.sv/.vhd|.vhdl/.md|.markdown/
.tcl/.sdc|.xdc`）在 **ctest** 与 **真 AppDir + 交付 F2 库 + AppRun 的 XDG 布局** 下都解析到期望定义名；
构造级断言（关键字/注释/字面量/类型名/围栏代码块/变量/命令替换/SDC 命令）全绿。

**repair 轮之后的真实数字**（本目录 `logs/09`、`logs/10` 由最终源码重建后重新生成，
`a3-repair/` 是逐条修复证据）：

- AppDir 断言 **104 checks / 0 failures**——完整输出含每条 `definition-parsed.*`（真实文件路径），
  见 `logs/09-appdir-checks-full.log`；
- ctest 相关 6 个测试 **6/6 通过（100%，0.96 s）**，含 `vedit-app-syntax-startup`
  （另一 lane 的悬垂指针修复后已绿），见 `logs/10-final-green.log`；
- 打包 self-check **SELFCHECK PASS**（22 个 CHECK；`APP-SHELL-SELFTEST ok=1 checks=312 failures=0`；
  `LC_ALL=C` 与 `LC_ALL=zh_CN.UTF-8` 下的 `ksh_guard` 均 PASS），见
  `a3-repair/10-selfcheck-appdir.log`；
- F2 隔离 **11 pass / 0 fail**（宿主反控 `defs=406`、用户级反控 `defs=6`、同名遮蔽负控被报告），
  见 `a3-repair/11-f2-isolation.log`；遮蔽布局下门口的两个“获胜文件”断言确实会 FAIL（`a3-repair/12`）。

**修复前的历史数字（已作废，见 `logs/00-LOG-INDEX.md`）**：AppDir 86 checks；交付的两份 ctest 日志
一份是 75%（1 个并发写树导致的 `vedit-edit-core-selftest` BAD_COMMAND）、另一份是 6 个中 5 个通过
（`vedit-app-syntax-startup` SEGFAULT，后归因另一 lane 并修复）；F2 脚本 6 pass/0 fail，但其中
“无重复名字”的判据由 KSyntaxHighlighting 的按名去重决定，**永远不可能失败**。

## 1. 产品映射表（`src/editor/kernel/highlight_mode.cpp`，表优先）

| 扩展名 | 定义名 | 定义文件（随包） | 定义文件 sha256 | 行/字节 |
|---|---|---|---|---|
| `.v` `.vh` `.vlg` | `Verilog` | `resources/syntax/vedit-verilog.xml` | `c6adc763c7b280d7493f48d06e5779d0b1e2068ac82f75fe7f31a07b0788a6c2` | 85 / 5,111 |
| `.sv` `.svh` | `SystemVerilog` | `resources/syntax/vedit-systemverilog.xml` | `2abba3342d0878e71b99d358d96e510f857cd7f232786951be8602f513e0b992` | 90 / 5,492 |
| `.vhd` `.vhdl` | `VHDL` | `resources/syntax/vedit-vhdl.xml` | `47a4539e08fc6b6257d0897c6187c25c4d9620fe8ac2b2023729b31d6ef75451` | 106 / 6,836 |
| `.md` `.markdown` `.mdown` `.mkd` | `Markdown` | `resources/syntax/vedit-markdown.xml` | `9724927b63ef9824739727f471ea7e98d61d0e9ff23b054c82ae21c73e70fc5a` | 59 / 3,559 |
| `.tcl` `.tk` | `Tcl` | `resources/syntax/vedit-tcl.xml` | `3e201a1df4ad0afc679635a105965a067d16fe4c3618662def4676680572e126` | 78 / 7,429 |
| `.sdc` `.xdc` | `Tcl` | （无独立文件，表映射到 Tcl） | — | — |

两种行为被断言：①表命中即返回（`.v` 不会被宿主的 `V`/vlang 抢走；`.sdc` 即使被 decoy 定义认领也仍是
`Tcl`）；②表未命中时回退到编辑器仓库的**任意合法定义**（不再要求 `author=="VEdit"`），都没有则空。

## 2. 许可与来源（逐份如实声明）

**三份新定义都是「独立编写」**：上游 KDE 文件只作**功能参照**（关键词/命令/构造清单的交叉核对），
**没有复制**任何文件结构、注释、正则或文本。因此按本仓库自己的许可登记
（`author="VEdit"`、`license="LGPL-2.1-or-later"`，全文 `docs/licenses/LGPL-2.1.txt`）。
若将来有人把某一份改成「改编自上游 X」，那一份**必须**改按上游许可登记（见 §2.2）。

### 2.1 上游参照（非复制，原件在本目录 `raw/`）

| 上游文件（KDE syntax-highlighting **v6.24.0**） | URL | 原件 sha256 | 该文件自述的许可 |
|---|---|---|---|
| `data/syntax/vhdl.xml` | `https://invent.kde.org/frameworks/syntax-highlighting/-/raw/v6.24.0/data/syntax/vhdl.xml` | `5df96ab7362d9ae05008ff130836f10cafb433b3ca7323cf0dc2f197762d1c4e` | **文件内无许可声明**（连 `license=""` 都没有）；但 Debian 版权整理把它列在 **MIT** 段（`raw/debian-kf6-copyright.txt:136`），不是"无许可"——见 §2.2 |
| `data/syntax/markdown.xml` | 同左，文件名 `markdown.xml` | `bf32150db4d97fbf539bcec4b6957d04c5afc2af148193e92db72223fb82c6fb` | `license="GPL,BSD"`（头注释：2008 GPL/BSD 双许可；2019 改动 MIT） |
| `data/syntax/tcl.xml` | 同左，文件名 `tcl.xml` | `1a0e538646e3491406d2d895c5be5c6e8ce3e34ca2bcb897670c9c11bb26a30a` | `license="BSD"`（Debian 版权整理记 BSD-3-Clause） |

### 2.2 为什么不是原样拷贝

- `vhdl.xml` **文件内**没有许可声明（连 `license=""` 都没有），但它**不是"无许可"**：本目录
  `raw/debian-kf6-copyright.txt` 的 Debian 版权整理把 `data/syntax/vhdl.xml` 列在 **MIT** 段
  （`:136`），上游 README 也说贡献按 MIT。选择自研的理由是：文件内没有可随拷贝保留的许可文本
  （无法在文件里如实登记来源），加上本仓库"文件内无声明即不随包"的取证纪律（`THIRD_PARTY_NOTICES.md`
  §9.1）；**不是**因为它不可用。
- `markdown.xml` 是 GPL/BSD 双许可，BSD **变体未指明**，按哪个 BSD 登记都是解释成本。
- `tcl.xml` 的语言名是 **`Tcl/Tk`**，而验收要求定义名 **`Tcl`**；原样拷贝必然要改文件（一旦改动就不是
  "上游原件"，登记方式同样要改）。
- 因此三份自研；文件名刻意不同（`vedit-*`），哈希/文件名扫描不会与禁运文件混淆。
- **改编即按上游许可**：若将来有人把某一份改成"改编自上游 X"，该份必须按上游许可登记
  （`vhdl.xml` = MIT、`markdown.xml` = GPL/BSD、`tcl.xml` = BSD），不得沿用本节的自研 LGPL 结论。

原始核对输出：`01-provenance-license.txt`（逐文件 grep 许可属性 + Debian `debian/copyright` 摘录 +
上游 README 的 licensing 段 + `LICENSE` 列表）；原件：`raw/`。

## 3. 验证（跑过的命令与观察结果）

| # | 检查 | 结果 | 日志 |
|---|---|---|---|
| 1 | `vedit-highlight-mode-selftest`（ctest + 直接跑，dev 模式） | **Passed**：81 checks / 0 failures（含新增的折叠与扩展名漂移断言） | `logs/10-final-green.log`、`a3-repair/05-r4e-vhdl-folding.log`、`a3-repair/06-r4d-extension-drift.log` |
| 2 | ctest 相关 6 个测试（含 `vedit-app-syntax-startup`） | **6/6 Passed（100%）** | `logs/10-final-green.log` |
| 2b | 历史：`vedit-app-syntax-startup` SEGFAULT | 修复前 3/3 SEGFAULT；另一 lane 修复后本次已绿；跨 lane 说明与绿跑引用见 `a3-repair/00-crosslane.txt` | `logs/13`、`logs/14`（历史） |
| 2c | 历史：并发写树下的 `vedit-edit-core-selftest` BAD_COMMAND | 单跑 Passed；本次 ctest 6/6 已包含它 | `logs/11-edit-core-rerun.log`（历史） |
| 3 | `vedit_syntax_kde_selftest`（5 份定义都在私有目录） | `forbidden-none` PASS；`private_definition_files` = 5 份；`data.no-forbidden-kde-definitions` PASS | `logs/10-final-green.log` |
| 4 | **AppDir + 交付 F2 库 + AppRun XDG 布局**（offscreen） | **104 checks / 0 failures**，每条 `definition-parsed.*` 的文件都在 AppDir 落点 | `logs/09-appdir-checks-full.log` |
| 5 | 历史：`verify-f2-isolation.sh`（repair 前） | 先 3 pass/2 fail，后 6 pass/0 fail，但“无重复名字”判据不可能失败 | `logs/05`、`06`、`12`（历史） |
| 6 | `verify-f2-isolation.sh`（repair 后） | **11 pass / 0 fail**：宿主反控 `defs=406`、用户级反控 `defs=6`、同名遮蔽负控“宿主 Tcl v999 赢且被报告” | `a3-repair/11-f2-isolation.log` |
| 6b | 门的负控：把 `/shadow` 纳入正控探针 | 同一“获胜文件”断言 **FAIL**：`Tcl=/shadow/…`，`summary pass=9 fail=2`，exit 1 | `a3-repair/12-r2-gate-fails-on-shadow.log` |
| 7 | AppDir 组装（最终源码） | `whitelist: Markdown,SystemVerilog,Tcl,VHDL,Verilog`，5 份 REQUIRED_SYNTAX 全在，`ASSEMBLE_EXIT=0` | `a3-repair/08-assemble-appdir.log` |
| 8 | R1：AppRun 的 `ksh_guard` 语言环境无关 | `LC_ALL=C` / `zh_CN.UTF-8` / `en_US.UTF-8` 均 `KSH-GUARD PASS`（修复前 zh/en 是 exit 2）；错误白名单仍 `FAIL` exit 2 | `a3-repair/01-r1-before.txt`、`a3-repair/02-r1-after.txt`、`a3-repair/03-r1-guard-fails-closed.txt` |
| 9 | 许可/不随包闸门（同一 AppDir） | `pass=8 fail=0`、`DELIVERY_BLOCKED=no` | `a3-repair/13-verify-not-shipped.log` |

要点：

- **表优先**由两条断言证明：①仓库对 `timing.sdc` 返回 `ProbeLanguage`（decoy 定义认领了 `*.sdc`），
  产品仍返回 `Tcl`；②`.v` 在宿主有 `V`/vlang 定义时仍返回 `Verilog`。
- **构造级高亮**（不是"名字能解析"）：`AbstractHighlighter` 捕获每条 format range，断言具体构造的
  **format 名**——VHDL `entity/architecture/is/end`→Keyword、`--`→Comment、`'1'`→String、
  `std_logic_vector`→Data Type；Markdown `# …`→Heading、`- `→List Marker、`` `…` ``→Code、
  `[..](..)`→Link、**围栏块内 `set period 10`→Code Block（不是普通文本）**；Tcl `proc/set/if/foreach`
  →Keyword、`#`→Comment、`$period`→Variable、`[`→Operator 且块内 `expr`→Keyword、双引号→String；
  SDC `create_clock`/`set_input_delay`→SDC Command、`[`→Operator。
- **AppDir 断言**用的是交付形态：`--appdir <AppDir>` 内部按 `AppRun` 的做法设
  `XDG_DATA_DIRS=XDG_DATA_HOME=<AppDir>/usr/share`、`VEDIT_SYNTAX_DIR=<AppDir>/usr/share/org.kde.syntax-highlighting`，
  进程再经 `LD_LIBRARY_PATH` 加载 **AppDir 自己的 `libKF6SyntaxHighlighting.so.6`**（自建 F2 库，不内嵌
  任何 KDE 定义），并断言每份解析结果的文件路径以 AppDir 开头。

## 4. 没验证的 / 已知边界（诚实声明）

**4.0 越界失败（不是本行的改动，但必须报告）**：`vedit-app --selfcheck`（ctest `vedit-app-syntax-startup`）
在最后阶段 **SEGFAULT / `APP-SHELL-SELFTEST ok=0 failures=2`**。同一测试在 `logs/09`（17:48）还是 Passed；
之后 `src/app/app_shell.cpp`、`src/app/ux_selftest.cpp`、`src/workspace/settings/*`、`src/app/main.cpp`
等文件被并行 lane 连续改写（mtime 01:38–01:55，见 `git status`）。本行最后改动的
`src/editor/kernel/highlight_mode_selftest.cpp` 只被 ctest 二进制引用，`highlight_mode.cpp` 的改动在
17:48 那次 Passed 时已经在树里；`--selfcheck` 输出里本行的检查全部 PASS
（`editcore.highlight.*`、`fold.native.highlight-mode`）。**结论：该失败属于并行改动，本行未改这些文件。**

**后续（repair 轮，2026-11-05）**：该崩溃已被另一 lane 定位并修复（`src/app/editor_area.cpp` 里
editor-provider 切换时的悬垂 `editor_` 指针）。重建后的绿跑是
`APP-SHELL-SELFTEST ok=1 checks=312 failures=0` + ctest `vedit-app-syntax-startup Passed`，见
`a3-repair/00-crosslane.txt`、`a3-repair/10-selfcheck-appdir.log`、`logs/10-final-green.log`。
旧的 `logs/13`、`logs/14` 保留为历史（已在 `logs/00-LOG-INDEX.md` 标 SUPERSEDED）。


1. **真实 GUI 观感未验**：没有 Xvfb/xcb 截图；offscreen 断言只证明隐藏引擎应用了期望的 format 范围，
   不证明配色好看或在真实屏上正确（用户验收项）。
2. **开发机上我们的同名定义会被内嵌 KDE 定义遮蔽**（AGENT.md §9）：ctest 用发行版
   `libKF6SyntaxHighlighting`（内嵌 450+ 定义），同名时内嵌者赢，所以 ctest 的构造断言用
   "只改语言名、规则字节不变"的 probe 副本（`VHDLProbe`/`MarkdownProbe`/`TclProbe`）；**交付形态**
   （F2 库）没有这个问题，AppDir 断言用真实名字并断言文件落点。
3. **`ksyntax_data.cpp` 的"禁运误伤"分支仍未被执行（本行主动披露，不是遗漏）**：那条分支只在
   "随包的 `vedit-*` 文件哈希等于被禁运的 KDE 文件"时触发，而仓库里不允许存在该上游文件
   （无法在不违反许可的前提下做负控）。因此**"三份新定义不会被误拒"这一结论只由正向断言支撑**：
   禁运表恒为 2 项、5 份随包文件全部 `isForbiddenDefinitionFile()==false`、加载报告
   `forbidden-none`（日志 #4）；`embargo_misfire` 分支本身没有负控，本文不声称它被执行过。
4. **VHDL 折叠**：开区规则覆盖 `entity`/`architecture`/`package`/`configuration`/`component`，
   以及可带标签的 `process`/`block`/`function`/`procedure`（`p1: process (clk) is` 已修并有断言；
   修复前是 begins=0/ends=1 的不平衡）。`generate`/`record` **不**开区（`resources/syntax/README.md`
   原先误写，已改）。真实 GUI 点 gutter 的双向折叠仍未在本行验证（没有 xcb 截图）。
5. **Tcl `{}` 不当作字符串**：`proc … { … }` 的 body 里命令仍按关键字/变量着色（这是刻意的：
   早先把 `{}` 当字符串会把整个 proc body 涂成文本，构造断言因此失败并已修）。
6. **`docs/handover/04-hard-facts.md:36` 的 "defs=2"**：A3 评审时不在本行作用域；repair 轮的
   `docs/handover/**` 已获授权，已加注 A3 之后的 5 份白名单与 `defs=406`（保留旧值作历史记录）。

## 5. 复现

```sh
# 镜像
IMG=vedit-qt6-build:local

# 1) ctest（在镜像里跑，工作区挂到 /vedit-build）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 "$IMG" bash -lc '
  cmake --build target/qt6-build --target vedit-highlight-mode-selftest --parallel 8 &&
  ctest --test-dir target/qt6-build -R "vedit-highlight-mode-selftest|vedit-syntax-kde-data|vedit-syntax-directory-equivalence|vedit-edit-core-selftest"'

# 2) AppDir（含 5 份定义）+ 交付 F2 库的 offscreen 断言
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 "$IMG" bash -lc '
  bash packaging/runtime/assemble-appdir.sh --out target/a3-appdir-repair/AppDir &&
  mkdir -p target/a3-appdir/libonly &&
  ln -sf "$PWD/target/a3-appdir-repair/AppDir/lib/libKF6SyntaxHighlighting.so.6" target/a3-appdir-repair/libonly/ &&
  LD_LIBRARY_PATH="$PWD/target/a3-appdir-repair/libonly" QT_QPA_PLATFORM=offscreen \
    target/qt6-build/src/app/editor-lane/vedit-highlight-mode-selftest --appdir "$PWD/target/a3-appdir-repair/AppDir"'

# 3) F2 隔离（正控 defs 由白名单条目数推导）
bash packaging/runtime/verify-f2-isolation.sh --appdir "$PWD/target/a3-appdir-repair/AppDir"
```

## 6. 文件清单

- `logs/01-build-and-ctest.log` … `logs/10-final-green.log`：构建、ctest、组装、AppDir 断言、F2 隔离修前/修后。
- `raw/`：三份上游参照原件 + Debian 版权 + 上游 README（只读证据，不随包）。
- `01-provenance-license.txt`：来源/许可/sha256 的逐文件核对输出。
