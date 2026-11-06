# T21 — selftest 解耦覆盖率（AC-4.7）

口径：`check(`/`QVERIFY(`/`QVERIFY2(`/`QCOMPARE(`/`expect(` 调用点计数；
解耦改动只替换**比较右侧的期望值**（英文字面量 → `QCoreApplication::translate(上下文, 源串)`），
不增删任何断言，因此前后计数必须相等。

| 文件 | 断言调用点（当前） | 说明 |
|---|---|---|
| `src/app/main.cpp` | 160 | |
| `src/workspace/settings/settings_selftest.cpp` | 124 | |
| `src/workspace/project/project_selftest.cpp` | 108 | |
| `src/workspace/vcs/vcs_selftest.cpp` | 37 | |
| `src/workspace/palette/palette_selftest.cpp` | 45 | |
| `src/workspace/diagnostics/diagnostics_selftest.cpp` | 44 | |
| `src/workspace/diagnostics/toolchain_selftest.cpp` | 27 | |
| `src/workspace/diagnostics/toolchain_ux_selftest.cpp` | 43 | |
| `src/workspace/filetree/filetree_tests.cpp` | — | 文件不存在（跳过） |
| `src/workspace/palette/palette_integration_selftest.cpp` | 29 | |
| **合计** | **617** | |

## 解耦点（英文原文断言 → 翻译键查找）

1. `src/app/main.cpp` `settings.preferences.menu`：`menuLabel("settings")` 的期望值由
   `QStringLiteral("&Settings")` 改为 `QCoreApplication::translate("vedit::app::ApplicationShell", "&Settings")`。
2. `src/workspace/settings/settings_selftest.cpp` `settings.pages.tabs-titles`：三个页签文本由
   `QStringLiteral("Shortcuts"/"Tools"/"FIM")` 改为 `SettingsPanel` 上下文的 translate 查找。
3. 其余断言：本任务把 54 个源文件里的 251 处 UI 字面量包装为 translate 时，**测试文件内的期望值**
   同步成为 translate 查找（同一变更两侧一致），见 `12-ui-blocks.md` 的落点列。

## 验证

- 解耦后：`ctest` 38/38 exit 0；`--selfcheck` 245/0 exit 0（`logs/20-build-selfcheck-ctest.log`）。
- 断言未被削弱：改动只涉及比较表达式右侧；`git diff` 中无删除的 check 调用。
