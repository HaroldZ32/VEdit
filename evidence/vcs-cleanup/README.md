# 批 7 · VCS 清理：类型化 JobValue + rebase 确认 + 第二步拒绝断言 + 小型 NIT（T45）

任务：`T45`（captain 创建的清理任务，cell `cell-20261103T123639841Z`）。
写域（captain）：`crates/vd-ffi/src/{service.rs,vcs.rs}`、`crates/vd-ffi/include/vd_ffi.h`、
`crates/vd-ffi/tests/abi.rs`、`crates/vd-vcs/{src,tests}/`、`src/workspace/vcs/**`、
`translations/vedit_zh_CN.ts`、`evidence/vcs-cleanup/**`。

## 0. 一屏结论

| 项 | 结果 |
|---|---|
| `VcsInfoData` 槽位 workaround | **已删除**；五类历史读各有 typed `JobValue` 变体，`VcsInfo` 只表示身份读 |
| 错误 poll 仍被拒 | `WRONG_JOB_KIND`（`-1`）；新增 ABI 测试覆盖 history↔workspace↔info 四种错配 |
| rebase 确认 | 单次确认 + 后果说明（重写历史 / 需 force-push / 冲突时 Continue/Abort）；拒绝零写入 |
| `confirmTwice` 第二步拒绝 | 新增"接受第一步、拒绝第二步"的 provider，Git discard 与 worktree remove 各断言一次：两次确认、零写入 |
| 面板 toolchain 属性（6a NIT） | 共享 `applyToolchainProperties()`；lane 在普通 `VcsPanel` 上断言五项属性 |
| 凭据默认 headless（4a NIT） | 未启用探针接缝时：不驱动对话框、不重试、拒绝并说明 |
| 7a i18n 缺口 | `backendName()` / `changeKindName()` 由裸英文字面量改为 `translate("VEdit", …)`；行内中文断言 |
| 断言数 | lane **673 → 697**（+24，未删任何检查）；Rust ABI 测试 **20 → 21** |
| 七道门禁 | 见 §6（全绿） |

## 1. 变更清单

### 1.1 类型化 JobValue（用户裁定，`crates/vd-ffi/src/service.rs`）

旧设计：history / workspace / info 三种读共用一个 `JobValue::VcsInfo(VcsInfoData)` 槽位，
`VcsInfoData` 是 `crates/vd-ffi/src/vcs.rs` 里的内部枚举（batch 4b 因 `service.rs` 不在写域而采用）。

新设计（`service.rs` 的 VCS 变体）：

```
VcsInfo(VcsInfoPayload)                 // 分支/refs 身份读（不再承载别的读）
VcsWorkspace(Box<VcsWorkspaceData>)     // stash / worktree 列表
VcsHistoryCommits(Box<VcsHistoryData>)  // 提交列表页
VcsHistoryCommit(Box<VcsHistoryData>)   // 单提交详情
VcsHistoryFile(Box<VcsHistoryData>)     // 单文件历史
VcsHistoryBlame(Box<VcsHistoryData>)    // blame
VcsHistoryCompare(Box<VcsHistoryData>)  // 两提交比较
```

- `crates/vd-ffi/src/vcs.rs`：删除 `VcsInfoData` 及其注释；`info_data()` 直接返回
  `VcsInfoPayload`；五个 history job 各自产出对应变体；workspace job 产出
  `VcsWorkspace`。`vd_vcs_history_poll` 只接受五个 `VcsHistory*`（一个 `history_payload()`
  助手），其余一律 `WRONG_JOB_KIND`；`vd_vcs_workspace_poll` 只接受 `VcsWorkspace`；
  `vd_vcs_info_poll` 只接受 `VcsInfo`。
- **行为不变**：C ABI 结构、kind 常量、poll 返回值（`0/1/2` 状态与 `-1` misuse）全部不变；
  `include/vd_ffi.h` 由 build-qt 的 cbindgen 重新生成（sha256 记在
  `logs/gate-cleanup-evidence.log`）。
- `crates/vd-ffi/tests/abi.rs`：新增
  `history_and_workspace_polls_reject_a_foreign_payload`——history 作业用 workspace/info poll
  读取被拒、workspace 作业用 history poll 读取被拒、identity 作业用 history poll 读取被拒；
  原有 "workspace poll 拒绝 status 作业" 检查保留。ABI 测试 20 → 21。
- 两处旧证据 README 的 workaround 说明已替换为 typed 设计（见 §5 的写域说明）。

### 1.2 rebase 确认（用户裁定，`src/workspace/vcs/vcs_panel.cpp`）

`VcsPanel::rebase()` 现在先走 `askConfirm()`（与 reset 同一 seam、同样"后果优先"的措辞）：

> Rebase the branch?
> Rebase `<branch>` onto `<target>`? Rebase rewrites the commits of `<branch>`: every
> commit after `<target>` is replayed on top of it and gets a new id. A published
> branch must be force-pushed afterwards. Conflicts stop the rebase until you
> Continue or Abort.

- 拒绝：`Rebase cancelled; nothing was written.`，不产生任何写请求。
- 空目标：直接拒绝（`A rebase target is required; nothing was written.`），不弹确认。
- 新增字符串全部进 `translations/vedit_zh_CN.ts`（lrelease 0 unfinished）。

lane 检查：`vcs.ops.rebase-declined`、`.rebase-declined-zero-write`、`.rebase-single-confirm`、
`.rebase-preview-spells-consequence`、`.rebase-empty-target-refused`、`.rebase-empty-target-zero-write`、
`.rebase-confirmed-requested`、`.rebase-confirmed-request`。

### 1.3 `confirmTwice` 第二步拒绝（5a/5b/6b review NIT）

之前的断言只覆盖"第一步拒绝"。现在用 `++step == 1` 的 provider（第一步接受、第二步拒绝）验证：

- Git discard（`vcs.ops.discard-second-step-*`）：返回 false、**两次**确认、零写入；
- worktree remove（`vcs.workspace.worktree-remove-second-step-*`）：同上。

之后把 provider 恢复为全接受，确认正常路径仍能写入（`.discard-first-step-accepted-after`）。

### 1.4 面板 toolchain 属性（6a review NIT）

T23 由 `vcs_integration.cpp` 的工厂把 `VCS-TOOLCHAIN` 与 git/svn 路径/版本写成 widget 属性，
但该工厂只在产品 shell 构建里存在。本批把属性写入抽成
`vcs_toolchain.h/.cpp::applyToolchainProperties(QWidget*, const ToolchainStatus&)`，
工厂与 lane 共用；lane 在普通 `VcsPanel` 上调用并断言
`veditVcsToolchain / veditVcsGitPath / veditVcsGitVersion / veditVcsSvnPath / veditVcsSvnVersion`
（`vcs.toolchain.panel-properties`）。工厂行为不变。

### 1.5 凭据对话框默认 headless（4a review NIT）

在 offscreen 且**未**启用探针接缝（`setModalCredentialDialogForProbe`）时，一个需要凭据的
失败写操作必须：询问一次凭据（`credentialPromptCount` +1）、**不打开**对话框
（`modalCredentialDialogs()` 不变）、**不重试**（写入次数 +1）、并以"已中止/未写入"收尾。

检查：`vcs.cred.headless-push-issued`、`.headless-default-refusal`、`.headless-no-modal`、
`.headless-no-retry`、`.headless-says-aborted`（非 offscreen 平台显式 SKIP，不静默通过）。

### 1.6 7a i18n 缺口：`vcs_source.cpp` 的裸字面量

`backendName()` 返回 `Git` / `Subversion` / `No repository` / `Unknown`，
`changeKindName()` 返回 `added` / `modified` / `deleted` / `renamed` / `conflict` / `unknown`，
两者原先都是**裸 C 字面量**，从不经过 `translate()`——即使 `.ts` 里已有译文，面板仍显示英文。
现在：

- 返回类型改为 `QString`，每个字面量都走 `QCoreApplication::translate("VEdit", …)`；
- 面板的三处调用（header、行文本、空状态）去掉多余的 `t()` 包装；
- `.ts` 新增 6 条（rebase 5 条 + `Unknown`），其余字面量本就已在 VEdit 上下文有译文；
- lane 断言：`vcs.i18n.source-literals`（`changeKindName(2)=="修改"`、`backendName(None)=="无仓库"`）
  与 `vcs.i18n.change-row-chinese`（zh 目录下真实面板行文本含"修改/新增"）。

## 2. 断言数（before → after）

| 套件 | before（批 6b） | after（本批） |
|---|---|---|
| lane selftest | 673 checks / 0 failures | **697 checks / 0 failures** |
| screenshots（未改 UI 布局，未重跑） | 70 / 0 | 70 / 0（无断言变化） |
| `cargo test -p vd-ffi --test abi` | 20 passed | **21 passed** |
| `cargo test -p vd-vcs` | 全部绿（svn/worktree E2E 无 env 时 SKIP） | 全部绿 |

没有删除或放宽任何既有检查；两处与行为相关的既有断言保持不变（`vcs.layout.action-bar` 的
按钮清单在 6b 已含 Remove，本批未动）。

## 3. 复跑

```bash
# 1) 构建 + lane（697 checks）
bash scripts/build-qt.sh
bash evidence/vcs-cleanup/run-selftest.sh

# 2) 七道门禁 + Rust ABI/vd-vcs 证据 + 本批断言
bash evidence/vcs-cleanup/run-gates.sh
```

日志：

- `logs/10-selftest.log` — lane 全量检查（697/0）
- `logs/10-rust-abi.log` — `cargo test -p vd-ffi --test abi`（含 typed-payload 错配测试）
- `logs/11-rust-vd-vcs.log` — `cargo test -p vd-vcs`
- `logs/gate-build-qt.log`、`gate-ctest.log`、`gate-selfcheck.log`、`gate-clippy.log`、
  `gate-fmt.log`、`gate-docs.log`、`gate-lrelease.log`
- `logs/gate-cleanup-evidence.log` — 本批的判定汇总（typed 变体存在、workaround 缺席、
  header sha256、lane 计数、lrelease）

## 4. 门禁（实测，`logs/gate-summary.log`）

| 门 | 观测 |
|---|---|
| build-qt | PASS（含 Rust test + cbindgen 重新生成 header + ctest + xcb 冒烟） |
| ctest | `100% tests passed, 0 tests failed out of 37` |
| vedit-app --selfcheck | `APP-SHELL-SELFTEST ok=1 checks=290 failures=0` |
| clippy | 0（`-D warnings`） |
| fmt | 0（`cargo fmt --all -- --check`） |
| docs | PASS |
| lrelease | `930 finished and 0 unfinished` |
| Rust 证据 | `cargo test -p vd-ffi --test abi` 21 passed；`cargo test -p vd-vcs` 11 个测试二进制全绿 |
| 本批判定 | `logs/gate-cleanup-evidence.log` → `result=PASS`、`lane_checks=697 (baseline 673, failures=0)` |

- build-qt / ctest / vedit-app --selfcheck / clippy / fmt / docs / lrelease 全绿；
- `lrelease`：**930 finished / 0 unfinished**（917 → 930，+13：`lupdate` 重新扫描后，7a 缺口
  涉及的 kind/backend 字面量以已有译文回到 VEdit 上下文，加上 rebase 5 条与 `Unknown`；
  其中 6 条新出现的未完成条目由 `tools/translations-cleanup.json` 填满，其余直接复用旧译文）；
- `ctest` 本轮为 **37/37**：与批 6b 的 38 相比少了一个 `vedit-syntax-directory-tests`
  ——**不是本批删除**，是批次 7 死代码任务（AC-7.7，`vedit_syntax_folding` 相关测试）在树上的
  外部变更；本批只记录，不修改该任务的产物。

## 5. 写域说明（一处超出清单，任务正文明确要求）

任务正文要求"移除 README workaround 说明（改为 typed 设计）"，对应文件在
`evidence/vcs-git-history/README.md` 与 `evidence/vcs-git-workspace/README.md`（均不在
任务列出的写域清单内）。本批只把这两处的 workaround 句子替换为"Superseded by batch 7 (T45)"
的 typed 设计说明（两行），未改其它证据内容；特此登记。

## 6. 边界

- 类型化只发生在 C ABI 内部（`JobValue` 不是 ABI 类型）；C 结构体、kind 常量、poll 语义不变。
- rebase 仍是单次确认（用户裁定允许）；它不改变 `reset --hard` / `discard` / `clean` /
  stash drop / worktree remove 的双重确认策略。
- 本批不改 read/write 门禁、不改打包语义、不动冻结文件。
