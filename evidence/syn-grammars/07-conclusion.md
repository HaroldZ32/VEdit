# syn-grammars (T17) 结论：Verilog/SystemVerilog grammar 绑定 + 三类 query

**结论：交付成立。** 交付用的 grammar 已内置、绑定进 T15 的 `SyntaxProvider` 接缝，
`highlights / folds / structure` 三类 query 在真实 `.v` 与 `.sv` 样例上逐条断言通过；
四种配置（`none` / `verilog` / `systemverilog` / `verilog-static`）configure+build 全部
exit 0，三种带 grammar 的构建各 **79/79 CHECK PASS**（ctest exit 0、测试二进制 exit 0）；
换 grammar 的源码 manifest 四次完全相同（C15）；grammar 为 **MIT**，provenance 已落
`third_party/tree-sitter/PROVENANCE.md`。

- Board task: **T17** · 契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/syn-grammars.md`
- 交付源码: `src/syntax/grammars/`（binding + CMake + 验证器 + 样例）
- 内置 grammar: `third_party/tree-sitter/verilog/`（`tree-sitter/tree-sitter-verilog` @
  `4457145e795b363f072463e697dfe2f6973c9a52`，MIT）
- queries: `resources/queries/{verilog,systemverilog}/{highlights,folds,structure}.scm`
- 构建镜像: `vedit-qt6-build:local`（Ubuntu 26.04.1 / Qt 6.10.2 / KF6 6.24.0 /
  tree-sitter **0.25.9** / g++ 15.2.0 / cmake 4.2.3，见 `00-pins.txt`）
- 重跑：`bash src/syntax/grammars/scripts/run-evidence.sh`（在固定镜像内，见 §8）

## 1. 构建与单测（观察到的命令与退出码）

| 变体 | 配置 | configure+build | ctest | 测试二进制 |
|---|---|---|---|---|
| `none` | `-DVEDIT_SYNTAX_GRAMMAR=none -DVEDIT_GRAMMAR_{VERILOG,SYSTEMVERILOG}=OFF` | exit 0（`01-build.log:34`） | 不跑（无 grammar） | — |
| `verilog` | `-DVEDIT_SYNTAX_GRAMMAR=verilog` / shared | exit 0（`:72`） | **exit 0**（`02-tests.log:12`） | **exit 0，79/79**（`:105`） |
| `systemverilog` | `-DVEDIT_SYNTAX_GRAMMAR=systemverilog` / shared | exit 0（`:110`） | **exit 0**（`02-tests.log:117`） | **exit 0，79/79**（`:210`） |
| `verilog-static` | 同上 + `-DVEDIT_SYNTAX_TS_LINK=static`（C18） | exit 0（`:149`） | **exit 0**（`02-tests.log:222`） | **exit 0，79/79**（`:315`） |

`09-result.txt` = `failures=0`。仓库级 gate 说明：`AGENTS.md` §4 的 cargo 系列针对
Rust/GTK 旧线，Qt6 线的门是 `build/gates/qt6-cxx.sh`；本 lane 尚未被根 `CMakeLists.txt`
纳入（与 T15/T16 同一状态，见 §6 第 5 条），因此本 lane 的门禁就是上面的独立构建 + ctest
（证据脚本 `run-evidence.sh` 的退出码即 gate 退出码）。

## 2. 契约 (a)–(d) 逐条对照

### (a) 许可：MIT，可默认编入交付二进制

- 依据 `evidence/r2-license/report.md` §0.3：Lapce 机制解析到的唯一 Verilog grammar 是
  `tree-sitter/tree-sitter-verilog` @ `4457145e…`，`LICENSE` 为 **MIT**（© 2018-2021
  Aliaksei Chapyzhenka），非 copyleft；顺带纠正了「Lapce 0.4.6 vendor 过 grammar」的前提。
- 实测（`06-licensing.txt`）：`LICENSE` 原文前 8 行、5 个随包文件 sha256、`LICENSE` 与
  `grammar.js` 中**无任何 GPL/LGPL 标记**；通知义务由 `PROVENANCE.md` §2 记录，
  `THIRD_PARTY_NOTICES.md` 留给 pack-final 汇总（本任务未改）。
- 编译期开关存在（`VEDIT_GRAMMAR_VERILOG` / `VEDIT_GRAMMAR_SYSTEMVERILOG`），
  因此「GPL 系必须默认 OFF」的规则有执行点；本 grammar 是 MIT，默认 ON。

### (b) 注册：CMake option + 注册表 + 锁定 commit/版本

- 注册表：`GrammarRegistry`（T15 的 `grammar_binding.h`），grammar lane 用
  `VEDIT_SYNTAX_REGISTER_GRAMMAR(verilog|systemverilog, factory)` 注册；另外导出
  `registerBuiltinGrammars()` 供集成方显式调用（静态库按需拉取 TU 的可靠入口，见 README）。
- 选择：`-DVEDIT_SYNTAX_GRAMMAR=` 烘焙本构建默认 id；`-DVEDIT_GRAMMAR_*` 决定 binding
  是否编译/注册；配置期做一致性校验（选中的 id 若对应 option OFF 则 FATAL）。
- 锁定：binding 的 `version` = 上游 commit `4457145e…`，`license` = `MIT`，并写进
  **构建清单** `grammars_manifest.txt`（`03-manifest.txt`：每个变体的 id/enabled/
  license/version/parser sha256/6 个 query sha256）。`parser.c` 的 ABI 标记
  `#define LANGUAGE_VERSION 13` 记录在 `00-pins.txt:14`；tree-sitter 0.25.9 实测接受
  （两个样例 `hasSyntaxErrors=0`）。

### (c) 三类 query；节点类型只出现在 queries 与绑定层

- `resources/queries/verilog/{highlights,folds,structure}.scm` 与
  `resources/queries/systemverilog/{…}.scm` 各 3 个，全部由本 lane 自写（**没有**复制
  helix 的 MPL-2.0 query；`folds.scm` 按 captain 要求独立自写，与 T16 的
  KSyntaxHighlighting XML 折叠区是两条独立来源）。
- 分层证据（`05-layering.txt`）：
  - provider 的 4 个公共头文件 grep `TS[A-Z]…|tree_sitter|TSNode|TSQuery|node_type`
    → **无匹配（exit 1）**；
  - 真正 `#include <tree_sitter/api.h>` 的翻译单元只有 `src/syntax/provider/tree_sitter_provider.cpp`；
  - `grep -rn 'module_declaration|interface_declaration|class_declaration' src/syntax/grammars`
    → **无匹配（exit 1）**，即绑定层连节点类型名都不出现；
  - 节点类型只出现在 4 个 query 文件（+ 内置 grammar 自带的 `node-types.json`）。

### (d) provenance

- `third_party/tree-sitter/PROVENANCE.md`：上游仓库 + commit + 日期、MIT 全文所在位置、
  每个随包文件的字节数/sha256、**未内置**文件与原因（`grammar.json` 7.7 MB 生成物、
  bindings/corpus/examples/CI 元数据）、更新步骤、C15 关系、许可义务。
- 自写 grammar 的对照记录：`probes/c15-seam/grammars/verilog_placeholder/` 只是探针用
  子集，不进入交付路径（PROVENANCE §1.2）。

## 3. Query 覆盖（真实样例上的断言，`02-tests.log`）

样例是**提交进仓库**的 `src/syntax/grammars/tests/data/sample.v`（58 行：模块/参数/
always/case/function/task/generate/例化/`$display`）与 `sample.sv`（50 行：package/
typedef enum/interface/class（构造 + 方法）/task/always_ff/always_comb/case/generate）。

| 维度 | Verilog 样例 | SystemVerilog 样例 |
|---|---|---|
| highlight spans | **117**（keyword 53 / number 26 / operator 32 / type 3 / comment 1 / string 1 / function.builtin 1） | **112**（keyword 63 / number 26 / operator 15 / type 5 / comment 1 / string 1） |
| folds | **12**（module×2 / function / task / generate / case / 6 个 begin 块，全部多行） | **12**（package / interface / class / 构造 / function / task / module / generate / case / 3 个 begin） |
| structure | 4 个条目：`counter, mask, pulse, sub_module` | 7 个条目：`alu_pkg, alu_if, alu_driver, (构造函数), set_op, drive, alu_top` |
| breadcrumbs | `module:counter > function:mask` | `class:alu_driver > function:set_op` |

fold 断言的判据是**文本驱动**的：对每个期望构造，取源文本里 open/close 关键字所在行，
断言 `foldRanges()` 里存在同起止行的区间；并且逐条断言「起点行含开符号、终点行含闭符号、
end > start、不越界」（`foldsLookSane`）。`queryRanges(tree,"highlights")` 与
`highlightSpans()` 的条数一致（117 vs 117），`queryIds()` 恰为 3 个。

## 4. C15 换装证明（`04-swap-proof.txt`）

- 四次配置（none / verilog / systemverilog / static）的 **源码 manifest 完全相同**
  （`src/syntax/grammars` + `resources/queries` + `third_party/tree-sitter` 全量 sha256，
  diff exit=0）→ 换 grammar 没有修改任何 tracked 源文件。
- 差异只在**构建产物**：`generated/grammars_config_generated.h` 的 enabled/query 内容，
  以及 CMake cache 里 `VEDIT_SYNTAX_GRAMMAR` 与 `VEDIT_GRAMMAR_*` 这几项（`04-swap-proof.txt`）。
- 运行时开关（A1h 接缝）也在验证器里断言：`setTreeSitterEnabled(false)` →
  `createProvider()==nullptr` 且 `available()==false`；恢复后重新可创建。

## 5. 分层与 owner（C16/A1g 关系）

本 lane 只产出 span/range，不做着色、不持 owner、不做仲裁；A1g 的「逐 range 单一 owner」
仍由 T15 的 `OwnerArbiter`（`POLICY.md`）执行。folds 作为独立来源提供给折叠视图（T19），
与 KSyntaxHighlighting XML 折叠区并列保留（A4）。

## 6. 已知限制与残余风险（如实列出）

1. **内置 `parser.c` 体积**：46,042,888 字节（上游生成物，`LANGUAGE_VERSION 13`）。
   这是选择上游完整 MIT grammar 的直接代价；换来的是真实 Verilog/SV 解析、无需自写
   子集 grammar、provenance 可逐字节核对。构建实测很快（parser.c 编译约 3 s/次）。
2. **上游 grammar 是 2022 年的**：`.v/.vh/.sv/.svh` 共用同一份 grammar（与 helix/Lapce
   机制一致），2022 之后的 SV 新构造可能不在其覆盖内；升级是**数据变更**（替换 5 个
   文件 + 更新 PROVENANCE sha256 + 重跑证据），C15 配置面不变。
3. **类构造函数**在上游是独立节点（`class_constructor_declaration`），structure 里以
   `function` 条目出现、没有 name 捕获（breadcrumb 名字为空）；已写入 README 的「已知限制」。
4. **folds 粒度是整行区间**：嵌套 begin 块会各自产生 fold（58 行样例出 12 个），
   嵌套的展示方式由折叠视图（T19）决定；provider 只丢弃单行区间与完全重复区间。
5. **根构建尚未纳入本 lane**：`CMakeLists.txt`（根）只自动加 `src/app`，`src/syntax/{provider,kde,grammars}`
   的接入片段在各自 README（本任务不改根构建）。在 build-base lane 落地前，仓库级
   gate 不覆盖本 lane；证据用的是与 T15/T16 相同的独立构建。
6. **`.v` → "V"(vlang) 陷阱**：本 lane 按 binding id（`verilog` / `systemverilog`）选择，
   不做文件名查询，因此该陷阱不在此层；编辑器侧扩展名→binding id 的映射仍由 T16 的
   loader 按名字钉住（`definitionForName`）。
7. 未测：超大文件（≥10 万行）下 query 执行的耗时、以及多视图/并发解析。T59 的注入成本
   结论属于 overlay 层，不覆盖本 lane。

## 7. 证据清单

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 镜像/版本、内置 grammar 5 文件 sha256、query sha256、ABI 标记、源码 manifest |
| `01-build.log` | 四个变体的 configure+build 全量输出与退出码 |
| `02-tests.log` | 三个变体的 ctest + 测试二进制全量输出（各 79 CHECK） |
| `03-manifest.txt` | 四个变体的构建清单（哪些 grammar 被编入、许可、版本、query sha256） |
| `04-swap-proof.txt` | 换装证明：生成头/cache 差异 + 源码 manifest 四次相同 |
| `05-layering.txt` | 分层纯度 grep（provider 接口无 grammar 类型；绑定层无节点类型） |
| `06-licensing.txt` | MIT 原文、sha256、无 copyleft 标记、provenance 行数 |
| `09-result.txt` | `failures=0` |
| `07-conclusion.md` | 本文件 |

## 8. 复跑命令

```bash
# 在固定镜像内（host 侧 wrapper；镜像 id 只作定位，锚点是 versions-qt.env + 锁文件）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash -c 'bash src/syntax/grammars/scripts/run-evidence.sh'
# 期望：exit 0；evidence/syn-grammars/09-result.txt = "== RESULT: failures=0 =="
```

单独开发/调试：

```bash
cmake -S src/syntax/grammars -B target/syn-grammars/dev -G Ninja \
      -DVEDIT_SYNTAX_GRAMMAR=verilog -DVEDIT_SYNTAX_TS_LINK=shared
cmake --build target/syn-grammars/dev --parallel
ctest --test-dir target/syn-grammars/dev --output-on-failure
```
