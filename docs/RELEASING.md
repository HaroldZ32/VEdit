# 分支、提交与发版

本项目采用 **分支推进 + PR 合入**：`dev` 是集成分支，改动通过短生命周期的
`feat/` `fix/` `chore/` `docs/` 分支以 **PR** 合入 `dev`；发版时从 `dev` 切出
`release/<ver>`，验证与打包通过后合入 `main`，并在 `main` 上打 tag。
已发布的历史**永不重写**。

> Qt6 产品 `vedit-app` 以 AppImage、`.tar.gz`、`.run` 三种格式发布，见[构建指南](BUILD.md) §7。

## 1. 为什么是 dev + release，而不是单线

- **`main` 只放已发布的内容**：`main` 上每个提交都对应一次发版或一个 tag，
  发布线因此始终可对照产物、可回滚。
- **`dev` 承担日常集成**：多个 `fix/` `feat/` 分支并行时先在 `dev` 汇合、跑门禁，
  未发版的中间状态不会被带进 `main`。
- **发版走 `release/<ver>`**：从 `dev` 切出后只做"冻结 + 验证 + 打包 + 打 tag"，
  通过后合入 `main`；pre-release 的修正留在 release 分支内，不夹带 `dev` 上仍在进行的改动。
- **发布产物在 GitHub Releases 分发**。二进制与校验清单不提交到 Git。
- **回滚直接**：`main` 上按 tag `git revert` 或重发一个 PATCH，不需要在分支图上找落点。

## 2. 分支

| 分支 | 用途 | 生命期 |
|---|---|---|
| `main` | **发布线**：只接收 `release/<ver>` 的合并；每个提交对应一次发版 | 永久 |
| `dev` | **集成分支**：所有 PR 的合入目标；发版时从这里切出 release | 永久 |
| `feat/<slug>` | 新功能 | 短（合入 `dev` 即删） |
| `fix/<slug>` | 缺陷修复 | 短（合入 `dev` 即删） |
| `chore/<slug>` | 工程/依赖/构建 | 短（合入 `dev` 即删） |
| `docs/<slug>` | 文档 | 短（合入 `dev` 即删） |
| `release/<ver>` | 冻结、验证、打包、打 tag | 短（合入 `main` 即删） |

日常开发：从 `dev` 拉分支，PR 合回 `dev`（**允许 merge commit**）：

```sh
git switch dev && git pull --ff-only
git switch -c fix/issue-1-focus
# … 开发、提交、自测 …
git push -u origin fix/issue-1-focus
gh pr create --base dev --head fix/issue-1-focus
gh pr merge <n> --merge
git switch dev && git pull --ff-only
git branch -d fix/issue-1-focus
```

禁止：直接向 `main` 提交、`git push --force`、`git rebase` / `amend` 已发布的历史。

## 3. 提交信息（Conventional Commits）

格式：`<type>(<scope>): <summary>`

- `type`：`feat` / `fix` / `chore` / `docs` / `test` / `refactor`
- `scope`：crate 或区域名，例如 `vd-app`、`vd-syntax`、`packaging`、`build`、`skeleton`
- `summary`：一句话、祈使语气、不超过约 72 字符；正文写"为什么"和证据位置

实际使用过的例子（`git log --oneline`）：

```
feat(syntax): explicit Verilog/SystemVerilog mapping with rendered AC-05 evidence (T11)
feat(packaging): self-contained tar.gz and self-extracting .run for CentOS 7 (T25)
chore(pins): pin the Verible suite by URL + sha256 in scripts/versions.env
docs(evidence): workspace gate green at T19 delivery (T17 deeplint landed)
```

一个提交只做一件事；不要把格式化、改名、功能混在一个提交里。

## 4. 版本号（SemVer）与 tag

- `MAJOR.MINOR.PATCH`；首个版本 **`v0.1.0`**（0.x 表示接口尚未冻结）。
- `MAJOR`：不兼容变更；`MINOR`：向后兼容的新功能；`PATCH`：向后兼容的修复。
- 预发布：`v0.2.1-rc.1`（后缀递增）。
- tag 打在 `main` 上（`release/<ver>` 合入之后），**annotated**：

```sh
git switch main && git pull --ff-only
git tag -a v0.4.1 -m "VEdit 0.4.1"
git push origin refs/tags/v0.4.1
```

## 5. 发布产物（GitHub Release 附件，不进 git）

| 附件 | 用法 |
|---|---|
| `VEdit-<ver>-x86_64.AppImage` | 单文件，支持 FUSE 挂载。无 FUSE 时加 `--appimage-extract-and-run` |
| `vedit-<ver>-linux-x86_64.tar.gz` | 解包后运行 `vedit-<ver>-linux-x86_64/AppRun`，无需 FUSE |
| `VEdit-<ver>-x86_64.run` | 自解压后启动，也可 `--extract DIR` 后保留安装目录，无需 FUSE |
| `SHA256SUMS` | 三个包各有一条 SHA-256 记录 |

三个包来自同一个 Qt6 AppDir，包含相同的应用、私有运行时、工具和数据文件。Git、Subversion 和终端 shell 使用宿主机版本。

```sh
bash packaging/runtime/build.sh
(cd packaging/runtime/out/release && sha256sum --check --strict SHA256SUMS)
```

## 6. GitHub Actions 验证与发布（Qt 产品线）

目标为 `dev` 的 PR 和 `dev` 的 push 运行日常 CI，包括 Rust 服务、C ABI、Qt6 构建、CTest、Xvfb、
翻译和文档检查。`release/<ver>` 向 `main` 提交 PR 时，`ci` 在合并测试提交上增加三个包的打包与验证。
也可以在 Actions 手动运行 `ci`，选中开发分支提前验证，前提是默认分支已登记该工作流。手动运行
`release` 只执行构建与验证。

发布 PR 以 merge commit 合入 `main` 后，在该提交创建并推送与工作区和 `--version` 匹配的 annotated
`v<ver>` tag。`release` 检查 tag 对象及 `main` 第一父链中的合并提交，从 tag 对应的源码构建并验证。
三个已验证的包和校验清单分别上传为四个 Actions artifacts。发布步骤下载同次构建的四份文件并检查校验
清单，随后上传到 GitHub Release 草稿。只有 GitHub 返回的全部附件名称、字节数和 SHA-256 与本地记录
一致后，工作流才公开发布。发布说明来自 `docs/releases/<ver>.md`，缺少该文件时由 GitHub 生成。
同一 tag 若已有发布或草稿，工作流会停下。重试前应先查看该草稿和失败日志。

Qt6 镜像使用固定 Ubuntu 镜像摘要、签名 APT 快照和锁定的包。Slang、Rust、Qt6、Scala 和打包使用独立
的 Ubuntu 24.04 runner。预热的 Scala 缓存会增加包体积和暂存需求。单个 GitHub Release 附件须小于
2 GiB，工作流在 Actions 上传前检查这一上限。上传的三个包已压缩，按独立 Actions artifacts 传输。用打包与
发布 runner 的磁盘记录检查剩余空间，保留 `target/ci-logs/` 的日志和 Actions artifacts。
CentOS 7 容器检查 glibc 2.17 用户空间，使用的是 runner 的内核。

### 本地发布前检查单

1. `git switch dev && git pull --ff-only`；工作区干净。
2. `bash build/gates/qt6-cxx.sh` 全绿（Qt6/KF6 C++ 车道：configure + build + ctest + xcb 冒烟）。
3. `bash scripts/gates.sh` 全绿（Rust 服务层：fmt / clippy / test / 依赖策略）。
4. `bash build/gates/lrelease.sh` 与 `bash build/gates/docs.sh` 全绿（翻译目录零 unfinished/untranslated；文档齐备）。
5. 运行 `bash packaging/runtime/build.sh`，在 `packaging/runtime/out/release/` 对三个包执行严格校验。
   确认 F2 隔离正反对照、CentOS 7 用户态探针、AppImage 解包启动及 `.tar.gz` / `.run` 无 FUSE 启动通过。
   核对版本、包内应用与工具、缺少宿主 Git/SVN 时的启动，以及测试程序失败时的退出码。
6. 从 `dev` 切出 `release/<ver>`，向 `main` 提交 PR。完成本轮 PR 的 Actions 完整打包并保存日志。
7. 按 `README.md`、`docs/USER_GUIDE.md` 和 `THIRD_PARTY_NOTICES.md` 核对发布包。使用新的
   `evidence/release-<ver>/` 记录测试提交、命令、时间和结果。
8. 以 merge commit 合入 `main`，推送对应的 annotated tag，等待该 tag 的发布工作流通过。
9. 核对公开发布页恰有三个同版本的包和 `SHA256SUMS`，记录资产字节数。下载实际资产重新执行
   `sha256sum --check --strict SHA256SUMS`，或独立比对 Actions 产物与 GitHub 公共资产的 SHA-256。
   保存核对结果，再向 `dev` 提交同步 PR。

## 7. 热修（hotfix）

从**最近的 tag** 拉分支，PATCH 递增；修完先合 `dev`，再按第 6 节走 release 流程进 `main`：

```sh
git switch -c fix/crash-on-empty-file v0.2.0     # 注意：从上一个 release tag 起步
# … 修复 + 测试 …
git push -u origin fix/crash-on-empty-file
gh pr create --base dev --head fix/crash-on-empty-file
# 合入 dev 后按第 6 节发一个 PATCH：release/0.2.1 → main → v0.2.1
```

## 8. 历史纪律

- **永不重写已发布的历史**：不 `amend`、不 `rebase`、不 `force-push` 已被他人拉取的提交。
- 提交信息里的小瑕疵（例如某条 message 丢了一个词）**不靠 amend 修**，靠后续提交/文档说明；
  代码正确性远比 message 措辞重要，而改写历史的代价更高。
