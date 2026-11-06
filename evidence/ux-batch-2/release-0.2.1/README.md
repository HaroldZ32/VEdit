# Release 0.2.1 — every document and helper script agrees with the new version

**任务**：T42。把文档与打包辅助脚本从 0.1.1/0.2.0 统一到 **0.2.1**（v0.2.0 tag 已存在，
本次发版号是 0.2.1）。代码/构建字面量由队长先行 bump；本卡负责文档、辅助脚本、README 与证据。

**写范围**：`docs/`、`README.md`、`packaging/runtime/`、`packaging/tarball/README.md`、
`evidence/ux-batch-2/release-0.2.1/`。未改代码版本字面量，未重建 AppImage。

## 1. 代码 / 构建 / 文档现在是同一个版本：0.2.1

| 位置 | 值 | 证据 |
|---|---|---|
| 工作区 `Cargo.toml`（打包脚本取值来源） | `0.2.1` | `Cargo.toml:25` |
| 打包脚本读取 | 读 `Cargo.toml` 的 `version` | `packaging/runtime/build-appimage.sh:53` |
| `CMakeLists.txt` | `project(VEdit VERSION 0.2.1 …)` | `CMakeLists.txt:19` |
| `src/terminal/CMakeLists.txt` | `project(VEditTerminal VERSION 0.2.1 …)` | `src/terminal/CMakeLists.txt:16` |
| `src/app/main.cpp` | `setApplicationVersion("0.2.1")` | `src/app/main.cpp:1851` |
| `cmake/smoke/qt6_window_smoke.cpp` | `setApplicationVersion("0.2.1")` | `cmake/smoke/qt6_window_smoke.cpp:133` |
| 文档 | 产物名与版本说明都是 0.2.1 | `docs/BUILD.md` §7.1；`packaging/runtime/README.md`；`packaging/runtime/verify/STEPS.md` |

**结论**：产物名 `VEdit-0.2.1-x86_64.AppImage` 与应用内版本 `0.2.1` 现在一致；上一轮
“产物名 0.1.1 / 应用 0.2.0 不同源”的说明已删除（`docs/BUILD.md` §7.1 现在是单一口径）。
`docs/BUILD.md` “两个版本不同源”的原段落已整体替换，全树不再有该说法。

## 2. 修改清单

| 文件 | 改动 |
|---|---|
| `packaging/runtime/verify/STEPS.md` | 6 行里的 7 处 `VEdit-0.1.1-x86_64.AppImage` → `VEdit-0.2.1-x86_64.AppImage`（第 109 行有两处）；第 30–33 行的“固定大小 95,349,240 / 固定 sha”改为“以同目录 `SHA256SUMS` 为准，`VEDIT_EXPECTED_SHA256` / `VEDIT_EXPECTED_SIZE` 可选覆盖” |
| `packaging/runtime/verify/verify-appimage-centos7.sh` | 3 处名称 → 0.2.1（注释、候选路径、失败提示）；**保留 `VEdit-*x86_64.AppImage` 通配回退**；删掉硬编码的旧 sha256/大小（`EXPECTED_SHA256`/`EXPECTED_SIZE` 现在默认空，优先从同目录 `SHA256SUMS` 读取该行，仍保留 `SHA256SUMS` 交叉校验；`bash -n` 通过） |
| `packaging/runtime/README.md` | 交付物行改为 `out/VEdit-0.2.1-x86_64.AppImage` + “sha256 见 `out/SHA256SUMS`”（不再重复旧哈希）；删除旧基线 `55a874d`/`e7ae067f…`；证据路径 `final-55a874d/` → `final-<commit>/`；`Delivery baseline` 段落不再写死旧哈希 |
| `packaging/runtime/hostmatrix.sh` | 默认 `APPIMAGE` → `out/VEdit-0.2.1-x86_64.AppImage`（`VEDIT_APPIMAGE` 覆盖保留） |
| `docs/BUILD.md` | §7.1 的“版本 0.1.1 与 0.2.0 不同源”整段替换为“代码、构建、文档统一为 0.2.1 + 五处字面量 + 产物名”；§7.1 删除写死的旧产物大小 137,079,288，改为“以 `out/SHA256SUMS` 与实际文件为准”；§7.2 的 0.1.1 体积对比标注为“0.1.1 时期实测” |
| `docs/RELEASING.md` | 预发布示例 `v0.2.1-rc.1`；tag 示例 `v0.2.1`（消息同步）；热修示例从上一个 release tag `v0.2.0` 起步、发 `release/v0.2.1`（与“PATCH 递增”一致） |
| `packaging/tarball/README.md` | 表格前加“**历史记录（0.1.1 时期的旧 GTK 产物）**…不代表 0.2.1，也不是当前交付物”；**数字与 sha256 原样保留**（历史产出记录） |
| `README.md` / `docs/USER_GUIDE.md` | 无 0.1.1 / 0.2.0 / `VEdit-0.x` 字面量（产物一律写 `VEdit-<ver>` / `VEdit-<版本>`）；无需改动，见 §3 |

## 3. 前后 grep（同一命令，`logs/02` / `logs/03`）

命令：

```sh
grep -rn --include=*.md --include=*.sh --include=*.txt --include=*.yml --include=*.env \
  -E '0\.1\.1|0\.2\.0|VEdit-0\.' . \
  --exclude-dir=evidence --exclude-dir=target --exclude-dir=.mpd --exclude-dir=.cache \
  | grep -v '^./packaging/appimage/.cache'
```

BEFORE：33 条命中（完整列表 `logs/03-grep-before.log`），集中在：

- `packaging/runtime/verify/STEPS.md`（6 行）、`verify-appimage-centos7.sh`（3 行）、
  `packaging/runtime/README.md`、`packaging/runtime/hostmatrix.sh`；
- `docs/BUILD.md`（2 行，版本分叉说明）、`docs/RELEASING.md`（2 行）；
- `docs/handover/*`（5 行）、`docs/plans/v2-slices.md`、`docs/rust-crates.txt`（子串）；
- `packaging/tarball/README.md`（2 行历史记录）、`tests/e2e/*`（7 行旧 GTK e2e）、
  `tests/packaging/hostmatrix.sh`、`scripts/docker/rpm-baseline.txt`（子串）。

AFTER：32 条命中（`logs/02-grep-after.log`；比 BEFORE 少 1 条，因为旧版本分叉说明整段被替换），剩余命中及原因：

| 命中 | 保留原因 |
|---|---|
| `packaging/tarball/README.md:10,15,16` | **历史记录**（0.1.1 时期真实产出的 tar.gz/.run 与 sha256），已加历史标签；按 T42 要求不改数字 |
| `packaging/runtime/verify/STEPS.md`、`verify-appimage-centos7.sh`、`packaging/runtime/README.md`、`packaging/runtime/hostmatrix.sh` 中的 `VEdit-0.2.1…` | **本轮新值**（命中正则 `VEdit-0\.` 的预期结果） |
| `docs/BUILD.md:160,175` | 160 是新的 0.2.1 单一口径；175 是 0.1.1 时期体积对比，已标“历史实测” |
| `docs/RELEASING.md:123` | 热修从**上一个** release tag `v0.2.0` 起步，产物是 `release/v0.2.1`；这是版本语义，不是旧版本残留 |
| `docs/plans/v2-slices.md:13` | 历史计划记录（0.1.1 产物 65,673,720 B，sha `74bd6aae…`），不是当前交付物 |
| `docs/handover/README.md:43`、`02-status-matrix.md:33`、`05-build-and-verify.md:16,104`、`08-evidence-index.md:45` | 内部交接/状态的历史基线（0.1.1 的 65,673,720 B、sha `74bd6aae…`）与历史版本提升记录；保留为历史 |
| `docs/rust-crates.txt:36` | 第三方 crate `find-msvc-tools 0.1.15`，只是子串命中 |
| `scripts/docker/rpm-baseline.txt:224` | 第三方 rpm `libthai-0.1.14-9.el7`，只是子串命中 |
| `tests/e2e/README.md:18,20`、`run.sh:8,10`、`ACCEPTANCE.md:12,14,43` | **旧 GTK3 产品线**的 e2e 记录/脚本（0.1.0 的 `packaging/appimage` 与 `packaging/tarball` 产物）；历史证据，不是 Qt 交付物 |
| `tests/packaging/hostmatrix.sh:28` | **超出本卡写范围**的测试副本，仍默认 `.probe.AppImage` 0.1.1；未使用于发布路径，建议后续测试行更新（见 §5） |

`README.md` / `docs/USER_GUIDE.md`：上述正则 **0 命中**（没有写死的版本号/产物名）。

## 4. 发布产物状态（如实记录）

`packaging/runtime/out/` 里当前仍是 **21:40 构建的 0.1.1 AppImage**
（`VEdit-0.1.1-x86_64.AppImage`，`out/SHA256SUMS` 也是旧行）。版本已 bump，**发布构建需要重新运行**
`bash packaging/runtime/build.sh` 才会生成 `VEdit-0.2.1-x86_64.AppImage` 并重写 `out/SHA256SUMS`。
文档现在指向新文件名与 `out/SHA256SUMS`（不写死哈希），因此重建前后都不会留下过期哈希。
未在本卡内重建产物（写范围是文档/证据）。

## 5. 超范围发现（未改）

- `tests/packaging/hostmatrix.sh:28` 默认 `.probe.AppImage` 仍是 0.1.1；属于 `tests/`，不在本卡
  WRITE SCOPE，且不在发布路径上。建议测试行或后续 release 清理卡更新。
- `docs/handover/**` 与 `docs/plans/v2-slices.md` 的 0.1.1 数字是历史记录（见 §3），没有改写成 0.2.1，
  否则会篡改历史证据。

## 6. 门禁

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
docs-gate exit=0
```

## 7. 证据文件

- `logs/01-docs-gate.log` — 文档门禁 PASS
- `logs/02-grep-after.log` — 修改后全树 grep（33 行，含预期新值与历史项）
- `logs/03-grep-before.log` — 修改前 33 行（同一命令）
- `logs/04-code-literals.log` — Cargo/CMake/main.cpp/smoke 的 0.2.1 与 `out/` 现状
