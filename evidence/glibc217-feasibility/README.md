# glibc 2.17 可行性尖兵（T76）：Qt 6.10.2 + KF6 6.24 + tree-sitter 能否交付到 CentOS 7？

- cell: `cell-20261031T085543161Z` · board task **T76** · 性质：**可行性评估 + 最小实证**（不产出产品代码，不 commit）
- 采集时间：2026-10-31 22:06–23:05 (Asia/Shanghai)
- `git rev-parse HEAD`：`c71fe31478887e3dc7c460f875ccfb93e61c0ec4`（分支 `feat/qt6-ktexteditor-migration`）
- `git status --porcelain | wc -l`：28 行（其他 lane 的未提交改动；本任务只新增 `evidence/glibc217-feasibility/` 与 `build/glibc217-spike/`）
- 环境：宿主 Linux **7.0.0-38-generic**；所有容器**共用宿主内核**（`uname -r` 在容器内同样是 7.0.0-38-generic）

---

## 0. 结论先行

**有条件能（原生 CentOS 7 构建路线）。**

在 **CentOS 7.9.2009（glibc 2.17）容器内**、用 **devtoolset-11（gcc 11.2.1）+ cmake 3.31 + Qt 6.10.2 源码**，
实测产出：

| 产物 | 最高 GLIBC 符号版本 | 需要 >2.17 的符号引用数 |
|---|---|---|
| `libQt6Core.so.6.10.2`（本机原生构建） | **GLIBC_2.16** | 0 |
| `libQt6Gui.so.6.10.2`（同上） | **GLIBC_2.14** | 0 |
| `libQt6Widgets.so.6.10.2`（同上） | **GLIBC_2.14** | 0 |
| Qt 平台插件 `libqoffscreen.so` | **GLIBC_2.14** | 0 |
| 最小 Qt6 **Widgets** 程序（链接上述库） | **GLIBC_2.2.5** | 0 |
| KF6 6.24.0 `libKF6SyntaxHighlighting.so.6.24.0` | **GLIBC_2.14** | 0 |
| 对照：现交付 `vedit-app`（Ubuntu 26.04 构建，glibc 2.43） | **GLIBC_2.39** | 30 |
| 对照：Ubuntu 的 `libQt6Core.so.6` | **GLIBC_2.35** | 35 |

并且**在干净的 stock `centos:7` 容器（glibc 2.17）里，原生构建的 Qt6 Widgets 程序直接跑通**：
`qt6=6.10.2 platform=offscreen widgets=200`，`CLEANROOM_RUN_EXIT=0`，**不需要打包 glibc、不需要自带 loader**，
只需 CentOS 7 自带的 `libpng`（yum 一条命令）。

**条件（都是实测撞出来的，不是推测）**：
1. 构建宿主必须是 glibc ≤ 2.17 的 CentOS 7；编译器用 **devtoolset-11**（CentOS 7 SCL，tuna vault 可用）；
   `gcc 4.8.5` 不支持 C++17，**不可用**。
2. CMake ≥ 3.22：用官方 `cmake-3.31.0-linux-x86_64` 二进制（实测其自身 `max GLIBC = 2.17`，可在 CentOS 7 跑）。
3. Qt6 configure 必须 **`-qt-freetype -qt-harfbuzz`（用自带）**：CentOS 7 的 FreeType 2.8 缺 `FT_Done_MM_Var`，
   系统 FreeType 会让 `src/gui/text/freetype/qfontengine_ft.cpp` 直接编译失败。
4. KF6 需要 **Qt6LinguistTools**（qttools 模块），qtbase 不含；需一并构建。
5. KSyntaxHighlighting 的数据生成脚本要求 **UTF-8 locale**（C locale 下 `UnicodeEncodeError: '\u03bc'`）。
6. **KCoreAddons（KF6 基座）要求 Python ≥ 3.9**，CentOS 7 自带 3.6.8 → 需旁装 Python 3.9+ 才能继续 KF6 链。
7. 真 GUI 还要 **xcb 平台插件**：本尖兵用 `QT_QPA_PLATFORM=offscreen` 验证；`libqxcb.so` 默认没构建（需 `-xcb` + xcb 相关 devel）。
8. KIO/KParts（KTextEditor 的依赖）需要 **Qt6 DBus**（本尖兵为压缩构建面用 `-no-dbus`，故未验证）。

**未证（见 §5，最重要的一条）**：**真 CentOS 7 / 内核 3.10 真机没验过**。所有容器测试共用宿主内核 **7.0.0**，
容器里的 "CentOS 7" 只换了用户态（glibc 2.17）。这条对两条路线都是同一块未证风险。

---

## 1. 问题一：能不能？（实测回答）

### 1.1 现交付物为什么"不能"直接跑

`evidence/m0-build/build-logs/34-qt6-elf.txt` 记录 `vedit-m0-smoke` 最高 `GLIBC_2.34`；
本次实测（`readelf --dyn-syms`，`logs/` 之外的可复跑命令见 §6）：

```text
target/qt6-build/src/app/vedit-app        max GLIBC_2.39   需要>2.17 的符号引用 30 个
target/qt6-build/cmake/smoke/vedit-m0-smoke max GLIBC_2.34   需要>2.17 的符号引用 1 个
/usr/lib/x86_64-linux-gnu/libQt6Core.so.6 (Ubuntu)  max GLIBC_2.35  需要>2.17 的符号引用 35 个
```

结论：**不是"我们的代码用了新 glibc"，而是我们链接的 Qt6 共享库本身就在 glibc 2.43 上构建**。
任何"打包 KF6/Qt6 的 .so"的做法在 glibc 2.17 上都会 `version 'GLIBC_2.39' not found`
（与 Lead 在 ubuntu:24.04 上看到的是同一机制）。

### 1.2 原生构建路线的实测（本尖兵的核心实证）

全部在容器 `t76-spike` 内完成（CentOS 7.9.2009 / glibc 2.17 / `gcc 4.8.5` 系统、SCC devtoolset-11）：

```text
# 工具链基线（logs/02-toolchain-baseline.log）
devtoolset-11 g++ 11.2.1 编译 C++17（threads + filesystem）
  → max GLIBC = 2.14；-static-libstdc++ -static-libgcc 后 NEEDED 只有 libm/libpthread/libc/ld
  （对照：gcc 4.8.5 不支持 -std=c++17 → Qt6 无法在系统 gcc 上构建）

# Qt 6.10.2 源码构建（logs/04、05、08、09）
./configure -release -no-dbus -no-icu -no-glib -no-openssl -no-opengl -no-pch \
            -qt-freetype -qt-harfbuzz -nomake examples -nomake tests
  configure: 17 s（Gui+Widgets 全开）
  Core-only 构建: 60 s（16 核）
  Core+Gui+Widgets+plugins 全量构建: 4m23s（16 核，user 57m）
  构建树 249 MB；install 到 /opt 88 MB

# 最小 Qt6 程序（logs/06、10；源码 spike/qt6_hello.cpp、spike/qt6_widgets.cpp）
qt6_hello   (QtCore)   : max GLIBC = 2.2.5，RUN_EXIT=0
qt6_widgets (QtWidgets): max GLIBC = 2.2.5，RUN_EXIT=0
  qt6=6.10.2 platform=offscreen widgets=200

# 干净 stock centos:7 容器（glibc 2.17）里跑原生构建的 Qt6 Widgets 程序
#   仅 yum install -y libpng（CentOS 7 自带包），不带 glibc、不带 loader、不设 LD_LIBRARY_PATH 技巧以外的任何东西
  qt6=6.10.2 platform=offscreen widgets=200
  CLEANROOM_RUN_EXIT=0
  ldd (GNU libc) 2.17
```

### 1.3 KF6 6.24 的实证（走通到第一个真实框架）

```text
# logs/12、13、14
ECM 6.24.0            : configure+build+install OK（0.8 s）
qttools（LinguistTools）: 构建+安装 OK；KSyntaxHighlighting 的 ECM 模块强依赖它
KSyntaxHighlighting 6.24.0:
    configure 3.6 s / build 4.1 s / **BUILD_EXIT=0**
    libKF6SyntaxHighlighting.so.6.24.0
      max GLIBC = GLIBC_2.14       需要 >2.17 的引用 = 0
      max GLIBCXX = GLIBCXX_3.4
      NEEDED: libQt6Gui/Qt6Network/Qt6Core + libstdc++/libm/libgcc_s/libpthread/libc
KCoreAddons 6.24.0    : **configure 失败** —— 要求 Python3 ≥ 3.9（CentOS 7 是 3.6.8）
```

即：**KF6 的工具链与代码在本环境可编译，且产物天然 ≤ 2.17**；卡点是构建宿主的辅助工具版本
（Python ≥ 3.9、Qt6LinguistTools、UTF-8 locale），不是 glibc 本身。

### 1.4 四个替代路线的结论（每条给"为什么"）

| 路线 | 结论 | 依据 |
|---|---|---|
| **A. 原生 CentOS 7 构建**（推荐） | **有条件能** | §1.2/§1.3 实测：Qt6 Core/Gui/Widgets + KF6(KSyntaxHighlighting) 产物 max GLIBC ≤ 2.16；干净 stock centos:7 跑通 |
| **B. 打包 glibc + 自带 loader**（Lead 已实测的路线） | **容器内能，真机未证** | Lead `evidence/runtime-deps/REPORT.md`：CentOS 7.9 容器（内核 7.0.0）EXIT=0；但 glibc 2.43 的 loader/libc 对内核有更低要求，**kernel 3.10 未验**。属于上游不背书的脆弱做法（发行版/内核变化会炸） |
| C. 静态链接 / musl | **不推荐，未实测** | Qt6 有 `-static` 支持，但 Widgets 字体/图形链路与 musl 组合上游不支持；本次未构建验证 → 属未证 |
| D. `patchelf` / 符号版本 shim | **不可行** | `vedit-app` 有 **30** 个符号引用需要 >2.17（`libQt6Core` 35 个）。glibc 2.34 把 libpthread 并入 libc（`pthread_*@GLIBC_2.34`），不是"改个版本号"能补的 |
| E. 新发行版构建 + 低 glibc 交叉工具链 | **等价于 A，无额外收益** | 交叉工具链的"低 glibc"就是 A 的 sysroot；A 已直接可用（CentOS 7 自身就是 glibc 2.17 宿主），且 A 的产物已被干净 CentOS 7 验证 |

---

## 2. 问题二：最小实证（硬要求，已完成）

见 §1.2 的 `qt6_widgets`：**CentOS 7 环境 + 选定工具链 → 编译并链接最小 Qt6 Widgets 程序 → 运行 → `readelf` 实测
`max GLIBC = 2.2.5`**，并在**干净 stock centos:7** 容器里跑通（只装了 `libpng`）。
源码：`evidence/glibc217-feasibility/spike/qt6_hello.cpp`、`spike/qt6_widgets.cpp`。
原始日志：`logs/02, 04, 05, 06, 08, 09, 10, 11, 13, 14`；干净 stock centos:7 实跑 = `logs/15-cleanroom-centos7.log`；
现交付物的 glibc 需求 = `logs/16-current-qt6-glibc-floor.log`。

---

## 3. 问题三：代价

### 3.1 原生 CentOS 7 构建路线

| 项 | 实测/估计 | 来源 |
|---|---|---|
| qtbase 6.10.2（Core+Gui+Widgets+plugins）构建 | **4m23s**（16 核）；Core-only 60 s | `logs/09`、`logs/05` |
| qttools（LinguistTools） | ~1.5 min | `logs/13` |
| KF6 ECM + KSyntaxHighlighting | < 10 s | `logs/12`、`logs/14` |
| KF6 其余链（KConfig/KI18n/KWidgetsAddons/KXmlGui/KIO/KParts/KTextEditor…） | **未测**；KDE Frameworks 6.24 一次发布 **72 个框架**（实测目录计数），KTextEditor 子树约 15–20 个；按 Qt 的构建密度估 **15–40 min**（16 核） | 目录实测 72；时间未测 |
| 交付镜像增量 | Qt install **88 MB** + KF6 库（未测）+ 镜像内构建工具链；建议 ≥ 2 GB | `logs/11` |
| 手工干预 | **6 处**：devtoolset-11（SCL vault）、cmake 3.31、`-qt-freetype -qt-harfbuzz`、qttools、UTF-8 locale、Python ≥3.9（旁装） | §0 |
| 与现有 `vedit-qt6-build:local` 的关系 | **建议双镜像**：现代镜像继续做开发/门禁（当前 16/16 门禁依赖它），新增 `vedit-centos7-delivery` 产出交付物；两套产物需各自的 ELF 断言（≤2.17） | — |
| 主要风险 | KF6 链的隐藏依赖（Python 版本、Qt 模块缺项、KIO 的 DBus/X11）；xcb 插件；字体/输入法行为在真机 3.10 未验 | — |

### 3.2 打包 glibc + loader 路线（Lead 的路线，作为备选）

- **能做**：容器内（glibc 2.17 用户态）已经跑通，`EXIT=0`，`kernel='KTextEditor 6.24.0'`（Lead 实测）。
- **脆弱点**：交付物里带的是构建基线的 glibc 2.43 的 `libc.so.6` + `ld-linux`；它要求的内核下限由 **Ubuntu 26.04 的
  glibc 构建选项**决定，**不是 3.10 时代的值** → 真 CentOS 7（3.10）**可能起不来**；此外 Qt 平台插件必须单独拷
  （`libqxcb.so`/`libqoffscreen.so`），`ldd` 看不见；升级任何 Qt/KF6 库都要重新核对整套 glibc 闭包。
- **未证**：内核 3.10（无真机）；非 glibc 系统（musl/Alpine）不可用是已知的。

---

## 4. 推荐

**首选：A 原生 CentOS 7 构建路线（有条件能）** —— 产物天然 ≤ 2.17，不需要打 glibc/loader，干净 CentOS 7 已验证；
代价是新增一套 CentOS 7 交付构建镜像与 KF6 链的调通（估 1–2 人日 + 15–40 min 构建时间）。
保留现有现代镜像做**开发/门禁**，交付物只从 CentOS 7 镜像产出（并在其内加一条 `max GLIBC ≤ 2.17` 的 ELF 断言）。

**备选：B 打包 glibc + loader** —— 只在"必须立刻出包、且接受真机风险"时用；需要把"内核 3.10 未验证"写进发布说明。

**不推荐**：C 静态/musl（未证且上游不支持）、D shim（30/35 个 >2.17 引用，且 2.34 合并了 libpthread）、E 交叉工具链（= A）。

---

## 5. 已证 / 未证（诚实边界）

**已证（本机实测，日志可复跑）**
1. CentOS 7 + devtoolset-11 能编译 C++17，产物 max GLIBC = 2.14（`logs/02`）。
2. Qt 6.10.2 qtbase（Core/Gui/Widgets + qoffscreen 插件）能在 CentOS 7 从源码构建；四个库 max GLIBC ≤ 2.16，
   >2.17 的引用 = 0（`logs/04,05,08,09,10`）。
3. 最小 Qt6 Widgets 程序（链接上述库）max GLIBC = 2.2.5，运行成功（`logs/10`）。
4. 该程序在 **干净 stock centos:7（glibc 2.17，仅 `yum install libpng`）** 里运行成功（`CLEANROOM_RUN_EXIT=0`）；
   **无 glibc 打包、无自带 loader**（`logs/15-cleanroom-centos7.log`）。
5. KF6 6.24：ECM + qttools(LinguistTools) + KSyntaxHighlighting 构建成功，KSyntaxHighlighting max GLIBC = 2.14。
6. 现交付 `vedit-app`/libQt6Core 的 glibc 需求（2.39/2.35）与 >2.17 引用数（30/35），解释"为什么现在跑不了 CentOS 7"。
7. 具体构建卡点：系统 gcc 4.8.5 无 C++17、FreeType 2.8 缺 `FT_Done_MM_Var`、KCoreAddons 要 Python ≥3.9、
   KSyntaxHighlighting 数据生成需 UTF-8 locale、KSyntaxHighlighting 需 Qt6LinguistTools。

**未证（不得当成"能"）**
1. **真 CentOS 7（内核 3.10）没验过** —— 所有容器共用宿主内核 7.0.0。对 A 路线风险较低（glibc 2.17 + CentOS 7 头文件
   天然只用到 3.10 已有的系统调用），对 B 路线风险最高（自带 glibc 2.43 的内核下限未知）。**这是最大未证风险**。
2. **真 GUI 窗口**没开过（只有 offscreen）；`libqxcb.so` 未构建（需要 `-xcb` + xcb devel 组合）。
3. **KTextEditor/KParts/KIO 未构建**：整条 KF6 链只走到 KSyntaxHighlighting（一个真实框架）+ ECM + qttools。
4. **tree-sitter 0.25 未在 CentOS 7 构建**（本次未做；它是 Rust/C 纯库，风险低但未证）。
5. 构建时长/镜像体积的 KF6 部分为**估计**（§3.1 已标注）。
6. 字体/CJK/输入法在真机上的行为未测。

---

## 6. 复跑

原始命令（全部在 `docker exec t76-spike` 内执行，容器由 `vedit-build:local` 启动）：

```sh
# 0) 容器
docker run -d --name t76-spike -v "$PWD":/vedit-build -w /vedit-build vedit-build:local sleep infinity

# 1) 工具链（SCL 走 tuna vault；devtoolset 包签名用 gpgcheck=0 只是为了尖兵速度，正式构建应导入 SCL 签名公钥）
#    仓库文件见 logs/01-devtoolset-install.log；随后：
scl enable devtoolset-11 -- g++ --version        # 11.2.1
/spike/cmake-3.31.0-linux-x86_64/bin/cmake --version  # 3.31.0（自身 max GLIBC 2.17）

# 2) Qt 6.10.2
curl -fsSLO https://download.qt.io/official_releases/qt/6.10/6.10.2/submodules/qtbase-everywhere-src-6.10.2.tar.xz
mkdir /spike/qt6-gui-build && cd /spike/qt6-gui-build
/spike/qtbase-everywhere-src-6.10.2/configure -release -no-dbus -no-icu -no-glib -no-openssl -no-opengl -no-pch \
    -qt-freetype -qt-harfbuzz -nomake examples -nomake tests -confirm-license -opensource
cmake --build . --parallel 16 && cmake --install .

# 3) 最小程序（源码在 evidence/glibc217-feasibility/spike/）
scl enable devtoolset-11 -- g++ -std=c++17 -O2 qt6_widgets.cpp -o qt6_widgets \
    -I/opt/qt6-gui/include -I/opt/qt6-gui/include/QtCore -I/opt/qt6-gui/include/QtGui \
    -I/opt/qt6-gui/include/QtWidgets -L/opt/qt6-gui/lib -lQt6Widgets -lQt6Gui -lQt6Core -Wl,-rpath,/opt/qt6-gui/lib
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORM_PLUGIN_PATH=/opt/qt6-gui/plugins/platforms ./qt6_widgets
readelf --dyn-syms --wide ./qt6_widgets | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1

# 4) KF6（ECM → qttools → KSyntaxHighlighting）
curl -fsSLO https://download.kde.org/stable/frameworks/6.24/extra-cmake-modules-6.24.0.tar.xz
curl -fsSLO https://download.kde.org/stable/frameworks/6.24/syntax-highlighting-6.24.0.tar.xz
# 见 logs/12、13、14 的逐条 cmake 命令行
```

`build/glibc217-spike/run-spike.sh` 是上述命令的**转录脚本**（由本次已执行的命令整理而成，
未再作为单一脚本整体重跑；时间盒内采用逐段执行 + 分段日志）。
