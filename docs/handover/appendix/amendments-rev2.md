# 契约补充 AMENDMENTS（rev 2）— `.sv` 关闭态折叠定案、许可归属更正、ADR 命名冲突修正、折叠语法等价性

> **效力声明**：本文件由 cell `cell-20261031T085543161Z` 的 board task `T33`
> （`contracts/docs-amendments.md`，`loop-20261031T095153-b31990`）产出，由此 cell 的
> captain 调度、由 Senior Engineer 执笔。**与 split revision 2 的冻结契约具有同等效力**。
> 它**只记录事实与判据等价性，不修改冻结契约正文、不放宽任何判据、不修改测试或门禁**；
> 与冻结契约正文冲突时，**以更严格的一条为准**。本文件**不升** `requirements.md` 的
> revision（不制造 STALE）。
>
> **本 rev 2 不是 A4.4 的 (b) 回退**：A4.4 规定只有"用户明确否决 (a)"才发布 (b) 版 rev 2；
> 实际情况相反（用户已选 (a)，见 §1）。本 rev 2 的作用是**把 (a) 定案、关闭 (b)、更正
> 许可归属与 ADR 命名冲突、并如实记录折叠数据形态的等价性与当前交付缺陷**。
>
> **基线快照（写入本文件时磁盘上的版本，sha256 见 `evidence/docs-amendments/00-context.txt`）**：

| 被补充/更正的文档 | Board task | sha256（基线） |
|---|---|---|
| `.mpd/cellwork/cell-20261031T085543161Z/requirements.md`（C14/C15 权威文本） | `T33` 输入 | `4a01064cdfeaac8b47c84957bc70d53b85ced0d23c854f390b0ea76ce19675a5` |
| `docs/handover/appendix/amendments-a1-a5.md`（rev 1，A4.1/A4.2/A4.4/A4.5） | `T33` 输入 | `ca5fbf108fd9f42cbdaa8cce183942db1e100db3a2b05dd6bb89c616b0738954` |
| `docs/decisions/r2-license.md`（ADR；§5 A/C 命名冲突） | `T33` 输入 | `425da67f1c6a6d647eae7b04a680734b59b54d213e164de039c3f9f0992ad141` |
| `THIRD_PARTY_NOTICES.md`（许可汇总；本次新增 §9） | `T33` 写域 | `aa8fba8d73c9507155bc937c6648a12a9296c9fe7a1671a003abd220c32da5e9` |
| `docs/handover/README.md`（文档地图；本次登记本文件） | `T33` 写域 | `879bc5451929cc7c67665185f5c586103fd27224dd633836fd790e9279a15cfb` |

> **证据总目录**：`evidence/docs-amendments/`。本文件引用的每条事实都可在其中复跑：
> `00-context.txt`（时间/HEAD/树状态/基线哈希）、`01-file-list.txt`（文件清单）、
> `02-sha256-check.txt`（C14 哈希核对）、`03-ksh-validator-rerun.log`（FoldingProbe 复跑）、
> `04-folding-form-probe.log`（折叠形态正/负控，含本文件 §4 的关键发现）、
> `05-builtin-kde-folding.txt`（KDE 内嵌定义的折叠写法）、`06-gates.txt`（门禁与范围检查）。

---

## 1. ① `.sv` 关闭态折叠的最终决定与理由

**决定（最终，不再作为用户决策项）**：`.sv` 在 tree-sitter 关闭态的折叠来源固定为
**我们自有的 LGPL-2.1-or-later `resources/syntax/vedit-systemverilog.xml`**；A4.1/A4.2
继续有效，A4.4 的 **(b) 路径关闭**（用户未否决 (a)，无需再选）。

**理由（逐条可查）**：

1. **用户已定案 (a)**：需求书 `requirements.md:99` 的决策记录写明"`.sv` 关闭态折叠不再作为
   用户决策"，依据是 ① A4.1 已定案采用 (a)；② 自有 LGPL XML 已交付；③ 其余只是证据/登记/文档。
2. **数据已交付，路径与哈希可核对**（C14；哈希为 T45 重写折叠规则后的当前值）：
   `resources/syntax/vedit-systemverilog.xml` 90 行 / 5,492 B / sha256
   `2abba3342d0878e71b99d358d96e510f857cd7f232786951be8602f513e0b992`
   （Verilog 版 85 行 / 5,111 B / `c6adc763c7b280d7493f48d06e5779d0b1e2068ac82f75fe7f31a07b0788a6c2`）。
   变更记录（2026-10-31）：T45 重写折叠规则后哈希变更；旧值 89 / 6,276 B `dc6e0a47…`、
   78 / 5,172 B `3d1ec41f…` 为重写前的历史值（旧证据与旧打包快照仍保留该值，不改写）。
   实算/核对输出见 `evidence/licensing-hash-sync/01-hash-recompute.txt`。
3. **许可干净**：两份 XML 为自有构建产物（XML 头 `author="VEdit"`、
   `license="LGPL-2.1-or-later"`），不随包 KDE 的 `verilog.xml`（许可未声明）与
   `systemverilog.xml`（GPL-3.0-or-later）；禁运由 sha256 + 文件名 + 内嵌扫描三重执行
   （`packaging/runtime/verify-not-shipped.sh:27-28,43-57,89-116`、
   `src/syntax/kde/ksyntax_data.cpp:21-23,113-134`）。
4. **回退 (b) 的代价与收益都不再成立**：(b) 会让 `.sv` 关闭态无折叠、并改写 A1f 期望；
   现在数据与验收路径都已存在，(b) 只会制造倒退。因此 A4.4 的 (b) 条件永不触发。

**执行状态（必须与"决定"分开看）**：决定成立。**折叠规则已由 T45 重写，并由 T66 在真 F2
交付库上重录**：两份 XML 改为 KDE 关键字列表惯用法后，在自建 `QRC_SYNTAX=OFF` 的 F2 库上，
探针断言 `author=VEdit / section=Hardware`，且 **`ldd` 证明 `libKF6SyntaxHighlighting` 解析到
F2 安装树**（不再出现 "F2 标签 + DISTRO 库"）。实测：**Verilog 7/7、SystemVerilog 11/11**
（SV 用 26 行真样例；每行 `library=F2` 前都有 `LDD library=F2 resolves=<F2 path>`）。
证据：`evidence/syn-kde-data/10-folding-before-F2.log`、`11-folding-after-F2.log`。
首版 `10`/`11` 日志已被证明测的是 DISTRO 库、且 SV 采样分支从未生效，文件保留并加
`# INVALID (T66)` 横幅，以 `-F2` 版为准。
**A4.5 仍未满足**：它的判据是视图级观测（§4.8 门槛 3），单元级 `applyFolding` 计数只能当前置正控，
视图级证据仍需按 §4.7/§4.8 重采（U-2）；这**不是**对 A4(a) 决定的变更，也**不**重开用户决策。

> **交付风险声明（T66 更正）**：T45 之前的 XML 在 F2 交付构建（自建 `QRC_SYNTAX=OFF` 库、
> 无内嵌定义）下确实给出 **0 折叠区**；T45 修复后，T66 在**真 F2 库**上重测为
> **Verilog 7/7、SystemVerilog 11/11**（SV 26 行样例；`ldd` 证明库指向 F2）。开发镜像里
> 的旧数字若来自 KDE 内嵌定义（§4.4），一律不作自有 XML 的证据。

---

## 2. ② `THIRD_PARTY_NOTICES.md` 登记与归属更正

`THIRD_PARTY_NOTICES.md` 已新增 **§9「Qt6 产品线：自有基础语法定义与 tree-sitter grammar」**，
按 C11/C14 登记以下三项；本节是同一事实的权威副本（两处必须一致）。

### 2.1 自有 LGPL-2.1-or-later 定义（两份）

| 项 | `vedit-verilog.xml` | `vedit-systemverilog.xml` |
|---|---|---|
| 语言名 | `Verilog` | `SystemVerilog` |
| 许可 | **LGPL-2.1-or-later** | **LGPL-2.1-or-later** |
| 仓库路径 | `resources/syntax/vedit-verilog.xml` | `resources/syntax/vedit-systemverilog.xml` |
| 行数 / 字节 | 85 / 5,111 B | 90 / 5,492 B |
| **sha256（C14；T45 重写后）** | `c6adc763c7b280d7493f48d06e5779d0b1e2068ac82f75fe7f31a07b0788a6c2` | `2abba3342d0878e71b99d358d96e510f857cd7f232786951be8602f513e0b992` |
| 安装落点（AppDir） | `usr/share/org.kde.syntax-highlighting/syntax/vedit-verilog.xml` | `usr/share/org.kde.syntax-highlighting/syntax/vedit-systemverilog.xml` |
| 安装后 sha256 | 与仓库一致（`runtime-manifest:196`） | 与仓库一致（`runtime-manifest:195`） |
| 许可全文 | `docs/licenses/LGPL-2.1.txt`、`resources/syntax/LICENSE-LGPL-2.1`（26,419 B） | 同左 |
| 上游功能参照（非复制） | GtkSourceView 4.6.1 `verilog.lang`，475 行，sha256 `7670ddfd7b443f5f99c202bfa79de31834c1bd8968ed74c39474dca4f439d464` | GtkSourceView 4.6.1 `systemverilog.lang`，262 行，sha256 `9d1f67411f2674d93f035620ce004da4beca5bbf469eb9955a83c332edb79b04` |
| 上游 tarball pin | `GSV_URL` / `GSV_SHA256=63f487a7ab59612ffa9e7be1883dbd274ab1016afcede3239b93b0fc384df874`（`scripts/versions.env:24-26`） | 同左 |

> **哈希变更记录（2026-10-31，T45）**：T45 把两份 XML 的折叠规则改写为 KDE 关键字列表惯用法后，
> 行数 / 字节 / sha256 变为上表值；旧的 78 / 5,172 B `3d1ec41f…`、89 / 6,276 B `dc6e0a47…` 是
> **T45 重写前的历史值**，只保留在旧证据与旧打包快照里（`evidence/highlight-v2/10-licensing-and-embargo.txt`、
> `evidence/m0-runtime/runtime-manifest.txt`、`evidence/docs-amendments/02-sha256-check.txt`），不改写。
> 许可结论不变：两份均为自有 LGPL-2.1-or-later 构建产物。

**归属更正（三句话，防止再次误归属）**：

1. 这两份 XML 是**本仓库自有的构建产物**，不是 GtkSourceView 的运行期文件、也不是 KDE 定义。
   GtkSourceView 的 `.lang` 只作**功能参照与交叉核对**（见 `resources/syntax/README.md`）；
   上游作者/版本归属见上表；LGPL 义务（声明 + 源码可得 = XML 本身）由仓库内文件直接满足。
2. 它们**不得**登记在"第三方 GTK/GtkSourceView 运行期组件"条目下，**不得**归因给 KDE；
   KDE 的 `verilog.xml`（许可未声明，sha256 `7199e367…`）与 `systemverilog.xml`
   （GPL-3.0-or-later，sha256 `3361d8b4…`）**永不随包**。
3. **C14 的身份核对（T45 重写后重算）**：当前两份源文件的路径/行数/字节数/sha256 与 §2 表一致
   （实算见 `evidence/licensing-hash-sync/01-hash-recompute.txt`）；`02-sha256-check.txt` 与
   `evidence/m0-runtime/runtime-manifest.txt:195-196` 记录的是重写前/旧打包快照的**历史值**，
   不改写；安装点本身仍与 `packaging/runtime/assemble-appdir.sh:109-112`、`packaging/runtime/AppRun:33` 一致。

### 2.2 MIT `tree-sitter-verilog`（随包静态编入）

| 项 | 值 |
|---|---|
| 上游仓库 / commit | `https://github.com/tree-sitter/tree-sitter-verilog` / **`4457145e795b363f072463e697dfe2f6973c9a52`**（2022-09-07） |
| 许可 | **MIT**，© 2018-2021 Aliaksei Chapyzhenka；`LICENSE` 全文随包（`third_party/tree-sitter/verilog/LICENSE`，1,082 B，sha256 `92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46`） |
| 仓库路径 | `third_party/tree-sitter/verilog/`（provenance：`third_party/tree-sitter/PROVENANCE.md`） |
| 随包文件与 sha256 | `grammar.js` 133,684 B `323c121a3e550df0f970d46c544b3f02485e5e6d754acb730d6be7b8dea7dc4b`；`src/parser.c` 46,042,888 B `a15f78b82068daeb62f3b54c8ad0101aa936a2c921a57806fe8f37dcbf0bfff2`；`src/tree_sitter/parser.h` 5,378 B `ab104936984904469572a4e868149f7a22fb2929347f837ae6a1f9b790f1b173`；`src/node-types.json` 271,904 B `e99b42baf4279d3be6dec59129dda0789f196b4e5c99144a781ea6cdf255425c` |
| 分发形态 | 静态编入 `libvedit_syntax_grammars`（`verilog/src/parser.c`），无运行期下载/`.so`/动态链接；`-DVEDIT_GRAMMAR_*` 可整体关闭 |
| 义务 | MIT：保留版权行 + 许可全文 + commit 锁定（已满足）；无 NOTICE 段落需转发 |

### 2.3 许可全文与最终复核

- 全文：`docs/licenses/LGPL-2.1.txt`、`docs/licenses/Apache-2.0.txt`、
  `docs/licenses/GPL-2.0.txt`、`docs/licenses/slang-LICENSE.txt` 等；
  `THIRD_PARTY_NOTICES.md` §7 的表格仍是旧产品线的全文索引。
- 最终产物一致性（每份随包文件都能在同一 AppDir 内按 sha256 找到）由 `pack-final` 的产物侧
  清单复核；本文件只固定仓库内身份。

---

## 3. ③ r2-license ADR 产物命名 与 `verify-not-shipped.sh` 文件名黑名单的冲突与处理

### 3.1 冲突原文（事实）

| 出处 | 原文/行为 | 冲突点 |
|---|---|---|
| `docs/decisions/r2-license.md` §5 **A** | 断言 `$PAYLOAD/share/ksyntaxhighlighting/syntax/systemverilog.xml` 与 `.../verilog.xml` **不得存在** | 路径是旧假设（实际安装点不是这里），见 §3.2 |
| `docs/decisions/r2-license.md` §5 **C** | 断言 `$PAYLOAD/share/ksyntaxhighlighting/syntax/systemverilog.xml` **必须存在**，且含 `LGPL-2.1` 与 `GtkSourceView` | **与 §5 A 直接互斥**，也与黑名单互斥 |
| `packaging/runtime/verify-not-shipped.sh:53-57` | 任何 basename 为 `systemverilog.xml` / `verilog.xml` 的文件一律 FAIL（**文件名黑名单**） | 若按 §5 C 命名，门禁**必然 FAIL** |
| `packaging/runtime/verify-not-shipped.sh:27-28,43-51` | 按 KDE 两份的 **sha256** 禁运（内容级） | 必要，但不足以防"文件名占位"误用 |
| 实际交付命名（C14） | `vedit-verilog.xml` / `vedit-systemverilog.xml` | 通过文件名黑名单；与 §5 C 字面路径不符 |
| 实际安装（`assemble-appdir.sh:109-112` + `AppRun:33`） | `$APPDIR/usr/share/org.kde.syntax-highlighting/syntax/`（原样 basename） | **不是** `share/ksyntaxhighlighting/syntax/` |

### 3.2 处理办法（不改判据、不放松门禁）

1. **保留文件名黑名单**。它是防"把 KDE 文件改名带入"与防"用 KDE 名字占位"的第二道防线
   （sha256 只防内容相同）。**禁止**为让 §5 C 过而删除/放宽它。
2. **按判据意图等价改写 §5 C 的路径与文件名**（不改变判据语义）：
   - 旧：`$PAYLOAD/share/ksyntaxhighlighting/syntax/systemverilog.xml`
     → 新：`$PAYLOAD/usr/share/org.kde.syntax-highlighting/syntax/vedit-systemverilog.xml`
   - 检查项保持：文件存在、含 `LGPL-2.1`、含 `GtkSourceView` 归属；两份 XML 均满足
     （`grep` 实测见 `02-sha256-check.txt`）。
3. **§5 A 的路径同样更正为实际安装点**；并注明"真正执行这一禁运的是
   `verify-not-shipped.sh`（遍历实际 AppDir + 文件名检查），ADR §5 A 只是同义提示"。
4. **不把 §5 C 的旧字面当阻塞项**：verifier 看到旧路径时按本 §3.2 的等价改写判读；
   不得以"文件名不是 `systemverilog.xml`"判 FAIL；也不得删除文件名黑名单换绿。
5. **ADR 落地方式**：在 `docs/decisions/r2-license.md` 末尾追加 **§9 勘误（本 rev 2 / C15-3）**，
   只记录修正与判读口径，**不改写 §5 正文**（ADR 是决策记录，修正以勘误追加）。
6. **G1 不受影响**：内嵌定义扫描（`verify-not-shipped.sh:89-116`）仍是覆盖
   "库内嵌 GPL 定义"的主闸门；本次命名修正只动文件级检查的路径字面。

### 3.3 更正后的可执行断言（可直接抄给 `pack-final`）

```sh
sv="$PAYLOAD/usr/share/org.kde.syntax-highlighting/syntax/vedit-systemverilog.xml"
[ -e "$sv" ] || { echo "FAIL: own systemverilog.xml missing (D2=B requires it)"; exit 1; }
grep -q 'LGPL-2.1' "$sv"        || { echo "FAIL: $sv lacks the LGPL-2.1 declaration"; exit 1; }
grep -qi 'GtkSourceView' "$sv"  || { echo "FAIL: $sv lacks upstream attribution"; exit 1; }
# 文件名黑名单保持（与 verify-not-shipped.sh 一致，不得删除）：
if find "$PAYLOAD" -type f \( -name 'systemverilog.xml' -o -name 'verilog.xml' \) | grep -q .; then
  echo "FAIL: a file named systemverilog.xml / verilog.xml is present"; exit 1
fi
```

---

## 4. ④ `<folding begin= end=/>` 字面形态 vs `beginRegion`/`endRegion` 属性形态

### 4.1 A4.1 的字面原文

`amendments-a1-a5.md` A4.1 写：自有 XML "**必须包含 `<folding begin="…" end="…"/>` 区**"，
使 tree-sitter 关闭态下 `.sv` 仍有折叠。C14 已指出该**字面元素形态不存在**，等价性由本
rev 2 记录。下面把"事实"与"判据等价性"分开写。

### 4.2 事实 A — KSyntaxHighlighting 6.24.0 没有 `<folding>` 定义元素

- 两份自有 XML 中 `grep -c '<folding'` = **0**；它们只用 `<keyword … beginRegion=…/endRegion=…>`。
- 探针（`evidence/docs-amendments/probe/folding_form_probe.cpp`，用公开 API
  `Repository::addCustomSearchPath` + `AbstractHighlighter`）把同一文件改为字面
  `<folding begin="module" end="endmodule"/>`（**两处放置**：`<context>` 内与 `<highlighting>` 下），
  定义仍 `definition_valid=1`（未知元素被忽略），但 `begins=0 ends=0`：
  `CONTROL literal-element-form: formats=14 begins=0 ends=0`（`04-folding-form-probe.log`）。
- KDE 自己的定义也不用该元素：内置 `Verilog` 定义写的是
  `<keyword String="beginwords" beginRegion="Block"/>` + `<list name="beginwords">…</list>`
  （`05-builtin-kde-folding.txt`）。

**结论 A**：`<folding begin= end=/>` 不是 KSyntaxHighlighting 的可执行写法；把它当验收对象
会得到"定义有效但零折叠"。**不得**因字面元素缺失判 FAIL（与 C14 一致）。

### 4.3 事实 B — 属性形态本身可用，但**必须挂在"关键字列表引用"规则上**

`<keyword String=X>` 在 KSyntaxHighlighting 里的 X 是**关键字列表名**，不是字面关键字；
若定义里没有 `<list name="X">`，加载期记录 `Unknown keyword list "X"` 并**丢弃该规则**。
当前 `resources/syntax/vedit-verilog.xml` / `vedit-systemverilog.xml` 的折叠规则正是
`<keyword … String="module" beginRegion="…"/>` 这种"字面关键字"写法（14 + 23 条），因此：

- 在唯一语言名下加载我们的 Verilog 定义：`CONTROL attribute-form: formats=40 begins=0 ends=0`，
  伴随 **14 条** `Unknown keyword list` 警告；
- 我们的 SystemVerilog 定义：`CONTROL shipped-SV-attribute-form: … begins=0 ends=0`；
- 全文共 **37 条** `Unknown keyword list` 警告（`04-folding-form-probe.log`）。

**结论 B（T45 修复前的历史事实）**：当时交付的两份 XML 产出的折叠区为 0 条。这不是
"属性形态不成立"，而是规则写法错误（见事实 D 的修正对照）。**T45 已按事实 D 修复，
T66 在真 F2 库上重测为 Verilog 7/7、SystemVerilog 11/11（26 行 SV 样例）**。

### 4.4 事实 C — 既有 FoldingProbe 的 7/7、9/9 证据来自 KDE 内嵌定义，不是我们的 XML

`src/syntax/kde/tests/validate_syntax_data.cpp:55-86` 的 FoldingProbe 本身是**正确的观测器**：
它统计 `applyFolding()` 的 `FoldingRegion::Begin/End` 回调。问题在"被观测的定义是谁"：

| 查询 | 实际解析到的定义 | 折叠观测 |
|---|---|---|
| `definitionForName("Verilog")` | `author=Yevgen Voronenko (ysv22@drexel.edu), Ryan Dalzell (ryan@tullyroan.com)`，`license=`（未声明）→ **KDE 内置** | `begins=7 ends=7` |
| `definitionForName("SystemVerilog")` | `author=Sean O'Boyle (seanoboyle@intelligentdv.com)`，**`license=GPLv3+`** → **KDE 内置** | `begins=9 ends=9` |

这两组数字与 `evidence/syn-kde-data/03-folding.log` 的 `FOLD verilog formats=50 begins=7 ends=7`
和 `FOLD systemverilog formats=51 begins=9 ends=9` **完全一致**；而我们的两份 XML（唯一名字下）
是 0/0（事实 B）。因此：

- **`03-folding.log` 不能作为"自有 XML 已提供折叠"的证据**；它测到的是开发镜像里
  `libKF6SyntaxHighlighting` **内嵌的 KDE 定义**（`.sv` 那条是 **GPLv3+**）。
- **`03-folding.log` 也不得用于任何许可/合规叙事**：它证明不了自有 LGPL 定义被加载，
  反而证明 `.sv` 折叠数字来自内嵌的 GPLv3+ KDE 定义；许可证据必须来自自有文件
  （sha256/归属/加载来源），不能引用该日志。
- 在交付形态（F2 自建库、`QRC_SYNTAX=OFF`、无内置定义）下，内置定义不存在；
  T45 修复前我们的 XML 会按事实 B 产出 **0 折叠**，T45 修复后 T66 在真 F2 上实测
  Verilog 7/7、SystemVerilog 11/11（见 §1 执行状态与 `-F2` 日志）。

### 4.5 事实 D — 修正后的属性形态可用（正控）

把同一批关键字搬进 `<list>` 并用规则引用：

```xml
<list name="veditFoldBegin"><item>module</item><item>begin</item><item>case</item>…</list>
<list name="veditFoldEnd"><item>endmodule</item><item>end</item><item>endcase</item>…</list>
<keyword attribute="Keyword" context="#stay" String="veditFoldBegin" beginRegion="veditFold"/>
<keyword attribute="Keyword" context="#stay" String="veditFoldEnd" endRegion="veditFold"/>
```

探针实测：`CONTROL corrected-attribute-form: formats=40 begins=6 ends=6`（`04-folding-form-probe.log`）。
这就是 KDE 内置定义的同款 idiom（`05-builtin-kde-folding.txt`）。

### 4.6 等价性结论（判据层，不是 XML 字面层）

1. **A4.1 的字面 `<folding begin= end=/>` 与 `beginRegion`/`endRegion` 不等价**：前者不是
   KSyntaxHighlighting 的写法，实测产出 0 折叠区；不能作为验收对象。
2. **属性形态是 A4.1 *意图*的等价实现，前提是规则写成"关键字列表引用"**（事实 D 实测 6/6）。
   验收的观测对象是折叠区（`applyFolding` 的 Begin/End 计数；视图层按 A3.1 的三种观测），
   不是 XML 字面。因此 verifier 应把 A4.1/A4.2 读作"beginRegion/endRegion 规则 + 关键字列表"，
   **不得**因字面元素缺失判 FAIL。
3. **但等价性结论不能替代交付验收**：事实 B/C 表明当前交付的折叠规则未生效、旧证据归属错误，
   所以 **A4.5 仍未满足**（AC-S3.5 未闭合），且**不得**引用 `03-folding.log` 作为满足证据。
4. **本 rev 2 不改任何判据**：A4.5 的"1k 行 `.sv` + tree-sitter 关闭 + A3.1 观测 ≥1 折叠 +
   期望值来自自有 XML"原样保留；修的是数据与证据归属，不是判据。

**C14 表述的更正（逐字替换口径）**：C14 原文"`src/syntax/kde/tests/validate_syntax_data.cpp`
的 FoldingProbe 已证属性形态可产出折叠区"应读作：

> 属性形态**经对照组证明可产出折叠区**（关键字列表引用，实测 6/6）；**交付件**（字面关键字串
> `<keyword String="module" …>`）实测产出 **0** 折叠区，被 KSyntaxHighlighting 当"未知关键字
> 列表"丢弃；修复后必须在 **F2 交付库**上重测并以探针的 `author/section` 证明测到的是自有文件。

### 4.7 修复口径（给实现 lane；不在本任务写域）

1. 把 `resources/syntax/vedit-verilog.xml` 与 `vedit-systemverilog.xml` 的 14 + 23 条折叠规则
   改成 `<list>` + `<keyword String="列表名" beginRegion/endRegion>`（或改为可实测出折叠区的
   `<RegExpr … beginRegion/endRegion>`），保留现有 begin/end 配对覆盖
   （module/interface/program/class/function/task/generate/case/begin-end）。
2. 修复后必须在 **F2 形态**（自建 `QRC_SYNTAX=OFF` 库，无内置定义）上重跑：
   `definitionForName("Verilog"/"SystemVerilog")` 的 `author` 必须是 `VEdit`，
   且 FoldingProbe 的 `begins/ends` > 0。
3. 重采 A4.5（A3.1 三种观测之一 + 期望值来自自有 XML 的 `beginRegion` 规则），
   并更正 `evidence/syn-kde-data/06-conclusion.md` (c) 与 C14 中"FoldingProbe 已证"的表述
   （改为：属性形态经正控证明可用；当前交付数据待修复重采）。

### 4.8 可执行提示（后续任何折叠证据的硬门槛）

> **后续任何折叠/`.sv` 关闭态证据，必须同时满足以下三条，缺一无效：**
>
> 1. **来自 F2 交付库**：被测量的 `libKF6SyntaxHighlighting` 是自建 `QRC_SYNTAX=OFF` 库
>    （或等价的无内嵌定义产物）；开发镜像里带内嵌定义的库**不能**作为交付证据来源。
> 2. **探针证明测到的是自有文件**：`definitionForName("Verilog")` /
>    `definitionForName("SystemVerilog")` 的 `author` 必须是 **`VEdit`**（并记录 `section`、
>    文件 sha256）；出现 KDE 作者（Yevgen Voronenko / Ryan Dalzell / Sean O'Boyle 等）即证明
>    测的是内嵌定义，证据作废。
> 3. **观测口径不降级**：A4.5 仍须用 A3.1 的三种观测之一（`firstDisplayedLine(RealLine/VisibleLine)`
>    差值 / `scroll_max` 差值 / 截图 diff），期望值来自自有 XML 的 `beginRegion` 规则；
>    单元级 `applyFolding` 计数只能作为前置正控，不能单独充当 A4.5。

---

## 5. 给验证者的硬要求

1. 评审 `T33` 时，`mpd_verify_record.sources` **必须同时包含**：本文件、
   `.mpd/cellwork/cell-20261031T085543161Z/requirements.md`（C14/C15）、
   `docs/decisions/r2-license.md`、`docs/handover/appendix/amendments-a1-a5.md`。
   缺本文件视为证据不全，不得判 PASS。
2. **不得**因 A4.1 字面元素缺失判 FAIL；**不得**把 `evidence/syn-kde-data/03-folding.log`
   当作自有 XML 的折叠证据（§4.4）；**不得**删除/放宽 `verify-not-shipped.sh` 的文件名黑名单；
   **不得**改测试或门禁。
3. **A4.5 状态 = 未满足**（§1 执行状态、§4 事实 B/C）。任何"AC-S3.5 已完成"的结论都必须在
   修复后按 §4.7 重采证据；本 rev 2 不提供该完成的结论。
4. 本文件 §2/§3 的路径与 sha256 必须与 `THIRD_PARTY_NOTICES.md` §9、ADR §9 勘误三处一致；
   不一致以本文件（更严格）为准，并要求 writer 修正后重验。
5. 后续任何折叠证据按 §4.8 的三条硬门槛复核：**F2 交付库 + 探针 `author=VEdit` + A3.1 观测**；
   开发镜像内嵌定义产出的数字（包括旧 `03-folding.log`）一律无效。

---

## 6. 未闭合项

| # | 项 | 归属 |
|---|---|---|
| **U-1** | ~~折叠规则不生效（0 折叠区），需改为关键字列表引用并重测~~ **已修复（T45），T66 重录成立**：规则改为关键字列表引用；T66 修复 runner 的 F2 路径与 SV 采样，`ldd` 证明库指向 F2 安装树，两份 XML 实测 **Verilog 7/7、SystemVerilog 11/11**（SV 26 行样例；`author=VEdit / section=Hardware`）。证据：`10-folding-before-F2.log`、`11-folding-after-F2.log`；首版日志已被标注 INVALID | `resources/syntax/` lane（T45）；证据重录 T66 |
| **U-2** | A4.5（1k 行 `.sv`、tree-sitter 关闭、A3.1 观测）未采；不得引用旧 03 日志。U-1 的规则层修复已完成，T29 的期望值可取自修复后自有 XML 的 `beginRegion` 规则，但视图级观测仍须重采 | 返修 lane + `acc-editor`；T29 依赖 U-1 修复（已就绪） |
| **U-3** | `evidence/syn-kde-data/06-conclusion.md` (c) 与 C14 的"FoldingProbe 已证"表述需按 §4.4/§4.7 更正 | 原 lane / 文档维护者 |
| **U-4** | ADR §5 A/C 的正式断言脚本落地（按 §3.3 等价改写），并在产物侧复核两份 XML 与 grammar | `pack-final` |
| **U-5** | `THIRD_PARTY_NOTICES.md` 的最终产物一致性（AppDir 内每份文件 sha256 可查） | `pack-final` |

---

## 7. 派活时必转的 3 句话（captain 直接复制）

1. **A4(a) 定案、不重开**：`.sv` 关闭态折叠用自有 LGPL-2.1-or-later
   `resources/syntax/vedit-systemverilog.xml`；A4.4 的 (b) 关闭；用户无需再选。
2. **折叠写法必须是"关键字列表引用"**：`<keyword String="module" beginRegion=…>` 会被
   KSyntaxHighlighting 当"未知关键字列表"丢弃；必须写成 `<list name="…">` +
   `<keyword String="列表名" beginRegion/endRegion>`（KDE 同款 idiom）。
3. **证据归属**：旧 `03-folding.log` 的 7/7、9/9 来自 KDE 内嵌定义（`.sv` 为 GPLv3+），
   **不是**我们的 XML；折叠规则已由 T45 重写并在 F2（无内置定义）上重测，但 A4.5 的**视图级**
   证据仍需在修复后的 F2 上按 §4.7/§4.8 重采（具体计数与 `ldd` 库指向见 `evidence/syn-kde-data/`
   的重录版；原 `10`/`11` 日志已被证明测的是 DISTRO 库）。
