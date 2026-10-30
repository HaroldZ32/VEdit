# Issue #1 / #2 — 复现、修复与独立验证

GitHub issues：[#1 窗口内无法编辑文字，敲键盘无反应](https://github.com/HaroldZ32/VEdit/issues/1) ·
[#2 打开文件滚轮滚动无反应，全屏后卡死，并且依然不可编辑](https://github.com/HaroldZ32/VEdit/issues/2)

修复提交：`7acd487`（父 `c5216c1`）· 独立验证 verdict：**approve**，
AC-1..AC-8 全部 PASS（[`fix-verify/REVIEW-REPORT.md`](fix-verify/REVIEW-REPORT.md)）。

## 结论一句话

三个症状（打不了字、滚轮无反应、全屏卡死）由**两个独立缺陷**解释：编辑器 View 从未获得键盘焦点；
50 ms 后台定时器把状态栏字符串无界追加并每拍重建 gutter 标记，60 秒内吃满一个 CPU 核。

## 修复前后（同一脚本、同一 pinned 镜像）

| 量具 | 修复前 | 修复后 |
|---|---|---|
| 单核占用，60 s 协议（`repro/probe-cpu.sh`） | 36.3 → 49.4 → 77.5 → 96.5 → **98.0%**，growth **+61.7pp** | 0.3 → 0.2 → 0.2 → 0.3 → **0.2%**，growth **−0.1pp** |
| 端到端打字（`repro/probe-typing.sh`，XTest 注入 + Ctrl+S 落盘比对） | `TYPING_BROKEN`（注入成功但文件未变） | `TYPING_WORKS`（注入的 `a` 进入 buffer） |
| 对照组（最小 GtkTextView，`repro/probe-typing-control.sh`） | `CONTROL_WORKS`（证明探针能鉴别） | `CONTROL_WORKS` |

原始捕获：`repro/cpu-before-fix-60s.txt`、`repro/typing-before-fix.txt`、
`fix-impl/cpu-after-fix-60s.txt`、`fix-verify/22-cpu-after.txt`、`fix-verify/21-typing-after.txt`。

## 根因

| # | 根因 | 位置 |
|---|---|---|
| R1 | 产品代码从未把键盘焦点交给编辑器 View；全仓唯一 `grab_focus()` 用于 Ctrl+F 的查找框 | `crates/vd-app/src/app.rs` 的 `add_tab` / `build_ui` |
| R2 | `sync_lsp_status()` 每拍向 `state.info` **尾部**追加 `"Verible LSP: generation N"`，守卫却用 `starts_with` 检查**头部**；任何一次 `set_info()` 覆盖头部后永不收敛 | `app.rs::sync_lsp_status` / `status_text` |
| R2b | `refresh_status()` 每拍无条件删除并重建全部 gutter 标记，`mark-set` 又嵌套回调 `refresh_status()` | `app.rs::refresh_gutter_marks` |
| R3 | `poll_background()` 在 LSP bridge 缺失时整体 `return`，连带 SLANG/FIM 也永远轮询不到 | `app.rs::poll_background` |
| R4 | FIM ghost text 的覆盖层未设 pass-through，语义上不应吞指针输入 | `app.rs::build_ui` |

## 改了哪些文件（`7acd487`）

`crates/vd-app/src/app.rs`（+150/−28）：`StatusModel` 有界状态模型、`Ui::focus_editor()`、
gutter 诊断签名去抖、`poll_background` 去早退、overlay pass-through、2 条免显示单测。
`crates/vd-app/src/document_tab.rs`（+6/−1）：`Tab::marks_key` 签名。
`crates/vd-app/src/selftest.rs`（+201/−0，纯新增）：7 条 AC 检查，`--selftest all` 51 → **58**
（smoke 20 → 27）；既有 51 条检查名一条不少。

## 本目录

| 路径 | 内容 |
|---|---|
| `repro/` | 复现量具：CPU 采样、XTest 打字注入 + 对照组、修复前原始捕获 |
| `fix-impl/` | 实现者证据：逐 AC 表、构建/门禁/自测输出、修复后 CPU 与打字原始输出 |
| `fix-verify/` | 独立评审：量具审计、51 条基线清单、门禁日志、逐 AC 判定、`REVIEW-REPORT.md` |
| `readiness.md` | 早前的就绪度记录（与本次修复无关） |

## 重要边界

- **已签入的 v0.1.0 产物（AppImage / tar.gz / .run）未重打包**，它们的 `vd-app` 仍是修复前的二进制
  （sha256 `41d02dca…`），自测报 51 项。要真正拿到修复，需要重新打包（`docs/RELEASING.md`）。
- 修的是源码与回归测试；没有新增依赖，没有改 `Cargo.toml` / `Cargo.lock` / `rust-toolchain.toml`。

## CI/CD（PR #6，合并入 `dev` 于 `5b818bd`）

仓库现在有三条 GitHub Actions 工作流（`.github/workflows/`）：

| 工作流 | 触发 | 作用 |
|---|---|---|
| `ci.yml` | PR → `dev`/`main`、push → `dev` | 拉取 GHCR 上的 pinned 镜像（拉不到才本地构建），`build/slang/build.sh` + `scripts/fetch-verible.sh` 之后跑 `scripts/gates.sh` 与 `scripts/build.sh`（release build、Xvfb、**6 套件 62 checks**、GLIBC ≤ 2.17），上传 `build/evidence/` 与二进制 |
| `image.yml` | `scripts/docker/**`、`versions.env`、`lib.sh`、`build-image.sh` 变更 | buildx（container driver）构建 pinned 镜像；**PR 只构建、push 到 `dev`/`main` 才推 GHCR**；层缓存用 `actions/cache` + `type=local` |
| `release.yml` | tag `v*` | slang → release build → AppImage → tar.gz/.run → `SHA256SUMS` → GitHub Release（**无 tag 未验证**） |

真实运行证据（逐 AC 判定见 `ci-verify/T5-REPORT.md`、`T12-REPORT.md`、`T14-REPORT.md`）：

- PR `ci` run `41047244806`：success，`real_slang` 6/6、`verible_ls` 8/8、225 tests、6 套件 62/62、GLIBC ≤ 2.17；
- `image` run `41053154474`：层缓存命中 **7 个 `CACHED`**，耗时 1m40s（冷构建 6m14s）；
- `image` run `41056011790`（dev push）：`published ghcr.io/haroldz32/vedit-build:5b818bd8… and :latest`；
- dev `ci` run `41056011838`：`using the published image ghcr.io/haroldz32/vedit-build:latest`。

CI 期间修掉的真实缺陷：干净 checkout 缺 slang / verible（6+8 条集成测试必挂）、
`build-in-container.sh` 把四个套件名当单参数传给 `--selftest`、root daemon 下容器以 root 写工作区导致
`build.sh` 末尾 `cp` 被拒、镜像源 TLS 抖动直接判红（重试 + digest/tag 回退）。

## 后续项（本次未做，均有证据）

- **F1**（既有，Medium）状态栏 FIM 原因重复渲染一次：修复前二进制即如此，非本次回归。
- **F2**（既有，Medium）打开文件后光标落在文件末尾（`Document::open` 的 `set_text`）——
  本修复让光标可见后此行为才显形，非本次回归。
- **F3/F4/F5/F6/F7**（Low/Nit）见 `fix-verify/REVIEW-REPORT.md` 的 findings 表。
- **issue #3**（多文件管理与工程导入）是独立功能请求，不在本轮范围。
