# T114 — 逐 code 审计：还有哪些“可选资源缺失”被升级成 warning？

审计范围：`crates/vd-config` 里所有 `WarningCode` 的**产生点**（`warnings.push` / `Warning::new`），
逐一判断“是不是可选资源缺失”。

| code | 产生点 | 语义 | 判定 |
|---|---|---|---|
| `filelist.unknown_token` | `filelist.rs` | 用户写的 `.f` 行无法识别 | 真实/可行动 → **保留** |
| `filelist.cycle` | `filelist.rs` | 嵌套 `-f` 成环 | 真实/可行动 → **保留** |
| `filelist.depth_exceeded` | `filelist.rs` | 嵌套深度超限 | 真实/可行动 → **保留** |
| `filelist.missing` | `filelist.rs`, `project.rs`（配置的 filelist） | 用户**显式配置/引用**的 `.f` 不存在 | 真实/可行动 → **保留**（captain 明确要求） |
| `filelist.unreadable` | `filelist.rs` | `.f` 存在但读不了 | 真实/可行动 → **保留** |
| `filelist.define_invalid` | `filelist.rs` | `+define+` 语法非法 | 真实/可行动 → **保留** |
| `filelist.duplicate_entry` | `filelist.rs` | 重复条目被丢弃 | 真实/信息性 → **保留** |
| `rules.search_stopped` | `project.rs::discover_rules`（本次前） | **可选** `.rules.verible_lint` 缺失 | **本次修复：正常路径不再产生**（枚举保留） |
| `config.parse_failed` | `lib.rs` | `vedit.toml` 语法错误 | 真实/可行动 → **保留** |
| `config.unknown_key` | `lib.rs` | 未知键 | 真实/可行动 → **保留** |
| `config.invalid_value` | `lib.rs`, `project.rs` | 类型对但值不可用 | 真实/可行动 → **保留** |
| `xdg.invalid` | `lib.rs` | XDG 环境变量误配 | 真实/可行动 → **保留** |
| `rules.unreadable` | `project.rs::discover_rules`（本次新增） | 规则文件**存在但不可读** | 真实/可行动 → **保留（新）** |

**结论**：所有 `WarningCode` 里，只有 `rules.search_stopped` 属于“**可选资源缺失**”这一类，
本次已从正常路径移除。其余都是“用户显式配置/书写的内容有问题”或“声明的东西不可用”，
保持 warning 是正确的。

**另查过、不需要动**：
- `vedit.toml` 缺失：不产生 warning（discovery 只是没找到根标记；loader 用默认值）。
- `.verible.hld`：仓库里没有它的处理路径（`grep -rn "verible.hld" crates/ src/` = 无命中）。
- `crates/vd-ffi/src/lsp.rs:81` 的 `.rules.verible_lint not found at <abs path>`：这是**调用方显式
  传入一个不存在的 rules 路径**时的 Error，语义上正确；且 `vd_lsp_configure` 在 `src/` 里**没有调用者**
  （只有 Rust 测试/example），**当前 GUI 不可达** → 按 captain 指示**不改**，只登记。
- 工具链缺失（`verible-verilog-ls`/`slang`）：T85 已按“中性状态 + 影响面 + 修复方式”处理，不需要动。
