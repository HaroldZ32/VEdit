# T18 — i18n 运行时（语言切换 + 一键重启 + .ts/.qm）：实现与证据

Cell `cell-20261102T100600176Z` · task **T21** (`i18n-t18`) · member **se-i18n** · verifier **Reviewer**
Frozen contract: `.mpd/cellwork/cell-20261102T100600176Z/contracts/i18n-t18.md`
Requirements: `.mpd/cellwork/cell-20261102T100600176Z/requirements.md` **AC-4.1 / AC-4.2 / AC-4.8`
（旧口径 AC-10.1.1.1、AC-10.1.2.1/2 的对应项见 §3）。
本任务**未编辑任何测试或门禁**；所有改动都在冻结契约的写域内。

## 0. 结论摘要

| 验收 | 结果 | 证据位置 |
|---|---|---|
| AC-4.1 设置里 Auto/English/简体中文，选择持久化 | **PASS** | `10-probe-offscreen.log`：`panel.language-values — auto,en,zh_CN`、`panel.language-stored — zh_CN`、`store.roundtrip — loaded=1 value=zh_CN`；截图 `31-zh-preferences.png` |
| AC-4.2 取消 → 中止、什么都不写、UI 明示、单对话框 | **PASS** | `cancel.one-prompt — 1`、`cancel.prompt-lists-two — alpha.sv, bravo.sv`、`cancel.panel-message/shell-message — 语言未切换：重启已取消`、`cancel.settings-unchanged/archive-unchanged/archive-mtime-unchanged/alpha-unchanged/bravo-unchanged`；截图 `32/33/34` |
| AC-4.2 true → `saveSession()` → 重启 | **PASS** | `save.one-prompt — 1`、`save.document-a/b-written`、`save.language-stored`、`save.archive-valid`、`save.relaunch-fired — 1`、`next-start.english`；真实进程重启 `13-restart-spawn.log` |
| AC-4.2 `session.json` 不被修改（sha 比对） | **PASS** | `save.archive-sha-unchanged — before=556477c049b5 after=556477c049b5`、`store.archive-sha-unchanged`（同一 sha）、`store.archive-mtime-unchanged`、`archive.no-language-key`、`save.archive-no-language-key` |
| AC-4.8 .ts/.qm 随包、离线可用 | **PASS** | `.qm` 由 `Qt6::lrelease` 在构建期生成到 `<binary>/translations/`；AppDir 实测含 `bin/translations/vedit_zh_CN.qm`（与构建树同 sha `6b844d46…`）；探针从本地文件装载（`i18n.zh.qm`） |
| 树可编译 / 产品门禁 | **PASS** | `bash scripts/build-qt.sh --no-test` exit 0（`logs/33-build-qt.log`）；`ctest` **38/38** exit 0；`--selfcheck` **245/0** exit 0（`logs/32-selfcheck-ctest.log` + `logs/35-selfcheck-ctest-final.log`（最终串行复跑）） |

## 1. 交付物与设计

### 1.1 `src/app/i18n.{h,cpp}`（新增，唯一 i18n 入口）
- `UiLanguage { Auto, English, SimplifiedChinese }`；写盘值只有 `auto` / `en` / `zh_CN`（`languageSetting()`），
  `ui.language` 是扁平设置存储（`settings.toml`）里唯一的键（`languageKey()`）。
- `installTranslations(settingsPath, translationsDir)`：**幂等**（先移除上一次安装的 translator），
  在 shell 构造**之前**调用，因此菜单/面板/对话框字符串在创建时就是目标语言。
  `Auto` 按 `QLocale::system()` 解析；产品只随包 `zh_CN`，其它系统区域保持英文源串。
  缺失 `.qm` 只记 diagnostic，不阻止启动。
- `restartApplication()`：默认路径 `QProcess::startDetached(自身, 原参数)` 后**立即退出本进程**；
  测试 seam `setRestartApplicationHandler()` 替换为记录调用（探针用）。
- `setRestartHandler()/requestRestart()`：shell 把严格重启契约注册进来，设置页不需要知道 shell 类型。

### 1.2 设置页（`settings_panel.*`）
工具页顶部新增一行：`界面语言:` `[Auto | English | 简体中文]` + `立即重启以应用` + 提示行
（`重启后生效。` / `界面语言在下次启动时生效。`）。
**选择本身只是预览**：只有重启动作通过未保存守卫后才写 `ui.language`；写盘**只改这一个键**
（基线是"从 store 读到的快照"，不是对话框里的其它预览值），因此取消分支真的零写入。
旧配置文件里 `ui.language` 缺失/未知 → `auto`，不阻止启动。

### 1.3 一键重启契约（`ApplicationShell::restartForLanguageChange`）
```
confirmUnsavedDocuments() == false -> 中止：不写语言、不写 session.json、不重启
                                    -> 状态栏 + 设置页显示「语言未切换：重启已取消」
                            == true -> persistLanguage(tag)   (只写 settings.toml)
                                    -> saveSession()          (T19 的既有方法)
                                    -> i18n::restartApplication()
```
- `confirmUnsavedDocuments()` 现在**先收集所有窗格/标签的脏文档**：1 个脏文档沿用 T19 的单文档
  `Save/Discard/Cancel` 对话框（T19 证据不变）；**≥2 个脏文档只弹一个对话框**，逐行列文件路径，
  按钮 `全部保存 / 全部丢弃 / 取消`；headless 一律 Cancel，绝不静默丢弃。
- `session.json` 的归属按 T19 合同执行：i18n 代码**从不写**该文件，只调用 T19 的 `saveSession()`
  （`.mpd/memory/agents/agent-vedit/repo/memory/t19-provider-a-18-t18-mvz100i0.md` §3：
  "T18 不得写该文件，只调 `saveSession()`"）；实测该调用写出的字节与调用前**完全相同**
  （`save.archive-sha-unchanged`），语言键从未进入存档（`archive.no-language-key`）。

### 1.4 翻译文件与打包
- `translations/vedit_zh_CN.ts`：84 条（**本任务引入/包装的壳与设置工具页字符串**；
  术语取自 `evidence/i18n/03-glossary.md` + `02-bilingual-table-draft.md`，不自造语料）。
  全量 215 条 UI 的逐条翻译按合同归 **T21**（i18n-t21），本文件会继续扩展。
- `src/app/CMakeLists.txt`：`qt_add_lrelease` + `add_dependencies(vedit-app release_translations)` +
  `install(FILES … DESTINATION bin/translations)`；`CMakeLists.txt` 里 `find_package(Qt6 … LinguistTools)`
  是 **QUIET 可选**（无 LinguistTools 的树照常配置，只是不生成 .qm）。
- `packaging/runtime/assemble-appdir.sh`：把 `bin/` 旁的 `translations/*.qm` 原样装进 AppDir 的
  `bin/translations/`（离线，本地文件，无网络）。
- `probe/real-start.sh`：用**发行二进制**（不是探针）在 Xvfb+xcb 下分别以 `ui.language=zh_CN`
  与无语言键启动，抓根窗口成 `shots/36-real-{zh,en}-start.png`；由 `probe/run.sh` 末尾调用。

## 2. 命令、观察结果、退出码

```bash
# 产品构建（文档化入口）
bash scripts/build-qt.sh --no-test                     # → BUILD_QT_EXIT=0（logs/33-build-qt.log）
# 单元/集成测试
QT_QPA_PLATFORM=offscreen ctest --test-dir target/qt6-build
                                                       # → CTEST_EXIT=0，100% tests passed, 0 failed out of 38
# 产品自检
QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/sc-t18-final
                                                       # → SELFCHECK_EXIT=0；APP-SHELL 245/0；EDIT-CORE 67/0
# 本 lane 探针（固定镜像、重建产品库、编译探针、offscreen + Xvfb/xcb + 真实重启）
bash evidence/i2-i18n/probe/run.sh                     # → RUN_EXIT=0；末行 I18N-PROBE-OK
#   offscreen: I18N-PROBE failures=0   PASS=56 FAIL=0
#   xcb:       I18N-PROBE failures=0   PASS=19 FAIL=0
#   run.sh 末尾还执行 probe/real-start.sh（发行二进制 zh/en 启动截图）→ REAL-START-OK / T18-EVIDENCE-OK
# 真实重启路径（默认 restartApplication，无测试 seam）
#   → parent-exit=0 / parent-marker=yes / relaunched-marker=yes / unexpected-return=no
# T19 会话 lane 回归（只跑 --checks，不写 evidence/i3-session/）
target/i3-session-probe/session-probe --checks         # → SESSION-PROBE failures=0（logs/30-t19-session-regression.log）
# AppDir 组装（.qm 随包）
docker run … bash packaging/runtime/assemble-appdir.sh --require-toolchain
                                                       # → ASSEMBLE_EXIT=0；APPDIR_FILES=217
                                                       #   out/AppDir/bin/translations/vedit_zh_CN.qm
                                                       #   sha256 6b844d46…（与构建树同 sha）
```

探针二进制 `probe/build.sh` 用产品自己的编译/链接行（从 ninja 提取，产品静态库直接链接），
`[probe] built …/target/i18n-probe/i18n-probe`；镜像为 `vedit-qt6-build:local`（Qt 6.10.2 / KF6 6.24.0）。

### 2.1 发行二进制启动时的语言装配（真实 `vedit-app`，非探针）

同一 `target/qt6-build/src/app/vedit-app` 二进制、两个空 state dir，设置文件里分别写
`"ui.language" = "zh_CN"` 与不写（Auto → C 区域 = English），Xvfb+xcb 下启动并抓根窗口：

```
CASE zh language=zh_CN shot_rc=0 app_rc=0
CASE en language=auto  shot_rc=0 app_rc=0
```

- `shots/36-real-zh-start.png`：菜单 设置(S)/文件(F)/视图(V)/帮助(H)，状态栏 项目/主题/窗格/活动/标签 中文；
- `shots/36-real-en-start.png`：同样的位置为 `Settings / File / View / Help`、`Project: no project` 等英文源串。
- 两图由 `logs/34-real-binary-i18n.log` 记录；抓图工具是 `probe/grab_root.cpp`（镜像内无 xwd/import，
  用 Qt `QScreen::grabWindow(0)`）。截前按探针惯例装入随包 NotoSansCJK（构建镜像只有 DejaVu，
  否则中文是豆腐块——这一点本身就是"字体随包"的既有事实，不是 i18n 缺陷）。

## 3. AC-4.2 三个分支（日志原文）

**分支 1 — 取消：零写入 + 单对话框 + 明确文案**（`10-probe-offscreen.log`）
```
PASS dirty.both
PASS cancel.one-prompt — 1
PASS cancel.prompt-lists-two — alpha.sv, bravo.sv
PASS cancel.panel-message — 语言未切换：重启已取消
PASS cancel.shell-message — 语言未切换：重启已取消
PASS cancel.no-relaunch
PASS cancel.settings-unchanged
PASS cancel.archive-unchanged
PASS cancel.archive-mtime-unchanged
PASS cancel.alpha-unchanged
PASS cancel.bravo-unchanged
PASS cancel.language-not-stored — zh_CN
```
`cancel.one-prompt == 1` 且一次拿到 2 个文档 = "多个脏文档只弹一个对话框" 的机制证据；
`32-batch-unsaved-dialog.png` 是同一个对话框的真实 xcb 截图（列 `alpha.sv`、`bravo.sv`，
按钮 `全部保存/全部丢弃/取消`）；`33-language-not-switched.png`/`34-cancel-message-shell.png`
是设置页与主窗口状态栏上的 `语言未切换：重启已取消`。

**分支 2 — 保存后重启：先存文档、再写语言、再存档、再重启**（同一日志）
```
PASS save.one-prompt — 1
PASS save.document-a-written
PASS save.document-b-written
PASS save.language-stored
PASS save.language-in-panel — en
PASS save.relaunch-fired — 1
PASS save.archive-valid
PASS save.archive-no-language-key
PASS save.archive-sha-unchanged — before=556477c049b5 after=556477c049b5
PASS next-start.english — translated=0 tag=en
PASS next-start.source-string — &File
```
`13-restart-spawn.log`：真正的默认重启路径——父进程 spawn 自身后退出（exit 0），子进程写入
`relaunched.txt`；`unexpected-return=no` 证明 `restartApplication()` 没有"返回后继续跑旧进程"。

同一分支在 xcb 用**真实对话框**点「全部保存」（`11-probe-xcb.log`）：
```
PASS shot.save-branch-dialog
PASS shot.save-branch-documents
PASS shot.save-branch-language
PASS shot.save-branch-relaunch — 1
PASS shot.save-branch-archive-sha
PASS shot.save-branch-settings-changed
```
截图 `37-save-branch-restarting.png`：设置页此时显示 `界面语言在下次启动时生效。` 与 `正在重启以应用界面语言…`。

**分支 3 — 语言写入不碰 `session.json`**（同一日志）
```
PASS store.language-write
PASS store.settings-updated
PASS store.archive-sha-unchanged — before=556477c049b5 after=556477c049b5
PASS store.archive-mtime-unchanged
PASS store.archive-no-language-key
PASS store.roundtrip — loaded=1 value=zh_CN
```
即：语言持久化只动 `settings.toml`；`session.json` 的内容 sha 与 mtime 都不变，且从没有语言键。

## 4. 截图（`evidence/i2-i18n/shots/`，Xvfb+xcb，软件渲染）

| 文件 | sha256（前 16） | 内容 |
|---|---|---|
| `30-zh-shell.png` | `c94c7dfaf97c369c` | 简体中文主窗口：菜单 设置(S)/文件(F)/视图(V)/帮助(H)、状态栏 项目/主题/窗格/标签 |
| `31-zh-preferences.png` | `8e73d029e75d32c2` | 设置→工具页：界面语言 = English（待应用）、`立即重启以应用`、`重启后生效。`，其余工具项中文标签 |
| `32-batch-unsaved-dialog.png` | `e932e27620af58a5` | 两个脏文档的**一个**对话框（2 个文档有未保存的更改 / 全部保存 / 全部丢弃 / 取消） |
| `33-language-not-switched.png` | `490c0394ad551b9e` | 取消后设置页状态栏：`语言未切换：重启已取消` |
| `34-cancel-message-shell.png` | `f41ca773b62356f3` | 主窗口状态栏同一文案 + 中文菜单 + 未保存标记 |
| `37-save-branch-restarting.png` | `91e6d59bab54e07f` | 保存分支：真实对话框点「全部保存」后，设置页显示 `界面语言在下次启动时生效。` + `正在重启以应用界面语言…` |
| `35-en-shell.png` | `d26aea2c5ca31780` | English：菜单 `&Settings` 等英文源串（持久化选择为 en 的下一次启动） |
| `36-real-zh-start.png` | `02a178e59c020450` | **发行二进制**以 `ui.language=zh_CN` 启动：中文菜单 + 中文状态栏 |
| `36-real-en-start.png` | `68cb61e762e8df60` | **发行二进制**无语言键（Auto→C 区域=C）：英文菜单/状态栏 |

三个分支的截图覆盖：取消分支 = `32/33/34`；保存分支 = `37`（加上真对话框操作）；
"不写 session.json" 分支是 sha/mtime 断言（无独有画面，日志见 §3）。

## 5. 未能证明 / 边界（交 verifier 判定）

1. **翻译完整度**：本任务只交付 84 条种子（壳 + 设置工具页 + 语言/重启文案）；设置页的 FIM/快捷键
   明细与其它面板的字符串仍为英文源串。**AC-4.3/4.4 的全量 215 条翻译是 T21（i18n-t21）的范围**，
   本报告不声称它们已完成。`.ts` 里没有未使用/未包装的死条目（lrelease：84 finished, 0 unfinished）。
2. **真实重启**：用同一二进制自 spawn + 立即退出验证（父 exit 0、子写标记）；**没有**在桌面会话里
   人工点两次并观察新窗口完整生命周期。`restartApplication` 成功路径用 `std::_Exit(0)` 退出
   （原因见 `src/app/i18n.h` 注释：模态 `QDialog::exec()` 里 `QCoreApplication::quit()` 是 no-op，
   实测；关闭窗口会触发 closeEvent → 第二次存档写入/守卫）。未证明项：真实桌面 WM 下的窗口继承。
3. **取消分支的"零写入"是进程内测量**（settings.toml / session.json / 两个文档的 sha 与 mtime）；
   没有对文件系统做 fanotify 级监听。
4. **单对话框**在 headless 由 `setUnsavedPromptProvider` 调用一次断言，在 xcb 由真实 QMessageBox
   截图断言；对话框标题栏未截入（`grab()` 只截客户区）。
5. **窗口位置/几何**不属于本任务（T19 的会话语义）；探针只断言"存档 sha 不变"。

## 6. 复现

```bash
bash evidence/i2-i18n/probe/run.sh          # exit 0，末行 I18N-PROBE-OK
# 只跑断言（已有探针二进制时）：
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash -lc 'QT_QPA_PLATFORM=offscreen target/i18n-probe/i18n-probe --checks \
            --translations /vedit-build/target/qt6-build/src/app/translations'
```

## 7. 产物 sha256

| 产物 | sha256 |
|---|---|
| `10-probe-offscreen.log` | `4276012437bd9f83d96847eedfbc59926c0bf0cfdf8bb134e65e5ac5d432f457` |
| `11-probe-xcb.log` | `032999be9b581e6c603cff59f0fa9e683ef1c17d6f3e21f5fd6ad505fa6d2959` |
| `12-probe-run.log` | `c990bcdb1b77692d57a08e3c23427911333541c70a49f968725111087d7b3ff5` |
| `13-restart-spawn.log` | `e019d8a9014aa99068ce386eadb3043154e1e11968ad648a975178dde2ab857f` |
| `translations/vedit_zh_CN.ts` | `300c0ce254246f95975a3b97e5ed785bb7cdc434c290328784fa56c573242858` |
| `.qm`（构建树 = AppDir 内，同 sha） | `6b844d46a9c848f0f84ba71aa659936ab4bd04d24fdc31258bc06561f525e45f` |
| `logs/33-build-qt.log` | `83859da3a2fe133ef21c19b9036c6e517b1c8ed45e6a2128a2f57c2f3a111e91` |
| `logs/35-selfcheck-ctest-final.log` | `bd2faa60d7a886018af0cf134282d26f89e7af6271bea195090cd03f843b8fd3` |
| `logs/31-appdir-assemble.log` | `637fd0e36f559538b426e7c5d77bc5cad471f78a463ac009156b8dbd4bccafd4` |
| `logs/30-t19-session-regression.log` | `835b7bec510b4068bce0f95a771354fe55ae0a0ce252dd04a366ba73324ad3dd` |
| `logs/34-real-binary-i18n.log` | `1b1c4734c5b50bfd1d32cbc48332ddf429c55fd5b5277fc29925ce460abf53b0` |
| `shots/30-zh-shell.png` | `c94c7dfaf97c369c165b73689929de8076728e3c3452acfecc8207710b492203` |
| `shots/31-zh-preferences.png` | `8e73d029e75d32c2e966d8f8901516346ad6a9b5e7e25036e8e15387c6173d1e` |
| `shots/32-batch-unsaved-dialog.png` | `e932e27620af58a5be0636fb9f70a8bad99214f3e334e0be47b4d8bf8f76bc18` |
| `shots/33-language-not-switched.png` | `490c0394ad551b9e35b62209a6fda00c288e6333be44f3df5cb9ddfb06ebfbca` |
| `shots/34-cancel-message-shell.png` | `f41ca773b62356f3963e891f27f4bc271796a42d24fb9402b65796bbdef5db56` |
| `shots/35-en-shell.png` | `d26aea2c5ca317809ee902c12799e322aafe872da810f29e7fd79dd1ca78807a` |
| `shots/36-real-zh-start.png` | `02a178e59c020450c1ebd06370938836487ad5ce394a085abb936ee0fcd2d7ff` |
| `shots/36-real-en-start.png` | `68cb61e762e8df6036de63f7761417486288cdb8f05200b1997553ee07ad2417` |
| `shots/37-save-branch-restarting.png` | `91e6d59bab54e07f5f3f644ed01780f951942c57979deb2ddafbeefdf17a139d` |

树状态：HEAD `4e2d0a5` + 既有在途工作树。本任务改动的路径（全部在冻结写域内）：
`src/app/i18n.{h,cpp}`（新）、`src/app/app_shell.{h,cpp}`、`src/app/main.cpp`、`src/app/CMakeLists.txt`、
`CMakeLists.txt`、`src/workspace/settings/settings_panel.{h,cpp}`、`translations/`（新）、
`packaging/runtime/assemble-appdir.sh`、`evidence/i2-i18n/`。

说明：`bash scripts/build-qt.sh` 自身按仓库设计把日志写进 `evidence/ffi-abi/logs/` 与 `evidence/m0-build/build-logs/`（文档化门禁的副作用，不是本任务手工扩写域；这些路径在开工基线里已脏）。
