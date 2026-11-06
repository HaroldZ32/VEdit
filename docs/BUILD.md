# 构建 VEdit

本文覆盖：**Qt6 交付物**的构建与打包（§2、§7.1、§12）、**旧 Rust/GTK3 产品线**的构建与打包
（§2.1、§7.2，以及 §3–§5、§8、§10、§11 的 CentOS 7 / GTK 内容；§6 与 §9 为两条 lane 共用）、
固定（pinned）输入与可复现性边界。

> **两条构建 lane 并存，交付物只有一条。**
>
> - **Qt6 交付物**（`vedit-app`）：`bash scripts/build-qt.sh`（§2）→ `bash packaging/runtime/build.sh`
>   （§7.1）→ `packaging/runtime/out/VEdit-<ver>-x86_64.AppImage` + `out/SHA256SUMS`。
>   构建底座、版本矩阵与边界见 §12。
> - **旧 Rust/GTK3 产品**（`vd-app`）：`bash scripts/build.sh`（§2.1）→ `build/artifacts/vd-app`，
>   打包见 §7.2。按需求非目标 5 保留，**不是交付物**。
>
> 两条 lane 互不替代；§6 的 slang 二进制、§9 的 Rust crate 清单两条 lane 共用，§10.1 是 Qt 产品的
> 运行期工具链发现。

## 1. 前提

- **Docker**（daemon 可用即可；rootless 也可，`scripts/lib.sh` 有说明）。
- 宿主操作系统不限（macOS/Linux 均可），真正的构建发生在**摘要固定**的镜像里：
  Qt6 交付物用 **Ubuntu 26.04** 镜像（§12.1），旧 GTK3 线用
  `docker.m.daocloud.io/library/centos:7`（§2.1）。
- **构建期允许联网**：需要拉取固定版本的 tarball / deb / rpm / verible 发布包。
  **产物运行期完全不需要网络**。
- 磁盘：构建镜像 + Cargo 缓存 + CMake 构建树，建议预留 **≥ 8 GB**。

## 2. 一条命令（Qt6 交付物）

```sh
bash scripts/build-qt.sh          # 固定镜像 → Rust 服务层 → CMake configure/build/ctest → xcb/软件渲染冒烟
```

产物：**`target/qt6-build/src/app/vedit-app`**（CMake 构建树 `target/qt6-build/` 已被 gitignore）。
这一步只构建产品二进制；**交付物**是下一步的单文件 AppImage：

```sh
bash packaging/runtime/build.sh   # 组装 AppDir + 许可/F2 隔离 + AppImage + 自检/隔离 + CentOS 7 探针
# -> packaging/runtime/out/VEdit-<ver>-x86_64.AppImage + out/SHA256SUMS
```

门禁（当前命令）：

- `bash build/gates/qt6-cxx.sh` —— Qt6/KF6 C++ 车道（configure + build + ctest + xcb 冒烟）；
- `bash scripts/gates.sh` —— Rust 服务层（fmt / clippy / test / 依赖策略）；
- `bash build/gates/lrelease.sh` —— 中文目录零 unfinished / untranslated；
- `bash build/gates/docs.sh` —— 文档门禁。

运行期依赖（KF6/Qt6 运行库、`libtree-sitter` 0.25、Qt 平台插件）与版本矩阵见 §12.3 / §12.6。

### 2.1 旧版 GTK3 构建（不是 Qt 交付物）

```sh
bash scripts/build.sh
```

它做四件事（每一步的输出落在 `build/evidence/`）：

1. 用 `scripts/versions.env` 里的 pin 构建 **pinned 构建镜像**（摘要固定的 centos:7 +
   Rust 1.99.0 + 源码构建的 GtkSourceView 4.6.1 + meson/ninja）；
2. 在镜像内 `cargo build --release --workspace`，并在 **Xvfb** 下跑主窗口冒烟（`--selftest smoke`）；
3. 在宿主上断言产物 ELF 的最高 GLIBC 符号版本 **≤ 2.17**，并记录 sha256；
4. 记录打包 lane 会消费的外部产物（T3 构建的 slang 二进制；此处不重建）。

产物：`build/artifacts/vd-app`（**旧产品**主程序，不是 `vedit-app`）。门禁：`bash scripts/gates.sh`
（`cargo fmt --check` / `cargo clippy --all-targets -D warnings` / `cargo test --workspace`，
外加 `Cargo.lock` 策略检查）。宿主没有 GTK 开发文件时，编译类门禁会自动放进 pinned 镜像里跑并
**明确打印这一降级**。这条 lane 的其余细节见 §3–§5、§8、§10、§11，打包见 §7.2。

## 3. 固定输入（旧版 lane 与共用工具：`scripts/versions.env` 是唯一来源）

> Qt6 车道的版本矩阵不在这里：它在 `scripts/versions-qt.env` 与
> `scripts/docker/qt6-locked-packages.txt`，见 §12.3。本表是旧版 GTK3 线的 pin，外加两条 lane
> 共用的 Verible / slang / appimagetool。

| 输入 | 固定方式 | 值 |
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

## 4. 镜像源替换（旧版 lane：CentOS 7 的坑）

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

## 5. GtkSourceView 4.6.1（旧版 lane 的源码构建）

> Qt 产品**不用** GtkSourceView：它用 KDE 的 KSyntaxHighlighting（§12.3）。本节只服务旧 GTK3 线。

CentOS 7 只有 gsv 3.24.8 的 yum 包；旧产品需要 gsv4 的 `verilog.lang` / `systemverilog.lang`，
所以从源码构建并安装到镜像里的 `/opt/gsv4`：

- 为什么是 **4.6.1** 而不是 4.8.x：4.8 的 `meson.build` 要求 `gtk+-3.0 >= 3.24`，而 CentOS 7 只有
  3.22.30（升 GTK 会连带升 GLib/Pango，等于自建桌面栈，违背"免安装"前提）；
  4.6.1 是接受 3.22 的最新 gsv4 版本；
- 4.6.1 与 4.8.4 的 **verilog/systemverilog 语言定义逐字节相同**（sha256 一致，
  见 `assets/language-specs/README.md`），因此这个版本回退对旧产品零损失；
- 构建参数：`meson setup build --prefix=/opt/gsv4 --libdir=lib64 -Dgtk_doc=false -Dgir=false
  -Dvapi=false -Dinstall_tests=false -Dglade_catalog=false`。

## 6. slang（两条 lane 共用：glibc 2.17 专用构建）

官方 Linux 二进制依赖 GLIBC 2.18/2.25/2.27，CentOS 7 跑不了，因此本项目从源码构建：

```sh
bash build/slang/build.sh
```

产物 `build/slang/out/slang` 既被旧线打包消费，也被 Qt 交付物的打包与裸构建的运行期解析消费
（§10.1）。

- 在**全新的** centos:7 容器内构建（宿主工具链不参与），slang `v12.1`
  （commit 固定为 `42710c78…`）；CMake 用 Kitware 的官方 x86_64 二进制（3.31.6，
  因为 v12.1 要求 CMake ≥ 3.28，而 CentOS 7 的 cmake3 只有 3.17）；
- 断言最高 GLIBC **≤ 2.17**、且不依赖 `libstdc++`/`libgcc_s`；
- 在**第二个无网** centos:7 容器里跑 `slang --version` 与一次真实的
  `--lint-only -Weverything --diag-json`，断言输出是 JSON 数组；
- 产物 `build/slang/out/slang`（sha256 与 `scripts/versions.env` 的 `SLANG_BIN_SHA256` 对应），
  证据在 `build/slang/evidence/`，说明在 `build/slang/REPORT.md`。

## 7. 打包：Qt 交付物与旧版产物

### 7.1 Qt 交付物（唯一 AppImage）

**交付物 = `bash packaging/runtime/build.sh` 的产物 `packaging/runtime/out/VEdit-<ver>-x86_64.AppImage`（配 `out/SHA256SUMS`）。**

```sh
bash packaging/runtime/build.sh
```

这一条命令产出**唯一交付物**：单文件 AppImage
`packaging/runtime/out/VEdit-<ver>-x86_64.AppImage`，以及配套的
`packaging/runtime/out/SHA256SUMS`。它是 F2 私有运行时形态：包内自带 loader / glibc / Qt6 / KF6 /
KTextEditor / 自建 KSyntaxHighlighting / tree-sitter / 字体，并带 `bin/translations/` 的中文翻译
（`vedit_zh_CN.qm`、`qtbase_zh_CN.qm`）。流程、白名单与隔离契约见 `packaging/runtime/README.md`
（该 README 把 `out/AppDir/` 明确列为**中间产物**；当前基线产物 137,079,288 字节，sha256 见
`out/SHA256SUMS`）。

**版本号说明**：`<ver>` 来自工作区 `Cargo.toml` 的 `version`（打包脚本
`packaging/runtime/build-appimage.sh:53` 读取），当前是 **0.1.1**，所以现在产物名是
`VEdit-0.1.1-x86_64.AppImage`；Qt 产品自报的版本是 **0.2.0**（`src/app/main.cpp:1851`）。
两个数字**不是同一来源**，本轮不统一（产品决策）：读文档时，产物名以打包脚本读到的
`Cargo.toml` 版本为准，应用内版本以 `main.cpp` 为准。

### 7.2 旧版 GTK 产物（不是 Qt 交付物）

```sh
bash packaging/appimage/run-evidence.sh   # 旧 Rust/GTK vd-app 的 AppImage
bash packaging/tarball/build.sh           # 旧 vd-app 的 tar.gz + 自解压 .run
```

这两条是**旧 Rust/GTK 产品**的打包/验收管线，按需求非目标 5 保留；`packaging/appimage/README.md`
首段自述为 “Legacy GTK product packaging … do not present this pipeline as the final Qt AppImage”，
载荷是 `build/artifacts/vd-app`，不是 `vedit-app`。它们**不是** Qt 交付物：

- 旧 AppImage 内**没有** `bin/translations/`（实测 `unsquashfs -o <offset> -l` 匹配
  `zh_CN|translations` = 0），而 7.1 的 Qt AppImage 有 `vedit_zh_CN.qm` / `qtbase_zh_CN.qm`；
- 体积不同：旧管线 47,745,528 字节 vs Qt 交付物 137,079,288 字节（同一台机器、同一天构建）；
- 旧管线的干净容器验收仍从宿主 glibc 家族解析 **8** 个库
  （`evidence/appimage/clean-room.log`：`from host glibc family: 8`），而 Qt 交付物是 F2 私有运行时。

旧管线自身的断言（pinned 输入 + sha256、GLIBC ≤ 2.17 全量断言、RPATH/RUNPATH 白名单）仍然有效，
但它们证明的是**旧产物**的性质，不能用来证明 Qt 交付物。

## 8. 可复现性（旧版 lane 的边界）

> 本节以下条目针对**旧产物**（`vd-app` / tar.gz / `.run`）。Qt 交付物的私有运行时、F2 隔离与
> 四容器矩阵证据见 `packaging/runtime/README.md` 与 `evidence/slice3-pack/`。

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

`docs/rust-crates.txt` 是生成物，不要手改。清单覆盖工作区 Rust crate；Qt 产品通过 `vd-ffi` 链接
Rust 服务层，所以两条 lane 共用它。在 pinned 镜像里：

```sh
cargo metadata --format-version 1 --all-features > meta.json
# 读取每个 package 的 license（或 license_file）字段，按 (name, version) 排序
```

七个 `vd-*` crate 是本项目自有代码，其许可随项目根 `LICENSE`。

## 10. 排障（旧版 lane）

> 下表是旧 GTK3 线的常见问题；Qt 产品的构建问题见 §12.7，运行期工具链发现见 §10.1。

| 现象 | 原因与处理 |
|---|---|
| `yum` 403 / `Cannot retrieve metalink` | 镜像源路径写错（缺 `/centos-vault/` 或多了 `/centos/`）；改 `CENTOS_VAULT_BASE` |
| `pkg-config ... gtksourceview-4 not found` | 在宿主直接 `cargo build` 了；GTK 栈只在 pinned 镜像里存在，用 `bash scripts/build.sh` |
| `glibc gate` 失败 | 某个 ELF 的符号需求 > 2.17：通常是误用了宿主预编译库，检查捆绑来源 |
| `RUNPATH` gate 报 `UNKNOWN` | 新增了绝对路径依赖；若确属上游自带，把理由写进 `rpath-allowlist.txt` 再放行 |
| `verible-verilog-ls` / `slang` 找不到 | 运行期按 **随包 → 本地取 → PATH → env** 解析（见 §10.1）；裸构建里先跑 `bash scripts/fetch-verible.sh` 与 `bash build/slang/build.sh`，或用 `VEDIT_VERIBLE_LS` / `VEDIT_SLANG` 指定 |
| `--selftest all` 失败在 `lsp` 的 definition 检查 | 运行目录里没有 fixtures：设置 `VEDIT_SELFTEST_FIXTURES=<share>/vedit/testdata` |

### 10.1 运行期工具链发现（Qt 产品：诊断 / 大纲：随包 / 本地取 / env）

`vedit-app` 的诊断/大纲面板需要 `verible-verilog-ls`（LSP）与 `slang`（deep lint）。
解析顺序固定为 **bundled → local checkout → PATH → env**，并把结果打印出来
（`--selfcheck` 的 `TOOLCHAIN source=verible:<source> slang:<source> missing=[...] local=[...]`）：

1. **随包（bundled）**：把两个二进制放进 `vd-app` 同目录的 `bin/`（AppDir/AppImage
   形态；打包后不需要任何额外配置）。
2. **本地取（local checkout，裸构建用这条）**：在源码树里执行
   ```sh
   bash scripts/fetch-verible.sh     # -> target/verible-pin/extract/verible-*/bin/
                                     #    以及 .mpd/recon/verible/bin/ 链接
   bash build/slang/build.sh         # -> build/slang/out/slang
   ```
   解析器从 `target/qt6-build/src/app/vedit-app` 向上找到 checkout 根（标记
   `scripts/fetch-verible.sh`）后自动探测上述产物，**不需要设置任何环境变量**。
3. **env 指定**：`VEDIT_VERIBLE_LS=/path/verible-verilog-ls`、
   `VEDIT_SLANG=/path/slang`（也支持 `VEDIT_VERIBLE_LS_DIR` / `VEDIT_SLANG_DIR`
   指向目录）。

缺工具时，diagnostics/outline 面板只显示**一条**面板级状态（缺什么、影响什么、
三条修复路径），outline 保持空表 + 一行原因；不会按文件/请求重复报错，也不把
“环境未满足”当作文件级 Error。

## 11. GitHub Actions（旧版 lane 的 CI / 镜像 / 发版）

> 以下三条工作流仍针对旧 GTK3 线。Qt 交付物的构建与发布流程见 `docs/RELEASING.md` §6 与本文 §7.1；
> 它们不是 Qt 交付物的发布路径。

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

> 本节是 **Qt6 交付物**（`vedit-app`）的构建底座，也就是 §2 那条命令的细节；§2.1 与 §3–§5、§8、
> §10、§11 是旧 Rust/GTK 线（§6 的 slang 二进制、§9 的 Rust crate 清单两条 lane 共用）。

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

### 12.2 构建命令与产物

```sh
bash scripts/build-qt.sh              # 镜像 → configure → build → ctest → xcb/软件渲染冒烟
bash scripts/build-qt.sh --no-test    # 只 configure + build（即 C++ 静态检查）
```

与 §2 相同的一条命令；本节列出产物路径、运行期依赖与证据。

**产物路径：`target/qt6-build/src/app/vedit-app`**（CMake 构建树 `target/qt6-build/` 已被 gitignore）。
`bash scripts/build.sh` 是旧 Rust/GTK3 产品，只写 `target/release/vd-app`，不产出 `vedit-app`。
运行期依赖（KF6/Qt6 运行库、`libtree-sitter0.25`、Qt 平台插件 `libqxcb`）的实测包名见
`evidence/runtime-deps/REPORT.md` §1，运行期打包边界见本文件 §12.6。

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
  T11（m0-runtime）；本节的产物是可执行的冒烟程序，**不是交付物**（交付物是 §7.1 的 AppImage，
  由 `packaging/runtime/build.sh` 产出）。
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
- **SystemVerilog 定义**：构建镜像里 `definitionForName("SystemVerilog")` 可加载（来自 KDE 内嵌
  定义），冒烟**只报告不断言**；交付物随包的是我们自己的 5 份 `resources/syntax/vedit-*.xml`
  （白名单由 §7.1 的 F2 形态断言），不是 KDE 内嵌定义。
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

## 13. slang-server（LSP 后端二）：spike 结论与构建/运行方式

`slang-server` **不随 slang 发布**，是第三方项目
[`hudson-trading/slang-server`](https://github.com/hudson-trading/slang-server)（基于 slang 库的
SystemVerilog LSP）。本节记录 AC-1.5 / AC-3.0 spike 的结论：**上游预编译产物不能在 CentOS 7 运行，
源码构建可以**。原始日志与结论见 `evidence/w2-spike/`。

### 13.1 上游预编译：不可用（实测）

v0.3.1 的 `slang-server-linux-x64.tar.gz`（sha256 `5162701e…`，`slang-server-old-linux-x64-gcc` 与其
字节相同）在干净的 `centos:7`（CentOS 7.9.2009，glibc 2.17）容器里启动即失败：

```text
/w/slang-server: /lib64/libm.so.6: version `GLIBC_2.27' not found (required by /w/slang-server)
/w/slang-server: /lib64/libc.so.6: version `GLIBC_2.18' not found (required by /w/slang-server)
/w/slang-server: /lib64/libc.so.6: version `GLIBC_2.25' not found (required by /w/slang-server)
```

证据：`evidence/w2-spike/02-prebuilt-centos7-run.txt`（`objdump -T` 的最高符号版本也是 2.27）。

### 13.2 源码构建：一条命令

```bash
bash probes/slang-spike/run-spike.sh
```

它做四件事：源码构建 → 干净 `centos:7` 里 `--version` → 真实 `initialize` 握手（客户端在宿主，
server 在容器内）→ fixture 上的层次化 elaboration（实例路径 + signal driver/load，含耗时与峰值内存）。
中间产物在 `target/tmp/spike-work/`，证据写 `evidence/w2-spike/`；需要 docker 与网络。

构建配方与仓库既有的 slang 构建（§6）同一条路线，脚本
`probes/slang-spike/build-sluserver-centos7.sh`：

- CentOS 7.9.2009 + tuna vault 镜像（含 SCLo）；`devtoolset-11`（gcc 11.2.1，CentOS 7 可用的最新编译器）；
- CMake 3.31.6（Kitware 预编译，跑在 glibc 2.17 上）与 CPython 3.12.8（slang 代码生成需要 ≥ 3.9），
  两者都按 sha256 校验，缓存放在 `target/tmp/spike-work/cache/`；
- 子模块 `external/slang`（AndrewNolte fork）、`external/reflect-cpp`、`external/ctre`；
- `-static-libstdc++ -static-libgcc`，产物只依赖 `libm/libpthread/libc`，最高 GLIBC 符号 **2.17**；
- **GCC 11.2 兼容**：libstdc++ 在 11.3 之前没有 `std::hash<std::filesystem::path>`，slang 的
  `hash<T>` fallback 会编译失败。仓库对 slang 12.1 用的是源码补丁
  `build/slang/patches/0001-gcc11-libstdcxx-path-hash.patch`；这里对 slang-server 改用
  `-include probes/slang-spike/gcc11_path_hash.h`，子模块保持原样。

### 13.3 实测结果（2026-11-02，16 核）

| 项目 | 观察值 | 证据 |
| --- | --- | --- |
| 构建耗时 | 440 s（yum 15 s + CPython ~90 s + clone + configure + compile/link） | `evidence/w2-spike/03f-build-timings.txt` |
| 产物 | `sha256 f691600f…`，11.7 MB，最高 `GLIBC_2.17` | `evidence/w2-spike/03e-build-artifact.txt` |
| 干净 centos:7 `--version` | `slang-server version 0.3.1+2ad996f`，exit 0 | `evidence/w2-spike/03-built-centos7-version.txt` |
| 真实 `initialize` 握手（server 在容器内） | 0.189 s，`serverInfo=0.3.1+2ad996f`，15 组 capability | `evidence/w2-spike/04-built-initialize-handshake.txt` |
| fixture elaboration（CLI `slang --ast-json`） | 4 ms，峰值 RSS 19.0 MB | `evidence/w2-spike/05-elaboration.json` |
| fixture elaboration（server，`slang.setBuildFile`） | 2 ms，峰值 RSS 25.6 MB（`/proc/<pid>/status` 的 VmHWM） | `evidence/w2-spike/06-hierarchy-lsp-shape.txt` |

spike 的 kill 判据是 **>30 s 或 >1 GiB**：两个引擎都远低于该门槛。

### 13.4 运行要点（给接入方）

- **cwd 必须是工程根，`rootUri` 必须指向它**：`slang-server` 以工作目录为基准解析文件路径；
  `initialize` 的 `rootUri` 还必须在该进程可见的文件系统里存在（容器里跑时把工程挂到 `rootUri` 路径，
  否则 initialize 直接报 `cannot set current path`）。
- **`.f` 的位置无关，`.f` 里的条目相对工程根最稳**：文件本身放哪都行（工程外、绝对路径都实测正确），
  条目写相对工程根（或绝对路径）在 cwd=工程根时都能解析。**真正决定 `location.uri` 是否正确的是 cwd**：
  cwd 不是工程根时，`getDrivers/GetLoads` 返回的 `location.uri` 会按错误的基准解析到工程根的兄弟/上一级
  （模块树看起来完全正常，双击却跳到另一个文件）。四个组合的实测见
  `evidence/w2-spike/07-buildfile-rule.txt`（`probes/slang-spike/buildfile_rule_probe.py`）。
- 层次/驱动命令：`slang.getScopes`、`slang.getScope`、`slang.getScopesByModule`、
  `slang.getInstancesOfModule`、`slang.searchHierarchy`、`slang.getDriversWithLocation`、
  `slang.getLoadsWithLocation`、`slang.setTopLevel`、`slang.setBuildFile`；响应形状见
  `evidence/w2-spike/06-hierarchy-lsp-shape.txt`。
