# VEdit 0.2.2 发布准备报告（版本冻结 + 五项检查 + 产物验证）

生成时间：2026-11-10T00:05+08:00（本报告由 Senior Engineer 在 `release/0.2.2` 上执行）
工作区：`/home/haroldzhao/MyProj/DshProj/VEdit`

**结论**：版本已冻结为 `0.2.2`；`docs/RELEASING.md` §6 的五项检查全部通过；
交付物 `packaging/runtime/out/VEdit-0.2.2-x86_64.AppImage`（138,811,896 B，
sha256 `2eae6145e399ccbf9ca226c3e4c9759f6016a650643d512b9462737cb98b1cca`）已产出并通过
sha256 / `--appimage-version` / `--appimage-extract` / 包内二进制一致性 / 无头启动验证。
**打包走的是脚本默认路径**（appimagetool 自行下载 944632 B 的 type2 runtime），未使用手工
runtime 兜底路径。

> 提交状态：船长已在我执行期间把冻结提交并合入 `main`：发布提交 `5e71ef3 chore(release): 冻结版本 0.2.2`
> → merge `f2fda0a`（`main`）→ annotated tag **`v0.2.2`**（`cae9aee` → `f2fda0a`）。`main` 的树中
> `Cargo.toml`/`CMakeLists.txt`/`main.cpp` 均为 `0.2.2`，`evidence/release-0.2.2/logs/01-05` 已在树中
> （期间仓库做过一次 purge/replay，purge 前的等价提交是 `e4cf68a`/`d371d0b`）。
> **当前工作区检出的不是 `main`**：父 agent 已切到 `tmp/clean`（purge 用的干净重建分支，其树仍是
> `0.2.1`）。本报告与 `logs/06-09` 是在该检出上生成的**未跟踪**文件，需要船长按发布分支/`main` 补提交。

---

## 1. 版本改动清单（文件:行 + 为什么它是"当前版本声明"）

判定口径：**声明"当前产品版本"的值**才改；历史示例、历史测量、证据、依赖版本一律不改。

| # | 文件:行 | 旧 → 新 | 为什么是"当前版本声明" |
|---|---|---|---|
| 1 | `Cargo.toml:25` | `0.2.1` → `0.2.2` | `[workspace.package] version`。**产物名的来源**：`packaging/runtime/build-appimage.sh:53` 用 `awk -F'"' '/^version = /' "$ROOT/Cargo.toml"` 取它，直接决定 `VEdit-<ver>-x86_64.AppImage`。 |
| 2 | `CMakeLists.txt:19` | `0.2.1` → `0.2.2` | 根项目 `project(VEdit VERSION …)`，是产品的 CMake 版本声明（`docs/BUILD.md` §7.1 把它列为同一版本集合的一部分）。树内无 `${PROJECT_VERSION}` 消费者（grep = 0 命中），属声明性来源。 |
| 3 | `src/terminal/CMakeLists.txt:16` | `0.2.1` → `0.2.2` | 终端子项目的 standalone `project(VEditTerminal VERSION …)`，同一产品版本集合（BUILD.md §7.1 明确点名）。 |
| 4 | `src/app/main.cpp:1861` | `0.2.1` → `0.2.2` | `QCoreApplication::setApplicationVersion(QStringLiteral("0.2.2"))` —— 字面量，`--version` 的输出源。实测 `vedit-app 0.2.2`。 |
| 5 | `cmake/smoke/qt6_window_smoke.cpp:133` | `0.2.1` → `0.2.2` | M0 smoke 二进制的 `setApplicationVersion` 字面量（BUILD.md §7.1 明确列为"同一个值"）。非门禁断言，改它不影响任何测试判定。 |
| 6 | `docs/BUILD.md:156,160` | `0.2.1` → `0.2.2` | "代码、构建与文档现在统一为 **0.2.2**" 与产物名 `VEdit-0.2.2-x86_64.AppImage`——事实陈述"当前版本"。 |
| 7 | `packaging/runtime/README.md:101` | `0.2.1` → `0.2.2` | "**Delivery baseline:** the AppImage is `out/VEdit-0.2.2-…`"——当前交付物声明。 |
| 8 | `packaging/runtime/hostmatrix.sh:16` | `0.2.1` → `0.2.2` | 交付物主机矩阵脚本的默认输入路径（现役工具，不是历史证据）。 |
| 9 | `packaging/runtime/verify/STEPS.md:12,22,28,41,109,133` | `VEdit-0.2.1-…` → `VEdit-0.2.2-…` | 现行 CentOS 7 人工验收步骤里的交付物文件名（照抄即可用）。 |
| 10 | `packaging/runtime/verify/verify-appimage-centos7.sh:5,88,93` | `VEdit-0.2.1-…` → `VEdit-0.2.2-…` | 同一验收脚本的注释、默认查找名与失败期望串（现役工具）。 |
| 11 | `Cargo.lock`（11 个 workspace member） | `0.2.1` → `0.2.2` | **生成物**：改 `Cargo.toml` 后 cargo 自动重写（`git show e4cf68a -- Cargo.lock` = 11 增 11 删，只有 workspace member 的 `version` 行）。不是手工声明。 |

### 1.1 明确**不改**的出现点（附理由）

| 文件:行 | 内容 | 不改的理由 |
|---|---|---|
| `docs/RELEASING.md:74,79,80,127` | `v0.2.1-rc.1` / `git tag -a v0.2.1` / `release/v0.2.1` | 历史**命令示例**，任务明令不改。 |
| `docs/BUILD.md:175` | "0.1.1 时期同机同日实测；0.2.1 重建后以实际文件为准" | 历史测量记录（旧管线 vs Qt 交付物的体积对比），不是当前版本声明。 |
| `packaging/tarball/README.md:11` | "它不代表 0.2.1 的产物" | 对旧 tarball 管线归属的历史陈述，与 0.2.2 无关。 |
| `tests/packaging/hostmatrix.sh:30,48` | 注释里的 "0.2.1 bump"、fallback 路径 | **测试文件，禁止修改**；且主路径是"按 mtime 选最新 `VEdit-*.AppImage`"，0.2.1 只是无产物时的报错兜底串，不影响 0.2.2。 |
| `src/workspace/settings/settings_selftest.cpp:549` | `// AC-10.2.1:` | 需求编号（AC-10.2.1），不是版本号。 |
| `Cargo.lock` 依赖版本（`libc 0.2.191`、`version-compare 0.2.1` 等）、`evidence/**`、`probes/**`、`build/gui-spike/**`、`crates/**/evidence/**`、`packaging/tarball/evidence/**`、`docs/rust-crates.txt`、`THIRD_PARTY_NOTICES.md` | 第三方 crate 版本 / 历史证据 / 依赖清单 | 与产品版本无关，按任务要求不动。 |

### 1.2 来源链（为什么不是"只改一处"）

**不是单一来源**，是三处独立声明：

1. **产物名** ← `Cargo.toml:25` → `build-appimage.sh:53` 的 awk → `VEdit-<ver>-x86_64.AppImage`；
2. **CMake 项目版本** ← `CMakeLists.txt:19`（+ 子项目 `src/terminal/CMakeLists.txt:16`），树内无消费者；
3. **应用内版本** ← `src/app/main.cpp:1861` 字面量 → `--version`（smoke 另有自己的字面量）。

所以三处代码/构建点都必须改；改 `Cargo.toml` 后 cargo 自动同步 `Cargo.lock`。

残留 `0.2.1` 的完整清单见 `logs/09-version-grep.txt`（除上述历史/依赖/证据外为 0）。

---

## 2. 发布前检查单（§6，逐条命令 + 观测结果）

所有原始日志在 `evidence/release-0.2.2/logs/`；各命令均在仓库根目录执行。

| # | 命令 | 观测结果 | 退出码 | 日志 |
|---|---|---|---|---|
| 1 | `bash build/gates/qt6-cxx.sh` | `100% tests passed, 0 tests failed out of 62`；`Total Test time (real) = 88.67 sec`；xcb + 软件渲染冒烟 `M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ... window=1 editor=1 document=1 verilog=1`；`[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke` | 0 | `logs/01-gate-qt6-cxx.log` |
| 2 | `bash scripts/gates.sh` | `PASS[lock]: no tree-sitter/openssl/native-tls; gtk-rs 0.18 generation; sourceview4 0.5 line`；宿主无 GTK 开发文件 → 编译门禁在 pinned CentOS 7 镜像内执行；53 条 `test result` 合计 **581 passed / 0 failed**；clippy `-D warnings` 无 error/warning；`GATES PASS (host: dependency policy locally + compile gates in the pinned image)` | 0 | `logs/02-gate-rust.log` |
| 3 | `bash build/gates/lrelease.sh` | `Generated 1278 translation(s) (1278 finished and 0 unfinished)`；`counts: 1278 translation(s) (1278 finished, 0 unfinished, 0 untranslated)`；qm 135472 B sha256 `e4860bce…`；`[lrelease] PASS` | 0 | `logs/03-gate-lrelease.log` |
| 4 | `bash build/gates/docs.sh` | `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md` | 0 | `logs/04-gate-docs.log` |
| 5 | `bash packaging/runtime/build.sh` | `steps_failed=0`；`appimage=…/VEdit-0.2.2-x86_64.AppImage`；`appimage_bytes=138811896`；`appimage_sha256=2eae6145…`；`whitelist=Markdown,SystemVerilog,Tcl,VHDL,Verilog`；`appdir_files=359`；F2 隔离 `F2-ISOLATION PASS`（AppDir + AppImage，正/反向控制均绿）；CentOS 7 机制探针 `CHECK PASS CentOS 7 mechanism probe`、`CHECK PASS bundled VCS toolchain in the clean container`；`BUILD: F2 runtime OK` | 0 | `logs/05-gate-packaging.log` |

补充观察（第 1 项内）：`target/qt6-build/src/app/vedit-app --version`（pinned 镜像内）输出
`vedit-app 0.2.2` —— 版本冻结在产物里生效。

---

## 3. 产物

| 项 | 值 |
|---|---|
| 名称 | `packaging/runtime/out/VEdit-0.2.2-x86_64.AppImage` |
| 大小 | 138,811,896 字节（`appimage_bytes` 与 `stat -c%s` 一致） |
| sha256 | `2eae6145e399ccbf9ca226c3e4c9759f6016a650643d512b9462737cb98b1cca` |
| 校验文件 | `packaging/runtime/out/SHA256SUMS`（内容 = 上述 hash + 文件名；由 `build.sh` 生成） |
| 打包路径 | **脚本默认路径**：`build-appimage.sh` 用 vendored `appimagetool`（sha256 `793fca0c…` 校验通过）→ 运行时自行下载 type2 runtime（日志 `Downloaded runtime binary of size 944632`）→ squashfs 359 文件 → 嵌入 ELF。**未触发 HTTP 504，未走手工 `--runtime-file` 兜底**。 |
| 包内二进制 | `bin/vd-app` sha256 `8a4212d2fc6dfb097a3bc4cd8456609f6b396c483c3a9c8822ff356add902449` = `target/qt6-build/src/app/vedit-app`（同一 hash） |

---

## 4. 产物验证（`logs/06-artifact-verify.log`）

| 检查 | 命令 | 结果 |
|---|---|---|
| 与 SHA256SUMS 一致 | `cd packaging/runtime/out && sha256sum -c SHA256SUMS` | `VEdit-0.2.2-x86_64.AppImage: OK`，rc=0 |
| `--appimage-version` | `./VEdit-0.2.2-x86_64.AppImage --appimage-version` | rc=0；type2 runtime 在 **stderr** 打印 `AppImage runtime version: https://github.com/AppImage/type2-runtime/commit/fedfad5`（该 runtime 的版本输出形式就是这一行，不是半角版本号） |
| squashfs 可读 | `--appimage-extract`（临时目录） | rc=0，解出 359 个文件，`squashfs-root/bin/vd-app` 可执行 |
| 包内 `vd-app` 一致 | `sha256sum squashfs-root/bin/vd-app target/qt6-build/src/app/vedit-app` | 两行同 hash `8a4212d2…`，`hash_match=yes` |
| 包内启动（无头） | `HOME=<fresh> QT_QPA_PLATFORM=offscreen squashfs-root/AppRun --hold 500` | rc=0，耗时 1 s，窗口创建后按定时器退出，无崩溃 |
| 包内版本 | `HOME=<fresh> … bin/vd-app --version`（pinned loader） | `vedit-app 0.2.2`，rc=0 |

---

## 5. 未验证项 / 范围外发现

1. **真实 CentOS 7 机器 / 真实桌面 GUI 未验证**：第 5 项里的 CentOS 7 是**容器机制探针**
   （共享宿主内核），不是 3.10 内核真机；无头启动用的是 `offscreen`。
2. **xcb 路径的"打包产物"未在宿主验证**：宿主没有 `Xvfb`/`xvfb-run`（`command -v` 为空），
   `xcb` 冒烟只在第 1 项的 pinned 镜像里跑过（`M0-SMOKE ok=1 platform=xcb`，针对构建树而非
   AppImage）。任务的允许条件是"offscreen 或 Xvfb"，本次用 offscreen 完成。
3. **范围外发现（已存在，非本次冻结引入）**：交付 AppImage 在**带 KDE 配置的真实 `$HOME`**
   （`/home/haroldzhao`）下、`QT_QPA_PLATFORM=offscreen` 启动会挂起（`--version` 都不返回，
   rc=124）；把 `XDG_CONFIG_HOME` 指到空目录或换 fresh `HOME` 就正常（rc=0，打印 `vedit-app 0.2.2`）。
   **0.2.1 旧产物在同一 HOME 下逐字复现**（`vedit-app 0.2.1` 同样挂起；fresh HOME 正常），
   因此不是 0.2.2 版本冻结导致的回归。strace 显示挂起前最后打开的是宿主 Qt 插件
   `/usr/lib/x86_64-linux-gnu/qt6/plugins/iconengines/libqsvgicon.so` 与宿主 `libQt6Svg.so.6`
   （随包 `plugins/` 只有 platforms/xcbglintegrations，不含 iconengines；Qt 回落到编译期插件目录）——
   **这是候选原因，未做根因证明**。证据：`logs/07-launch-home-finding.log`。修复不在本任务范围。
4. **未跑 lupdate**（任务禁止）；未做 `git commit` / `git tag` / `git push`（船长统一做：发布提交
   `5e71ef3` + merge `f2fda0a` + tag `v0.2.2` 已在 `main` 上）。
5. **Release 上传 / GitHub Release notes** 不在本次任务内。

---

## 6. 证据路径

```
evidence/release-0.2.2/
├── REPORT.md                       # 本报告
├── RELEASE-NOTES.md                # 船长撰写的发布说明（已提交）
└── logs/
    ├── 01-gate-qt6-cxx.log         # 门禁 1（62/62 + xcb 冒烟），EXIT=0
    ├── 02-gate-rust.log            # 门禁 2（fmt/clippy/test/lock），EXIT=0
    ├── 03-gate-lrelease.log        # 门禁 3（1278/0/0），EXIT=0
    ├── 04-gate-docs.log            # 门禁 4（docs PASS），EXIT=0
    ├── 05-gate-packaging.log       # 门禁 5（steps_failed=0 + F2 + CentOS7），EXIT=0
    ├── 06-artifact-verify.log      # 产物 5 项验证（sha256/version/extract/hash/launch）
    ├── 07-launch-home-finding.log  # 真实 KDE HOME 挂起 + 0.2.1 对照 + strace 尾部
    ├── 08-version-diff.txt         # 冻结提交 e4cf68a 的逐文件 diff（含 Cargo.lock 生成行）
    └── 09-version-grep.txt         # 残留 0.2.1 清单 + 新 0.2.2 清单
```

## 7. 工作区状态说明

- 任务开始时工作区已有未提交的门禁日志改动（`build/evidence/`、`evidence/ffi-abi/`、
  `evidence/m0-build/`、`evidence/slice3-pack/` 等，来自此前构建）；本次跑门禁又重写了它们。
  这些日志已随船长的提交进入 `main`。
- **当前检出是 `tmp/clean`**（父 agent 在 00:03 为仓库 purge/replay 切出的干净重建分支，树仍是
  `0.2.1`），`main`（`f2fda0a`，tag `v0.2.2`）才带着 0.2.2 冻结。本报告与 `logs/06-09` 是
  purge 之后在 `tmp/clean` 工作区重建的未跟踪文件；**请船长把它们补到发布分支/`main`**。
- 产物在 `packaging/runtime/out/`（被 `.gitignore` 忽略，按发布纪律不进 git）；产物与
  `target/qt6-build/src/app/vedit-app` 的 hash 在报告生成时再次复核，未变。
