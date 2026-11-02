# T67 — M0 结案：delivery 版 AppImage 双宿主矩阵 + 宿主定义隔离

- cell `cell-20261031T014507403Z` · task **T67** · owner: Deep Worker · 日期 2026-10-31
- 被测产物：`packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage`
  （**provenance**：65,673,720 B，sha256 `74bd6aae…` —— 仅作来源记录，**不作为产物身份判据**；判据是下面的 key=value 与版本矩阵）
- 运行器：T12 的 `tests/packaging/hostmatrix.sh`（**未修改**），只换 `--appimage` 为 delivery 产物
- 本目录：`evidence/m0-hostmatrix/delivery/`（T12 的探针版证据原样保留在上一级）

## 0. 结论

| 闸门 | 环境（pinned digest） | 探针版（T12） | **delivery 版（本任务）** |
|---|---|---|---|
| A0a | CentOS 7.9.2009（glibc 2.17，stock base） | PASS | **PASS** |
| A0b | Ubuntu 24.04.5 LTS（glibc 2.39，stock base） | PASS | **PASS** |
| A0d | base-only CentOS 7（宿主无 Qt/KF6/X11/fuse2） | PASS | **PASS** |
| A0e | CentOS 7 + 无 GPU/无 GL + 软件回退 | PASS | **PASS** |

**四环境没有出现「只在 delivery 版才暴」的问题**：矩阵判定与 T12 完全一致（`pass=4 fail=0`）。
delivery 版**新增**了 T12 验不到的断言：**宿主定义隔离**（见 §2）。

## 1. 判定方式（key=value，不整行比对）

每个环境解析以下键：`M0-SMOKE` 行的 `ok=`、`window=`、`platform=`、`ktexteditor=`、`document=`、
`verilog=`、`themes=`、`builtin_valid_defs=`、`kshlib=`、`libc=`，以及 `KSH-GUARD` / `KSH-ENV defs=`。

| 环境 | ok | platform | window | ktexteditor/document | verilog | 库内定义 | libc |
|---|---|---|---|---|---|---|---|
| a0a-centos7 | 1 | xcb | 1（640×480，像素差 307200） | 1 / 1 | 1 | builtin=2（白名单） | `/app/lib/libc.so.6` |
| a0b-ubuntu2404 | 1 | xcb | 1（同上） | 1 / 1 | 1 | builtin=2 | `/app/lib/libc.so.6` |
| a0d-base-only | 1 | offscreen | 1 | 1 / 1 | 1 | builtin=2 | `/app/lib/libc.so.6` |
| a0e-no-gpu | 1 | xcb | 1（同上） | 1 / 1 | 1 | builtin=2 | `/app/lib/libc.so.6` |

原始记录：`delivery/<env>/{meta.txt,run.log,00-empty.png,10-window.png,11-after-input.png}`。

## 2. 宿主定义隔离（delivery 版最该验的一条）

夹具：宿主 `/usr/share/org.kde.syntax-highlighting/syntax/` 放 **402** 个明文 XML（400 `HostProbeNNN`
+ `HostLeakProbe` + `SystemVerilog`）；用户 `$HOME/.local/share/.../syntax/UserLeakProbe.xml`。
在**模拟装了 KDE 的宿主**上跑 delivery AppImage 的 `AppRun --selfcheck`：

| 环境 | ok | `KSH-ENV defs=` | `KSH-GUARD PASS` | 宿主标记可见 | 用户标记可见 |
|---|---|---|---|---|---|
| a0a-centos7 | **1** | **2** | **1** | **0** | **0** |
| a0b-ubuntu2404 | **1** | **2** | **1** | **0** | **0** |

反向控制（模拟一个「AppRun 忘了收窄」的坏环境，直接给 probe 传
`XDG_DATA_DIRS="$APPDIR/usr/share:/usr/share"`）：

```
KSH-ENV defs=404 hash=…      # 402 host + 1 user + 2 whitelist − 1 同名去重(SystemVerilog)
```

> 说明：401/402 的算术 —— 宿主 402 + 用户 1 + 白名单 2 = 405，减去 `SystemVerilog` 在宿主与白名单里
> 同名重复计 1 个 → **404**。反向控制复现了泄漏，说明隔离断言确实能失败（不是永真）。

原始记录：`delivery/isolation/{a0a-centos7.log,a0b-ubuntu2404.log,reverse-control.log}`。

## 3. 与探针版的实质差异

| 维度 | 探针版（T12，`524f7b07…`） | delivery 版（本任务，`74bd6aae…`） |
|---|---|---|
| KSyntaxHighlighting 库 | 发行版，**内嵌 450 定义 / 36 个严格 GPL**（含 `SystemVerilog=GPLv3+`） | **自建 F2 库，库内 0 定义**，30 主题内嵌 |
| 定义来源 | 库内嵌 | **白名单**：`SystemVerilog,Verilog`（T16 自有 LGPL），经 XDG 加载 |
| 运行时守卫 | 无 | **有**（可见集合 ≠ 白名单 → exit 2） |
| 许可闸门 | `DELIVERY_BLOCKED=yes`（EXPECTED-RED ×2） | **`pass=8 fail=0`，`DELIVERY_BLOCKED=no`** |
| 矩阵结果 | 4×PASS | 4×PASS（一致） |
| 宿主隔离 | 无法验证（本来就带着 GPL 定义，且无守卫） | **已验证**：defs=2、标记不可见、反向 404 |

## 4. 两条“看起来通过、其实没测到”的教训（T64 实修，必须留在此处供后人）

1. **自建库被发行版库覆盖**：装配时先放 F2 库，随后 `ldd` 闭包从构建镜像解析同名依赖，把发行版库
   （2,010,872 B，内嵌 36 个严格 GPL）覆盖回去 → 交付物静默退回 GPL。修复：放入后立刻标记
   `COPIED[]`，并断言 `scan-embedded-defs --expect-clean` 必 PASS。
2. **`LD_LIBRARY_PATH` 污染宿主工具**：AppRun 提前导出它 → CentOS 7 的 `/bin/cat` 用宿主 loader
   加载我们的 glibc 2.43 → `GLIBC_2.35 not found` → 守卫读到**空白名单** → 空集合相等 → **假 PASS**。
   修复：`LD_LIBRARY_PATH` 只在 exec 前导出；守卫在白名单缺失/为空时 `exit 2`（fail-closed）。

两处细节与断言见 `evidence/m0-runtime/F2.md` §4。

## 5. 复跑

```bash
# 矩阵（需要绝对路径：脚本会 cd 进 work 目录再解包 AppImage）
bash tests/packaging/hostmatrix.sh --appimage "$PWD/packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage"
# 宿主隔离（正控+反向控制，AppDir 与 AppImage 各一遍）
bash packaging/runtime/verify-f2-isolation.sh --appdir packaging/runtime/out/AppDir \
  --appimage packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage
```

## 6. 边界

- 仍沿用 T12 的边界：无窗口管理器（Xvfb 无 WM）、「输入」只证到键事件送达 + 进程存活（M0 smoke
  窗口无编辑控件，按键回显待 M1）；旧 Mesa 真机未跑；stock base 无 `libfuse2`，用提取内容验证。
- T12 的探针版证据保持原样；`SUMMARY.md` 只加了一行指引，未改其内容。
