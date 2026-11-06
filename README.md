# VEdit — 面向 Verilog / SystemVerilog 的桌面编辑器

VEdit 是 **Qt6 + KF6 + KTextEditor** 的 Verilog / SystemVerilog 桌面编辑器，目标环境是
**CentOS 7.9 x86_64（glibc 2.17）**：免安装、无 root、可完全离线运行。

当前交付物是**一个单文件 AppImage**（Qt6 产品 `vedit-app`）：包内自带私有运行时和全部外部工具链，
目标机不需要另外安装 Qt/KDE、verible、slang 或 git/svn。

## 交付物

| 项目 | 内容 |
|---|---|
| 构建 | `bash packaging/runtime/build.sh` |
| 产物 | `packaging/runtime/out/VEdit-<ver>-x86_64.AppImage` + `packaging/runtime/out/SHA256SUMS` |
| 运行 | `chmod +x VEdit-<ver>-x86_64.AppImage` 后直接执行；没有 FUSE 时用 `--appimage-extract-and-run`（或 `APPIMAGE_EXTRACT_AND_RUN=1`） |
| 包内入口 | Qt6 版 `vedit-app`（AppDir 内 `bin/vd-app`），`AppRun` 经包内 loader 启动 |
| 平台 | CentOS 7.9 x86_64（glibc 2.17）及更新的主流发行版；已在 CentOS 7 / Ubuntu 24.04 / Rocky Linux 9 / Ubuntu 26.04 的容器环境验证启动与基本功能 |

运行时是 **F2 私有运行时**：包内自带 loader、glibc、Qt6、KF6、KTextEditor、自建
KSyntaxHighlighting、tree-sitter 与字体；语法定义查找收窄到 AppDir 白名单；启动时可观测到
`TOOLCHAIN source=…` / `VCS-TOOLCHAIN …` 行。契约与隔离证据见
[`packaging/runtime/README.md`](packaging/runtime/README.md)。

## 能做什么

| 能力 | 说明 |
|---|---|
| 编辑 | KTextEditor：多标签、分屏、多光标/块选择、原生撤销栈、原生折叠；`.v/.vh` 与 `.sv/.svh` |
| 工程 | 文件夹即工程；`.f` filelist（`$VAR` / `${}` / `$()` / `$$` / `~/`、`+incdir+`、`+define+`、嵌套 `-f`）；Project 面板；工程变量按工程根记忆 |
| 导航 | 跳转到定义、查找引用、重命名（文件作用域）、符号大纲、转到文件（`Ctrl+P`）、面包屑 |
| 诊断 | verible LSP 逐文件诊断 + 工程 lint（verible 或 slang 深度 lint）；保存后自动分析；诊断面板、悬浮修复、文件状态点 |
| 搜索替换 | 当前文件 PCRE / 整个工程 RE2；替换前校验并先落盘；结果可双击跳转 |
| 模块视图 | 模块树（根 = 从未被例化的顶层模块）+ 接口面板（端口方向/类型/位宽/位置，双击跳声明） |
| 版本控制 | Source Control 面板：Git / Subversion 的工作区与历史（stage/commit/branch/tag/stash/worktree/blame/diff 等） |
| 其他 | 内置终端；可选 FIM 补全（默认关闭）；界面语言中/英；中文注释与中文翻译随包（CJK 宽度约 1.60× 的已知取舍） |

以上行为的细节、快捷键与已知限制见 [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md)。

## 自带工具链（无需在目标机安装）

- **Verible**：`verible-verilog-ls` / `verible-verilog-lint` / `verible-verilog-format`（Apache-2.0）
- **slang** 12.1（MIT）：深度 lint 与模块目录
- **Git** 2.53.0 与 **Subversion** 1.14.5（`usr/lib/vcs/`）：版本控制面板；解析顺序
  **bundled > PATH > env**，可设 `VEDIT_GIT` / `VEDIT_SVN` 覆盖
- Qt6 / KF6 / KTextEditor / tree-sitter / 字体：随 AppImage 的私有运行时

## 从源码构建

```sh
bash scripts/build-qt.sh          # 固定镜像 → Rust 服务层 → CMake configure/build/ctest → xcb 冒烟
bash packaging/runtime/build.sh   # 组装 AppDir → 许可/F2 隔离 → 单文件 AppImage + SHA256SUMS
```

`build-qt.sh` 的裸二进制在 `target/qt6-build/src/app/vedit-app`；直接运行它需要宿主提供 KF6/Qt6、
`libtree-sitter` 0.25 与 Qt 平台插件（版本矩阵见 [`docs/BUILD.md`](docs/BUILD.md) §12）。完整构建、
固定版本与可复现性说明见 BUILD.md。门禁：`bash build/gates/qt6-cxx.sh`（Qt 车道）、
`bash scripts/gates.sh`（Rust 服务层）、`bash build/gates/lrelease.sh`（翻译目录）、
`bash build/gates/docs.sh`（文档）。

## 旧版 GTK3 产品线（不是交付物）

仓库还保留**旧 Rust/GTK3 产品** `vd-app` 的构建与打包：`bash scripts/build.sh` 产出
`build/artifacts/vd-app`，`packaging/appimage/` 与 `packaging/tarball/` 是它的 AppImage / tar.gz /
`.run` 管线（按需求非目标 5 保留）。**它不是本仓库的 Qt 交付物**；管线自述也写着
“Legacy GTK product packaging … do not present this pipeline as the final Qt AppImage”。
两条产品线的区别与实测校验见 [`docs/BUILD.md`](docs/BUILD.md) §7.2。

## 文档

| 文档 | 内容 |
|---|---|
| [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md) | 内置用户手册：界面、工程、编辑/搜索、诊断、模块树、设置、语言与已知限制 |
| [`docs/BUILD.md`](docs/BUILD.md) | Qt6/KF6 构建底座、打包入口（§7）、固定版本与可复现性 |
| [`docs/RELEASING.md`](docs/RELEASING.md) | 分支模型、提交规范、SemVer 与 Qt 发版流程 |
| [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) | 全部第三方组件、版本、许可与源码获取说明 |
| [`docs/licenses/`](docs/licenses/) | 许可全文 |

## 许可

- 项目自身代码以 **MIT** 许可发布：见根目录 [`LICENSE`](LICENSE)。
- 第三方组件各自保留其许可，见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)（其中 §1–§8
  描述旧 GTK3 产物、§9 描述 Qt6 产品线的语法数据与工具链；范围以该文件为准）。
