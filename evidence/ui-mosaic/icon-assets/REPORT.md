# UI-Mosaic · 图标资产与打包全链路（icon-assets）证据报告

- 车道：`icon-assets`（写入方 C），契约 `.mpd/plans/ui-mosaic/CONTRACT.md` §3，需求 `REQUIREMENTS.md` A4
- 任务：共享看板 T1
- 状态：**本机可执行部分全部通过**；4 项需要完整构建/打包环境的事项未执行，逐条列在 §8
- 证据目录：`evidence/ui-mosaic/icon-assets/`（本文件 + `logs/` + `icon-chain-sim.sh`）
- 环境：Python 3.14.4 / Pillow 12.1.1；宿主无 Qt6/KF6 开发头文件（见 §8.3）

---

## 1. 产物清单（sha256 全部取自脚本自身输出 `logs/build-app-icons-run2.log`）

### 1.1 PNG（14 个，全部 8-bit/color RGBA）

| 路径 | 尺寸 | sha256 |
|---|---|---|
| `resources/icons/src/vedit-dark.png` | 2160x2160 | `fd53e0570b7a8f5a67fbb862862ab5577f8579a8c706832adbdc843601a7e704` |
| `resources/icons/src/vedit-light.png` | 2160x2160 | `da475ebe50841d59ca45333b562d55f6b51a5ef81828222e1569cf558138fa47` |
| `resources/icons/src/vedit-mono.png` | 2160x2160 | `ab20f26a4d04935b01b18c495829de8008a92e93d667b249a473785f81f1ec54` |
| `resources/icons/app/vedit-dark.png` | 512x512 | `f3509db824b38d02d970b0c2d191187bdaa49879adedd94936a8d13819d4982e` |
| `resources/icons/app/vedit-light.png` | 512x512 | `2ec5b469ae311bf73e95b2574e16619f0c87407a563b1928b527a0cf7692c4b8` |
| `resources/icons/app/vedit-mono.png` | 512x512 | `529175898f2ac0b4a42949fc7a1c6c7daf3d7ecdad4742badeb1539be8e44841` |
| `resources/icons/hicolor/16x16/apps/io.github.vedit.VEdit.png` | 16x16 | `8d7c7dc1d887a6a394f5c13fd70283e04ec356f8a4bf9c1ae1a2bba319701b51` |
| `resources/icons/hicolor/24x24/apps/io.github.vedit.VEdit.png` | 24x24 | `2370e425aab60aa2c6f47f0b2da1e9627d2dfd51b0597cb301aff18835fd653e` |
| `resources/icons/hicolor/32x32/apps/io.github.vedit.VEdit.png` | 32x32 | `029bd674c4338cc955c7845c0040ae9193dccd0a38d8b6adf72a6eba4b3d596b` |
| `resources/icons/hicolor/48x48/apps/io.github.vedit.VEdit.png` | 48x48 | `32f60f0b519ea8e9f982b6f88958766d0d06f6849855cf52fb136ba41b7aca74` |
| `resources/icons/hicolor/64x64/apps/io.github.vedit.VEdit.png` | 64x64 | `d011f0b459948885b3808df07f94e5984e038c7d3d15b0fa8dfa5237fd114d21` |
| `resources/icons/hicolor/128x128/apps/io.github.vedit.VEdit.png` | 128x128 | `e035cecb941ba5358203def3c1fa01a72f8119fdb9a85f49be12c96f525e497c` |
| `resources/icons/hicolor/256x256/apps/io.github.vedit.VEdit.png` | 256x256 | `3e7efb5b041332e6530e9e7b1bf21fa53c02b355a4406b854c2b9e80f37c4eb3` |
| `resources/icons/hicolor/512x512/apps/io.github.vedit.VEdit.png` | 512x512 | `2ec5b469ae311bf73e95b2574e16619f0c87407a563b1928b527a0cf7692c4b8` |

- `src/` 三份是输入原图的**逐字节副本**（1=dark、2=light、3=mono），已用 sha256 复核：
  1.png→vedit-dark.png、2.png→vedit-light.png、3.png→vedit-mono.png 三对全部相等。
- `hicolor/512x512` 与 `app/vedit-light.png` 字节相同（同一 512 主图，Lanczos 只做一次）。
- 另有 `resources/icons/actions/*.svg` 15 个（下表）。

### 1.2 SVG（15 个，24x24，`stroke="currentColor"` / `fill="none"` / `stroke-width="1.75"`）

| 文件 | sha256 | 造型 |
|---|---|---|
| `new.svg` | `d01746748117cad471e0373c55b90c4bd8ac491ae688c0002ea47df3112b224d` | 文档 + 折角 + 加号 |
| `open.svg` | `2ff7955075c3139ecc66ad508b110030dfe89eed1ef25af3a14d6a629ddc2a3b` | 文件夹 + 上箭头 |
| `save.svg` | `f86b48ae874f96333212a640add685c467bd2ef8079a78316aa3bc9f5d3ae306` | 软盘（含写入窗/标签） |
| `save-as.svg` | `f0a42a1b082a855e2c3330af1358cc7ae33ea28f86c9872ea6175aa50eca12d7` | 软盘 + 铅笔 |
| `undo.svg` | `af79920a1e5d56f496c404e5480ac7a31e1f6b613831416827b60a7fb8ef5770` | 左弯箭头 |
| `redo.svg` | `fb826ac5d720c20b2c5f90fb5eda0b2ecc83ffb594112c7a19695814ebd946c9` | 右弯箭头 |
| `find.svg` | `e45e6c273a88a4f3381f3515a4c25787570b3eedb26b51279280d8e23d7a2465` | 放大镜 |
| `replace.svg` | `a119046b6fc291cc43bf42acb14d37bf9b89903fb21f95c88b4d1cf2c4a248ac` | 双向交换箭头 |
| `sidebar.svg` | `b0de14fb0cd687992c2e5fad24af69e5acc06fdd7336ad621f42a94792f89fe0` | 矩形 + 左分栏 |
| `terminal.svg` | `2668f612849a47655ebe93df20930de3bdbb03c4c361a7ef29c9b54dbaf1d096` | 终端窗口 + 提示符 |
| `build.svg` | `eae0c3594d37b412caffc6c77003e1a93bbf0189acf41188f875d954ae0e30a1` | 立方体（构建产物） |
| `vcs.svg` | `6494956a7cad7cf0838120b1211c4738a05222d071f918c0a53c0785a51603b5` | 分支（两节点 + 曲线，git-branch 造型） |
| `settings.svg` | `09bf43257dc53a1252fc78833636e8e5b68d1ec714106b5ce234e8ed7bbbb23b` | 三条滑杆 |
| `theme.svg` | `c612921e037c870d631327a715f67a519476372867e120933ac02bc4f41e79be` | **实心圆点 + 右半环**（契约规定） |
| `help.svg` | `61ef0b20741771025c951778c5284105dacc7f7b86bd567a81b18dda24999ecc` | 圆圈 + 问号 |

> 完整 sha256 以 `logs/svg-set.txt`（逐文件结构断言）与上表为准。

设计说明：
- 全部为手绘几何（直线/矩形/圆/弧），未复制任何第三方图标集的几何，避免许可问题。
- 线宽 1.75、圆头圆角端点；`theme.svg` 的圆点用 `fill="currentColor" stroke="none"`，
  所以该文件里 `currentColor` 出现 **2 次**（其余 14 个各 1 次）——`themed_icons` 的字符串
  替换必须替换**全部**出现，否则主题按钮的圆点会保持黑色（已当面提醒 T2）。
- 16px 栅格化目检可辨认（见 §4.6）；`build` 用立方体而非锤子，是与现有 settings/terminal
  在 16px 下更易区分的取舍。

---

## 2. 改动文件（diff stat）

```
 packaging/runtime/assemble-appdir.sh     | 44 +++++++++++++++++++++++++++---
 packaging/runtime/assets/vedit.desktop   |  1 +
 packaging/runtime/verify-f2-isolation.sh | 44 +++++++++++++++++++++++++++++++-
 src/app/main.cpp                         |  6 +++++
 4 files changed, 89 insertions(+), 3 deletions(-)
新增：scripts/build-app-icons.py、resources/icons/**（29 个产物）、evidence/ui-mosaic/icon-assets/**
```

`git status` 里另有若干 `evidence/**` 日志显示为 modified —— **不是本车道改的**（另一个成员正在跑
Qt 构建，`target/qt6-build/compile_commands.json` 时间戳同分钟）。本车道未执行任何 git add/commit，
二进制产物留待船长统一提交。

---

## 3. 生成算法与一个必须记录的关键决策

`scripts/build-app-icons.py`：

1. **轮廓（silhouette）**：对**深色变体**（`1.png`，深底与 #ECEFF3 对比度最大）做四角泛洪：
   每像素取 `max(|R-236|,|G-239|,|B-243|)`，≤ 12 视为页面灰；`ImageDraw.floodfill` 从四个角
   标记与之 4-连通的区域；被标记者 alpha=0。对 `3.png` 独立重复，结果与深色**逐字节一致**
   （脚本断言 `dark/mono` 轮廓相同），因此三个变体共享同一轮廓。
2. **裁切**：按不透明 bbox 裁切 → 补 2% 边距 → 居中放到正方形画布（hicolor 必须是正方形，
   否则 NxN 缩放会拉伸）。
3. **输出**：`app/vedit-*.png` 由各变体自己的像素 + 共享轮廓，2246² 直接 Lanczos 到 512；
   hicolor 全部由 **light** 变体的 2246² 主图直接 Lanczos 到各尺寸（不级联缩放）。
4. **断言**：16/512 存在且尺寸正确；打印每个产物路径 + 尺寸 + sha256；顺带打印 16/256/512 与
   `app/vedit-light.png` 的角/中心 alpha。

**关键决策（与契约字面算法的偏差，必须让复核者知道）**：契约 §3.1 写的是"从四角泛洪、容差≈12"。
对浅色变体（`2.png`）**不能**独立套用：它的圆角方形内侧是 `#e5ebf3`→`#f6f8fb` 的浅灰渐变，
与页面灰 `#ECEFF3` 的最大通道差只有 4–8（<12），泛洪会穿过轮廓把图案吃掉。实测：

```
dark  independent fill: opaque=0.9582 bbox=(0, 0, 2160, 2160)
light independent fill: opaque=0.2026 bbox=(204, 204, 1956, 1936)   <- 只剩 20% 不透明
mono  independent fill: opaque=0.9582 bbox=(0, 0, 2160, 2160)
light: its own fill leaked into the artwork (0.2026 vs 0.9582 opaque) — using the shared dark silhouette
```

所以脚本**仍对三个变体各跑一次四角泛洪作诊断**，但只在泄漏检测通过时采用浅色变体自己的结果；
本数据下自动退回共享轮廓（三个渲染本来就是同一几何）。浅色变体的像素（含接近页面灰的内侧）
原样保留，四角干净透明。这是"保留图案"与"容差 12"不可兼得时的唯一正确解。

---

## 4. 命令与观测输出

### 4.1 生成 + 自检（`logs/build-app-icons-run1.log` / `-run2.log`）

```
$ python3 scripts/build-app-icons.py
== source copies (byte-identical originals) ==
  resources/icons/src/vedit-dark.png  sha256=fd53e0...  (from 1.png)
  ...
== silhouette (four-corner flood fill, tolerance 12) ==
  dark  independent fill: opaque=0.9582 bbox=(0, 0, 2160, 2160)
  light independent fill: opaque=0.2026 bbox=(204, 204, 1956, 1936)
  ...
== app masters (512x512, :/icons/app/) ==
== hicolor theme (light variant, Lanczos) ==
assert: hicolor 16x16 and 512x512 exist with the expected dimensions
== alpha criterion (corner 0 / centre 255) ==
verify hicolor 16x16: 16x16 corner_alpha=[0, 0, 0, 0] centre_alpha=255 -> ok
verify hicolor 256x256: 256x256 corner_alpha=[0, 0, 0, 0] centre_alpha=255 -> ok
verify hicolor 512x512: 512x512 corner_alpha=[0, 0, 0, 0] centre_alpha=255 -> ok
verify app light: 512x512 corner_alpha=[0, 0, 0, 0] centre_alpha=255 -> ok
== artifacts ==
  ... 14 行 path/dimensions/sha256 ...
OK: 14 artifacts written under resources/icons/
$ echo $?
0
```

耗时（`logs/build-app-icons-timing.txt`）：`real 0m4.121s / user 0m4.008s / sys 0m0.095s`。

### 4.2 幂等（`logs/idempotency.txt`）

```
$ python3 scripts/build-app-icons.py   # 连续两次
run1 manifest (taken after build-app-icons-run1.log): 14 PNG artifacts
run2 (the third invocation) now re-executes the script and re-hashes:
artifacts: 14
idempotent (run1 sha256 == run2 sha256): True
```

两次运行的完整输出 `diff` 也逐字相同（无时间戳、无随机 PNG 块）。

### 4.3 像素判据（`logs/pixel-checks.txt`，独立于构建脚本的第二次测量）

```
Pillow 12.1.1
path                                                                size          corners  centre
resources/icons/hicolor/16x16/apps/io.github.vedit.VEdit.png   16x16          [0, 0, 0, 0]     255  OK
resources/icons/hicolor/24x24/apps/io.github.vedit.VEdit.png   24x24          [0, 0, 0, 0]     255  OK
resources/icons/hicolor/32x32/apps/io.github.vedit.VEdit.png   32x32          [0, 0, 0, 0]     255  OK
resources/icons/hicolor/48x48/apps/io.github.vedit.VEdit.png   48x48          [0, 0, 0, 0]     255  OK
resources/icons/hicolor/64x64/apps/io.github.vedit.VEdit.png   64x64          [0, 0, 0, 0]     255  OK
resources/icons/hicolor/128x128/apps/io.github.vedit.VEdit.png 128x128         [0, 0, 0, 0]     255  OK
resources/icons/hicolor/256x256/apps/io.github.vedit.VEdit.png 256x256         [0, 0, 0, 0]     255  OK
resources/icons/hicolor/512x512/apps/io.github.vedit.VEdit.png 512x512         [0, 0, 0, 0]     255  OK
resources/icons/app/vedit-light.png                            512x512         [0, 0, 0, 0]     255  OK
resources/icons/app/vedit-dark.png                             512x512         [0, 0, 0, 0]     255  OK
resources/icons/app/vedit-mono.png                              512x512         [0, 0, 0, 0]     255  OK
```

判据：四角 alpha 全 0、中心 alpha 255、尺寸等于目录名 —— 11/11 `OK`。还额外核对了每张图的
alpha bbox 与不透明像素占比（512 主图 `(7,7,505,505)` / 89.75%，即 2% 边距生效；16px 因 Lanczos
重采样达到 98.44%，四角仍为 0）。

### 4.4 脚本语法（`logs/bash-n.txt`）

```
$ bash -n packaging/runtime/assemble-appdir.sh       -> exit=0
$ bash -n packaging/runtime/verify-f2-isolation.sh   -> exit=0
$ bash -n evidence/ui-mosaic/icon-assets/icon-chain-sim.sh -> exit=0
```

`verify-f2-isolation.sh -h` 仍打印完整头部注释（帮助行号已随新增说明同步）。

### 4.5 图标链行为（`logs/icon-chain-sim.log`，`icon-chain-sim.sh` 复跑可重现）

`assemble-appdir.sh` 需要完整 AppDir 载荷（Qt/KF6 库、vd-app、工具链）才能在宿主跑完整流程，
所以证据用**抽取真实代码块**的方式取得，而不是复写逻辑：脚本用 awk 按小节标记从
`assemble-appdir.sh` 抽出 §6 图标安装块（从 `install -m 0644 .../vedit.desktop` 到 `log "icons:`）
和 7b 不变量块（从 `# UI-Mosaic A4: an installed launcher` 到 `log "icon invariants:`），在
`.mpd/tmp/icon-chain-sim/AppDir` 上 eval 执行。

```
PASS  install block exited 0
LOG: icons: AppDir root + .DirIcon + hicolor 16..512 from resources/icons/hicolor (256px light)
installed tree:
./.DirIcon
./io.github.vedit.VEdit.png
./usr/share/icons/hicolor/{16,24,32,48,64,128,256,512}x{}/apps/io.github.vedit.VEdit.png
./vedit.desktop
PASS  7b icon invariants exited 0
PASS  full tree is accepted
== negative controls: every icon-chain hole must fail the build ==
PASS  missing .DirIcon                (invariants refused the tree)
PASS  missing AppDir root PNG         (invariants refused the tree)
PASS  missing vedit.desktop           (invariants refused the tree)
PASS  missing hicolor 16x16           (invariants refused the tree)
PASS  missing hicolor 256x256         (invariants refused the tree)
PASS  empty AppDir root PNG           (invariants refused the tree)
PASS  root icon is not a PNG          (invariants refused the tree)
PASS  desktop lacks StartupWMClass    (invariants refused the tree)
PASS  desktop lacks Icon=             (invariants refused the tree)
== verify-f2-isolation.sh check_icon_chain (real function) ==
CHECK PASS  sim: vedit.desktop carries Icon=io.github.vedit.VEdit
CHECK PASS  sim: vedit.desktop carries StartupWMClass=io.github.vedit.VEdit
CHECK PASS  sim: icon artifact present: io.github.vedit.VEdit.png (34447 bytes)
CHECK PASS  sim: icon artifact present: .DirIcon (34447 bytes)
CHECK PASS  sim: icon artifact present: usr/share/icons/hicolor/16x16/... (629 bytes)
CHECK PASS  sim: icon artifact present: usr/share/icons/hicolor/64x64/... (5073 bytes)
CHECK PASS  sim: icon artifact present: usr/share/icons/hicolor/256x256/... (34447 bytes)
CHECK PASS  sim: icon artifact present: usr/share/icons/hicolor/512x512/... (122932 bytes)
CHECK PASS  sim: io.github.vedit.VEdit.png is a PNG (magic ok)
CHECK PASS  sim: .DirIcon is a PNG (magic ok)
sim: pass=10 fail=0
...
CHECK FAIL  sim-negative: icon artifact missing or empty: usr/share/icons/hicolor/64x64/apps/io.github.vedit.VEdit.png
PASS  check_icon_chain reports the missing 64x64 icon
== summary: fails=0 ==
ICON-CHAIN SIM PASS
$ echo $?   -> 0
```

### 4.6 SVG 集合（`logs/svg-set.txt`）

```
files: 15
exact-name match: True
names: build.svg find.svg help.svg new.svg open.svg redo.svg replace.svg save-as.svg save.svg settings.svg sidebar.svg terminal.svg theme.svg undo.svg vcs.svg
  new       xml=parse-ok root_attrs=True currentColor_occurrences=1
  ...（14 个为 1）
  theme     xml=parse-ok root_attrs=True currentColor_occurrences=2
  help      xml=parse-ok root_attrs=True currentColor_occurrences=1
```

形状目检：把 `currentColor` 替换成 `#2A2A28` 后用 librsvg（gdk-pixbuf SVG loader）栅格化 48px 与
16px 网格逐一查看：15 个全部渲染出预期形状、无空图、无越界；`vcs` 的弧线端点原先落在节点圆内，
已改为与圆相切（`M17.5 8.5a9 9 0 0 1-8.5 9.5`）后复检。

---

## 5. 打包改动

### 5.1 `packaging/runtime/assets/vedit.desktop`

只加一行（`Icon=` 保持不变）：

```
 Icon=io.github.vedit.VEdit
+StartupWMClass=io.github.vedit.VEdit
```

### 5.2 `packaging/runtime/assemble-appdir.sh`

- §6：删掉"从 `assets/io.github.vedit.VEdit.png` 装根图标/.DirIcon"的旧两行；改为
  `resources/icons/hicolor/256x256/apps/io.github.vedit.VEdit.png` → `$OUT/io.github.vedit.VEdit.png`
  与 `$OUT/.DirIcon`；再循环安装 `usr/share/icons/hicolor/{16,24,32,48,64,128,256,512}x{}/apps/`
  同名 PNG。图标源缺失 → 直接 `ERROR ... exit 1`（不回退旧占位图）。新增一条 `log "icons: ..."`。
- 7b：新增图标链不变量块 —— `vedit.desktop` / 根 PNG / `.DirIcon` / hicolor 16、64、256、512
  **存在且非空**（`-s`），根 PNG 的 8 字节魔数必须是 `89504e470d0a1a0a`，`.desktop` 必须同时含
  `Icon=io.github.vedit.VEdit` 与 `StartupWMClass=io.github.vedit.VEdit`；任一不满足 → `exit 1`。
  原 `log "delivery invariants: ..."` 保留（其它证据日志引用过这一行）。

### 5.3 `packaging/runtime/verify-f2-isolation.sh`

新增 `check_icon_chain <label> <root>`：断言 `vedit.desktop`（含 Icon/StartupWMClass）、根 PNG、
`.DirIcon`、hicolor 16/64/256/512 存在且非空，并校验两张根图是 PNG 魔数；计数进既有 `pass/fail`
与 `pass=N fail=N` 汇总。调用点：`appdir`（AppDir）与 `appimage`（解包后的 squashfs-root）各一次。
**没有新增 ctest id**，只加强既有脚本。头部注释与 `-h` 行号同步更新。

---

## 6. `src/app/main.cpp`（最小改动）

```diff
 #include <QFileInfo>
+#include <QIcon>
 #include <QJsonDocument>
@@
     QApplication app(argc, argv);
+    // UI-Mosaic A4：桌面条目、窗口图标与 WM_CLASS 必须指向同一个 id，否则任务栏/
+    // 窗口切换器会退回通用图标，且 .desktop 条目无法把自己的窗口归组。深浅切换由
+    // ThemeManager::applyUiTheme 覆盖，这里只是启动瞬间的兜底图标。
+    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.vedit.VEdit"));
+    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app/vedit-light.png")));
     QCoreApplication::setApplicationName(QStringLiteral("vedit-app"));
```

- 位置：`QApplication` 构造之后、`ApplicationShell` 构造之前，符合契约 §3.2 的时序要求。
- **与契约 §3.2 字面的偏差**：契约写 `QApplication::setWindowIcon(themed_icons::appIcon(false))`；
  船长本轮明确要求主程序只做"setDesktopFileName + setWindowIcon(QIcon(...))"两行，避免 main.cpp
  在 T2 落地 `themed_icons.{h,cpp}` 之前编译失败。深层切换由 `ThemeManager::applyUiTheme` 负责
  （契约 §1.5 的固定副作用顺序）。若复核要求回到 `themed_icons::appIcon(false)`，属一行替换，
  但必须先落 T2 的头文件。
- 本机**没有编译过**（原因见 §8.3），只有 diff 与语法审查。

---

## 7. 与 T2（theme-kernel）的接口（请复核者转达）

1. `resources/app_icon.qrc` 当前**尚未**被 `src/app/CMakeLists.txt` 登记（我只读检查：该文件里只有
   `resources/help/user_manual.qrc`，且 `CMAKE_AUTORCC ON`）。在 T2 把 `app_icon.qrc` 加进目标之前，
   `:/icons/app/vedit-light.png` 解析不到 —— `QIcon` 为空图标，不会崩溃，只是窗口图标为空。
2. 若 `app_icon.qrc` 被加进**静态库**（`VEDIT_APP_SHELL_SOURCES` 所在的 shell 库），则需要在
   `main.cpp` 里按 `help_viewer.cpp` 的模式调用 `Q_INIT_RESOURCE(app_icon)` 才能注册资源；
   若 qrc 直接作为可执行目标的源文件则不需要。**这个判断属 T2/船长**（我被要求不改 CMake/qrc、
   且 main.cpp 只做两行）。若需要，加一行即可。
3. `theme.svg` 的 `currentColor` 出现 2 次（圆点 fill + 半环 stroke），着色替换要全量替换。
4. 契约 §1.7 单元测试第 6 条要求 `appIcon(true)/appIcon(false)` 非空且 `pixmap(64,64)` 有效 ——
   依赖上述 qrc 登记，测试失败时先看第 1 点。

---

## 8. 未能证明 / 未执行（如实标注）

1. **没有运行完整的 `bash packaging/runtime/assemble-appdir.sh`**。它需要完整 AppDir 载荷
   （Qt6/KF6 运行时、`bin/vd-app`、verible/slang、git/svn 工具链等），只能在固定构建容器里跑；
   本轮只对**真实代码块**做了正例 + 9 个负例的行为测试（§4.5），没有验证整脚本端到端成功。
2. **没有运行完整的 `bash packaging/runtime/verify-f2-isolation.sh`**。它需要现成 AppDir 与
   `docker.m.daocloud.io/library/centos@sha256:be65f488...` 探针容器；我只对新增的
   `check_icon_chain` 函数做了真实函数级测试（正例 10 项全过、负例 1 项报错）。
3. **没有编译/链接 `src/app/main.cpp`**。宿主机没有 Qt6/KF6 开发头文件（`/usr/include` 下无完整
   Qt6Widgets/KF6），且船长明确禁止跑 `bash scripts/build-qt.sh`（构建目录归另一成员）。因此
   `setDesktopFileName` / `QIcon` 的编译期正确性**未由我验证**，须由 Qt 构建车道覆盖。
   风险评估：两个 API 都是 Qt6 稳定公开 API，`QApplication` 公有继承 `QGuiApplication`，静态成员
   可直接限定调用；`<QIcon>` 已显式包含。
4. **没有用 Qt 的 `QSvgRenderer` 做渲染校验**（宿主的 PyQt6 缺 `QtSvg` 模块，产物只有
   `libQt6Svg.so` 无头文件）；形状目检用的是 librsvg/gdk-pixbuf。`themed_icons` 的单元测试
   （契约 §1.7 第 6 条）才是权威的 Qt 渲染证据。
5. **没有在真实桌面环境观察任务栏/启动器图标**（无 X11/Wayland 会话可用）；窗口图标、WM_CLASS
   与桌面分组的最终观感只能由运行产物验证。

## 9. 已知观察（不是本车道缺陷，供船长决策）

1. **16–48px 的 light 变体在白底上对比度低**：浅色图案本身就是"浅灰圆角方形 + 白卡片"，
   与小尺寸下的白底几乎同色（16px 目检最明显）。契约 §3.1 规定 hicolor 用 light 变体，我按契约
   执行；若希望小尺寸改用 dark 变体，需要改契约（一行 size→变体映射），不应由我擅自改。
2. **旧占位图仍在用**：`packaging/runtime/assets/io.github.vedit.VEdit.png` 已不被 Qt6 的
   `assemble-appdir.sh` 引用，但**旧 GTK3 打包线** `packaging/appimage/build-appimage.sh:248`
   仍在用它（`Exec=vd-app` 的 v1 产品，且不安装 hicolor）。该文件不在契约 §3 的写入范围，
   我未改；若 A4 要覆盖 v1 打包线，需要船长另派或扩范围。
3. `git status` 中 `evidence/ffi-abi/**`、`evidence/m0-build/**` 等日志的 modified 状态来自
   另一成员正在进行的构建，与本车道无关。
