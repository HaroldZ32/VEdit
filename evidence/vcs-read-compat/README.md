# 批 3a-fix · VCS 读路径兼容交付基线证据（AC-3.0，含 SVN 1.7 参数扫描）

任务：`vcs-read-compat`（board **T39**，cell `cell-20261103T123639841Z`，split rev2）。
契约：`.mpd/cellwork/cell-20261103T123639841Z/contracts/vcs-read-compat.md`。
写者：Deep Worker（承接被 fenced 的 Senior Engineer 交接）。评审：Architect（`review-vcs-read-compat`）。

本文件只记录**命令、环境、退出码与关键输出**；实现细节在代码里。

## 0. 一屏结论

| AC | 观测 |
|---|---|
| (1) `current_dir` 进 seam | `CommandRunner::run(program, working_tree, args)`；`SystemRunner` 用 `Command::current_dir`；读闸门仍是 spawn 前第一步；写路径（`SystemWriteRunner`）本就用 `current_dir`，写白名单语义未动 |
| (2) status 兼容写法 | `git status --porcelain --branch`（无 `=v1`）；其余读子命令逐条实测（§2 矩阵） |
| (3) 双 git + shim 证据 | 1.8.3.1（`vedit-build:local`）矩阵 + Rust/ABI 真仓库测试；2.53.0（`vedit-qt6-build:local` / 宿主机）lane selftest + 矩阵；受控 shim：`rejected=0 forwarded=40` |
| (4) 零残留 | `grep` 命中共 0（`"-C"` / `'-C'` / `--porcelain=v1` / `--show-item`），源码与测试均 0 |
| (5) 读闸门不放宽 | `readonly.rs` + `compat.rs` 全部写子命令仍被读闸门在 spawn 前拒绝；写白名单/异步 job 未改 |
| (6) SVN 1.7 扫描 | `svn info --show-item`（1.9+）已改 `svn info --xml`（1.0+）解析；其余 SVN 读参数均 1.7 可用；无 svn 二进制，真实 E2E 归 6b |
| (7) 断言只增不减 | Rust：`abi` 16→17、`vd-vcs` lib 6→8、新增 `compat` 3；lane selftest **48 → 58 checks / 0 failures** |
| (8) 七道门禁 | 全绿（§5），`build-qt` exit 0、ctest 38/38、smoke ok=1、selfcheck 290/0 |

## 1. 缺陷与修复

**修复前（字段实测）**：读路径给 `git` 传全局路径选项 + `--porcelain=v1`；交付基线 git 1.8.3.1 两者都不认：

```
$ git -C "$d" status --porcelain=v1 --branch     # git 1.8.3.1
Unknown option: -C                                (exit 129)
$ git status --porcelain=v1 --branch             # 1.8.3.1，去掉路径选项也一样
error: option `porcelain' takes no value          (exit 129)
```

后果：CentOS 7 上 VCS 面板读不出 backend/branch/changes/diff。

**修复**：
- seam 携带工作目录（`Invocation.working_tree`、`CommandRunner::run(program, working_tree, args)`、
  `SystemRunner::new(..).current_dir(working_tree)`），读闸门 `ensure_read_only` 仍是 spawn 前第一步；
- `status_with`：`git status --porcelain --branch`，cwd = 仓库根；
- `ProcessVcsService::git()` / `svn()`：不再拼全局路径选项，cwd = 仓库根；
- `subcommand_index` 不再跳过路径选项：若有人重新传入，首个非选项参数会变成未列子命令 → 默认拒绝；
- 写路径（`write.rs`）本就用 `current_dir` + 仓库相对路径，本轮只清理注释，**白名单/异步语义零改动**。

## 2. 兼容矩阵（同一条命令，两个 git）

`logs/10-matrix-1831.log`（`vedit-build:local`，**git 1.8.3.1**）：

```
status             exit=0   | ## master| M rtl/a.sv|
rev-parse-abbrev   exit=0   | master
rev-parse-short    exit=0   | 289a85e
diff-worktree      exit=0   | diff --git a/rtl/a.sv b/rtl/a.sv|index 0fb97fc..a0e8734 100644|
diff-cached        exit=0   |
diff-no-index      exit=1   | diff --git a/rtl/b.sv b/rtl/b.sv|new file mode 100644|   # 有差异时 exit 1，符合预期
legacy-path-option exit=129 | Unknown option: -C|usage: git [...]          # 负控制
legacy-porcelain-v1 exit=129| error: option `porcelain' takes no value|...   # 负控制
```

`logs/20-matrix-current.log`（宿主机，**git 2.53.0**）：同样的读命令全部 exit 0，输出一致；
两条 legacy 写法在 2.53 上 exit 0 —— 这正是旧缺陷在开发机上不可见的原因。

结论：`--porcelain` 在 1.8.3 与 2.53 上输出同一 v1 格式；`rev-parse --abbrev-ref/--short`、
`diff [--cached|--no-index] --no-color -- <path>` 两版一致。`diff --no-index` 的 exit 1 是
"有差异"的正常语义，服务层读取输出、不把退出码当请求失败。

## 3. 受控 git shim（第二证据）

`shim/git`（**仅证据，不进产品**）被前置到 `PATH`：记录 `cwd`+argv，拒绝两个 legacy 写法（exit 129，
与 1.8.3 一致），其余转发 `/usr/bin/git`。用它跑产品真实读测试（`compat` 3 tests + `abi` 的
`vcs_read` 测试）：

```
legacy-path-option exit=129
legacy-porcelain-v1 exit=129
test result: ok. 3 passed; 0 failed   (compat)
test result: ok. 1 passed; 0 failed   (abi vcs_read)
rejected=0  forwarded=40  outside_tmp=0  workspace_hits=0  fixture_reads=4
shim_probe=PASS
```

`rejected=0`：产品读路径一次都没有发出被拒绝的写法；`fixture_reads=4`：status 读取确实发生在
`/tmp/.tmp*` 临时 fixture 内（cwd 隔离）；`workspace_hits=0`：没有任何 git 进程在 VEdit 工作树里跑。

## 4. grep 证明（源码 + 测试）

`logs/40-grep.log`：

```
grep -rnE -- "\"-C\"|'-C'" crates/ src/workspace/vcs/     count=0
grep -rn -- "--porcelain=v1" crates/ src/workspace/vcs/  count=0
grep -rn -- "--show-item"    crates/ src/workspace/vcs/  count=0
source_hits=0   grep_probe=PASS
```

仅存的字面量在**证据**里（`matrix.sh` 的负控制、`shim/git` 的拒绝规则），用于证明旧写法必失败；
产品源码与 selftest/测试为 0。更早的 `evidence/vcs-write/README.md` 是 T11 对同一缺陷的历史记录
（只读历史证据，按 Non-goal C8 未改）。

## 5. 七道门禁（最终树）

| # | 命令 | 结果 | 日志 |
|---|---|---|---|
| 1 | `bash scripts/build-qt.sh` | **exit 0**（Rust 层 + configure/build + ctest + xcb smoke） | `logs/40-gate-build-qt-summary.log` |
| 2 | ctest（同上） | **38/38 passed, 0 failed**；`#37 vedit-workspace-vcs-selftest` Passed | 同上 |
| 3 | `vedit-app --selfcheck`（镜像内 offscreen） | **exit 0，290 checks / 0 failures** | `logs/60-gate-selfcheck-lrelease.log` |
| 4 | `cargo clippy --workspace --all-targets -- -D warnings` | **exit 0** | `logs/50-gate-clippy-fmt.log` |
| 5 | `cargo fmt --all -- --check` | **exit 0**（容器 + 主机 `RUSTUP_TOOLCHAIN=stable`） | `logs/50-*.log`、`logs/70-gate-docs-fmt.log` |
| 6 | `bash build/gates/docs.sh` | **exit 0** | `logs/70-gate-docs-fmt.log` |
| 7 | `lrelease translations/vedit_zh_CN.ts` | **607 finished / 0 unfinished** | `logs/60-gate-selfcheck-lrelease.log` |

Rust 测试（`build-qt` 的 Rust 阶段，`vedit-build:local` = git 1.8.3.1）：`vd-ffi` lib 45/45、
`abi` **17/17**、`qt_abi_guard` 1/1；`vd-vcs` lib **8/8**、`compat` **3/3**、`readonly` 10/10、
`write` 9/9。`vd_ffi.h` sha256 仍为 `53a53985…3bc247a`（无 ABI 变化）。

## 6. 断言只增不减

| 位置 | 之前 | 之后 |
|---|---|---|
| `crates/vd-vcs/tests/readonly.rs` | 9 tests | **10 tests**，且 git 会话逐条断言「首个参数=子命令」「cwd=仓库根」「status 参数恰为 `status --porcelain --branch`」；svn 会话断言 cwd |
| `crates/vd-vcs/tests/compat.rs`（新） | — | **3 tests**：真实 git 读 status/info/diff；双仓库 cwd 隔离；读闸门仍拒绝全部写子命令（SystemRunner + RecordingRunner） |
| `crates/vd-ffi/tests/abi.rs` | 16 tests | **17 tests**：+1 真实仓库 C ABI 读（status/info/diff 三个 job） |
| `src/workspace/vcs/vcs_selftest.cpp` | 48 checks | **58 checks / 0 failures**：+10 读兼容（git 可用、fixture、双仓库、status、cwd 隔离、info、diff、读路径零写入） |
| `crates/vd-vcs/src/service.rs`（单元测试） | 6 tests | **8 tests**：+2 `svn info --xml` 解析（有值/空态） |

「非法子命令仍拒」的证明在 `readonly.rs` 与 `compat.rs`（两者都在 `build-qt` 的 Rust 阶段运行）；
lane selftest 增加的是「旧参数集下读路径成功 + cwd 隔离 + 读路径零写入」（HEAD 与 porcelain 前后一致）。

## 7. SVN 1.7 参数扫描（AC-3.0 item 6，供 6b 引用）

`logs/60-svn-scan.log` 结论：

```
svn status <path>                since 1.0   compatible with the bundled 1.7.14
svn info --xml <path>            since 1.0   compatible; replaced the 1.9-only single-field selector form
svn diff --internal-diff <path>  since 1.7   compatible with 1.7.14
svn log/list/cat/propget/proplist/blame   allow-listed, but not issued by the service today
conclusion=no 1.8+ option remains in the SVN read path
scan_fail=0
```

- 发现：`ProcessVcsService::info()` 原用 `svn info --show-item revision|url`（**1.9+**），1.7.14 会失败；
  已改为一次 `svn info --xml`，从 `<entry revision="…">` 与 `<url>…</url>` 取同一信息（新增 2 个解析单测）。
- 扫描的 1.8+/1.9+ 选项（`--show-item`、`--include-externals`、`--search`、`--accept`、`--parents`、
  `--pin-externals`）在源码中命中 **0**。
- **本镜像与主机都没有 `svn`**（`NO_SVN`），所以 SVN 的真实 subprocess E2E 属批 6b（rev2 §126）；
  本任务只交付「参数集在 1.7 合法」的扫描结论。

## 8. 复现

```bash
bash evidence/vcs-read-compat/run-all.sh
# logs/: 00-context, 10-matrix-1831, 20-matrix-current, 30-shim, 40-grep, 60-svn-scan
```

- 每个探针启动第一句断言 `pwd -P != 工作树`，全部工作在 `mktemp -d`；
- `matrix.sh` 可在任意机器上单独跑：`bash matrix.sh <workspace> <label>`（cwd 不得是 workspace）。

## 9. 改动清单

| 路径 | 内容 |
|---|---|
| `crates/vd-vcs/src/runner.rs` | `Invocation.working_tree`；`CommandRunner::run(program, working_tree, args)`；`SystemRunner.current_dir`；`RecordingRunner` 记录 cwd；`subcommand_index` 不再跳过路径选项 |
| `crates/vd-vcs/src/lib.rs` | `status_with`：`status --porcelain --branch` + cwd（git/svn 两路） |
| `crates/vd-vcs/src/service.rs` | `git()`/`svn()` 走 cwd；`svn info --xml` + 解析器；+2 单测 |
| `crates/vd-vcs/src/write.rs` | 仅注释（写路径本就 cwd；白名单语义未动） |
| `crates/vd-vcs/tests/readonly.rs` | seam 签名 + cwd/参数断言（10 tests） |
| `crates/vd-vcs/tests/compat.rs` | **新增**：真实 git 读兼容 3 tests |
| `crates/vd-vcs/tests/write.rs` | 仅注释 |
| `crates/vd-ffi/tests/abi.rs` | **+1** 真实仓库 C ABI 读测试；注释清理 |
| `crates/vd-ffi/include/vd_ffi.h` | 未变（sha256 `53a5398…`，无 ABI 变化） |
| `src/workspace/vcs/vcs_selftest.cpp` | **+10 checks**：真实仓库读兼容 + cwd 隔离 + 读路径零写入 |
| `evidence/vcs-read-compat/**` | 本目录 |

冻结文件、依赖、写白名单与异步 job 语义均未改。

## 10. 边界与未做

1. **SVN 真实 subprocess E2E 未做**：环境无 `svn`（1.7.14 二进制在批 6 随包），归 6b（rev2 §126）。
2. **`--porcelain` 与 `--porcelain=v1` 的等价性**只由矩阵与真仓库测试证明（1.8.3.1 / 2.53.0 输出一致），
   未逐一覆盖 git 1.8.3 之前或 3.x 的版本。
3. **`svn info --xml` 解析是最小字符串解析**（不引入 XML 依赖）：只取 `<entry revision>` 与 `<url>`；
   URL 中的 XML 实体（`&amp;`）未解码，SVN URL 里极少出现，批 6b 若需要可再收紧。
4. 读路径 `diff --no-index` 的 exit 1 语义未改（原有行为），矩阵已注明。
