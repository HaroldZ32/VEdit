# R2 取证报告：tree-sitter Verilog/SystemVerilog grammar 身份、commit、许可 + C7 `.sv` 处置

- 任务：`T6 / r2-license`（cell `cell-20261031T014507403Z`，split revision 2）
- 执行席位：Deep Worker · 取证时间 2026-10-31（Asia/Shanghai）· 网络：GitHub API / raw 可达（HTTP 200）
- 边界：只读上游 + 只写 `evidence/r2-license/`、`docs/decisions/`；**未**下载任何 grammar 源码进交付树，**未**改生产代码/构建脚本/打包
- 复现脚本：`evidence/r2-license/verify.sh`（逐条断言本文件全部 sha256 与关键字符串，**任一不符即退出码 1**）

---

## 0. 结论（可直接用于决策）

### 0.1 一句话回答「这个 grammar 能不能默认编译进交付的 AppImage？」

**能，附 MIT 通知保留义务**（`LICENSE:12-13`：「The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software」——随包分发时必须带版权行与许可全文，见 §4 第 6 条「通知义务」）。

依据：Lapce 0.4.6 用到 Verilog/SystemVerilog 的那条链最终指向 **`tree-sitter/tree-sitter-verilog` @ `4457145e795b363f072463e697dfe2f6973c9a52`**，其 `LICENSE` 原文是 **MIT**（E8），`Cargo.toml` 第 9 行 `license = "MIT"`（E8）。MIT 与静态编译进单一二进制兼容：不产生 copyleft 传染，只需满足 MIT 第 12–13 行的通知保留义务。

### 0.2 但任务书里的前提需要更正：Lapce 0.4.6 **没有** vendored 任何 grammar

「Lapce 0.4.6 vendored 的 Verilog / SystemVerilog grammar」这个前提在本仓库发布的 v0.4.6 上**不成立**（已核实，非推测）：

| 事实 | 证据 |
|---|---|
| `v0.4.6` 标签（annotated tag `a54a1bb…`）指向 commit `b012cef…`，`release: v0.4.6`，workspace 版本 `0.4.6` | E1、E2 |
| `Cargo.lock` 里**只有** `tree-sitter` 运行库，没有任何 `tree-sitter-<lang>` grammar crate | E3 |
| `lapce-core/src/language.rs` 的 `Verilog` 条目 `extensions: &[]`，且 `TreeSitterProperties::DEFAULT` 的 `grammar: None` —— 0.4.6 的 Verilog 既没有扩展名映射，也没有绑定 grammar | E5 |
| grammar 由 `lapce-app/src/app/grammars.rs` 在**运行期**从 `lapce/tree-sitter-grammars` 的 GitHub release 下载预编译 `.so`（`grammars-<os>-<arch>`） | E4 |
| `lapce/tree-sitter-grammars` **没有 `v0.4.6` 的 tag 或 release**（最新 `v0.4.5+1f9f9dd`），而下载逻辑要求 release 版本与 `VERSION` **semver 相等** ⇒ 0.4.6 在这条路径上直接报错，拿不到任何 grammar | E6、E4:46–49 |
| 该仓库构建 grammar 的方式：clone `helix-editor/helix` @ `0a4432b…`，读其 `languages.toml`，按 `source.git` + `source.rev` 逐个构建 | E7、E8 |

⇒ **0.4.6 的二进制里没有 Verilog grammar 的 C 代码，也没有可用的下载源。** 因此「照 Lapce 做」不可能照抄一份 vendored grammar；我们能确定的只是 Lapce 的 grammar 机制**会解析到哪个上游**（下面 E9–E11 给出去重后的唯一身份）。

### 0.3 唯一身份（Lapce 机制指向的 Verilog/SV grammar）

| 项 | 值 |
|---|---|
| 仓库 | `https://github.com/tree-sitter/tree-sitter-verilog` |
| commit | `4457145e795b363f072463e697dfe2f6973c9a52`（2022-09-07，`Merge pull request #56 from andreytkachenko/master`） |
| 覆盖文件类型 | `.v` `.vh` `.sv` `.svh`（由 helix `languages.toml:2199` 决定；**没有独立的 SystemVerilog grammar**） |
| 许可 | **MIT**（`LICENSE` 1–21 行原文，© 2018-2021 Aliaksei Chapyzhenka） |
| 判定 | 非 GPL ⇒ 「静态编译进二进制会迫使整个 AppImage 按 GPL 分发」的风险**不成立**；可默认随包 |

### 0.4 C7（`.sv` 底座定义）结论

- KDE `data/syntax/systemverilog.xml` 文件内自带 GPLv3-or-later 声明（第 5–8 行）与 `license="GPLv3+"`（第 42 行）⇒ 与用户已核对事实一致（E13）。**不再重证许可事实**，本报告只给处置口径（§5）。
- 本次新增一条影响面更大的事实：KDE 的 `data/syntax/verilog.xml` **`license=""`、无 SPDX 头**（E13），抽样 8 个定义显示该目录许可**混杂**（`LGPL` / `MIT` / `GPLv2+` / `GPLv3+` / 空 / 无属性，E14）⇒ **不能整目录随包**，必须白名单化。
- **处置（已裁决，2026-10-31）**：D2 = **方案 B** —— 移植 GtkSourceView 4.6.1 的 `.lang`（LGPL-2.1-or-later）为 KSyntaxHighlighting XML（详见 `docs/decisions/r2-license.md` §3 D2 与 §7 转交清单）。**在移植产物落地前，`.sv` 定义仍不得随包**（ADR §5 断言 C 在此之前保持 RED）；零风险兜底是 v1 只做 `.v`（方案 C，仅作功能降级）。

### 0.5 裁决记录（本报告后置，不改写取证）

- **2026-10-31**：用户裁决 **D2 = 方案 B**（captain 于任务 `T54` 的 dispatch 2026-10-31T02:47:01Z 转达）。本报告 §0.4 / §5.2 / §6 已同步；**决策与执行口径以 `docs/decisions/r2-license.md` 为准**，本报告只承担取证与本 ADR 无法脱离的事实。

---

## 1. 方法与可达性

| 探测 | 结果 |
|---|---|
| `git ls-remote https://github.com/lapce/lapce refs/tags/v0.4.6` | 200，返回 annotated tag `a54a1bb…` |
| `curl -sS -o /dev/null -w '%{http_code}' https://api.github.com/repos/lapce/lapce` | `200` |
| `raw.githubusercontent.com` 按 commit 取单文件 | 全部 200（见每条证据的 http/bytes/sha256） |
| `invent.kde.org` | **未使用**（规划席位已记录网页抓取受限）。KDE 侧证据一律取 **GitHub 官方镜像 `github.com/KDE/syntax-highlighting`**，并记录镜像 commit 与 sha256 |

**取证纪律**：每条证据标注 `仓库@commit` + URL + 字节数 + sha256 + 原文行号；三态区分 **已核实（有原文）/ 未核实（索引或网络没覆盖）/ 不存在（检索 0 命中）**。本报告没有一条结论来自记忆或二手转述。

---

## 2. 证据链

### E1 — Lapce v0.4.6 标签与 commit

```
$ git ls-remote https://github.com/lapce/lapce 'refs/tags/v0.4.6' 'refs/tags/v0.4.6^{}'
a54a1bb875e3e2cb5b39638cc72c1fa732923625	refs/tags/v0.4.6
b012cef466741528f338879fa708355217fc40bd	refs/tags/v0.4.6^{}
```
`clone --branch v0.4.6` 后 `git log -1` 输出 `b012cef466741528f338879fa708355217fc40bd 2026-01-21T20:44:40+01:00 release: v0.4.6`。**已核实**（tag 对象 ≠ commit，peeled commit 为 `b012cef…`）。

### E2 — 版本号被编译期注入（决定 E6 的 semver 比较）

- `lapce/Cargo.toml` @ `b012cef`：第 26 行 `version      = "0.4.6"`（`[workspace.package]`，等号前为对齐空格）。
- `lapce-core/src/meta.rs` @ `b012cef`（159 B）：`include!(concat!(env!("OUT_DIR"), "/meta.rs"));` ⇒ `lapce_core::meta::VERSION` 在构建期由 workspace 版本生成，稳定版即 `0.4.6`。
- 因此 `grammars.rs` 里的 `Version::parse(VERSION)` = `0.4.6`。**已核实**。

### E3 — `Cargo.lock` 中没有任何 grammar crate

`https://raw.githubusercontent.com/lapce/lapce/b012cef466741528f338879fa708355217fc40bd/Cargo.lock`
（193,528 B，sha256 `ab95631c8b2267175f604eeac7f854fed6c14d1a214d0713bc28a2a9ce2b5c4b`）

```
$ grep -cE '^name = "tree-sitter' Cargo.lock
1
$ grep -c 'tree-sitter-verilog\|tree-sitter-systemverilog' Cargo.lock
0
```
全文件**只有 1 个** `tree-sitter*` 包（运行库 `tree-sitter`），无 `tree-sitter-verilog` / `tree-sitter-systemverilog`。**已核实**：0.4.6 的二进制不可能静态包含任何 grammar 的 C 代码。
`verify.sh` 对本条做**真拉取**：拉取 `Cargo.lock` → 断言 sha256、断言 `^name = "tree-sitter` 计数 == 1、断言两种 grammar crate 名缺席（并附负控证明这三条断言可失败）；不再是文字声明。

### E4 — grammar 是运行期下载的预编译产物

`lapce-app/src/app/grammars.rs` @ `b012cef`（4,082 B，sha256 `4ea6ce659f71cc41b677af63c72dbac80fbf3ab2088801495238fbca33553140`）

```
24:        "https://api.github.com/repos/lapce/tree-sitter-grammars/releases?per_page=100",
46:            let sv = Version::parse(tag_name).ok()?;
47:            let version = Version::parse(VERSION).ok()?;
49:            if matches!(sv.cmp_precedence(&version), Ordering::Equal) {
68:    let file_name = format!("grammars-{}-{}", env::consts::OS, env::consts::ARCH);
```
**已核实**：download-only 路径；`cmp_precedence == Equal` 是硬条件，没有「取最近版本」的兜底。

### E5 — Verilog 在 0.4.6 里既无扩展名映射、也无 grammar 绑定

`lapce-core/src/language.rs` @ `b012cef`（61,971 B，sha256 `651cda912938c3002329bf2e65b346239d7dc8ab9603401c803c0824c3959ebe`）

```
  146	impl TreeSitterProperties {
  147	    const DEFAULT: Self = Self {
  148	        grammar: None,
  149	        grammar_fn: None,
  150	        query: None,
...
 1628	    SyntaxProperties {
 1629	        id: LapceLanguage::Verilog,
 1630	        indent: Indent::tab(),
 1631	        files: &[],
 1632	        extensions: &[],
 1633	        comment: comment_properties!(),
 1634	        tree_sitter: TreeSitterProperties::DEFAULT,
 1635	    },
  432	    Typst,
  433	    #[strum(message = "Verilog")]
  434	    Verilog,
```
**已核实**：`SystemVerilog` 作为独立语言**不存在** —— 在 `.rs`/`.toml`/`.json`/`.yml`/`.yaml`/`.lock` 上 `grep -ril verilog` 只命中 `lapce-core/src/language.rs`，`defaults/` 下无 verilog 条目。⚠️ 这是 0.4.6 的**功能缺口**（Lapce 自己的 Verilog 支持是空的），不是我们该照抄的行为。

### E6 — `lapce/tree-sitter-grammars` 没有 0.4.6 的发布

`GET https://api.github.com/repos/lapce/tree-sitter-grammars/tags?per_page=100`：
`v0.4.5+1f9f9dd`、`v0.4.4+1f9f9dd`、`v0.4.3+1f9f9dd`、`v0.4.2+1f9f9dd`、`v0.4.1+…`、`v0.4.0`、`nightly`、`0.4.0` —— **无 `v0.4.6`**。
`GET .../releases?per_page=15`（12 条）最新为 `v0.4.5+1f9f9dd`（2025-10-03，`target_commitish=1f9f9dd5ce93e29992bb50bff0b0aa99e664154b`）。**已核实**：0.4.6 的 `find_grammar_release()` 必然返回 `Err("Couldn't find any release")`。

### E7 — grammar 集合由 helix 的 `languages.toml` 定义

`lapce/tree-sitter-grammars` @ `1f9f9dd5ce93e29992bb50bff0b0aa99e664154b`（仓库共 12 个条目，无 vendored grammar 目录）

URL：`https://raw.githubusercontent.com/lapce/tree-sitter-grammars/1f9f9dd5ce93e29992bb50bff0b0aa99e664154b/src/main.rs`

`src/main.rs`（4,823 B，sha256 `4afbff5080eeeeeb8eb65b35ed1987aa22ec4b877262168ba6138b5a0e8f8f17`）

```
 91:    let helix_dir = tmp_dir.join("helix");
 93:        &helix_dir,
 94:        "https://github.com/helix-editor/helix",
 95:        "0a4432b104099534f7a25b8ea4148234db146ab6",
 98:    let Ok(languages_config) = &fs::read_to_string(helix_dir.join("languages.toml"))
160:    let path = tmp_dir.join(format! {"tree-sitter-{}",grammar.name});
```
构建命令为 `tree-sitter build --output libtree-sitter-<name>.<DLL_EXTENSION>`（同一文件 `build_tree_sitter`）。**已核实**。

### E8 — helix 的 pin：Verilog/SV 只有一个 grammar 条目

`helix-editor/helix` @ `0a4432b104099534f7a25b8ea4148234db146ab6`，`languages.toml`（103,835 B，sha256 `600842bec59d9cb1622ab46c2a6d7b2ca80f6de44f24f124900ce850abd72ef7`）

```
 2196	[[language]]
 2197	name = "verilog"
 2198	scope = "source.verilog"
 2199	file-types = ["v", "vh", "sv", "svh"]
 2200	comment-token = "//"
 2201	block-comment-tokens = { start = "/*", end = "*/" }
 2202	language-servers = [ "svlangserver" ]
 2203	indent = { tab-width = 2, unit = "  " }
 2204	injection-regex = "verilog"
 2205	
 2206	[[grammar]]
 2207	name = "verilog"
 2208	source = { git = "https://github.com/tree-sitter/tree-sitter-verilog", rev = "4457145e795b363f072463e697dfe2f6973c9a52" }
```
全文件大小写不敏感 `grep -ci systemverilog` = **0**（`verify.sh` 断言）⇒ **SystemVerilog 没有独立 grammar，`.sv/.svh` 复用 Verilog grammar**。**已核实**。

### E9 — grammar 身份与许可（核心证据）

`tree-sitter/tree-sitter-verilog` @ `4457145e795b363f072463e697dfe2f6973c9a52`（commit 时间 2022-09-07T23:11:11Z）

URL：仓库 `https://github.com/tree-sitter/tree-sitter-verilog`；本条目引用的文件 raw 前缀统一为
`https://raw.githubusercontent.com/tree-sitter/tree-sitter-verilog/4457145e795b363f072463e697dfe2f6973c9a52/`
（下文 `LICENSE`、`Cargo.toml`、`package.json` 均在该前缀下）。

`LICENSE`（1,082 B，sha256 `92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46`）

```
 1	MIT License
 2	
 3	Copyright (c) 2018-2021 Aliaksei Chapyzhenka
 4	
 5	Permission is hereby granted, free of charge, to any person obtaining a copy
 ...
12	The above copyright notice and this permission notice shall be included in all
13	copies or substantial portions of the Software.
 ...
21	SOFTWARE.
```

`Cargo.toml`（535 B，sha256 `721c4a33e1c4d298f90723db07abdfece112964d2ce52bbe0f327c502b93756c`）

```
  1	[package]
  2	name = "tree-sitter-verilog"
  4	version = "0.0.1"
  7	repository = "https://github.com/tree-sitter/tree-sitter-javascript"
  9	license = "MIT"
```

`package.json`（1,224 B，sha256 `4e5298fe9a340e8498af31f17865c060559e6207ff113dcf94497142b1837b0b`）：第 3 行 `"version": "0.20.2"`，第 5 行 `"license": "MIT"`。
GitHub API `GET /repos/tree-sitter/tree-sitter-verilog` → `license.spdx_id = "MIT"`。

> **元数据异常（如实记录，不改变许可结论）**：`Cargo.toml:7` 的 `repository` 字段错指向 `tree-sitter-javascript`。这是上游复制模板留下的笔误；许可有三个独立一致的来源（LICENSE 原文 / `Cargo.toml:9` / `package.json:5`）与 GitHub API 的 SPDX 判定，故按 **MIT** 认定。

可复现性附注：该 rev 的 `src/parser.c` 已提交（46,042,288 B），`src/grammar.json`（7,763,660 B）、`src/node-types.json`（271,904 B）齐全 ⇒ 不依赖 tree-sitter CLI 生成即可编译，适合构建锁定。

### E10 — SV 覆盖度（决定「能不能用」而非「能不能随包」）

同一 rev 的 `grammar.js`（133,684 B / 4,991 行，sha256 `323c121a3e550df0f970d46c544b3f02485e5e6d754acb730d6be7b8dea7dc4b`；URL `https://raw.githubusercontent.com/tree-sitter/tree-sitter-verilog/4457145e795b363f072463e697dfe2f6973c9a52/grammar.js`）中出现的 SV 构造关键字计数：

```
always_ff 1   always_comb 1   interface 7   endinterface 3   package 1
endpackage 1  class 3         endclass 2    logic 1           typedef 1
struct 2      enum 2          property 6    covergroup 1      modport 1
clocking 5    assertion 0
```
corpus 13 个文件（`corpus/{pkg,uvm,module,always,directives,function,tf,assign,comments,net_declaration,module_instantiation,5.6}.txt` + `examples/module.v`）中**没有**面向 SV 的系统性用例。**已核实**：语法层面「认识」大部分 SV 关键字，但 A1f 要求的 1k 行 SV 折叠一致性**尚未验证**（见 §6 未闭合项 U4）。

### E11 — MIT/Apache 替代候选

**MIT 候选（专用 SystemVerilog grammar，已核实）**：`gmlarumbe/tree-sitter-systemverilog` @ `08c5e379b9b6022d4c433a9249e6d54dedd051ba`（2026-10-21，`Regenerate parser and update tests after #86`）

`LICENSE`（1,080 B，sha256 `f4ceaa91e73e623d6b30895a3b644e0e277754a81c8b28655b75d7a9113f1ac4`）

```
 1	MIT License
 3	Copyright (c) 2024-2025 Gonzalo M. Larumbe
```
`Cargo.toml`（758 B，sha256 `b446910e2866a92f79a4fccf85897fe91559c274dd9ed50d92c0b61ebd94db52`）：`version = "0.4.1"`、`license = "MIT"`、`tree-sitter = "0.25.3"`。
`grammar.js` 232,409 B / 6,238 行（sha256 `d844947bf6b95e7c4b5703436b1bc8174d3b7f0a9f72cfbcc2d2da8f8f540289`），SV 构造计数显著更高（`class 30`、`covergroup 26`、`interface 18`、`constraint 13`）；测试在 `test/`（非 `test/corpus` 布局）；query 只有 `queries/highlights.scm`。

**Apache-2.0 候选：不存在（检索 0 命中）**。GitHub 仓库检索
`tree-sitter verilog license:apache-2.0` → `total_count: 0`；
`systemverilog grammar license:apache-2.0` → `total_count: 0`。
⇒ 如实记为**未命中**（不是「不存在于宇宙」，是「GitHub 仓库检索口径内不存在」）。

**Apache-2.0 的结构性替代（非 tree-sitter）**：Verible（`chipsalliance/verible`，Apache-2.0）本仓库已作为 LSP/格式化后端随包（`THIRD_PARTY_NOTICES.md:13`，`v0.0-4298-g085eef05`）。它有完整的 SV 前端（parser + CST，精度高于任何 tree-sitter 语法），但**不能**以 tree-sitter 的 query/API 形态喂给 `SyntaxProvider`：接入它意味着给 overlay 写第二个后端、或把折叠/面包屑从 tree-sitter query 改成 Verible CST 遍历——与 C15「抽象 SyntaxProvider」并行可行，但工作量远大于换 grammar（估 2–4 人周，且 Verible LSP 进程模型与增量解析不同）。

**自写 grammar 成本量级（以实测规模为基线）**：

| 路线 | 规模基线 | 估算 | 备注 |
|---|---|---|---|
| 自写 Verilog+SV tree-sitter grammar | 参考实现 `grammar.js` 4,991 行 / 133 KB、13 个 corpus；SV 专用实现 6,238 行 / 232 KB、已到 `v0.4.1` | **4–8 人周** | 含 query（highlights/locals/folds）、错误恢复调优；这是最贵的兜底 |
| 自写 KSyntaxHighlighting SV XML（IEEE 1800 关键字表 + UVM/`` `ifdef ``） | 目标产物量级 500–900 行 XML | **2–4 人日** | 无许可风险；底座本来就只是「逐行 + 上下文栈」，不追求 AST |
| 移植 GtkSourceView `.lang` → KSyntaxHighlighting XML | `verilog.lang` 475 行、`systemverilog.lang` 262 行 | **1.5–3 人日** | 见 §5 方案 B |

### E12 — 本地 LGPL 数据资产的真实位置（对 C7 的输入）

- `assets/language-specs/` 在 HEAD `eebbfd1`（分支 `dev`）**只有 `README.md`（2,946 B），没有任何 `.lang` 实体文件**（`find assets/language-specs -maxdepth 1 -type f | wc -l` = 1，`verify.sh` 断言）。**任何任务都不得按 `assets/language-specs/{verilog,systemverilog}.lang` 取文件——该路径不存在**；必须从 `scripts/versions.env:24-26` 锁定的 GtkSourceView tarball 取。需求文档 C7 的措辞「仓库现有 `assets/language-specs/{verilog,systemverilog}.lang`」与实际目录不一致（README 明确写了「不 vendored，避免第二份漂移副本」），下游按路径取文件会落空。
- 真正的 LGPL 定义来自 **GtkSourceView 4.6.1** 固定 tarball：`scripts/versions.env:24-26`
  `GSV_VERSION="4.6.1"`，`GSV_URL=https://download.gnome.org/sources/gtksourceview/4.6/gtksourceview-4.6.1.tar.xz`，`GSV_SHA256=63f487a7ab59612ffa9e7be1883dbd274ab1016afcede3239b93b0fc384df874`。
- 已有实测身份：`evidence/highlight/03-langspec-provenance.txt`
  `verilog.lang` 475 行 sha256 `7670ddfd7b443f5f99c202bfa79de31834c1bd8968ed74c39474dca4f439d464`；
  `systemverilog.lang` 262 行 sha256 `9d1f67411f2674d93f035620ce004da4beca5bbf469eb9955a83c332edb79b04`；
  继承引用在第 256 行 `<context ref="verilog:verilog"/>`。**已核实（本地证据链）**；许可 = LGPL-2.1-or-later（`assets/language-specs/README.md:57`、`THIRD_PARTY_NOTICES.md:15`）。

### E13 — KDE 两个 Verilog/SV 定义的许可原文

镜像 `KDE/syntax-highlighting` @ `2d07614ca2ba8565915cc7e0959fd44745702ca1`（GitHub 镜像 master，`pushed_at 2026-10-30T07:57:08Z`；仓库无 `REUSE.toml`，`data/syntax/*.xml` 共 **414** 个）

`data/syntax/systemverilog.xml`（37,920 B，sha256 `3361d8b4ef62c4868165ef045c02d8b6e85a1e57f61c304b399661bdc06be015`）

```
  5	<!--  This program is free software: you can redistribute it and/or modify -->
  6	<!--  it under the terms of the GNU General Public License as published by -->
  7	<!--  the Free Software Foundation, either version 3 of the License, or    -->
  8	<!--  (at your option) any later version.                                  -->
 ...
 42	<language name="SystemVerilog" version="11" kateversion="5.0" section="Hardware" extensions="*.sv;*.svh" mimetype="text/x-systemverilog-src" author="Sean O'Boyle (seanoboyle@intelligentdv.com)" license="GPLv3+">
```
⇒ 精确表述：**GPL-3.0-or-later**（「either version 3 … or any later version」，不是 `-only`）。文件头 `$Id: systemverilog.xml 10 2009-06-25 03:41:39Z seanoboyle`，作者/公司为 Sean O'Boyle / Intelligent Design Verification。

`data/syntax/verilog.xml`（9,483 B，sha256 `7199e3677b0a942e65b40f95cb2481be062b1ca48eb048519e7d4efc098f5970`）

```
  3	<language name="Verilog" version="8" kateversion="5.0" section="Hardware" extensions="*.v;*.V;*.vl" mimetype="text/x-verilog-src" author="Yevgen Voronenko (ysv22@drexel.edu), Ryan Dalzell (ryan@tullyroan.com)" license="">
```
全文件 `grep -i 'spdx|license|copyright'` 只命中这一行，且 `license=""` ⇒ **该文件没有声明任何许可**（不属于「已核实为某个许可」，而属于「许可未声明」）。
仓库策略对照：`README.md:167` 「Contributions to KSyntaxHighlighting shall be licensed under MIT」，`README.md:169-177` 要求每个文件带 `SPDX-License-Identifier: MIT`。`systemverilog.xml` 是 2009 年政策前的存量文件（GPLv3+），`verilog.xml` 连 SPDX 头都没有 ⇒ **不能拿 README 的 MIT 政策去推定这两个文件的许可**。

### E14 — KDE 定义目录许可混杂（打包面风险）

抽样 `data/syntax/` 8 个定义在同一镜像 commit 上的 `license=` 与 SPDX：

| 文件 | `license=` 属性 | SPDX 头 |
|---|---|---|
| `cpp.xml` | `LGPL` | 无 |
| `java.xml` | `LGPL` | 无 |
| `rust.xml` | `MIT` | 无 |
| `python.xml` | `""`（空） | 无 |
| `verilog.xml` | `""`（空） | 无 |
| `systemverilog.xml` | `GPLv3+` | 无 |
| `go.xml` | `GPLv2+` | 无 |
| `vhdl.xml` | 无该属性 | 无 |

⇒ C2/G7 要求「把 KSyntaxHighlighting 的 syntax 定义随包」时，**不能整目录复制**：必须按 `license=` 白名单（或排除 GPL 系与空值），并把白名单写进打包断言（§5.4）。

### E15 — helix query 文件的许可（顺带闭合的坑）

`helix-editor/helix` @ `0a4432b…` 的 `LICENSE`（16,725 B，sha256 `1f256ecad192880510e84ad60474eab7589218784b9a50bc7ceee34c2b91f1d5`）第 1 行：

```
Mozilla Public License Version 2.0
```
`runtime/queries/verilog/` 下只有 `highlights.scm`（4,469 B）、`injections.scm`（68 B）、`locals.scm`（1,363 B）、`textobjects.scm`（165 B）——**没有 `folds.scm`**。

⇒ 若我们复用 helix 的 query（Lapce 的发布包就是这么做的：`release.yaml` 把 `helix/LICENSE` 复制为 `runtime/queries/helix.LICENSE`），那是 **MPL-2.0 文件级 copyleft**：可放进 MIT 项目，但必须保留这些文件及其 MPL 许可与源码可得性；**我们的折叠 query（A1f 需要）得自己写**（本仓库产出，MIT）。

---

## 3. 许可判定汇总

| 组件 | commit | 许可 | 随包/静态链接结论 |
|---|---|---|---|
| `tree-sitter/tree-sitter-verilog`（Lapce 机制指向的 Verilog/SV grammar） | `4457145e…` | **MIT**（E9） | **可默认编译进 AppImage**；义务 = 随包 MIT 声明（版权行 + 许可全文） |
| `gmlarumbe/tree-sitter-systemverilog`（替代候选） | `08c5e37…` | **MIT**（E11） | 可随包；若换用需重跑 A1f/A1g 回归 |
| Apache-2.0 tree-sitter Verilog/SV grammar | — | **未命中**（E11） | 不存在可选对象 |
| Verible（Apache-2.0，现有 LSP） | `v0.0-4298-g085eef05` | Apache-2.0（`THIRD_PARTY_NOTICES.md:13`） | 已随包；非 tree-sitter 形态 |
| KDE `systemverilog.xml` | 镜像 `2d07614…` | **GPL-3.0-or-later**（E13） | 决定前**不得随包**（§5） |
| KDE `verilog.xml` | 镜像 `2d07614…` | **未声明**（E13） | 同样不得随包（许可状态不明，比 GPL 更糟） |
| helix queries（若复用） | `0a4432b…` | **MPL-2.0**（E15） | 可随包，文件级；须带许可与源码 |
| GtkSourceView `.lang`（LGPL 资产，C7 替代来源） | 4.6.1 tarball | **LGPL-2.1-or-later**（E12） | 数据文件，可移植；派生物需保持 LGPL 并提供源码 |

**关于「静态编译进二进制会迫使整个 AppImage 按 GPL 分发」的精确口径**（供后续任务引用）：

1. 若 grammar 是 **GPL 系 C 代码并静态链接**进 `vd-app`，那么整个 AppImage 是 GPL 的「组合作品」，分发时必须以 GPL 全文 + 完整对应源码分发 ⇒ 与产品自身的 MIT（`LICENSE:1-3`）冲突。
2. 本次取证的 grammar 是 **MIT**，上述风险**不触发**。
3. 若未来在 C15 接缝上换入 GPL grammar：`dlopen` 一个独立 `.so` **在本报告的工程判断中不构成可靠的 copyleft 隔离**（共享地址空间/紧耦合即组合作品）。**这一条是法律判断，需法务确认**；在确认前按最保守口径设计（默认不随包）。唯一保守口径是**不随包**，让用户在运行期自行提供；因此那时「默认不随二进制、由可替换接缝选择」是**必须项**，不是可选项。
4. KSyntaxHighlighting 的 `.sv` **XML 是运行时数据**，不是编译进二进制的代码：它不会被「静态链接」逻辑直接命中，但随包分发 GPL-3.0-or-later 文件仍需附 GPLv3 全文与该文件源码，且属交付集合内的 GPL 组件。风险等级 **中**（不等于「整个 AppImage 被迫 GPL」），但义务边界模糊 + 上游策略自相矛盾 ⇒ 结论：**不随包 KDE 定义**（已裁决 D2=B：改为随包自有的 LGPL-2.1-or-later 移植产物）。

---

## 4. 对 C15 / C18 与构建锁定的落地口径

1. **单一锁文件**：新增 `grammars.lock.toml`（建议置于 `assets/` 或 `build/`，由 syn-grammars 任务落地），字段 `name / git / rev / subpath? / license / license_sha256 / parser_c_sha256?`。构建只读这个文件，**禁止**在代码里写死 grammar 名或 API。
2. **本任务给出的第一条记录**（可直接抄进锁文件）：
   ```toml
   [[grammar]]
   name = "verilog"            # 覆盖 .v .vh .sv .svh（helix languages.toml:2199）
   git  = "https://github.com/tree-sitter/tree-sitter-verilog"
   rev  = "4457145e795b363f072463e697dfe2f6973c9a52"
   license = "MIT"
   license_sha256 = "92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46"
   ```
3. **构建期断言**（`syn-grammars` + `pack-final`）：checkout 后校验 `LICENSE` 的 sha256 与上表一致，不一致即拒绝构建（防上游 force-push/换文件）。
4. **C15 接缝**：`SyntaxProvider` 保持「编译期可选 grammar、运行期可整体关闭」；默认开启（因 MIT）但配置项必须真实存在并以 A1h 断言。
5. **C18**：`libtree-sitter` 与 `libtree-sitter-verilog.so` 都进私有运行时；若改成静态编译，锁文件与断言**不变**（可替换性在配置层保留）。
6. **通知义务**：`THIRD_PARTY_NOTICES.md` 需新增一行（`MIT` + commit + `docs/licenses/` 下随包 MIT 全文），并把 `LICENSE` 原文的版权行原样保留。

---

## 5. C7 处置：`.sv` 底座定义

### 5.1 事实基线（不重证，只引用）

- KDE `systemverilog.xml` = **GPL-3.0-or-later**（E13，文件内原文）；用户已亲自核对。
- KDE `verilog.xml` = **许可未声明**（E13）。
- 本仓库可用的 LGPL 替代：GtkSourceView 4.6.1 `verilog.lang` / `systemverilog.lang`（E12，LGPL-2.1-or-later，pin + sha256 已存在）。

### 5.2 四个选项与代价

| 方案 | 做法 | 许可后果 | 工程代价 | 建议 |
|---|---|---|---|---|
| **A 接受 GPLv3+** | 原样随包 KDE `systemverilog.xml` | 交付集含 GPL-3.0-or-later 组件：需随包 GPLv3 全文 + 该 XML 源码；与产品 MIT 的「整体许可叙事」冲突，需用户书面接受 | **0 工程日** | ✗ 除非用户明确接受 |
| **B 移植 LGPL 定义（推荐）** | 把 `systemverilog.lang`（262 行，含第 256 行跨文件继承）与 `verilog.lang`（475 行）翻译成 KSyntaxHighlighting XML，放在本仓库，声明 LGPL-2.1-or-later 派生 | 无 GPL 组件；LGPL 数据派生物须保持 LGPL 并随包提供该 XML（即源码）与 LGPL 全文 | **1.5–3 人日** + A1g/A1h 回归 | ✓ **已定为 v1 默认（用户 2026-10-31 裁决，ADR D2 = Accepted(B)）** |
| **C v1 只做 `.v`** | 不随包任何 `.sv` 定义；`.sv/.svh` 只由 tree-sitter overlay 处理，底座不着色或退回纯文本 | 无任何 GPL/LGPL 新增义务 | **0 人日**（但功能缺口可见） | ✓ 零风险兜底 |
| **D 自写 SV XML** | 依 IEEE 1800 关键字表 + UVM/`` `ifdef `` 自写 500–900 行 XML，本仓库授权（MIT） | 最干净（可 MIT） | **2–4 人日** | ✓ 长期目标 |
| **E 向上游求再许可** | 联系 Sean O'Boyle / Intelligent Design Verification 请求 MIT/双许可 | 若成功则等同方案 A 且无传染 | 时间不可控 | 记录，不作计划 |

**关键工程细节（方案 B 的坑）**：`systemverilog.lang` 依赖 `<context ref="verilog:verilog"/>`（第 256 行）在 GtkSourceView 内跨语言继承；KSyntaxHighlighting **没有**跨文件 context 引用能力 ⇒ 必须把 Verilog 上下文**展平**进目标 XML（这也是 1.5–3 人日而不是 0.5 人日的原因）。另：KSyntaxHighlighting 是逐行正则 + 上下文栈（C12），移植后的语义上限等于原 `.lang`，不会比它更差。

### 5.3 执行口径（决定前）

1. **`.sv`/`.svh` 不得随包任何 KDE 定义**：`share/ksyntaxhighlighting/syntax/systemverilog.xml` 与 `verilog.xml` 在打包产物中必须不存在。
2. `.sv/.svh` 在决定前**只有两条允许路径**：(a) 映射到我们自有的 LGPL/MIT 定义（方案 B/D 落地后）；(b) 不映射，底座不着色，tree-sitter overlay 独立工作（方案 C）。
3. 若用户明确选择方案 A：在 `docs/decisions/` 记录书面接受 + `THIRD_PARTY_NOTICES` 增加 GPL-3.0-or-later 行 + 随包 `docs/licenses/GPL-3.0.txt` 与 XML 源码。
4. **口径不得靠自觉**：见 5.4 的构建断言。

### 5.4 打包/构建断言（可直接实现）

```bash
# 1) 产物中不得出现 GPLv3+ 的 .sv 定义（决定前）
f="$PAYLOAD/share/ksyntaxhighlighting/syntax/systemverilog.xml"
[ -e "$f" ] && { echo "FAIL: systemverilog.xml bundled before decision"; exit 1; }
# 2) 任何被随包的 .sv 定义必须带已批准的许可头（LGPL-2.1-or-later 或 MIT）
#    并禁止出现 KDE 两份定义的确切 sha256
for x in "$PAYLOAD"/share/ksyntaxhighlighting/syntax/*.xml; do
  case "$(sha256sum "$x" | cut -d' ' -f1)" in
    3361d8b4ef62c4868165ef045c02d8b6e85a1e57f61c304b399661bdc06be015|\
    7199e3677b0a942e65b40f95cb2481be062b1ca48eb048519e7d4efc098f5970)
      echo "FAIL: KDE definition bundled: $x"; exit 1;;
  esac
done
# 3) KDE 定义目录整体白名单化（见 E14：LGPL/MIT/GPLv2+/GPLv3+/空/无属性混杂）
```

---

## 6. 未闭合项（必须由后续任务或用户闭合）

| # | 未闭合项 | 影响 | 建议闭合者 |
|---|---|---|---|
| U1 | **未下载** `grammars-linux-x86_64.tar.zst`（16.9 MB）枚举其内容；且 0.4.6 根本没有对应 release，无法枚举 | 无法断言 0.4.6 发布包里的 grammar 集合与许可附带情况 | syn-grammars（若需要 0.4.5 口径对照） |
| U2 | **未执行** Lapce 二进制；E4/E5/E6 是静态读码结论 | 「运行期下载」是代码事实，不是运行观测 | 无（结论已足够支撑决策） |
| U3 | KDE 证据取自 **GitHub 镜像** `2d07614…`（`invent.kde.org` 抓取受限），镜像可能滞后 | 仅方案 A 相关；**A 已否决 ⇒ 不再阻塞** | — |
| U4 | **A1f 未验收**：1k 行 SV 样本的折叠区间一致性、以及 `4457145e` 对 SV 的实际解析成功率 | 决定「用不用这个 grammar」，不影响「能不能随包」 | 编辑内核/overlay 任务（M1 之后） |
| U5 | **D2 已裁决 = B**（2026-10-31）：`.lang` → KSyntaxHighlighting XML 的移植本身尚未落地（展平 `verilog:verilog` 继承） | 移植完成前 `.sv` 无底座定义 | `syn-kde-data`（T16），口径见 ADR §3 D2 |
| U6 | KDE `data/syntax/` 414 个定义的许可**未逐片清点**（只抽样 8 个） | 整目录随包会混入 GPL/未声明文件 | pack-final 出清单 |
| U7 | 未做方案 E（向上游求再许可）的联系 | **不再需要**（A 已否决） | — |
| U8 | 未验证 `tree-sitter` 运行库版本与本 grammar rev（`tree-sitter 0.20` API）的 ABI 兼容性 | 编译期问题，风险低但需在锁文件落地时实测 | syn-grammars |
| U9 | **T16 冻结契约文本尚未对齐**：其 (c) 仍写「systemverilog 定义在 C7 处置未定前不得随包」；C7=B 已定，严格按原文执行会把「随包自有 LGPL `.sv` 定义」判为违规 | 合规产物可能被 verifier 判 FAIL | captain / cell-planner（给 verifier 书面口径或更新契约） |

---

## 7. 复现

```bash
# 逐条断言本报告的 sha256 与关键字符串（需要网络；不写入交付树）
bash evidence/r2-license/verify.sh            # 期望 exit 0，输出见 evidence/r2-license/verify.log
```

`verify.sh` 覆盖（断言清单即事实，逐项可失败；**51 条**，含 3 条负控）：
- **E1**：`git ls-remote 'refs/tags/v0.4.6^{}'` = `b012cef…`（标签 peel）；
- **E3**：拉取 `Cargo.lock`（193,528 B）→ sha256 + `^name = "tree-sitter` **计数 == 1** + `tree-sitter-verilog` / `tree-sitter-systemverilog` **缺席**；
- **E4/E5**：`grammars.rs` 与 `language.rs` 的 sha256 + 行号片段（下载端点、`cmp_precedence == Equal`、`grammar: None`、Verilog 用 DEFAULT）；
- **E2**：workspace 版本 `0.4.6`；
- **E6**：`tree-sitter-grammars` 的 tags/releases 中无 `v0.4.6`；
- **E7/E8**：`main.rs` 的 helix pin + `languages.toml` 的 verilog `file-types` 与 grammar rev + 全文无 `systemverilog` 字样；
- **E9**：`tree-sitter-verilog` 的 LICENSE/Cargo.toml/package.json/grammar.js 的 sha256 与 MIT 原文（版权行 + 通知义务句）；
- **E11**：`gmlarumbe/tree-sitter-systemverilog` 的 LICENSE/Cargo.toml/grammar.js 的 sha256 与 MIT 原文；
- **E12**：本地 LGPL 资产的三条 pin（`assets/language-specs` 只有 README.md；`versions.env` 的 GSV sha256；provenance 里的 `systemverilog.lang` sha256）；
- **E13**：KDE 两份定义的 sha256 + `license="GPLv3+"` / `license=""` + 仓库贡献政策文本；
- **E15**：helix LICENSE 的 sha256 与 MPL-2.0 头；
- **负控（3 条）**：用合成的 lock 夹具证明「计数 == 1」与「缺席」两条断言真能失败、干净夹具上不误报。

任一条不符即非零退出。

> 替代候选的准确表述（更正早前的过度概括）：**MIT grammar 候选有 1 个** —— `gmlarumbe/tree-sitter-systemverilog@08c5e37…`；**Apache 路线的替代不是 tree-sitter grammar**（GitHub 仓库检索 0 命中），而是 **Verible（Apache-2.0，非 tree-sitter 形态的结构性替代，估 2–4 人周另写后端）**。详见 E11。
