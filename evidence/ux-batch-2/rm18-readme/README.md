# RM-18 — README.md / docs/RELEASING.md 仍把旧 GTK3 产品当交付物

**任务**：T38。T35 修了 `docs/BUILD.md:101` 的打包入口之后，这条继续清理仓库“前门”：`README.md`
整篇写的是旧 Rust+GTK3 产品（≈48 MB、三种产物），`docs/RELEASING.md` 的发布检查单也还是旧管线。

**写范围**：`README.md`、`docs/RELEASING.md`、`evidence/ux-batch-2/rm18-readme/`。未改代码，未动
`docs/BUILD.md`（T35 已修 §7；其余旧章节作为 out-of-scope 发现列在 §5）。

## 1. 旧文写的是什么（`git show HEAD:README.md` / `HEAD:docs/RELEASING.md`）

README（旧）：

```
3:VEdit 是一个用 **Rust + GTK3 + GtkSourceView** 写的 Verilog / SystemVerilog 专用文本编辑器，
19:## 三种产物，怎么选
23:| `VEdit-<ver>-x86_64.AppImage` | ≈ 48 MB | …
30:三者都自带：GTK3 运行时、GtkSourceView 4.6.1 数据（163 个语言定义 + 主题）、
132:| `undo` / `redo` | … | 撤销 / 重做（GtkSourceView 原生撤销栈） |
186:  **GtkSourceView / GTK3 为 LGPL**，源码获取方式在 NOTICES 的 "LGPL source availability" 一节。
```

RELEASING（旧）：

```
81:发版时上传**三个产物 + 校验文件**：
104:2. `bash scripts/build.sh` 全绿（pinned 镜像构建 + Xvfb 冒烟 + GLIBC 断言）；
105:3. `bash packaging/appimage/run-evidence.sh` 与 `bash packaging/tarball/build.sh` 的**干净无网**验收全绿
```

问题：读者（包括下一个 agent）打开仓库首页看到的是**旧产品**；发布检查单会让人去发旧的三产物。

## 2. 现在写的是什么

### 2.1 `README.md`（82 行，重写）

结构：产品一句话（**Qt6 + KF6 + KTextEditor**、CentOS 7.9 / glibc 2.17、免安装离线）→
**交付物表**（`bash packaging/runtime/build.sh` → `out/VEdit-<ver>-x86_64.AppImage` + `out/SHA256SUMS`；
FUSE 缺失时的 extract-and-run）→ **能做什么**（编辑 / 工程 / 导航 / 诊断 / 搜索替换 / 模块树+接口 /
VCS / 终端+FIM+语言字体）→ **自带工具链**（verible、slang、git/svn、Qt/KF6/KTextEditor/tree-sitter/字体）→
**从源码构建**（`scripts/build-qt.sh` / `packaging/runtime/build.sh` + 门禁命令）→
**旧版 GTK3 产品线（不是交付物）**（一段；`scripts/build.sh` → `build/artifacts/vd-app`，
`packaging/appimage/` + `packaging/tarball/`；镜像 `docs/BUILD.md` §7.2 的措辞）→ 文档表 → 许可。

### 2.2 `docs/RELEASING.md`

- 顶部新增范围说明：当前发布线是 Qt6 产品；旧 GTK3 线保留但**不是发布路径**。
- §1 “发布产物不进 git”的“40–48 MB”改为“100 MB 量级”（Qt AppImage 实测 137,079,288 字节）。
- §5 从“三个产物”改为**一个 AppImage + SHA256SUMS**（`packaging/runtime/build.sh` 自动写到 `out/SHA256SUMS`）。
- §6 检查单改为 Qt 主路径：`bash build/gates/qt6-cxx.sh`（Qt C++ 车道）→ `bash scripts/gates.sh`
  （Rust 服务层）→ `bash build/gates/lrelease.sh` + `bash build/gates/docs.sh` →
  `bash packaging/runtime/build.sh`（F2 隔离 + CentOS 7 探针）→ release 分支/tag → 上传一个 AppImage；
  末尾注明旧 GTK3 线**不是**本检查单的一部分。

## 3. grep 前后（`logs/02-grep-before-after.log`）

BEFORE（`git show HEAD:README.md` / `HEAD:docs/RELEASING.md`）：

- README：`Rust + GTK3`（:3）、`三种产物`（:19）、`≈ 48 MB`（:23）、`GtkSourceView`（:13/:30/:71/:132/:186）；
- RELEASING：`三个产物`（:81/:88/:110）、`scripts/build.sh`（:104）、`appimage/run-evidence.sh` +
  `tarball/build.sh`（:105）。

AFTER（工作区）：

- README：只余**意图性**命中——`:16` 的 `bin/vd-app`（**当前 Qt 包的入口名**，不是旧产品）与 `:60-63`
  的“旧版 GTK3 产品线（不是交付物）”一段；
- RELEASING：只余 `:9`/`:115` 的“旧 GTK3 线不是发布路径”范围说明（意图性）。
- 全文再 grep `Rust + GTK3`：0 命中；`三种产物`：0 命中。

## 4. 事实核对（每条断言都能在树里查到；`logs/03-facts.log`）

| README 断言 | 依据 |
|---|---|
| 产品名 `vedit-app`（Qt6） | `src/app/main.cpp:1850-1851` `setApplicationName("vedit-app")`、`setApplicationVersion("0.2.0")` |
| 交付物 = `bash packaging/runtime/build.sh` → `out/VEdit-<ver>-x86_64.AppImage` + `out/SHA256SUMS` | `packaging/runtime/README.md:11`（一键命令）、`:19`（**唯一交付物** + `out/SHA256SUMS` 配套） |
| 当前产物存在且大小 137,079,288 字节 | `stat packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage`；`out/SHA256SUMS` 94 字节 |
| 包内入口是 Qt 版 `vedit-app`（AppDir 内 `bin/vd-app`） | `ls packaging/runtime/out/AppDir/bin/` = `vd-app` + `translations/` + `git`/`svn`/`slang`/`verible-verilog-{ls,lint,format}` |
| 自带 Git/SVN（`usr/lib/vcs/`） | `ls packaging/runtime/out/AppDir/usr/lib/vcs/bin/` = `git`, `svn`；`evidence/packaging-2/72-clean-container-e2e.log` 的 `VCS-TOOLCHAIN git=… version=2.53.0 svn=… version=1.14.5 source=bundled` |
| 自带 verible/slang | `evidence/slice3-pack/final-206033c/build/60-selfcheck-appimage.log`：`TOOLCHAIN source=verible:bundled slang:bundled missing=[]` |
| 从源码构建 `scripts/build-qt.sh`、`packaging/runtime/build.sh` | 两个脚本都在树里；`docs/BUILD.md` §12.2（产物 `target/qt6-build/src/app/vedit-app`） |
| 门禁命令存在 | `build/gates/qt6-cxx.sh`、`scripts/gates.sh`、`build/gates/lrelease.sh`、`build/gates/docs.sh` 均存在（`logs/03`） |
| 旧线不是交付物 | `packaging/appimage/README.md:1` “Legacy GTK product packaging … do not present this pipeline as the final Qt AppImage”；`docs/BUILD.md` §7.2 |

## 5. 扫到但未改的（超出 T38 写范围）

1. **`docs/BUILD.md` §2“一条命令”仍是旧产品**：`bash scripts/build.sh` → `build/artifacts/vd-app`
   （`docs/BUILD.md:20-38`），Qt 构建在 §12.2。建议后续行把 Qt 的 `scripts/build-qt.sh` 提为 §2、
   旧产品降为附录；T35 只改了 §7。
2. **`docs/ACCEPTANCE-MATRIX.md:209`** 的 `GTK3` 命中是“主机无 GTK3 dev 文件”的环境说明，不是交付物表述，无需改。
3. **`docs/handover/**`** 是内部交接文档，保留旧产品历史（如 `03-architecture.md` 明确区分 runtime 与 legacy），
   不属于 front-facing。
4. **`THIRD_PARTY_NOTICES.md`** 顶部已有“§1–§8 旧 GTK3、§9 Qt6”的范围说明，无需改。

## 6. 门禁

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
docs-gate exit=0
```

## 7. 边界

- 没有重新构建/运行任何产物：README 的事实来自树内脚本、已有产物（18:09 的 AppImage）与批内证据日志。
- 没有改 `docs/BUILD.md`（T35 已修 §7；其余旧章节见 §5 的发现）。

## 8. T39 更新 — BUILD.md §2 改为 Qt 主路径 + 全文一致性

### 8.1 §2 前后

BEFORE（`git show HEAD:docs/BUILD.md` 的 §2）：

```
## 2. 一条命令

bash scripts/build.sh
```

→ 产物 `build/artifacts/vd-app`（旧产品）；Qt 只在 §12.2。

AFTER：

```
## 2. 一条命令（Qt6 交付物）

bash scripts/build-qt.sh          # → target/qt6-build/src/app/vedit-app
bash packaging/runtime/build.sh   # → packaging/runtime/out/VEdit-<ver>-x86_64.AppImage + out/SHA256SUMS
```

门禁列出现行命令：`build/gates/qt6-cxx.sh`（Qt C++ 车道）、`scripts/gates.sh`（Rust 服务层）、
`build/gates/lrelease.sh`、`build/gates/docs.sh`。旧的一键构建**原样保留**，移到 §2.1
「旧版 GTK3 构建（不是 Qt 交付物）」，产物写明 `build/artifacts/vd-app`（不是 `vedit-app`）。

### 8.2 全文一致性（改了哪些 / 留了哪些）

改动（`logs/05-build-consistency.log`）：

1. **开头**：从“§1–§11 旧产品、§12 Qt”改为“两条 lane 并存、交付物只有 Qt6 一条”，并给出
   `build-qt.sh` → `packaging/runtime/build.sh` → AppImage 的一行链路；说明 §6/§9 共用、§10.1 属于 Qt。
2. **§1 前提**：不再只说 CentOS 7；写明 Qt 用 Ubuntu 26.04 固定镜像、旧线用 centos:7。
3. **§2 → §2（Qt）+ §2.1（旧线）**：见 8.1。
4. **§3 固定输入**：表头 `值（0.1.0）` 去掉；标题标为“旧版 lane 与共用工具”，并注明 Qt 矩阵在
   `scripts/versions-qt.env` / `scripts/docker/qt6-locked-packages.txt`（§12.3）。
5. **§4 镜像源替换**：标题标 `旧版 lane`。
6. **§5 GtkSourceView**：标题标 `旧版 lane 的源码构建`；加一句“Qt 产品不用 GtkSourceView，用
   KSyntaxHighlighting（§12.3）”；`本产品` → `旧产品`。
7. **§6 slang**：标题改为“两条 lane 共用”，并写明 `build/slang/out/slang` 同时被旧线打包与
   Qt 的打包/裸构建运行期解析消费。
8. **§8 可复现性**：标题标 `旧版 lane 的边界`，加注“以下针对旧产物；Qt 的隔离/矩阵证据见
   packaging/runtime/README.md 与 evidence/slice3-pack/”。
9. **§9 Rust crate 清单**：注明覆盖工作区 crate，Qt 通过 `vd-ffi` 链接 Rust 服务层，两条 lane 共用。
10. **§10 排障**：标题标 `旧版 lane`，加注 Qt 构建问题见 §12.7、运行期工具链见 §10.1；
    **§10.1 标题**加“Qt 产品”。
11. **§11 GitHub Actions**：标题与开头都标明三条工作流仍是旧 GTK3 线、**不是 Qt 发布路径**
    （Qt 发布见 `docs/RELEASING.md` §6 与 BUILD.md §7.1）。
12. **§12**：顶部注从“§1–§11 是旧产品”改为“本节是 Qt 交付物的底座；§2.1 与 §3–§5、§8、§10、§11
    是旧线（§6/§9 共用）”；§12.2 标题改为“构建命令与产物”并注明与 §2 同一命令；§12.6 的
    “不是交付物”补上“交付物是 §7.1 的 AppImage”；SystemVerilog 边界改为“构建镜像加载的是 KDE
    内嵌定义，交付物随包的是我们自己的 5 份 `resources/syntax/vedit-*.xml`”（不再是“C7 未定”的
    旧说法）。

保留未改（及原因）：

- **§12.1–§12.7 的技术内容**：本来就是 Qt 底座（镜像、版本矩阵、CMake、门禁、边界、排障），只改
  标题/交叉引用，不改事实。
- **§13 slang-server spike**：第三方 LSP 的可行性实验，不是产品交付物声明；内容仍然有效。
- **§7.2 旧产物差异**：T35 已按实测数字写好，保留。
- **§6 的 slang 构建步骤**：两条 lane 共用，保留。
- **§9 的 cargo metadata 步骤**：工作区共用，保留。

### 8.3 版本号说明（记录，不修）

- `packaging/runtime/build-appimage.sh:53` 从**工作区 `Cargo.toml`** 读取 `version`：
  `Cargo.toml:25` = **0.1.1** → 当前产物名 `VEdit-0.1.1-x86_64.AppImage`（`logs/06` 的 `ls` 佐证）。
- Qt 产品自报 **0.2.0**：`src/app/main.cpp:1850-1851`（`setApplicationName("vedit-app")` /
  `setApplicationVersion("0.2.0")`）。
- 两个数字**不是同一来源**；BUILD.md §7.1 已加一行说明并给出读者应期待的值（产物名 = `Cargo.toml`
  的 0.1.1；应用内 = `main.cpp` 的 0.2.0）。**不改任何版本号**（产品决策）。

### 8.4 扫到但未改（超出 T39 写范围）

- `docs/handover/03-architecture.md:157` 引用 `docs/BUILD.md:278-287`（Qt lane 折进门禁的行号）；
  本次编辑后该区间已移位到 §12.5 附近（`grep -n '^### 12.5' docs/BUILD.md`）。内部交接文档，未改。
- `.github/workflows/ci.yml` / `release.yml` 仍是旧 GTK3 线的实现（§11 已如实标注）；Qt 的 CI 化不在
  本卡范围。

### 8.5 门禁

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
docs-gate exit=0
```

### 8.6 边界

- 没有运行 `scripts/build-qt.sh` / `packaging/runtime/build.sh`；本卡的编辑是文档一致性，产物事实来自
  已有产物（18:09 的 AppImage）与树内脚本。
- 版本号未改，只记录两个来源。
