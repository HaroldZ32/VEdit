# 分支、提交与发版

本项目采用 **分支推进 + PR 合入**：`dev` 是集成分支，改动通过短生命周期的
`feat/` `fix/` `chore/` `docs/` 分支以 **PR** 合入 `dev`；发版时从 `dev` 切出
`release/<ver>`，验证与打包通过后合入 `main`，并在 `main` 上打 tag。
已发布的历史**永不重写**。

## 1. 为什么是 dev + release，而不是单线

- **`main` 只放已发布的内容**：`main` 上每个提交都对应一次发版或一个 tag，
  发布线因此始终可对照产物、可回滚。
- **`dev` 承担日常集成**：多个 `fix/` `feat/` 分支并行时先在 `dev` 汇合、跑门禁，
  未发版的中间状态不会被带进 `main`。
- **发版走 `release/<ver>`**：从 `dev` 切出后只做"冻结 + 验证 + 打包 + 打 tag"，
  通过后合入 `main`；pre-release 的修正留在 release 分支内，不夹带 `dev` 上仍在进行的改动。
- **发布产物不进 git**（见第 5 节）：一个 40–48 MB 的二进制每次发版都进历史会让仓库迅速不可用，
  产物作为 GitHub Release 附件分发。
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
- 预发布：`v0.2.0-rc.1`（后缀递增）。
- tag 打在 `main` 上（`release/<ver>` 合入之后），**annotated**：

```sh
git switch main && git pull --ff-only
git tag -a v0.1.0 -m "VEdit 0.1.0 — first release for CentOS 7"
git push origin v0.1.0
```

## 5. 发布产物（GitHub Release 附件，不进 git）

发版时上传**三个产物 + 校验文件**：

| 附件 | 说明 |
|---|---|
| `VEdit-<ver>-x86_64.AppImage` | 单文件，需要 FUSE（可用 extract-and-run） |
| `vedit-<ver>-linux-x86_64.tar.gz` | 目录包，不需要 FUSE |
| `VEdit-<ver>-x86_64.run` | 单文件自解压，不需要 FUSE |
| `SHA256SUMS` | 三个产物的 sha256 **+ 包内 verible 三件套与 slang 的 sha256** |

**产物绝不提交到 git**（仓库 `.gitignore` 已忽略 `packaging/*/out/`、`*.AppImage` 等）。
`SHA256SUMS` 的生成方式：

```sh
cd <产物目录>
sha256sum VEdit-*x86_64.AppImage vedit-*-linux-x86_64.tar.gz VEdit-*-x86_64.run > SHA256SUMS
# 包内二进制（与 scripts/versions.env 的 pin 对照）：
tar xzf vedit-*-linux-x86_64.tar.gz
sha256sum VEdit/bin/verible-verilog-{ls,lint,format} VEdit/bin/slang >> SHA256SUMS
```

## 6. 发布前检查单

1. `git switch dev && git pull --ff-only`，`bash scripts/gates.sh` 全绿（fmt / clippy / test / 依赖策略）；
2. `bash scripts/build.sh` 全绿（pinned 镜像构建 + Xvfb 冒烟 + GLIBC 断言）；
3. `bash packaging/appimage/run-evidence.sh` 与 `bash packaging/tarball/build.sh` 的**干净无网**验收全绿
   （`--selftest all` 全部通过；数量随版本变化，不要在文档里写死，两个产物各自独立）；
4. 从 `dev` 切出 `release/<ver>`，该分支只做冻结、验证与打包，**不要加新功能**；
5. 证据目录中有本次运行的日志（带时间戳，**不要引用旧绿灯**）；
6. 文档与实际行为一致：`README.md`、`docs/USER_GUIDE.md`、`THIRD_PARTY_NOTICES.md`；
7. `release/<ver>` 以 PR 合入 `main`（merge commit），打 annotated tag，上传三个产物与 `SHA256SUMS`，
   Release notes 写"变更 + 已知限制 + 证据路径"；若发版期间 `dev` 又有新提交，把 `main` 合回 `dev`。

## 7. 热修（hotfix）

从**最近的 tag** 拉分支，PATCH 递增；修完先合 `dev`，再按第 6 节走 release 流程进 `main`：

```sh
git switch -c fix/crash-on-empty-file v0.1.0     # 注意：从 tag 起步
# … 修复 + 测试 …
git push -u origin fix/crash-on-empty-file
gh pr create --base dev --head fix/crash-on-empty-file
# 合入 dev 后按第 6 节发一个 PATCH：release/v0.1.1 → main → tag
```

## 8. 历史纪律

- **永不重写已发布的历史**：不 `amend`、不 `rebase`、不 `force-push` 已被他人拉取的提交。
- 提交信息里的小瑕疵（例如某条 message 丢了一个词）**不靠 amend 修**，靠后续提交/文档说明；
  代码正确性远比 message 措辞重要，而改写历史的代价更高。
