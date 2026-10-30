# 构建 VEdit

本文覆盖：从零构建（干净 CentOS 7 容器内）、固定（pinned）输入、镜像源替换、
GtkSourceView 与 slang 的源码构建、三种产物的打包命令，以及可复现性的边界。

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
  （6 个套件），否则**逐个**跑 `smoke highlight config fim`（CI 只构建 slang、没有 verible，走这条）；
  任一套件非 0 即 `FATAL` 退出。
