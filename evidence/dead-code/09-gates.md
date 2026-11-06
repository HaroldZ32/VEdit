# 09-gates — T29 收口七道门禁（命令 + 观测 exit code + 日志）
# captured: 2026-11-04T13:47:47+08:00

| 门禁 | 命令 | 观测 | 日志 |
|---|---|---|---|
| 构建 + ctest | bash scripts/build-qt.sh | exit 0；100% tests passed, 0 failed out of 37；M0-SMOKE ok=1 | 05-build-after.log |
| 迁移后的真实点击判据 | ctest -R '^vedit-fold-gutter-click$' -V | exit 0；NATIVE-GUI fold_direction=1 unfold_direction=1 result ok=1；hidden 0→38→0、scroll_max 939→901→939 | 14-gutter-click-v.log |
| selfcheck | vedit-app --selfcheck --state-dir /tmp/sc（offscreen） | exit 0；EDIT-CORE checks=99 failures=0 / APP-SHELL checks=290 failures=0 | 13-selfcheck.log |
| clippy | cargo clippy --workspace --all-targets -- -D warnings（vedit-build:local） | exit 0 | 10-clippy.log |
| fmt | cargo fmt --all -- --check（rustc 1.99.0） | exit 0（无输出） | 11-fmt.log |
| docs | bash build/gates/docs.sh | exit 0；[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md | 12-docs.log |
| lrelease（0 unfinished） | lrelease translations/vedit_zh_CN.ts | exit 0；Generated 922 translation(s) (922 finished and 0 unfinished) | 04-lrelease-after.log |

## 交叉门禁（不属于 T29 契约，但本轮改了 .ts，必须确认没把别人搞红）
python3 evidence/i18n-l2/tools/make_reconciliation.py  -> exit 0，findings=319 rows=319，205/73/23/18
python3 evidence/i18n-l2/tools/verify_claims_selftest.py -> exit 0，4 cases 0 failure

## 断言点（AC-4.7 口径，见 03-before-assertions.txt / 08-after-ctest.txt / 16-check-ids-after-repair.txt）
src/ 2447 -> 2439（T29 删掉 syntax_directory_tests.cpp 的 10 个调用点；T49 把其中 2 个仍在用的）
**T49 更正**：T29 当时写的「-10 全部是重复覆盖」是**过度声明**。实测：那 9 个 check 调用点里
只有 4 个的语义被等价测试覆盖；2 个（syntax-dir.missing-path-is-empty / empty-string-is-empty）
测的是活代码 resolveSyntaxDefinitionDirectory，已原样补回 src/syntax/kde/tests/syntax_directory_equivalence.cpp；
另 3 个（file-verilog / file-systemverilog / application-fallback）测的 2 个 helper 已零调用点，
T49 把 helper 一并删除 => 这 3 条没有覆盖点（死代码断言，记录在案）。

## 说明：ctest 38 -> 37
删了 2 个 add_test（vedit-syntax-directory-tests、vedit-fold-gutter-click），
重新注册了 1 个（vedit-fold-gutter-click，同名；vedit-fold-native-probe 本来就没有 add_test）=> 37。

## T49 修复后的复验
见 15-build-after-repair.log（构建 + ctest 37 项）、16-check-ids-after-repair.txt（CHECK id 清单 + 死 helper grep=0）、
17-gutter-and-lane-tests.log（ctest -R）、18-gates-after-repair.md（七道门禁逐条）。
