# T6 · project-model — evidence

Task: **T6 工程模型：开文件夹即根、Filelist 导入、并集文件集、-I/-D、持久化恢复**
(cell `cell-20261102T050548232Z`, verifier loop `loop-20261102T063252-538c6e`).

Reproduce everything with:

```sh
bash evidence/w3-project/run.sh
```

It needs the two pinned images already on the host (`vedit-build:local`,
`vedit-qt6-build:local`) and writes every log into this directory.

## What was verified (command → observed result)

| evidence | command | observed |
|---|---|---|
| `logs/10-selfcheck-and-restart.log` | `ctest`/selftest `--selfcheck tests/fixtures/verilog-project` inside `vedit-qt6-build:local`, `QT_QPA_PLATFORM=offscreen` | `PROJECT-MODEL-SELFTEST ok=1 checks=70 failures=0` |
| same log | `--seed` (process 1) then `--restore` (process 2) sharing `target/tmp/w3-state` | `SEED ok=1 files=11 filelists=1 incdirs=5 defines=6`, `RESTORE ok=1 …`, `STATE-IDENTICAL` (the persisted JSON is byte-identical before/after the restart) |
| `logs/11-ctest.log` | `ctest --test-dir target/qt6-build -R vedit-workspace-project-selftest` | `1/1 Test #27 … Passed`, `100% tests passed` |
| `logs/30-rust-project-tests.log` | `cargo test -p vd-ffi project::tests -- --nocapture` | all `project::tests` ok; the three bad `.f` cases print `filelist.missing: …/bad.f:2: include directory …`, `filelist.unknown_token: …/bad.f:3: …`, `filelist.missing: …/bad.f:4: …`; `BACKEND-ARGV engine=slang … -I<dir> -DBOARD=7 -DSIM <file>` |
| `logs/40-clippy-fmt.log` | `cargo clippy --workspace --all-targets -- -D warnings` then `cargo fmt --all -- --check` (pinned image) | `CLIPPY-EXIT=0`, `FMT-EXIT=0` |
| product build (captain's gate shape) | `bash scripts/build-qt.sh --no-test` | `[qt6] PASS: Rust service layer + configure + build (warnings-as-errors) inside vedit-qt6-build:local`, exit 0 |

The self-check covers, in order: the `directoryOpened` seam setting the root
(fake emitter, no shell), the union file set (7 `.f` sources + 4 scanned `.vh`
headers = 11), root-relative `relativeFiles()` (`f` generation), the panel's
count/list/include dirs/defines/diagnostics, `-I`/`-D` reaching the model and
`backendArguments()`, the settings save re-expanding without a restart (watch +
1 s stamp fallback), import/remove filelist (atomic on a bad path), the
three bad `.f` lines through the panel, double-click jumping through the shared
`IJumpTarget` registry with the absolute path and 1-based position, two-process
persistence, a vanished root surfacing as a visible service error, and a corrupt
state file being reported instead of crashing.

## ADDENDUM A-9 — filelist variables (added after the T6 PASS)

A-9 extends this task's deliverable (same write scope):

| evidence | command / probe | observed |
|---|---|---|
| `logs/60-a9-expansion.log` | `--expansion evidence/w3-project/a9-fixture RTL=rtl WIDTH_VALUE=8 PRECEDENCE=project` (env `PRECEDENCE=environment`, `HOME=/tmp/a9home-evidence`) | `files=7 skipped=2 missing=[MISSING_VAR]`; files include `rtl/a.sv`, `rtl/b.sv`, `rtl/c.sv` (the three variable forms), `/tmp/a9home-evidence/a9-home/d.sv` (leading `~`), `rtl/e.sv` (two-level relative nested `-f`); defines `WIDTH=8`, **`PRECEDENCE=project`** (project table wins over the environment); diagnostics with file:line for the missing `+incdir+` (line 3), the unresolved variable (line 8) and the cycle `cycle_a.f -> cycle_b.f -> cycle_a.f` (naming both files) |
| same log, control | `--expansion evidence/w3-project/a9-fixture` with only `PRECEDENCE=environment` | `missing=[MISSING_VAR,RTL,WIDTH_VALUE]`, `skipped=6`, `PRECEDENCE=environment` — the table is what changes the result |
| selfcheck | `--selfcheck` | `a9.*` checks: dollar/braced/parenthesized/tilde expansion, two-level nested `-f`, project>env, cycle names both files, missing collected, skipped count, file:line, one-dialog row per variable (`ProjectVariablesDialog::rowCount()`), dialog values re-expand and clear the unresolved set, variables persisted in `project-model.json` |
| restart | `--seed` / `--restore` | `PROJECT-MODEL-SELFTEST ok=1 checks=106 failures=0`（A-9 时为 91，A-17 追加 15 条）；the seeded `"variables": ["SEED_VAR=seed"]` survives the restart and the JSON is byte-identical (`STATE-IDENTICAL`) |
| cycles/depth | Rust tests | `a_nested_cycle_names_both_files`, `depth_limit_is_enforced` (16) |
| never silent | Rust + UI | every dropped line is a diagnostic with `.f` file + 1-based line; `VdProjectInfo.skipped_lines` / `ProjectModel::skippedCount()` and `missing_variables` / `missingVariables()` feed the panel and summary (`skipped N` / `N unresolved`) |

The dialog is `src/workspace/project/project_variables_dialog.*`: ONE dialog,
one row per variable, pre-filled from the project table; empty rows are not
written (the variable stays unresolved). The panel opens it on
`ProjectModel::missingVariablesDetected`; a cancel keeps the import result and
lists the affected lines (spec A-9.4).

## ADDENDUM A-17 — literal `$` in filelist paths (added after the A-9 review)

A-17 把 A-9 复审发现的 Medium 缺陷修掉：`$` 后不跟标识符起始时是**字面 `$`**（shell 规则），
`out_dollar$.sv` / `out_tail.sv$` 这类合法路径不再被当成硬错误丢掉；展开产生的路径若不存在，
要**报诊断**（保留条目）；depth 超限要**点名被跳过的目标**；用户文档必须写清变量/转义/优先级。
契约写域：`crates/vd-config/src/filelist.rs`、`crates/vd-config/src/project.rs`（如需）、
`src/workspace/project/project_selftest.cpp`、`evidence/w3-project/**`、`docs/USER_GUIDE.md`（该小节）。
**实际改动只有 `filelist.rs`（+单测）、`project_selftest.cpp`、`docs/USER_GUIDE.md`、本目录**——
`project.rs` 不需要动，未改。

| evidence | command / probe | observed |
|---|---|---|
| `logs/70-a17-before.log` | 同一个 `--expansion`，但用**修复前**的 lane 二进制（日志首行写明构建时间）：`a17-fixture` | `EXPANSION … files=1 skipped=2`，两条 `config.invalid_value`：`project.f:2: cannot expand this line (a `$` must be followed by a name …)`、`project.f:3: … (a `$` at the end of the line is not a variable)` — 即契约里的“当前 1” |
| `logs/75-a17-after.log` | 修复后同一 fixture（`out_dollar$.sv` + `out_tail.sv$` + `rtl/ok.sv`） | `files=3 skipped=0 missing=[]`，三个 FILE 分别命中 `out_dollar$.sv`、`out_tail.sv$`、`rtl/ok.sv` |
| same log | `$`-转义控制 `a17-escape`（`escaped$$.sv`，目标在扫描根之外） | 修复前后都 `files=1`：`$$` 仍是一个字面 `$`，且不是“需要文件存在的展开” |
| same log | `a17-missing`（`$A17_RTL/missing_after_expansion.sv`、`~nosuchuser/never_home.sv`、`rtl/ok.sv`，表 `A17_RTL=rtl`） | `files=3 skipped=0`（条目**保留**）+ 两条 `filelist.missing`：`project.f:2: entry `…/rtl/missing_after_expansion.sv` does not exist (after variable expansion); the path is kept …`、`project.f:3: … (~nosuchuser …) (a leading `~` is only expanded as `~/` …)` |
| `logs/71-a17-vdconfig-test.log` | `cargo test -p vd-config`（pinned image） | `TEST-EXIT=0`；7 个新测试 `a_dollar_that_starts_no_reference_is_literal`、`a_double_dollar_is_still_the_escape`、`a_real_reference_still_resolves_or_is_reported`、`an_expanded_entry_that_does_not_exist_is_reported_and_kept`、`an_unexpanded_home_path_is_reported`、`depth_limit_names_the_skipped_target`、`a_malformed_reference_is_still_an_error` 全部 ok（unit 55 passed，原 48） |
| `logs/76-a17-selfcheck.log` | `--selfcheck tests/fixtures/verilog-project` | 15 条新检查 `a17.*` 全 PASS；`PROJECT-MODEL-SELFTEST ok=1 checks=106 failures=0`（A-9 时 91） |
| `logs/72-a17-clippy-crate.log`、`logs/74-a17-clippy-workspace.log` | `cargo clippy -p vd-config --all-targets -- -D warnings` / 全工作区同形 | `CLIPPY-CRATE-EXIT=0`，`CLIPPY-WORKSPACE-EXIT=0` |
| `logs/78-a17-workspace-test.log` | 仓库声明的测试门禁 `cargo test --workspace`（pinned image，AGENTS.md §4） | `WORKSPACE-TEST-EXIT=0`（全 workspace 测试二进制 `test result: ok`） |
| `logs/73a-a17-fmt-vdconfig.log`、`logs/73b-a17-fmt-workspace.log`、`logs/73-a17-fmt.log` | `cargo fmt -p vd-config -- --check` / `cargo fmt --all -- --check` | `FMT-VDCONFIG-EXIT=0`，`FMT-WORKSPACE-EXIT=0`。注：18:50 的第一次全工作区 fmt **曾**报红，唯一 diff 在 `crates/vd-lsp/src/client.rs`（本任务写域之外、另一席位的在飞改动，本任务未触碰）；该文件被其 owner 格式化后复跑为 0，`73-a17-fmt.log` 保留那次红的历史输出 |
| `logs/77-a17-docs-gate.log` | `bash build/gates/docs.sh` | `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md`，`DOCS-GATE-EXIT=0` |

语义落点（`crates/vd-config/src/filelist.rs`）：`expand_line` 只把 `$NAME` / `${NAME}` / `$(NAME)` /
`$$` / 行首 `~{}` 当引用，其余 `$` 原样输出；`ExpandedLine.substituted` 记录“这一行真的展开过”，
只有这种行（或行首 `~` 未能展开的字面路径）解析后不存在时才发 `filelist.missing`，普通相对路径
仍按 AC-2.3 的口径不报（生成文件是正常流程）；`+incdir+` 的诊断补上**解析后的绝对路径**；
depth 超限的两个出口都点名被跳过的文件。用户文档：`docs/USER_GUIDE.md` §2 的
“文件列表（`.f`）里的变量与转义”。

**没有证明的**：`70-a17-before.log` 是历史记录（用修复前二进制采集，无法用当前树重放）；
`run.sh` 只能重放“修复后”的 `75/76`。fixture 里 `$` 出现在文件名中，仅 Linux 文件系统验证过。

## What was NOT proved here (owner / next task)

1. **A real slang-server / verible-LS launch with `cwd` = root and
   `rootUri` = root.** The model exposes the one root (`projectRoot()`), the
   `-I`/`-D` lists and the root-relative `.f` projection, but the process
   launch is the LSP lane / diagnostics wiring (T7/T11/T20). This evidence
   proves the values, not the launch.
2. **The diagnostics panel consuming the model.** `ProjectModel` emits
   `stateChanged`/`filelistsChanged` and `projectModelForShell()` exposes it, but
   wiring the diagnostics/lint panels is T8/T20 (and `src/workspace/diagnostics/**`
   is outside this task's write scope).
3. **Visual appearance** of the dock. Every check is headless (`offscreen`);
   nothing here claims how the panel looks on screen.
4. **The Preferences page editing `-I`/`-D`** is T10 (already delivered). This
   task reads `tools.include_dirs` / `tools.defines` from the same store and
   re-expands on save; it does not own the page.
5. **Gate note (external red, owner: captain's commit flow).** `typecheck`'s
   generated-header drift check compares the working tree against `HEAD:
   crates/vd-ffi/include/vd_ffi.h`. This task adds a field to the C ABI
   (`VdProjectInfo.relative_files`, see `logs/50-ffi-header-change.diff`); until
   that regenerated header is committed, that one check is red by construction.
   The clippy half of `typecheck` is green (`CLIPPY-EXIT=0`).

## Measured rule kept by this task (2026-11-02)

The authoritative controlled 4-way experiment (`evidence/w2-spike/07-buildfile-rule.txt`)
says the **server's cwd must be the project root** (and `rootUri` must exist);
the `.f` location and entry style are secondary. This lane therefore:

* exposes exactly ONE root (`ProjectModel::projectRoot()`, persisted, pinned by
  the Rust expansion — it never walks up to a parent `.git`);
* fails visibly (`VD_ERR_NOT_FOUND`, shown as a `service:` diagnostic) when the
  root no longer exists;
* provides the root-relative `.f` projection as a portability aid only, never as
  the correctness mechanism.
