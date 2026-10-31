# T6 独立评审结论 — Slice 1：工程模型 + `.f`（Rust 服务，可诊断、可持久）

- **评审者**：Reviewer（cell `cell-20261031T085543161Z` 的第 4 个 Reviewer 实例；verifier ≠ writer `e1acec86…`）
- **被评审任务**：board `T6`（kind=work，owner Senior Engineer 3，status=completed）
- **冻结契约**：`.mpd/cellwork/cell-20261031T085543161Z/contracts/project-model.md`（split revision 2，2026-10-31T09:51:34Z）
- **验证环**：`loop-20261031T095153-cc3f0c`（`mpd_verify_seat` 已绑定本会话 2026-10-31T11:01:03Z）
- **结论**：**PASS**（bound seat），附 3 条 finding（F-1 Low / F-2 Medium·build-infra / F-3 Low）
- **盲评声明**：判据、文档与下面这些**我自己重跑的门禁**形成结论；本轮**未读任何实现源码**
  （`crates/vd-config/src/**`、`crates/vd-ffi/src/project.rs`、`src/workspace/project/**`），
  故实现只作为"交付物存在"被 `git status` 计数，不作为判据来源。
- **证据目录**：`evidence/verdicts/project-model/`（本文件 + `verify-run/` 全套复跑日志 + 复跑脚本）

---

## 1. 我实际跑过的门禁与观测输出

### 1.1 Rust 服务（pinned CentOS 7 镜像 `vedit-build:local`，rustc/cargo 1.99.0）

```text
$ docker run --rm -e HOME=/vedit-build/target/home -e CARGO_HOME=/vedit-build/target/cargo-home \
    -e RUSTUP_HOME=/opt/rust -e RUSTUP_TOOLCHAIN=1.99.0 -v "$PWD":/vedit-build -w /vedit-build \
    vedit-build:local bash -lc 'cargo test -p vd-config -p vd-ffi > /tmp/t.log 2>&1; echo TEST-EXIT=$?; \
    grep -E "^test result:" /tmp/t.log; grep -c "^failures:" /tmp/t.log; \
    cargo clippy -p vd-config -p vd-ffi --all-targets -- -D warnings > /tmp/c.log 2>&1; echo CLIPPY-EXIT=$?'
TEST-EXIT=0
test result: ok. 33 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
test result: ok. 14 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
test result: ok. 2 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out      ← project_model_fixture.rs
test result: ok. 4 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
test result: ok. 3 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out      ← vd_ffi project::tests
test result: ok. 12 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out     ← tests/abi.rs
test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out      ← doc-test
test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
FAILED-lines: 0
CLIPPY-EXIT=0
```

其中与 AC-S1.3 直接相关的用例（日志内逐行可见）：
`the_repository_fixture_expands_to_the_checked_in_expectations`、
`bad_entries_name_the_filelist_file_and_line`、
`project::tests::fixture_expansion_matches_the_service_contract`、
`project::tests::bad_entries_render_actionable_diagnostics`、
`project::tests::expansion_is_deterministic`、
`project_parse_expands_the_fixture_filelist`、`projected_fixture_serves_all_seven_services`。

### 1.2 Qt lane 全链复跑（configure → build → ctest → 36 项 selfcheck → 两进程重启 → 坏条目）

我用 writer 的 `evidence/project-model/run-standalone.sh`（sha256 `c30eaa5c…`）**逐命令等价复跑**：
复制品只改了 `OUT` 重定向（写进本评审证据目录）与 `ROOT` 的解析方式（副本层级不同）；
`diff <(grep -v '^OUT=\|^ROOT=' 原脚本) <(…副本…)` → **COMMAND BODIES IDENTICAL**。
输出：`evidence/verdicts/project-model/verify-run/`。

```text
$ bash evidence/verdicts/project-model/verify-run/rerun.sh ; echo RUN-EXIT=$?
RUN-EXIT=0
== 03-standalone-ctest.log ==
1/1 Test #1: vedit-workspace-project-selftest ...   Passed    0.05 sec
100% tests passed, 0 tests failed out of 1
== 04-selftest-selfcheck.log ==
PROJECT-MODEL-SELFTEST ok=1 checks=36 failures=0        （36 行 CHECK，逐行 PASS）
== 05-restart-seed.log ==
SEED phase=seed ok=1 files=7 root=/vedit-build/tests/fixtures/verilog-project state=…/project-model.json
== 07-restart-restore.log ==
RESTORE phase=restore ok=1 files=7 root=/vedit-build/tests/fixtures/verilog-project error=
== 09-bad-entry-diagnostics.txt ==
filelist.define_invalid: /tmp/bad-project-t6/project.f:1: malformed +define+ token; skipped
filelist.unknown_token: /tmp/bad-project-t6/project.f:2: unsupported filelist token `+libext+.v`; skipped
filelist.missing: /tmp/bad-project-t6/project.f:3: nested -f target `missing.f` does not exist; skipped
```

### 1.3 根构建产物的面板测试（产品内集成）

```text
$ docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e XDG_RUNTIME_DIR=/tmp/xdg-runtime \
    -e LANG=C.UTF-8 vedit-qt6-build:local bash -lc \
    'QT_QPA_PLATFORM=offscreen ctest --test-dir /vedit-build/target/qt6-build -R vedit-workspace-project-selftest'
1/1 Test #14: vedit-workspace-project-selftest ...   Passed    0.05 sec
100% tests passed, 0 tests failed out of 1
CTEST-EXIT=0
```

### 1.4 服务层确实链进产品二进制（C4 / AC-0.2 的 C ABI 入口）

```text
$ nm -C target/qt6-build/src/app/vedit-app | grep -cE ' T vd_abi_version$'
1
$ nm -C target/qt6-build/src/app/vedit-app | grep -E ' T vd_[a-z_]+$' | wc -l   （见 writer 17 日志：## count 36）
36
```

### 1.5 仓库声明的门禁（契约 Criterion 2）

```text
$ mpd_verify_evidence {kind:"gate", gate:"docs"}
gate docs exit 0 -> .mpd/verify/evidence/ev-20261031T111029-7b48e1.log
```

---

## 2. AC-S1.3 / AC-0.2 逐条对照（只引文档与可复跑证据）

| 判据 | 我的观测 | 结果 |
|---|---|---|
| fixture 覆盖 `+incdir+` / `+define+` / 嵌套 `-f` / 注释 | 直接读 fixture（数据，非实现）：`tests/fixtures/verilog-project/project.f` 同时含 `//` 与 `#` 全行/尾随注释、多目录 `+incdir+…+…`、`NAME=VALUE` 与裸 `+define+`、`-f filelists/tb.f`、`-f filelists/extra.f`（内含 `../rtl/mem.sv` 归一化与同文件重复）；`expected/{files,incdirs,defines}.txt` 齐备 | ✅ |
| 展开后文件数与列表 | selfcheck 36 项含 `7 files`、4 incdir、4 defines、与 `expected/files.txt` 逐项一致；`displayedFiles()` 与模型一致 | ✅（无头数值） |
| UI **显示**（视觉） | 无显示服务器；`--selfcheck` 只断言控件数据与模型一致，不是视觉证据 | **UNVERIFIABLE-IN-CONTAINER**；AC-S1.6 用户手工验收未做 |
| 坏条目可诊断 | 三条真实诊断均 `文件:行: 原因`（见 1.2），单测 `bad_entries_name_the_filelist_file_and_line` 绿 | ✅ |
| 重启后工程与成员仍在 | 两个**独立进程**：seed 写 state 后，restore 仅拿 state 目录仍 `files=7`；持久化 JSON 见 06（version/root/files/incdirs/defines/diagnostics） | ✅ |
| AC-0.2 服务归属 + C ABI | 面板数据来自 `vd_project_*`（writer 证据 + ABI 12 项测试绿）；二进制导出 `vd_abi_version`（1 处）与 36 个 `vd_*` 符号 | ✅（C ABI/单测）；"Qt 侧无第二套实现"见 §4 |

---

## 3. 写域核对（Criterion 1）

| 路径 | 状态 | 契约写域 | 判定 |
|---|---|---|---|
| `crates/vd-config/`（`src/*` 已在 `8b940f2` 收录；`tests/project_model_fixture.rs` M） | 在域 | `crates/vd-config/` | ✅ |
| `crates/vd-ffi/src/project.rs`（`8fedbdd` 收录） | 在域 | `crates/vd-ffi/src/project.rs` | ✅ |
| `src/workspace/project/`（未跟踪新 lane） | 在域 | `src/workspace/project/` | ✅ |
| `evidence/project-model/`（未跟踪） | 在域 | `evidence/project-model/` | ✅ |
| `src/app/CMakeLists.txt`（+16/−1）/ `src/workspace/integration.cpp`（+6/−0） | **域外** | 不在四条写域内 | ⚠ 见 F-1：captain 于 `2026-10-31T10:34:34.010Z` 的团队邮箱消息中**明确授权**这两处（精确到 A/B 两段），diff 与授权范围一致 |

---

## 4. 未能验证的部分（诚实声明）

1. **视觉/交互**：面板外观、字体、DPI、真实窗口表现——本机无显示服务器，一律不认证；AC-S1.6 需用户在有屏机器验收。
2. **"Qt 侧无第二套实现"**（AC-0.2 后半句）：这是源码级"不存在"的性质，没有门禁能证明；盲评纪律下我未读
   `src/workspace/project/**`。建议后续由一次**源码级追查**（或该 lane 的下一次评审）单独确认，不作为本次 PASS 的依据。
3. **一条命令的根构建** `bash scripts/build-qt.sh`：当前仍会红，但原因是 **build-infra 的 SIGPIPE 假红**
   （见 F-2，我已独立复现），不是本 lane 代码；captain 已另建 **T58** 修它。
4. 根 ctest 全量在 writer 采集时是 13/14，唯一失败 `#10 vedit-edit-core-selftest (Not Run)` 属 **T10 在飞**；
   我只复跑了 `#14`（本 lane）并通过。

---

## 5. Findings

| ID | 严重度 | 症状 | 期望 | 证明来源（doc source） |
|---|---|---|---|---|
| **F-1** | Low | 两处改动在冻结写域外：`src/app/CMakeLists.txt`（+16/−1）、`src/workspace/integration.cpp`（+6/−0）；冻结契约 `project-model.md` 的 `Allowed write scopes` 只有四条。授权是**存在的**（captain 团队邮箱 `2026-10-31T10:34:34.010Z`：`set(VEDIT_WORKSPACE_LANES …)` 末尾加 `project` + `integration.cpp` 的 include/`installPanels()` 调用），但契约文本未同步 | 由 captain 在契约/需求记录里补记该豁免（或 bump 契约写域），使「写域」与事实一致；不要靠报告里的口头说明 | `.mpd/cellwork/cell-20261031T085543161Z/contracts/project-model.md`（写域）；`.mpd/team/mailbox.jsonl`（授权原文） |
| **F-2** | Medium（build-infra，非 lane 代码） | `bash build/gates/typecheck.sh` → `TYPECHECK-EXIT=1`，报 `FATAL: vedit-app does not export vd_abi_version`。**我复现并证伪该报错**：容器内 `nm -C vedit-app \| grep -qE ' T vd_abi_version$'` → **status=141**（SIGPIPE），同命令去掉 `-q` → **status=0**；nm 输出 **3,871,546 B** ≫ 64 KiB 管道缓冲，符号本身存在（`grep -c` = 1）。根因在 `scripts/build-qt-in-container.sh:90`（`pipefail` + `grep -q`） | 一行修复（落文件再 grep 或去掉 `-q`），修后必须做**负例证伪**（缺符号仍判红）；captain 已建 **T58** 承接 | `build/gates/typecheck.sh`、`scripts/build-qt-in-container.sh:90`；`.mpd/team/mailbox.jsonl`（captain 11:03Z 指派 T58） |
| **F-3** | Low | 面板用 `addDockWidget` 直插（objectName `dock-project`），**未注册 `PanelRegistry`** → View 菜单没有 project 面板 toggle（writer 报告 §5.1 自认） | AC-S1.3 只要求「显示文件数与列表」，不要求 toggle；若要 toggle，作为 app-shell 的独立小任务评审 | `requirements.md` AC-S1.3；`evidence/project-model/REPORT.md` §5.1 |

---

## 6. 结论

**PASS**。AC-S1.3 的可无头证明部分（`.f` 全特性展开、文件数/列表、坏条目 `文件:行` 诊断、两进程重启持久化）
由我自己的复跑全部成立；AC-0.2 的 C ABI 入口与单测成立；产品二进制确实链接服务层与面板测试。
三条 finding 均不阻塞：F-2 是已归 T58 的 build-infra 假红（我已复现），F-1 只需 captain 补记授权，
F-3 是超出 AC 的增强。视觉与"无第二套实现"按 §4 明确标为未证。
