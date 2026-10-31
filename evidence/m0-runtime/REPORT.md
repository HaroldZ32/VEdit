# T11 证据报告 — M0 私有运行时（AppImage 自带 loader/glibc/Qt6/KF6/KTextEditor/KSyntaxHighlighting/tree-sitter）

- cell `cell-20261031T014507403Z` · task `m0-runtime` / **T11** · owner: Deep Worker · verifier: Reviewer（loop `loop-20261031T023040-db7dd9`）
- 契约：`.mpd/cellwork/cell-20261031T014507403Z/contracts/m0-runtime.md` · 日期 2026-10-31（Asia/Shanghai）
- 关联：C2（私有运行时）、C6（KSyntaxHighlighting 私有数据路径）、C18（tree-sitter 随包）、C7/D2=B（语法定义许可）、A0c（libc 路径判据）
- 写域：`packaging/runtime/`、`evidence/m0-runtime/`（未改生产代码、未重建镜像、未跑多宿主矩阵）

---

## 0. 结论

1. **AppDir 组装、AppRun、私有 loader/glibc、C6 数据路径、A0c 自检全部打通**：
   `AppRun` 显式经包内 `ld-linux-x86-64.so.2 --library-path $APPDIR/lib` 启动；
   loader 解析表 **148 条全部落在包内**，`libc.so.6 => $APPDIR/lib/libc.so.6`。
2. **单文件 AppImage 已产出**（探针）：`VEdit-0.1.1-x86_64.probe.AppImage`，
   **66,763,256 B**，sha256 `524f7b0780ff4ee093095e6ebae34b816b18c3aec94310dd8abe281c1e04bd28`，
   在 **CentOS 7.9.2009（glibc 2.17）** 里经包内 loader 自检 `M0-SMOKE ok=1`、
   `libc=/app/lib/libc.so.6`。
3. **delivery 被 T60 阻塞（按设计）**：探针用的是发行版 `libKF6SyntaxHighlighting.so.6.24.0`，
   它内嵌 **36 个严格 GPL 定义**（`SystemVerilog=GPLv3+`，T56）。许可闸门输出
   `DELIVERY_BLOCKED=yes`，AppDir 被盖 `PROBE-NOT-FOR-DELIVERY.txt`，`build-appimage.sh`
   在 delivery 模式会拒绝这种 AppDir。**我没有把探针产物表述成可交付物。**
4. **可复现**：同树连续两次 `build.sh` 产出 **bit-identical** AppImage（同一 sha256）。
5. **未做**：A0a/A0b/A0d/A0e 的正式双宿主矩阵（T12）；未直接 FUSE 挂载（宿主无 libfuse2，
   用 `APPIMAGE_EXTRACT_AND_RUN=1` + 提取目录验证内容）。

---

## 1. 交付物

| 路径 | 作用 |
|---|---|
| `packaging/runtime/assemble-appdir.sh` | 在固定镜像内组装 AppDir：loader/glibc/库闭包/插件/字体/testdata/syntax 白名单/manifest/notices；两种模式（probe / delivery） |
| `packaging/runtime/AppRun` | 包内 loader 显式启动 + 全部搜索路径指向包内 + `--selfcheck` 分派 |
| `packaging/runtime/build-appimage.sh` | appimagetool（sha256 校验）→ 单文件 AppImage；`SOURCE_DATE_EPOCH` 固定；拒绝探针章 |
| `packaging/runtime/verify-not-shipped.sh` | 许可/不得随包闸门：文件级 sha256 + 库级内嵌定义扫描（zstd 优先，binutils 兜底） |
| `packaging/runtime/selfcheck.sh` | 干净环境跑 AppRun/AppImage 自检，断言 `M0-SMOKE ok=1` 与 `libc=$PREFIX/…` |
| `packaging/runtime/build.sh` | 一键：组装 → 闸门 → 自检 → 探针 AppImage → 自检 AppImage → CentOS 7 机制探针 |
| `packaging/runtime/README.md` | 运行手册 + 两种模式 + T60 插槽 + 已知限制 |
| `evidence/m0-runtime/` | 全部原始日志、runtime manifest、loader 解析表、内嵌定义清单 |

---

## 2. 观测（命令 + 原文）

### 2.1 组装

```
$ bash packaging/runtime/build.sh
===== 1/5 assemble AppDir (probe mode: distro KSyntaxHighlighting) =====
[assemble] binary: .../vedit-m0-smoke (1135456 B, sha256 d629a1ada168b88f143ae6ca9f64a2fd8fb2f8d54d75acce341233f7efbd449b)
[assemble] STAMPED PROBE-NOT-FOR-DELIVERY.txt
[assemble] AppDir: .../packaging/runtime/out/AppDir
[assemble] files=192 bytes=178299044 libs=171
[assemble] loader: 2c6c38a0…（见 manifest）  libc: …（见 manifest）
[assemble] ksynhl: da9d5f1c1640059b2809ddbb5c22e855479f9214005821abb3aa25c38a62f5e5 (source=probe)
APPDIR_MODE=probe
```

- 171 个库 = 从 smoke + katepart + 平台插件 + xcb GL 集成出发的 `ldd` 传递闭包；
- 平台插件：`libqoffscreen/libqminimal/libqxcb/libqwayland`；`kf6/parts/katepart.so` 必需；
- 每个文件的 sha256+大小进 `runtime-manifest.txt`（192 行）；
- 组件版本与许可进 `RUNTIME-NOTICES.txt`。

### 2.2 许可 / 不得随包闸门（探针预期红）

```
$ bash packaging/runtime/verify-not-shipped.sh --expect-probe packaging/runtime/out/AppDir
CHECK PASS  no KDE systemverilog.xml / verilog.xml by sha256
CHECK PASS  no file named systemverilog.xml / verilog.xml
   detector: zstd scanner (evidence/r2-ksh-embedded/scan-embedded-defs.py)
CHECK EXPECTED-RED  library embeds strict-GPL definitions (n=36) [INFO SystemVerilog: license='GPLv3+' bytes=25672]
CHECK EXPECTED-RED  probe AppDir is stamped PROBE-NOT-FOR-DELIVERY.txt
CHECK PASS  packaged loader present
CHECK PASS  packaged libc present
== summary: pass=4 fail=0 expected_red=2 ==
DELIVERY_BLOCKED=yes
```

⇒ 两条权威事实同时在案：**文件层面干净**（没有拷 KDE 的 XML），**库层面不干净**
（内嵌 36 个严格 GPL 定义）。后者只能靠 T60 的 `QRC_SYNTAX=OFF` 库消除。

### 2.3 AppDir 自检（A0c）

```
$ bash packaging/runtime/selfcheck.sh --appdir packaging/runtime/out/AppDir
M0-SMOKE ok=1 platform=offscreen qt=6.10.2 kf6=6.24.0 ktexteditor=1 ksynhl=6.24.0
  tree_sitter=0.25.9 window=1 editor=1 document=1 definitions=451 themes=30 verilog=1
  systemverilog=loadable(not-asserted,C7-pending) custom_path=1 tree_sitter_parser=1
  libc=/…/packaging/runtime/out/AppDir/lib/libc.so.6
CHECK PASS  M0-SMOKE ok=1
CHECK PASS  libc resolved under package
CHECK PASS  KTextEditor loaded
CHECK PASS  tree-sitter parser
SELFCHECK PASS
```

`custom_path=1` 来自项目自有 MIT 定义（`share/ksyntaxhighlighting/testdata/syntax/vedit-smoke.xml`）经
`Repository::addCustomSearchPath(dir)` 加载（该 API 读 `dir/syntax`，组装脚本因此把文件放在
`testdata/syntax/` 下一层——这是本次踩到并修掉的一个真问题）。

### 2.4 loader 解析表（A0c 机制证据，`70-loader-list.txt`）

```
$ "$APP/ld-linux-x86-64.so.2" --library-path "$APP/lib" --list "$APP/bin/vedit-m0-smoke"
	libKF6TextEditor.so.6 => /…/AppDir/lib/libKF6TextEditor.so.6
	libKF6SyntaxHighlighting.so.6 => /…/AppDir/lib/libKF6SyntaxHighlighting.so.6
	libtree-sitter.so.0.25 => /…/AppDir/lib/libtree-sitter.so.0.25
	libc.so.6 => /…/AppDir/lib/libc.so.6          ← 不是宿主
CHECK PASS  every NEEDED entry resolved inside the package (n=148)
CHECK PASS  libc.so.6 resolves to the packaged copy
```

### 2.5 探针 AppImage + 自检（提取模式，FUSE-free）

```
$ bash packaging/runtime/build-appimage.sh --probe packaging/runtime/out/AppDir
appimagetool sha256 OK (793fca0ce4bcc309dc65d3f66669f58e3755643e4dadbe03932e7b436128494f)
SOURCE_DATE_EPOCH=1793377525
APPIMAGE=packaging/runtime/out/VEdit-0.1.1-x86_64.probe.AppImage
APPIMAGE_BYTES=66763256
APPIMAGE_SHA256=524f7b0780ff4ee093095e6ebae34b816b18c3aec94310dd8abe281c1e04bd28
```

AppImage 自检（提取到临时目录后跑 AppRun）：

```
M0-SMOKE ok=1 … custom_path=1 … libc=/tmp/tmp.*/squashfs-root/lib/libc.so.6
CHECK PASS  M0-SMOKE ok=1 / libc resolved under package / KTextEditor / tree-sitter
SELFCHECK PASS
```

### 2.6 CentOS 7.9 机制探针（**不是** A0a/A0b/A0d/A0e 判定）

```
image: docker.m.daocloud.io/library/centos@sha256:be65f488…（CentOS 7.9.2009）
host glibc: ldd (GNU libc) 2.17
$ docker run … /app/AppRun --selfcheck --expect-libc-prefix /app
M0-SMOKE ok=1 … kshlib=/app/lib/libKF6SyntaxHighlighting.so.6 libc=/app/lib/libc.so.6
centos7_smoke_exit=0
CHECK PASS  CentOS 7 mechanism probe (bundled loader, bundled libc)
```

容器共享宿主内核（非 3.10 实机），正式矩阵与旧 Mesa/软件渲染归 T12/A0e —— **本报告不宣称过闸**。

### 2.7 可复现性

- `SOURCE_DATE_EPOCH` 默认取 `git log -1 --format=%ct`；`runtime-manifest.txt` 去掉了墙钟时间。
- 实测同一树连续两次 `build.sh`：

```
run 1: APPIMAGE_SHA256=524f7b0780ff4ee093095e6ebae34b816b18c3aec94310dd8abe281c1e04bd28  steps_failed=0
run 2: APPIMAGE_SHA256=524f7b0780ff4ee093095e6ebae34b816b18c3aec94310dd8abe281c1e04bd28  steps_failed=0
```

- 版本锚点按 T10 的结论：基础镜像 digest + `scripts/versions-qt.env`（sha256 `bb510b35…`）+
  `scripts/docker/qt6-locked-packages.txt`（sha256 `cbfa9b42…`，组合指纹 `c97a2566…`），
  **不拿 image id 当判据**（本镜像 id `sha256:2ab92e7c…` 只用于本次运行的记录）。

### 2.8 闸门的负控（证明它不是橡皮章，`80-negative-controls.log`）

```
### NC1: delivery-mode gate on a PROBE AppDir (must FAIL)      exit=1
CHECK FAIL  library embeds strict-GPL definitions (n=36) [... SystemVerilog: license='GPLv3+']
CHECK FAIL  probe AppDir is stamped PROBE-NOT-FOR-DELIVERY.txt
### NC2: delivery build from a PROBE AppDir (must REFUSE)      exit=1
ERROR: refusing to build a DELIVERY AppImage from a probe AppDir.
### NC3: a planted systemverilog.xml must be caught            exit=1
CHECK FAIL  a file named systemverilog.xml is present (must not ship; use our own LGPL definitions)
```

三条负控各自在一个真实方向上失败：许可闸门在 delivery 模式亮红、交付构建拒绝探针输入、
改名/新植入的 `.sv` 文件按名字与 sha256 被拦。没有一条是「看着像检查」的永真断言。

---

## 3. 与 T56 / D2=B 的关系

- ADR §5 的 A/B 断言（文件级）在本 AppDir 上**通过**，但 §5 的 G1（库级）**亮红** ——
  这正是 T56 预测的覆盖缺口，本任务用真实 AppDir 复现了它。
- 本任务因此把 G1 落成了可执行闸门（`verify-not-shipped.sh`，正控=本库 36 个 GPL → 拒；
  负控=无语法数据的 `libKF6TextEditor.so` → 过），并让 delivery 构建**在 T60 之前不可能通过**。
- 交付形态、loader、C6、C18 与许可解耦：换掉那一个 `.so` 即可从 probe 变 delivery，
  其余 191 个文件与 AppRun 不变（`--ksynhl` 插槽）。

---

## 4. 未闭合项

| # | 项 | 影响 | 归属 |
|---|---|---|---|
| U1 | **T60 干净库未交付**：delivery 构建被拒；探针 AppImage 只能作机制/自检证据 | 阻断交付 | Qt Build Engineer（T60） |
| U2 | **T16 自有语法定义未交付**：`share/ksyntaxhighlighting/syntax/` 只有 README；探针靠发行版内嵌定义才 `verilog=1` | delivery 自检会失败 | syn-kde-data（T16） |
| U3 | 30 个内嵌主题许可未清点（`QRC_SYNTAX` 管不到主题），本包不另发主题文件 | 许可面未闭合 | 数据许可清点任务 |
| U4 | 宿主无 libfuse2（只有 libfuse3）：直接 FUSE 挂载未测，自检走提取模式 | A7 运行前提需在 T12 真机记录 | T12 |
| U5 | 翻译未随包（镜像内 `qt6/translations` 为 0） | UI 本地化缺口（非运行时失败） | 后续 UI 任务 |
| U6 | QML 模块未随包（KTextEditor 链接 Qt6Qml，但 smoke 不建 QML 引擎） | 若 UI 用到 QML 需补 7.3 MB 模块 | M1/T12 观察 |
| U7 | 多宿主矩阵（A0a/A0b/A0d/A0e）未跑；本次只有 CentOS 7 机制探针 | 正式判定未做 | T12 |
| U8 | 打包的 smoke 二进制 sha256 `d629a1ad…` 来自 `target/qt6-build`（T10 证据里记录的是 `f0f53bfd…` 的另一份构建）；smoke 源正被其他 lane 并发修改（出现 `whitelist_verilog=` 等新字段） | 自检输出字段可能随重建变化；T11 未改该源（不在写域） | m0-build / 后续 lane 须知 |

---

## 5. 边界

- 只写 `packaging/runtime/` 与 `evidence/m0-runtime/`；未改 `src/`、`scripts/`、`build/`、门禁阈值；
- 未重建镜像（用 T10 的 `vedit-qt6-build:local`）；缺件已如实记录（翻译/主题/QML）；
- 未跑多宿主矩阵（T12）；CentOS 7 只做机制探针并明确标注；
- 没有把发行版 KSyntaxHighlighting 当作可交付输入：它只在**盖了探针章、闸门 EXPECTED-RED、delivery 构建被拒**的前提下出现。

---

## 6. 复跑

```bash
bash packaging/runtime/build.sh                 # 全流程（约 2–3 分钟；含 CentOS 7 机制探针）
bash packaging/runtime/build.sh --no-centos-probe
bash packaging/runtime/verify-not-shipped.sh --expect-probe packaging/runtime/out/AppDir
bash packaging/runtime/selfcheck.sh --appdir packaging/runtime/out/AppDir
bash packaging/runtime/selfcheck.sh --appimage packaging/runtime/out/VEdit-0.1.1-x86_64.probe.AppImage
```

T60 交付后改为 delivery：

```bash
docker run --rm -v "$PWD:/vedit-build" -w /vedit-build vedit-qt6-build:local \
  bash packaging/runtime/assemble-appdir.sh --ksynhl <T60>/libKF6SyntaxHighlighting.so.6 --syntax <T16 dir>
bash packaging/runtime/verify-not-shipped.sh packaging/runtime/out/AppDir   # 必须全绿
bash packaging/runtime/build-appimage.sh packaging/runtime/out/AppDir       # 不再加 --probe
```
