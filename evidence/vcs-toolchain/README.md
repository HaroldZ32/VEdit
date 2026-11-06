# 批 6a · 随包 VCS 工具链证据（git 2.53 + subversion 1.14，requirements rev3）

任务：`svn-bundle-packaging`（board **T23**，cell `cell-20261103T123639841Z`）。
契约：`.mpd/cellwork/cell-20261103T123639841Z/contracts/svn-bundle-packaging.md`（split rev1）
——**由 requirements rev3 取代**（captain 书面裁定，见 §0）。写者：Deep Worker。评审：Reviewer。

## 0. 版本变更（supersession，必读）

| 项 | 冻结契约（rev1/rev2） | **rev3（cap 裁定，本任务执行）** |
|---|---|---|
| SVN | 1.7.14 | **1.14.5**（>= 1.14） |
| git | 仅系统 git（*兼容* 1.8.3.1） | **随包 2.53.0**（>= 2.43），1.8.3.1 兼容约束**取消** |
| 解析 | 仅 PATH | **bundled > PATH > env（`VEDIT_GIT`/`VEDIT_SVN`）**，且可观测 |
| `svn info --show-item` | 禁止（1.9+） | **允许**（>=1.9）；T39 的 `svn info --xml` 保留为兼容写法 |
| 证据目录 | `evidence/svn-bundle/` | **`evidence/vcs-toolchain/`**（cap 书面确认） |

**没有下载、没有固定任何 1.7.14 产物**；本目录取代旧契约的版本口径。

## 1. 一屏结论

| 判据 | 观测 |
|---|---|
| 随包版本 | 干净 CentOS 7.9.2009（glibc 2.17）容器里 `/app/bin/git --version` → **2.53.0**；`/app/bin/svn --version --quiet` → **1.14.5** |
| 依赖库 | 7 个 sha256 固定的 `.deb`（git/subversion/libsvn1/APR/APR-util/serf/utf8proc）解包到 `usr/lib/vcs/`；`ldd` 闭包 **57** 个库进 `usr/lib/vcs/lib/`，与产品 `lib/` 互不遮蔽 |
| 解析顺序 | 产品 VCS lane 打印 `VCS-TOOLCHAIN git=/app/usr/lib/vcs/bin/git version=2.53.0 svn=/app/usr/lib/vcs/bin/svn version=1.14.5 **source=bundled**`；面板同时也把值写成属性 |
| 干净容器自检 | `AppRun --selfcheck` → `ok=1 checks=290 failures=0` |
| AC-3.0 功能集 | 全部 PASS：status / info / diff / refs / history / workspace / **worktree add-list-remove**（13 项 CHECK） |
| svnadmin | **不随包**（AppDir 内断言缺席；只在 `build/vcs-tools/` 供 6b E2E） |
| 打包链 | `bash packaging/runtime/build.sh` → `steps_failed=0`、`BUILD: F2 runtime OK`；AppImage **129,952,248 B**，sha256 `204b6645…7003a5e` |
| 许可 | THIRD_PARTY_NOTICES §11：git **GPL-2.0-only**、Subversion/APR/APR-util/serf **Apache-2.0**、utf8proc MIT + 源码可得性 |
| 门禁 | 七道全绿（§6） |

## 2. 捆绑布局与启动机制

```
AppDir/
  bin/git                    -> ../usr/lib/vcs/bin/git          (包装脚本)
  bin/svn                    -> ../usr/lib/vcs/bin/svn
  usr/lib/vcs/
    bin/git, bin/svn         包装脚本：readlink 求 AppDir → 用 AppDir 自带 loader 启动
    libexec/git, libexec/svn 发行版原始二进制（未修改）
    libexec/helpers/*        git 的外部 helper（git-remote-http 等，各自有包装脚本）
    git-core/*               git 内建命令（符号链接到 ../bin/git；argv0 分派）
    lib/*                    git/svn 的依赖库（57 个，排除 glibc 家族）
    share/git-core/templates
```

包装脚本要点（`packaging/runtime/assemble-appdir.sh` 生成）：

- 通过 `readlink -f "$0"` 找到真实路径，再从 `<appdir>/usr/lib/vcs/bin[/git-core]` 上溯四级得到 AppDir；
- 执行 `<appdir>/ld-linux-x86-64.so.2 --library-path <appdir>/usr/lib/vcs/lib:<appdir>/lib …`——
  与产品相同的私有 loader 机制，VCS 进程**先**看自己的库目录；
- git 用 loader 的 `--argv0` 传子命令名（内建命令是 `git-core/<name> -> ../bin/git`，
  glibc ≥2.33 的 `--argv0` 让它按内建分派，不依赖符号链接的真实二进制）；
- helper 与子命令都会把 `<appdir>/bin` 前置到 PATH（git 会重新 exec 自己，如
  `git maintenance run`、`worktree add` 里的 `reset`）。

**两个实测坑（已修，见 §7）**：loader 启动时 `applicationDirPath()` 指向 AppDir 根而非 `bin/`；
产品继承的 `LD_LIBRARY_PATH` 会让包装脚本的宿主 `/bin/sh` 加载随包 glibc 而失败（CentOS 7 的
ld.so 2.17 无法加载 glibc 2.43）。

## 3. 产品侧解析器（bundled > PATH > env）

- Rust（真正决定 spawn 哪个程序）：`crates/vd-vcs/src/toolchain.rs`；候选顺序
  `<exe>/../usr/lib/vcs/bin/<tool>`、`<exe>/usr/lib/vcs/bin/<tool>`、`<exe>/<tool>`、
  `<exe>/bin/<tool>`，再 PATH，最后 `VEDIT_GIT`/`VEDIT_SVN`。读/写 runner 在**闸门之后**
  调用它，闸门与写白名单语义未变。
- spawn 时对 VCS 子进程移除 `LD_LIBRARY_PATH`（见 §7.2）；`QProcess` 探针同样移除。
- 可观测：C++ lane（`src/workspace/vcs/vcs_toolchain.cpp`）实现同一顺序，在 lane 安装时打印
  **一行** `VCS-TOOLCHAIN git=<path> version=<v> svn=<path> version=<v> source=<bundled|path|env|mixed|missing> git_source=… svn_source=…`，
  并把 `veditVcsToolchain` / `veditVcsGitPath` / `veditVcsGitVersion` / `veditVcsSvnPath` /
  `veditVcsSvnVersion` 写成面板属性供探针读取（`vcs_integration.cpp`）。
  lane selftest 断言：行形状、source 合法、fake AppDir 的 bundled 判定（+7 checks）。

## 4. 干净容器验证（`logs/30-clean-container.log`）

```
== host ==           CentOS Linux release 7.9.2009 (Core) / ldd 2.17
== bundled versions ==  git version 2.53.0 / 1.14.5
== AppRun --selfcheck == APP-SHELL-SELFTEST ok=1 checks=290 failures=0  (selfcheck_exit=0)
== AC-3.0 functional set ==
CHECK git>=2.43 PASS 2.53.0      CHECK svn>=1.14 PASS 1.14.5
CHECK status-branch-and-change PASS   ## master | M rtl/top.sv
CHECK info-branch-and-revision PASS   master @ 183e20f
CHECK diff-shows-change PASS          diff --git a/rtl/top.sv b/rtl/top.sv
CHECK refs-list PASS                  refs/heads/master
CHECK history-log PASS                183e20f fixture commit
CHECK workspace-toplevel PASS         /tmp/vedit-vcs-toolchain-…/repo
CHECK worktree-add-list PASS          add=0  (secondary worktree listed)
CHECK worktree-remove PASS            add=0 remove=0
CHECK svn-binary-runs PASS            svn, version 1.14.5 (r1922182)
CHECK svnadmin-absent PASS            build-only
ac30_fail=0
== product resolver line ==
VCS-TOOLCHAIN git=/app/usr/lib/vcs/bin/git version=2.53.0 svn=/app/usr/lib/vcs/bin/svn version=1.14.5 source=bundled git_source=bundled svn_source=bundled
```

夹具全部在 `mktemp -d`，探针启动断言 `pwd` 不是 VEdit 工作树。

## 5. deb pin 验证（`logs/10-deb-pins.log`）

| 包 | 版本 | 结果 |
|---|---|---|
| git | 2.53.0-1ubuntu1.1 | sha256 PASS |
| subversion | 1.14.5-6build1 | sha256 PASS |
| libsvn1 | 1.14.5-6build1 | sha256 PASS |
| libapr1t64 | 1.7.6-3 | sha256 PASS |
| libaprutil1t64 | 1.6.3-3ubuntu3.1 | sha256 PASS |
| libserf-1-1 | 1.3.10-3ubuntu2 | sha256 PASS |
| libutf8proc3 | 2.10.0-2 | sha256 PASS |

URL 与 sha256 在 `scripts/versions.env` 的 `VCS_DEB_URLS`/`VCS_DEB_SHA256S`（1:1 对应）；
`assemble-appdir.sh` 每次打包都重新校验，缓存目录 `packaging/runtime/.cache/vcs-debs/`。

## 6. 七道门禁

| # | 命令 | 结果 |
|---|---|---|
| 1 | `bash scripts/build-qt.sh` | exit 0（Rust 层 + build + ctest + xcb smoke） |
| 2 | ctest | 38/38 passed, 0 failed（`vedit-workspace-vcs-selftest` 558 checks / 0 failures，含本任务 +7） |
| 3 | `vedit-app --selfcheck`（镜像内） | exit 0，290 checks / 0 failures |
| 4 | `cargo clippy --workspace --all-targets -- -D warnings` | exit 0 |
| 5 | `cargo fmt --all -- --check` | exit 0 |
| 6 | `bash build/gates/docs.sh` | exit 0 |
| 7 | `lrelease translations/vedit_zh_CN.ts` | **880 finished / 0 unfinished**（VCS-TOOLCHAIN 是机器记号行，与既有 TOOLCHAIN 行同样不进 `.ts`） |

打包链另计：`bash packaging/runtime/build.sh` → `steps_failed=0`、`BUILD: F2 runtime OK`
（AppDir 347 文件；AppImage **129,952,248 B**，sha256 `204b6645…7003a5e`；CentOS 7 探针含 `CHECK PASS bundled VCS toolchain in the clean container`）。

## 7. 实测发现（代码里已处理）

1. **loader 启动时 `applicationDirPath()` 是 AppDir 根**：`AppRun` 用 `<appdir>/ld-linux…` 启动产品，
   `/proc/self/exe` 指向 loader，Qt 返回 loader 所在目录。因此解析器同时探测
   `<exe>/../usr/lib/vcs/bin/<tool>` 与 `<exe>/usr/lib/vcs/bin/<tool>` 两种布局（Rust 与 C++ 一致）。
2. **`LD_LIBRARY_PATH` 不能泄漏给包装脚本**：产品环境带着随包 `lib/`，包装脚本由宿主 `/bin/sh`
   启动；CentOS 7 的 ld.so 2.17 加载随包 glibc 2.43 直接失败
   （`version GLIBC_2.35 not found`）。读/写 runner 对 VCS 子进程 `env_remove("LD_LIBRARY_PATH")`，
   lane 的 `QProcess` 版本探针同样移除；包装脚本再用 AppDir loader 显式指定库路径。
3. **git 子命令分派**：发行版把内建命令做成指向 `git` 的符号链接；直接复制会在宿主 loader 下失败。
   我们用 `--argv0` 分派 + helper 包装脚本（见 §2）。
4. `git worktree add` 需要 git ≥ 2.5（随包 2.53 满足），且会重新 exec `reset`——PATH 前置已覆盖。
5. **打包自检存在 pipefail/EPIPE 竞态（已修）**：`packaging/runtime/selfcheck.sh` 用
   `printf '%s' "$OUT" | grep -q …` 检查产品输出；输出变长后 `grep -q` 在首个匹配处退出，
   写端收到 EPIPE，`set -o pipefail` 把检查判成 FAIL（实测一次 `CHECK FAIL toolchain is not fully bundled`
   但同一 AppImage 的 `AppRun --selfcheck` 是 ok=1）。改为先把输出写文件再一次 grep，消除竞态。

## 8. 复现

```bash
bash evidence/vcs-toolchain/run-all.sh
# logs/: 00-context, 10-deb-pins, 20-assemble, 30-clean-container, 40-resolver
```

单步：
- 只打包：`bash packaging/runtime/assemble-appdir.sh --require-toolchain`（镜像内）
- 完整打包链（AppDir→AppImage→隔离→CentOS 探针）：`bash packaging/runtime/build.sh`
- 干净容器功能集：`docker run --rm -v packaging/runtime/out/AppDir:/app:ro <centos7> bash /evidence/ac30.sh`

## 9. 改动清单

| 路径 | 内容 |
|---|---|
| `scripts/versions.env` | 新增 `VCS_*` pins（7 个 deb 的 URL+sha256、版本与最小值） |
| `packaging/runtime/assemble-appdir.sh` | 新增 5c 段：下载/校验/解包、wrapper、git-core、库闭包、版本自检、`vcs-toolchain.txt`、许可文本；delivery invariants 与 svnadmin 缺席断言 |
| `packaging/runtime/AppRun` | `$APPDIR/bin` 前置到 PATH（子进程/hook 一致） |
| `packaging/runtime/build.sh` | 1b 增加 VCS 文件/版本/svnadmin 断言；7/7 CentOS 探针增加版本与 svnadmin 断言 |
| `packaging/runtime/.gitignore` | 忽略 `.cache/` |
| `crates/vd-vcs/src/toolchain.rs` | **新增** bundled > PATH > env 解析器 + 错误说明 |
| `crates/vd-vcs/src/runner.rs` / `write.rs` | spawn 前解析；移除 `LD_LIBRARY_PATH`；闸门与写白名单不变 |
| `crates/vd-vcs/tests/toolchain.rs` | **新增** 8 个解析顺序测试（三分支 + 优先级 + 缺失 + 非可执行 + 候选布局） |
| `src/workspace/vcs/vcs_toolchain.{h,cpp}` | **新增** 同序解析 + 版本探针 + `VCS-TOOLCHAIN` 行 |
| `src/workspace/vcs/vcs_source.cpp` | lane 初始化打印一次 |
| `src/workspace/vcs/vcs_integration.cpp` | lane 安装时打印 + 面板属性 |
| `src/workspace/vcs/vcs_selftest.cpp` | +7 checks（行形状/source/AppDir/版本/fake bundled 布局） |
| `src/workspace/vcs/CMakeLists.txt` | 接入 `vcs_toolchain.cpp` |
| `THIRD_PARTY_NOTICES.md` | §11：git GPLv2 / subversion+APR+serf Apache-2.0 / utf8proc MIT + 源码可得性 + 许可文本随包 |

`translations/vedit_zh_CN.ts` 未改：`VCS-TOOLCHAIN` 与既有 `TOOLCHAIN` 一样是机器记号行（G7 判据），
不是用户句子。

## 10. 边界（未做/不在本任务）

1. **真实 SVN 工作副本 E2E 不在本任务**：`svnadmin` 只解包到 `build/vcs-tools/`（构建期工具；
   已实测 `build/vcs-tools/svnadmin --version --quiet` → **1.14.5**，宿主与构建镜像内均可用），
   真实 checkout/status/commit/update 由批 6b（AC-6.2）执行；本任务只证明 `svn 1.14.5` 随包可运行、
   依赖闭包完整、svnadmin 不随包。
2. **HTTPS 远端未做真实网络 E2E**：`git-remote-http` 与 libcurl 闭包已随包；验收只跑本地夹具
   （AC-4.1 也只用本地裸仓库）。
3. **性能/SVN 1.7 兼容**：rev3 已取消 1.8.3.1 兼容约束；T39 的 cwd + `--porcelain` 写法保留为健壮性，
   `svn info --xml` 保留为兼容写法。
4. 打包链自身重写 `evidence/slice3-pack/`、`evidence/m0-build/`、`evidence/ffi-abi/logs/` 的
   gate 日志（项目门禁的既有行为，非手工改动）。
