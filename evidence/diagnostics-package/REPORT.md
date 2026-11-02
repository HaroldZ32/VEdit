# T138 — 最终 AppImage `de19746c…` 上的内联诊断实证

- collected_at: 2026-11-02T11:59（Asia/Shanghai） · git_head `4e07e27`
- **所验包**：`packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage`
  `sha256 = de19746c62a9e633de85e40b34f41f7e70350ca14ba4be284a9adca9d544501d`（与任务给定值一致）
- 容器：**stock `docker.m.daocloud.io/library/centos:7`**（glibc 2.17），无 X 显示，`QT_QPA_PLATFORM=offscreen`
- 夹具：`/tmp/bad.sv`（`logic a b;` —— 删掉一个逗号的语法错）
- **未改产品代码、未重打包**（本任务只跑命令）

## 1. 正常路径（命令原文 + 完整关键输出）

```bash
docker run --rm \
  -v $PWD/target/t138/bad.sv:/tmp/bad.sv:ro \
  -v $PWD/packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage:/test.AppImage:ro \
  docker.m.daocloud.io/library/centos:7 \
  bash -c 'QT_QPA_PLATFORM=offscreen VEDIT_DEBUG=1 APPIMAGE_EXTRACT_AND_RUN=1 \
           /test.AppImage /tmp/bad.sv --hold 8000 2>&1; echo APP_EXIT=$?'
```

原始输出（`02-package-run-offscreen.log` 第 19–24 行，未截断）：

```
open.paths: opened 1 file(s): /tmp/bad.sv
diagnostics: analyze /tmp/bad.sv (saved file)
diagnostics: 1 inline annotation(s) on /tmp/bad.sv
TOOLCHAIN source=verible:bundled slang:bundled missing=[] local=[]
APP_EXIT=0
```

逐条断言：

| 断言 | 结果 | 原文行 |
|---|---|---|
| 文件确实被 `<file>` 位置参数打开 | **PASS** | `open.paths: opened 1 file(s): /tmp/bad.sv` |
| 自动分析（无 `--project`、无点击） | **PASS** | `diagnostics: analyze /tmp/bad.sv (saved file)` |
| **内联注解计数** | **PASS** | `diagnostics: 1 inline annotation(s) on /tmp/bad.sv` |
| 工具链来自包内 bundled | **PASS** | `TOOLCHAIN source=verible:bundled slang:bundled missing=[] local=[]` |
| 干净退出（无崩溃/SIGPIPE） | **PASS** | `APP_EXIT=0` |

“1 inline annotation(s)” 是 `AnnotationBridge` 在把诊断推进 `KTextEditor::AnnotationModel`
**之后**打印的内部计数；它出现即证明：controller 里该文件有 ≥1 条诊断，且注解模型已装载到活动 View。

## 2. 反向控制（同一命令 + `VEDIT_NO_ACTIVE_FILE_WATCHER=1`）

```bash
... bash -c 'QT_QPA_PLATFORM=offscreen VEDIT_DEBUG=1 VEDIT_NO_ACTIVE_FILE_WATCHER=1 \
             APPIMAGE_EXTRACT_AND_RUN=1 /test.AppImage /tmp/bad.sv --hold 8000 2>&1; echo APP_EXIT=$?'
```

```
open.paths: opened 1 file(s): /tmp/bad.sv      <- 文件仍然被打开（控制非空洞）
APP_EXIT=0
```

`grep -c 'diagnostics: analyze'` = **0**，`grep -c 'inline annotation'` = **0**。
→ 关掉 `ActiveFileWatcher` 后**没有任何分析/注解**，而文件打开照旧。步骤 1 因此不是空转。

## 3. 如实说明：包内**观察到什么、没观察到什么**

- **能证实的**：包内自动触发分析、真实 verible（bundled）出诊断、**注解计数 = 1**、干净退出。
- **包内不可观测的**：
  1. **面板状态行文字**（`edited.sv — analyzed HH:MM:SS (saved file): 1 diagnostic(s)`）是 `QLabel` 文本，
     包内二进制不把它写到 stderr；`grep 'diagnostic(s)'` = 0。该文字已在**产品构建**验过（T131 17/17，截图
     `evidence/diagnostics-inline/shot-seam.png`），包内只能由“注解计数 1 + analyze 行”间接佐证。
  2. **注解的具体行号/像素**：包内没有截图入口（`--screenshots` 是另一套自检模式，不接受位置参数），
     无头容器里也没有 xwd/import；因此“编辑器第 2 行出现 ✖”的**像素证据只在产品构建上有**
     （T131 截图）。包内证据是注解**计数行**，不是像素。
  3. **缓冲区版（不保存编辑）**：包内没有可驱动“编辑缓冲区”的入口（无 CLI 改文本），所以
     **缓冲区版仅在 T131 产品构建验收**；包内只验了**磁盘版**（saved file）。这一点不含糊地记录在此。
- 另外说明：第一次尝试（无 `QT_QPA_PLATFORM`）失败于容器无 X：
  `This application failed to start because no Qt platform plugin could be initialized`（APP_EXIT=127），
  已改用 offscreen 并保留原始日志 `01-package-run.log`。

## 4. 一句结论

**包内内联诊断可被证实**：最终 AppImage `de19746c…` 在 stock centos:7、无 `--project`、仅 `<file>` 参数下，
自动产生 `diagnostics: 1 inline annotation(s)`，反向控制下归零；**缺口只剩两处不可观测**
（面板状态行文字、注解像素与缓冲区版编辑），它们已在产品构建（T131）验过，包内需截图入口或 GUI 才能补。

## 5. 证据清单
- `00-pins.txt` — C9 头（时间/HEAD/包 sha256/容器/夹具）+ 两侧断言摘录
- `01-package-run.log` — 首次失败尝试（无平台插件，APP_EXIT=127）
- `02-package-run-offscreen.log` — 正常路径完整输出（含 `1 inline annotation(s)`）
- `03-package-reverse-control.log` — 反向控制完整输出（analyze 行 0、注解行 0）
