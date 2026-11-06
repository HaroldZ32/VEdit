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

#### 9.1.1（A3 / UX 批次 2）新增三份定义：VHDL / Markdown / Tcl

与 9.1 同一条目性质（**本仓库自有**、`author="VEdit"`、`license="LGPL-2.1-or-later"`，
许可全文 `docs/licenses/LGPL-2.1.txt`，随包 `resources/syntax/LICENSE-LGPL-2.1`）：

| 项 | `vedit-vhdl.xml` | `vedit-markdown.xml` | `vedit-tcl.xml` |
|---|---|---|---|
| 语言名 | `VHDL` | `Markdown` | `Tcl` |
| 许可 | **LGPL-2.1-or-later** | **LGPL-2.1-or-later** | **LGPL-2.1-or-later** |
| 仓库路径 | `resources/syntax/vedit-vhdl.xml` | `resources/syntax/vedit-markdown.xml` | `resources/syntax/vedit-tcl.xml` |
| `extensions=` | `*.vhd;*.vhdl` | `*.md;*.markdown;*.mdown;*.mkd` | `*.tcl;*.tk` |
| 行数 / 字节 | 106 / 6,836 B | 59 / 3,559 B | 78 / 7,429 B |
| **sha256** | `47a4539e08fc6b6257d0897c6187c25c4d9620fe8ac2b2023729b31d6ef75451` | `9724927b63ef9824739727f471ea7e98d61d0e9ff23b054c82ae21c73e70fc5a` | `3e201a1df4ad0afc679635a105965a067d16fe4c3618662def4676680572e126` |
| 安装落点（AppDir） | `usr/share/org.kde.syntax-highlighting/syntax/vedit-vhdl.xml` | …`/vedit-markdown.xml` | …`/vedit-tcl.xml` |
| 上游功能参照（**非复制**） | KDE `syntax-highlighting` v6.24.0 `data/syntax/vhdl.xml`（76,513 B，sha256 `5df96ab7362d9ae05008ff130836f10cafb433b3ca7323cf0dc2f197762d1c4e`） | 同版本 `data/syntax/markdown.xml`（51,969 B，sha256 `bf32150db4d97fbf539bcec4b6957d04c5afc2af148193e92db72223fb82c6fb`） | 同版本 `data/syntax/tcl.xml`（20,286 B，sha256 `1a0e538646e3491406d2d895c5be5c6e8ce3e34ca2bcb897670c9c11bb26a30a`） |
| 参照文件 URL | `https://invent.kde.org/frameworks/syntax-highlighting/-/raw/v6.24.0/data/syntax/<name>.xml` | 同左 | 同左 |

**编写方式（逐份如实声明，BINDING）**：三份都是**独立编写**——只把上游定义当作**功能参照**
（关键词/命令/构造清单的交叉核对与遗漏检查），**没有复制**任何上游的文件结构、注释、正则或文本；
因此按本仓库自己的许可登记（LGPL-2.1-or-later），而不是按上游许可登记。若将来有人把其中任何一份
改成"改编自上游 X 文件"，那一份**必须**改写为上游的许可（KDE `tcl.xml` = BSD、`markdown.xml` =
GPL/BSD、`vhdl.xml` = MIT——见下条），不能沿用本节结论。

**为什么是自研而不是上游拷贝**（与 9.1 的 KDE 禁运同一条纪律）：

- KDE `vhdl.xml` 的**文件内没有许可声明**（连 `license=""` 属性都没有）。但它**不是"无许可"**：
  本仓库保存的 Debian 版权整理（`evidence/ux-batch-2/a3/raw/debian-kf6-copyright.txt`）把
  `data/syntax/vhdl.xml` 明确列在版权文件的 **MIT** 段（行 136），上游 README 也称贡献按 MIT。
  所以 A3 选择自研的理由**不是**"该文件不可用"，而是：①文件内没有可随拷贝保留的许可文本，
  复制后无法在文件里如实登记来源；②本仓库"文件内无声明即不随包"的既有纪律（9.1）适用于
  取证，而不是给该文件扣一顶"无许可"的帽子；③语言名/验收要求（见下条）。
  `markdown.xml` 自述 `license="GPL,BSD"`（双许可，BSD 变体未指明）；`tcl.xml` 自述
  `license="BSD"`（Debian 版权整理记录为 BSD-3-Clause）。把这三份**原样**纳入 MIT 产品都要
  额外承担许可解释成本。
- 产品按**定义名**设置高亮模式，验收断言的名字是 `VHDL` / `Markdown` / `Tcl`；
  上游 Tcl 定义的语言名是 **`Tcl/Tk`**，原样拷贝会直接对不上，仍须改文件。
- 因此三份均为**自有 LGPL-2.1-or-later 定义**，上游文件只作**功能参照**（关键词/构造清单
  交叉核对），文件正文未复制任何上游文本，文件名也刻意不同。
- `.sdc` / `.xdc` **没有**独立定义：产品扩展名表（`src/editor/kernel/highlight_mode.cpp`）
  把它们映射到 `Tcl`（用户裁定）。映射由 ctest `vedit-highlight-mode-selftest` 断言。

证据：`evidence/ux-batch-2/a3/`（上游参照文件原件 + sha256、许可核对、ctest 与 AppDir 断言日志）。

### 9.2 MIT `tree-sitter-verilog`（静态编入）

| 项 | 值 |
|---|---|
| 上游仓库 / commit | `https://github.com/tree-sitter/tree-sitter-verilog` / **`4457145e795b363f072463e697dfe2f6973c9a52`**（2022-09-07） |
| 许可 | **MIT**，© 2018-2021 Aliaksei Chapyzhenka；`LICENSE` 全文随包（`third_party/tree-sitter/verilog/LICENSE`，1,082 B，sha256 `92dbb929e5518c6790db68d32d71c257c0c6cbb3974fcd4d930bf28cf1b1bb46`） |
| 仓库路径 / provenance | `third_party/tree-sitter/verilog/`；`third_party/tree-sitter/PROVENANCE.md` |
| 随包文件与 sha256 | `grammar.js`（133,684 B）`323c121a3e550df0f970d46c544b3f02485e5e6d754acb730d6be7b8dea7dc4b`；`src/parser.c`（46,042,888 B）`a15f78b82068daeb62f3b54c8ad0101aa936a2c921a57806fe8f37dcbf0bfff2`；`src/tree_sitter/parser.h`（5,378 B）`ab104936984904469572a4e868149f7a22fb2929347f837ae6a1f9b790f1b173`；`src/node-types.json`（271,904 B）`e99b42baf4279d3be6dec59129dda0789f196b4e5c99144a781ea6cdf255425c` |
| 分发形态 | 静态编入 `libvedit_syntax_grammars`（`verilog/src/parser.c`）；无运行期下载 / `.so` 加载 / 动态链接；`-DVEDIT_GRAMMAR_*` 可整体关闭 |
| MIT 义务 | 保留版权行 + 许可全文 + 锁定 commit（已满足）；上游无 NOTICE 段落需转发 |

### 9.3 随包外部工具链：Verible（Apache-2.0）与 slang（MIT）

CentOS 7 的软件仓库里没有这两个工具，所以交付物**随包分发**它们（由
`packaging/runtime/assemble-appdir.sh` 安装并逐字节校验）：

| 组件 | 版本 / 身份 | 许可 | 随包路径（AppDir） | 许可全文 |
|---|---|---|---|---|
| **Verible**（`verible-verilog-ls`、`-lint`、`-format`） | `v0.0-4298-g085eef05`，三个二进制的 sha256 由 `scripts/versions.env` 的 `VERIBLE_{LS,LINT,FORMAT}_SHA256` 钉死 | **Apache-2.0** | `bin/verible-verilog-ls`、`bin/verible-verilog-lint`、`bin/verible-verilog-format` | `usr/share/doc/vedit/licenses/Apache-2.0.txt` |
| **slang**（深度 lint） | 12.1，sha256 由 `SLANG_BIN_SHA256` 钉死（本仓库从源码构建） | **MIT** | `bin/slang` | `usr/share/doc/vedit/licenses/slang-LICENSE.txt` |

**显式上游来源（URL）**：

| 组件 | 上游仓库 | 具体来源 / 版本 |
|---|---|---|
| **Verible** | `https://github.com/chipsalliance/verible` | release `v0.0-4298-g085eef05`；下载 URL 与 sha256 钉在 `scripts/versions.env` 的 `VERIBLE_URL`、`VERIBLE_TARBALL_SHA256`、`VERIBLE_{LS,LINT,FORMAT}_SHA256` |
| **slang** | `https://github.com/MikePopoloski/slang` | 本仓库从该上游源码构建（`build/slang/build.sh`，源码身份见 `build/slang/evidence/03-source.txt` 的 `repository=` 行与 commit），产物 sha256 由 `scripts/versions.env:SLANG_BIN_SHA256` 钉死 |

- **sha256 校验**：安装器逐个比对 pin，任何不一致立即失败；实际安装身份写入
  `usr/share/vedit/toolchain.txt`，并进入 `runtime-manifest.txt`。
- **发现顺序**：产品内固定为 **bundled > local checkout > PATH > env**（四级：AppDir 内 `bin/`
  随包副本 > 源码树里由 `scripts/fetch-verible.sh` / `build/slang/build.sh` 产出的本地 artifact >
  `PATH` > `VEDIT_VERIBLE_LS(_DIR)` / `VEDIT_SLANG(_DIR)` 环境变量）；选中的来源
  （`bundled` / `local` / `PATH` / `env:<name>`）与 `missing: [...]`、`local=[...]` 会出现在状态栏、诊断/大纲面板状态和
  `--selfcheck` 输出里——缺工具时显式说明「缺哪个、影响什么、怎么解决」，不静默等同于无功能。
- Verible 上游 tarball 不含 `LICENSE`/`NOTICE` 文件（与本文 §3 的 legacy 说明一致），因此随包提供
  Apache-2.0 正文；slang 的 MIT 全文随包提供。
- 许可全文的仓库内权威位置仍是 `docs/licenses/`（`Apache-2.0.txt`、`slang-LICENSE.txt`）。

### 9.4 范围与复核

- 本节只登记 C11/C14 要求的三项。Qt6/KF6 运行期组件（Qt6、KTextEditor、KSyntaxHighlighting、
  Verible、slang、Rust 服务层 crate）的完整清单由打包收口（`pack-final`）补齐；在补齐前，
  §1–§8 的 legacy 条目**不覆盖** Qt6 产物。
- 最终产物一致性（AppDir 内每份文件可按 sha256 找到）由 `pack-final` 的产物侧清单复核；
  本节只固定仓库内身份。

### 9.5 随包 CJK 字体：Noto Sans CJK（OFL-1.1）

CentOS 7 最小安装没有任何字体，而 AppDir 的 fontconfig 只指向包内字体；不随包
CJK 字体时，中文正文/中文文件名会渲染成方块（T115 的 `inFont_CJK=0` 实测）。

| 字体 | 版本 / 来源 | 随包文件（sha256） | 许可 | 许可全文 |
|---|---|---|---|---|
| **Noto Sans CJK**（TTC 合集：SC/TC/JP/KR/HK + Mono CJK） | Ubuntu 包 `fonts-noto-cjk 1:20240730+repack1-1build1`；上游 <https://github.com/notofonts/noto-cjk> | `packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc`（**19,484,784 B**，sha256 **`b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a`**） | **OFL-1.1**，Copyright **2010-2012, Google Corporation** | 包内 `usr/share/doc/vedit/licenses/Noto-CJK-OFL-1.1.txt`；仓内 `packaging/runtime/assets/licenses/Noto-CJK-OFL-1.1.txt` |

- fontconfig 别名（随包 `etc/fonts/fonts.conf`）：UI 的 `DejaVu Sans` 优先回退
  `Noto Sans CJK SC`；编辑器等宽的 `DejaVu Sans Mono` 优先回退
  **`Noto Sans Mono CJK SC`**（中文全角按 2× 拉丁字宽对齐）；拉丁字符仍由 DejaVu 匹配。
- `assemble-appdir.sh` 按 `scripts/versions.env` 的 `NOTO_CJK_FONT_SHA256` 校验后安装；
  sha 不符即失败。`selfcheck.sh` 断言包内字体存在、OFL 文本存在且含
  `SIL OPEN FONT LICENSE` 与版权行。
- 许可文本随包分发（OFL 的再分发条件）；包内文件与仓内文件内容相同。

## 10. Qt6 产品线：Qt 标准对话框翻译（`qtbase_zh_CN.qm`，T38 / AC-9.3）

> **范围声明（captain 书面授予的本次例外）**：本节由 T38（G7 i18n L2）在该任
> 务的写域之外追加，写 `THIRD_PARTY_NOTICES.md` 是 captain 明确授权的 scope
> 例外；除此之外本任务未改本文件其他内容。追加前 sha256
> `af69af60ee160574e06e387c736a947eade8e0f9ac962e9ad836b100125c6853`（17,739 B），
> 追加后 sha256 记录在 `evidence/i18n-l2/06-license-third-party.md`。

Qt 自带的标准对话框按钮（OK/Cancel/Open/Save/Apply/Close…）由 Qt 自己翻译，
文案不在 VEdit 的 `vedit_zh_CN.qm` 里。产品因此随包一份 **`qtbase_zh_CN.qm`**：

| 组件 | 版本 / 来源 | 随包文件（sha256） | 许可（事实核对） | 分发形态 |
|---|---|---|---|---|
| **Qt6 基础翻译（中文）** | Debian/Ubuntu 包 **`qt6-translations-l10n`** 提供的 `qtbase_zh_CN.qm`；宿主文件 `/usr/share/qt6/translations/qtbase_zh_CN.qm`；Qt **6.10.2+dfsg-7**（与 `vedit-qt6-build:local` 的 Qt 6.10.2 同版本） | `translations/qtbase_zh_CN.qm`（**147,222 B**，sha256 **`0481a9b92a6f0578400e960a7d5ee70217a6a450eaf7f98b7d32b8dbd0d58616`**）；构建期复制到 `<build>/src/app/translations/`，安装/打包到 `bin/translations/` | 该文件在 Debian 包的 copyright 中登记为 **GPL-3 with the Qt-1.0 exception**（`Files: * Copyright 2017-2021 The Qt Company Ltd.`）；**不是 LGPL**（LGPL 说法见下方更正），**GFDL 只覆盖 Qt 文档，不覆盖本 `.qm`** | 独立数据文件，随 AppDir 的 `bin/translations/` 分发，不链接进任何 ELF |

- **聚合方式**：`.qm` 是独立翻译数据文件，与 VEdit 的 MIT 代码不构成派生作品；
  产品在运行时用 `QTranslator::load` 读取它（`src/app/i18n.cpp`），不改写、不重编译该文件。
- **装载顺序**：先装 `qtbase_<tag>.qm`、再装 `vedit_<tag>.qm`，因此 VEdit 自己的上下文
  （若将来撞名）优先；缺失 qtbase 不是启动错误，标准按钮回落英文（`i18n.h` 的
  `InstallResult::qtbaseLoaded` 记录装载结果）。
- **可复现**：源文件路径、字节数、sha256 与 Qt 版本见 `evidence/i18n-l2/04-qtbase-vendor.md`；
  `git status`/校验命令在 `evidence/i18n-l2/50-i18n-l2-report.md` 的 gate 段。
- **许可全文**：该包的完整 Debian copyright 由 `qt6-translations-l10n` 包提供；仓库不复制
  Qt 自身的 GPL 文本（项目原有 `docs/licenses/` 无 Qt 条目）。分发 AppImage/tar.gz 时
  上述「来源 + 版本 + 许可 + sha256」即为该数据文件的归属声明。

## 11. Qt6 产品线：随包 VCS 工具链（git / Subversion，T23 / requirements rev3）

产品交付自带运行环境（含自有 glibc/Qt6），因此也**自带 VCS 工具**，目标机不需要安装
git 或 svn。两个客户端及其依赖库都来自 `scripts/versions.env` 中 **URL + sha256 固定**
的 Ubuntu 26.04 `.deb`（未修改的上游二进制），由 `packaging/runtime/assemble-appdir.sh`
校验后解包到 AppDir 的 `usr/lib/vcs/`，通过 AppDir 自带的 loader 启动；
解析顺序为 **bundled > PATH > env（`VEDIT_GIT`/`VEDIT_SVN`）**，产品 VCS lane 会在初始化时
打印一行 `VCS-TOOLCHAIN ...`（`src/workspace/vcs/vcs_toolchain.cpp`）。

| 组件 | 版本 | 许可 | 来源（固定方式） | 分发形态 |
|---|---|---|---|---|
| **git** | `2.53.0-1ubuntu1.1` | **GPL-2.0-only** | `VCS_DEB_URLS`/`VCS_DEB_SHA256S`（`scripts/versions.env`）；Ubuntu 26.04 `git` 包 | `usr/lib/vcs/libexec/git` + `git-core/` + `bin/git` 包装脚本 |
| **Subversion**（`svn`） | `1.14.5-6build1` | **Apache-2.0** | 同上；Ubuntu 26.04 `subversion` 包 | `usr/lib/vcs/libexec/svn` + `bin/svn` 包装脚本 |
| **libsvn1** | `1.14.5` | **Apache-2.0** | Ubuntu 26.04 `libsvn1` 包 | `usr/lib/vcs/lib/libsvn_*.so.1` |
| **APR** | `1.7.6` | **Apache-2.0** | Ubuntu 26.04 `libapr1t64` 包 | `usr/lib/vcs/lib/libapr-1.so.0` |
| **APR-util** | `1.6.3` | **Apache-2.0** | Ubuntu 26.04 `libaprutil1t64` 包 | `usr/lib/vcs/lib/libaprutil-1.so.0` |
| **serf** | `1.3.10` | **Apache-2.0** | Ubuntu 26.04 `libserf-1-1` 包 | `usr/lib/vcs/lib/libserf-1.so.1` |
| **utf8proc** | `2.10.0` | **MIT** | Ubuntu 26.04 `libutf8proc3` 包 | `usr/lib/vcs/lib/libutf8proc.so.3` |
| git / svn 的其余共享库（libcurl-gnutls、OpenSSL、zlib、pcre2、SQLite、liblz4、expat、krb5、Berkeley DB 等） | 构建镜像同版本 | 各自的许可（MIT 系 / Apache-2.0 / OpenSSL / Zlib / LGPL-2.1 等） | 由同一固定镜像提供，`assemble-appdir.sh` 按 `ldd` 闭包复制到 `usr/lib/vcs/lib/` | 独立目录，与产品自己的 `lib/` 互不遮蔽 |

- **svnadmin 不随包**：它只被解包到 `build/vcs-tools/` 供批 6b 的 E2E 使用；`assemble-appdir.sh`
  会在离开前断言 AppDir 内不存在 `svnadmin`。
- **许可全文随包**：`usr/share/doc/vedit/licenses/git-GPL-2.0.txt`（构建镜像的
  `/usr/share/common-licenses/GPL-2`）与 `usr/share/doc/vedit/licenses/Apache-2.0.txt`
  （仓库 `docs/licenses/Apache-2.0.txt`）。
- **源码可得性（GPLv2 §3 / Apache-2.0 §4 的对应做法）**：
  - git 2.53.0 上游源码：`https://git.kernel.org/pub/scm/git/git.git`（tag `v2.53.0`）；
    对应 Debian 源码包：`apt-get source git=1:2.53.0-1ubuntu1.1`（archive.ubuntu.com，
    与二进制包同一套固定 URL/sha256 记录在 `scripts/versions.env`）。
  - Subversion 1.14.5：`https://svn.apache.org/repos/asf/subversion/`（tag `1.14.5`），
    发行归档 `https://archive.apache.org/dist/subversion/`；Debian 源码包
    `apt-get source subversion=1.14.5-6build1`。
  - APR 1.7.6 / APR-util 1.6.3 / serf 1.3.10：`https://apr.apache.org/`、
    `https://serf.apache.org/`（Apache 发行归档）；Debian 源码包同名可 `apt-get source`。
  - utf8proc 2.10.0：`https://github.com/JuliaStrings/utf8proc`（tag `v2.10.0`）。
  以上地址与版本即为分发时的源码指认；产品自身仍为 **MIT**，上述组件各自保留其许可。
- **可复现与验证**：deb 的 URL + sha256、AppDir 内两个二进制的 sha256、`svnadmin` 缺席、
  捆绑版本（`git --version` ≥ 2.43、`svn --version --quiet` ≥ 1.14）与干净容器内的
  `AppRun --selfcheck` 证据见 `evidence/vcs-toolchain/`。
