# T63 — 根 CMake 纳入 lane 目录（含反例验证）

- 任务：T63（captain 指派）· 席位：Qt Build Engineer · 日期 2026-10-31
- 写域：根 `CMakeLists.txt`（T10 的构建底座文件）+ 本证据目录；**未改任何 lane 目录**
  （`src/app/CMakeLists.txt` 归 app-shell lane，`src/syntax/provider/` 归 syn-provider lane）

## 0. 结论

1. 根 `CMakeLists.txt` 现在用一个**显式列表 + `if(EXISTS)` 守卫**（**没有** `file(GLOB)`）把顶层
   lane 目录纳入构建：`src/syntax/provider`（已落地→真的编译）、`src/syntax/kde`（未落地→跳过）。
   `src/workspace/*` 仍由 `src/app/CMakeLists.txt` 负责（未重复纳入）。
2. 纳入后**真的编译、真的进 ctest**：`libvedit_syntax_provider.a`、
   `libvedit_syntax_test_fixture_grammar.a`、`vedit_syntax_provider_tests` 都出现在构建树；
   `ctest` 3/3 通过（`vedit-m0-smoke`、`vedit-syntax-provider-tests`、`vedit-app-shell-selftest`）。
3. **反例验证通过**：在隔离副本里让 provider lane 编译失败 → `bash scripts/build-qt.sh` **exit 1**，
   日志里有 `#error "T63 counter-example"` 与 `ninja: build stopped`。证明"lane 目录坏了门禁一定红"。

## 1. 根 CMake 的改动（唯一的产品改动）

```cmake
set(VEDIT_LANE_DIRECTORIES
  src/syntax/provider   # syn-provider lane (T15)
  src/syntax/kde        # syn-kde-data lane (T16), added once it lands
)
foreach(lane_dir IN LISTS VEDIT_LANE_DIRECTORIES)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${lane_dir}/CMakeLists.txt")
    add_subdirectory("${lane_dir}")
    message(STATUS "VEdit: building lane directory ${lane_dir}")
  else()
    message(STATUS "VEdit: lane directory ${lane_dir} not present yet; skipped")
  endif()
endforeach()
```

- 显式列表，**不是 GLOB**：构建不依赖磁盘上恰好有什么文件；lane 落地时在**同一个 PR** 里把目录
  加进列表（写在文件注释里）。
- `src/app` 的自动纳入逻辑原样保留（未改）；`src/workspace/*` 归 `src/app/CMakeLists.txt`。

### 顺带发现并修复：根工程必须启用 C 语言

首次纳入 provider 后，configure 通过、**generate 失败**：

```
CMake Error: CMake can not determine linker language for target: vedit_syntax_test_fixture_grammar
```

原因：provider 的测试夹具是 C 源码（`tests/fixture/parser.c`），而根 `project()` 只有
`LANGUAGES CXX`（standalone 构建时它自己的 `project(... LANGUAGES C CXX)` 掩盖了这个问题）。
修复：根 `project(VEdit VERSION 0.2.0 LANGUAGES C CXX)`。这是"只加 `add_subdirectory` 不够"的
直接证据——`CMAKE_C_COMPILER` 在镜像里已具备（build-essential），无需新增依赖。

## 2. 正向证据（绿）

| 文件 | 内容 |
|---|---|
| `10-green-configure.log` | `-- VEdit: building lane directory src/syntax/provider` / `src/syntax/kde not present yet; skipped` / `building src/app` |
| `11-green-build.log` | provider 5 个源文件 + `tests/fixture/parser.c`(C) + 测试可执行文件的编译/链接行 |
| `12-green-ctest.log` | `100% tests passed, 0 tests failed out of 3`（含 `vedit-syntax-provider-tests`） |

构建树产物（运行 `bash scripts/build-qt.sh` 后）：

```
target/qt6-build/src/syntax/provider/libvedit_syntax_provider.a
target/qt6-build/src/syntax/provider/libvedit_syntax_test_fixture_grammar.a
target/qt6-build/src/syntax/provider/vedit_syntax_provider_tests
```

## 3. 反例验证（红）

为避免改动其他 lane 的在飞文件，反例在**隔离副本**里做（复制根 `CMakeLists.txt`、`cmake/`、
`scripts/`、`src/` 到 `target/t63-counterexample/`，保持同一构建脚本/镜像），然后：

```sh
cat >> target/t63-counterexample/src/syntax/provider/CMakeLists.txt <<'EOF'
add_library(t63_counterexample_fail STATIC t63_counterexample_fail.cpp)
EOF
printf '#error "T63 counter-example: this lane deliberately fails to build"\n' \
  > target/t63-counterexample/src/syntax/provider/t63_counterexample_fail.cpp
bash target/t63-counterexample/scripts/build-qt.sh
# -> exit 1
# FAILED: .../t63_counterexample_fail.cpp.o
# /vedit-build/src/syntax/provider/t63_counterexample_fail.cpp:1:2: error: #error "T63 counter-example…"
# ninja: build stopped: subcommand failed.
```

原始输出：`30-counterexample-red.log`（`counterexample_build_exit=1`）。副本随后删除；**live 树
未受影响**，live 树的 `src/syntax/provider/CMakeLists.txt` 与源码未被修改（可 `git status` 复核）。

## 4. 说明与边界

- 只改了根 `CMakeLists.txt`（lane 列表 + `LANGUAGES C CXX`）与本目录；未改任何 lane 源码/CMake。
- `src/syntax/kde` 已预列入，T16 的 `CMakeLists.txt` 一出现就会被自动编译与测试（同一脚本）。
- 本次为验证门禁重跑了 `bash scripts/build-qt.sh`，它会重写 `evidence/m0-build/build-logs/30..36-*`
  （T62 事故后那批恢复日志）；这批是"同一命令的当前输出"，T62 归档时应以最新一次运行 + 头部
  标注为准，不要把旧恢复日志当成 T10 原件。
- 复跑：`bash scripts/build-qt.sh`（绿），`ctest` 应有 3 个测试；反例可按 §3 在副本中重放。
