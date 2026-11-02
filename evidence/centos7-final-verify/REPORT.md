# T113 — 最终 AppImage 的真机验证包（脚本 + 中文 STEPS）

- Cell: `cell-20261031T085543161Z` · Board task: **T113** · 日期: 2026-11-01（Asia/Shanghai）
- 交付物（被测对象）: `packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage`
  **95,349,240 B** · sha256 `e7ae067fe66c8e271ba72da974795a7ee3db0efa59bda45079b5a048b4a09558`（与 `SHA256SUMS` 一致）
- 本任务只新增两个文件（+ 本证据目录），**未改 AppImage、未改 `src/**`、未改 `packaging/runtime/*.sh`**。

## 1. 交付内容

| 文件 | 用途 |
|---|---|
| `packaging/runtime/verify/verify-appimage-centos7.sh` | 单文件验证脚本：16 项检查，逐项 `RESULT PASS|FAIL|SKIP`，生成 `verify-report-<host>-<时间>.txt` |
| `packaging/runtime/verify/STEPS.md` | 中文操作说明（scp / chmod / 一条命令 / 看哪几行 / 手工栏 / 常见问题） |

**用户只需拷两个文件**到 VM（`SHA256SUMS` 可选，多一条交叉校验）：
`VEdit-0.1.1-x86_64.AppImage` + `verify-appimage-centos7.sh`，放同一目录后
`chmod +x` 并 `bash verify-appimage-centos7.sh`。不需要 sudo、不需要装任何包、不需要联网。
（与 T83 的 104 MB 开发验证包完全分开：旧的 `build/centos7-verify/` 未被使用，本包只带交付物本身。）

## 2. 脚本自测（stock centos:7 容器，`evidence/centos7-final-verify/03-selftest-final.log`）

```
# container: CentOS Linux release 7.9.2009 (Core) | kernel: 7.0.0-38-generic (host kernel, expected)
# ldd: ldd (GNU libc) 2.17
RESULT FAIL kernel.is-3.10 | expected=3.10.x ... actual=7.0.0-38-generic note=containers share the host kernel; a container run is not the real-machine answer
RESULT PASS glibc.system | actual=ldd (GNU libc) 2.17
RESULT PASS appimage.sha256 | actual=e7ae067f…09558 note=size=95349240 (expected 95349240)
RESULT PASS appimage.sha256sums | actual=SHA256SUMS row matches
RESULT PASS run.selfcheck | actual=exit=0 via EXTRACT-AND-RUN (--appimage-extract-and-run)
RESULT PASS selfcheck.app-shell | actual=APP-SHELL-SELFTEST ok=1 checks=200 failures=0
RESULT PASS selfcheck.own-xml | actual=… own-xml=[vedit-systemverilog.xml,vedit-verilog.xml] … verilog=1
RESULT PASS selfcheck.fold-975 | actual=CHECK fold.wiring.closed.hidden-975 PASS hidden=975
RESULT PASS selfcheck.toolchain | actual=TOOLCHAIN source=verible:bundled slang:bundled missing=[]
RESULT PASS extract | actual=…/squashfs-root
RESULT PASS loader.packaged | actual=… maps line with <extract>/ld-linux-x86-64.so.2
RESULT PASS loader.no-system-loader | actual=only <extract>/ld-linux-x86-64.so.2
RESULT PASS loader.packaged-libc | actual=… maps line with <extract>/lib/libc.so.6
RESULT PASS lint.real | actual=…/line_length_bad.sv:2:101-134: Line length exceeds max: 100; is: 134 [line-length]
RESULT PASS launch.project | actual=exit=0（--project <生成的 .f> --hold 300）
RESULT PASS launch.project-files | actual=effective file set from the project model: 1 file(s)
SUMMARY: PASS=15 FAIL=1 SKIP=0
SCRIPT_EXIT=1
```

**唯一的 FAIL 是 `kernel.is-3.10`，这是预期的**：容器与宿主共享内核，只有 CentOS 7 VM 能给出 3.10。
`SCRIPT_EXIT=1` 正是这个 FAIL 的结果（脚本在 FAIL>0 时返回非零）。本机（Ubuntu 系，内核 7.0）复跑结果相同
（`04-selftest-host.log`：`PASS=15 FAIL=1 SKIP=0`，唯一 FAIL 也是 kernel）。

## 3. 负向对照（`06-negative-controls.log`）——证明脚本不会“假 PASS”

| 场景 | 结果 |
|---|---|
| AppImage 只有前 1 MiB（传输截断） | `appimage.sha256` FAIL（实际 sha `8e9bf68d…`）、`run.selfcheck`/`extract` FAIL、依赖项 SKIP，`SUMMARY: PASS=1 FAIL=4 SKIP=10`，exit 1 |
| 目录里没有 AppImage | `appimage.found` FAIL，`SUMMARY: PASS=0 FAIL=1 SKIP=0`，exit 1 |
| 有 AppImage、没有 SHA256SUMS | `appimage.sha256sums` SKIP（写明“可选文件不存在”），其余不变：`PASS=14 FAIL=1 SKIP=1`（FAIL 仍是 kernel） |

## 4. 每条检查取的是什么证据（不是“看起来像”）

| # | 检查 | 证据 |
|---|---|---|
| 1 | `kernel.is-3.10` | `uname -r` 必须 `3.10.*` |
| 2 | `glibc.system` | `ldd --version` 原文（CentOS 7 为 2.17） |
| 3 | `appimage.sha256`(+`sha256sums`) | `sha256sum` 与脚本内置期望值、以及与同目录 `SHA256SUMS` 行比对 |
| 4 | `run.selfcheck` | 依次尝试 `./X --selfcheck`（FUSE）→ `--appimage-extract-and-run` → `APPIMAGE_EXTRACT_AND_RUN=1`，第一个成功者写入报告（本次为 extract-and-run） |
| 5 | `selfcheck.app-shell` / `own-xml` / `fold-975` | 从**真实自检输出**里抓原文行：`APP-SHELL-SELFTEST ok=1 checks=200 failures=0`、`own-xml=[vedit-systemverilog.xml,vedit-verilog.xml] … verilog=1`、`CHECK fold.wiring.closed.hidden-975 PASS hidden=975` |
| 6 | `loader.packaged` / `no-system-loader` / `packaged-libc` | 启动真实进程后读 `/proc/<pid>/maps`：包内 `ld-linux-x86-64.so.2` 在、包外没有任何 `ld-linux`、包内 `lib/libc.so.6` 在 |
| 7 | `lint.real` | 包内 `bin/verible-verilog-lint --rules=line-length` 对脚本生成的 134 字符行报出 `[line-length]` 诊断（linter exit=1 属“发现违规”，预期） |
| 8 | `launch.project`(+`files`) | `AppRun --project <生成的 .f> --hold 300` 退出 0，且 `effective file set …: 1 file(s)`（生成的 filelist 真被展开） |
| 9 | `SKIP` 语义 | 做不了的检查（如解包失败后的 loader/lint/launch）显式 SKIP 并在 note 写原因，绝不默认 PASS |

## 5. 自测中发现并已修正的一个真实陷阱

在本机（非目标机、带桌面环境）直接运行 AppImage 时，`AppRun` 的 C7 白名单守卫（`vedit-ksh-env-probe`）
会因为继承了桌面环境而**一直等待**，表现为“程序无输出卡住”。CentOS 7 容器里不出现。
修正：脚本对所有 AppImage 运行使用**干净环境**（`env -i PATH=… HOME=… TMPDIR=… QT_QPA_PLATFORM=offscreen
LANG=C.UTF-8`）并把 stdin 接到 `/dev/null` —— 这与 `packaging/runtime/selfcheck.sh` 的做法一致。
修正后本机复跑 15 PASS/1 FAIL（唯一 FAIL 仍是 kernel），与容器一致。

## 6. 未验证 / 边界（如实说明）

- **真机结果还没有**：本任务只交付“用户一键跑”的包，并在容器/本机自测；`kernel.is-3.10` 的 PASS
  **必须**由用户在他的 CentOS 7 VM 上跑出来。不要把本目录的容器结果当成真机结果。
- **直接 FUSE 模式未在本环境验证**：自测环境没有 `/dev/fuse`/`fusermount`，所以走的是
  `--appimage-extract-and-run`。脚本仍会先试 FUSE 路径并在报告里写明“用了哪条”；VM 上若装了
  `libfuse2` 则可能走 FUSE 路径（是否成功以报告为准）。
- **带窗/双击的视觉与交互**是用户可做项（STEPS.md §6 的手工栏，不计入 SUMMARY）。
- 脚本假定 VM 具备 coreutils（`uname`/`ldd`/`sha256sum`/`awk`/`sed`/`grep`/`wc`/`mktemp`/`timeout`），
  CentOS 7 最小安装即满足；缺失时对应检查会 FAIL 并在 note 说明，不会静默通过。

## 7. 证据清单（`evidence/centos7-final-verify/`）

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 时间、HEAD、镜像 id、被测 AppImage 的 size/sha256、本任务文件 sha256、写域状态 |
| `01-selftest-centos7-container.log` | 首轮容器自测 + 负向对照的完整原始输出 |
| `03-selftest-final.log` | **最终脚本**在 stock centos:7 的自测全文（`PASS=15 FAIL=1 SKIP=0`） |
| `04-selftest-host.log` | 第二环境（本机）自测全文（同样 `PASS=15 FAIL=1`，唯一 FAIL=kernel） |
| `05-report-sample-host.txt` | 脚本实际生成的报告样例（16 条 RESULT + 完整自检输出 + 手工栏） |
| `06-negative-controls.log` | 截断/缺失/无 SHA256SUMS 三种负向对照的最终脚本输出 |
| `selftest-in-container.sh` | 可复跑的自测入口（`bash evidence/centos7-final-verify/selftest-in-container.sh`） |
