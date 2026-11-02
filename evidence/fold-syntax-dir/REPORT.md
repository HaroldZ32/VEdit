# T109 — 修产品侧真实缺陷：`VEDIT_SYNTAX_DIR` 数据根归一化（折叠接线）

collected_at: 2026-11-01T16:12:06+0800
collected_at_utc: 2026-11-01T08:12:06Z
git_head: 206033c1faa91ac6b89cca0d575d36283ab69d7f
git_status_porcelain:
  M build/evidence/typecheck/00-run.txt
  M build/evidence/typecheck/10-typecheck-image-build.log
  M build/evidence/typecheck/11-typecheck-image-id.txt
  M crates/vd-ffi/include/vd_ffi.h
  M crates/vd-ffi/src/service.rs
  M evidence/ffi-abi/logs/10-rust-image.txt
  M evidence/ffi-abi/logs/19-rust-stage-console.log
  M evidence/ffi-abi/logs/20-cargo-build-release.log
  M evidence/ffi-abi/logs/21-staticlib.txt
  M evidence/ffi-abi/logs/23-cargo-test.log
  M evidence/ffi-abi/logs/26-vd_ffi.h
  M evidence/ffi-abi/logs/27-vd_ffi-comment-free.txt
  M evidence/m0-build/build-logs/20-qt6-image-build.log
  M evidence/m0-build/build-logs/21-qt6-image-id.txt
  M evidence/m0-build/build-logs/31-qt6-configure.log
  M evidence/m0-build/build-logs/32-qt6-build.log
  M evidence/m0-build/build-logs/33-qt6-ctest.log
  M evidence/m0-build/build-logs/34-qt6-elf.txt
  M evidence/m0-build/build-logs/37-qt6-rust-abi.txt
  M packaging/appimage/README.md
  M packaging/runtime/AppRun
  M packaging/runtime/README.md
  M packaging/runtime/assemble-appdir.sh
  M packaging/runtime/build.sh
  M packaging/runtime/selfcheck.sh
  M packaging/runtime/verify-f2-isolation.sh
  M packaging/runtime/verify-not-shipped.sh
  M src/editor/kernel/fold_breadcrumb.cpp
  M src/syntax/folding/CMakeLists.txt
  M src/syntax/folding/ksyntax_fold_source.cpp
  M src/syntax/folding/ksyntax_fold_source.h
  M src/syntax/kde/CMakeLists.txt
  M src/syntax/kde/ksyntax_data.cpp
  M src/workspace/palette/README.md
  M src/workspace/search/CMakeLists.txt
  M src/workspace/search/README.md
  M src/workspace/search/global_search.cpp
  M src/workspace/search/global_search.h
 ?? evidence/fold-syntax-dir/
 ?? evidence/slice1-stop/81-typecheck-t106.log
 ?? evidence/slice1-stop/82-build-qt-t106.log
 ?? evidence/slice3-pack/final-206033c/
 ?? evidence/verdicts/fim-async/
 ?? packaging/runtime/hostmatrix.sh
 ?? src/syntax/common/
 ?? src/syntax/folding/tests/syntax_directory_tests.cpp
 ?? src/syntax/kde/tests/syntax_directory_equivalence.cpp
git_head_note: 本报告头的 git_head/porcelain 是**落盘时刻**快照；每份日志的 C9 头记录的是**采集时刻**的 HEAD/porcelain（工作树中有其它 lane 在飞，HEAD 也可能在采集后前进）。两者不同不是矛盾，日志头为准。

## 0. 结论（一句话）

`FoldBreadcrumbBinding::definitionDirectory()` 从“把 `VEDIT_SYNTAX_DIR` 当 `syntax` 子目录”改为**与 T84
同口径的数据根归一化**，并且把这段归一化抽成**一处共享工具**
`src/syntax/common/syntax_directory.{h,cpp}`，由 `FoldBreadcrumbBinding` 与 `KsyntaxFoldSource`
共同使用。修后，**只含 `<root>/syntax/vedit-*.xml` 的 T84 数据根布局**（即拆掉打包侧双份拷贝的布局）
在真产品里 `fold.wiring.closed.hidden-975` **PASS（975/975）**、`registry=200`、`APP-SHELL 200/0`。

**T31 可以移除双份拷贝。**

## 1. 病因（先诊断）

| 组件 | 修复前 | 问题 |
|---|---|---|
| `FoldBreadcrumbBinding::definitionDirectory()`（`src/editor/kernel/fold_breadcrumb.cpp`） | `VEDIT_SYNTAX_DIR` **原样返回**，然后拼 `<env>/vedit-*.xml` | 假设 env 是 `syntax` 子目录；T84 契约是数据根 → 包内找不到 XML，closed source 加载失败（`injected_ranges=0`） |
| `KsyntaxFoldSource`（T29） | 由调用方传 `definitionPath` | 本身无辜，但它拿到的路径来自上面那层错误假设 |
| `ksyntax_data.cpp`（T84） | 自己内联了“数据根/子目录”归一化 | 与折叠接线**各写一套**，于是出现了口径漂移 |

修复前实测（`01-prefix-data-root-fail.log`，真产品 + 仅 `<root>/syntax/` 的 T84 布局）：

```
FOLD-WIRING closed_state injected_ranges=0 hidden_in_viewport=0 scroll_max=958
CHECK fold.wiring.closed.observed FAIL
CHECK fold.wiring.closed.hidden-975 FAIL hidden=0
CHECK fold.wiring.closed.registry-200 FAIL registry=0
APP-SHELL-SELFTEST ok=0 checks=200 failures=3
```

（T31 报的是 2 项 FAIL；本机复现是 **3 项**——`observed` 也一起红了。以实测为准。）

## 2. 修复（写域内）

| 文件 | 改动 |
|---|---|
| `src/syntax/common/syntax_directory.h/.cpp`（新，中性共享工具，T110 上移；命名空间 `vedit::syntax`） | `resolveSyntaxDefinitionDirectory(value)`：与 T84 同款归一化——`<value>/syntax` 存在 → 用它；否则 basename 为 `syntax` → 直接用；否则空。`syntaxDefinitionFileName(mode)`；`applicationSyntaxDefinitionDirectory()`：`VEDIT_SYNTAX_DIR` → 归一化，失败时回落到源码 checkout 的 `resources/syntax`（裸构建） |
| `src/syntax/folding/ksyntax_fold_source.h/.cpp` | 新增 `definitionDirectoryFromEnvironment()` / `definitionPathForMode(mode)`，内部走同一共享工具（不再各自拼路径） |
| `src/editor/kernel/fold_breadcrumb.cpp` | `definitionDirectory()` 改为调用共享工具；`definitionFileName()` 改为共享的 mode→文件名映射；删除本地 walk-up（移入工具） |
| `src/syntax/common/CMakeLists.txt`（新） | 最小静态库 `vedit_syntax_common`；由 folding / kde 两侧在缺少目标时 add_subdirectory |
| `src/syntax/kde/CMakeLists.txt` + `tests/syntax_directory_equivalence.cpp`（新，T110） | kde loader 链接共享工具；新增等价性测试：loader 的 `search_paths`/`private_dir` 对两种拼写都与共享工具一致、flat root 仍被拒且诊断保留 |
| `src/syntax/folding/CMakeLists.txt` + `tests/syntax_directory_tests.cpp` | 共享工具的 8 条无头单测（数据根归一化、子目录透传、缺失路径、空串、**flat root 拒绝**、mode→文件名、裸构建回落） |

**未改**：`packaging/**`（T31 写域）、`src/workspace/**`、`src/syntax/kde/ksyntax_data.*`（T84 成果；
是否进一步统一见 §5）、其他 lane 在飞文件。

## 3. 验收（判据未降）

> 判据保持 `fold.wiring.closed.hidden-975`（975 = 25×39），未下调、未改断言。

| 场景 | 布局 | 结果 |
|---|---|---|
| **拆掉双份拷贝** | 仅 `<root>/syntax/vedit-*.xml`（T84 数据根） | `hidden_in_viewport=975`、`hidden-975 PASS`、`registry-200 PASS`、`APP-SHELL 200/0`（`03-postfix-data-root-only.log`） |
| 兼容旧拼写 | `VEDIT_SYNTAX_DIR=<root>/syntax` | 同上（`04-postfix-legacy-subdir.log`） |
| 无 env（裸构建） | checkout `resources/syntax` | 同上（`05-postfix-no-env.log`） |
| 旧双份拷贝仍在 | XMLs 同时在 root 与 `syntax/` | 同上（root 副本被忽略，`06-postfix-double-copy-compat.log`）——向后兼容 |
| flat root（无 `syntax/` 子目录） | XMLs 只在 root | **resolver 拒绝**（单测 `syntax-dir.flat-root-without-syntax-child-rejected`）；开发机上产品运行会回落到 checkout 而“看起来绿”，交付 AppDir 无此回落（`07-flat-root-scenario.log` 里如实标注） |

共享工具单测：`SYNTAX-DIRECTORY-TESTS ok=1 checks=8 failures=0`（`02-fix-unit-tests.log`）。

## 4. 门禁（自跑输出）

| 门禁 | 结果 |
|---|---|
| `ctest -R "vedit-syntax-directory-tests\|vedit-folding-tests"` | **2/2 Passed**（`08-ctest.log`） |
| 根 ctest（T109 时） | **26/26 Passed**（`09-root-ctest.log`） |
| 根 ctest（T110 统一后） | **27/27 Passed**（`12-t110-ctest.log`，新增 kde 等价性测试） |
| `bash build/gates/typecheck.sh`（T109 时） | **TYPECHECK-EXIT=0**（`10-typecheck.log`） |
| `bash build/gates/typecheck.sh`（T110 统一后） | **TYPECHECK-EXIT=0**（`13-t110-typecheck.log`） |

## 5. T110：三方统一（裁定 ②，已完成）

- 共享工具已上移到 **`src/syntax/common/syntax_directory.{h,cpp}`**（中性的 `src/syntax/common` 层，
  命名空间 `vedit::syntax`），新增最小 CMake 目标 `vedit_syntax_common`。
- 三方都已改为调用同一个函数：
  1. `src/editor/kernel/fold_breadcrumb.cpp`；
  2. `src/syntax/folding/ksyntax_fold_source.cpp`；
  3. **`src/syntax/kde/ksyntax_data.cpp`（T84；按授权只替换归一化那一段）**——保留
     `private syntax directory missing: %1/syntax (set VEDIT_SYNTAX_DIR)` 诊断文本、`addCustomSearchPath`、
     `report_.search_paths` / `report_.private_dir` 语义；**未动**禁运白名单/sha256 黑名单/`report_.ok` 判定。
- 依赖方向正确：`kde` 与 `folding` 都只向下依赖 `common`，没有出现“底层依赖上层”。
- 等价性断言（`12-t110-ctest.log` 里 4 个测试全绿）：
  - `syntax_directory_tests`：8/0（含 flat-root 拒绝）；
  - `vedit-syntax-directory-equivalence`：8/0 —— loader 的 `search_paths` 对
    `<root>` 与 `<root>/syntax` 两种拼写都等于共享工具结果、`private_dir` 一致；flat-root 下
    loader 不收录 search path 且保留原诊断。
- 三条布局复跑（`11-t110-three-layouts.log`）：仅 `<root>/syntax/`、旧 `<root>/syntax` 拼写、
  双份拷贝 —— 三种都 `hidden-975` / `registry-200` / `APP-SHELL 200/0` PASS（判据未降）。

## 6. 证据文件（均带 C9 头：collected_at/UTC/HEAD/porcelain）

`01-prefix-data-root-fail.log`、`02-fix-unit-tests.log`、`03-postfix-data-root-only.log`、
`04-postfix-legacy-subdir.log`、`05-postfix-no-env.log`、`06-postfix-double-copy-compat.log`、
`07-flat-root-scenario.log`、`08-ctest.log`、`09-root-ctest.log`、`10-typecheck.log`。
复现布局：`/tmp/t109-data`（仅 `syntax/`）、`/tmp/t109-dupe`（双份）、`/tmp/t109-flat`（仅 root），
日志里给出了每次运行的 `VEDIT_SYNTAX_DIR` 与关键 CHECK 行。T110 追加：`11-t110-three-layouts.log`、
`12-t110-ctest.log`、`13-t110-typecheck.log`。

**flat-root 陷阱（保留并强调）**：`07-flat-root-scenario.log` 里的说明仍然是有效警告——开发机上
`applicationSyntaxDefinitionDirectory()` 会回落到 checkout 的 `resources/syntax`，于是 flat-root 配置
“看起来绿”；**交付 AppDir 没有这个回落**，flat-root（XML 只在数据根、无 `syntax/` 子目录）在那里会真正失败。
不要用开发机的绿色来判断打包布局。
