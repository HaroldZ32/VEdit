# V12 — 强制 Qt 自绘文件对话框 + 弹层诊断日志

- 工作区：`/home/haroldzhao/MyProj/DshProj/VEdit`
- 分支 / 基线：`feat/ui-mosaic` / `0eaa8e9`（V11 弹层加固之后）
- 目标：定位用户实机「**打开文件夹**对话框里右键菜单（显示隐藏文件 / 新建文件夹）整块纯黑」。
  V11 已证明 QSS 层加固在无头 Xvfb 下菜单正常，但用户重打包后照样黑；剩余最大变量是
  **文件对话框走了原生 / portal 实现**——原生对话框的菜单由桌面主题绘制，QSS 根本管不到。
- 本次做两件事：① 所有文件对话框强制 `QFileDialog::DontUseNativeDialog`；
  ② 在 `ApplicationShell` 上装 `QApplication` 级事件过滤器，把**每个 `QMenu` 弹出**的
  关键状态自动追加到 `<stateDir>/ui-menu-diag.log`（用户无需设环境变量）。

## 1. 改动清单

### ① 强制 Qt 自绘文件对话框

改动前 `grep` 结果：生产代码里**没有任何** `DontUseNativeDialog`（只有
`src/app/file_dialog_filters_selftest.cpp` 的回归用例自己设过），所以 ① 不是空操作。

集中封装在 `src/common/file_dialog_filters.h`（已有的对话框配置头）新增：

| helper | 作用 |
|---|---|
| `vedit::common::useOwnFileDialog(QFileDialog &)` | 对已构造的对象 `setOption(DontUseNativeDialog)` |
| `vedit::common::chooseExistingDirectory(...)` | `getExistingDirectory` + `ShowDirsOnly \| DontUseNativeDialog` |
| `vedit::common::chooseSaveFileName(...)` | `getSaveFileName` + `DontUseNativeDialog` |

6 个生产创建点全部改到（`grep` 确认不再有裸的
`QFileDialog::getExistingDirectory/getOpenFileName/getSaveFileName` 生产调用）：

| # | 文件 | 位置 | 对话框 | 改法 |
|---|---|---|---|---|
| 1 | `src/app/app_shell.cpp` | `openProjectFolderDialog()` | File > Open Folder / Switch Project | `chooseExistingDirectory` |
| 2 | `src/app/app_shell.cpp` | `file.open` handler | File > Open（对象式，带 filter 预应用） | `useOwnFileDialog` |
| 3 | `src/app/app_shell.cpp` | `saveActiveDocumentAs()` | File > Save As | `chooseSaveFileName` |
| 4 | `src/workspace/filetree/file_tree_widget.cpp` | `FileTreeWidget::onOpenFolder()` | Files 面板 Open Folder | `chooseExistingDirectory` |
| 5 | `src/workspace/project/project_panel.cpp` | `ProjectPanel::onImportRequested()` | Import Filelist | `useOwnFileDialog` |
| 6 | `src/workspace/settings/settings_panel.cpp` | `makePathRow()` 的 `…` 按钮 | 选工具链文件 | options 形参直接传 `QFileDialog::DontUseNativeDialog` |

第 6 处没有走公共 helper：settings lane 还支持 standalone 构建
（`src/workspace/settings/CMakeLists.txt` 的 `CMAKE_SOURCE_DIR == CURRENT_SOURCE_DIR`
分支不包含 `src/common` 的 include 路径），为不越界改 CMake，按任务允许的
options 形参写法落在原地。

### ② 弹层诊断日志

`src/app/app_shell.{h,cpp}`：

- 构造函数在 stateDir 解析后 `QApplication::instance()->installEventFilter(this)`；
  析构时显式 `removeEventFilter`（同一进程里多个 shell 的 ctest 不会留下悬空过滤器）。
- `eventFilter()` 只观察 `QEvent::Show`，`qobject_cast<QMenu *>(watched)` 成功即记录，
  从不消费事件。
- 日志路径：`<ShellOptions::stateDir>/ui-menu-diag.log`——与 `session.json` 同目录；
  正常启动时 stateDir 是 `QStandardPaths::AppConfigLocation`，`--state-dir` 时就是该目录。
- 目录不存在先 `mkpath`，文件以 `Append | Text` 打开，一条一行紧凑 JSON。
- 失败全静默：`mkpath` / `open` 失败直接 return，不影响启动，也不打 warning。
- 字段（每行都有）：`ts`、`class`、`objectName`、`parentClass`、`parentObjectName`、
  `isWindow`、`translucent`（`WA_TranslucentBackground`）、`width`/`height`、
  `paletteWindow` / `paletteBase` / `paletteText` / `paletteButton`、
  `styleObjectName`、`styleClass`、`styleSheetEmpty`（**只记是否为空，不写样式表内容**）。
- 未改 `resources/**`、未改测试、未新增依赖。

## 2. 诊断日志实测（Xvfb + XTest 真实点击）

管线：`capture-v12.sh` + `driver_v12.py`，容器 `v12-shot`，Xvfb `:98`
（1680x1050x24），`QT_QPA_PLATFORM=xcb`，主题 `yunwu`（surface `#FFFFFF`），
每个风格独立 `--state-dir /tmp/v12-state-<style>`。步骤：点工具栏
`Open Folder…`（243,44）→ 在文件列表内右键（700,420）→ Escape 关菜单/对话框 →
点应用自己的 `View` 菜单（125,10）。

本次只打开了两个菜单，日志里正好每个菜单**一行**（两种风格各 2 行）：

fusion（`v12-menu-diag-fusion.log`）：

```json
{"class":"QMenu","height":54,"isWindow":true,"objectName":"","paletteBase":"#ffffff","paletteButton":"#ffffff","paletteText":"#1f2328","paletteWindow":"#ffffff","parentClass":"QTreeView","parentObjectName":"treeView","styleClass":"QStyleSheetStyle","styleObjectName":"","styleSheetEmpty":true,"translucent":false,"ts":"2026-11-09T09:47:40.043","width":170}
{"class":"QMenu","height":492,"isWindow":true,"objectName":"menu-view","paletteBase":"#ffffff","paletteButton":"#ffffff","paletteText":"#1f2328","paletteWindow":"#ffffff","parentClass":"QMenuBar","parentObjectName":"","styleClass":"QStyleSheetStyle","styleObjectName":"","styleSheetEmpty":true,"translucent":false,"ts":"2026-11-09T09:47:48.164","width":353}
```

windows（`v12-menu-diag-windows.log`）：

```json
{"class":"QMenu","height":54,"isWindow":true,"objectName":"","paletteBase":"#ffffff","paletteButton":"#ffffff","paletteText":"#1f2328","paletteWindow":"#ffffff","parentClass":"QTreeView","parentObjectName":"treeView","styleClass":"QStyleSheetStyle","styleObjectName":"","styleSheetEmpty":true,"translucent":false,"ts":"2026-11-09T09:48:07.934","width":169}
{"class":"QMenu","height":492,"isWindow":true,"objectName":"menu-view","paletteBase":"#ffffff","paletteButton":"#ffffff","paletteText":"#1f2328","paletteWindow":"#ffffff","parentClass":"QMenuBar","parentObjectName":"","styleClass":"QStyleSheetStyle","styleObjectName":"","styleSheetEmpty":true,"ts":"2026-11-09T09:48:16.050","width":353}
```

读法（也是给用户实机的判据）：

- 文件对话框右键菜单的 `parentClass=QTreeView`、`parentObjectName=treeView`
  —— 这是 `QFileDialog` **Qt 自绘实现**内部的文件列表视图；两行都带
  `parentClass` 说明它是 Qt 进程内的 `QMenu`，**原生/portal 对话框不会向 Qt
  创建任何 `QMenu`，这一行会缺失**。
- `styleClass=QStyleSheetStyle`：应用级 QSS 代理确实作用到这个菜单上；
  `styleSheetEmpty=true` 是正常的——样式表设在 `QApplication` 上，不在单个 menu 上。
- `translucent=false`、`paletteWindow=#ffffff`、`isWindow=true` 是当前环境健康形态。
  用户实机若仍看到黑底，新日志会直接暴露差异项（例如 `translucent=true`、父类不是
  `QTreeView`/`QMenuBar`、或 palette 为深色）。
- `styleObjectName` 为空 + `styleClass=QStyleSheetStyle`：Qt 的 QSS 包装 style
  没有 objectName，这是事实记录，不是漏字段。

## 3. 对话框是否仍是 Qt 自绘 / 外观差异

截图（1680x1050，yunwu）：

| 风格 | 对话框 | 右键菜单 | View 菜单 |
|---|---|---|---|
| fusion | `v12-dialog-fusion.png` | `v12-menu-fusion.png` | `v12-view-fusion.png` |
| windows | `v12-dialog-windows.png` | `v12-menu-windows.png` | `v12-view-windows.png` |

- 与 V11 的**改动前**截图逐像素对比（同分辨率/同主题/同 fixture）：
  - 对话框区域（400,150–945,595）：fusion 仅 **14 px** 差异（x=416、y=516–529
    一条竖线 = `Directory:` 输入框的闪烁光标），windows **0 px**；
  - 整帧其余差异（约 1 万 px）全部落在右侧 Diagnostics/Outline 面板，是异步
    LSP 结果在这次运行中先返回所致，**不是对话框外观变化**。
  - 结论：`DontUseNativeDialog` 在本取证环境里**没有改变对话框外观**。原因实测：
    构建镜像里没有任何 `platformthemes` 插件
    （`find / -path "*plugins/platformthemes*" -name "*.so"` = 0 条，也没有
    xdg-desktop-portal 的 Qt 后端），所以这个环境本来就只能走 Qt 自绘；
    该选项的价值在**用户实机**（存在 portal/GTK 平台主题时把原生分支关掉）。
- 右键菜单纯黑像素测量（区域 700,420–870,474，与 V11 同口径）：
  fusion `0/9405` 纯黑，主导色 `#FFFFFF` 82.8%；windows `0/9405`，主导色
  `#FFFFFF` 82.9%。即本次构建的菜单是白底浅灰字，正常。
- 截图哈希：
  - `v12-dialog-fusion.png` `33b0569750e5c7e9667f13a1b0aef183767ca03d4aaa662b83c8f9b70d427691`
  - `v12-dialog-windows.png` `36805a5c611ca8254936c7407b74d841463173224d350a683b92912a4b1f6481`
  - `v12-menu-fusion.png` `461eccd6a5e76a1897d3544b08c49593dc5fa0a15560ce5262b5b4d898831b96`
  - `v12-menu-windows.png` `63551c67d702c290b5af37693a817a1e0f071ae01fc602e05e5a8061e54b7fac`

## 4. 构建与测试

命令：`bash scripts/build-qt.sh`（后台运行），观察到 **exit 0**。

`evidence/m0-build/build-logs/33-qt6-ctest.log` 尾部：

```
61/62 Test #61: vedit-workspace-settings-selftest ..............   Passed    1.36 sec
      Start 62: vedit-ui-scale-restart
62/62 Test #62: vedit-ui-scale-restart .........................   Passed    0.33 sec

100% tests passed, 0 tests failed out of 62

Total Test time (real) =  88.74 sec
```

两个已知 flake 本次均一次通过：

```
43/62 Test #43: vedit-module-tree-catalog-test .................   Passed    3.78 sec
51/62 Test #51: vedit-rename-e2e ...............................   Passed    1.21 sec
```

xcb 软件渲染 smoke（同一次构建）：

```
M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ksynhl=6.24.0
tree_sitter=0.25.9 window=1 editor=1 document=1 definitions=451 themes=30
... kshlib=/usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0
```

证据：`evidence/m0-build/build-logs/32-qt6-build.log`（402/402 链接完成）、
`33-qt6-ctest.log`、`36-qt6-xcb-smoke.log`。

## 5. 未验证项 / 边界（如实声明）

- **未复现用户实机的纯黑菜单**：取证镜像没有合成器、没有 platformthemes /
  portal 后端，任何风格下都复现不了（与 V11 结论一致）。本次能证明的是：
  所有生产文件对话框都强制走 Qt 自绘分支；Qt 自绘分支里文件对话框右键菜单
  确实是一个 `QMenu` 并已被诊断日志记录；本环境下它不黑。
- **不能证明用户实机缺陷已修**：若用户的截图本来就是 Qt 自绘对话框（“查找范围:”
  布局正是 Qt 自绘 `QFileDialog` 的形态），则 ① 不改变其行为，需要靠 ② 在用户
  实机产生的日志来定位（看 `translucent` / `parentClass` / palette 哪一项异常）。
- 未在真实 X11 合成器、真实 portal、AppImage 运行时复测；未验证 HiDPI / 深色主题。
- 诊断日志**没有轮转/上限**（任务未要求）；每弹出一个菜单一行，正常使用增长很慢。
- 构建命令的 stdout 未落盘（后台作业的 `/tmp` 在本次环境中不可持久），因此以
  构建脚本自己写的 `evidence/m0-build/build-logs/**` 为准。

## 6. 证据清单

| 内容 | 路径 |
|---|---|
| 产品改动 | `src/app/app_shell.{h,cpp}`、`src/common/file_dialog_filters.h`、`src/workspace/filetree/file_tree_widget.cpp`、`src/workspace/project/project_panel.cpp`、`src/workspace/settings/settings_panel.cpp` |
| 取证脚本 | `evidence/ui-mosaic/visual-fix/capture-v12.sh`、`driver_v12.py` |
| 菜单诊断日志 | `evidence/ui-mosaic/visual-fix/v12-menu-diag-{fusion,windows}.log` |
| 截图 | `evidence/ui-mosaic/visual-fix/v12-{dialog,menu,view,closed,view-before}-{fusion,windows}.png` |
| 应用日志 | `evidence/ui-mosaic/visual-fix/v12-app-{fusion,windows}.log` |
| 构建/ctest | `evidence/m0-build/build-logs/32-qt6-build.log`、`33-qt6-ctest.log`、`36-qt6-xcb-smoke.log` |
