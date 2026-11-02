# packaging/runtime — VEdit 私有运行时（F2 形态，T64）

单文件 AppImage 的私有运行时：包内自带 `ld-linux` + glibc + Qt6 + KF6 +
KTextEditor + **自建** KSyntaxHighlighting + tree-sitter + 字体，`AppRun` 显式经包内
loader 启动，并把语法定义查找**收窄到 AppDir 白名单**。交付二进制要求
`GLIBC_2.34`，目标是 CentOS 7.9（glibc 2.17），所以自带运行时是**运行前提**（C2）。

## 一键

```bash
bash packaging/runtime/build.sh          # 组装 → 许可闸门 → 自检 → F2 隔离(正+反) → AppImage → AppImage 自检/隔离 → CentOS 7 机制探针
```

产物：

| 路径 | 说明 |
|---|---|
| `packaging/runtime/out/AppDir/` | **中间产物**（不是交付物）：202 个文件，约 283 MB；`bin/vd-app` 的 sha256 与交付 AppImage 内的同一份（当前 `b93f8cf4…`）。目录内容来自最终基线，仅用于检查/调试。 |
| `packaging/runtime/out/VEdit-<ver>-x86_64.AppImage` | **唯一交付物**：单文件 AppImage（当前基线 `55a874d` 的产物 sha256 `e7ae067f…`）；`out/SHA256SUMS` 与之配套。 |
| `evidence/slice3-pack/` | 最终打包证据（`final-55a874d/`：C9、构建、干净容器 selfcheck、四容器矩阵）。 |

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

**反向控制是验收的一部分**：把 `/usr/share` 加回 `XDG_DATA_DIRS` 重跑，必须复现
`defs=403`（402 宿主 + 1 用户）。正控是 `defs=2`（`SystemVerilog,Verilog`）。

## 不随包（许可闸门强制）

- KDE `systemverilog.xml`（GPL-3.0-or-later）与 `verilog.xml`（许可未声明）——按 sha256 拦，改名也拦；
- 发行版 `libKF6SyntaxHighlighting`（内嵌 450 定义 / 36 个严格 GPL）——按“库内 0 定义”拦；
- `syntax-bundled/`（压缩定义会被自动加载）；
- 任何不在白名单里的定义。

`verify-not-shipped.sh` 现在是 **delivery 模式全绿**（`pass=8 fail=0`，`DELIVERY_BLOCKED=no`）。

## AppRun 契约

- `exec "$APPDIR/ld-linux-x86-64.so.2" --library-path "$APPDIR/lib" …`；
- `QT_PLUGIN_PATH`/`QT_QPA_PLATFORM_PLUGIN_PATH`/`XDG_*`/`VEDIT_SYNTAX_DIR`/`FONTCONFIG_FILE` 全部指向包内；
- **`LD_LIBRARY_PATH` 只在 exec 前导出**：提前导出会让宿主工具（`cat`/`sed`/`grep`）用宿主 loader 加载我们的 glibc 而失败（CentOS 7 实测 `GLIBC_2.35 not found`），从而让守卫读到空白名单、假 PASS；
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
- `build.sh` writes `out/SHA256SUMS` for the single-file AppImage and copies the
  evidence into `evidence/slice3-pack/`.

**Delivery baseline:** `55a874da076d605c6b620e0bb20f9ac5932d5f46`. The delivery
AppImage is `out/VEdit-0.1.1-x86_64.AppImage` with sha256
`e7ae067fe66c8e271ba72da974795a7ee3db0efa59bda45079b5a048b4a09558`; any source or
packaging-script change invalidates it and requires a rebuild. `out/AppDir/` is
an intermediate inspection tree (its `bin/vd-app` sha matches the AppImage), not
a deliverable.


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
