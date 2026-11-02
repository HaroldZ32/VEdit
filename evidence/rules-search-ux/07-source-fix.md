# T114 — 源头修复：可选 `.rules.verible_lint` 缺失不再是 diagnostics

## 缺陷链路（before）

```
crates/vd-config/src/project.rs::discover_rules()   ← 找不到时 push Warning{rules.search_stopped, message: "no … found … search stopped at <abs path>"}
crates/vd-ffi/src/project.rs:98                     ← format!("{}: {}: {}", code, file.display(), message)
src/workspace/project/project_service.cpp:30        ← payload.diagnostics = toList(info.warnings)
src/workspace/project/project_model.cpp:239         ← state_.diagnostics = …
src/workspace/project/project_panel.cpp             ← "Diagnostics (N)" 列表
```

用户看到：`rules.search_stopped: /home/haroldzhao: no .rules.verible_lint found from the project root upward; search stopped at /home/haroldzhao`

## 修复（after）

1. **源头不再发出**（`crates/vd-config/src/project.rs::discover_rules`）：
   - **不存在** → 搜索继续/结束，**不 push 任何 warning**（可选文件缺失=正常，verible 用内置规则）。
   - **存在但不可用**（不是可读的普通文件：权限/IO/同名目录）→ 仍然告警，但改用**新增**
     `WarningCode::RulesUnreadable`（`rules.unreadable`）：
     - `file` 字段为 `None`（不让绝对路径进 GUI 投影）
     - 消息用**相对工程根**的文件名（祖先目录则只写文件名），文案：
       `the Verilog lint rules file .rules.verible_lint could not be read; falling back to built-in rules`
       —— 明确告诉用户**功能仍然可用**（回落内置规则），符合 T85 口径。
2. **枚举保留**：`WarningCode::RulesSearchStopped` 仍在库里（`--print-config` 机器契约形状不变），
   正常路径不再产生；`RulesUnreadable` 是**追加**的变体（兼容变更）。
3. **纵深防御**（`crates/vd-ffi/src/project.rs`）：GUI 投影再过滤一次 `rules.search_stopped`
   —— 即使面对旧/pinned 的 service 实现，也不会把这条发现过程信息送进面板；机器契约不受影响。
4. **`$HOME` 边界不动**：`discover_rules` 的 doc 注释原本就写明
   *“The search stops at `$HOME` when `root` lives under it, otherwise at the filesystem root,
   so it can never walk out of the user's tree.”* —— 这是**安全边界，不是 bug**。
   同一说明已补进 `AGENT.md`（"Project discovery: optional files are not errors (T114)"）。
5. **不改** `crates/vd-ffi/src/lsp.rs:81`（显式传入的 rules 路径不存在→Error）：`vd_lsp_configure`
   在 `src/` 无调用者，**当前 GUI 不可达**，只登记（见 08-per-code-audit.md）。

## 新增测试（控制组）

| 测试 | 位置 | 断言 |
|---|---|---|
| `missing_optional_rules_file_is_silent` | `crates/vd-config/src/project.rs` | 无 rules → found=None、`warnings.is_empty()` |
| `unreadable_rules_file_is_reported_without_an_absolute_path` | 同上 | 同名目录 → 1 条 `rules.unreadable`、`file.is_none()`、消息含 "could not be read"+"falling back to built-in rules"、不含工程绝对路径、不含 `/home/` |
| `a_present_rules_file_is_found_without_warnings` | 同上 | 有 rules → adopted、无任何噪声 |
| `chmod_000_rules_file_hits_one_of_the_two_expectations` | 同上 | root 容器 → 可读分支（found+无 warning）；普通账户 → unreadable 分支；两条都断言，不静默 skip（打印 `chmod-000 readable_by_this_process=…`） |
| `rules_search_stops_at_the_home_boundary` | 同上 | 假 home 下的工程 → `stop == home`（不是 `/`）、无 warning |
| `missing_optional_rules_file_is_not_a_panel_diagnostic` | `crates/vd-ffi/src/project.rs` | C ABI 投影里没有 `rules.*`，也没有 `/home/` |
| `a_present_rules_file_produces_no_panel_noise` | 同上 | 有 rules → warnings 全空 |
| `actionable_filelist_warnings_still_reach_the_panel` | 同上 | `+broken+token` → `filelist.unknown_token: …:1:` 仍在 |
| `unreadable_rules_file_still_warns_without_an_absolute_path` | 同上 | `rules.unreadable` 到面板、路径自由 + 回落文案 |

## 用户可见结论

这条消息**不会再出现在 GUI 上**：普通工程（无 `.rules.verible_lint`）的 “Diagnostics (N)” 列表里
不会再有任何 `rules.*` 条目；即使打开 `--print-config` 的机器 JSON，也看不到家目录路径。
只有“规则文件存在但读不了”这一真实问题时，面板才会出现一条**不带路径**、并说明**已回落内置规则**的提示。
