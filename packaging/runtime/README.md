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
| `packaging/runtime/out/AppDir/` | 196 个文件，约 178 MB |
| `packaging/runtime/out/VEdit-<ver>-x86_64.AppImage` | 单文件交付物（F2 后许可闸门全绿） |
| `evidence/m0-runtime/` | 每一步日志 + manifest + `F2.md`（T64 报告） |

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

1. **delivery 双宿主矩阵未重跑**：用 `tests/packaging/hostmatrix.sh --appimage packaging/runtime/out/VEdit-<ver>-x86_64.AppImage` 跑 A0a/A0b/A0d/A0e；这是 M0 的结案动作（T12 脚本可直接复用）。
2. 宿主直接 FUSE 挂载需 `libfuse2`（A7 前提）；无 libfuse2 时用 `APPIMAGE_EXTRACT_AND_RUN=1` 验证内容。
3. 30 个主题仍内嵌在库（上游无开关，全部 MIT），本包不另发主题文件。
4. 图标只随包应用图标；若 UI 需要系统图标主题，需补 hicolor 子集。
