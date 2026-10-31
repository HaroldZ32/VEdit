# ADR r2-license：tree-sitter grammar 许可与 `.sv` 底座定义处置

- 状态：**Accepted，但 D2 的打包前提已修订（T56，待用户在其二选项中裁决，见 §3 D2「前提修订」）**
- 日期：2026-10-31（D2 收口于 `T54`；前提修订于 `T56`）· cell `cell-20261031T014507403Z` · 任务 `T6` → `T54` → `T56`
- 证据：`evidence/r2-license/report.md`（E1–E15，`verify.sh` 51 条断言）· `evidence/r2-ksh-embedded/report.md`（Q1–Q5，`run.sh` 正/负控）
- 关联需求：C7（许可处置）、C15（可替换 grammar 接缝）、C18（tree-sitter 随包）、G2/G3a、A1f/A1g/A1h

---

## 1. 背景

产品从 GTK3 + GtkSourceView 重写为 **Qt6 + KTextEditor + KSyntaxHighlighting + tree-sitter**，并以单文件 AppImage 交付（C2/G7）。需求书把「tree-sitter Verilog/SystemVerilog grammar 的许可」列为**严重未决项**：若 grammar 是 GPL-3.0 且静态编译进二进制，整个 AppImage 将被迫按 GPL 分发，而本产品自身是 **MIT**（仓库 `LICENSE:1-3`）。

本 ADR 依据实测证据（非记忆、非二手转述）回答两件事：grammar 用什么、`.sv` 的底座定义怎么办。

---

## 2. 关键事实（已核实，逐条对应报告）

| # | 事实 | 报告条目 |
|---|---|---|
| F1 | Lapce v0.4.6（tag `a54a1bb…` → commit `b012cef…`）**没有 vendored 任何 grammar**：`Cargo.lock` 只有 `tree-sitter` 运行库，Verilog 条目 `extensions: &[]` 且 `TreeSitterProperties::DEFAULT.grammar = None` | E1–E5 |
| F2 | Lapce 的 grammar 是**运行期**从 `lapce/tree-sitter-grammars` release 下载的预编译 `.so`，且要求版本 semver 精确相等；该仓库**没有 `v0.4.6` 发布** ⇒ 0.4.6 在这条路径上拿不到 grammar | E4、E6 |
| F3 | 该仓库的 grammar 集合由 `helix-editor/helix@0a4432b…` 的 `languages.toml` 决定 | E7、E8 |
| F4 | `verilog` 语言覆盖 `.v/.vh/.sv/.svh`，grammar = `tree-sitter/tree-sitter-verilog@4457145e…`；**没有独立 SystemVerilog grammar** | E8 |
| F5 | 该 grammar 的 `LICENSE` 原文为 **MIT**（© 2018-2021 Aliaksei Chapyzhenka），`Cargo.toml:9 license = "MIT"`，`package.json:5 "MIT"` | E9 |
| F6 | KDE `systemverilog.xml` 文件内自带 **GPL-3.0-or-later** 声明（第 5–8 行）与 `license="GPLv3+"`（第 42 行） | E13 |
| F7 | KDE `verilog.xml` 的 `license=""`、无 SPDX 头 ⇒ **许可未声明**；KDE `data/syntax/` 414 个定义许可**混杂**（LGPL/MIT/GPLv2+/GPLv3+/空/无属性） | E13、E14 |
| F8 | 可用的 LGPL 替代：GtkSourceView 4.6.1 `verilog.lang`(475 行) / `systemverilog.lang`(262 行，第 256 行跨语言继承)，pin + sha256 已在 `scripts/versions.env:24-26` 与 `evidence/highlight/03-langspec-provenance.txt` | E12 |
| F9 | Apache-2.0 的 tree-sitter Verilog/SV grammar **检索 0 命中**（GitHub 仓库检索口径） | E11 |
| F10 | 复用 helix query = **MPL-2.0** 文件级 copyleft；且 helix 的 verilog queries **没有 `folds.scm`**（折叠 query 需自写） | E15 |

---

## 3. 决策

### D1 — 默认 grammar：`tree-sitter/tree-sitter-verilog` @ `4457145e795b363f072463e697dfe2f6973c9a52`，MIT，**默认编译进 AppImage**

- 依据：F3–F5。MIT 与静态链接单一二进制兼容，**不触发 copyleft 传染**。
- 义务：随包分发 MIT 版权行 + 许可全文（`docs/licenses/` 下留档 + `THIRD_PARTY_NOTICES.md` 行），并锁定 commit。
- 状态：**Accepted（取证已闭合）**。

### D2 — `.sv` 底座定义：**Accepted = 方案 B**（移植 LGPL 定义）

- **裁决**：用户 2026-10-31 决定 D2 = **方案 B**，经 captain 于任务 `T54`（dispatch 2026-10-31T02:47:01Z）转达。
- **一句话**：把 GtkSourceView 4.6.1 的 LGPL-2.1-or-later `verilog.lang` / `systemverilog.lang` 移植成 KSyntaxHighlighting XML，作为**我们自己的构建产物**随 AppImage 分发；KDE 的两份定义（GPL-3.0-or-later / 许可未声明）**永不随包**。
- **代价依据**（报告 §5.2 方案 B 行）：无 GPL 组件；派生物保持 LGPL-2.1-or-later 并随包提供源码（即 XML 本身）；估算 **1.5–3 人日**（必须展平 `verilog:verilog` 继承）。

**D2 执行口径（6 条，后续 lane 直接引用）**

1. **来源**：GtkSourceView **4.6.1** 的 `verilog.lang`（475 行，sha256 `7670ddfd7b443f5f99c202bfa79de31834c1bd8968ed74c39474dca4f439d464`）与 `systemverilog.lang`（262 行，sha256 `9d1f67411f2674d93f035620ce004da4beca5bbf469eb9955a83c332edb79b04`），许可 **LGPL-2.1-or-later**。
   实体文件**不在** `assets/language-specs/`（那里只有 `README.md`）；实体在 `scripts/versions.env:24-26` 锁定的 tarball（`GSV_SHA256=63f487a7ab59612ffa9e7be1883dbd274ab1016afcede3239b93b0fc384df874`）里，身份见 `evidence/highlight/03-langspec-provenance.txt`。
2. **产物**：移植为 KSyntaxHighlighting XML，作为**本仓库构建产物**随 AppImage 分发（落点由 `syn-kde-data` 契约决定：`resources/syntax/`）。**必须**：(a) 保留 LGPL-2.1-or-later 声明与上游/原作者归属（GtkSourceView 4.6.1 及其 `verilog.lang` / `systemverilog.lang` 的作者）；(b) 随包提供该定义的源码（XML 本身即源码，须在交付物中可见、可修改）；(c) 在 `THIRD_PARTY_NOTICES.md` 登记（组件 / 版本 / 许可 / 来源与 sha256 / 落点）。
3. **禁止**：不得随包 KDE 的 `systemverilog.xml`（sha256 `3361d8b4ef62c4868165ef045c02d8b6e85a1e57f61c304b399661bdc06be015`，GPL-3.0-or-later）与 `verilog.xml`（sha256 `7199e3677b0a942e65b40f95cb2481be062b1ca48eb048519e7d4efc098f5970`，许可未声明）。**构建期/打包期加 sha256 断言**（§5 A/B 段），命中即失败。
4. **坑**：必须**展平** `systemverilog.lang:256` 的 `<context ref="verilog:verilog"/>` 跨文件继承 —— KSyntaxHighlighting **没有**跨文件 context 引用；展平后才是一个自洽的 `systemverilog.xml`。工作量按 **1.5–3 人日**计。
5. **映射**：`.sv/.svh` 映射到**自有的 LGPL 定义**；若映射失败（文件缺失/加载失败），**不允许**回退到 KDE 定义 —— 只允许降级为「底座不着色 + tree-sitter overlay 独立工作」，并给出可诊断日志而非崩溃。
6. **附带（折叠）**：A1f 的折叠 query **自写**。helix 的 `runtime/queries/verilog/` 只有 `highlights.scm` / `injections.scm` / `locals.scm` / `textobjects.scm`，**无 `folds.scm`**；且 helix 整体是 **MPL-2.0**，其 query 是文件级 copyleft，**不要直接复制**（若确需复用，必须保留 MPL 许可与来源并随包许可文本）。

- 候选与代价（报告 §5.2，保留以备复核）：

  | 方案 | 许可后果 | 代价 | 裁决 |
  |---|---|---|---|
  | A 原样随包 KDE `systemverilog.xml` | 交付集含 GPL-3.0-or-later 组件，需 GPLv3 全文 + 源码 | 0 工程日 | ✗ 已否决 |
  | **B 移植 LGPL `.lang` → KSyntaxHighlighting XML** | 无 GPL；派生物保持 LGPL-2.1-or-later + 源码可得 | **1.5–3 人日**（须展平继承） | ✓ **Accepted（用户 2026-10-31）** |
  | C v1 只做 `.v` | 零新增义务 | 0 人日，功能缺口可见 | 仅作映射失败时的功能降级（口径第 5 条），**不是**许可兜底 |
  | D 自写 SV XML（IEEE 1800 + UVM） | 最干净，可 MIT | 2–4 人日 | 长期可选（若 B 的移植质量不达标） |
  | E 向上游求再许可 | 成功则等同 A 且无传染 | 时间不可控 | 不再需要 |
- 状态：**Accepted（B）**。执行侧：`syn-kde-data`（T16，写 `src/syntax/kde/` 与 `resources/syntax/`）落地移植；`pack-final`（T38）在产物侧断言；`syn-grammars`（T17）负责口径第 6 条的 folds query。

#### D2 前提修订（T56，2026-10-31）—— B 的**选择策略不变，打包前提不成立**

- **新事实（实测，非推测）**：发行版 `libkf6syntaxhighlighting6 6.24.0-0ubuntu1` 的 `libKF6SyntaxHighlighting.so.6.24.0` **内嵌语法定义**：450 个 `*.xml` 资源条目、30 个 `.theme`；可解压出 **387 个定义**，其中 **36 个是严格 GPL 系**，含 **`SystemVerilog = GPLv3+`**、`Vera = GPLv3+`、`Kconfig = GPLv3`、`Markdown = GPL,BSD`。证据：`evidence/r2-ksh-embedded/report.md` + `scan.log`（正/负控已跑通）。
- **后果（对上面的口径）**：口径 3 的**文件级 sha256 断言不足以证明干净** —— 文件确实没随包，但 GPL 内容已随 `.so` 分销。「不拷文件即干净」这个前提**不成立**。
- **B 因此必须按 B′ 执行**：`B + 用 -DQRC_SYNTAX=OFF 重新构建 KSyntaxHighlighting`（该开关默认 **ON**；关掉后定义改为安装成文件，见上游 `data/CMakeLists.txt:144-156`），再加 `-DNO_STANDARD_PATHS=ON` 把加载集合限制为我们的白名单。代价 **0.5–1 人日**（构建接线 + 验证），风险是与 KTextEditor 的 KF6 版本/ABI 匹配（同 6.24.0）。
- **未决（用户/法务）**：若不自建库，则等价于「随包发行版 `.so` + 接受分销 GPL 内容」；法律定性（聚合 vs 组合作品）**需法务确认**，本 ADR **不**替用户接受，也**没有**把 D2 改成 D。
- **D 单独不解决**：改自写 `.sv` 定义只换「选中谁」，`.so` 里那 36 个 GPL 定义照旧随包；D 与 `.so` 构建选项是正交的。
- 断言升级见 §5 G1–G3（文件级 A/B 保留为必要不充分）。

### D3 — C15/C18 接缝与锁定

- 单一锁文件 `grammars.lock.toml`：`name / git / rev / subpath? / license / license_sha256`；构建只读它，代码中不写死 grammar 名或 API。
- 构建期校验 `LICENSE` 的 sha256（本 ADR 已给出 `92dbb929…`），不符即拒绝构建（防上游改写/force-push）。
- 运行期可整体关闭 tree-sitter（A1h 断言），关闭后回退 KSyntaxHighlighting 底座（C12 的兜底语义）。
- **若未来换入 GPL grammar**：`dlopen` 独立 `.so` **不是**可靠的 copyleft 隔离（共享地址空间/紧耦合即组合作品）。唯一干净口径是**不随包**、由用户运行期自备；因此那时「默认不随包 + 接缝可选」是**必须项**，不是可选项。
- 状态：**Accepted（工程口径）**。

### D4 — 通知与文档义务

- grammar（MIT）：版权行 + 全文 + commit；
- 若复用 helix queries（MPL-2.0）：保留这些文件与 MPL 许可，随包 `docs/licenses/MPL-2.0.txt`，并注明来源 commit；
- 折叠 query 自写（helix 无 `folds.scm`），归属本仓库（MIT）；
- KSyntaxHighlighting 定义随包必须**白名单化**（F7），禁止整目录复制；D2=B 的 LGPL 移植产物是白名单内的条目。

---

## 4. 后果

**正面**：许可阻塞解除——grammar 是 MIT，可默认随包；不必为「GPL 二进制」重做架构；C15 接缝仍值得保留（换 grammar 是数据变更，不是代码变更）。

**负面/风险**：
- 该 grammar 是 2022 年的实现，SV 覆盖偏弱（`assertion` 在 `grammar.js` 中计数为 0），A1f 的 1k 行 SV 折叠一致性**尚未验收**（U4）；若不达标，替换为 MIT 的 `gmlarumbe/tree-sitter-systemverilog@08c5e37…`（需重跑 A1f/A1g）。
- `.sv` 底座已按 **D2=B** 定案，但**移植产物尚未落地**：在 `resources/syntax/systemverilog.xml`（T16 的产物）进入树之前，`.sv` 只有 overlay 着色。这是**过渡态**，不是最终交付态（U9）。
- KDE `verilog.xml` 许可未声明这一点**此前未被记录**：任何「直接抄 KDE 定义」的路径（包括 `.v`）都不成立。

---

## 5. 强制断言（打包/构建，供 pack-final / syn-kde-data / syn-grammars 落地）

```bash
# A. KDE 的两份定义永不随包（含 KSyntaxHighlighting 自带 install 树）
for f in systemverilog.xml verilog.xml; do
  p="$PAYLOAD/share/ksyntaxhighlighting/syntax/$f"
  [ -e "$p" ] && { echo "FAIL: KDE definition path present: $p"; exit 1; }
done

# B. 任何随包的 syntax XML 都不得与 KDE 那两份字节相同（防改名带入）
for x in "$PAYLOAD"/share/ksyntaxhighlighting/syntax/*.xml; do
  case "$(sha256sum "$x" | cut -d' ' -f1)" in
    3361d8b4ef62c4868165ef045c02d8b6e85a1e57f61c304b399661bdc06be015|\
    7199e3677b0a942e65b40f95cb2481be062b1ca48eb048519e7d4efc098f5970)
      echo "FAIL: KDE definition bundled: $x"; exit 1;;
  esac
done

# C. D2=B 的正向断言：.sv 定义必须存在，且是我们的 LGPL 产物（带声明与归属）
sv="$PAYLOAD/share/ksyntaxhighlighting/syntax/systemverilog.xml"
[ -e "$sv" ] || { echo "FAIL: own systemverilog.xml missing (D2=B requires it)"; exit 1; }
grep -q 'LGPL-2.1' "$sv"        || { echo "FAIL: $sv lacks the LGPL-2.1 declaration"; exit 1; }
grep -qi 'GtkSourceView' "$sv"  || { echo "FAIL: $sv lacks upstream attribution"; exit 1; }

# D. .sv/.svh 映射必须指向我们的定义；映射失败只允许「底座不着色」，不得指向 KDE
k="$PAYLOAD/share/ksyntaxhighlighting/syntax-mapping.conf"   # 实际文件名以应用实现为准
grep -q 'systemverilog.xml' "$k" || { echo "FAIL: .sv mapping missing"; exit 1; }

# E. grammar LICENSE 锁定（构建期，checkout 后）
[ "$(sha256sum "$GRAMMAR_DIR/LICENSE" | cut -d' ' -f1)" = \
  "92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46" ] || {
  echo "FAIL: tree-sitter-verilog LICENSE drifted"; exit 1; }

# F. 折叠 query 必须是本仓库文件（内容归属由 T17 的报告说明）
[ -s resources/queries/folds.scm ] || { echo "FAIL: own folds.scm missing"; exit 1; }

# G1. 主闸门（T56）：随包的 KSyntaxHighlighting 库**不得内嵌严格 GPL 定义**
python3 evidence/r2-ksh-embedded/scan-embedded-defs.py --expect-clean \
        "$PAYLOAD/lib/libKF6SyntaxHighlighting.so.6" \
  || { echo "FAIL: KSyntaxHighlighting embeds strict-GPL definitions"; exit 1; }
# 期望 exit 0；发行版构建（内嵌 36 个 GPL 定义，含 SystemVerilog=GPLv3+）→ exit 1

# G2. 快速预检（只需 binutils）：资源名表里不得出现 systemverilog.xml
if strings -a -el "$PAYLOAD/lib/libKF6SyntaxHighlighting.so.6" | grep -qx 'systemverilog.xml'; then
  echo "FAIL: systemverilog.xml is embedded in the library"; exit 1
fi

# G3. 构建来源：自建库的 CMake 配置必须记录 QRC_SYNTAX=OFF（进 m0 证据目录）
grep -q 'QRC_SYNTAX:BOOL=OFF' "$BUILD_DIR/CMakeCache.txt"
```

> A/B/E 现在即可实现（A/B 不依赖移植产物）。**C 在 `syn-kde-data` 的 XML 落地后启用**：落地前它必须是 RED，禁止用「先关断言」换绿。D 依赖应用侧映射表实现，F 属 T17。
>
> **重要（T56 修订）**：**A/B 是必要不充分条件** —— 它们只能证明「没有把某份 XML 文件拷进包」，**证明不了「库内嵌了什么」**。发行版 `.so` 内嵌 450 个定义（含 36 个严格 GPL，`SystemVerilog=GPLv3+`）时，A/B 仍然会全部通过。**G1 是覆盖该缺口的主闸门**，已用正/负控证明它会亮红：发行版 6.24.0 库 → `--expect-clean` exit 1；无语法数据的 `libKF6TextEditor` 库 → exit 0（`evidence/r2-ksh-embedded/scan.log`）。G2 是无 python 依赖的快速预检；G3 证明「干净」来自构建配置，而非侥幸。

---

## 6. 未闭合项

| # | 项 | 归属 |
|---|---|---|
| U1 | ~~用户裁决 D2~~ → **已闭合：B（2026-10-31）** | — |
| U2 | A1f：1k 行 SV 折叠一致性 + 该 grammar 的 SV 解析成功率实测 | overlay / 编辑内核任务 |
| U3 | **D2=B 的移植本身**（`.lang` → KSyntaxHighlighting XML，展平继承；1.5–3 人日）：本 ADR 只给口径，实现属 T16 | syn-kde-data（T16） |
| U4 | KDE 414 个定义的许可清点与白名单 | pack-final（T38） |
| U5 | ~~KDE 镜像滞后~~ → **不再相关**（仅方案 A 需要，A 已否决） | — |
| U6 | `tree-sitter` 运行库版本与该 grammar rev（`tree-sitter 0.20` API）的 ABI 实测 | syn-grammars（T17） |
| U7 | `grammars-linux-x86_64.tar.zst` 未下载枚举；0.4.6 无对应 release，无法枚举 | 无需闭合（结论不依赖） |
| **U8** | **T16 冻结契约文本对齐**：契约 (c) 写「systemverilog 定义在 C7 处置未定前不得随包」；现 C7=B 已定，严格按文本执行会把「随包自有 LGPL `.sv` 定义」判成违规。需 captain 向 T16 的 verifier 给出书面口径，或更新契约 | captain / cell-planner |
| **U9** | 移植产物尚未落地：`resources/syntax/{verilog,systemverilog}.xml` 不存在；§5 断言 C 在此之前保持 RED | syn-kde-data（T16） |
| **U10** | **用户/法务裁决**：B′（自建 `QRC_SYNTAX=OFF` 的 KSyntaxHighlighting）还是「随包发行版 `.so` + 接受分销 36 个 GPL 定义」 | 用户 / captain / 法务 |
| **U11** | 主题（30 个 `.theme`）**无条件嵌入**（`data/CMakeLists.txt:107`），`QRC_SYNTAX` 管不到；其许可未清点 | 数据许可清点任务 |
| **U12** | 计数不一致：资源名 450、可解压定义 387、T7 运行期 451（不影响「有 GPL 内容」的结论） | 可留待复核 |
| **U13** | 自建 `QRC_SYNTAX=OFF` 的 6.24.0 库与 KTextEditor 6.24.0 的运行时兼容（ABI/符号）未验证 | m0-runtime（若选 B′） |
| **U14** | `NO_STANDARD_PATHS=ON` + `addCustomSearchPath()` 组合下主题的加载行为未验证（主题始终内嵌） | syn-kde-data + m0 |
| **U15** | 法律定性（聚合 vs 组合作品）与通知义务的最终形态 | **法务**（需法务确认） |

---

## 7. 转交清单：`syn-kde-data`（T16）/ `pack-final`（T38）/ `syn-grammars`（T17）的约束

### 7.1 给 T16 `syn-kde-data`（写 `src/syntax/kde/`、`resources/syntax/`）

| # | 约束 | 判据（verifier 可直接查） |
|---|---|---|
| C1 来源 | 只从 GtkSourceView **4.6.1** 的 `verilog.lang`（475 行）/ `systemverilog.lang`（262 行）移植；tarball 与 sha256 见 `scripts/versions.env:24-26` | 产物注明上游版本 + sha256 `7670ddfd…` / `9d1f6741…`，并引用 `evidence/highlight/03-langspec-provenance.txt` |
| C2 许可与归属 | 产物声明 **LGPL-2.1-or-later**，保留 GtkSourceView 上游与原作者归属；XML 即源码、随包可得 | XML 头含 LGPL-2.1 声明 + GtkSourceView 归属；`THIRD_PARTY_NOTICES.md` 新增行（组件/版本/许可/来源 sha256/落点） |
| C3 禁止 KDE | 树内与产物内都不得出现 KDE 两份定义（含改名） | `systemverilog.xml` / `verilog.xml` 的 sha256 ≠ `3361d8b4…` / `7199e367…` |
| C4 展平 | `systemverilog.lang:256` 的 `<context ref="verilog:verilog"/>` 必须展平（KSyntaxHighlighting 无跨文件 context ref） | 产物内无跨文件 `context ref`；`.sv` 样本中 `module` / `always_ff` / `interface` 均可着色（渲染证据） |
| C5 映射与降级 | `.sv/.svh` → 自有定义；加载失败只降级为「底座不着色」+ 可诊断日志，**不得**回退 KDE | 映射表含 `.sv`/`.svh` → `systemverilog.xml`；缺文件时的日志进 `evidence/syn-kde-data/` |
| C6 折叠 | A1f 的 `folds.scm` **自写**（不复制 helix 的 MPL-2.0 query） | `resources/queries/folds.scm` 由本仓库产出，归属在 T17 报告中声明 |

> **契约对齐提醒（U8）**：T16 的冻结契约 (c) 原文是「systemverilog 定义在 C7 处置未定前不得随包」。C7 现已定为 **B**，该前提不再成立；若 verifier 严格按原文判，会把 C1–C5 的合规产物判成违规。建议 captain 在 T16 的验证记录里附本 ADR 作为裁决依据。

### 7.2 给 T38 `pack-final`

| # | 约束 | 判据 |
|---|---|---|
| P1 | 产物侧执行 §5 的 **A/B/C/G1/G2** 断言；C 在 T16 落地前保持 **RED**，G1 在「自建 `QRC_SYNTAX=OFF` 库」落地前保持 **RED**；都不得关断 | 断言脚本落 `packaging/final/`，日志进 `evidence/pack-final/` |
| P2 | `THIRD_PARTY_NOTICES.md` 与实际随包一致：(i) MIT grammar `4457145e…`；(ii) LGPL-2.1-or-later 移植定义；(iii) 若复用 helix query 则 MPL-2.0 | notices 行与产物 sha256/路径互相可查 |
| P3 | KDE 414 个定义的清点与白名单（U4）——不得整目录随包 | 白名单文件 + 每条 `license=` 判定 |
| P4 | A6/A7 与首启提示不得声称 `.sv` 高亮来自 KDE | 文档措辞与本 ADR 一致 |

### 7.3 给 T17 `syn-grammars`

口径第 6 条：`folds.scm` 自写；若确需复用 helix query（MPL-2.0 文件级），必须保留 MPL 声明与来源 commit `0a4432b…`，并随包 `docs/licenses/MPL-2.0.txt`。

---

## 8. 复现

```bash
bash evidence/r2-license/verify.sh   # 44 条断言；本文件所有 sha256 与关键原文均被断言
```

---

## 9. 勘误（AMENDMENTS rev 2 / C15-3，2026-10-31）

本节**只记录修正与判读口径，不改写 §5 正文**（ADR 是决策记录，修正以勘误追加）。
权威副本：[`docs/handover/appendix/amendments-rev2.md`](../handover/appendix/amendments-rev2.md) §3。

1. **命名冲突（事实）**：§5 **C** 要求 `$PAYLOAD/share/ksyntaxhighlighting/syntax/systemverilog.xml`
   存在，而 §5 **A** 与 `packaging/runtime/verify-not-shipped.sh:53-57` 的黑名单**禁止任何**
   basename 为 `systemverilog.xml` / `verilog.xml` 的文件 —— 按 §5 C 的字面命名，门禁必然 FAIL。
   实际交付名是 **`vedit-systemverilog.xml` / `vedit-verilog.xml`**（C14）。
2. **路径更正（事实）**：实际安装点是
   `$PAYLOAD/usr/share/org.kde.syntax-highlighting/syntax/`
   （`packaging/runtime/assemble-appdir.sh:109-112`、`packaging/runtime/AppRun:33`、
   `evidence/m0-runtime/runtime-manifest.txt:195-196`），**不是** §5 A/C 写的
   `share/ksyntaxhighlighting/syntax/`。§5 A 的"路径不存在"检查因此是**空检查**；
   真正执行禁运的是 `verify-not-shipped.sh`（文件名 + sha256 双黑名单），它**不得被删除或放宽**。
3. **判据等价改写（语义不变）**：§5 C 的检查对象读作
   `.../usr/share/org.kde.syntax-highlighting/syntax/vedit-systemverilog.xml`，
   检查项（存在 + `LGPL-2.1` 声明 + `GtkSourceView` 归属）原样保留；两份自有 XML 均满足
   （grep 实测见 `evidence/docs-amendments/02-sha256-check.txt`）。§5 A/B 的"必要不充分"提示
   与 **G1 内嵌扫描主闸门**保持原样，本次修正只动文件级检查的路径字面。
4. **与 D2 执行口径不冲突**：D2 口径 2 的落点由 `syn-kde-data` 契约定为 `resources/syntax/`；
   本勘误只修 §5 的旧路径/旧文件名字面，不改变"自有 LGPL 定义随包、KDE 定义永不随包"的决策。
5. **交叉引用（A4 相关，非本 ADR 的决策）**：交付件折叠规则当前不生效（0 折叠区，A4.5 未满足），
   旧 `03-folding.log` 的 7/7、9/9 测到的是 KDE 内嵌定义（`.sv` 为 GPLv3+），不得引用。
   详见 amendments rev2 §4；本 ADR 的 D2/许可结论不受影响。

**更正后的可执行断言（可抄给 `pack-final`）**：

```sh
sv="$PAYLOAD/usr/share/org.kde.syntax-highlighting/syntax/vedit-systemverilog.xml"
[ -e "$sv" ] || { echo "FAIL: own systemverilog.xml missing (D2=B requires it)"; exit 1; }
grep -q 'LGPL-2.1' "$sv"        || { echo "FAIL: $sv lacks the LGPL-2.1 declaration"; exit 1; }
grep -qi 'GtkSourceView' "$sv"  || { echo "FAIL: $sv lacks upstream attribution"; exit 1; }
# 文件名黑名单保持（与 verify-not-shipped.sh 一致）：
if find "$PAYLOAD" -type f \( -name 'systemverilog.xml' -o -name 'verilog.xml' \) | grep -q .; then
  echo "FAIL: a file named systemverilog.xml / verilog.xml is present"; exit 1
fi
```
