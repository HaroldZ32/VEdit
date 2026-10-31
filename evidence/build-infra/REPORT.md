# T58 — build-infra：pipefail+SIGPIPE 假红修复 + 同类审计 + 负例证伪

collected_at: 2026-10-31T19:24:00+08:00
git_head: 95c496e682e3841add1a081edd78d29c34957aac
（提交由 Lead 统一负责；本任务不 commit、不 push。下文的改动都在工作区。）

无头声明：本机没有显示服务器。本报告的全部结论来自脚本、构建、ctest、`nm`、sha256 与容器内运行；没有任何“看起来正确”的目视结论。

---

## 1. 结果一览

| 项 | 修前 | 修后 |
|---|---|---|
| `bash build/gates/typecheck.sh` | `TYPECHECK-EXIT=1`，C++ 半段打印 **假** `FATAL: vedit-app does not export vd_abi_version`（`evidence/project-model/17-typecheck-gate.log:572`，而同一次运行的 `nm` 列表第 530 行就有 `T vd_abi_version`、`## count 36`） | `TYPECHECK-EXIT=0`（`06-typecheck-final.log:708`；`04-typecheck-after.log:702-703` 同值） |
| `bash scripts/build-qt.sh`（一命令全集） | 在 ABI 断言处中断（同一假红） | **`BUILD-QT-EXIT=0`**，`100% tests passed, 0 tests failed out of 16`，`M0-SMOKE ok=1`（`05-build-qt-full.log`） |
| `build/gates/gates.sh`（artifact 在位） | 绿（该次） | 绿：`gate with restored artifact: exit=0`（`02-slang-missing-branch.log`） |
| `evidence/gate-infra/11-typecheck-image-id.txt` | 每次 typecheck 都被覆盖（T54 的证据被后续运行改写） | sha256 运行前后不变：`ed3d3926…`（`06-typecheck-final.log` 的 before/after 两行） |

## 2. 改动清单（写域内，显式路径）

| 文件 | 改动 | 作用 |
|---|---|---|
| `scripts/build-qt-in-container.sh` | `nm -C "$APP" \| grep -qE ' T vd_abi_version$'` → `grep -E … > /dev/null`（+ 注释说明 pipefail/SIGPIPE） | **主修复**：检查内容不变（命中=0，缺失=1→FATAL），只消除 SIGPIPE 假红 |
| `build/gates/typecheck.sh` | ① `EVID` 从 `evidence/gate-infra` 改为 `build/evidence/typecheck`；② 每次运行把 `00-run.txt`（采集时间 + `git rev-parse HEAD` + `git status --porcelain`）写进**门禁自己的**日志目录 | **追加 A**：停止“证据腐烂”；门禁日志可自描述 |
| `packaging/runtime/assemble-appdir.sh` | `! find … \| grep -q . \|\| {ERROR}` → `[ -n "$(find … -print -quit)" ]` | **假绿**修复：`syntax-bundled/` 真的存在时原来会被 SIGPIPE 判成不存在 |
| `packaging/runtime/verify-not-shipped.sh` | 同上（文件与目录两处）；`strings -a -el … \| grep -qx` → `grep -x … > /dev/null` | **假绿**修复（合规门禁）：发货的 `systemverilog.xml`/`verilog.xml`/`syntax-bundled/` 或内嵌定义原来可能被报成“干净” |
| `packaging/runtime/build.sh` | `ldconfig -p \| grep -m1 libfuse` → `grep libfuse \| sed -n '1p'` | 假诊断修复：ldconfig 全库列表可超过管道缓冲 |
| `packaging/appimage/clean-room-accept.sh` | `fc-list :lang=zh \| grep -q 'wqy-zenhei'` → `grep … > /dev/null` | 假红修复：字体列表可超过管道缓冲 |
| `packaging/tarball/stage.sh` | `strings … \| grep … \| sort -u \| head -5` → `sed -n '1,5p'` | 假红修复：`strings` 输出可达数 MB |
| `scripts/build-ksh-selfbuild-in-container.sh` | `comm -13 … \| head -20` → `sed -n '1,20p'` | 假红修复：符号集差异可能很大且只是信息性输出 |
| `build/gates/pack.sh` | `printf … \| grep -qE …` → `grep -E … > /dev/null` | 假红修复：归档条目列表随包增长 |

完整审计表（66 处管道、10 处修复、56 处有理由留下）见 **`AUDIT.md`**，原始搜索输出见 `00-raw-audit.txt`。

注：`build/gates/typecheck.sh` 的 `git diff` 里还包含 **T54 的未提交委派块**（Rust 半段进镜像），那不是本任务的改动；本任务只加了上面 ① ② 两项。

## 3. 负例证伪（关键）：检查仍能真判红

`01-negative-check.log`（`run-negative-check.sh`，在 pinned 镜像 `vedit-qt6-build:local` 内执行——
与生产门禁同一环境）记录：

```
match + OLD (grep -q), unbounded producer     -> status=141 FALSE-FAIL (producer dies on the broken pipe; the real gate logged 141)
match + OLD (grep -q), bounded producer       -> status=141 FALSE-FAIL (producer write error; any non-zero reads as a missing symbol)
match + NEW (grep ... >/dev/null), bounded    -> status=0   PASS
no match + NEW (grep ... >/dev/null), bounded -> status=1   FAIL (correct)

## 2. the exact fixed predicate against real and symbol-less binaries
real vedit-app                 predicate=0  vd_* count=36
shim without vd_abi_version    predicate=1  vd_* count=5
/bin/true (no vd_* at all)     predicate=1  vd_* count=0

## 3. script-level verdict (0 = passes to the next step, 1 = FATAL + exit)
real vedit-app                 verdict=0
shim without vd_abi_version    verdict=1
/bin/true (no vd_* at all)     verdict=1
```

- 负例 1：`gcc -shared` 编出的 shim，导出 5 个 `vd_*` 符号但**没有 `vd_abi_version`** → 修复后的谓词返回 1，脚本级 verdict=1（FATAL），第二道 `<20` 计数也 FATAL。用临时目录编译，仓库零改动。
- 负例 2：`/bin/true`（完全没有 `vd_*`）→ 同样判红。
- 正例：真实 `vedit-app`（`nm` 里 36 个 `T vd_*`，含 `vd_abi_version`）→ 0，门禁继续。
- **没有削弱检查**：没有 `|| true`、没有 `set +o pipefail`、没有删检查、没有把 FATAL 降级；`grep -c <20` 的第二道 FATAL 原样保留。
- 关于“必然触发”的更正：SIGPIPE 是**潜伏/时序相关**的——同一二进制在不同符号布局/调度下可能 141、可能 4、也可能不触发；本任务在 T6 的真实门禁运行里记录到了 141→假 FATAL（`evidence/project-model/17-typecheck-gate.log`）。因此负例证据既在容器里复现了 141（与真实门禁一致），也用**确定性合成流**（早命中且仍在写）复现机制，而不是依赖当次二进制布局。修复消除的是这一类，而不是某一次。

## 4. 追加 A：证据腐烂（typecheck 不再改写 T54 的任务证据）

- 修前：`build/gates/typecheck.sh:53` `EVID="$ROOT/evidence/gate-infra"`，于是**每一次** typecheck 都会覆盖 `evidence/gate-infra/10-typecheck-image-build.log` / `11-typecheck-image-id.txt`。这违反证据可信度：任务证据应当是一次交付的不可变记录；一个**可重复运行的自动化步骤**在别人后续重跑时静默改写它，评审读到的就不再是当初交付的那份字节，且无法区分“评审重跑”和“作者回填”。
- 修后：`EVID="$ROOT/build/evidence/typecheck"`（仓库既有惯例：`scripts/gates.sh` 用 `build/evidence/`）。运行前后 T54 文件的 sha256：
  ```
  before: ed3d3926d3376f2b0325e46183b668ac0bc442aa31011c00dd30ba7f27264714  evidence/gate-infra/11-typecheck-image-id.txt
  after : ed3d3926d3376f2b0325e46183b668ac0bc442aa31011c00dd30ba7f27264714  evidence/gate-infra/11-typecheck-image-id.txt
  ```
  完整 before/after 块见 `06-typecheck-final.log`。
- 新日志路径：`build/evidence/typecheck/`，每次运行写 `00-run.txt`（**C9 形状头**）、`10-typecheck-image-build.log`（仅镜像需要重建时）、`11-typecheck-image-id.txt`。`00-run.txt` 实测头部：
  ```
  # gate log: typecheck (operational; rewritten by every run)
  collected_at: 2026-10-31T19:21:07+08:00
  git_head: 95c496e682e3841add1a081edd78d29c34957aac
  git_status_porcelain:
     M .gitignore
     M build/gates/typecheck.sh
     …
  ```
  它与任务证据的区别：这是**可重复运行的操作日志**（被引用的是 `evidence/build-infra/` 里的副本），带 C9 头是为了自我描述，不冒充不可变证据。

## 5. 追加 B：真演练“slang 缺失 → 可操作错误”分支

`02-slang-missing-branch.log`（`run-slang-missing-drill.sh`）选择了 **方案 (a)：真演练**。方法：把 `build/slang/out/slang` **临时移走**（不删除），用 `DOCKER_HOST=tcp://127.0.0.1:1` 让一次性构建不可达，跑真实的 `bash build/gates/gates.sh`，然后恢复产物并对比 sha256。

观测：
```
[gates] slang artifact is missing: …/build/slang/out/slang
[gates] building it now with the documented one-command path: bash build/slang/build.sh
…
FATAL: cannot obtain the slang build image …
[gates] FAIL: the slang artifact could not be built, so the artifact checks cannot run.
       Missing  : build/slang/out/slang  (the CentOS 7 slang binary this gate verifies)
       Fix      : bash build/slang/build.sh        # the documented one-command build
       Logs     : build/slang/evidence/            # raw output of the failed build
       This gate does NOT skip and does NOT pass: without the artifact there is nothing to verify.
GATES-EXIT=1

## assertions
branch-entered (artifact missing message): YES
actionable error printed:                 YES
missing path named:                       YES
fix command named:                        YES
gate exit code:                           1 (expected 1)

## restore proof
after:  sha256=63cb60a05f6ca57f648b8e2ef6de61ee0a3132c1c9d481f831dba52388832252 size=9526400 executable=yes
same sha256: YES
same size:   YES
gate with restored artifact: exit=0
DRILL-OK=1
```

即：分支真的进入、打印可操作错误（Missing/Fix/Logs + 不跳过声明）、**exit 1**；产物已恢复且 **sha256 与演练前完全一致**，恢复后整个 `gates` 门禁 exit 0。

副作用声明：演练会真实调用 `build/slang/build.sh`，因此它按自身逻辑重写了 `build/slang/evidence/*.log` 这些**可再生的构建日志**（tracked 文件，`git status` 会看到 M）；源码与 `build/slang/out/slang` 均未被改动（恢复后 sha256 一致，见上）。

关于那个误导性的 `evidence/gate-infra/21-gates-slang-missing-path.log`：该文件属于 T54 的任务证据目录（不在本任务写域），我没有改它；本任务的 `02-slang-missing-branch.log` 是**该分支第一次真正的端到端演练**，应视为取代它。若 T54 的所有者愿意，可自行把 21 号文件改名为“artifact-present / rust-half-blocked”以免继续误导。

## 6. 归因（不替别人改）

- Rust 半段：T8 已把 `crates/vd-lsp/tests/session_file_set.rs` 的 fmt 与 clippy 修掉；本次 `typecheck` 的 Rust 半段在 pinned 镜像内跑 `cargo clippy --workspace --all-targets -- -D warnings` → 通过（`TYPECHECK-EXIT=0`）。
- C++ 半段此前在 T10 的 `main.cpp` 处中断；本次运行（HEAD `95c496e`）已由 T10/T41 落地后全绿：`build-qt.sh` 全量 `BUILD-QT-EXIT=0`，ctest `16/16`，含 `#14 vedit-workspace-project-selftest`。
- 本任务未改任何产品代码、`resources/syntax/**`、其他 lane 的在飞文件；`gates.sh` 的既有未提交改动属 T54。

## 7. 证据清单（`evidence/build-infra/`，均带 C9 头）

| 文件 | 内容 |
|---|---|
| `AUDIT.md` | 全仓同类模式审计表：66 处、10 处修复、56 处留下并说明为何不触发；两类损害（假红/假绿） |
| `00-raw-audit.txt` | 当前树的原始搜索输出（同模式；as-found 列表见 AUDIT.md） |
| `01-negative-check.log` | SIGPIPE 机制确定性复现 + 修复后谓词 + 负例（shim/`/bin/true`）+ 脚本级 verdict |
| `02-slang-missing-branch.log` | slang 缺失分支真演练 + 产物恢复 sha256 一致证明 |
| `04-typecheck-after.log` / `06-typecheck-final.log` | typecheck 修后全绿（`TYPECHECK-EXIT=0`）、T54 证据 sha 前后不变、新日志路径与头 |
| `05-build-qt-full.log` | `bash scripts/build-qt.sh` 全量：`BUILD-QT-EXIT=0`、ctest 16/16、M0 smoke ok=1 |
| `run-negative-check.sh` / `run-slang-missing-drill.sh` | 可复跑脚本（后者演练后自动恢复产物并有 sha256 断言） |

## 8. 复跑

```sh
bash evidence/build-infra/run-negative-check.sh            # 机制 + 负例 + 修后谓词
bash evidence/build-infra/run-slang-missing-drill.sh       # slang 缺失分支 + 恢复断言
bash build/gates/typecheck.sh                              # 期望 TYPECHECK-EXIT=0
bash scripts/build-qt.sh                                   # 期望 BUILD-QT-EXIT=0
```
