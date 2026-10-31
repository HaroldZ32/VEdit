# R2 追问：KSyntaxHighlighting 把语法定义编进 `.so` —— 事实、开关、法律含意与对 D2=B 的影响

- 任务：`T56`（cell `cell-20261031T014507403Z`）· 触发：T7 探针实测（`evidence/r2-c17/`）
- 席位：Deep Worker · 取证时间 2026-10-31（Asia/Shanghai）
- 复现：`bash evidence/r2-ksh-embedded/run.sh`（正/负控各一，日志 `scan.log`）
- 边界：只读上游 + 只读本机已安装库；**未**改生产代码/构建/打包/门禁阈值；法律部分**不下结论**

---

## 0. 结论（BLUF）

1. **T7 的发现是真的，而且比他描述的更广。** Ubuntu 的 `libkf6syntaxhighlighting6 6.24.0-0ubuntu1` 里，`libKF6SyntaxHighlighting.so.6.24.0` **确实内嵌了语法定义**：450 个 `*.xml` 资源条目、30 个 `.theme`；本报告解压出 **387 个定义**（下界），其中 **36 个是严格 GPL 系**（含 `SystemVerilog = GPLv3+`、`Vera = GPLv3+`、`Kconfig = GPLv3`、`Markdown = GPL,BSD`），另有 112 个 LGPL、64 个未声明、175 个其它许可。
2. **ADR §5 现有的文件级 sha256 断言（A/B）不足以覆盖这一点。** 它们只能证明「包内没有那份 XML 文件」，不能证明「声称 GPL 的内容没有随 `.so` 分销」。这是与 T39 的 M1 同性质的覆盖缺口，后果更重。
3. **上游有开关**：`-DQRC_SYNTAX=OFF`（默认 **ON**）会把定义改为**安装成文件**而不是编进库；KDE 源码注释**自己写明**这个开关就是为了「differently licensed」的 XML。另有 `-DNO_STANDARD_PATHS=ON` 可关掉 QStandardPaths 查找。
4. **方案 B 的「选择策略」仍然成立，但它的「打包前提」不成立。** 自有 LGPL 定义只解决「**选中谁**」，不解决「**分销了什么**」——只要随包的是发行版构建的 `.so`，那 36 个 GPL 定义照样被分销。B 要成立，必须配 **B′ = B + 用 `QRC_SYNTAX=OFF` 重新构建 KSyntaxHighlighting**（或接受分销 GPL 内容——那是**用户/法务决策**，不是我能改的）。
5. **方案 D（自写定义）单独不能解决**这个问题：它换掉的只是 `.sv` 定义来源，`.so` 里的 GPL 内容一字不少。**D 与 `.so` 构建选项是正交的两件事。**
6. 法律判断**未做**：报告给出两种解读及各自动作，全部标 **需法务确认**（§3）。

---

## 1. Q1 — 事实：到底把哪些定义编进去了？

### 1.1 版本与来源（可复现）

| 项 | 值 |
|---|---|
| 被检库 | `/usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0`（软链自 `.so.6`） |
| 大小 / sha256 | **2,010,872 B** / `da9d5f1c1640059b2809ddbb5c22e855479f9214005821abb3aa25c38a62f5e5` |
| 所属包 | `libkf6syntaxhighlighting6:amd64 6.24.0-0ubuntu1`（`dpkg -S`） |
| 上游 tag | `v6.24.0`（annotated tag `3d9810494eee77edd70ee84394348d14aa0455d9`）→ commit **`bbea2326fbd4fc720518efc43f1b018f8a7d0670`** |
| 对照（负控） | `/usr/lib/x86_64-linux-gnu/libKF6TextEditor.so.6.24.0`，4,127,608 B，sha256 `85d3cd4a2a19e308ac67b6e12360fd78db0f490c54bb0d13374a25495d6d0ddc` |

### 1.2 机制（源码级，行号来自 v6.24.0）

1. **定义集合 = 全部源 XML + 生成的变体**：`data/CMakeLists.txt:100`
   `file(GLOB src_defs "${CMAKE_CURRENT_SOURCE_DIR}/syntax/*.xml")` —— **不是白名单**。
   另有生成目标：`php-{html,css,javascript,javascript-react,typescript,mustache}`、`twig-{html,css,javascript,javascript-react,typescript}`、`doxygenlua`、`xslt`、`jsonc`、`jinja-*`。
2. **默认编进库**：顶层 `CMakeLists.txt:66`
   `option(QRC_SYNTAX "Bundle the syntax definition files inside the library as resources" ON)`
   顶层 `CMakeLists.txt:81-83`：`if (QRC_SYNTAX) add_definitions(-DHAS_SYNTAX_RESOURCE)`。
   `data/CMakeLists.txt:144-147`：`if (QRC_SYNTAX)` → `Qt6::rcc --name syntax_data -o qrc_syntax-data.cpp` → 作为 `SyntaxHighlightingData` 目标源编进库。
3. **定义先被压缩再嵌入**：`data/CMakeLists.txt:110/117/126-136` —— `katehighlightingindexer` 把 XML 压成 `generated/compressed-syntax/*`，qrc 再 `alias` 到这些压缩文件。**所以 `.so` 里看不到明文 XML**，只能按 zstd 帧解压（实测 418 个 zstd 帧）。
4. **主题无条件嵌入**：`data/CMakeLists.txt:107` 直接 `target_sources(... themes/theme-data.qrc)`；`data/themes/theme-data.qrc` 列出 **30** 个 `.theme`，**没有**类似 `QRC_SYNTAX` 的开关。

### 1.3 实测（本机库，`scan.log` 原文）

```
resource names (utf-16le): xml=450 theme=30
zstd frames parsed as definitions: 387
license classes: {'GPL': 36, 'LGPL': 112, 'none': 64, 'other': 175}
strict-GPL definitions embedded: 36
  GPL  ... SystemVerilog | license='GPLv3+' | 25672 B | sha256=3f1ecc7316a4212e
  GPL  Vera            | license='GPLv3+' | 15239 B | sha256=727a1fb18909da7b
  GPL  Kconfig         | license='GPLv3'  | 66451 B | sha256=d57d32112d3ea399
  GPL  Markdown        | license='GPL,BSD'| 40057 B | sha256=eab886e5566c1e7d
  ...（共 36 条，完整清单见 scan.log）
INFO Verilog: license='' bytes=6759 sha256=67f7aaf11c277b20bd0dc075849e47feac3dabbe5e0111c71cf177496b99006b
```

- **SystemVerilog 定义确实在 `.so` 里**：解压后 25,672 B，`<language name="SystemVerilog" … license="GPLv3+">`，sha256 `3f1ecc73…`（解压后内容哈希，非上游 XML 原始字节，因经过 `katehighlightingindexer` 处理）。
- **Verilog 定义也在**，`license=""`（与上游 `verilog.xml` 的「未声明」一致）。
- 负数说明：**387 是下界**（有的 zstd 帧不是 XML 或解压失败），资源名表给出的 `*.xml` 条目是 **450**；T7 运行期 `Repository::definitions()` 报 **451**。三者差异见 §6 U12 —— **不影响结论**（结论只需要「至少有一个 GPL 定义被嵌入」，这里已经有 36 个）。

### 1.4 为什么 T7 的「外部文件数 = 0」与「embedded = 451」同时成立

Debian/Ubuntu 的 `libkf6syntaxhighlighting-data` **只装翻译与文档**：`dpkg -L libkf6syntaxhighlighting-data | grep -c syntax-bundled` = **0**，`find /usr/share -name 'verilog.xml'` = **0**。也就是说这套包的**定义唯一来源就是嵌入的 `.so` 资源** —— 外部文件确实是 0，但定义并未消失，它们在被检库内部。

---

## 2. Q2 — 开关：上游能不能排除内置定义？

**能，有官方开关；而且是同一条代码路径上的二选一。**

| 选项 | 默认 | 作用 | 位置 |
|---|---|---|---|
| **`QRC_SYNTAX`** | **ON** | ON = 把定义编成 Qt 资源进 `.so`；**OFF = 不编进库，改为安装成文件**到 `${KDE_INSTALL_DATADIR}/org.kde.syntax-highlighting/syntax-bundled/` | 顶层 `CMakeLists.txt:66`、`data/CMakeLists.txt:144-156` |
| `NO_STANDARD_PATHS` | OFF | 关掉对 `QStandardPaths`（含系统数据目录）中定义/主题的查找；配合 `Repository::addCustomSearchPath()` 可让加载集合完全由我们决定 | 顶层 `CMakeLists.txt:72`；`-DNO_STANDARD_PATHS` 定义于 `:86-88` |

**上游自己的理由（原文，顶层 `CMakeLists.txt:64`）**：

> `# allow to install the "differently" licensed syntax xml files instead of putting them in a QRC and link them in`

⇒ KDE 明确把「许可不同的定义」当作 `QRC_SYNTAX=OFF` 的正当用途。这不是我们发明的旁路。

**没有**的选项（如实说）：没有任何「只排除某几个定义文件」的 cmake 选项；`file(GLOB …)` 是全量。若要「编进库但剔除某几个」，只能改 `data/CMakeLists.txt`／替换 `syntax-data.qrc` 内容——**属于维护 fork，不建议**。干净的官方路径就是 `QRC_SYNTAX=OFF` + 自定义搜索路径 + 白名单数据。

**主题例外**：30 个主题仍然无条件嵌入（`data/CMakeLists.txt:107`），`QRC_SYNTAX` 管不到；主题许可需单独清点（§6 U11）。

---

## 3. Q3 — 法律含意：**需法务确认**（本节不下结论）

> 我不是律师，没有资格判定「随包 `.so` 是否构成 GPL-3.0-or-later 内容分销」以及「是否传染整个 AppImage」。以下只给两种解读的**风险与动作**，供法务/用户裁决。

### 解读 A：视为「聚合分发」（aggregate）

- 论点：语法 XML 是**数据**，不是代码；它们由 MIT 的库在运行时读取，二者是聚合而非组合作品。
- 后果：义务**只落在被 GPL 的组件上** —— 随包提供 GPLv3 全文 + 该组件的源码可得性（KDE 上游源码公开，可满足）；我们自己的 MIT 代码不受影响。
- 需要动作：`THIRD_PARTY_NOTICES` 增加 GPLv3 条（含 36 个定义清单或「随 KDE 6.24.0 分发」的说明）、随包 `GPL-3.0.txt`、给出上游源码 commit（`bbea2326…`）。

### 解读 B：视为「组合作品」（combined work）

- 论点：定义被编译进**同一个 `.so`**、由该库解析并驱动高亮，是库功能不可分割的一部分；且 `-DHAS_SYNTAX_RESOURCE` 表明构建期就把它当作库的一部分。
- 后果：该 `.so` 整体受 GPLv3；我们随包并链接它 ⇒ AppImage 整体要满足 GPLv3（源码可得、许可兼容）⇒ 与产品 MIT 叙事冲突，且几乎必然要求整个交付物 GPL。
- 需要动作：要么不随包该构建（即 §2 的 `QRC_SYNTAX=OFF`），要么接受整包 GPL 的后果（用户/商业决策）。

### 两种解读都成立的前提下，**唯一同时消除争议的动作**

用 `-DQRC_SYNTAX=OFF` 自行构建 KSyntaxHighlighting，让 `.so` 内**不含任何**语法定义；随包的定义只有我们白名单里的自有产物。这样：
- 解读 A 下：无需 GPL 组件通知（我们提供的自有 LGPL 定义照 LGPL 处理）；
- 解读 B 下：`.so` 不含 GPL 内容，传染论点失去事实基础。

**注**：这条不改变「谁被选中使用」，只改变「我们分销了什么」——法律上「分销」看的是**是否随包分发**，与最终用户是否启用无关。

---

## 4. Q4 — 对 D2 = B 的影响：选择策略成立，打包前提不成立

把两件事分开：

| 问题 | 现状（随包发行版 `.so`） | 用 `QRC_SYNTAX=OFF` 自建 `.so` |
|---|---|---|
| **使用**：`.sv` 由谁着色 | 可以只选我们的 LGPL 定义（`addCustomSearchPath` + 我们自己的 `systemverilog.xml`） | 同样可以，且**只剩**我们的定义可选 |
| **分销**：包里有什么 GPL 内容 | `.so` 内 36 个严格 GPL 定义（含 `GPLv3+` 的 SystemVerilog） | `.so` 内 **0** 个定义 |

结论：
1. **B 本身（自有 LGPL 定义用于 `.sv`）仍然成立且必要**——它保证「选中」的是干净的定义。
2. **但 B 不能单独证明交付物干净**：文件级断言会让「文件没随包」通过，而 GPL 内容已经在 `.so` 里。这正是本次风险的实质。
3. **B′ = B + `QRC_SYNTAX=OFF` 自建库** 是唯一能在工程侧闭合的路线；代价见 §5。
4. **方案 D（自写 SV 定义）不解决该问题**：`.so` 里的 36 个 GPL 定义与「`.sv` 定义从哪来」无关。若用户只想换定义来源、不动 `.so`，风险依旧。
5. 是否接受「随包发行版 `.so` + 法务确认解读 A」是**用户决策**，不是工程默认；本任务**没有**把 D2 改成 D，也没有替用户接受。

**B′ 的工程代价（估算，供排期）**：
- 用 `-DQRC_SYNTAX=OFF -DNO_STANDARD_PATHS=ON` 构建 KSyntaxHighlighting 6.24.0（依赖 Qt6 + ECM；构建本身约 5–10 分钟）→ 打包进私有运行时，**0.5–1 人日**含接线与验证；
- 风险：KTextEditor 需要同版本 KF6（ABI/符号一致）；自建库必须与 KTextEditor 的 `KF6SyntaxHighlighting` 版本匹配（同 6.24.0 即可）；
- 收益：`.so` 体积下降（去内嵌定义），且许可面变干净。

---

## 5. Q5 — 替代断言（可执行、可失败）

现状：ADR §5 的 A/B 是**文件级 sha256**断言 —— 对「`.so` 内嵌」**必然通过**，是覆盖缺口。

替代/补充断言（已在 `run.sh` 中实现并跑出正/负控）：

```bash
# G1（主闸门）：库里不得嵌入任何严格 GPL 定义；SystemVerilog 必须在清单外
python3 evidence/r2-ksh-embedded/scan-embedded-defs.py --expect-clean \
        "$PAYLOAD/lib/libKF6SyntaxHighlighting.so.6"
# 期望：exit 0；若库是发行版构建（内嵌 36 个 GPL）→ exit 1

# G2（快速预检，无 python 依赖）：资源名表里不得出现 systemverilog.xml
! strings -a -el "$PAYLOAD/lib/libKF6SyntaxHighlighting.so.6" | grep -qx 'systemverilog.xml'

# G3（构建来源）：记录自建库的 CMake 配置，证明用了 QRC_SYNTAX=OFF
grep -q 'QRC_SYNTAX:BOOL=OFF' "$BUILD_DIR/CMakeCache.txt"   # 构建期证据，进 m0 证据目录
```

**为什么不是永真断言（实测正/负控，`scan.log`）**：

| 控制 | 对象 | 结果 | 说明 |
|---|---|---|---|
| 正控 | 发行版 6.24.0 库 | `--expect-clean` **exit 1**；扫出 36 个 GPL 定义、SystemVerilog=`GPLv3+` | 闸门在真实问题上会亮红 |
| 负控 | `libKF6TextEditor.so.6.24.0`（无语法数据） | 0 名字 / 0 定义，`--expect-clean` **exit 0** | 闸门不会在干净库上误报 |

**断言依赖**：`python3` + `zstandard`（本机 0.25.0；`zstd` CLI 1.5.7 亦可辅助验证）。G2 只需 `strings`（binutils）。

**建议的落点**：G1/G2 进 `packaging/final/` 的打包断言（pack-final 执行）；G3 进 m0-runtime 的构建证据。**本任务不实施**（边界：不动构建/打包）。

---

## 6. 未闭合项

| # | 项 | 影响 | 归属 |
|---|---|---|---|
| U10 | **用户决策**：B′（自建 `QRC_SYNTAX=OFF` 的 `.so`）还是接受「随包发行版 `.so` + 法务确认」 | 决定后续所有 `.sv` 与许可工作 | 用户 / captain / 法务 |
| U11 | 主题（30 个 `.theme`）**无条件嵌入**，本次未清点其许可（`QRC_SYNTAX` 管不到） | 若主题含 GPL，问题同构且独立于定义 | 后续任务（数据许可清点） |
| U12 | 计数不一致：资源名 450、可解压定义 387、T7 运行期 451 | 只影响「到底多少个」的表述，不影响「有 GPL 内容」的结论 | 本任务已标注，可留待复核 |
| U13 | 未验证「自建 `QRC_SYNTAX=OFF` 的 6.24.0 库」与 KTextEditor 6.24.0 的运行时兼容（ABI/符号） | B′ 的可行性 | m0-runtime（若选 B′） |
| U14 | 未判定 `NO_STANDARD_PATHS=ON` 与 `addCustomSearchPath()` 组合下的主题加载行为（主题始终内嵌，是否仍需外部主题） | 私有路径加载方案细节 | syn-kde-data + m0 |
| U15 | 法律解读 A/B 的选择与「聚合 vs 组合」定性 | 决定通知义务与整包许可 | **法务**（需法务确认） |

---

## 7. 复现

```bash
# 正控（发行版库，应报 36 个 GPL 定义、SystemVerilog=GPLv3+）+ 负控（KTextEditor 库，应 0）
bash evidence/r2-ksh-embedded/run.sh
# 输出：evidence/r2-ksh-embedded/scan.log（正控 expect-clean=1、负控 expect-clean=0、controls PASSED）

# 单库检查 / 单定义查询
python3 evidence/r2-ksh-embedded/scan-embedded-defs.py <libKF6SyntaxHighlighting.so>
python3 evidence/r2-ksh-embedded/scan-embedded-defs.py --name SystemVerilog <lib.so>
python3 evidence/r2-ksh-embedded/scan-embedded-defs.py --expect-clean <lib.so>   # 闸门语义：有严格 GPL 定义即 exit 1
```
