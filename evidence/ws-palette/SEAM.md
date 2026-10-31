collected_at: unknown (header added retroactively at 2026-10-31T10:19:00Z)
git_head: 9957f5fe4d63c17c681e8753288262fa2c00955e
git_status_porcelain:
 M CMakeLists.txt
 M Cargo.lock
 M Cargo.toml
 M THIRD_PARTY_NOTICES.md
 M docs/decisions/r2-license.md
 M docs/handover/README.md
 M evidence/m0-build/build-logs/21-qt6-image-id.txt
 M evidence/m0-build/build-logs/31-qt6-configure.log
 M evidence/m0-build/build-logs/32-qt6-build.log
 M evidence/m0-build/build-logs/33-qt6-ctest.log
 M evidence/m0-build/build-logs/34-qt6-elf.txt
 M evidence/syn-grammars/00-pins.txt
 M evidence/syn-grammars/01-build.log
 M evidence/syn-grammars/02-tests.log
 M evidence/syn-grammars/03-manifest.txt
 M evidence/syn-grammars/04-manifest-none.txt
 M evidence/syn-grammars/04-manifest-static.txt
 M evidence/syn-grammars/04-manifest-systemverilog.txt
 M evidence/syn-grammars/04-manifest-verilog.txt
 M evidence/syn-grammars/04-swap-proof.txt
 M evidence/syn-grammars/05-layering.txt
 M evidence/syn-grammars/06-licensing.txt
 M evidence/syn-grammars/07-conclusion.md
 M evidence/syn-grammars/09-result.txt
 M evidence/term-panel/00-pins.txt
 M evidence/term-panel/01-configure.log
 M evidence/term-panel/02-build.log
 M evidence/term-panel/03-ctest.log
 M evidence/term-panel/03b-checks.log
 M evidence/term-panel/04-elf-needed.txt
 M evidence/term-panel/05-no-network.txt
 M evidence/term-panel/06-xcb-and-screenshots.log
 M evidence/term-panel/07-conclusion.md
 M evidence/term-panel/08-structure.txt
 M evidence/term-panel/09-root-build-with-lane.log
 M evidence/ws-filetree/00-pins.txt
 M evidence/ws-filetree/00-steps.txt
 M evidence/ws-filetree/01-configure.log
 M evidence/ws-filetree/02-build.log
 M evidence/ws-filetree/03-ctest.log
 M evidence/ws-filetree/03a-tests-full.log
 M evidence/ws-filetree/04-fixture.log
 M evidence/ws-filetree/05-screenshots.log
 M evidence/ws-filetree/06-permissions-nonroot.log
 M evidence/ws-filetree/07a-gate-qt6-cxx-pre-seam.log
 M evidence/ws-filetree/08-real-shell-seamcheck.log
 M evidence/ws-filetree/09-real-app-install.log
 M evidence/ws-filetree/10-rootcheck-configure.log
 M evidence/ws-filetree/11-rootcheck-build.log
 M evidence/ws-filetree/12-gate-build-qt-final.log
 M evidence/ws-filetree/12a-gate-build-qt-first.log
 M evidence/ws-filetree/13-gate-qt6-cxx-final.log
 M evidence/ws-filetree/14-real-shell-check-run.log
 M evidence/ws-filetree/REPORT.md
 M evidence/ws-palette/00-pins.txt
 M evidence/ws-palette/30-build-qt.log
 M evidence/ws-palette/31-bench.log
 M evidence/ws-palette/32-ctest-palette.log
 M evidence/ws-palette/33-elf.txt
 M evidence/ws-palette/34-artifacts.txt
 M evidence/ws-palette/36-app-seam-diff.md
 M evidence/ws-palette/37-gate-qt6-cxx.log
 M evidence/ws-palette/REPORT.md
 M evidence/ws-palette/SEAM.md
 M evidence/ws-search/04-selfcheck-root.log
 M evidence/ws-search/05-selfcheck-nonroot.log
 M evidence/ws-search/06-screenshots.log
 M evidence/ws-search/07-screenshots-nonroot.log
 M evidence/ws-search/08-build-qt.log
 M evidence/ws-search/09-structure.txt
 M evidence/ws-search/10-ctest-root-search.log
 M evidence/ws-search/13-integration-proof.log
 M evidence/ws-search/14-standalone.log
 M evidence/ws-search/REPORT.md
 M scripts/build-qt-in-container.sh
 M scripts/build-qt.sh
 M src/app/CMakeLists.txt
 M src/app/main.cpp
?? cmake/VEditRustFfi.cmake
?? crates/vd-ffi/
?? crates/vd-search/
?? crates/vd-settings/
?? crates/vd-vcs/
?? docs/handover/appendix/amendments-rev2.md
?? evidence/docs-amendments/
?? evidence/ffi-abi/
?? evidence/fixture/
?? evidence/m0-build/build-logs/37-qt6-rust-abi.txt
?? evidence/verdicts/
?? scripts/verify-fixture.sh
?? tests/fixtures/
backfill: 补记 —— C9 头为事后补齐；git_head / git_status_porcelain 为补记时刻快照，非原始采集时刻；原始采集时刻不可回溯，文件 mtime 2026-10-31T04:07:29Z 仅供参考
---
# Workspace lane → `vedit-app` 接入契约（SEAM）

> 本文件是 ws-filetree / ws-palette / ws-search 三条 workspace lane 接入真实
> `vedit-app` 的**唯一权威约定**。由 T22（ws-palette）在 captain 授权下建立，
> 目的是让 **A（ws-filetree）与 C（ws-search）不需要改 `src/app/`**：
> `src/app/main.cpp` 只调用一次统一入口，其余全部在各自的 lane 目录里完成。
>
> 签名、目标名、命名空间以本文件为准；如与某条 lane 现有代码冲突，以本文件为准
> 并在该 lane 内改一行（不要改契约）。

## 0. 一句话

每条 workspace lane 在自己的目录里提供一个静态库 + 一个
`install<Lane>(ApplicationShell&, const IntegrationOptions&)` 函数；
`src/workspace/integration.cpp` 自动调用已落地的 lane；`main.cpp` 只写一行。

## 1. app 侧已经落地的三处（T22，captain 授权，其他 lane 不需要再动）

| 文件 | 改动 |
|---|---|
| `src/app/main.cpp` | 构造 shell 后加一行 `vedit::workspace::installPanels(shell, {options.projectRoot, options.stateDir});`（另加一个 `#include "integration.h"`） |
| `src/app/CMakeLists.txt` | 末尾新增 workspace 段：按 `if(EXISTS ...)` 纳入 `src/workspace/<lane>`，把已接线的 lane 库链接进 `vedit-app`，并给 `src/workspace/integration.cpp` 定义 `VEDIT_WORKSPACE_HAVE_<LANE>` |
| `src/workspace/integration.h` / `integration.cpp` | 统一入口 `installPanels()`；按宏逐条调用 `install<Lane>()`，缺失的 lane 自动跳过 |

`main.cpp` 里的调用（原样，勿改签名）：

```cpp
ApplicationShell shell(options);
vedit::workspace::installPanels(shell, {options.projectRoot, options.stateDir});
shell.show();
```

## 2. 一条 lane 必须提供的四样东西

以 `<lane>` = `filetree` / `palette` / `search` 为例：

### 2.1 目录

```
src/workspace/<lane>/
```

### 2.2 CMakeLists.txt + 静态库目标

目录里必须有 `CMakeLists.txt`（根 `src/app/CMakeLists.txt` 检测到它才纳入）。
目标名约定 `vedit_workspace_<lane>`；**已经用别的名字落地的 lane 不用改名**，
在 `src/app/CMakeLists.txt` 的映射表里有一行对应关系：

```cmake
set(VEDIT_WORKSPACE_FILETREE_TARGET vedit_file_tree)          # A 的现有目标名
set(VEDIT_WORKSPACE_PALETTE_TARGET  vedit_workspace_palette)  # 约定名
set(VEDIT_WORKSPACE_SEARCH_TARGET   vedit_workspace_search)   # 约定名
```

该 `CMakeLists.txt` 必须满足两种被 `add_subdirectory()` 的场景：

- **根构建**：`src/app` 之后被 `add_subdirectory`（因此可以假定
  `TARGET vedit_app_shell` 存在）；
- **独立构建**：`cmake -S src/workspace/<lane> -B <dir>` 也能配置（证据用）。
  推荐用 `if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)` 分支。

lane 库**不得**链接 KTextEditor / tree-sitter：`vedit-app` 的 `readelf -d` NEEDED
里这两个必须保持 0（T13 的保证，不能破）。需要编辑器状态时走回调/接缝，不要
把编辑器依赖拖进 app 层。

### 2.3 入口头文件 `src/workspace/<lane>/<lane>_integration.h`

文件名必须是 `<lane>_integration.h`，**函数必须声明在 `vedit::workspace`
命名空间**（不是 `vedit::workspace::<lane>` 子命名空间——`integration.cpp` 按
`vedit::workspace::install<Lane>` 调用）：

```cpp
#pragma once

#include "../integration.h"          // IntegrationOptions

namespace vedit::app { class ApplicationShell; }

namespace vedit::workspace {

void installFileTree(vedit::app::ApplicationShell &shell,
                     const IntegrationOptions &options);

} // namespace vedit::workspace
```

函数名规则：`install` + lane 名的首字母大写（`installFileTree` /
`installPalette` / `installSearch`）。`integration.cpp` 里已有三条对应调用：

```cpp
#if defined(VEDIT_WORKSPACE_HAVE_FILETREE)
    installFileTree(shell, options);
#endif
```

### 2.4 实现 `src/workspace/<lane>/<lane>_integration.cpp`

- 只在根构建里编译（lane 自己的 CMakeLists 用 `if(TARGET vedit_app_shell)`
  守卫，参考 `src/workspace/palette/CMakeLists.txt`）。
- 可以 include `app_shell.h` / `panel_registry.h` / `command_registry.h`。
- **dock 面板**（file-tree / search / terminal / vcs / settings / outline）：
  只注册工厂，不要自己插入 dock、不要改布局：

  ```cpp
  void installFileTree(ApplicationShell &shell, const IntegrationOptions &options)
  {
      if (shell.panels() == nullptr) {
          return;
      }
      const QString root = options.projectRoot;
      shell.panels()->setFactory(QStringLiteral("file-tree"),
                                 [root]() { return makeFileTreePanel(root); });
  }
  ```

  已注册的 6 个面板 id 与默认区域见 `evidence/app-shell/06-seams.md` §2；
  工厂返回的 widget 由 shell 接管生命周期（父对象由 dock 设置）。
  面板保持“按需插入”：安装函数**不要**调用 `showPanel()`。

- **overlay 面板**（命令面板这类不是 dock 的 UI）：通过 shell 的
  `CommandRegistry` 注册命令（这样键位可被 `IShortcutStore` 重绑），再
  `shell.addAction(action)` 让窗口级快捷键生效；菜单是 shell 构造时建好的，
  需要出现在菜单里的命令自己插进去（参考
  `src/workspace/palette/palette_integration.cpp::registerCommand()`）。
  示例：`file.goto-file`（默认 `Ctrl+P`）。

- 安装函数必须**幂等且可重入安全**（`installPanels` 每次进程只调一次，但
  selftest 会重复 install）；重复 install 时更新项目根并返回已有对象。

- 线程：安装函数在 GUI 线程、`shell.show()` 之前调用；不要在这里做长扫描。
  需要磁盘扫描时懒加载（palette 的做法：第一次 `Ctrl+P` 才扫一次）。

## 3. CMake 如何自动接线（lane 不需要改 `src/app`）

`src/app/CMakeLists.txt` 末尾的 workspace 段逻辑：

1. `VEDIT_WORKSPACE_LANES = filetree palette search`；对每个 lane，若
   `src/workspace/<lane>/CMakeLists.txt` 存在 → `add_subdirectory`；
2. 若 `src/workspace/integration.cpp` 存在 → 建 `vedit_workspace_integration`，
   并逐 lane 检查：
   - 该 lane 的库目标存在（约定名或映射名），**且**
   - `src/workspace/<lane>/<lane>_integration.cpp` 存在
   才 `target_link_libraries` + 定义 `VEDIT_WORKSPACE_HAVE_<LANE>=1`；
3. `vedit-app` 链接 `vedit_workspace_integration`。

也就是说：**A 只需要新增 `filetree_integration.h/.cpp` 并把 `.cpp` 加进自己的
`VEDIT_FILETREE_SOURCES`（仅当 `TARGET vedit_app_shell`）**，根构建就会自动
调用 `installFileTree`，`src/app` 一行都不用动。

配置日志里应出现：

```
VEdit: workspace lane 'filetree' included
VEdit: workspace lane 'palette' included
VEdit: workspace lane 'search' included
VEdit: workspace lane 'filetree' wired (vedit_file_tree)
VEdit: workspace lane 'palette' wired (vedit_workspace_palette)
VEdit: workspace lane 'search' wired (vedit_workspace_search)
VEdit: workspace integration enabled (src/workspace/integration.cpp)
```

## 4. 三条 lane 的现状对照

| lane | 库目标 | 入口文件 | 入口函数 | 状态 |
|---|---|---|---|---|
| ws-filetree (A) | `vedit_file_tree` | **缺** `filetree_integration.{h,cpp}` | `installFileTree` | 需补：照 §2.3/2.4 加两个文件，并把 `filetree_integration.cpp` 加入源列表（`if(TARGET vedit_app_shell)` 守卫） |
| ws-palette (B) | `vedit_workspace_palette` | `palette_integration.{h,cpp}` | `installPalette` | 已接线（本 lane 交付） |
| ws-search (C) | `vedit_workspace_search` | `search_integration.{h,cpp}` | `installSearch` | 入口签名已符合；内部 `search::createSearchPanel(...)` 当前报 “is not a member of `vedit::workspace::search`”，在 `search_panel.h` 里把 `createSearchPanel` 声明进 `vedit::workspace::search`（或按声明处命名空间调用）即可，与接缝无关 |

## 5. 验证（lane 自己就能跑）

```sh
# 1) 全量（pinned 镜像内 configure + build + ctest + xcb 冒烟）
bash scripts/build-qt.sh

# 2) 只静态检查（快）
bash build/gates/qt6-cxx.sh --no-test

# 3) 确认 app 没有多链编辑器/语法引擎（两项计数必须都是 0）
readelf -d target/qt6-build/src/app/vedit-app | grep -E 'KTextEditor|tree-sitter' | wc -l
```

ctest 里每条 lane 自己的 selftest 必须仍然绿；根构建会把它们一起跑。

## 6. 参考实现

- 最完整样例：`src/workspace/palette/`（core + `palette_integration.cpp` +
  `palette_integration_selftest.cpp` + CMake 双模式）。
- dock 面板样例：`src/workspace/search/search_integration.cpp`。
- 面板工厂约定：`evidence/app-shell/06-seams.md` §2。
