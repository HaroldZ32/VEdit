# 批 7c 乙 — AppImage 打包端到端重跑证据（AC-7.4）

任务 **T31** · cell `cell-20261103T123639841Z` · 冻结契约
`.mpd/cellwork/cell-20261103T123639841Z/contracts/delivery-evidence.md` · requirements rev3 治理
（随包 git 2.53.0 + svn 1.14.5，bundled > PATH > env）。**纯证据任务：不改产品代码、不改 packaging 语义。**

结论：**PASS**。当前树枝上 `packaging/runtime/build.sh` 全链 **exit 0 / `steps_failed=0`**；
AppImage `130,181,624` B · sha256 `6076ff87…`；AppDir `347` 个文件；干净 CentOS 7.9.2009（glibc 2.17）
容器内 `AppRun --selfcheck` = `ok=1 checks=290 failures=0`、`git 2.53.0`、`svn 1.14.5`、svnadmin 不随包、
`VCS-TOOLCHAIN … source=bundled`、普通启动（无 `--project`）把持久化根送到四个面板；七道门禁全绿。
视觉评审见 `evidence/xcb-visual-2/README.md`（PASS，8 条非阻断观察项）。

---

## 1. 环境

| 项 | 值 |
|---|---|
| 工作树 | `/home/haroldzhao/MyProj/DshProj/VEdit`，`git head=4e2d0a5`（main） |
| 树内既有未提交改动 | 323 个（本批开工前已在；**本任务未编辑任何产品/packaging 文件**，见 §6） |
| 采集时间 | `2026-11-04T14:22:30+08:00` 起，14:22:57 结束 |
| 构建镜像 | `vedit-qt6-build:local` id `sha256:0f4b479e9324f547f4e3ff83d65d6bd42833d1e1380f590f3ae3703681b52ef8`，2,183,723,690 B |
| Rust 门禁镜像 | `vedit-build:local`（CentOS 7.9，glibc 2.17，Rust 1.99.0） |
| 干净容器 | `docker.m.daocloud.io/library/centos@sha256:be65f488b7764ad3638f236b7b515b3678369a5124c47b8d32916d6487418ea4`（CentOS Linux release 7.9.2009 (Core)，glibc 2.17） |
| appimagetool | vendored `packaging/appimage/vendor/appimagetool-x86_64`，sha256 `793fca0ce4bcc309dc65d3f66669f58e3755643e4dadbe03932e7b436128494f`（脚本内 pinned 校验通过） |

## 2. 打包全链（一条命令）

```bash
VEDIT_PACK_EVIDENCE="$PWD/evidence/packaging-2" bash packaging/runtime/build.sh
# 观测：PACKAGING_EXIT=0；BUILD: F2 runtime OK；99-summary.txt: steps_failed=0
# 原始控制台：evidence/packaging-2/00-run.log（59,654 B，含每一步的 exit code）
```

| 步骤 | 结果 | 关键观测 | 日志 |
|---|---|---|---|
| preflight | PASS | 镜像/`vd-app`/KSH 探针/F2 树/2 个 XML/verible+slang/appimagetool 全部就位；`vd-app sha256=2d51781f…` | `00-preflight.log` |
| 1/7 assemble AppDir（F2：自建库 + 白名单） | exit 0 | `assemble exit=0`，AppDir 建成 | `10-assemble.log` |
| 1b/7 交付不变量 | PASS | 12/12 项 `OK`（`bin/vd-app`、xcb/offscreen 平台插件、katepart、verible/slang、`bin/git`、`bin/svn`、`usr/lib/vcs/libexec/{git,svn}`、`vcs-toolchain.txt`、syntax XML）；静态 FFI：`libvd_ffi.so NEEDED entries: 0`，`vd_* symbols: 82` | `15-delivery-invariants.log` |
| 2/7 licence / not-shipped | exit 0 | `summary: pass=8 fail=0 expected_red=0` | `20-not-shipped.log` |
| 3/7 self-check（AppDir，干净环境） | exit 0 | `SELFCHECK PASS`；`strict-GPL definitions embedded: 0` | `30-selfcheck-appdir.log` |
| 4/7 F2 隔离（模拟 KDE 宿主 + 用户定义） | exit 0 | `appdir: defs=2`（== 白名单）、宿主/用户探针不可见、反向对照复现泄漏（defs=403）；`pass=5 fail=0` | `40-f2-isolation-appdir.log` |
| 5/7 构建 AppImage | exit 0 | `appimagetool exit=0`；sha256 校验通过；`SOURCE_DATE_EPOCH=1793594952`（HEAD 提交时间） | `50-appimage.log`、`SHA256SUMS` |
| 6/7 AppImage 自检 + F2 隔离 | exit 0 | `SELFCHECK PASS`；AppImage `pass=10 fail=0`；`F2-ISOLATION PASS` | `60-selfcheck-appimage.log`、`61-f2-isolation-appimage.log` |
| 7/7 CentOS 7.9 机制探针 | PASS | `host: CentOS Linux release 7.9.2009 (Core)`、`glibc: ldd (GNU libc) 2.17`、`APP-SHELL-SELFTEST ok=1 checks=290 failures=0`、`centos7_smoke_exit=0`、`git version 2.53.0`、`1.14.5`、`svnadmin=absent`、`centos7_vcs_exit=0`；`CHECK PASS bundled VCS toolchain in the clean container` | `70-centos7-probe.log` |
| 收尾 | — | AppDir 文件清单 347 行；内嵌定义扫描 `71-embedded-defs.log`；`runtime-manifest.txt`、`RUNTIME-NOTICES.txt` | `appdir-files.txt` 等 |

## 3. 产物事实（本次运行，非旧基线）

| 项 | 值 |
|---|---|
| AppImage | `packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage` |
| AppImage 大小 | **130,181,624 B**（≈124.1 MiB） |
| AppImage sha256 | **`6076ff87f4ec60b54005790295e62c308b309b1df50bc50928fc934182e89edd`** |
| AppDir 文件数 | **347**（`appdir-files.txt` 行数） |
| 白名单 | `SystemVerilog,Verilog` |
| 自建 KSyntaxHighlighting | `libKF6SyntaxHighlighting.so.6` sha256 `aac288a16e508d529e9ae275daf4e3766fe4309a68c2b93ed16083d252785fa6`（= F2 安装树中的 6.24.0） |
| 随包工具链 | verible-verilog-ls `07bf6b45…`、slang `63cb60a0…`（= preflight 哈希） |
| 旧基线对照（12:03 树） | `appimage_bytes=129952248`、sha `204b6645…`；本批因当前树内容/重建而变化（+229,376 B），旧证据目录 `evidence/slice3-pack/` **未被本次覆盖** |

## 4. 干净容器 E2E 补充（build.sh 第 7 步之外，`72-clean-container-e2e.log`）

build.sh 的 7/7 只验 `--selfcheck` 与两个 VCS 客户端的版本；本脚本补上 dispatch 要求的
**可观测 `VCS-TOOLCHAIN` 行**与**普通启动（无 `--project`）持久化根到达面板**：

```bash
bash evidence/packaging-2/scripts/clean-container-e2e.sh
# 观测：CLEAN-CONTAINER-E2E steps_failed=0 / CLEAN-CONTAINER-E2E PASS / SCRIPT_EXIT=0
```

容器内实测（`/app` = 从 AppImage 解出的 AppDir，只读挂载；`HOME=/tmp`，offscreen）：

```
CentOS Linux release 7.9.2009 (Core)
ldd (GNU libc) 2.17
git version 2.53.0
1.14.5
svnadmin=absent

session.json: {"version":2,"project":"/tmp/proj","panels":["file-tree","search","vcs","module-tree"]}
ordinary_start_exit=0
KSH-GUARD PASS whitelist=SystemVerilog,Verilog
filetree: installFileTree projectRoot='/tmp/proj' dockInserted=0 placeholderReplaced=0
module-tree: installed (root='/tmp/proj' state='/tmp/state')
VCS-TOOLCHAIN git=/app/usr/lib/vcs/bin/git version=2.53.0 svn=/app/usr/lib/vcs/bin/svn version=1.14.5 source=bundled git_source=bundled svn_source=bundled
search: root='/tmp/proj' (ready)
vcs: root='/tmp/proj'
TOOLCHAIN source=verible:bundled slang:bundled missing=[] aux source=slang-server:missing verible-format:bundled verible-lint:bundled missing=[slang-server] local=[]

APP-SHELL-SELFTEST ok=1 checks=290 failures=0 state=/tmp/vedit-app-sTwuWe/state
selfcheck_exit=0
```

12 条断言全 PASS：容器版本/glibc、git 2.53.0、svn 1.14.5、svnadmin 不随包、普通启动 exit 0、
`VCS-TOOLCHAIN … source=bundled`、四条 lane 的根行、容器内 `--selfcheck`。

## 5. 七道门禁（最终树，本次实测）

| 门禁 | 命令 | 观测 | 日志 |
|---|---|---|---|
| 1. 构建 build-qt（含 ctest + xcb smoke） | `bash scripts/build-qt.sh` | **exit 0**；`100% tests passed, 0 tests failed out of 37`；容器内 configure/build/ctest/xcb 冒烟通过 | `gates/01-build-qt.log`（含 `BUILD_QT_EXIT=0`） |
| 2. ctest | 包含在上一步 | **37/37**，`Total Test time (real) = 53.11 sec` | `gates/02-ctest.log`（`CTEST_EXIT=0`） |
| 3. 产品 selfcheck | `vedit-app --selfcheck --state-dir /tmp/sc-7c`（pinned 镜像，offscreen） | **exit 0**，`APP-SHELL-SELFTEST ok=1 checks=290 failures=0` | `gates/03-selfcheck.log` |
| 4. clippy | `cargo clippy --workspace --all-targets -- -D warnings`（`vedit-build:local`） | **exit 0**，`Finished dev profile … in 40.49s` | `gates/04-clippy.log` |
| 5. fmt | `cargo fmt --all -- --check`（`vedit-build:local`） | **exit 0**（无输出） | `gates/05-fmt.log` |
| 6. docs | `bash build/gates/docs.sh` | **exit 0**，`[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md` | `gates/06-docs.log` |
| 7. lrelease | `/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts`（pinned 镜像） | **exit 0**，`Generated 936 translation(s) (936 finished and 0 unfinished)` | `gates/07-lrelease.log` |

一键重跑：`bash evidence/packaging-2/scripts/seven-gates.sh`（观测 `SEVEN-GATES PASS` / `steps_failed=0`）。

## 6. 边界、未证明与写域说明

1. **写域**：本任务新增/修改的交付物只有 `evidence/packaging-2/**` 与 `evidence/xcb-visual-2/**`。
   `packaging/runtime/**` 的 git 改动（`.gitignore`、`AppRun`、`assemble-appdir.sh`、`build.sh`、
   `selfcheck.sh`）**全部是批 6a 的既有改动**（mtime 11:42–12:03，早于本任务 14:16 开工），本任务一个字节未改；
   `packaging/runtime/out/**`、`target/**` 是 gitignore 的构建产物（`out/` 在 `packaging/runtime/` 写域内）。
2. **门禁脚本的自写日志**：`scripts/build-qt.sh` 按其固定路径写 `evidence/m0-build/build-logs/*` 与
   `evidence/ffi-abi/logs/*`（这两个目录在本任务开工前就已是 modified；本任务用同一门禁命令重跑，
   因此它们的 mtime/内容随本次构建更新）。本报告的权威副本是 `gates/01-build-qt.log`。
3. **容器内核**：干净容器与宿主共享内核（非 CentOS 7 的真实 3.10 内核），故“机制探针”成立而非内核兼容性认证；
   内核实机验证属 T12/T83 的矩阵。
4. **CentOS 7 容器内只做了 offscreen**：容器没有 xcb GUI 依赖；真实 X 服务器 + xcb 的成品截图在
   pinned 构建镜像内完成，见 `evidence/xcb-visual-2/`（那里的 `--require-xcb` 保证不是 offscreen 冒充）。
5. **未覆盖**：AppImage 的 zsync/增量更新、桌面集成（`.desktop`/MIME）、签名；本链只产出并验证
   `*.AppImage` 本体与 `SHA256SUMS`。
6. **AppImage 哈希随树内容与 `SOURCE_DATE_EPOCH`（HEAD 提交时间）变化**：本报告的 sha 只对应
   `4e2d0a5` 树的这次运行；换树后必须重跑本命令重取，不能沿用旧 sha。

## 7. 文件索引

```
evidence/packaging-2/
├── README.md                      # 本文件
├── 00-run.log                     # 全链控制台（命令/各步 exit/时间戳）
├── 00-preflight.log 10-assemble.log 15-delivery-invariants.log 20-not-shipped.log
├── 30-selfcheck-appdir.log 40-f2-isolation-appdir.log 50-appimage.log
├── 60-selfcheck-appimage.log 61-f2-isolation-appimage.log 70-centos7-probe.log
├── 71-embedded-defs.log 99-summary.txt appdir-files.txt runtime-manifest.txt
├── SHA256SUMS RUNTIME-NOTICES.txt
├── 72-clean-container-e2e.log     # 干净容器 E2E（VCS-TOOLCHAIN + 普通启动）
├── scripts/
│   ├── clean-container-e2e.sh     # 上面那份日志的产生脚本（可重跑）
│   └── seven-gates.sh             # 六道剩余门禁（build-qt 单独在 gates/01）
└── gates/
    ├── 01-build-qt.log  02-ctest.log  03-selfcheck.log
    ├── 04-clippy.log  05-fmt.log  06-docs.log  07-lrelease.log
```
