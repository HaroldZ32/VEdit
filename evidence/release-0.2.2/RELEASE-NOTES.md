# VEdit 0.2.2 — 卡片式界面、六套产品主题与全工程程序图标

发布日期：2026-11-09（分支 `release/0.2.2` → `main`，tag `v0.2.2`）

## 变更

### 界面（照 `Mosaic` 参考图重做）

- **六套产品主题**：云雾白 / 暖杏黄 / 薄荷绿 / 晴空蓝 / 曜石黑 / 深夜蓝（英文名 Cloud White / Warm Apricot /
  Mint Green / Sky Blue / Obsidian / Deep Navy，随界面语言翻译）。切换入口三个且行为一致：`视图 → 主题` 子菜单、
  工具栏最右端主题按钮、首选项的「界面主题」下拉；三者写同一个 `uiTheme` 键，重启读回最后选择，
  settings 与会话存档冲突时按文件 mtime 仲裁（相同以 settings 为准）。
- **卡片式布局**：`QMainWindow::separator` 造画布间隙（横向 8px / 纵向 16px），dock 标题与内容上下拼接圆角描边
  （Qt 不绘制 `QDockWidget` 本体的 border），卡片阴影用 `QGraphicsDropShadowEffect`；浅色有阴影、深色按反馈改为**纯描边**。
- **按钮与控件**：工具栏按钮图标 + 文字（菜单保持纯文字，`setIconVisibleInMenu(false)`）；`QPushButton/QToolButton`
  统一可见描边；选中行改用 `selection`（surface 向 accent 混合 14%/28%），不再用强调色铺满整行。
- **工程面板**：`Files / Include dirs / Defines / Diagnostics` 四个子区块加独立边界（标题底色 + 分隔线、内容 1px 描边盒）。

### 编辑器

- **语法主题真正作用于编辑器**：经 `KTextEditor::View::setConfigValue("theme", …)` 接到每个 View（新建/分屏 View 自动继承），
  无效主题名回退当前全局主题。此前语法主题只改应用调色板、对编辑器无效。

### 图标与打包

- 新图标包接入全工程：窗口图标随深浅主题切换、`.desktop` 带 `StartupWMClass`、AppDir 根图标与 `.DirIcon`、
  `usr/share/icons/hicolor/` 八档（16–512）；打包脚本把图标链列为交付不变量（缺任一档即失败）。
- 文件对话框统一强制 Qt 自绘（`DontUseNativeDialog`），避免落到原生/portal 实现而绕过主题与样式。
- 新增弹层诊断：每个 `QMenu` 弹层向 `<stateDir>/ui-menu-diag.log` 追加一行 JSON（类名/parent/半透明/调色板/样式）。

## 已知限制

1. **卡片上边无阴影**：Qt 中向上延伸的阴影会被上方相邻控件的重绘覆盖，实测 0–2px；判据 `CONTRACT.md §5` 已显式豁免并留痕。
2. **深色主题无阴影**（仅描边）：按实机反馈刻意为之，与浅色观感不同。
3. **`Diagnostics` 长句换行末行被下沿裁一点**：固定高度面板 + 长文本换行，**改版前即存在**，本次未处理。
4. **卡片左上角/右上角的阴影**同样受 (1) 影响；四角圆角本身已按像素验收（角点非 surface 像素 0→65）。
5. **AppImage 需要 FUSE**：CentOS 7 桌面若 FUSE 不可用，用 `APPIMAGE_EXTRACT_AND_RUN=1`；本次另提供完整 AppDir
   `tar.gz`（解包 `./AppRun`，不需要 FUSE）。
6. **`QFileDialog` 右键菜单在部分环境整块纯黑**：用户实机反馈过，当前包层已加固（菜单方角 + 显式底色）并强制自绘，
   但**未能在开发环境复现**，需用户实机确认；诊断日志路径见上。
7. **两个既有 flake**（与本版无关，单跑必过）：`vedit-rename-e2e (SIGPIPE)`、`vedit-module-tree-catalog-test`。
8. 未验证：真实窗口管理器下的任务栏/桌面分组、高 DPI（2×）、AppImage 六套主题全跑、长时性能。

## 证据

- 发布检查单与日志：`evidence/release-0.2.2/logs/01-gate-qt6-cxx.log` … `05-gate-packaging.log`
- 界面改造与四轮实机反馈修复：`evidence/ui-mosaic/README.md`（含每轮的像素量化与前后对照）
- 独立复核：`evidence/ui-mosaic/review-r1/VERDICT-R1.reviewer3.md`、`evidence/ui-mosaic/review-visual-v2/VERDICT-V2.md`
- 设计契约与判据：`.mpd/plans/ui-mosaic/CONTRACT.md`（§5 视觉验收判据）

## 产物

| 附件 | sha256 |
|---|---|
| `VEdit-0.2.2-x86_64.AppImage`（138,811,896 字节） | `2eae6145e399ccbf9ca226c3e4c9759f6016a650643d512b9462737cb98b1cca` |
| `SHA256SUMS` | 与上表一致 |

免 FUSE 备用：完整 AppDir `tar.gz`（同一内容，`./AppRun` 直接运行）。
