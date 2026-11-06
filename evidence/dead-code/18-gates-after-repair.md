# 18-gates-after-repair — T49 复验门禁（命令 + 观测 exit code + 日志）
# captured: 2026-11-04T14:03:55+08:00

| 门禁 | 命令 | 观测 | 日志 |
|---|---|---|---|
| 构建 + ctest | bash scripts/build-qt.sh | exit 0；100% tests passed, 0 failed out of **37**；M0-SMOKE ok=1 | 15-build-after-repair.log |
| 受影响用例 | ctest -R 'vedit-syntax-directory-equivalence|vedit-fold-gutter-click|vedit-native-fold-tests|vedit-syntax-kde-data' -V | exit 0；4/4 passed；补回的 2 条断言 CHECK ... PASS | 17-gutter-and-lane-tests.log |
| lane selftests | ctest -R 'vedit-edit-core-selftest|vedit-syntax-kde-data|vedit-syntax-directory-equivalence' -V | exit 0；3/3 passed；136 条 CHECK 行 | 25-lane-selftests-after-repair.log |
| selfcheck | vedit-app --selfcheck | exit 0；EDIT-CORE checks=99 / APP-SHELL checks=290 failures=0；CHECK id 集合 290→290，missing=0 | 22-selfcheck-after-repair.log、16-check-ids-after-repair.txt |
| clippy | cargo clippy --workspace --all-targets -- -D warnings | exit 0（Finished dev profile） | 19-clippy-after-repair.log |
| docs | bash build/gates/docs.sh | exit 0 | 21-docs-after-repair.log |
| lrelease（0 unfinished） | lrelease translations/vedit_zh_CN.ts | exit 0；Generated 922 translation(s) (922 finished and 0 unfinished) | 23-lrelease-after-repair.log |
| 对账交叉检查 | python3 evidence/i18n-l2/tools/make_reconciliation.py | exit 0；319 行 / 205·73·23·18 | 24-reconciliation-after-repair.log |

## fmt 门禁：RED —— 原因在 T49 写域之外（如实记录，未修）
cargo fmt --all -- --check -> exit 1（20-fmt-after-repair.log）
diff 只出现在两个 VCS lane 的文件里：
  crates/vd-ffi/src/vcs.rs:1317,1408,1632
  crates/vd-ffi/tests/abi.rs
归因证据：
  1. T49 的改动全是 C++/CMake：src/syntax/common/syntax_directory.{h,cpp}、
     src/syntax/kde/tests/syntax_directory_equivalence.cpp、evidence/dead-code/**；没有动任何 .rs。
  2. crates/vd-ffi/src/vcs.rs 的 mtime = 2026-11-04 14:01:42，晚于 T49 开始（13:59）与本轮第一次跑门禁的时间；
     该文件当时正被并行的 VCS lane 修改（git status: ' M crates/vd-ffi/src/vcs.rs'）。
  3. 同一个 fmt 命令在 T29 收口时（13:47，evidence/dead-code/11-fmt.log）exit 0 —— 那时这两个文件尚未被改。
结论：红的原因是并行 lane 尚未 rustfmt 的在途改动，不是 T49；按纪律不修改写域外文件、也不放宽门禁。
建议：等 VCS lane 收口后由 captain 重跑 fmt，或让 reviewer 以本条归因为准。
