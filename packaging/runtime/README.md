# packaging/runtime — VEdit 私有运行时（F2 形态，T64）

AppImage、`.tar.gz` 与 `.run` 从同一个 AppDir 生成。包内包含 `ld-linux`、glibc、Qt6、KF6、
KTextEditor、自建 KSyntaxHighlighting、tree-sitter 和字体。`AppRun` 经包内 loader 启动，
语法定义只从 AppDir 白名单加载。CentOS 7.9 的宿主 glibc 是 2.17。应用 ELF 的需求以本次构建的
`readelf` 结果为准。

## 构建与验证

从仓库根目录运行。准备 Qt6 产品和随包依赖的步骤见[构建指南](../../docs/BUILD.md)。

```bash
bash packaging/runtime/build.sh
(cd packaging/runtime/out/release && sha256sum --check --strict SHA256SUMS)
```

| 路径 | 说明 |
|---|---|
| `packaging/runtime/out/AppDir/` | 三种格式共用的构建中间目录 |
| `packaging/runtime/out/release/VEdit-<ver>-x86_64.AppImage` | AppImage，支持 FUSE 挂载或解包运行 |
| `packaging/runtime/out/release/vedit-<ver>-linux-x86_64.tar.gz` | 解包后从 `vedit-<ver>-linux-x86_64/AppRun` 启动 |
| `packaging/runtime/out/release/VEdit-<ver>-x86_64.run` | 临时解包后启动，`--extract DIR` 可保留解包目录 |
| `packaging/runtime/out/release/SHA256SUMS` | 三包共用的 SHA-256 清单 |

构建先检查 AppDir 的许可、自检和 F2 隔离，再校验三个包的内容、版本和无 FUSE 启动。`.run` 额外检查参数转发、失败退出码及临时目录清理。
AppImage 解包后再运行自检、隔离和 CentOS 7 用户态探针。`--no-centos-probe` 仅用于跳过本地容器探针，完整发布验证需要运行该探针。
日志默认写到 `target/runtime-pack-evidence/`，可通过 `VEDIT_PACK_EVIDENCE` 指定目录。`50-release-packages.log` 记录打包过程，
`62-verify-release-packages.log` 记录三种格式的检查，`60-selfcheck-appimage.log`、`61-f2-isolation-appimage.log` 和
`70-centos7-probe.log` 分别记录 AppImage 自检、隔离与容器探针。物理机或虚拟机验证见 [CentOS 7 操作步骤](verify/STEPS.md)。
保存发布证据时使用实际包的哈希及字节数。`evidence/workbench-upgrade/release/` 保存的是 0.4.0 的构建记录。

## F2 形态（T61 定案；缺一条就会泄漏宿主定义）

| # | 条件 | 落点 |
|---|---|---|
| C1 | 库 `QRC_SYNTAX=OFF`（库内 0 定义） | `assemble-appdir.sh` 用 `--ksh-f2`（默认 `target/ksh-selfbuild/F2/install`），并断言 `scan-embedded-defs --expect-clean` |
| C2 | 库 `NO_STANDARD_PATHS=OFF`（按 XDG 查找） | 同上（F2 构建） |
| C3 | `XDG_DATA_DIRS="$APPDIR/usr/share"`（**只含 AppDir**） | `AppRun` |
| C4 | `XDG_DATA_HOME="$APPDIR/usr/share"` | `AppRun`（与 C3 必须同时设） |
| C5 | 只放白名单 `usr/share/org.kde.syntax-highlighting/syntax/*.xml`；**无** `syntax-bundled/` | 装配来自 `resources/syntax/*.xml`（T16 的 LGPL 定义），装配末尾断言 |
| C6 | 两个变量由 AppRun 自己设 | `AppRun` |
| C7 | 运行时白名单守卫（可见集合 ≠ 白名单 → 失败） | `AppRun` 的 `ksh_guard()`，调用 `bin/vedit-ksh-env-probe` |
| C8 | 图标/主题/字体自备 | DejaVu + `FONTCONFIG_FILE`；30 个主题内嵌在自建库；应用图标随包 |

**反向控制与负控是验收的一部分**。正控的期望值**不写死**：
`verify-f2-isolation.sh` 从随包的 `usr/share/vedit/ksh-whitelist.txt` **数条目**得到 `WANT`
（A3 之后是 5：`Markdown,SystemVerilog,Tcl,VHDL,Verilog`；A3 之前是 2）。

- **正控（AppDir）**：断言 `defs == WANT`、`names == 白名单`，并且**每一条可见定义的获胜文件**
  （探针输出的 `Definition::filePath()`）都在 AppDir 的
  `usr/share/org.kde.syntax-highlighting/syntax/` 下。这一条才是“互不遮蔽”的判据：
  KSyntaxHighlighting 按**名字**去重（`try_emplace`，同名只有更高 `version()` 能替换），
  所以同名遮蔽**不会**改变名字列表或数量，只会改变**哪个文件赢**。
- **宿主反向控制**：把 `/usr/share` 加回 `XDG_DATA_DIRS`，必须复现
  `defs > WANT`（本次 406 = 402 个宿主名字 + 5 个白名单名字 − 1 个双方同名的
  `SystemVerilog`；**不是**“402 宿主 + 1 用户”）。
- **用户级反向控制**：把 `XDG_DATA_HOME` 设为 `$HOME/.local/share`（`UserLeakProbe` 夹具所在），
  必须让它重新可见且 `defs > WANT`——验证 AppRun 对用户目录的收窄，而不只是 `XDG_DATA_DIRS`。
- **同名遮蔽负控**：`/shadow` 里放一份**同名、更高版本**的 `Tcl.xml`（version 999）。它可见时必须
  **赢过**随包副本，且上面的“获胜文件”断言必须**报告**它赢了；同时它的名字集合与 `defs` 不变
  ——这正是“名字/重复项检查永远测不出遮蔽”的证明。

## 不随包（许可闸门强制）

- KDE `systemverilog.xml`（GPL-3.0-or-later）与 `verilog.xml`（许可未声明）——按 sha256 拦，改名也拦；
- 发行版 `libKF6SyntaxHighlighting`（内嵌 450 定义 / 36 个严格 GPL）——按“库内 0 定义”拦；
- `syntax-bundled/`（压缩定义会被自动加载）；
- 任何不在白名单里的定义；
- Git、Subversion、svnadmin 和 shell 可执行文件。随包脚本通过宿主机的 `/bin/sh` 启动。

交付模式要求 `verify-not-shipped.sh` 返回 0 并报告 `DELIVERY_BLOCKED=no`。

## AppRun 契约

- `exec "$APPDIR/ld-linux-x86-64.so.2" --library-path "$APPDIR/lib" …`；
- `QT_PLUGIN_PATH`/`QT_QPA_PLATFORM_PLUGIN_PATH`/`XDG_*`/`VEDIT_SYNTAX_DIR`/`FONTCONFIG_FILE` 全部指向包内；
- **`LD_LIBRARY_PATH` 只在 exec 前导出**：提前导出会让宿主工具（`cat`/`sed`/`grep`）用宿主 loader 加载我们的 glibc 而失败（CentOS 7 实测 `GLIBC_2.35 not found`），从而让守卫读到空白名单、假 PASS；
- `AppRun` 将启动时导出的 `PATH`（包括空值）保存到 `VEDIT_HOST_PATH`，设定 `APPDIR`，再为应用添加包内 `bin`。终端和版本控制使用过滤后的宿主机路径。重启应用时会恢复原来的 `PATH`，并移除上一挂载目录的私有库路径。CentOS 7 探针验证没有 Git / SVN 的工作台启动。
- `AppRun --selfcheck` 额外传 `--ksh-selfbuild --expect-kshlib-prefix --expect-libc-prefix`。

## 复现性

- `SOURCE_DATE_EPOCH` 默认取 `git log -1 --format=%ct`；`runtime-manifest.txt` 不含墙钟时间；
- 同树两次构建 → bit-identical（T11 已验；F2 沿用同一机制）。

## 已知限制

1. **四宿主矩阵是容器矩阵**：`packaging/runtime/hostmatrix.sh` 已在 centos:7 / ubuntu:24.04 / rockylinux:9 / ubuntu:26.04 上跑通（启动、bundled lint、高亮均 PASS），**但全部共享宿主内核 `7.0.0-38-generic`，真 CentOS 7（内核 3.10）未测**。
2. 宿主直接 FUSE 挂载需 `libfuse2`（A7 前提）；无 libfuse2 时用 `APPIMAGE_EXTRACT_AND_RUN=1` 验证内容。
3. 30 个主题仍内嵌在库（上游无开关，全部 MIT），本包不另发主题文件。
4. 图标只随包应用图标；若 UI 需要系统图标主题，需补 hicolor 子集。


## Delivery form (T31)

- The AppDir installs the Qt/KTextEditor product as **`bin/vd-app`** (the M0
  smoke binary is gone); `AppRun` only starts that binary.
- Qt platform plugins **`libqxcb.so` and `libqoffscreen.so` are required** and are
  copied explicitly (they are `dlopen`-ed and invisible to `ldd`).
- The external toolchain (`bin/verible-verilog-*`, `bin/slang`) is installed with
  `--require-toolchain` and sha256-checked; `selfcheck.sh` asserts
  `TOOLCHAIN source=verible:bundled slang:bundled missing=[]`.
- `VEDIT_SYNTAX_DIR` stays the **data root** (T84); the product's shared
  syntax-directory helper normalises both spellings (T109/T110), so the assembly
  ships **only** `<root>/syntax/vedit-*.xml` — the root-level duplicate used in
  the skeleton phase is gone (a duplicate would drift and make the
  THIRD_PARTY_NOTICES hash ambiguous).
- `selfcheck.sh` reads `/proc/<pid>/maps` of a real product process and proves
  the packaged loader + libc are used and the system loader is absent.
- `build.sh` writes `out/release/SHA256SUMS` for all three packages. Keep the
  checksum file with the outputs it validates. Older `evidence/slice3-pack/`
  records apply to their own builds. Check the final archives when inspecting
  the bytes that users will receive.


## CJK font fallback (T119)

The AppDir ships `usr/share/fonts/NotoSansCJK-Regular.ttc` (19,484,784 B,
sha256 `b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a`,
OFL-1.1) and its licence at `usr/share/doc/vedit/licenses/Noto-CJK-OFL-1.1.txt`.
`etc/fonts/fonts.conf` aliases UI `DejaVu Sans` → `Noto Sans CJK SC` and editor
`DejaVu Sans Mono` → `Noto Sans Mono CJK SC`; Latin still matches DejaVu first.

The editor keeps DejaVu Sans Mono for Latin (so ASCII metrics are unchanged) and
falls back to Noto Sans Mono CJK for CJK glyphs. Because the two fonts have
different em advances, a CJK glyph is **1.60×** a Latin advance rather than the
typographic 2.0×; mixed Chinese/Latin comment alignment is therefore approximate,
while CJK runs are internally monospaced. This is a deliberate trade-off: ASCII
width stability was chosen over a 2.0 mixed ratio.

Back door: set the editor's primary font family to `Noto Sans Mono CJK SC` (one
configuration line) for strict 2.00; the cost is Latin advance 10px → 8px.

## appimagetool is vendored (reproducibility, T31)

`appimagetool` is **not** downloaded from the upstream `continuous` asset at build
time: that asset drifts. On 2026-11-01 two independent downloads hashed
`ac7fd56feade6002c2f57db39587113275d351fce911a5fbb29ea84cd0666cab`
(15,137,272 B), while `scripts/versions.env` pins
`793fca0ce4bcc309dc65d3f66669f58e3755643e4dadbe03932e7b436128494f`.

The pinned binary is committed as
`packaging/appimage/vendor/appimagetool-x86_64` (15,104,504 B, sha256
`793fca0ce4bcc309dc65d3f66669f58e3755643e4dadbe03932e7b436128494f`) and the
builders prefer it; the cache/network fallback remains only for a deliberate
refresh. The sha256 check is **fail-closed** in both paths (`build-appimage.sh`,
`build.sh`) — a mismatched tool aborts the build instead of warning.
Rationale: a reproducible build needs a reproducible tool; pinning a `continuous`
URL means "works until upstream publishes again, then fails closed", which is
what this workspace measured.
