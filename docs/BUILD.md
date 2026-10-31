# 构建 VEdit

本文覆盖：从零构建（干净 CentOS 7 容器内）、固定（pinned）输入、镜像源替换、
GtkSourceView 与 slang 的源码构建、三种产物的打包命令，以及可复现性的边界。

> **两条构建 lane 并存**：§1–§11 是仍在树里的 Rust/GTK 产品（旧产品，Open Q7 未决）；
> §12 是重写后的 **C++/Qt6 构建底座**（M0，T10：`bash scripts/build-qt.sh`，
> 固定 Ubuntu 26.04 镜像 + Qt6/KF6/KTextEditor/KSyntaxHighlighting/tree-sitter）。
> 两者互不替代，门禁同时覆盖。

## 1. 前提

- **Docker**（daemon 可用即可；rootless 也可，`scripts/lib.sh` 有说明）。
- 宿主操作系统不限（macOS/Linux 均可），真正的构建发生在
  `docker.m.daocloud.io/library/centos:7` 的**摘要固定**镜像里。
- **构建期允许联网**（AC-19）：需要拉取固定版本的 tarball / wheel / rpm / verible 发布包。
  **产物运行期完全不需要网络**。
- 磁盘：构建镜像 + Cargo 缓存 + 产物，建议预留 **≥ 8 GB**。

## 2. 一条命令

```sh
bash scripts/build.sh
```

它做四件事（每一步的原始输出都落在 `build/evidence/`）：

1. 用 `scripts/versions.env` 里的 pin 构建 **pinned 构建镜像**（摘要固定的 centos:7 +
   Rust 1.99.0 + 源码构建的 GtkSourceView 4.6.1 + meson/ninja）；
2. 在镜像内 `cargo build --release --workspace`，并在 **Xvfb** 下跑主窗口冒烟（`--selftest smoke`）；
3. 在宿主上断言产物 ELF 的最高 GLIBC 符号版本 **≤ 2.17**，并记录 sha256；
4. 记录打包 lane 会消费的外部产物（T3 构建的 slang 二进制；此处不重建）。

产物：`build/artifacts/vd-app`（编辑器主程序）。门禁：`bash scripts/gates.sh`
（`cargo fmt --check` / `cargo clippy --all-targets -D warnings` / `cargo test --workspace`，
外加 `Cargo.lock` 策略检查：禁止 `tree-sitter` / `openssl` / `native-tls`，gtk-rs 固定 0.18 世代）。
宿主没有 GTK 开发文件时，编译类门禁会自动放进 pinned 镜像里跑并**明确打印这一降级**。

## 3. 固定输入（`scripts/versions.env` 是唯一来源）

| 输入 | 固定方式 | 值（0.1.0） |
|---|---|---|
| 基础镜像 | **digest** | `docker.m.daocloud.io/library/centos@sha256:be65f488…8ea4`（7.9.2009） |
| centos-vault 镜像源 | URL | `https://mirrors.tuna.tsinghua.edu.cn/centos-vault/7.9.2009` |
| Rust | 版本 + 安装器 sha256 | `1.99.0` / rustup 1.29.2 |
| GtkSourceView | URL + sha256 | 4.6.1 / `63f487a7…df874` |
| meson / ninja | wheel URL + sha256 | 0.59.4 / 1.11.1 |
| Verible | 发布 tarball sha256 + **三个二进制各自 sha256** | `v0.0-4298-g085eef05` |
| slang | 产物 sha256（由 T3 的 `build/slang/` 构建） | `63cb60a0…832252` |
| appimagetool | sha256 | `793fca0c…8494f` |

`scripts/docker/rpm-baseline.txt`（382 个包）是构建镜像的 rpm 快照，用来发现"构建环境被悄悄改动"。

## 4. 镜像源替换（CentOS 7 的坑）

`vault.centos.org` 从本项目构建宿主返回 **403**，因此必须用 **centos-vault 快照镜像**：

- 可用：清华 TUNA、阿里云、中科大；
- 路径形如 `.../centos-vault/7.9.2009/{os,updates,extras}/x86_64/`
  —— **注意没有 `/centos/` 这一段**，写错就是 403；
- 换源只需改 `scripts/versions.env` 的 **`CENTOS_VAULT_BASE`**（构建镜像与 slang 构建都读它），
  不要改 Dockerfile 里的 URL 拼接；
- 举例：`CENTOS_VAULT_BASE="https://mirrors.aliyun.com/centos-vault/7.9.2009"`。
- 拉取基础镜像失败时 `scripts/lib.sh` 会重试 3 次（2 s / 4 s 退避），仍失败则用**同一 digest**
  回退到 `VEDIT_BASE_IMAGE_FALLBACK`（默认 `docker.io`），并把解析出的 ref 通过 `--build-arg BASE_IMAGE`
  传给 Dockerfile；主镜像源可用 `VEDIT_BASE_IMAGE` 覆盖（digest 不变）。

## 5. GtkSourceView 4.6.1（源码构建）

CentOS 7 只有 gsv 3.24.8 的 yum 包；VEdit 需要 gsv4 的 `verilog.lang` / `systemverilog.lang`，
所以从源码构建并安装到镜像里的 `/opt/gsv4`：

- 为什么是 **4.6.1** 而不是 4.8.x：4.8 的 `meson.build` 要求 `gtk+-3.0 >= 3.24`，而 CentOS 7 只有
  3.22.30（升 GTK 会连带升 GLib/Pango，等于自建桌面栈，违背"免安装"前提）；
  4.6.1 是接受 3.22 的最新 gsv4 版本；
- 4.6.1 与 4.8.4 的 **verilog/systemverilog 语言定义逐字节相同**（sha256 一致，
  见 `assets/language-specs/README.md`），因此这个版本回退对本产品零损失；
- 构建参数：`meson setup build --prefix=/opt/gsv4 --libdir=lib64 -Dgtk_doc=false -Dgir=false
  -Dvapi=false -Dinstall_tests=false -Dglade_catalog=false`。

## 6. slang（glibc 2.17 专用构建）

官方 Linux 二进制依赖 GLIBC 2.18/2.25/2.27，CentOS 7 跑不了，因此本项目从源码构建：

```sh
bash build/slang/build.sh
```

- 在**全新的** centos:7 容器内构建（宿主工具链不参与），slang `v12.1`
  （commit 固定为 `42710c78…`）；CMake 用 Kitware 的官方 x86_64 二进制（3.31.6，
  因为 v12.1 要求 CMake ≥ 3.28，而 CentOS 7 的 cmake3 只有 3.17）；
- 断言最高 GLIBC **≤ 2.17**、且不依赖 `libstdc++`/`libgcc_s`；
- 在**第二个无网** centos:7 容器里跑 `slang --version` 与一次真实的
  `--lint-only -Weverything --diag-json`，断言输出是 JSON 数组；
- 产物 `build/slang/out/slang`（sha256 与 `scripts/versions.env` 的 `SLANG_BIN_SHA256` 对应），
  证据在 `build/slang/evidence/`，说明在 `build/slang/REPORT.md`。

## 7. 打包三种产物

```sh
bash packaging/appimage/run-evidence.sh   # AppImage（含离线干净容器验收）
bash packaging/tarball/build.sh           # tar.gz + 自解压 .run（含离线干净容器验收）
```

- 两者都只消费 pinned 输入：verible 从 `VERIBLE_URL` 下载并按 sha256 校验（**不复制** `.mpd/recon/`）、
  slang 用 `SLANG_BIN_SHA256` 校验、gsv/GTK 来自 pinned 镜像；
- 两者都做 **GLIBC ≤ 2.17 全量断言**（遍历包内每个 ELF）与 **RPATH/RUNPATH 白名单**检查
  （唯一允许项是 CentOS 7 上游 elfutils 自带的 `/usr/lib64/elfutils`，理由见
  `packaging/tarball/rpath-allowlist.txt`）；
- 干净容器验收镜像**不含** gtk3/GtkSourceView，负断言写在镜像定义里，
  验收脚本在 `--network none` 下跑 `--selftest all`（6 个套件；检查总数随版本增长，
  以实际输出为准，不要在文档里写死）。

## 8. 可复现性：已控制到什么程度

**已控制**

- 基础镜像按 **digest** 固定；所有下载按 **sha256** 校验；rpm 快照可比对；
- 构建发生在目标平台自身（centos:7），因此工具链/glibc 不会因宿主不同而漂移；
- gsv 的 tarball sha256 固定；verible 的 tarball 与三个二进制分别校验；slang 产物按 sha256 消费。

**边界（如实记录）**

- CentOS 7 的 `tar 1.26` **没有 `--sort=name`**：tarball 已归一 owner/group 且 `gzip -n`，
  但目录项顺序随文件系统，**不声称字节级可复现**；
- Rust 会把 panic 位置编译进 ELF。发布构建统一注入 `RELEASE_RUSTFLAGS`（`scripts/versions.env`，
  一组 `--remap-path-prefix`：构建树 → `/vedit`、cargo home → `/cargo`），**所有产物必须消费同一份**，
  否则不同产物里的 vd-app 不是同一个二进制。打包脚本会**统计并分类**产物中残存的 `/vedit-build` 字符串（目标 0；`--remap-path-prefix`
  不保证覆盖 `env!()` 这类编译期字面量，残留项会被逐个列出并标注来源，不预设为 0）；
- "冷重建结果一致"的自动断言属于端到端验收 lane（`tests/e2e/`，T27）的范围；
  **该套件落地前，本文不声称这一项已验证**。落地后以它的退出码与证据为准。

## 9. 重新生成 Rust crate 许可清单

`docs/rust-crates.txt` 是生成物，不要手改。在 pinned 镜像里：

```sh
cargo metadata --format-version 1 --all-features > meta.json
# 读取每个 package 的 license（或 license_file）字段，按 (name, version) 排序
```

七个 `vd-*` crate 是本项目自有代码，其许可随项目根 `LICENSE`。

## 10. 排障

| 现象 | 原因与处理 |
|---|---|
| `yum` 403 / `Cannot retrieve metalink` | 镜像源路径写错（缺 `/centos-vault/` 或多了 `/centos/`）；改 `CENTOS_VAULT_BASE` |
| `pkg-config ... gtksourceview-4 not found` | 在宿主直接 `cargo build` 了；GTK 栈只在 pinned 镜像里存在，用 `bash scripts/build.sh` |
| `glibc gate` 失败 | 某个 ELF 的符号需求 > 2.17：通常是误用了宿主预编译库，检查捆绑来源 |
| `RUNPATH` gate 报 `UNKNOWN` | 新增了绝对路径依赖；若确属上游自带，把理由写进 `rpath-allowlist.txt` 再放行 |
| `verible-verilog-ls` 找不到 | 运行期只按"配置 → 环境变量 → 包内路径"解析，**不查 PATH**；用 `VEDIT_VERIBLE_DIR` 指向目录 |
| `--selftest all` 失败在 `lsp` 的 definition 检查 | 运行目录里没有 fixtures：设置 `VEDIT_SELFTEST_FIXTURES=<share>/vedit/testdata` |

## 11. GitHub Actions（CI / 镜像 / 发版）

仓库有三条工作流（`.github/workflows/`），全部复用同一个 pinned CentOS 7 镜像：

| 工作流 | 触发 | 做什么 |
|---|---|---|
| `ci.yml` | PR → `dev`/`main`；push → `dev` | `scripts/gates.sh` + `scripts/build.sh`（release build、Xvfb smoke、GLIBC ≤ 2.17 断言），上传 `build/evidence/` 与 `build/artifacts/vd-app` |
| `image.yml` | `scripts/docker/**`、`scripts/versions.env`、`scripts/lib.sh`、`scripts/build-image.sh` 变更；手动 | 构建 pinned 镜像并推送 `ghcr.io/<owner>/vedit-build:<sha>` 与 `:latest` |
| `release.yml` | tag `v*`；手动 | slang → `scripts/build.sh` → AppImage → tar.gz/.run → `SHA256SUMS` → GitHub Release |

工作流复用已发布的镜像、不重建时用三个脚本开关（`scripts/lib.sh`）：

```sh
VEDIT_BUILD_IMAGE_REF=ghcr.io/<owner>/vedit-build:latest \
VEDIT_SKIP_IMAGE_BUILD=1 \
  bash scripts/gates.sh
```

- `VEDIT_BUILD_IMAGE_REF`：镜像 ref，默认 `vedit-build:local`；
- `VEDIT_BUILD_IMAGE_PULL=1`：先 `docker pull` 该 ref；
- `VEDIT_SKIP_IMAGE_BUILD=1`：只复用现有镜像，缺失时打印 `FATAL:` 并 exit 1；
- 只建镜像（不编译工作区）：`bash scripts/build-image.sh`；
- **不设这些变量时本地行为与以前完全一致**。

注意：

- 镜像构建走 BuildKit，不再需要 `DOCKER_BUILDKIT=0`；`scripts/docker/Dockerfile.build` 用
  `COPY vault.repo.in` + `sed` 渲染 yum 源，两种 builder 语法都合法；
- GHCR 包默认私有：同仓库的 `GITHUB_TOKEN` 可以拉取，fork PR 拉不到时工作流退回本地构建；
- `scripts/build.sh` 在镜像内跑两处 `--selftest`：先 `--selftest smoke --probe-hold-ms 2500`
  做 Xvfb 窗口冒烟，再跑套件集合——`VEDIT_SLANG` 与 `VEDIT_VERIBLE_LS` 同时可用时是一次 `all`
  （6 个套件），否则**逐个**跑 `smoke highlight config fim`；任一套件非 0 即 `FATAL` 退出。
  CI 会先 `bash build/slang/build.sh` 与 `bash scripts/fetch-verible.sh`，因此走 `all` 六套件。

## 12. Qt6 / KF6 C++ 构建底座（M0，T10）

> 本节描述**重写后的 C++/Qt6 产品**的构建底座；§1–§11 描述仍在树里的 Rust/GTK 产品
> （Open Q7 未决，两条 lane 并存，彼此不替代）。

### 12.1 为什么用现代发行版做构建镜像

交付物把 **loader + 新版 glibc + Qt6/KF6 运行时**打进 AppImage（C2，T11），所以 C++
二进制可以合法地要求比 CentOS 7 的 glibc 2.17 更新的符号——由包内 loader 提供。
在 CentOS 7 上构建这条 lane 反而**不可能**：

- KF6 6.24.0 上游要求 Qt ≥ 6.8（`set(REQUIRED_QT_VERSION 6.8.0)`，见
  `KDE/ktexteditor` v6.24.0 与 `KDE/syntax-highlighting` v6.24.0 的 CMakeLists，
  证据：`evidence/m0-build/upstream/`）；
- CentOS 7 的 vault 里根本没有 Qt ≥ 6.5 的包（C5 的起步线都够不到）。

构建镜像因此是固定 digest 的 **Ubuntu 26.04.1 LTS**；它在
`scripts/docker/Dockerfile.build` 里是一个 stage（`qt6-build`），**最后一个 stage 仍是
原来的 CentOS 7 镜像**，所以不带 `--target` 的
`docker build -f scripts/docker/Dockerfile.build` 行为与以前完全一致。

CentOS 7（kernel 3.10）仍然成立：镜像里的 glibc 2.43 编译下限是 kernel **3.2.0**
（`/lib/x86_64-linux-gnu/libc.so.6` 内嵌 `Minimum supported kernel: 3.2.0`，其
`.note.ABI-tag` 也是 `ABI: 3.2.0`）；ELF 基线是普通 x86-64
（`x86 ISA needed: x86-64-baseline`），不是 x86-64-v3。

### 12.2 一条命令

```sh
bash scripts/build-qt.sh              # 镜像 → configure → build → ctest → xcb/软件渲染冒烟
bash scripts/build-qt.sh --no-test    # 只 configure + build（即 C++ 静态检查）
```

每一步的原始输出落在 **`evidence/m0-build/build-logs/`**（T10 的证据目录，随仓库提交）；
CMake 构建树在 gitignored 的 **`target/qt6-build/`**，探针目录在 `target/qt6-probe/`：

| 文件 | 内容 |
|---|---|
| `build-logs/20-qt6-image-build.log` | 固定镜像构建（digest、.deb sha256 校验、安装后版本断言） |
| `build-logs/21-qt6-image-id.txt` | 镜像 id |
| `build-logs/30-qt6-image-facts.txt` / `30-qt6-package-snapshot.txt` | 镜像事实与完整 dpkg 快照 |
| `build-logs/31-qt6-configure.log` | cmake configure（版本矩阵打印） |
| `build-logs/32-qt6-build.log` | 编译 |
| `build-logs/33-qt6-ctest.log` | `ctest` |
| `build-logs/34-qt6-elf.txt` | 产物 ELF：NEEDED、最高 GLIBC 符号、构建期解析到的库、sha256 |
| `build-logs/35-qt6-xvfb.log` / `36-qt6-xcb-smoke.log` | Xvfb + xcb + llvmpipe 的窗口冒烟 |

### 12.3 版本矩阵与锁定方式

| 组件 | 锁定值 | 来源 |
|---|---|---|
| 构建镜像 | `docker.m.daocloud.io/library/ubuntu@sha256:6a06111c…4d3c` | Ubuntu 26.04.1 LTS |
| Qt6 | 6.10.2（`qt6-base-dev 6.10.2+dfsg-7`） | Ubuntu 26.04 |
| KF6 | 6.24.0（`libkf6*-dev 6.24.0-0ubuntu1`） | Ubuntu 26.04 |
| KTextEditor | 6.24.0 | 同上 |
| KSyntaxHighlighting | 6.24.0 | 同上 |
| tree-sitter | 0.25.9（`libtree-sitter-dev 0.25.9-7`，`pkg-config tree-sitter`） | Ubuntu 26.04 |
| glibc（随包） | 2.43-2ubuntu2.4，min kernel 3.2.0 | `libc6` |
| 工具链 | cmake 4.2.3 / ninja 1.13.2 / g++ 15.2.0 | 同上 |

锁定有三层，任一层不符都**硬失败**：

1. `scripts/versions-qt.env`：版本矩阵与最低版本（C5 的 Qt 6.5 起步线、KF6 6.24 的
   Qt 6.8 要求、glibc 的 kernel 下限）；
2. `scripts/docker/qt6-locked-packages.txt`：每行 `<包>=<精确版本> <*.deb sha256>`，
   镜像构建时逐个 `apt-get download` 校验 sha256 再安装，安装后再用 `dpkg-query` 断言；
3. `cmake/VEditVersions.cmake`：把同一份 env 读进 CMake，所有依赖用 `EXACT` 请求
   （Qt6 / KF6 / KTextEditor / KSyntaxHighlighting / tree-sitter），版本漂移在
   configure 阶段就报错。

### 12.4 CMake 骨架

- 根 `CMakeLists.txt`：C++20；Qt6 Core/Gui/Widgets/Network/PrintSupport/Qml/TextToSpeech
  （后两个是 KTextEditor 的链接依赖）；KF6 CoreAddons/Config/TextEditor/
  SyntaxHighlighting；tree-sitter 经 pkg-config 精确锁定；
- `cmake/VEditVersions.cmake`：从 `scripts/versions-qt.env` 读 pin，并校验 floor；
- `cmake/smoke/`：**M0 冒烟 target**——Qt6 窗口、KTextEditor 文档、
  KSyntaxHighlighting 定义/主题、tree-sitter 运行时各验证一次，编译参数
  `-Wall -Wextra -Wpedantic -Werror`，`ctest` 在 offscreen 下跑它；
- `src/app/` 是 app-shell lane（T13）的交付物：根 CMakeLists 在它出现后**自动**纳入，
  本文件无需二次编辑。T10 不写 `src/app/`。

冒烟程序打印一行机器可读结论，并报告进程实际解析到的 `libc.so.6` 路径
（A0c 自检钩子，T11 复用）：

```
M0-SMOKE ok=1 platform=offscreen qt=6.10.2 kf6=6.24.0 ... definitions=452 themes=30 verilog=1 ... libc=/usr/lib/x86_64-linux-gnu/libc.so.6
```

### 12.5 门禁

C++ lane 没有独立的 MPD gate id（ledger 的 id 表是固定的），因此它折进三个脚本
（公共实现在 `build/gates/qt6-cxx.sh`）：

- `gates`（唯一可跑重活的 ledger gate）：完整 configure + build + ctest + xcb 冒烟；
- `typecheck`：`--no-test`，即 `-Werror` 编译（C++ 静态检查）；
- `tests`（仅人工）：同样的 ctest。

`CMakeLists.txt` 存在即视为**硬要求**：C++ lane 失败则 gate 红，不允许打印 N/A 通过。

### 12.6 已知边界（如实记录）

- **T10 不含运行时打包**：把 loader/glibc/Qt6/KF6/plugins/syntax data 打进 AppImage 是
  T11（m0-runtime）；本节的产物是可执行的冒烟程序，不是交付物。
- **双宿主闸门是 T12**：本机构建容器与宿主**共享内核**，"CentOS 7.9 + glibc 2.17
  userland 能跑"已在 `centos:7` 容器里验证（`evidence/m0-build/14-*`：包内 loader 与
  包内 glibc 解析成功），但 kernel 3.10 本身没有在这里被跑到；真实 3.10 主机由 T12 验。
- 冒烟窗口是 offscreen 或 Xvfb+xcb，没有真实窗口管理器；旧 Mesa 的软件渲染回退需 T12
  在真机按 A0e 验（镜像内已用 llvmpipe + `QT_OPENGL=software` +
  `LIBGL_ALWAYS_SOFTWARE=1` 跑通一遍）。
- **`.v` 按文件名会解析成 "V" 语言**：本镜像的 KSyntaxHighlighting 内嵌 451 个定义，
  `definitionForName("Verilog")` 有效，但 `definitionForFileName("test.v")` 返回 `V`
  （vlang 也占用 `*.v`）。T16/T20 必须按名字或自定义映射处理，不能只依赖扩展名。
  证据：`evidence/m0-build/12-ksyntax-definitions-probe.txt`。
- **SystemVerilog 定义**：镜像里 `definitionForName("SystemVerilog")` 可加载，但冒烟
  **只报告不断言**，因为 C7 的许可处置未定（Open Q1），"构建镜像能加载"不等于
  "交付物随包"。
- 镜像重建需要联网拉取固定版本 .deb；归档一旦停供某个精确版本，镜像构建会**硬失败**
  （预期行为：复用已发布镜像 ref，而不是悄悄换版本）。

### 12.7 排障

| 现象 | 原因与处理 |
|---|---|
| `mkdir ~/.docker/... read-only file system` | 沙箱/只读 HOME；`scripts/lib-qt.sh` 自动退到 `target/docker-config` |
| `base name (${BASE_IMAGE}) should not be blank` | 两个 base image ARG 必须声明在**第一个 FROM 之前** |
| `find_package(KF6 …)` 找不到 | KF6 没有总包 config；按框架 `find_package(KF6TextEditor …)`（本仓库已如此） |
| `Could not find Qt6Qml/TextToSpeech` | KTextEditor 的链接依赖；镜像装 `qt6-declarative-dev` / `qt6-speech-dev`（已锁定） |
| `KTextEditor::Editor::instance()` 为 null | 缺 katepart 插件（`libkf6texteditor-katepart`，已锁定） |
| 想复用已发布镜像 | `VEDIT_QT6_BUILD_IMAGE_REF=… VEDIT_QT6_BUILD_IMAGE_PULL=1 bash scripts/build-qt.sh` |
