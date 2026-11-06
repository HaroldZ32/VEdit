# T38 / AC-9.3 — `qtbase_zh_CN.qm`：vendor、装载、构建与打包

## 1. 来源与身份（事实）

| 项 | 值 |
|---|---|
| 文件 | `translations/qtbase_zh_CN.qm` |
| 字节数 | **147,222 B** |
| sha256 | **`0481a9b92a6f0578400e960a7d5ee70217a6a450eaf7f98b7d32b8dbd0d58616`** |
| 宿主来源 | `/usr/share/qt6/translations/qtbase_zh_CN.qm`（与 captain 记录一致） |
| Debian 包 | `qt6-translations-l10n`，Qt **6.10.2+dfsg-7**（与 `vedit-qt6-build:local` 的 Qt 6.10.2 同版本） |
| 许可（按包 copyright 事实） | **GPL-3 with the Qt-1.0 exception**（`Files: * Copyright 2017-2021 The Qt Company Ltd.`）；不是 LGPL；GFDL 只覆盖 Qt 文档，不覆盖本 `.qm` |
| 镜像内可得性 | 镜像 `qmake6 -query QT_INSTALL_TRANSLATIONS` = `/usr/share/qt6/translations` 且**目录不存在**；镜像无 `qtbase_*.qm`、无 Qt .ts 源 → 不能在镜像内生成，只能 vendor |

## 2. 装载（运行期）

`src/app/i18n.cpp::installTranslations`（`src/app/i18n.h` 的 `InstallResult` 增加
`qtbasePath` / `qtbaseLoaded`）：

- 目录与 `vedit_<tag>.qm` 相同（`translationsDirectory()`：`VEDIT_TRANSLATIONS_DIR` → 可执行文件旁
  `translations/` → `../share/vedit/translations`），**不新增任何搜索规则**；
- `QTranslator::load(qtbase_<tag>.qm)` 成功后**先** `installTranslator(qtbase)`，再装载 vedit，
  因此 VEdit 上下文优先；`removeTranslator()` 同时移除两者；
- 文件缺失/装载失败**不是启动错误**：`qtbaseLoaded=false`，标准按钮回落英文。

实测（`logs/28-i18n-probe-after-b2.log`）：

```
language: setting=zh_CN tag=zh_CN vedit=…/translations/vedit_zh_CN.qm qtbase=…/translations/qtbase_zh_CN.qm
ok   i18n.qtbase-loaded — qtbase_zh_CN.qm
ok   i18n.qtbase-file — …/translations/qtbase_zh_CN.qm
     qt standard buttons: Ok='确定' Cancel='取消' Save='保存' Open='打开'
```

## 3. 构建接线（`src/app/CMakeLists.txt`）

- `find_package(Qt6 … COMPONENTS LinguistTools)`（根 `CMakeLists.txt`）+ `qt_add_lrelease` 把
  `translations/vedit_zh_CN.ts` 编译为 `<build>/src/app/translations/vedit_zh_CN.qm`；
- vendored `qtbase_zh_CN.qm` 由 `add_custom_command` 复制到同一目录（ALL target +
  `add_dependencies(vedit-app …)`），并 append 进 `VEDIT_QM_FILES`；
- 单一安装规则：`install(FILES ${VEDIT_QM_FILES} DESTINATION bin/translations)`；
- 缺 LinguistTools 或缺 `.ts` 时显式 STATUS 消息（"UI stays English"），构建不失败。

构建树实测：`target/qt6-build/src/app/translations/` 内
`vedit_zh_CN.qm`（53,828 B）+ `qtbase_zh_CN.qm`（147,222 B，sha256 与源码一致）。

## 4. 打包（`packaging/runtime/assemble-appdir.sh`）

脚本已有 T18 的翻译 hunk：把 `$(dirname "$APP")/translations/*.qm` 复制到
`$OUT/bin/translations/`，整树 `find` 生成 `runtime-manifest.txt`。本轮**运行**验证（未改脚本）：

```
[assemble] translations: qtbase_zh_CN.qm vedit_zh_CN.qm        （日志中的 translations 行）
appdir-exit=0
APPDIR=target/tmp/t38-appdir APPDIR_FILES=218
$ ls target/tmp/t38-appdir/bin/translations/
qtbase_zh_CN.qm  147222 B
vedit_zh_CN.qm    53828 B
$ grep translations target/tmp/t38-appdir/runtime-manifest.txt
0481a9b9…8616  147222  bin/translations/qtbase_zh_CN.qm
e89c575e…a696   53828  bin/translations/vedit_zh_CN.qm
```

日志：`logs/29-appdir-assemble.log`。AppImage 脚本（`packaging/appimage/build-appimage.sh`）
消费的是同一个 AppDir，因此无需改动。

## 5. 边界

- `qtbase_zh_CN.qm` 是**数据文件**，不链接进任何 ELF，不改变 MIT 代码的许可；
  归属声明见 `THIRD_PARTY_NOTICES.md` §10 与 `06-license-third-party.md`。
- 若目标是"完全不 vendor"，则 AC-9.3 的替代路径是对账表写 `infeasible` + Qt 标准按钮自绘；
  本任务按 captain 的裁定选择 vendor，故该替代路径未采用。
