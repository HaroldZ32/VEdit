# Third-party notices / 第三方组件与许可

**VEdit 自身以 MIT 许可发布**（根目录 `LICENSE`，Copyright (c) 2026 Harold Zhao）。
下面这些第三方组件随产物一起分发（AppImage、tar.gz、`.run` 三者相同），并**各自保留其许可**——
MIT 只覆盖本项目自己的代码，不改变任何第三方组件的许可条款。
本文件是**分发用的许可声明**：每个组件的版本、许可、来源与验证方式。
许可全文见 [`docs/licenses/`](docs/licenses/)，Rust crate 清单见 [`docs/rust-crates.txt`](docs/rust-crates.txt)（生成物）。

> **两条产品线的范围说明（归属更正，2026-10-31 · AMENDMENTS rev 2 / C15）**：
> **§1–§8 描述旧 Rust/GTK3 产物（legacy）**；**§9 描述当前 Qt6 产品线**的自有基础语法数据
> （两份 LGPL-2.1-or-later XML）与 MIT `tree-sitter-verilog` grammar。§9 与 §1–§8 的组件
> **不可互相替代**，也不得把 §9 的两份 XML 归到 GtkSourceView 或 KDE 名下。
> 权威副本与登记口径见 [`docs/handover/appendix/amendments-rev2.md`](docs/handover/appendix/amendments-rev2.md) §2。

## 1. 组件清单

| 组件 | 版本 | 许可 | 来源（固定方式） | 分发形态 |
|---|---|---|---|---|
| **Verible**（`verible-verilog-ls/-lint/-format`） | `v0.0-4298-g085eef05` | **Apache-2.0** | `VERIBLE_URL` + tarball sha256 + 三个二进制各自 sha256（`scripts/versions.env`） | `bin/verible-verilog-*`（上游静态二进制，未修改） |
| **slang**（深度 lint） | 12.1（tag `v12.1`, commit `42710c78…`） | **MIT** | 本项目从源码构建；产物 sha256 = `SLANG_BIN_SHA256` | `bin/slang`（静态 libstdc++，glibc ≤ 2.17） |
| **GtkSourceView**（`libgtksourceview-4.so.0` + 163 个 `.lang` + 主题） | 4.6.1 | **LGPL-2.1-or-later** | `GSV_URL` + sha256（`scripts/versions.env`），从源码构建到 `/opt/gsv4` | `lib/libgtksourceview-4.so.0`、`share/gtksourceview-4/` |
| **GTK3** | 3.22.30-8.el7_9 | LGPLv2+ | CentOS 7.9.2009 vault rpm，pinned 镜像 | `lib/libgtk-3.so.0` 等 |
| GLib / GIO / GObject | 2.56.1-9.el7_9 | LGPLv2+ | 同上 | `lib/libglib-2.0.so.0` 等 |
| Pango | 1.42.4-4.el7_7 | LGPLv2+ | 同上 | `lib/libpango-1.0.so.0` 等 |
| ATK / at-spi2 | 2.28.1-2.el7 | LGPLv2+ | 同上 | `lib/libatk-1.0.so.0` 等 |
| cairo | 1.15.12-4.el7 | LGPLv2 or MPLv1.1 | 同上 | `lib/libcairo.so.2` 等 |
| gdk-pixbuf（+ loaders） | 2.36.12-3.el7 | LGPLv2+ | 同上 | `lib/libgdk_pixbuf-2.0.so.0`、`lib/gdk-pixbuf-2.0/…/loaders/` |
| **gtk3-immodule-xim** | 3.22.30-8.el7_9 | LGPLv2+ | 同上 | `lib/gtk-3.0/3.0.0/immodules/im-xim.so` |
| **ibus-gtk3** | 1.5.17-14.el7_9 | LGPLv2+ | 同上 | `lib/gtk-3.0/3.0.0/immodules/im-ibus.so` |
| **WenQuanYi Zen Hei** | 0.9.46-11.el7 | **GPLv2 with font-embedding exception** | `wqy-zenhei-fonts` rpm（CentOS 7 vault） | `share/fonts/wqy-zenhei/wqy-zenhei.ttc` |
| DejaVu Sans / Sans Mono | 2.33-6.el7 | Bitstream Vera + Public Domain（含 Arev 部分） | `dejavu-*-fonts` rpm | `share/fonts/dejavu/*.ttf` |
| Adwaita icon theme | 3.28.0-1.el7 | LGPLv3+ 或 CC-BY-SA | `adwaita-icon-theme` rpm | `share/icons/Adwaita/`（16–48px） |
| HighContrast / HighContrastInverse themes | 3.28.0-1.el7 | LGPLv3+ | 同上 | `share/themes/` |
| **Rust crates** | 见 `docs/rust-crates.txt` | 逐 crate（以 MIT / Apache-2.0 为主，另有 Unicode-3.0、ISC、BSD-3-Clause、LGPL-2.1-or-later 等） | `Cargo.lock`（生成清单） | 静态链入 `bin/vd-app` |

> **包名纠正**：GTK3 的输入法模块来自 **`gtk3-immodule-xim`**（`im-xim.so`）与
> **`ibus-gtk3`**（`im-ibus.so`）。**不存在 `gtk3-immodule-ibus` 这个包**，请勿据此排查。

> **emoji 字体未捆绑**：出于体积与许可考虑，产物不含 emoji 字体；emoji 会由系统字体回退渲染，
> 目标机没有 emoji 字体时显示为占位符。这是已知限制，不是缺陷。

## 2. LGPL 组件的源码获取（source availability）

GTK3 栈与 GtkSourceView 都是 **LGPL**（GtkSourceView 为 LGPL-2.1-or-later，GTK3 为 LGPLv2+）。
按 LGPL 的要求，分发时须能获得**对应的完整源码**：

- **GtkSourceView 4.6.1**：
  `https://download.gnome.org/sources/gtksourceview/4.6/gtksourceview-4.6.1.tar.xz`
  sha256 `63f487a7ab59612ffa9e7be1883dbd274ab1016afcede3239b93b0fc384df874`
  （未修改；构建参数见 `docs/BUILD.md` 第 5 节）。
- **GTK3 3.22.30 及其依赖（GLib/Pango/ATK/cairo/gdk-pixbuf）**：CentOS 7.9.2009 的官方源码 RPM，
  来自本项目固定的 vault 镜像 `.../centos-vault/7.9.2009/`（`os/Source/SPackages/`、`updates/Source/SPackages/`）；
  构建镜像的 rpm 快照见 `scripts/docker/rpm-baseline.txt`（382 个包，含精确版本）。
- 本仓库的构建脚本正是**从上述固定输入**获取并构建/打包这些组件
  （`scripts/build.sh`、`packaging/appimage/run-evidence.sh`、`packaging/tarball/build.sh`），
  因此源码与二进制之间的对应关系是可复现的。
- **再分发者请注意**：若你把这三个产物再次分发给第三方，LGPL 要求你同样提供（或书面提供）上述源码。
  本项目提供的获取方式即本节的两个来源 + `scripts/versions.env` 中的固定版本与 sha256。

## 3. Apache-2.0 组件（Verible）

- Verible 的发布 tarball（`VERIBLE_URL`）**不包含 `LICENSE` / `NOTICE` 文件**（已核对 tarball 内容），
  因此没有需要原样转发的 NOTICE 段落；Apache-2.0 正文见 `docs/licenses/Apache-2.0.txt`。
- 分发的是**未修改的上游静态二进制**，按 sha256 固定（见 `docs/BUILD.md` 第 3 节与
  `packaging/tarball/evidence/02-verible-sha256.txt`）。

## 4. GPLv2 + 字体嵌入例外（WenQuanYi Zen Hei）

- CentOS 包元数据：`wqy-zenhei-fonts-0.9.46-11.el7` → `License: GPLv2 with exceptions`；
- 上游 README 明确为 "GPL v2 (with font embedding exception)"，例外条款原文与来源见
  [`docs/licenses/WenQuanYi-EXCEPTION.txt`](docs/licenses/WenQuanYi-EXCEPTION.txt)；
- 基础许可正文：`docs/licenses/GPL-2.0.txt`；
- 捆绑文件：`share/fonts/wqy-zenhei/wqy-zenhei.ttc`，
  sha256 `38ed4249f9a655c15cb0f4574336e23db46b569f867fb35d0ac7f7b8d83ad623`。
- 该字体**未修改**；嵌入文档的许可影响由上述例外条款排除。

## 5. 字体（DejaVu）

DejaVu 字体为 Bitstream Vera 许可 + DejaVu 修改部分进入公有领域（另有 Arev 字形的第三方版权）。
许可全文见 [`docs/licenses/DejaVu-LICENSE.txt`](docs/licenses/DejaVu-LICENSE.txt)。

## 6. Rust crates

`docs/rust-crates.txt` 由 `cargo metadata`（数据源 `Cargo.lock`）生成，共 **192** 个 crate，
逐项列出 name / version / license / repository。许可分布（生成时统计）：

- `MIT OR Apache-2.0` 101；`MIT` 37；`Unicode-3.0` 18；`Apache-2.0 OR MIT` 9；
  `MIT/Apache-2.0` 5；`Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT` 3；
  `ISC` 2；`LGPL-2.1-or-later` 2；其余单项若干（Unlicense OR MIT、BSD-3-Clause、CDLA-Permissive-2.0 等）。
- 其中 7 个 `vd-*` 是**本项目自有 crate**，许可随项目根 `LICENSE`。
- **TLS 纪律**：HTTP 栈用 **rustls**，依赖树中**没有** `openssl` / `native-tls`
  （由 `scripts/gates.sh` 的 lock 策略门禁强制），因此产物不链接系统 OpenSSL。

## 7. 许可全文

| 文件 | 内容 |
|---|---|
| [`docs/licenses/LGPL-2.1.txt`](docs/licenses/LGPL-2.1.txt) | GNU LGPL 2.1（GtkSourceView、GTK3 栈） |
| [`docs/licenses/Apache-2.0.txt`](docs/licenses/Apache-2.0.txt) | Apache License 2.0（Verible） |
| [`docs/licenses/slang-LICENSE.txt`](docs/licenses/slang-LICENSE.txt) | MIT（slang，上游原样） |
| [`docs/licenses/GPL-2.0.txt`](docs/licenses/GPL-2.0.txt) | GNU GPL 2（WenQuanYi 基础许可） |
| [`docs/licenses/WenQuanYi-EXCEPTION.txt`](docs/licenses/WenQuanYi-EXCEPTION.txt) | 字体嵌入例外（原文与来源） |
| [`docs/licenses/DejaVu-LICENSE.txt`](docs/licenses/DejaVu-LICENSE.txt) | Bitstream Vera / DejaVu |

## 8. 自证方式（可复现）

```sh
# 产物内所有 ELF 的最高 GLIBC 符号版本（应 ≤ 2.17），以及 RUNPATH 白名单
sh packaging/tarball/glibc-gate.sh <payload-dir> packaging/tarball/rpath-allowlist.txt /dev/stdout

# 三个 verible 二进制与 slang 的 sha256 是否等于 pin
grep -E 'VERIBLE_(LS|LINT|FORMAT)_SHA256|SLANG_BIN_SHA256' scripts/versions.env
sha256sum <payload>/bin/verible-verilog-ls <payload>/bin/verible-verilog-lint \
          <payload>/bin/verible-verilog-format <payload>/bin/slang
```

证据位置：`packaging/tarball/evidence/`（载荷清单、二进制身份、字体、许可相关日志）与
`evidence/appimage/`（AppImage 侧的同类证据）。

## 9. Qt6 产品线：自有语法定义与 tree-sitter grammar（归属更正）

本节按需求 C11/C14 登记**当前 Qt6 产品线**随包的三项组件。**身份核对**（路径 / 行数 / 字节 /
sha256）的原始输出见 [`evidence/docs-amendments/02-sha256-check.txt`](evidence/docs-amendments/02-sha256-check.txt)；
更详细的登记口径见 [`docs/handover/appendix/amendments-rev2.md`](docs/handover/appendix/amendments-rev2.md) §2。

### 9.1 自有基础语法定义（两份，LGPL-2.1-or-later）

| 项 | `vedit-verilog.xml` | `vedit-systemverilog.xml` |
|---|---|---|
| 语言名 | `Verilog` | `SystemVerilog` |
| 许可 | **LGPL-2.1-or-later** | **LGPL-2.1-or-later** |
| 仓库路径 | `resources/syntax/vedit-verilog.xml` | `resources/syntax/vedit-systemverilog.xml` |
| 行数 / 字节 | 85 / 5,111 B | 90 / 5,492 B |
| **sha256** | `c6adc763c7b280d7493f48d06e5779d0b1e2068ac82f75fe7f31a07b0788a6c2` | `2abba3342d0878e71b99d358d96e510f857cd7f232786951be8602f513e0b992` |
| 安装落点（AppDir） | `usr/share/org.kde.syntax-highlighting/syntax/vedit-verilog.xml` | `usr/share/org.kde.syntax-highlighting/syntax/vedit-systemverilog.xml` |
| 许可全文 | `docs/licenses/LGPL-2.1.txt`（亦随包 `resources/syntax/LICENSE-LGPL-2.1`，26,419 B） | 同左 |
| 上游功能参照（非复制） | GtkSourceView 4.6.1 `verilog.lang`（475 行，sha256 `7670ddfd7b443f5f99c202bfa79de31834c1bd8968ed74c39474dca4f439d464`） | GtkSourceView 4.6.1 `systemverilog.lang`（262 行，sha256 `9d1f67411f2674d93f035620ce004da4beca5bbf469eb9955a83c332edb79b04`） |
| 上游 tarball pin | `GSV_SHA256=63f487a7ab59612ffa9e7be1883dbd274ab1016afcede3239b93b0fc384df874`（`scripts/versions.env:24-26`） | 同左 |

> **哈希变更记录（2026-10-31，T45）**：T45 把两份 XML 的折叠规则改写为 KDE 关键字列表惯用法
> （`<list>` + `<keyword String="列表名" beginRegion/endRegion>`）后，行数 / 字节 / sha256 变为上表值；
> 旧的 78 / 5,172 B `3d1ec41f…`、89 / 6,276 B `dc6e0a47…` 是 **T45 重写前的历史值**，只保留在旧证据与
> 旧打包快照里（`evidence/highlight-v2/10-licensing-and-embargo.txt`、
> `evidence/m0-runtime/runtime-manifest.txt`、`evidence/docs-amendments/02-sha256-check.txt`），不改写。
> 许可结论不变：两份均为自有 LGPL-2.1-or-later 构建产物。实算与核对见
> `evidence/licensing-hash-sync/01-hash-recompute.txt`。

**归属更正**：

- 这两份 XML 是**本仓库自有的构建产物**（XML 头 `author="VEdit"`、
  `license="LGPL-2.1-or-later"`），**不是** GtkSourceView 的运行期文件，**也不是** KDE 定义；
  GtkSourceView 的 `.lang` 只作功能参照与交叉核对（见 `resources/syntax/README.md`）。
- **不得**把它们登记在"第三方 GTK/GtkSourceView 运行期组件"条目下，**不得**归因给 KDE；
  KDE 的 `verilog.xml`（许可未声明，sha256 `7199e367…`）与 `systemverilog.xml`
  （GPL-3.0-or-later，sha256 `3361d8b4…`）**永不随包**（sha256 + 文件名 + 内嵌扫描三重禁运）。
- LGPL 义务（许可声明 + 源码可得）：**XML 本身即源码**，随包安装于上述落点；仓库内文件可修改。

### 9.2 MIT `tree-sitter-verilog`（静态编入）

| 项 | 值 |
|---|---|
| 上游仓库 / commit | `https://github.com/tree-sitter/tree-sitter-verilog` / **`4457145e795b363f072463e697dfe2f6973c9a52`**（2022-09-07） |
| 许可 | **MIT**，© 2018-2021 Aliaksei Chapyzhenka；`LICENSE` 全文随包（`third_party/tree-sitter/verilog/LICENSE`，1,082 B，sha256 `92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46`） |
| 仓库路径 / provenance | `third_party/tree-sitter/verilog/`；`third_party/tree-sitter/PROVENANCE.md` |
| 随包文件与 sha256 | `grammar.js`（133,684 B）`323c121a3e550df0f970d46c544b3f02485e5e6d754acb730d6be7b8dea7dc4b`；`src/parser.c`（46,042,888 B）`a15f78b82068daeb62f3b54c8ad0101aa936a2c921a57806fe8f37dcbf0bfff2`；`src/tree_sitter/parser.h`（5,378 B）`ab104936984904469572a4e868149f7a22fb2929347f837ae6a1f9b790f1b173`；`src/node-types.json`（271,904 B）`e99b42baf4279d3be6dec59129dda0789f196b4e5c99144a781ea6cdf255425c` |
| 分发形态 | 静态编入 `libvedit_syntax_grammars`（`verilog/src/parser.c`）；无运行期下载 / `.so` 加载 / 动态链接；`-DVEDIT_GRAMMAR_*` 可整体关闭 |
| MIT 义务 | 保留版权行 + 许可全文 + 锁定 commit（已满足）；上游无 NOTICE 段落需转发 |

### 9.3 范围与复核

- 本节只登记 C11/C14 要求的三项。Qt6/KF6 运行期组件（Qt6、KTextEditor、KSyntaxHighlighting、
  Verible、slang、Rust 服务层 crate）的完整清单由打包收口（`pack-final`）补齐；在补齐前，
  §1–§8 的 legacy 条目**不覆盖** Qt6 产物。
- 最终产物一致性（AppDir 内每份文件可按 sha256 找到）由 `pack-final` 的产物侧清单复核；
  本节只固定仓库内身份。
