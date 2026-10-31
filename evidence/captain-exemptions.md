# Captain exemptions — 写域口头授权登记表

generated: 2026-10-31T11:53:00Z
revised: 2026-10-31T11:57:00Z（补认第 7 条；登记双 lane 共写）
git_head: 95c496e682e3841add1a081edd78d29c34957aac
purpose: 本表登记 captain 口头授权的「写域外改动」。没有这张表，下一位 verifier 会按字面契约把这些改动判成越界 FAIL。
authority: captain（cell `cell-20261031T085543161Z`），经 cell mail 授权；本文件由 Lead（T16）**依据邮件记录整理落盘**，非 captain 本人签署。

---

## 已登记的豁免（7 条）

| # | 任务 / lane | 路径 | 实际改动（本次实测） | 契约写域之外的哪一条 | 授权 |
|---|---|---|---|---|---|
| 1 | T6 / Senior Engineer 3（工程模型 + `.f`） | `src/app/CMakeLists.txt` | `+16 / −1`（`git diff --numstat`）——lane 子目录列表加入 `project` | `contracts/project-model.md` 所列四条写域之外 | captain 2026-10-31T10:34:34Z 授权 |
| 2 | T6 / Senior Engineer 3 | `src/workspace/integration.cpp` | `guarded include` + `installProject`（T6 部分 `+6 / −0`） | 同上 | captain 2026-10-31T10:34:34Z 授权 |
| 3 | T59 / Senior Engineer 4（诊断） | `src/app/CMakeLists.txt` | 同文件内再加 `diagnostics` 到 lane 列表 | 同上 | captain 授权（cell mail） |
| 4 | T59 / Senior Engineer 4 | `src/workspace/integration.cpp` | 同文件内再加 `installDiagnostics`（合计当前 `+12 / −0`） | 同上 | captain 授权（cell mail） |
| 5 | T43 / Junior Engineer | `evidence/ws-filetree/fix-evidence-c9-REPORT.md` | 批量 C9 报告改放此路径（16173 bytes，实测存在） | 契约原路径在写域外 | captain 裁定 |
| 6 | T59 | `crates/vd-ffi/include/vd_ffi.h` | `+398 / −0`（cbindgen 生成物，由 `crates/vd-ffi/build.rs` 每次构建重写） | 生成物不在人工写域；**保留不 revert** | captain 授权（cell mail） |
| **7** | **T10（编辑内核）** | **`src/app/CMakeLists.txt`** | `add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../editor" …)` + `target_link_libraries(vedit-app PRIVATE vedit_editor_core)` + `target_compile_definitions(vedit-app PRIVATE VEDIT_HAVE_EDIT_CORE=1)`（约 `+15` 行） | T10 契约的写域不含 `src/app/` | **captain 2026-10-31T11:56Z 补认**：来源「T10 交付所需的产品接线，captain 在此授权补登；写域来自 T10 的契约扩展」。理由：不把编辑内核接进构建，`setEditorProvider` 根本编不进去 —— 这是 T10 交付的**必要条件**，不是越界 |

### 判读规则
- 上表所列路径的改动，**视为已获授权**，verifier 不应按「越界写」判 FAIL。
- 授权的**只是这些改动**，不是这些文件的所有权：后续 lane 若再改同一文件，需要**追加登记**（见下节）。
- 生成物 `crates/vd-ffi/include/vd_ffi.h` 的正确性由 `build.rs` + 构建决定，**不以人工审阅为准**；verifier 可以重建核对，但不能要求 revert。

---

## 多 lane 共写的文件（不是豁免，是判读提示）

有些文件被**两条 lane 先后写入**。它们本身在各自写域内，但 verifier 若只看最终 diff，容易误判成「A lane 改了 B lane 的代码」。登记如下：

| 路径 | 共写的 lane | 各自负责什么 | 来源 |
|---|---|---|---|
| `src/app/app_shell.h`、`src/app/app_shell.cpp`、`src/app/main.cpp` | **T47**（会话恢复顺序）+ **T71**（Ctrl+S / 菜单 Save） | T47 管启动时的会话恢复顺序；T71 管保存动作的绑定与菜单项 | captain 2026-10-31T11:56Z 提醒 |

**提交纪律**：这两条 lane 的改动在同一批提交里出现时，提交信息必须写明「T47 + T71 双 lane 共写」，避免未来的人以为其中一条 lane 改了另一条的代码。

---

## 本表自身的写域说明

T16 的派出消息声明写域为 `evidence/slice1-stop/`；captain 的 T16 指令另行明确要求落盘本文件与
`evidence/verdicts/{gate-infra,repo-hygiene}/verdict.md` 及 C9 重采/补记。本表把这些**由 captain 显式下令产生的写域扩展**一并登记在此，供 verifier 判读。
