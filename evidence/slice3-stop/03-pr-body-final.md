# VEdit v2 — 最终交付（停点 2）

> **状态：用户正在实机复验。未合并、未打 tag、未发 Release。**
> 本 PR 是整个 v2 迁移的交付出口；`dev` 是集成分支，`main` 只在发布时移动。

---

## 1. 交付物

| 项 | 值 |
|---|---|
| 文件 | `VEdit-0.1.1-x86_64.AppImage` |
| 大小 | **110,443,000 bytes** |
| **sha256** | **`de19746c62a9e633de85e40b34f41f7e70350ca14ba4be284a9adca9d544501d`** |
| **built from** | **`43afcab`** |
| 位置 | **本服务器**：`packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage`（该目录被 gitignore，**不在 git 里**） |
| PR | https://github.com/HaroldZ32/VEdit/pull/11 （base `dev`） |

## 2. 怎么拿到它

```bash
# 1) 先在本服务器核对（确认产物本身没坏）
sha256sum packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage
# 期望：de19746c62a9e633de85e40b34f41f7e70350ca14ba4be284a9adca9d544501d

# 2) 拷到目标机
scp packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage <user>@<host>:/tmp/

# 3) 到了目标机再核一次（防传输损坏）
sha256sum /tmp/VEdit-0.1.1-x86_64.AppImage

# 4) 运行（CentOS 7 常无 fusermount3；且 AppRun 在带桌面的环境里会等待输入，见 §5）
chmod +x /tmp/VEdit-0.1.1-x86_64.AppImage
/tmp/VEdit-0.1.1-x86_64.AppImage --appimage-extract-and-run
```

**本版新增两个"打开文件"入口**（T137）：
- **命令行**：`vd-app <file>...` —— 首个文件进当前 pane，后续每个文件新开水平分屏；与 `--project <dir> <file>` 同款；路径会先整体校验，缺失/目录 → 一行可读错误 + **exit 2**，不空窗。
- **菜单**：`File → Open…`（**Ctrl+O**，无冲突），走真实 `QFileDialog`，复用同一个 `openPaths()`。
- **`.desktop` MIME 关联**：`MimeType=text/x-svsrc;text/x-verilog;text/x-systemverilog;…`、`Exec=AppRun %F` → 双击 `.sv`/`.v` 用 VEdit 打开已具备（**无头环境未真点文件管理器**，见 §4）。

## 3. 已验（本轮真实数字）

| 项 | 结果 |
|---|---|
| **包内 `APP-SHELL-SELFTEST`** | **`ok=1 checks=245 failures=0`**（245/245） |
| 其中 `open.*` | **12 条全 PASS**（command.exists / in-file-menu / shortcut-ctrl-o / dialog.action-opens / fixture-a/b / missing.nonzero-readable / missing.rejected / multiple.ok / active-second / pane-count / paths） |
| 其中 `fold.native.*` | **24 条**全 PASS |
| 其中 `close.*` | **18 条**全 PASS（button.present / single-view / split、command.exists / in-view / shortcut-ctrl-w、last-pane.active / keeps-one 等） |
| **四容器矩阵** | **4/4 PASS**：`centos:7`（glibc **2.17**）/ `ubuntu:24.04` / `rockylinux:9` / `ubuntu:26.04`，均未装任何 Qt/KF6/verible/slang，每行 `selfcheck + startup + lint` 全 PASS |
| **干净 worktree 认证** | **`CLEAN-BUILD-QT-EXIT=0`**（其内部 ctest **31/31**）；**`CLEAN-TYPECHECK-EXIT=0`**（含 `no cbindgen drift`） |
| **包内内联诊断实证**（T138） | 最终 AppImage 上实跑：stderr 出现 **`diagnostics: 1 inline annotation(s) on /tmp/bad.sv`**；**反向控制** `VEDIT_NO_ACTIVE_FILE_WATCHER=1` → analyze 行 **0**、注解行 **0**（且文件仍打开，证明控制非空洞） |
| **SIGPIPE 反向控制**（T134） | 裸 pipe：默认 → **`exit 141`**（SIGPIPE 杀进程）；`SIG_IGN` → `EPIPE` + **`exit 0`**。产品 C ABI 20 轮「会话起 → 子进程已退出 → 写请求」：忽略后 **20/20 存活**；**去掉忽略第 14 轮死于 141** |
| 交付边界 | `verify-not-shipped.sh --expect-clean` 全绿；F2 隔离含反向 403 控制 |
| FFI | Rust 服务层静态链接（`libvd_ffi` NEEDED = 0） |

证据索引：`evidence/{epipe-crash,file-open,diagnostics-package,diagnostics-inline,fold-native,fold-gutter-click,slice3-pack,runtime-deps}/`。

## 4. 未验（一条都不删）

- **真 CentOS 7 / 内核 3.10** —— 容器共用宿主内核（7.0.0），测不到 3.10。**本次就是请用户在真机验这一步。**
- **文件管理器双击 `.sv`** —— `.desktop` 的 MIME 关联已配好，但无头环境**没有真的点过文件管理器**。
- **缓冲区版诊断的包内观测** —— 包内没有 CLI 入口可以改缓冲区（已保存文件版有实证，见 §3）；产品构建里验过 17/17 + 截图。
- **GUI 观感与交互** —— 执行服务器**无显示器**；任何截图都只能来自无头渲染，**不构成人眼验收**。
- **SVN 真子进程路径** —— stub-only：宿主与打包镜像都没有 `svn`/`svnadmin`，只有 Git 端到端验证过。
- **A4.5 视图级折叠观感** —— 只有无头断言。
- **FIM ghost 多行** —— 已知限制：只预览首行。

## 5. 已知行为（不是缺陷，但会让人误解）

- **`AppRun` 在带桌面的环境里会等待输入**：C7 白名单守卫会继承桌面环境而一直等待（无输出、看起来卡死），纯容器里不出现。自动化请用干净环境（`env -i …` + stdin `/dev/null`）。**手工直接跑卡住 ≠ 产物损坏。**
- **Rust 运行时静态链接进 C++ 宿主**：`SIGPIPE` 等**进程级默认行为不会自动安装**，必须由宿主显式设置（`src/app/main.cpp` 已设 `std::signal(SIGPIPE, SIG_IGN)`，见 `AGENT.md` §11）。任何依赖 Rust 运行时初始化的新行为都必须由宿主显式设置。
- **VCS 面板**：三个读全部异步 job，GUI 线程永不跑 `git`/`svn` 子进程；无写入口。
- **FIM**：默认关闭；未配置时零网络（syscall 级证明）。
- **CI**：该分支未配置 workflow，`gh pr checks` 为 `no checks reported`。

## 6. 构建方式

```bash
bash scripts/build-qt.sh            # -> target/qt6-build/src/app/vedit-app
bash packaging/runtime/build.sh     # -> packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage
```

- 工具链 pin 在 `scripts/versions.env` / `scripts/versions-qt.env`；`appimagetool` 用仓内 vendor（sha256 `793fca0c…`，fail-closed）。
- **复现边界**：产物 `de19746c…` 绑定 `43afcab` + 当前打包脚本；任何后续源码/脚本改动都会让它失效。

## 7. 评审 verdict（交付相关）

| verdict | 结论 |
|---|---|
| [`evidence/verdicts/pack/verdict.md`](evidence/verdicts/pack/verdict.md) — T32 最终打包 | **PASS**（AC-S3.3 / AC-S0.4 / glibc ≤2.17），附 N1–N5 非阻塞 finding |
| [`evidence/verdicts/appdir-toolchain/verdict.md`](evidence/verdicts/appdir-toolchain/verdict.md) — T87 | **PASS** |
| [`evidence/verdicts/ctrl-s-highlight/t88-verdict.md`](evidence/verdicts/ctrl-s-highlight/t88-verdict.md) — T88 | **PASS** + 2 条 Low |
| [`evidence/verdicts/supporting-deliverables/verdict.md`](evidence/verdicts/supporting-deliverables/verdict.md) — T89 | **PASS**（带 findings） |
| [`evidence/verdicts/fim-reeval/verdict.md`](evidence/verdicts/fim-reeval/verdict.md) — T107 | **PASS** |

全量 verdict 索引见 `evidence/verdicts/**`。

## 8. 用户验收（AC-S3.4）

见下面 §9 的七条清单，对号入座即可。**真 CentOS 7（内核 3.10）这一步只能由你在实机完成。**

## 9. 待用户复验的七条

1. **`.f` ↔ `.v` 来回切 20 次不崩**
2. **删一个逗号（不保存）** → 编辑器行首 **✖** + 面板明细 + 状态行计数
3. **`View → Diagnostics`（Ctrl+Alt+D）** 入口存在
4. **gutter 折叠标记双向可用、不崩**
5. **中文注释 / 文件名不方块**
6. **`vd-app <file>` 与双击 `.sv` 能打开**
7. **56MB 文件从卡死 → 秒开**

## 10. 红线

**未合并、未打 tag、未发 Release。** 交付出口 = 本 PR + 该 AppImage。
