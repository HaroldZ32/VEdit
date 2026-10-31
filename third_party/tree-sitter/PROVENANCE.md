# Provenance — tree-sitter grammars shipped with VEdit

本文件是 `third_party/tree-sitter/` 的许可与来源账本（契约 `syn-grammars.md` (d)）。
`THIRD_PARTY_NOTICES.md`（交付汇总）由 `pack-final` 统一生成，本任务**不直接改**它。

## 1. 交付树里实际存在的 grammar

### 1.1 `verilog/` — tree-sitter-verilog（上游 MIT，已内置）

| 项 | 值 |
|---|---|
| 上游仓库 | `https://github.com/tree-sitter/tree-sitter-verilog` |
| commit | **`4457145e795b363f072463e697dfe2f6973c9a52`**（2022-09-07） |
| 覆盖文件 | `.v` `.vh` `.sv` `.svh`（上游没有独立的 SystemVerilog grammar；KTextEditor/编辑器侧按扩展名映射到本 grammar） |
| 许可 | **MIT**，`LICENSE` 全文随包（© 2018-2021 Aliaksei Chapyzhenka） |
| 生成物 ABI | `src/parser.c` 由上游生成，`LANGUAGE_VERSION 13`；本仓库实测 tree-sitter 运行时 **0.25.9** 接受并正常解析（最小兼容版本 13） |
| 为何选它 | `evidence/r2-license/report.md` §0.3 认定它是 Lapce 机制解析到的**唯一身份**；MIT 与静态编入单一二进制兼容，只需保留通知（本文件 §3） |

随包文件与 sha256（`sha256sum` 原文；复核命令：`sha256sum -c` 见 `evidence/syn-grammars/06-licensing.txt`）：

| 文件 | 字节 | sha256 |
|---|---|---|
| `verilog/LICENSE` | 1082 | `92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46` |
| `verilog/grammar.js` | 133684 | `323c121a3e550df0f970d46c544b3f02485e5e6d754acb730d6be7b8dea7dc4b` |
| `verilog/src/parser.c` | 46042888 | `a15f78b82068daeb62f3b54c8ad0101aa936a2c921a57806fe8f37dcbf0bfff2` |
| `verilog/src/tree_sitter/parser.h` | 5378 | `ab104936984904469572a4e868149f7a22fb2929347f837ae6a1f9b790f1b173` |
| `verilog/src/node-types.json` | 271904 | `e99b42baf4279d3be6dec59129dda0789f196b4e5c99144a781ea6cdf255425c` |

**未内置**（有意省略，保持交付树体积可控并避免把生成物当源文件）：

- `src/grammar.json`（7.7 MB，`grammar.js` 的生成物，可由 `grammar.js` + tree-sitter CLI 重建）；
- `bindings/`、`Cargo.toml`、`binding.gyp`、`package.json`（Node/Rust 绑定，本项目不用）；
- `corpus/`、`examples/`（上游测试语料，不参与构建）；
- `.github/`、`.travis.yml`、`cache.txt`（CI/缓存，不参与构建）。

**更新方式**（只改数据，不改代码）：

```sh
git fetch --depth 1 https://github.com/tree-sitter/tree-sitter-verilog <new-commit>
# 复制 5 个文件，更新本文件的 sha256 表，然后重跑
bash src/syntax/grammars/scripts/run-evidence.sh
```

生成 parser.c 需要 tree-sitter CLI（0.27.1 可重建；上游 parser.c 是 v0.20 时代生成，ABI 13）。
**构建不需要 CLI**：`parser.c` 已内置，官方构建镜像里只需 C 编译器。

### 1.2 自写 grammar（C15 探针用，不在交付路径）

`probes/c15-seam/grammars/verilog_placeholder/` 是 C15 接缝探针的**自写 MIT 子集** grammar
（module/net/assign/always/begin 等），仅用于证明「换 grammar = 换配置」。它**不被**
`src/syntax/grammars/` 引用，也不进交付二进制；其 provenance 见该目录自己的 `PROVENANCE.md`。

## 2. 许可判定与义务

- MIT（`LICENSE`）要求：随包分发时保留版权行与许可全文。本目录 `verilog/LICENSE` 即全文，
  交付汇总时由 `pack-final` 把它并入 `THIRD_PARTY_NOTICES.md`（本任务不直接改该文件）。
- 该 grammar 只被**静态编译**进 `libvedit_syntax_grammars`（`verilog/src/parser.c`），
  没有运行期下载、没有 `.so` 加载、没有动态链接；`VEDIT_GRAMMAR_*` 编译期开关可整体关闭它。
- 与本项目其它许可无冲突：grammar 是 MIT，KTextEditor/KSyntaxHighlighting 的 KDE 数据
  另走 `resources/syntax/`（LGPL-2.1-or-later，见 `evidence/syn-kde-data/` 与
  `docs/decisions/r2-license.md`）。两者互不替代。

## 3. C15 关系（换 grammar 只是配置）

`src/syntax/grammars/CMakeLists.txt` 把本目录当作**数据 + 注册**：grammar id、版本、
许可与三类 query 全部在配置期从本目录/`resources/queries/` 读出，生成
`grammars_config_generated.h`。换 grammar 只改 `-DVEDIT_SYNTAX_GRAMMAR=<id>`
（或关闭 `-DVEDIT_GRAMMAR_<ID>=OFF`），**不改任何源码**——见
`evidence/syn-grammars/04-swap-proof.txt` 的源码 manifest 相等证明。
