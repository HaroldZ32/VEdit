# T65 独立评审结论 — T64 `GlobalSearch` 析构期 use-after-free 返修（T23 M2 / T42 N-1）

- **评审者**：Reviewer 4（cell `cell-20261031T085543161Z`；verifier ≠ writer `Deep Worker (instance 2)`）
- **被评审任务**：board `T64`（`memory safety`；来源 `evidence/verdicts/ws-search/verdict.md` M2）
- **验证环**：`loop-20261031T114854-5f3d85`（`mpd_verify_seat` 已绑定）
- **结论**：**PASS**（bound seat）。附 1 条 Low（非掩蔽性冗余判空）、1 条 **外部 High**（并发 lane T69 的 19:52 改动把当前共享树的自检打红，**不是 T64 的改动**，详见 §4）。
- **盲评顺序声明**：先读 M2 原文、契约、证据与自跑门禁；随后按 T65 判据 2 的**点名清单**做了一次定向 diff 审计
  （`try/catch`、`if (ptr)`、空回调、无时序 `deleteLater`、丢行为的断连）。审计结果见 §3，未据此调换判据。

---

## 1. 机理核对：T64 讲的是不是 M2 那件事？

**M2 原文的时序**（`evidence/verdicts/ws-search/verdict.md` §2 M2）：
`~GlobalSearch` 只等 10 s，而默认扫描预算 15 s；`Scanner` 成员是 `const std::atomic_bool &cancel_`（引用 QObject 成员），
worker 还通过 `QMetaObject::invokeMethod(this, …)` 向 QObject 投递 → worker 可以活过对象、读已释放的 atomic、向已销毁对象投递。

**T64 的解释与之逐条一致**（`evidence/search-uaf/REPORT.md` §1），且我用 **HEAD 的修前源码**核对了它引用的每一行：

```text
$ git show HEAD:src/workspace/search/global_search.cpp | sha256sum
9cd0d9d35139bf925bc34f437af5f760d4048410191d866ff45005ba06e84f81   ← 与 02-before-red-asan.log 声称的被测源码 sha 相同
$ git show HEAD:src/workspace/search/global_search.cpp | sed -n '82p;68p;238p;264p'
    bool cancelled() const { return cancel_.load(std::memory_order_relaxed); }   ← :82（ASan 的 READ 栈）
        summary_.state = cancelled() ? ...                                        ← :68（run()）
    const std::atomic_bool &cancel_;                                              ← :238（引用 QObject 成员 = M2 机理）
    QCoreApplication::removePostedEvents(this);                                   ← :264（ASan 的 free 栈 = 析构返回）
```

→ **没有换题**：T64 修的就是 M2 点名的"worker 引用/投递到已销毁 QObject"的时序，
不是另找一个更轻的问题。ASan 的 UAF 栈（`cancelled()` :82 / freed by `~GlobalSearch()` :264）
即 M2 预言的"读已释放 atomic + 析构后访问"。

## 2. 复现强度：真复现，不是只构造性

| 项 | 观测 | 判定 |
|---|---|---|
| 修前 RED（非 ASan） | `SLOWLIFECYCLE delete-returned elapsedMs=10001 released-marker=0`、子进程 `exit=11`(SIGSEGV)、`failures=1` | ✅ 真红 |
| 修前 RED（ASan） | `AddressSanitizer: heap-use-after-free ... READ of size 1 ... #2 cancelled global_search.cpp:82 ... freed by ~GlobalSearch() :264` | ✅ 真复现，栈来自搜索路径 |
| 修后 GREEN（ASan） | 无 sanitizer 报告，`released-marker=1`、`elapsedMs=11297` | ✅ |
| 修后 GREEN（根构建自检） | 我自己实跑（T64 源码状态，19:50）：`WORKSPACE-SEARCH-SELFTEST ok=1 checks=102 failures=0 skipped=2`，13/13 `jump.product.*` PASS | ✅ |
| 受控条件 | 阻塞由**仅测试用** LD_PRELOAD 垫片 `tools/slowio` 注入（`*.slowio` 的 `read()` 睡 11.5 s），不链进产品 | ✅ 如实标注 |

**哈希闭环（我独立核对，这是关键）**：
- RED 日志的被测源码 sha `9cd0d9d3…` == **HEAD 里修前版本**的 sha（上 §1）→ RED 确实跑在修前代码上；
- GREEN 日志的被测源码 sha `8febfb2c…` == **当前工作树** `global_search.cpp` 的 sha：
  `$ sha256sum src/workspace/search/global_search.cpp` → `8febfb2c1d88ad228c92b57bf833c06015d426d267ea201cec6c96a947c89e98` ✓
- 我**重跑了修后 ASan 二进制**（`target/search-asan`，由上述同一份 8febfb2c 源码构建）：
  `ASAN-SELFTEST-EXIT=0`、`AddressSanitizer/ERROR` 计数 **0**、`WORKSPACE-SEARCH-SELFTEST ok=1 checks=84 failures=0`、
  `SLOWLIFECYCLE delete-returned elapsedMs=11299 released-marker=1` → **UAF 不再复现**。
  日志：`evidence/verdicts/search-uaf/03-asan-rerun.log`。

## 3. 修法审计（判据 2：不得是掩盖式）

我读了 `git diff -- src/workspace/search/global_search.{h,cpp}`，按 T65 的点名清单逐项核：

| 掩蔽模式 | 是否出现 | 说明 |
|---|---|---|
| `try/catch` 包住访问 | **无** | diff 里无 try/catch |
| `if (ptr)` 判空后继续执行 | **有一处，但非掩蔽** | `cancel()` 里 `if (cancelRequested_ != nullptr) cancelRequested_->store(true);` —— 见 F-1。它是 `shared_ptr`（拥有型句柄，pointee 不会悬垂），且构造/`start()` 都已初始化，属**死代码**；UAF 路径不是靠它关掉的 |
| 回调改成空实现 | **无** | `BatchSink`/`finished` 语义未变 |
| 总是 `deleteLater` 而不说明时序 | **无** | 正常完成仍用 `finished→deleteLater`；析构改为 `wait()` 后**确定性** `removePostedEvents(thread_)` + `delete thread_`，并写了原因 |
| 断连导致行为丢失 | **无** | 只断开 `QThread::finished→deleteLater`（随即自己 delete 线程对象），搜索行为不受影响 |

**实际用的就是所有权/生命周期手段**：
1. 每次扫描一个 `std::shared_ptr<std::atomic_bool>` 取消令牌，worker **按值**捕获（`global_search.h` 成员改为 shared_ptr；`runScan(..., const std::shared_ptr<...>&)`）；
2. 析构 `cancel(); thread_->wait();`（**去掉 10 s 上限**）→ "worker 先退出，成员后销毁"的顺序被钉死；
3. `QThread` 回收路径确定化（disconnect 后 `removePostedEvents(thread_)` + `delete thread_` + 置空），不再赌事件循环里的 deleteLater；
4. 每次 `start()` 新令牌、旧令牌先 cancel 再 join。

→ 这符合"正确方向"清单（QObject 父子/`QPointer`/析构断连/明确 `deleteLater` 时序），**不是** `if (ptr)` 掩盖。

## 4. 我自己实跑的门禁与观测

```text
# 1) 搜索自检（根构建，T64 源码状态 19:50 实跑；此时 selftest sha 仍为 T64 记录的 7d075dda…）
BUILD-EXIT=0
SELFTEST-EXIT=0
108 CHECK lines ；WORKSPACE-SEARCH-SELFTEST ok=1 checks=102 failures=0 skipped=2
SLOWLIFECYCLE delete-returned elapsedMs=11297 released-marker=1 stallMs=11500
CHECK jump.product.* PASS 计数 = 13 ；CHECK .* FAIL 计数 = 0

# 2) 修后 ASan 自检（见 §2）
ASAN-SELFTEST-EXIT=0 ；AddressSanitizer/ERROR 计数 0 ；checks=84 failures=0

# 3) typecheck 门禁（我自己跑，非引用 writer 日志）
$ VEDIT_SKIP_IMAGE_BUILD=1 bash build/gates/typecheck.sh
[typecheck] cargo clippy --workspace --all-targets -- -D warnings
[typecheck] PASS (pinned image vedit-build:local)
TYPECHECK-EXIT=0            （日志 evidence/verdicts/search-uaf/02-typecheck-rerun.log）

# 4) 根 ctest —— 见下面的外部发现
$ ctest --test-dir target/qt6-build -R vedit-workspace-search-selftest --output-on-failure
CHECK jump.product.first-cursor-line FAIL
CHECK jump.product.first-cursor-column FAIL
CHECK jump.product.second-cursor-line FAIL
WORKSPACE-SEARCH-SELFTEST ok=0 checks=105 failures=3 skipped=2
1 tests failed out of 1            （日志 evidence/verdicts/search-uaf/01-ctest-failure.log）
```

### 4.1 外部发现（**High，非 T64 的缺陷**）：根 ctest 当前被 T69 的在飞改动打红

- 当前 `search_selftest.cpp` sha = `7214621f…`、mtime **19:52:24**；T64 的证据（`09-source-sha256.txt`，19:45）记录的是 `7d075dda…`。
- 我在 **19:50** 用 T64 状态的 selftest 实跑：**102 checks / 0 failures / 13 条 jump.product 全 PASS**（上面 §4.1 第 1 块）。
- 19:52 之后，`search_selftest.cpp` 被**另一个 lane** 改到 105 checks；`target/.../vedit-workspace-search-selftest` 于 **19:53** 被重建；
  重建后的 3 条失败全是 `jump.product.*-cursor-*`（T41 的点击定位断言），**不涉及** T64 改的 `global_search.*`（其 sha 仍是 `8febfb2c`）。
- 归属证据：`.mpd/team/contracts/T69.json` —— T69 于 **11:48:53Z (19:48)** 由 SE2 claim，标题即
  "第三处同类回归：`search_jump_target.cpp:55-57` 仍把 editorWidget 硬 cast 成 `QPlainTextEdit`（产品里 KTextEditor 下跳转失效）"；
  `search_jump_target.cpp/h` 仍与 T41 记录的 sha 一致（18:52 后未变），说明这 3 条失败来自 **T69 正在改的 selftest/editor-core 接缝**，不是 T64。
- **结论**：T64 交付时（19:44–19:47）的 16/16 基线成立（其 06 日志 + 我 19:50 对唯一受影响测试 #13 的独立实跑）；
  当前共享树是 T69 的在飞中间态。**不要把这条记成 T64 的 FAIL**；T69 完成并全绿后本 verdict 仍然成立。

## 5. T41 成果未被回退

```text
$ grep -n 'installSearch|EditorJumpTarget|openFileInEditor' src/workspace/search/search_integration.cpp search_jump_target.cpp integration.cpp
search_integration.cpp:26: shell.findChild<search::EditorJumpTarget *>()   （installSearch 安装产品消费者）
search_jump_target.cpp:50: if (!vedit::workspace::openFileInEditor(*shell_, request.filePath, &openError))  （复用唯一 open seam）
integration.cpp:35: installSearch(shell, options);
selftest: grep -c 'jump.product' = 13
```
自检 checks：T64 状态 **102 ≥ 95**（`07-root-selftest.log` 与我的实跑一致）；T64 没有删/弱化任何既有断言
（`search_selftest.cpp` 的 2 行删除是文件头注释，其余 300 行全是新增）。

---

## 6. Findings

| ID | 严重度 | 症状 | 期望 | 证明来源（doc source） |
|---|---|---|---|---|
| **F-1** | Low | `cancel()` 里 `if (cancelRequested_ != nullptr) { cancelRequested_->store(true); }`：构造函数与 `start()` 都保证非空，属死代码；且 T65 判据 2 把 `if (ptr)` 判空列为掩蔽气味 | 直接 `cancelRequested_->store(true)`（或说明它防的是什么状态）；**不影响本 verdict**——它是拥有型 `shared_ptr`，pointee 不会悬垂，UAF 由"无上限 join + 按值令牌"关闭，不是靠这个判空 | `src/workspace/search/global_search.cpp` `cancel()`；T65 判据 2 |
| **F-2** | **High（外部，非 T64）** | 当前共享树根 ctest #13 失败（3 条 `jump.product.*-cursor-*` FAIL），`checks=105`；出现于 19:52 之后 | 由 **T69**（SE2，19:48 claim，编辑 `search_jump_target`/editor-core 接缝）完成修复并重跑；不要把这条记到 T64 | `src/workspace/search/search_selftest.cpp`（mtime 19:52、sha `7214621f…` vs T64 证据 `7d075dda…`）；`.mpd/team/contracts/T69.json`；`evidence/verdicts/search-uaf/01-ctest-failure.log` |

---

## 7. 未能验证的部分（含"无法稳定复现"边界）

1. **无法在真实慢盘上复现**：阻塞是 LD_PRELOAD 垫片注入的（11.5 s 的 `read()`），不是真实慢盘/网络盘。
   但机理与"旧实现只等 10 s、预算 15 s"的数字关系成立，且 ASan 给出了真实 UAF 栈——这是可接受的等价复现。
2. **无法复跑修前 RED**：修前代码只存在于 `HEAD` 的已提交版本；要重跑 RED 需回滚工作树（等于改被评审的工作，禁止）。
   RED 的真实性由"日志声称的源码 sha == HEAD 修前 sha"+"ASan 栈行号与 HEAD 预修复源码逐行吻合"锚定（§1）。
3. **ASan 只覆盖 lane 独立构建（无 app shell）**：root 构建的 `vedit-app` 未做 sanitizer 构建；被修代码是同一份。
4. **无多线程 fuzz**：只覆盖"扫描中析构"这一条 M2 路径；`start()/cancel()/析构` 的并发序列未压测。
5. **无上限等待是活跃性取舍**（卡死文件系统会拖慢关面板/退出）——报告已声明，我接受为设计选择。
6. **GUI**：无显示服务器，不认证任何视觉行为。

## 8. 结论

**PASS**。机理与 M2 逐条一致（不是换题），修法是所有权/生命周期（按值 shared_ptr 令牌 + 析构无上限 join + 确定性 QThread 回收），
**无任何掩蔽式手段**（唯一判空是 F-1 的死代码，不是 UAF 关闭手段）；证据是**真复现**（ASan heap-use-after-free，源码 sha 与 HEAD 修前版本逐字吻合）
而不是只构造性证明，修后 ASan 干净且我独立重跑确认；T41 的产品跳转路径与 13 条 `jump.product` 断言仍在，checks 由 95 → 102。
当前根 ctest 的红是 **T69 在飞改动**造成的（19:52 改了 selftest、19:53 重建二进制），已按 F-2 明确归因，不计入 T64。
