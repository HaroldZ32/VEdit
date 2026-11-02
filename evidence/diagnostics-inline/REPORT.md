# T131 — 真机：找不到 Diagnostics + Lint 不好使 + 要求编辑器内联标注

- collected_at: 2026-11-02（Asia/Shanghai） · git_head: `af85225` · 镜像 `vedit-qt6-build:local`
- 包: `VEdit-0.1.1-x86_64.AppImage` sha256 `29182106…`（用户反馈的那个包，**未重打包**）

## 1. 取证：Lint 到底卡在哪一层

### 1.1 包里到底有没有 T118 的代码：**有**
Qt 的 `QStringLiteral` 是 UTF-16，普通 `strings` 看不到；用 `strings -e l`：

```
analyzed                        UTF16LE=1
analysis starts automatically   UTF16LE=1
not saved yet                   UTF16LE=1
No symbols yet                  UTF16LE=2
```

→ **T118 的代码在包里**（也已在 HEAD `af85225`）。所以“没生效”不是“没打进包”。

### 1.2 用户为什么“完全没找到 Diagnose 窗口”：**这个入口根本不存在**
- split dock 的标题是 **"Outline"**（`app_shell.cpp:193 addPanel("outline","Outline",…)`），
- View 菜单里只有 **"To&ggle Outline Panel"**（`app_shell.cpp:254`），
- 全树 `src/app` 里没有任何标题为 “Diagnostics/Diagnose” 的面板或菜单项。
→ 用户在 View 菜单里找 “Diagnostics” 必然找不到；他看到的空 panel 写着 “No symbols yet …”，
   那是 outline，不是诊断。**这是命名/可发现性缺陷，不是“隐藏”问题。**

### 1.3 更关键：真机上的“打开文件”路径从来没有被 headless 验证过
- 产品里 **没有 File→Open**（`QFileDialog` 在 `src/app` 出现 0 次），也 **没有位置参数处理**
  （`addPositionalArgument` 0 次）。打开一个文件的唯一路径是**在文件树里双击**。
- T118 的 30/30、9/9 全部是**直接调用 pane 接缝**（`pane->openFile`）或用 C ABI 驱动 controller，
  **从未经过包内二进制的 GUI 路径**。这就是盲区：测试证明了“接缝→控制器→LSP→面板”是通的，
  但没有证明“双击文件树→同一个 pane→watcher”在真实二进制里也是通的。
- 另外 AGENT.md §T134 记录：Rust 静态链进 C++ 宿主时 **SIGPIPE 默认行为会杀进程**
  （写已死的 verible 子进程管道），且 crash reporter 不捕获 SIGPIPE。T118 的
  `analyzeFile()` 在**换文件时 stop 旧会话 + 起新会话**，正好是“写已死子进程”的高风险动作。
  若用户当时“切文件后窗口消失”，那就是这条；T134 已在 `main.cpp` 忽略 SIGPIPE，
  但本任务的 session-restart 仍应尽量少做（见 §4 的后续建议）。

**结论（一句话）**：卡在**可发现性 + 未验证的 GUI 打开路径**这两层；分析管线本身（接缝→控制器→LSP→缓冲区→诊断）是通的。

## 2. 用户要求：Lint 直接标在编辑器里（已实现）

- 新增 `src/editor/kernel/diagnostics_annotations.{h,cpp}`：
  `KTextEditor::AnnotationModel` 子类 + 每个 View 一个 `DiagnosticsAnnotations`
  （在 `EditCoreProvider::createEditor()` 里 `install(view)`，与 fold/breadcrumb 同款生命周期）。
  - 行首注解：`✖` 错误 / `⚠` 警告 / `ℹ` 信息 + 红/黄/蓝底色；
  - 悬停：`message [rule]`（例如 `syntax error at "b" [verible]`）。
- 新增 `AnnotationBridge`（diagnostics lane，300 ms）：把**当前活动文件**的诊断推进活动 View；
  没有诊断时不动。
- 面板：状态行 `<file> — analyzed HH:MM:SS (editor buffer, not saved yet|saved file): N diagnostic(s)`，
  并新增**诊断明细列表**（`file:line — message [rule]`），与 outline 表并存。
- 发现入口：View 菜单新增 **“&Diagnostics”**（Ctrl+Alt+D）命令；面板自身 `windowTitle = "Diagnostics"`。

## 3. 验收证据（产品构建，非包——见 §5）

`02-verification.log`（`vedit-diagnostics-product-seam`，真实 `ApplicationShell` + 真实
KTextEditor provider + 真实 verible，**无 `--project`**，17/17 PASS）：

```
seam.after-edit status='edited.sv — analyzed 03:30:48 (editor buffer, not saved yet): 1 diagnostic(s)'
seam.disk-untouched  disk=76 bytes
seam.panel-lists-the-issue  1
seam.ktexteditor-view PASS
seam.annotations-installed PASS
seam.annotation_lines=1 tooltip='syntax error at "b" [verible]'
seam.screenshot-saved  evidence/diagnostics-inline/shot-seam.png (1280x860)
seam.reverse-control-no-analysis PASS  (VEDIT_NO_ACTIVE_FILE_WATCHER=1 → 无 analyzed)
```

`shot-seam.png`（同一帧）：左侧 KTextEditor 第 2 行 gutter 有 **✖ 内联标记**，
右侧 “Outline/Diagnostics” dock 显示 `edited.sv — analyzed … (editor buffer, not saved yet): 1 diagnostic(s)`、
outline 表、底部诊断明细 `edited.sv:2 — syntax error at "b" [verible]`；状态栏 `Project: no project`。

对照验收：
1. 无 `--project` 打开含错文件 → analyzed + 诊断 + 内联标记：PASS（17/17）。
2. 删逗号（不保存）→ 标记更新、来源变 “not saved yet”、磁盘 76 字节未变：PASS。
3. 加回逗号 → 错误清零：由 `vedit-diagnostics-ffi-selftest` 的 `t118.restore-clears-error` 覆盖（T118）。
4. 入口：View 菜单 “Diagnostics” 命令 + 面板标题 “Diagnostics”：PASS（代码+截图标题）。
5. 反向控制：禁用 watcher → 无 analyzed：PASS。
6. 未回退：typecheck PASS；ctest 30/31（唯一失败是 **T129/T130 正在改的 settings 面板注册**，与我无关）。

## 4. 为什么 T118 全绿而真机还是不行（盲区分析，交付重点）

| 盲区 | 说明 | 本任务的补救 |
|---|---|---|
| 入口命名 | 面板叫 Outline，菜单叫 Toggle Outline Panel；没有 Diagnostics 字样 | 面板标题 + View 菜单 “Diagnostics”(Ctrl+Alt+D) |
| GUI 打开路径未验证 | 测试直接调 `pane->openFile`，没走“文件树双击→同一 pane” | 仍需一次真机/打包验证（见 §5） |
| 包内无内联呈现 | 只有 panel 文本，错误不在代码行上 | 内联 AnnotationModel |
| 会话重启的 SIGPIPE 风险 | 换文件时 stop+start 会写已死子进程（T134） | 建议后续：同一目录多文件复用会话；或先 `shutdown` 再换 set |

## 5. 打包产物验证：**未完成，需要 Lead 重打包**
本任务明确“不要自己打包”。当前包 `29182106…` 构建于 HEAD `af85225`，**不含本任务代码**
（`strings -e l` 无 “Diagnostics” 新字样/注解模型）。因此在**本包**上只能证明“用户看到的就是旧行为”。
重打包后请按下面命令在包上复验（新代码已具备可 headless 驱动的入口）：

```bash
QT_QPA_PLATFORM=offscreen ./VEdit-*.AppImage --appimage-extract-and-run --hold 6000   # 无 --project
# 期望 stderr: diagnostics: analyze <file> (saved file)  →  dx: … 1 inline annotation(s)
```
（更直接的包级验证需要一个“打开文件”的入口；当前产品仍没有 File→Open / 位置参数，
建议下一个任务补上 `vd-app <file>`，否则只能在 GUI 里点文件树验证。）

## 6. 改动文件
- 新增: `src/editor/kernel/diagnostics_annotations.{h,cpp}`、
  `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp`（T118 引入，本任务扩展）
- 修改: `src/editor/kernel/ktexteditor_provider.cpp`（install 注解）、`src/editor/CMakeLists.txt`、
  `src/workspace/diagnostics/{CMakeLists.txt,diagnostics_integration.cpp,diagnostics_panel.h/.cpp}`、
  `src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp`
- 证据: `evidence/diagnostics-inline/{00-pins,01-package-diagnosis,02-verification,REPORT.md,shot-seam.png}`
- 未 commit/push（Lead 统一提交）。
