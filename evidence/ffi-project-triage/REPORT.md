# T97 归属报告 — `--selfcheck-ffi tests/fixtures/verilog-project` 的 4 条 `ffi.project.*` FAIL

- Cell: `cell-20261031T085543161Z` · Board task: **T97**（来源：T24 评审的 Info）
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-11-01（Asia/Shanghai）· 基线 `HEAD 4856960`
- 结论: **(b) 参数/夹具差异 —— 不是回归，也不是断言过时**。
  `--selfcheck-ffi` 的断言针对 **C ABI 冒烟夹具** `crates/vd-ffi/tests/fixtures/project`
  （3 个 rtl 文件、`+incdir+inc`、`define WIDTH=8/SIM`）；T24 评审传入的是**工程模型夹具**
  `tests/fixtures/verilog-project`（T14，7 个文件、自己的 `+incdir+`/`+define+` 语义）。
  两个夹具的期望值本来就不同，四条 FAIL 是**期望与输入不匹配**，产品解析本身正确
  （同一份 T14 夹具由 `cargo test -p vd-ffi -p vd-config` 按它自己的 `expected/` 验证，**全 PASS**）。
- 已做的修复：`--selfcheck-ffi` 现在**显式拒绝**错误夹具并给出正确命令（exit 2，不再产生 4 条误导性 FAIL）；
  四条断言的 detail 打印实际值；`--selfcheck-ffi` 帮助文本与 `AGENT.md` 都写明了正确调用方式。
  **没有放宽任何断言**：在正确夹具上，真回归仍然会让同样四条 FAIL。

## 1. 复现（当前 HEAD，完全相同的命令）

`target/qt6-build/src/app/vedit-app --selfcheck-ffi tests/fixtures/verilog-project`（offscreen）：

```
FFI-ABI fixture=tests/fixtures/verilog-project
CHECK ffi.project.start.returns-job PASS
CHECK ffi.project.start.non-blocking PASS
CHECK ffi.project.ready PASS
CHECK ffi.project.file-count FAIL
CHECK ffi.project.file-order FAIL
CHECK ffi.project.include-dir FAIL
CHECK ffi.project.defines FAIL
CHECK ffi.project.unknown-job-rejected PASS
FFI-ABI-SELFTEST ok=0 checks=20 failures=4      ← exit 1
```

与 T24 报告的现象完全一致。**同一二进制**换正确夹具再跑：

`--selfcheck-ffi crates/vd-ffi/tests/fixtures/project` →
`FFI-ABI-SELFTEST ok=1 checks=20 failures=0`（exit 0），四条 `ffi.project.*` 全 PASS
（`04-after-correct-fixture.log` 带实际值：`actual=3 files=[rtl_a.sv,rtl_b.sv,rtl_c.sv]`、
`actual=[rtl_a.sv,rtl_b.sv,rtl_c.sv]`、`actual=[.../rtl/inc]`、`actual=[WIDTH=8,SIM]`）。

## 2. 四条 FAIL 的逐条对账（为什么是夹具差异）

| check | `--selfcheck-ffi` 的硬编码期望（C ABI 夹具） | `tests/fixtures/verilog-project`（T14）的真实展开（其 `expected/`） | 判定 |
|---|---|---|---|
| `ffi.project.file-count` | 3 | **7**（`expected/files.txt`：rtl/alu_pkg.sv, rtl/alu.sv, rtl/counter.v, rtl/top.sv, tb/tb_top.sv, tb/tb_aux.sv, rtl/mem.sv） | 夹具差异 |
| `ffi.project.file-order` | `rtl_a.sv, rtl_b.sv, rtl_c.sv` | 上面 7 个文件，顺序完全不同 | 夹具差异 |
| `ffi.project.include-dir` | 恰好 1 个、以 `inc` 结尾 | **4 个**：`inc`, `inc/generated`, `rtl/include`, `tb/include`（`expected/incdirs.txt`） | 夹具差异（数量 4≠1） |
| `ffi.project.defines` | 包含 `WIDTH=8` 与 `SIM` | `WIDTH=8`, `DEPTH=16`, `DEBUG`, `SIMULATION`（`expected/defines.txt`；**没有裸 `SIM`**） | 夹具差异（`SIMULATION` ≠ `SIM`） |

而 C ABI 夹具自己的 `rtl/files.f` 明确写着期望展开：
`// The expected expansion is: rtl_a.sv, rtl_b.sv, rtl_c.sv (nested -f last).`
→ 四条 FAIL 全部由「T14 夹具 ≠ C ABI 夹具」解释，无一条指向解析缺陷。

## 3. 为什么不是回归（三份独立证据）

1. **正确夹具全绿**：同一二进制、同一产品路径，`--selfcheck-ffi crates/vd-ffi/tests/fixtures/project`
   → 20 checks / 0 failures（exit 0）；ctest 注册的同一个命令
   `vedit-app-ffi-abi-selftest ... Passed`（`05-ctest.log`）。
2. **T14 夹具由它自己的期望值验证且全 PASS**：`cargo test -p vd-ffi -p vd-config`（pinned Rust 镜像）
   → `RUST-TEST-EXIT=0`；其中 `project::tests::fixture_expansion_matches_the_service_contract`
   与 `project_parse_expands_the_fixture_filelist` 正是用 `tests/fixtures/verilog-project/expected/`
   校验同一个工程模型（`06-rust-fixture-tests.log`）。若 T14 夹具的 7 文件/incdir/define 展开真的坏了，
   这两条 Rust 测试会红。
3. **VCS 两条在两种夹具上都 PASS**：VCS 检查对夹具内容不敏感（只看“目录在 git 仓库内”），
   与项目模型断言无关 —— 与 T24 观察到的「VCS 绿、project 红」一致。

额外说明 T6/T7 的验收口径：T6（工程模型 + `.f`）当时就是用 `tests/fixtures/verilog-project/expected/files.txt`
（文件数 = 7）验证的，本任务没有改变任何夹具或期望值；`expected/` 与 C ABI 夹具都未被修改。

## 4. 修复（不改变断言强度）

`src/app/main.cpp`（selfcheck 段，最小改动）：

1. `ffi::runSelfCheck()` 开头增加**夹具守卫**：目录必须含 `vedit.toml`（C ABI 夹具标记）。
   否则打印明确的 `reason=wrong-fixture` + 正确命令提示并 **exit 2**（用法错误），
   不再把“传错夹具”表现成 4 条产品回归。守卫只按**夹具身份**判断，不按解析结果判断，
   因此在正确夹具上真回归仍会逐条 FAIL。
2. 四条 `ffi.project.*` 断言保留原期望，新增 detail 打印**实际值**（files/order/incdir/defines），
   排查时不用再猜「FAIL 是差在哪」。
3. `--selfcheck-ffi` 的帮助文本写明它要的是 `crates/vd-ffi/tests/fixtures/project`。

`AGENT.md` §4 Commands 增加了一条文档：正确命令、两个夹具的区别、以及 T14 夹具的正确验证方式
（`cargo test -p vd-ffi -p vd-config` / `scripts/verify-fixture.sh`），避免下一个人再踩。

## 5. 修前/修后对照（同一命令）

| 命令 | 修前（旧二进制） | 修后（当前 HEAD + T97） |
|---|---|---|
| `vedit-app --selfcheck-ffi tests/fixtures/verilog-project` | **exit 1**，`ok=0 checks=20 failures=4`（file-count/file-order/include-dir/defines） | **exit 2**，`FFI-ABI-SELFTEST ok=0 reason=wrong-fixture` + 正确命令提示（不再有 4 条误导 FAIL） |
| `vedit-app --selfcheck-ffi crates/vd-ffi/tests/fixtures/project` | **exit 0**，20/0 | **exit 0**，20/0（新增 actual 值 detail） |
| `ctest -R vedit-app-ffi-abi-selftest` | Passed | **Passed**（`05-ctest.log`） |

## 6. 影响面（若用户/脚本传错夹具）

- **产品运行不受影响**：用户打开真实工程时，文件数/顺序/incdir/define 来自 `vd_config` 的加载器
  （由 `crates/vd-config` 的 `project_model_fixture` 测试与产品面板路径覆盖），与 `--selfcheck-ffi`
  的硬编码夹具期望无关。本次没有改动任何解析代码。
- 唯一行为变化：**开发者的 `--selfcheck-ffi` 传错夹具时立即得到用法错误而不是 4 条假回归**。

## 7. 证据（`evidence/ffi-project-triage/`）

`00-pins.txt`、`01-before-wrong-fixture.log`（修前 exit 1 + 4 FAIL）、
`02-before-correct-fixture.log`（修前正确夹具 20/0）、`03-after-wrong-fixture-guard.log`（修后 exit 2 + 提示）、
`04-after-correct-fixture.log`（修后 20/0 + actual 值）、`05-ctest.log`、`06-rust-fixture-tests.log`
（`cargo test -p vd-ffi -p vd-config` = RUST-TEST-EXIT=0，T14 夹具按自己期望验证通过）、本报告。

未 commit/push。未触碰 `crates/vd-config/**`、`crates/vd-ffi/src/project.rs`（解析代码无需改动）、
`src/syntax/**`、`src/workspace/vcs/**`。
