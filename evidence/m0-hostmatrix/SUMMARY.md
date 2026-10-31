# T12 — M0 双宿主闸门矩阵（A0a / A0b / A0d / A0e）

> **delivery 版结果见 [`delivery/SUMMARY-delivery.md`](delivery/SUMMARY-delivery.md)**（T67；同一脚本、`--appimage` 换为 delivery 产物，并新增宿主定义隔离断言）。本文件保持 T12 探针版原样。

- cell `cell-20261031T014507403Z` · task `m0-hostmatrix` / **T12** · owner: Deep Worker · verifier: Reviewer（loop `loop-20261031T023040-…`）
- 契约：`.mpd/cellwork/cell-20261031T014507403Z/contracts/m0-hostmatrix.md`
- 运行器：`tests/packaging/hostmatrix.sh`（+ `tests/packaging/sidecar.Dockerfile`，测试用 X 服务器）
- 日期：2026-10-31（Asia/Shanghai）

> ⚠️ **不要误读「可输入」**：本节矩阵证明的是「X 按键事件送达窗口 + 进程存活 + KTextEditor 文档 API 往返（`document=1`）」。
> M0 smoke 的窗口**没有编辑控件**（源码无 `setCentralWidget`），因此**「按键回显到编辑器」尚未证明**，要等 M1 的编辑器窗口后补同一条断言（详见 §5 U2）。
>
## 0. 判定摘要

| 闸门 | 环境 | 结果 | 关键判据 |
|---|---|---|---|
| **A0a** | CentOS 7.9.2009（glibc **2.17**），stock base（无 Qt/KF6） | **PASS** | `M0-SMOKE ok=1 platform=xcb ktexteditor=1 document=1 verilog=1 themes=30`；`libc=/app/lib/libc.so.6`；窗口 640×480 可见；输入事件送达后进程存活；exit 0 |
| **A0b** | Ubuntu 24.04.5 LTS（glibc **2.39**），stock base（无 Qt/KF6） | **PASS** | 同一份产物、同一组断言全绿；窗口可见、输入后存活、exit 0 |
| **A0d** | base-only CentOS 7（证明不借宿主 Qt/KF6） | **PASS** | 宿主 `libQt6*/libKF6*/libkf6*=0`、`katepart.so=0`、KSyntax 数据目录=0、`libX11.so=0`、`libfuse2=0`；应用仍 `ok=1`、`libc=/app/…` |
| **A0e** | CentOS 7 + 无 GPU/无 GL + 软件回退环境 | **PASS** | 容器内 `/dev/dri` 为空、`libGL.so=0`、无 `glxinfo`；`LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QSG_RHI_BACKEND=software`；窗口可见、输入后存活、exit 0 |

`summary.tsv`：`a0a-centos7 PASS / a0b-ubuntu2404 PASS / a0d-base-only PASS / a0e-no-gpu PASS`（`pass=4 fail=0`）。

## 1. 产物（诚实声明）

**被测产物**：`packaging/runtime/out/VEdit-0.1.1-x86_64.probe.AppImage`
sha256 `524f7b0780ff4ee093095e6ebae34b816b18c3aec94310dd8abe281c1e04bd28`，66,763,256 B。

- 这是 T11 的 **PROBE** 构建：库用的是发行版 KSyntaxHighlighting（内嵌 36 个严格 GPL 定义，`SystemVerilog=GPLv3+`，T56），**delivery 被 T60 阻塞**。
- ⇒ 本矩阵证明的是**运行时机制**在四个宿主形态上成立；**不是**交付物许可判定。T60/T16 落地后必须用 delivery 产物**重跑同一脚本**（脚本对产物无硬编码，`--appimage` 可换）。
- 四环境用的是**同一个文件**（sha256 相同），符合契约「同一份 AppImage」。

## 2. 方法

- 被测容器使用 **stock base 镜像**（pinned digest），只从外部获得：只读的 `/app`（AppImage 提取内容）+ `DISPLAY`。
- 显示服务由**测试 sidecar** 提供（`vedit-hostmatrix-xside:local` = Ubuntu 24.04 + Xvfb + xdotool + imagemagick，`tests/packaging/sidecar.Dockerfile`），经 **TCP** 暴露 `:99`；**不挂载 X socket、不装宿主库**。sidecar 是测试工具，不是产品构建镜像（`scripts/docker/*` 未改）。
- 测试 sidecar 镜像 id：`sha256:cd5426124d79438c3f7544887bb7ff5b0111f146d29f0d0a69b255c822e2d9af`（`tests/packaging/sidecar.Dockerfile`，base 为 Ubuntu 24.04 digest `fe815609…`）。
- 每个场景：先截空屏 → 启动 AppRun（`--selfcheck --hold 16000`，xcb）→ 5 秒后截图 + `xdotool search` 找窗 + `windowfocus`/`type`/`key` 注入 → 再截图 → 等待退出。
- 「窗口可见」用两帧像素差客观化：空屏 vs 有窗帧的 `compare -metric AE` = **307200 = 640×480**（窗口整面积），不是「看起来像有窗口」。

## 3. 逐环境命令与观测

### A0a — CentOS 7.9.2009（glibc 2.17）

```
tested_image: docker.m.daocloud.io/library/centos@sha256:be65f488…（CentOS Linux 7.9.2009）
tested_glibc: ldd (GNU libc) 2.17
host_qt_kf6_count: 0
$ docker run --rm --network vedit-hm-net -v <work>/root:/app:ro \
    -e DISPLAY=xside-a0a:99 -e QT_QPA_PLATFORM=xcb -e HOME=/tmp -e LANG=C.UTF-8 \
    <centos@digest> /app/AppRun --selfcheck --hold 16000
M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ksynhl=6.24.0 tree_sitter=0.25.9
  window=1 editor=1 document=1 definitions=451 themes=30 verilog=1
  systemverilog=loadable(not-asserted,C7-pending) custom_path=1 tree_sitter_parser=1
  kshlib=/app/lib/libKF6SyntaxHighlighting.so.6 libc=/app/lib/libc.so.6
M0-SMOKE theme=Atom One Dark
window=640x480 @ 0,0；screenshot_pixel_diff=307200；input=sent；alive_after_input=1；smoke_exit=0
```

### A0b — Ubuntu 24.04.5 LTS（glibc 2.39）

```
tested_image: docker.m.daocloud.io/library/ubuntu@sha256:fe815609…（Ubuntu 24.04.5 LTS）
tested_glibc: ldd (Ubuntu GLIBC 2.39-0ubuntu8.9) 2.39
host_qt_kf6_count: 0
同一份产物 → M0-SMOKE ok=1 platform=xcb … verilog=1 themes=30 libc=/app/lib/libc.so.6
window=640x480；pixel_diff=307200；input=sent；alive_after_input=1；smoke_exit=0
```

### A0d — base-only（不借宿主 Qt/KF6）

```
host_libQt6_or_libKF6=0
host_qmake_or_qtpaths=0
host_ksyntax_data=0
host_katepart=0
host_libX11=0
host_libfuse2=0
$ docker run --rm -v <work>/root:/app:ro -e QT_QPA_PLATFORM=offscreen \
    <centos@digest> /app/AppRun --selfcheck --expect-libc-prefix /app
M0-SMOKE ok=1 platform=offscreen … libc=/app/lib/libc.so.6      (offscreen_exit=0)
```

宿主连 `libX11`/`libfuse2` 都没有 ⇒ xcb 能起来这一事实本身证明 **X11 客户端库也来自包内**（A0a/A0e 已证）。

### A0e — 无 GPU / 软件回退

```
gpu_devices_in_container=0     # 无 /dev/dri
mesa_on_host=0                 # 宿主连 libGL 都没有（比「旧 Mesa」更严）
extra_env: LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QSG_RHI_BACKEND=software
M0-SMOKE ok=1 platform=xcb … window=1 editor=1 document=1
window=640x480；pixel_diff=307200；input=sent；alive_after_input=1；smoke_exit=0
```

容器里没有任何 GL 栈（无 `libGL`、无 `glxinfo`），在软件回退环境变量下窗口仍然绘制出来、按键事件送达后不崩。
**边界**：本 M0 smoke 的窗口是纯 raster（QWidget）窗口，A0e 证明的是「无 GL 也能起窗口」，不是「GL/HW 路径的回退渲染质量」；旧 Mesa 真机由后续 UI lane 按 A0e 补。

## 4. 证据文件

每环境一份：`evidence/m0-hostmatrix/<env>/{meta.txt, run.log, 00-empty.png, 10-window.png, 11-after-input.png}`；
A0d 另有 `host-purity.log`、`run-offscreen.log`；总表 `summary.tsv`。
`meta.txt` 记录：产物 sha256、镜像 digest、宿主 OS/glibc、宿主 Qt 计数、窗口 id/几何、输入结果、退出码、像素差。

## 5. 未闭合 / 边界（不是静默假设）

| # | 项 | 说明 |
|---|---|---|
| U1 | **探针产物**：T60（`QRC_SYNTAX=OFF` 库）与 T16（自有 Verilog/SV 定义）未交付 | 本矩阵只能证机制；delivery 后**必须重跑**（`bash tests/packaging/hostmatrix.sh --appimage <delivery>`） |
| U2 | **「可输入」的深度** | smoke 窗口没有编辑控件（源码无 `setCentralWidget`），所以只能证明：X 键事件送达窗口 + 进程存活 + KTextEditor 文档 API 往返（`document=1`）。**「按键回显到编辑器」需要 M1 的编辑器窗口**，届时在 A0e 里补同一条断言 |
| U3 | **真实 FUSE 挂载未测** | stock base 无 `libfuse2`；四环境都用 `--appimage-extract` 得到的内容（同一 sha256）。A7 的 `libfuse2` 运行前提需在真机文档中记录（T38/A7） |
| U4 | **旧 Mesa 真机** | 本矩阵的容器根本没有 GL（比旧 Mesa 更弱）；旧 Mesa/软件渲染的**画质**判定属 UI lane |
| U5 | **无 WM** | Xvfb 里没有窗口管理器，窗口无标题栏/装饰；`window=640x480` 与像素差已足以证明「窗口被绘制」，装饰行为不属 M0 |
| U6 | **测试 sidecar 构建方式** | 本环境 buildx 无法写 `$HOME`，用 `DOCKER_BUILDKIT=0`（legacy builder）构建测试镜像；该镜像是测试工具，不进交付物 |

## 5b. 交付产物就绪后的动作（排期）

T60（F2 形态的 KSyntaxHighlighting）与 T16（自有 Verilog/SV 定义）落地后，**用同一脚本重跑 delivery 版矩阵**：

```bash
bash tests/packaging/hostmatrix.sh --appimage packaging/runtime/out/VEdit-<ver>-x86_64.AppImage
```

判据不变；差异只在于产物从 probe 变为 delivery（库内无 GPL 定义、定义来自白名单）。
本矩阵的结论**不得**被引用为 delivery 判定，直到这次重跑完成。

## 6. 复跑

```bash
# 全矩阵（约 3 分钟；自动构建测试 sidecar、pinned 镜像）
bash tests/packaging/hostmatrix.sh
# 单环境
bash tests/packaging/hostmatrix.sh --only a0a
# delivery 产物就绪后（T60/T16）：同一条命令换产物
bash tests/packaging/hostmatrix.sh --appimage packaging/runtime/out/VEdit-<ver>-x86_64.AppImage
```

判据是脚本内的 grep（`M0-SMOKE ok=1`、`platform=xcb`、`ktexteditor=1`、`document=1`、`verilog=1`、`themes>=1`、`M0-SMOKE theme=`、`libc=/app/`、退出码、窗口存在、截图存在、输入后存活），任一不满足该环境判 FAIL 并退出非零。本轮四环境 `pass=4 fail=0`。
