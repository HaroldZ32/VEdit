# T83 — 真机验证包（自包含 tarball + 一条命令的 verify-centos7.sh）

- cell: `cell-20261031T085543161Z` · board task **T83**
- 采集时间：2026-10-31 23:05 – 2026-11-01 00:12 (Asia/Shanghai)
- `git rev-parse HEAD`（构建 tarball 时）：`c682694`（`git rev-parse --short HEAD`；完整值见 `build-console.txt` 头）
- 构建镜像：`vedit-qt6-build:local`（Ubuntu 26.04 / glibc **2.43** / Qt 6.10.2 / KF6 6.24.0）

> **更新（2026-10-31T15:33Z，Lead 在 GO-4 提交时补记）**
> 本文件是 **T83 当时的历史快照**。其中「clean-room 上 `highlight.verilog-definition` FAIL」的结论**已被 T84 取代**：根因是 `VEDIT_SYNTAX_DIR` 指向了 `…/syntax` 子目录（产品契约要求的是**数据根**），修法与红→绿对照见 **`evidence/appdir-syntax-dir/`**（修复后该检查 FAIL → PASS）。
> **历史结论保持原样不改**（不把历史快照改成假绿）；判读时请以 `evidence/appdir-syntax-dir/` 为准。

- 构建方式：`bash build/centos7-verify/build-tarball.sh`（可复跑，日志见 `build-console.txt`）

## 交付物

| 文件 | 说明 |
|---|---|
| `build/centos7-verify/dist/vedit-centos7-verify-c682694.tar.gz` | **要拷到真机的 tarball**（104 MB，自包含） |
| `build/centos7-verify/dist/…tar.gz.sha256` / `tarball-sha256.txt` | sha256 = `6ecf90a258c2e8b2d1b82b114bd65d54dd0c3d38b883e155592555bd83803b51` |
| `build/centos7-verify/verify-centos7.sh` | 一条命令跑完的验证脚本（打进 tarball） |
| `build/centos7-verify/STEPS.md` | 给用户的中文步骤（打进 tarball） |
| `build/centos7-verify/build-tarball.sh` | 可复跑的构建脚本（AppDir + 样例 + 脚本 + MANIFEST → tarball） |
| `build/centos7-verify/samples/verilog-demo/` | 样例工程：`demo.f` + `top.v` + `counter.v` + 故意违规的 `bad.v` |
| `report-container/verify-report-container.txt` | **在干净 stock `centos:7` 容器里对本 tarball 跑出的完整报告**（原文，未编辑） |

tarball 结构：

```
vedit-centos7-verify-c682694/
  AppDir/  bin/vedit-app  bin/verible-verilog-{ls,lint,format}  bin/slang
           bin/vedit-ksh-env-probe  bin/vedit-diagnostics-ffi-selftest
           lib/（Qt6/KF6/KSyntaxHighlighting/tree-sitter + glibc 2.43 全家）
           ld-linux-x86-64.so.2（自带 loader） plugins/platforms/ usr/share/
  samples/verilog-demo/   verify-centos7.sh   STEPS.md   MANIFEST.sha256
```

## 自测结果（这就是验证脚本的原样输出）

在**干净的 `docker.m.daocloud.io/library/centos:7`**（glibc 2.17、无 Qt6/KF6、无网络）里解压并执行
`bash verify-centos7.sh`：

```
SUMMARY: PASS=14 FAIL=2 SKIP=2
```

| 检查 | 结果 | 实际值 |
|---|---|---|
| `kernel.is-3.10` | **FAIL** | `actual=7.0.0-38-generic` —— 容器共享宿主内核，**这条 FAIL 正是设计意图**：容器里不能冒充 CentOS 7 内核 |
| `glibc.system` | PASS | `ldd (GNU libc) 2.17` |
| `package.files` / `package.manifest` | PASS | 8 个关键文件齐全；MANIFEST 211 个文件全部 sha256 一致 |
| `toolchain.verible` / `toolchain.slang` | PASS | `Version v0.0-4298-g085eef05` / `slang version 12.1.0+0` |
| `app.selfcheck` | PASS | exit 0；`EDIT-CORE-SELFTEST ok=1 checks=67` + `APP-SHELL-SELFTEST ok=1 checks=176` |
| `startup.toolchain` | PASS | `TOOLCHAIN source=verible:bundled slang:bundled missing=[]` |
| `startup.project` | PASS | `installFileTree projectRoot='<解压路径>/samples/verilog-demo'`（`.f` 工程被打开） |
| `startup.exit` | PASS | exit 0（`--hold 1500` 后正常退出） |
| `highlight.stack` | PASS | `grammar=verilog backend=1 startup-hook=1 private-dir-in-AppDir=yes` |
| `highlight.verilog-definition` | **FAIL** | `verilog=0 own-xml=[] definitions=3` —— 产品自己报告 Verilog 定义不可用（详见 §发现2） |
| `highlight.mode` | SKIP | 需要带窗会话，offscreen 观测不到 `highlightingMode`（已在报告里写明原因） |
| `lint.bundled` | PASS | 随包 verible lint 在 `bad.v` 上报 **3** 条诊断（`explicit-parameter-storage-type` / `line-length` / `always-comb`） |
| `lint.product` | SKIP | 随包的 `vedit-diagnostics-ffi-selftest` 需要自己的 FFI fixture 目录，本次返回 1；已如实标 SKIP 并给日志 |
| `symbols.app` | PASS | `vedit-app` 需 `GLIBC_2.39`；随包 libc 提供到 `GLIBC_2.43` → 包自洽 |
| `loader.used` | PASS | `/proc/<pid>/maps` 含 `<AppDir>/ld-linux-x86-64.so.2` |
| `loader.system-not-used` | PASS | maps 里没有 `/lib64/ld-linux-x86-64.so.2` |

`SCRIPT_EXIT=1` 是**预期**的：脚本按设计“有 FAIL 就返回 1”。真机上 `kernel.is-3.10` 应为 PASS，
`highlight.verilog-definition` 是否转 PASS 取决于 §发现2 的修复。

## 发现（两条，都属于"验证脚本应当暴露"的东西）

### 发现 1（已证）：容器里跑不等于 CentOS 7 跑，脚本会自己把这件事暴露出来
本报告的一切都是**容器内**实测（glibc 2.17 用户态 + 宿主 7.0.0 内核）。`kernel.is-3.10` 因此 FAIL。
**真机上这条才会 PASS** —— 这正是把包交给用户、让用户在真 VM 上跑的原因。脚本的其它 14 条 PASS
不依赖内核版本（用户态证据），但 `loader.used` 之后的"能不能在 3.10 上真跑起来"只能由真机回答。

### 发现 2（已证，包形态问题，不是脚本问题）：F2 AppDir 形态下产品报告"Verilog 定义不可用"
真机报告里 `syntax-product:` 行原文（容器内）：

```
syntax-product: grammar=verilog backend=1 registered=[systemverilog,verilog] active='verilog'
backend-available=1 private-dir='<AppDir>/usr/share/org.kde.syntax-highlighting/syntax'
definitions=3 own-xml=[] themes=30 verilog=0 systemverilog=0 kernel='KTextEditor 6.24.0'
embargoed=[] startup-hook=1
```

- `own-xml=[]`：产品在 private-dir 下没列出任何自带 XML（该目录里确实有 2 个文件，
  `package.files` 与 `vedit-ksh-env-probe` 的 whitelist 都能看到 `Verilog,SystemVerilog`）。
- `verilog=0`：按 `src/syntax/kde/ksyntax_data.cpp:161-165`，这要求
  `repository_->definitionForName("Verilog").isValid() && private_verilog_shipped`；
  产品自己给出的诊断是 “.v files fall back to plain text”。
- **推断（未证，需 highlight/packaging lane 确认）**：`AppRun` 把 `VEDIT_SYNTAX_DIR` 指向
  `<AppDir>/usr/share/org.kde.syntax-highlighting/syntax`（syntax 子目录），而仓库里跑通时该变量是
  数据根（`/vedit-build/resources`，其下有 `syntax/` 子目录）。两者相差一层，很可能就是
  `own-xml=[]` 的直接原因。**本任务写域不包含 `packaging/**` 之外的运行期代码与 `AppRun` 语义**，
  故只报告、不修改；这也解释了为什么 `highlight.stack` PASS 而 `highlight.verilog-definition` FAIL：
  栈起来了，但 `.v` 的定义解析没起来。

→ 建议下一步（给 captain 的交办）：由 highlight/packaging lane 用 `VEDIT_SYNTAX_DIR=<数据根>` 与
`<数据根>/syntax` 各跑一次 `vedit-app --selfcheck`，比对 `own-xml`/`verilog` 两字段，确认后修正 AppRun；
修好后重跑 `build-tarball.sh` 与真机验证，`highlight.verilog-definition` 应转 PASS。

## 边界（照实说）

- 没有真 CentOS 7（内核 3.10）机器：tarball 与脚本的自测都在容器里完成，**内核 3.10 未验**（脚本会把这条 FAIL 出来）。
- 未开真窗口：`highlight.mode` SKIP；xcb 插件虽随包，但容器无 DISPLAY，未验证窗口路径。
- `lint.product` SKIP：诊断 selftest 需要 fixture 目录，未在包里带（它验证的是 LSP 链路，不是本包主判据）。
- 脚本自测覆盖了 **FAIL / SKIP / PASS 三种分支**（本报告里三种都出现了），说明"失败继续跑完并如实报告"是有效的。
- tarball 未入库（104 MB，`build/centos7-verify/dist/` 由脚本重建）；证据里只放 sha256 与报告原文。
