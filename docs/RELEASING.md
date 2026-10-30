# 分支、提交与发版

本项目采用 **trunk-based（主干开发）**：`main` 始终可发布，改动通过**短生命周期分支**
以 **rebase / squash** 合入，历史保持线性，**永不重写**。

## 1. 为什么是 trunk-based，而不是 git-flow

- **本项目的历史本来就是线性的**：截至目前所有提交都直接落在默认分支，没有 merge commit，
  也没有长期存在的 `develop`。git-flow 要求的双长期分支与 release 分支在这里没有对应的需求。
- **并行度低、写作用域不相交**：每个任务有独立的写作用域（路径集），同一时刻同一路径只有一个写者；
  任务板 + 独立评审已经承担了 PR 的"审查 + 合并门槛"职责，不需要再引入一道分支结构。
- **发布产物不进 git**（见第 5 节）：一个 40–48 MB 的二进制每次发版都进历史会让仓库迅速不可用；
  git-flow 的 release 分支常见用途（在分支里固化产物）在这里被 GitHub Release 附件取代。
- **回滚更直接**：`main` 线性 ⇒ 回滚就是 `git revert <sha>` 或重新打一个 tag，不需要在分支图上找落点。

## 2. 分支

| 分支 | 用途 | 生命期 |
|---|---|---|
| `main` | **始终可发布**；线性历史；禁止 merge commit；禁止 `push --force` / 改写历史 | 永久 |
| `feat/<slug>` | 新功能 | 短（合入即删） |
| `fix/<slug>` | 缺陷修复 | 短（合入即删） |
| `chore/<slug>` | 工程/依赖/构建 | 短（合入即删） |
| `docs/<slug>` | 文档 | 短（合入即删） |

**没有 `develop`**，也没有长期 release 分支。

```sh
git switch main && git pull --ff-only
git switch -c feat/format-on-save
# … 开发、提交 …
git fetch origin && git rebase origin/main     # 合入前先 rebase 到最新 main
# 评审通过后，用 squash merge（或 rebase merge）合入，保持线性：
git switch main && git pull --ff-only
git merge --squash feat/format-on-save && git commit
git branch -d feat/format-on-save
```

禁止：`git merge` 产生 merge commit、`git push --force` 到 `main`、`git rebase` 已发布的历史。

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
- tag 打在 `main` 上，**annotated**：

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

1. `bash scripts/gates.sh` 全绿（fmt / clippy / test / 依赖策略）；
2. `bash scripts/build.sh` 全绿（pinned 镜像构建 + Xvfb 冒烟 + GLIBC 断言）；
3. `bash packaging/appimage/run-evidence.sh` 与 `bash packaging/tarball/build.sh` 的**干净无网**验收全绿
   （`--selftest all` 51/51，两个产物各自独立）；
4. 证据目录中有本次运行的日志（带时间戳，**不要引用旧绿灯**）；
5. 文档与实际行为一致：`README.md`、`docs/USER_GUIDE.md`、`THIRD_PARTY_NOTICES.md`；
6. 打 annotated tag，上传三个产物与 `SHA256SUMS`，Release notes 写"变更 + 已知限制 + 证据路径"。

## 7. 热修（hotfix）

从**最近的 tag** 拉分支，PATCH 递增，修完合回 `main`，重新打 tag：

```sh
git switch -c fix/crash-on-empty-file v0.1.0     # 注意：从 tag 起步
# … 修复 + 测试 …
git switch main && git merge --squash fix/crash-on-empty-file && git commit
git tag -a v0.1.1 -m "VEdit 0.1.1 — fix crash on empty file"
```

## 8. 历史纪律

- **永不重写已发布的历史**：不 `amend`、不 `rebase`、不 `force-push` 已被他人拉取的提交。
- 提交信息里的小瑕疵（例如某条 message 丢了一个词）**不靠 amend 修**，靠后续提交/文档说明；
  代码正确性远比 message 措辞重要，而改写历史的代价更高。
