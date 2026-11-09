# UI-Mosaic V5 视觉修复报告 —— 纵向卡片间隙 + 四边阴影

分支：`feat/ui-mosaic`。任务：三处编辑 + 构建 + 真实截图像素测量。

## 1. 改了什么（只动这三个文件）

| # | 文件 | 位置 | 改动 |
|---|---|---|---|
| 1 | `resources/themes/cards.qss` | `QMainWindow::separator` | `height` 由 `@gap@`(8px) → **16px**；`width: @gap@` 不动（横向间隙已达标，且 `@gap@` 令牌还被 6 处引用） |
| 2 | `src/app/panel_registry.cpp` | `makeCardShadow()` | 注释改成与代码一致：**浅色 α22 / 深色 α220**（原文写 150，已删除）；`setBlurRadius(light ? 12 : 14)`（第一轮先试 8/10，见 §3） |
| 3 | `src/app/editor_area.cpp` | `EditorArea` 构造里的 `singleShot(0)` | 同步 `setBlurRadius(light ? 12 : 14)` 与注释（V5：blur 12/14，浅色 α22 / 深色 α220） |

`offset (0,1)` 与 alpha `light ? 22 : 220` 未改；没有动测试、`ui_theme_selftest.cpp`、`product_theme.*`、`@gap@` 令牌或 `lupdate`。

> 说明：工作树在本任务开始前已有未提交改动（`product_theme.*`、cards.qss 的 V1 卡片体规则、panel_registry 的 `veditDockCard` 等）。它们不是本任务写的，本报告只对上面三处负责；`git diff --stat` 里其余条目请按既有未提交工作看待。

## 2. 两轮参数

| 轮次 | separator height | blur（浅/深） | 结果（yunwu） |
|---|---|---|---|
| 第一轮 | 14px | 8 / 10 | a = **10px** 达标；b 上 **1**、下 4、左 **2**、右 3 → 上/左不达标 |
| 第二轮 | 16px | 12 / 14 | a = **10px**；b 上 **3**、下 **5**、左 **4**、右 **4** → 全部达标 |

第一轮把 blur 收窄后，纵向画布确实够了，但阴影带也跟着变窄（offset (0,1) 下上边只剩 1px、左边 2px）。
复核脚本 `evidence/ui-mosaic/review-visual/analyze.py` 的 C 项也是从描边外侧向外数阴影带，其修复方向是
"把阴影带做宽 1–2px（blur 12–14）"。所以第二轮按该方向放宽 blur，同时把 separator 提到任务允许的
16px 兜底，保证间隙中段仍 ≥6px。

## 3. 实测数字（真实运行截图，Xvfb + xcb + 真二进制）

命令：

```bash
bash evidence/ui-mosaic/screenshots/capture-themes.sh yunwu yaoshi
python3 evidence/ui-mosaic/visual-fix/measure-shadow-gap.py --both \
        --json evidence/ui-mosaic/visual-fix/measure-round2.json
```

判定口径：`a` = x=1100 列、Module Tree 卡底描边与 Interface 卡顶描边之间，与 canvas 色最大通道差
`Δ≤3` 的最长连续像素数；`b` = 从 Interface 卡四条外描边向外扫，与 canvas `Δ≥3` 的连续像素数。
几何由脚本从截图自动检测（三张卡的 1px 描边），不写死 y 坐标。

### 3.1 yunwu（canvas `#F4F4F2`）—— 达标

| 指标 | 实测 | 门限 | 结论 |
|---|---|---|---|
| a 纵向连续画布（x=1100） | **10px** | ≥6 | **PASS** |
| b 上 / 下 / 左 / 右阴影 | **3 / 5 / 4 / 4 px** | 每边 ≥3 | **PASS** |

### 3.2 yaoshi（canvas `#131315`）—— 仍有不达标项

| 指标 | 实测 | 门限 | 结论 |
|---|---|---|---|
| a 纵向连续画布（x=1100） | **16px** | ≥6 | PASS |
| b 上 / 下 / 左 / 右阴影（Interface 卡） | **0 / 0 / 0 / 0 px** | 每边 ≥3 | **FAIL（既有问题，参数调不动）** |

原因（像素证据）：右栏 **Outline** 卡的深色阴影是 2 / 5 / 4 / 4 px，而 **Module Tree / Interface**
是 0 px。Outline 是启动时 `applyThemeForStartup()` **之后**才创建的 dock；Module Tree / Interface
在主题调色板应用**之前**创建，`makeCardShadow()` 里的 `owner->palette()` 当时还是浅色默认调色板，
于是拿到 `light ? 22 : 220` 的 **α22** 分支。α22 的黑阴影在 `#131315` 画布上理论最大 Δ≈1.6，
**永远到不了 Δ≥3**，所以靠 blur / separator 重试无法让深色四边达标；需要把 dock 的阴影创建
推迟到主题应用之后（`editor_area.cpp` 已用 `QTimer::singleShot(0, ...)` 这样处理），这是本任务
"三处编辑"之外的顺序问题，未改。

## 4. 构建

命令：`bash scripts/build-qt.sh --no-test`（构建目录独占；日志
`evidence/ui-mosaic/visual-fix/build-round2.log`）。**exit 0**。尾部 20 行：

```text
 0x0000000000000001 (NEEDED)             Shared library: [libQt6Widgets.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libQt6Core.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libtree-sitter.so.0.25]
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
## highest GLIBC symbol the binary itself requires (informational; the private runtime supplies it)
GLIBC_2.34
## libraries resolved at build time
	libKF6TextEditor.so.6 => /usr/lib/x86_64-linux-gnu/libKF6TextEditor.so.6 (0x000070f9395e7000)
	libKF6SyntaxHighlighting.so.6 => /usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6 (0x000070f9393fb000)
	libQt6Core.so.6 => /usr/lib/x86_64-linux-gnu/libQt6Core.so.6 (0x000070f938656000)
	libtree-sitter.so.0.25 => /usr/lib/x86_64-linux-gnu/libtree-sitter.so.0.25 (0x000070f938627000)
	libstdc++.so.6 => /usr/lib/x86_64-linux-gnu/libstdc++.so.6 (0x000070f938385000)
	libc.so.6 => /usr/lib/x86_64-linux-gnu/libc.so.6 (0x000070f938136000)
## sha256
ec570d7d2ec94f790a42fcaf989d2c282fbc191ac4e0aa2cab743125e359ee39  /vedit-build/target/qt6-build/cmake/smoke/vedit-m0-smoke
[qt6] --no-tests: ctest and the xcb smoke were not run
[qt6] PASS: Rust service layer + configure + build (warnings-as-errors) inside vedit-qt6-build:local; tests skipped (--no-test)
[qt6] evidence: evidence/ffi-abi/logs/ (Rust layer) and evidence/m0-build/build-logs/2x-qt6-*, 3x-qt6-*
```

按任务要求只编译（`--no-test`），**ctest / 门禁未跑**，这一点不冒充已验证。

## 5. 是否仍有不达标项

- **yunwu：无。** a=10px，四边 3/5/4/4 px。
- **yaoshi：有。** 右栏上两张卡的深色阴影四边均为 0px（任务口径 Δ≥3），根因是 dock 在主题应用前
  创建、阴影走了 α22 分支；该问题不在三处编辑内，且参数无法修复。Outline 卡（主题后创建）实测
  2/5/4/4 px，可作为根因的对照证据。
- 深色主题的阴影观感是否可接受，属于既有设计取舍；本任务未改变其 α 分支逻辑。

## 6. 复跑

```bash
bash scripts/build-qt.sh --no-test
bash evidence/ui-mosaic/screenshots/capture-themes.sh yunwu yaoshi
python3 evidence/ui-mosaic/visual-fix/measure-shadow-gap.py --both
```

产物：`evidence/ui-mosaic/visual-fix/measure-round2.json`、`build-round2.log`、
`capture-round2.log`，以及重新生成的 `evidence/ui-mosaic/screenshots/after-{yunwu,yaoshi}.png`。
