# src/syntax/grammars — 具体 grammar 绑定 + queries（T17）

把交付用的 Verilog/SystemVerilog grammar 接进 T15 的 `SyntaxProvider` 接缝。本目录**不**
实现语言解析，也不碰 overlay/开关/provider 接口；它只做三件事：

1. 把内置的 tree-sitter grammar（`third_party/tree-sitter/verilog`，MIT，
   `4457145e795b363f072463e697dfe2f6973c9a52`）以**不透明函数指针**交给 provider；
2. 把 `resources/queries/{verilog,systemverilog}/{highlights,folds,structure}.scm`
   作为逻辑 query 注入 `GrammarBinding`；
3. 用 `GrammarRegistry` 注册 `verilog` / `systemverilog` 两个 binding，由
   `-DVEDIT_SYNTAX_GRAMMAR=<id>` 决定本构建默认激活哪一个。

| 文件 | 作用 |
|---|---|
| `bindings.cpp` / `bindings.h` | 唯一命名 grammar 的翻译单元：C 入口点 + query 源 + 注册。**不出现任何节点类型**。 |
| `CMakeLists.txt` | 编译期开关、query 嵌入、构建清单、单测。 |
| `tests/test_syn_grammars.cpp` | lane 验证器（ctest）：解析样例、断言三类 query 的命中/fold 行区间/breadcrumb 路径、binding 身份与许可、运行期开关。 |
| `tests/data/sample.v` / `sample.sv` | 提交进仓库的样例（Verilog 计数器 + FSM；SV package/interface/class/always_ff）。 |
| `scripts/run-evidence.sh` | 在固定镜像内一键重跑全部证据（`evidence/syn-grammars/`）。 |

## 构建 / 测试（独立构建，固定镜像内）

```sh
cmake -S src/syntax/grammars -B target/syn-grammars -G Ninja \
      -DVEDIT_SYNTAX_GRAMMAR=verilog -DVEDIT_SYNTAX_TS_LINK=shared
cmake --build target/syn-grammars --parallel
ctest --test-dir target/syn-grammars --output-on-failure
```

## 编译期开关（C15：换 grammar = 换配置）

| option | 值 | 作用 |
|---|---|---|
| `VEDIT_SYNTAX_GRAMMAR` | `none` / `verilog` / `systemverilog` | 烘焙进本构建的默认 grammar id |
| `VEDIT_GRAMMAR_VERILOG` | `ON`/`OFF` | 编译并注册 Verilog binding（跟随的 `parser.c` 也只在有 binding 时编译） |
| `VEDIT_GRAMMAR_SYSTEMVERILOG` | `ON`/`OFF` | 编译并注册 SystemVerilog binding |
| `VEDIT_GRAMMAR_BUILD_TESTS` | `ON`/`OFF` | lane 验证器（需要 tree-sitter 后端） |

`-DVEDIT_SYNTAX_GRAMMAR=none` 且两个 `VEDIT_GRAMMAR_*=OFF` 时本目录不编译 grammar，
构建里没有任何语法引擎（供「干净回退」与换装证明使用）。**换 grammar 不修改任何 tracked
源码**：id、许可、版本、query 内容都在配置期生成到 `generated/grammars_config_generated.h`
与 `grammars_manifest.txt`（构建清单，见 `evidence/syn-grammars/03-manifest.txt`）。

## 与根构建的集成（build-base lane 落地，本任务不改根 CMake）

```cmake
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/syntax/grammars/CMakeLists.txt")
  add_subdirectory(src/syntax/grammars)
endif()
```

provider 目录若尚未被根构建加入，本目录会自行 `add_subdirectory(../provider)`，因此
独立构建与集成构建两种方式都成立。

## 分层与 C16/A1g

- 节点类型（`module_declaration` 等）**只**出现在 `resources/queries/*.scm`；绑定层与
  provider 接口都不命名它们（`evidence/syn-grammars/05-layering.txt` 的 grep 证明）。
- `folds.scm` 是**独立来源**：与 T16 的 KSyntaxHighlighting XML `beginRegion/endRegion`
  折叠区互不替代（A4），两边都保留。
- 着色 owner 仲裁不在这里：本 lane 只产出 span/range，owner 由 T15 的
  `OwnerArbiter`（POLICY.md）决定。

## 许可 / 来源

`third_party/tree-sitter/PROVENANCE.md` 是账本：上游 commit、MIT 全文、每个随包文件的
sha256、未内置的文件与原因、更新步骤。`THIRD_PARTY_NOTICES.md` 由 `pack-final` 汇总，
本 lane 不直接改它。

## 已知限制

- 上游没有独立的 SystemVerilog grammar：`.v/.vh/.sv/.svh` 共用同一份 grammar
  （与 helix/Lapce 机制一致），因此 SV 的**新**构造覆盖取决于上游 2022 年那份 grammar。
- `parser.c` 是上游生成物（`LANGUAGE_VERSION 13`），本仓库实测 tree-sitter 0.25.9 兼容；
  重新生成需要 tree-sitter CLI 0.27.1，**构建本身不需要** CLI。
- folds 的分组粒度是「整行区间」，单行构造会被 provider 丢弃（`FoldRange` 语义）。
- 类构造函数在上游是独立节点（`class_constructor_declaration`），structure 里以
  `function` 条目出现、没有 name 捕获（breadcrumb 名字为空）。
