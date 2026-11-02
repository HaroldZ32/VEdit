generated: 2026-10-31T14:19:09Z
git_head: c71fe31478887e3dc7c460f875ccfb93e61c0ec4
git_status_porcelain:
 M AGENT.md
 M build/evidence/typecheck/00-run.txt
 M build/evidence/typecheck/10-typecheck-image-build.log
 M build/evidence/typecheck/11-typecheck-image-id.txt
 M docs/BUILD.md
 M docs/USER_GUIDE.md
 M evidence/ffi-abi/logs/10-rust-image.txt
 M evidence/ffi-abi/logs/19-rust-stage-console.log
 M evidence/ffi-abi/logs/20-cargo-build-release.log
 M evidence/ffi-abi/logs/21-staticlib.txt
 M evidence/ffi-abi/logs/23-cargo-test.log
 M evidence/m0-build/build-logs/20-qt6-image-build.log
 M evidence/m0-build/build-logs/21-qt6-image-id.txt
 M evidence/m0-build/build-logs/31-qt6-configure.log
 M evidence/m0-build/build-logs/32-qt6-build.log
 M evidence/m0-build/build-logs/34-qt6-elf.txt
?? evidence/glibc217-feasibility/
?? evidence/user-guide-split/
?? evidence/verdicts/fix-shell-terminal/
?? evidence/verdicts/search-jump-kte/
?? evidence/verdicts/user-guide/
---
# build entry documents aligned (T78: T77 F-1)

Three documents now agree on which command produces which artifact. The paths
were verified by reading the scripts (not assumed):

- `scripts/build-qt.sh` delegates to `scripts/build-qt-in-container.sh`, where
  `BUILD_DIR="$WORK/target/qt6-build"` (line 11), cmake configures
  `-B "$BUILD_DIR"` (line 55) and `APP="$BUILD_DIR/src/app/vedit-app"` (line 72).
  The binary target is `add_executable(vedit-app main.cpp)` in
  `src/app/CMakeLists.txt:42`; the build produced
  `target/qt6-build/src/app/vedit-app` (also the artifact measured in
  `evidence/runtime-deps/REPORT.md`).
- `scripts/build.sh` sets `BIN="$ROOT/target/release/vd-app"` (line 63) and
  copies it to `build/artifacts/vd-app` (line 112); it never builds the Qt6 app.

## A. AGENT.md §4 — Build split into two parallel entries, Qt6 first

Before:
```text
- Build: `bash scripts/build.sh` (one command: pinned image ->
  `cargo build --release --workspace` -> ELF assertion that the highest GLIBC
  symbol is <= 2.17 -> Xvfb `--selftest smoke` window check).
  Gate wrapper: `bun run verify:gates`.
```

After:
```text
- Build the **delivered Qt6 product** (v2): `bash scripts/build-qt.sh` ->
  `target/qt6-build/src/app/vedit-app`. Its runtime needs the KF6/Qt6 shared
  libraries, `libtree-sitter` 0.25 and the Qt platform plugins; the measured
  package names are in `evidence/runtime-deps/REPORT.md` §1 and the build matrix
  in `docs/BUILD.md` §12. **If you are verifying the v2 deliverable, use this
  command.**
- Build the **legacy Rust/GTK3 product**: `bash scripts/build.sh` (one command:
  pinned image -> `cargo build --release --workspace` -> ELF assertion that the
  highest GLIBC symbol is <= 2.17 -> Xvfb `--selftest smoke` window check). It
  writes `target/release/vd-app` (copied to `build/artifacts/vd-app`) and
  **never produces `vedit-app`**.
  Gate wrapper: `bun run verify:gates`.
```

## B. docs/USER_GUIDE.md §1 — new "1.4 从源码得到 Qt6 构建（v2 交付物）"

There was no Qt6 build section before (`grep -c 'build-qt' docs/USER_GUIDE.md` was 0).
Added section:
```text
### 1.4 从源码得到 Qt6 构建（v2 交付物）

§1.1–1.3 是**旧 GTK3 产物**的安装方式。要拿到 v2 的 Qt6 交付物，从源码构建：

```sh
bash scripts/build-qt.sh              # 固定镜像 → Rust 服务层 → configure → build → ctest → xcb 冒烟
bash scripts/build-qt.sh --no-test    # 只 configure + build
```

产物是 **`target/qt6-build/src/app/vedit-app`**（CMake 构建树在 gitignored 的 `target/qt6-build/`）。
`bash scripts/build.sh` 是**旧产品**：只产出 `target/release/vd-app`，**不产出 `vedit-app`**。

把 `vedit-app` 放到另一台机器直接跑时，需要 KF6/Qt6 运行库、`libtree-sitter0.25` 和 Qt 平台插件
（`libqxcb`）；实测包名见 `evidence/runtime-deps/REPORT.md` §1，构建期版本矩阵见 `docs/BUILD.md`
§12.3。注意构建基线是 glibc 2.43，比它老的系统（含 CentOS 7）要连 glibc 一起打包（同文件 §2）。

```

## C. docs/BUILD.md §12.2 — artifact path now named

Before:
```text
### 12.2 一条命令

```sh
bash scripts/build-qt.sh              # 镜像 → configure → build → ctest → xcb/软件渲染冒烟
bash scripts/build-qt.sh --no-test    # 只 configure + build（即 C++ 静态检查）
```

每一步的原始输出落在 **`evidence/m0-build/build-logs/`**（T10 的证据目录，随仓库提交）；
```

After:
```text
### 12.2 一条命令

```sh
bash scripts/build-qt.sh              # 镜像 → configure → build → ctest → xcb/软件渲染冒烟
bash scripts/build-qt.sh --no-test    # 只 configure + build（即 C++ 静态检查）
```

**产物路径：`target/qt6-build/src/app/vedit-app`**（CMake 构建树 `target/qt6-build/` 已被 gitignore）。
`bash scripts/build.sh` 是旧 Rust/GTK3 产品，只写 `target/release/vd-app`，不产出 `vedit-app`。
运行期依赖（KF6/Qt6 运行库、`libtree-sitter0.25`、Qt 平台插件 `libqxcb`）的实测包名见
`evidence/runtime-deps/REPORT.md` §1，运行期打包边界见本文件 §12.6。

```

## Runtime dependencies: no package name invented

The guide and BUILD.md point at `evidence/runtime-deps/REPORT.md` §1, which lists
the package names measured with `readelf -d` + `dpkg -S`
(`libkf6texteditor6` pulling the rest of KF6, `libtree-sitter0.25`, Qt6
widgets/gui/core, plus the `libqxcb` platform plugin) and at §2 for the glibc 2.43
baseline caveat. Nothing here is guessed; where a name was not already measured it
is not written.

## Verification

- `bash build/gates/docs.sh` → `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md`, exit 0 (`03-docs-gate.txt`).
- `grep -n 'build-qt\|vedit-app\|build.sh' AGENT.md docs/USER_GUIDE.md docs/BUILD.md`
  → all three files name the artifacts (`02-grep.txt`).
- Unified diffs: `01-diffs.txt` (only the regions above changed; no unrelated
  section rewritten, no GUI behaviour claimed).

## Scope

Changed: `AGENT.md`, `docs/USER_GUIDE.md`, `docs/BUILD.md`, plus this evidence
directory. No code, no tests/gates, no other docs, no `evidence/verdicts/**`.
No commit, no push (the Lead commits).
