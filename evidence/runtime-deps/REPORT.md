# 运行期依赖清单与自包含可行性实测

generated: 2026-10-31T14:14:15Z
git_head: 19303ae3a58491767d97815d2fe1e0947816e5b6
artifact: `target/qt6-build/src/app/vedit-app`（71618608 bytes，sha256 `49c45d1b8fafd86c…`）
build_image: `vedit-qt6-build:local`，**glibc 2.43**（`ldd --version` 实测）

---

## 1. 权威运行期依赖清单

### 1.1 直接 NEEDED（`readelf -d`，11 条）
其中"非常规"的 6 条（其余是 libc/libm/libstdc++/libgcc_s/ld-linux）：

| soname | Debian/Ubuntu 包 | 镜像内路径 |
|---|---|---|
| `libKF6TextEditor.so.6` | `libkf6texteditor6` | `/usr/lib/x86_64-linux-gnu/` |
| `libKF6Parts.so.6` | `libkf6parts6` | 同上 |
| `libKF6SyntaxHighlighting.so.6` | `libkf6syntaxhighlighting6` | 同上 |
| `libQt6Widgets.so.6` | `libqt6widgets6` | 同上 |
| `libQt6Gui.so.6` / `libQt6Core.so.6` | `libqt6gui6` / `libqt6core6t64` | 同上 |
| `libtree-sitter.so.0.25` | `libtree-sitter0.25`（候选 0.25.9-7） | 同上 |

**另有 Qt 平台插件不是 ldd 能看见的**：`/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms/{libqxcb.so,libqoffscreen.so}` —— 只拷 .so 不拷插件，窗口起不来。

### 1.2 完整闭包（`ldd`）
- 总计 **146** 个共享库
- 其中 **32 个 KDxx**（KF6 系列）、**11 个 libQt6**
- 明细见 `02-ldd-closure.txt`

### 1.3 一条装齐的 apt 命令
```bash
sudo apt install -y libkf6texteditor6 libtree-sitter0.25
```
`libkf6texteditor6` 经依赖链自动拉入 parts / syntaxhighlighting / KIO / KConfig 等整套 KF6 运行库。
显式写全的等价命令：
```bash
sudo apt install -y libkf6texteditor6 libkf6parts6 libkf6syntaxhighlighting6 \
  libqt6widgets6 libqt6gui6 libqt6core6t64 libtree-sitter0.25
```
⚠️ 本机装了 KF6，所以 `apt-get -s` 干跑只列出 `libtree-sitter0.25` 待装 —— **干净机器上会多装那一批 KF6 包**。

### 1.4 备用方案（不装系统包）
见 `04-fallback-sos.txt`：从镜像里把闭包打成 tar，再 `LD_LIBRARY_PATH` + `QT_PLUGIN_PATH` 指向它。

---

## 2. 自包含实测（三档干净环境）

| 环境 | 打包内容 | 结果 |
|---|---|---|
| **ubuntu:26.04**（无任何 KF6/Qt6） | AppRun + usr/lib（**不含 glibc**） | ✅ **EXIT=0** |
| **ubuntu:24.04**（glibc 2.39，无 KF6/Qt6） | AppRun + usr/lib（不含 glibc） | ❌ **失败**：`libQt6Gui.so.6: version `GLIBC_2.43' not found` —— 不是缺 KF6，是**打包进来的库自身要求 glibc 2.43** |
| **CentOS 7.9 clean-room**（glibc **2.17**，无 KF6/Qt6） | usr/lib **含 glibc**，用自带 `ld-linux` 启动 | ✅ **EXIT=0**，`kernel='KTextEditor 6.24.0'`、filetree/search 均安装成功 |

### 结论
1. **打库是可行的**：同基线系统上自包含成立；把 glibc 一起打包并用自带 loader，**连 glibc 2.17 的 CentOS 7 都能起来**。
2. **只打 KF6/Qt6 不够**：构建基线是 glibc 2.43，凡是比它老的系统，必须连 glibc 一起打（否则就是 24.04 那一档的失败）。
3. **容器测不出内核**：三档共用宿主内核 7.0.0；真实 CentOS 7 是 3.10，**必须另验**。
4. **三档都是 offscreen 无头**：没有开过真窗口，真 GUI 需 `libqxcb.so` + 可用 DISPLAY。

---

## 3. 这是不是交付缺陷？——是

**证据 1：用户跑的路径根本没有打包链。**
`packaging/appimage/build-appimage.sh` 的 AppDir 里装的是 **旧 GTK3 的 `vd-app`**（`usrdir/usr/bin/vd-app`，并从 `` 拷 GtkSourceView / gdk-pixbuf loaders / GTK immodules），**没有一条路径装 `vedit-app`**。

**证据 2：契约缺口。**
T31 契约（`contracts/packaging-final.md`）写的是：
> `assemble-appdir.sh 把 vedit-app 装为 bin/vd-app` … `AppImage 内含 FFI 库或静态链接`（readelf/ldd 证据）… `glibc ≤2.17 不回归`

它要求了 **FFI 库**、要求了 **glibc ≤2.17**，但**从未要求把 Qt6/KF6/KTextEditor/KSyntaxHighlighting/tree-sitter 的运行库打进 AppDir**。这正是本次用户在干净机器上撞墙的根因 —— **契约缺口，不是实现疏忽**。

**建议补的契约条款**：
> AppImage/AppDir 必须自包含 `ldd vedit-app` 的全部非常规库（KF6/Qt6/tree-sitter）+ Qt 平台插件（`libqxcb.so`，至少一个可用后端），并在**未安装任何 KF6/Qt6 包的干净镜像**里实测启动（记录 exit code 与首屏日志）。若目标平台 glibc < 构建基线（当前 2.43），必须连 glibc 一并打包并以自带 loader 启动，或在目标等同基线上重建。

## 4. 给 T76 可行性尖兵的直接结论

- **可行**：已在本机证明"Ubuntu 26.04 构建 + 全量打库（含 glibc）+ 自带 loader"能在 **glibc 2.17 的 CentOS 7 容器**里把 VEdit 起起来（EXIT=0，KTextEditor 6.24.0 已加载）。
- **未证**：真实 CentOS 7 的 **3.10 内核**；以及**真窗口**（xcb + DISPLAY）。
- **建议 T76 第一件事**：在一台**真 CentOS 7 机器（非容器）**上跑一次同样的 bundle，重点看内核相关失败（glibc 2.43 对内核有最低版本要求）。这一条过不了，后面全部不用做。
