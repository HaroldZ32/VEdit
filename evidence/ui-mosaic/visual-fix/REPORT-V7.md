# UI-Mosaic V7 视觉修复报告

分支：`feat/ui-mosaic`。

## 1. 测试期望值跟随冻结常量（`manager.dock-separator-gap`）

| # | 文件 | 位置 | 改动 |
|---|---|---|---|
| 1 | `src/app/ui_theme_selftest.cpp` | `manager.dock-separator-gap`（第 373–375 行） | 期望值 `gap == 8` → `gap == 16`；断言语义不变（仍检查分隔条间隙存在且等于设计值），上方加两行注释说明依据。本文件其余断言未动。 |

```diff
         const int gap = central->x() - dock->width();
-        check("manager.dock-separator-gap", gap == 8, QStringLiteral("gap=%1").arg(gap));
+        // 纵向分隔条高度 16px 是 CONTRACT.md §5 冻结的设计常量（横向仍 8px）：
+        // 8px 缝扣掉卡片阴影只剩约 2px 连续画布，达不到 ≥6px。改期望值是跟随设计，不是放宽判据。
+        check("manager.dock-separator-gap", gap == 16, QStringLiteral("gap=%1").arg(gap));
```

原因：视觉修复把纵向堆叠分隔条高度从 8px 改为 16px（横向仍是 8px），变更已冻结在
`.mpd/plans/ui-mosaic/CONTRACT.md §5「间隙语义」`；该节明确写明本断言的期望值随之
从 8 改为 16，且**仅此一处**。改期望值是让测试跟随设计常量，不是放宽判据。

## 2. 验证

### 2.1 全量构建 + ctest

命令：

```bash
bash scripts/build-qt.sh
```

结果：exit 0。ctest 汇总行（`evidence/m0-build/build-logs/33-qt6-ctest.log`）：

```text
100% tests passed, 0 tests failed out of 62

Total Test time (real) =  88.67 sec
```

该日志中本测试：

```text
16/62 Test #16: vedit-app-ui-theme-selftest ....................   Passed    0.04 sec
```

### 2.2 `vedit-app-ui-theme-selftest` 单独运行

命令（在构建镜像内，沿用 `run_in_qt6_container` 的参数）：

```bash
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/vedit-build -w /vedit-build \
  -e HOME=/tmp -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 \
  vedit-qt6-build:local bash -lc \
  'ctest --test-dir target/qt6-build -R vedit-app-ui-theme-selftest -V'
```

输出尾部（完整日志 `evidence/ui-mosaic/visual-fix/selftest-ui-theme-gap16.log`）：

```text
16: ok   manager.dock-separator-gap
16: ok   manager.dock-card-title-border
16: ok   manager.dock-card-body
16: ok   manager.dock-card-round-corner
16: PASS: 0 failure(s)
1/1 Test #16: vedit-app-ui-theme-selftest ......   Passed    0.04 sec

100% tests passed, 0 tests failed out of 1
```

## 3. 其它

- 本批次没有出现既有的 `vedit-rename-e2e (SIGPIPE)` 或
  `vedit-module-tree-catalog-test` flake：全量 62/62 一次通过。
- 未改其它文件、未改其它断言、未跑 `lupdate`。
