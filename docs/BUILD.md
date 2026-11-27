# 构建 VEdit

本文说明 Qt6 产品的构建、打包与 CI（§2、§7.1、§11、§12），以及旧 Rust/GTK3 产品的构建与打包
（§2.1、§3–§5、§7.2、§8、§10）。两条构建路径共用 §6 和 §9 的工具与许可说明。

> **当前发布产品为 Qt6 `vedit-app`。** 先按 §2 构建，再按 §7.1 打成 AppImage、`.tar.gz` 和 `.run`，
> 三种格式共用 `packaging/runtime/out/release/SHA256SUMS`。构建底座见 §12，发布流程见[发布指南](RELEASING.md)。
>
> 旧 Rust/GTK3 `vd-app` 的构建在 §2.1，历史打包流程在 §7.2。§6 的 slang 二进制、§9 的 Rust crate 清单
> 可供两种构建共用。§10.1 说明 Qt 产品运行时如何定位工具。

## 1. 前提

- **Docker**（daemon 可用即可；rootless 也可，`scripts/lib.sh` 有说明）。
- 宿主操作系统不限（macOS/Linux 均可），真正的构建发生在**摘要固定**的镜像里：
  Qt6 交付物用 **Ubuntu 26.04** 镜像（§12.1），旧 GTK3 线用
  `docker.m.daocloud.io/library/centos:7`（§2.1）。
- 构建期需要联网以获取固定版本的镜像和依赖。编辑、随包 HDL 分析和预热的 Scala 示例可离线运行。
  可选的 FIM 服务、远端版本控制和未预热的 Scala 依赖按各自配置使用网络。
- 安排磁盘空间时同时计入 Docker 镜像与缓存、Cargo/CMake 构建树、解包的 Scala 工具链、打包暂存和三份压缩产物。

## 2. 构建 Qt6 产品

```sh
bash scripts/fetch-verible.sh
bash build/slang/build.sh
bash scripts/build-qt.sh          # 固定镜像 → Rust 服务层 → CMake configure/build/ctest → xcb/软件渲染冒烟
```

二进制在 `target/qt6-build/src/app/vedit-app`。Rust 服务构建需要固定版本的 `build/slang/out/slang`，
包括使用 Qt 的 `--no-test` 时。生成发布包的步骤见 §7.1。

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

### 2.2 Rust 服务层：`cargo +stable` 与 `CARGO_HOME`

Rust 服务层（`crates/vd-*`）的门禁由 `scripts/gates.sh` 执行；它调用普通 `cargo`，在宿主缺 GTK
开发包时自动放进 pinned 镜像（`scripts/lib.sh:run_in_build_container`，镜像内
`CARGO_HOME=/vedit-build/target/cargo-home`）。**在宿主上直接跑 Rust 命令时**，用这一对：

```sh
export CARGO_HOME=$PWD/target/cargo-home
cargo +stable test -p vd-config -p vd-ffi -p vd-deeplint
cargo +stable clippy --workspace --all-targets -- -D warnings
cargo +stable fmt -p vd-ffi -- --check
```

为什么必须这样（实测，2026-11-12）：

```text
$ cargo --version
error: the 'cargo' binary, normally provided by the 'cargo' component, is not
applicable to the '1.99.0-x86_64-unknown-linux-gnu' toolchain
$ CARGO_HOME=$PWD/target/cargo-home cargo +stable --version
cargo 1.99.0 (5f94df478 2026-08-27)
```

- pinned 的 1.99.0 工具链**没有 cargo 组件**（`rustc 1.99.0` 在，`cargo` 不在），裸 `cargo` 会直接报错；
  `+stable` 解析到带 cargo 组件的 stable 工具链（实测 `cargo 1.99.0`，与 pin 同版本），命令照常执行。
- 宿主 `$HOME/.cargo` 是**只读**的：cargo 的 registry/git 缓存必须落到工作区内的
  `target/cargo-home`，不设置 `CARGO_HOME` 的宿主构建会因写缓存失败而失败。
- 这两条是**环境事实**，不是代码缺陷；`cargo check -p vd-app` 在宿主还会死在 GTK 的 pkg-config，
  产品构建与 ctest 走 pinned 镜像（见 §12.5）。

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

### 7.1 Qt 交付物

在 Linux x86_64 上完成 §2 后，从仓库根目录准备打包输入并构建：

```sh
bash scripts/build-ksh-selfbuild.sh --form F2
docker run --rm -v "$PWD:/vedit-build" -w /vedit-build vedit-qt6-build:local \
  bash packaging/runtime/build-vhdl-payload.sh
ROOT="$PWD" bash packaging/runtime/build-scala-payload.sh
bash packaging/runtime/build.sh
```

Actions 从对应构建任务取得 F2、Qt6 和预热的 Scala 输入。它在打包任务中构建 VHDL 输入。

从同一个暂存 AppDir 生成三种格式。发布文件在 `packaging/runtime/out/release/`：

- `VEdit-<ver>-x86_64.AppImage`，支持 FUSE 挂载和解包运行。
- `vedit-<ver>-linux-x86_64.tar.gz`，解包后运行顶层目录中的 `AppRun`。
- `VEdit-<ver>-x86_64.run`，临时解包后启动，也支持 `--extract DIR`。
- `SHA256SUMS`，包含上述三个包各一条 SHA-256 记录。

三个包均包含私有 loader、glibc、Qt6、KF6、KTextEditor、自建 KSyntaxHighlighting、tree-sitter、字体
和中文翻译（`bin/translations/vedit_zh_CN.qm`、`qtbase_zh_CN.qm`）。打包检查包内工具、许可和语法定义的
隔离边界，并验证实际包的解包与启动。按[打包说明](../packaging/runtime/README.md)检查交付字节，暂存 AppDir
是构建中间产物。如需复查用户安装命令，见[用户手册](USER_GUIDE.md#附录-a-产物形态与安装)。

**版本号**为 **0.5.0**。`Cargo.toml` 的工作区版本必须与 `Cargo.lock`、根目录和终端的 CMake 项目版本、
应用和窗口冒烟程序的版本一致。打包从工作区版本派生文件名和桌面文件的 `X-AppImage-Version`。
应用的 About 与 `--version` 应显示同一版本。

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
- 体积不同：旧管线 47,745,528 字节 vs Qt 交付物 137,079,288 字节（0.1.1 时期同机同日实测；0.2.1 重建后以实际文件为准）；
- 旧管线的干净容器验收仍从宿主 glibc 家族解析 **8** 个库
  （`evidence/appimage/clean-room.log`：`from host glibc family: 8`），而 Qt 交付物是 F2 私有运行时。

旧管线自身的断言（pinned 输入 + sha256、GLIBC ≤ 2.17 全量断言、RPATH/RUNPATH 白名单）仍然有效，
但它们证明的是**旧产物**的性质，不能用来证明 Qt 交付物。

### 7.3 交付物随包的外部工具

三个 Qt6 发布包除 Qt/KF6/KTextEditor/KSyntaxHighlighting/tree-sitter、字体和中文翻译外，还提供下列
**外部工具**。每个都由 `packaging/runtime/assemble-appdir.sh` 安装；组装的必检清单（`REQUIRED_*`
与自检）缺少条目时**硬失败**，不会悄悄产出一个缺工具的包。版本台账见
`scripts/versions.env`、`scripts/versions-qt.env`、`scripts/versions-scala.env` 与
`packaging/runtime/out/*-payload/PAYLOAD.env`。

| 工具 | 版本（实测） | 用途 | 证据 |
|---|---|---|---|
| `verible-verilog-ls` / `verible-verilog-format` / `verible-verilog-lint` | `v0.0-4298-g085eef05` | Verilog LSP / 格式化 / lint | §3 |
| `slang` | v12.1（CentOS 7 源码构建） | 模块树、deep lint、工程级 rename 的 AST | §6 |
| `slang-server` | 0.3.1+2ad996f（源码构建） | 第二个 LSP 后端 | §13 |
| GNU `diff` | 3.3（CentOS 7 `diffutils-3.3-5.el7`） | 双栏文件和 SVN 快照比较 | `evidence/workbench-upgrade/release/preflight/` |
| `vhdl_ls` | 0.88.0 | VHDL LSP | `evidence/batch-20261110c/vhdl/01-lsp.log` |
| `ghdl` | 6.0.0（mcode） | VHDL 分析（合并 lint；VSG 格式化的语法门） | `evidence/batch-20261110c/vhdl/03-merged-lint.log` |
| `vsg` + 随包 CPython | 3.36.0（Python 3.12.8） | VHDL 风格 lint 与 `--fix` 格式化 | `evidence/batch-20261110c/vhdl/04-format.log` |
| JDK（Temurin） | 21.0.13+9 | Scala 运行 / Metals | `scripts/versions-scala.env` |
| `scala-cli` | 1.18.0（static native） | Scala 运行 lane | `evidence/batch-20261110c/scala/01-run.log` |
| `sbt` | 1.13.1 | sbt 工程运行 | `scripts/versions-scala.env` |
| `metals` / `cs`（coursier） | 1.6.9 / 2.1.26 | Metals 启动器与离线解析 | `scripts/versions-scala.env` |
| Bloop | 1.5.18（sbt）/ 2.1.2（scala-cli BSP） | Metals 的构建服务器 | `evidence/batch-20261110c/scala/metals-matrix.md` |
| 预置缓存 | SpinalHDL 1.15.1 / Scala 2.13.12；Chisel 7.16.0 / Scala 2.13.18 + firtool | 两个示例的离线构建 | `evidence/batch-20261110c/scala/03-cache-manifest.txt`、`scala-payload/` |

上述 HDL / Scala 工具的运行时解析顺序为 **bundled → local checkout → PATH → env**（工具各自的 env 名见 §10.1、
`docs/USER_GUIDE.md` 附录 B）；工具缺失时产品给**一条**可操作的状态行，不按文件重复报错。
文件比较优先使用包内的私有 GNU `diff`，源码构建从 `PATH` 查找 GNU diffutils。

集成终端和版本控制使用宿主机上已安装的 shell。Git 和 SVN 客户端也由宿主机提供。打包门禁检查 AppDir 文件，并在空 `PATH` 下启动实际产品。真实 SVN 测试可用 `VEDIT_SVN_BIN` 和 `VEDIT_SVNADMIN` 指向测试环境中已安装的客户端和夹具工具。

### 7.4 运行期离线验证

发布流水线从本次 AppImage 解包，在 `docker run --network none` 的 CentOS 7 环境中检查工具版本、
运行 C ABI 自检并生成两个 Scala 示例的 RTL。下面是此前批次用只读挂载与 `strace` 采集的观测：

- 两个 Scala 示例（`bin/scala-example spinalhdl|chisel`）：只读挂载 + `--network none` 下
  `EXIT=0`，分别产出 `Counter.v` / `Counter.sv`；两套预置 home 互不污染（污染计数为 0）。
  证据：`evidence/batch-20261110c/scala-payload/01-appimage-offline.log`。
- Metals 出厂 wrapper（`bin/metals`）：真实 LSP 会话在只读 AppDir + `--network none` 下
  `initialized=True`、BSP 连接 7.25 s、编译 2.9 s、6 次 `publishDiagnostics`；
  `Error downloading=0`、`Read-only file system=0`、**外部 TCP 443/80 = 0**。
  证据：`evidence/batch-20261110c/scala-metals-launch/01-launch-session.log`。
- **诚实边界**：`strace` 仍记录到 DNS（UDP 53）尝试，全部 `ENETUNREACH`（scala-cli 的
  `api.github.com` 提交探测与 JVM 的本机名查询）。因此本仓库**不声称“零系统调用”**；
  要真正零尝试需要 BSP 层的 `--offline`，该 argv 由 Metals 的 scala-cli 集成生成，不归本仓库控制。
  矩阵与审计见 `evidence/batch-20261110c/scala/05-network-audit.md`、
  `evidence/batch-20261110c/scala/metals-matrix.md` §A–§D。

### 7.5 C ABI 的加法增长与重建要求（以 `VdRenameOptions` 为例）

重命名的 `-I` / `-D` 是通过**在结构体末尾追加字段**接入的（T72）：`VdRenameOptions` 尾部新增
`struct VdStringList include_dirs` 与 `defines`，原有字段一个也没有移动。仓库的演进规则写在
`crates/vd-ffi/src/lib.rs` 的 `VD_ABI_VERSION` 注释里：

- `VD_ABI_VERSION` 在**已导出签名的含义变化**时增加，**也**在**由提供方填充的结构体增长**时增加：
  追加字段保住的是每个既有**偏移**，不是 `sizeof`，而 `vd_*_poll` 写的是整个当前结构体
  （G1-c 的 `vd_catalog_poll` 写 112 字节；旧头消费者只分配 88 字节 → 越界写 24 字节）。
  `VdModuleCatalog` 的扩展因此把版本从 1 升到 **2**。版本 **3** 为 `VdVcsFileStat` 增加了 SVN 路径操作、节点类型和复制来源。数组中相邻条目之间的字节数随之改变，读取修订详情的 C/C++ 调用方需要使用新版头文件重新编译。
- 结构体**按指针传递**且 Rust 端**无条件读取**追加字段（`crates/vd-ffi/src/rename.rs` 的
  `read_options`），所以用**旧 header** 编译过的下游 C 调用方**必须重编**，否则会读到旧结构体
  末尾之外；
- header 由 `build.rs`/cbindgen 从声明生成，本仓库的 C++ 与 Rust 永远同树构建，因此同树消费者
  总是匹配；零默认（`VdRenameOptions options{}`）= 空列表 = T72 之前的行为；
- 追加字段的类型与拼写沿用既有 ABI（`VdStringList`，与 `VdLintBatchOptions.defines` 相同），
  没有新增导出函数、没有删除或重排字段。

**重命名的回滚/原子性边界**（用户视角见 `docs/USER_GUIDE.md` §7.1）：每个文件先写临时文件再原子
替换；任一文件失败则按校验和回滚已写文件，并点名失败的文件；预览、`Cancel`、以及 `Apply` 之前都
不写盘。多文件写入是**回滚**而不是文件系统事务（两个文件之间崩溃不会回滚）。

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

### 10.1 运行期工具链发现（Qt 产品：Verilog / VHDL / Scala：随包 / 本地取 / env）

`vedit-app` 的 HDL 与 Scala 功能通过**同一个解析器**查找外部工具
（`src/workspace/diagnostics/toolchain.cpp`），顺序固定为
**bundled → local checkout → PATH → env**，并把结果打印出来：

- Verilog/SystemVerilog：`verible-verilog-ls`（LSP）与 `slang`（deep lint、模块树、
  工程级 rename 的 AST）——`--selfcheck` 的 `TOOLCHAIN source=verible:<source> slang:<source>
  missing=[...] local=[...]`；
- VHDL：`vhdl_ls`、`ghdl`、`vsg`（+ 随包 CPython）——`VHDL-TOOLCHAIN source=vhdl_ls:… ghdl:…
  vsg:… missing=[...]`；
- Scala：`java`、`scala-cli`、`sbt`、`metals`——同一 `TOOLCHAIN` 行尾的 `scala source=…`；

1. **随包（bundled）**：把二进制放进 `vd-app` 同目录的 `bin/`（AppDir/AppImage
   形态；打包后不需要任何额外配置）。
2. **本地取（local checkout，裸构建用这条）**：在源码树里执行
   ```sh
   bash scripts/fetch-verible.sh     # -> target/verible-pin/extract/verible-*/bin/
                                     #    以及 .mpd/recon/verible/bin/ 链接
   bash build/slang/build.sh         # -> build/slang/out/slang
   ```
   解析器从 `target/qt6-build/src/app/vedit-app` 向上找到 checkout 根（标记
   `scripts/fetch-verible.sh`）后自动探测上述产物，**不需要设置任何环境变量**。
3. **env 指定**：`VEDIT_VERIBLE_LS`、`VEDIT_SLANG`、`VEDIT_VHDL_LS`、`VEDIT_GHDL`、
   `VEDIT_VSG`（诊断类工具都支持 `_DIR` 形式指向目录）。
   Scala 的 `java` / `scala-cli` / `sbt` / `metals` 走**同一个解析器**（bundled → local
   checkout → PATH；设置页暂无它们的路径字段），本批次不声称它们接受 `VEDIT_*` 覆盖。

版本控制另用宿主机的有效 `PATH`，找不到时读取 `VEDIT_GIT` / `VEDIT_SVN` 中的绝对路径。诊断行 `VCS-TOOLCHAIN` 记录 `path`、`env` 或 `missing`。

缺诊断工具时，对应面板/状态栏只显示**一条**状态（缺什么、影响什么、三条修复路径），
outline 保持空表 + 一行原因；不会按文件/请求重复报错，也不把“环境未满足”当作文件级 Error。
VHDL 的三条工具缺失文案与 Scala 的随包解析细节见 `docs/USER_GUIDE.md` §9.4 / §9.5，
随包版本台账见本文 §7.3。

## 11. GitHub Actions 与旧 GTK3 镜像复用

| 工作流 | 触发 | 做什么 |
|---|---|---|
| `ci.yml` | PR → `dev`/`main`、push → `dev`、手动 | Rust、Qt6 和文档门禁。目标为 `main` 的 PR 和手动运行增加发布包验证 |
| `image.yml` | 被调用 | 分开构建 Rust、Qt6 和 Scala 输入，打包时验证 Qt6 发布文件 |
| `release.yml` | push `v*` tag、手动 | 构建并验证三种发布包。符合要求的 tag 推送触发 GitHub Release 发布 |

操作步骤和发布条件见[发布指南](RELEASING.md) §6。以下脚本选项用于本地旧 GTK3 构建，允许复用已有镜像：

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
- 旧 `scripts/build.sh` 先在 Xvfb 下运行 `--selftest smoke --probe-hold-ms 2500`，然后运行套件。
  设置 `VEDIT_SLANG` 与 `VEDIT_VERIBLE_LS` 时运行 `all`，否则逐个运行 `smoke highlight config fim`。
  任一套件非零即退出。

## 12. Qt6 / KF6 C++ 构建底座（M0，T10）

> 本节说明 §2 的 Qt6 构建与运行时依赖。§11 说明 Actions 工作流。

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

历史 ELF 检查中，镜像的 glibc 2.43 声明最低内核版本为 **3.2.0**，ISA 要求为 `x86-64-baseline`。
实际的 CentOS 7 原生 3.10 内核运行结果需由对应发布包在物理机或虚拟机上验证。

### 12.2 构建命令与产物

```sh
bash build/slang/build.sh             # 首次构建时生成固定版本的 slang
bash scripts/build-qt.sh              # Rust 构建与测试 → Qt 构建、CTest、Xvfb
bash scripts/build-qt.sh --no-test    # Rust 构建与测试 → Qt 构建
```

Rust 服务层测试需要 `build/slang/out/slang`，`--no-test` 只跳过 Qt 的测试。CI 从固定的 slang 构建任务获取并校验同一产物。

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

### 12.8 ctest 基线与已知红点

- **基线随卡增长，以当时的 `ctest -N` 为准（不是固定数字）**：仓库只冻结过一个历史参照——release
  0.2.1 的 `100% tests passed, 0 tests failed out of 60`
  （`evidence/ux-batch-2/release-0.2.1/gates/qt6-lane.log:1415`）。用例数随 lane 增加：本批次窗口
  实测 63（`evidence/batch-20261110c/gate-fixes/logs/20-window-ctest-full.log`），同一工作树后续
  再实测 **65**（`evidence/batch-20261110c/vhdl/02-config.log:226-231`，16:15 终态树：438 个构建
  目标、`0 failed out of 65`、`CTEST_EXIT=0`）。所以判据是：**先跑 `ctest -N` 记下当时注册数 N，
  再要求 `ctest` 100% 通过且总数等于 N**；60 只是历史参照值，不是当前阈值。示例：注册 65 条 ↔
  T14 终态树（16:15，其他卡的编辑已落定）。
- **已知 flaky（不是产品缺陷），已由 batch-20261114 T1 修测试消除**：`vedit-rm9-status-dots-e2e` 的
  `rm9.tree-group-status-matches-map` / `rm9.panel-status-matches-map` /
  `rm9.panel-renders-status-mark` 曾在同批二进制上偶发失败（隔离复跑 55 次中 6 次，≈11%）。
  根因：测试在 “From filelist” 分组出现后立即断言状态点，而状态点由 400 ms 的 `liveTimer_` 轮询
  发布；分组由 `projectFilesRevision` **同步发布**，但**文件树在自己的 500 ms tick 上消费**（分组与
  状态点一并重算），两条链路各自计时 —— 之前写成「分组是同步的、所以没有差拍」是**半真**的，
  差拍正来自「发布同步、消费不同步」。定性、复跑数据与失败日志：`evidence/batch-20261110c/gate-fixes/README.md`
  §2.3 与 `gate-fixes/logs/`。
  **batch-20261114 T1（契约 §4/§8 A53）已修测试**：三条断言前先等发布出去的
  `vedit.diagnostics.viewRevision` 越过本次 pass 的完成点（`rm9.published-past-pass`），不睡固定时长，
  也不放宽断言；验收 50 连跑 + `taskset -c 0` 负载下 10 跑全绿，
  证据 `evidence/batch-20261114/t1/`。本条不再适用「不得通过改测试消除它」——那是上一批在
  「根因未证实、只观察到 flaky」时的裁定；本批先复现 RED 再定根因在测试，故允许且要求改测试。
- **`vedit-rename-e2e` 的 SIGPIPE**：真缺陷（T134 类：Rust 静态链进 C++ 宿主，Rust 运行时的
  `SIGPIPE -> SIG_IGN` 不执行）；已在用例入口装 `std::signal(SIGPIPE, SIG_IGN)`（只此一处），
  隔离 2/2 与窗口内全量 ctest 全绿。证据同目录 §2.1。
- **`vedit-workspace-vcs-selftest` 的陈旧 e2e 二进制**：全量重建后无代码改动即绿（非产品缺陷）。

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

## 14. MPD gate ids 的两个已知缺陷（记录，不修）

本仓库的 gate 定义映射在 `package.json`（`verify:*` → `build/gates/*.sh`），随仓库一起交付，
但真正的判定走 MPD ledger。有两个 **ledger 侧的已知缺陷**，按需求“不改任何 gate 定义、只记录并
附原因与输出”处理；`manifest` 也不得顶替 `gates` 作为 PASS 证据。

1. **`tests` id 对 Rust/C++ 产品不可达**：ledger 的固定 argv 是 `bun test packages`，而本仓库没有
   `packages/` 目录、也没有任何 `bun test` 套件，所以该 id 必然红。真实的测试入口是
   `build/gates/gates.sh`（ledger 可达的 `gates` id 把一个完整重活跑完：slang ELF 断言 +
   `scripts/gates.sh` 的 Rust 全套 + `build/gates/qt6-cxx.sh` 的 configure/build/ctest/xcb 冒烟）；
   `build/gates/tests.sh` 是**人工入口**，不是 evidence 来源（脚本首行注释即写明）。
   记录出处：`package.json` 的 `description`、`build/gates/tests.sh` 文件头、
   `build/gates/gates.sh` 尾部注释、`evidence/final-acceptance-2/AC-MATRIX.md:35`。
2. **G10：`comments` / `rows` 是 MPD-bundle 的门禁，本产品没有对应实体**：`comments` 依赖 MPD
   bundle 的 comment-checker 二进制；`rows` 检查 MPD ledger 的行纪律。两者在本仓库都**没有**
   可检查的对象，脚本按“诚实红”处理（`no_gate` 打印原因，不伪装绿）。
   记录出处：`build/gates/comments.sh`、`build/gates/rows.sh`、
   `evidence/batch-20261110/shell-cleanup/AUTHORIZATION.md:70`。

这两条都不在本仓库的修复范围（改 gate 定义会破坏 ledger 的冻结口径，也违反“不得改 gate 通关”）；
验收矩阵把它们记为**已记录的已知缺陷/豁免**，而不是 PASS。
