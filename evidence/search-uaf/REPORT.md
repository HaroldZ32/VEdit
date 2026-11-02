# T64 返修报告 — 内存安全：`GlobalSearch` 析构期 use-after-free（T23 verdict M2 / T42 N-1）

- Cell: `cell-20261031T085543161Z` · Board task: **T64** · 来源: `evidence/verdicts/ws-search/verdict.md` §2 **M2**、`evidence/verdicts/fix-search-jump/verdict.md` 行 141（N-1）
- 执行: **Deep Worker (instance 2)** · 日期: 2026-10-31（Asia/Shanghai）
- 代码基线: `HEAD = 95c496e`（分支 `feat/qt6-ktexteditor-migration`）；被测源码 sha256 见 `09-source-sha256.txt`
- 环境: 宿主 Linux 7.0.0-38-generic x86_64；pinned 镜像 `vedit-qt6-build:local`（Qt 6.10.2 / g++ 15）与 `vedit-build:local`（CentOS 7 / rustc 1.99.0）
- 结论: **缺陷已复现（含 ASan heap-use-after-free 栈）、已按所有权/生命周期修复、修后 ASan 干净、根 ctest 16/16、typecheck gate PASS。** 不是“加个判空”。

---

## 1. 机理（先搞清楚谁比谁活得长）

旧的 `global_search.cpp` 里，`GlobalSearch` 与它的 worker 是**双向**的裸引用关系：

1. `Scanner` 的成员是 **`const std::atomic_bool &cancel_`**（旧 `global_search.cpp:238`），构造时绑定到
   `GlobalSearch::cancelRequested_`（一个 QObject 成员，旧 `global_search.cpp:318` 传入）。worker
   在 `cancelled()` 里读它（`global_search.cpp:82`，被 `stopRequested()` → `run()` 调用）。
2. worker 的批量投递 lambda **按裸指针捕获 `this`**（旧 `global_search.cpp:318`），并在扫描结束时
   再对 `this` 发一次 `QMetaObject::invokeMethod`（旧 `:332`）——投递目标是一个 QObject。
3. 析构只等 **10 s**：旧 `~GlobalSearch()` 是 `cancel(); thread_->wait(10000); removePostedEvents(this);`
   （旧 `:255-264`）。而**默认扫描预算是 15 s**（`search_types.h:38` `maxDurationMs = 15000`）。

时间窗：`cancel()` 只能让 worker 在**检查点**退出（每个目录条目 / 每 256 行）。一次**阻塞 syscall**
（`QFile::readAll()` 在慢盘/网络盘、或巨大的目录 `entryInfoList()`）期间 worker 根本不看取消标志。
于是：

```
t=0      析构开始：cancel() → wait(10000)
t=10.0s  wait 超时 → 析构返回 → delete GlobalSearch → cancelRequested_、vtable、QObject 全部 free
t=10.x   worker 的 read() 返回，继续扫描：
         · cancelled() 读已释放的 atomic（UAF read）
         · invokeMethod(this, …) 对已销毁 QObject 投递（UAF）
         · removePostedEvents(this) 已在 t=10.0s 跑完，之后的投递一个也不会被清掉
```

这不是“概率理论”：`02-before-red-asan.log` 里 ASan 给出的栈就是上面第 1 条，
free 的栈就是第 3 条的析构返回。

## 2. 复现（先写用例，再修）

### 2.1 第一次尝试：FIFO —— 不成立，记录原因

先想用 `mkfifo` 让 `readAll()` 永久阻塞。结果（`01-before-red-standalone.log` 首次运行）：

```
CHECK lifecycle.worker-blocked FAIL
LIFECYCLE delete-returned elapsedMs=0 fifo-released=0 holdMs=10300
```

原因不是猜测：`Scanner::walk()` 在 `!entry.isFile()` 时直接 `continue`（`global_search.cpp:130`），
而 `QFileInfo::isFile()` 对 FIFO **不是** true（只有 regular file 为 true），所以 FIFO 被跳过，扫描
瞬间完成。**产品里能被阻塞的只有 regular file 的读或目录枚举**，因此复现必须让*普通文件*的
`read()` 变慢。

### 2.2 最终复现：`tools/slowio` 慢盘注入（不改进产品代码）

`src/workspace/search/tools/slowio/slowio.cpp` 是**仅测试用**的 LD_PRELOAD 垫片（不链接进
`vedit-app`，也不链接进 `vedit_workspace_search`）：`read()` 命中路径以 `.slowio` 结尾的 fd 时
睡 **11.5 s**（> 旧的 10 s 析构上限，< 15 s 扫描预算），然后写 `<path>.released` 标记。
自检的 `--selfcheck` 会**再执行自己**（`--lifecycle-slow`），把垫片放进 `LD_PRELOAD`：

- 子进程创建 `a.txt`（70 条命中，保证扫描先送出一批、已越过 a.txt）+ `zz_slow.slowio`（普通文件）；
- 等批次到达并 settle 200 ms ⇒ worker 正停在 `zz_slow.slowio` 的 `QFile::readAll()` 里；
- `delete search`，测耗时，并检查 `.released` 标记是否存在：
  **正确的析构只能在这条被阻塞的读结束之后返回**。

### 2.3 RED（修前，同一 harness）

非 ASan（`01-before-red-standalone.log`）：

```
SLOWLIFECYCLE delete-returned elapsedMs=10001 released-marker=0 stallMs=11500
CHECK slowlifecycle.destructor-waits-for-stalled-read FAIL
...
SLOWLIFECYCLE-CHILD finished=1 exit=11
```

`elapsedMs=10001` 正是旧实现的 10 s 上限：析构在 worker 还卡在 `read()` 时就返回了；
随后 worker 恢复执行并访问已释放对象，子进程 **SIGSEGV（exit=11）**。

ASan（`02-before-red-asan.log`，原样摘录）：

```
==159==ERROR: AddressSanitizer: heap-use-after-free on address 0x7af12d1e9431 ...
READ of size 1 at 0x7af12d1e9431 thread T1 (vedit-global-se)
    #0 std::__atomic_base<bool>::load(...) /usr/include/c++/15/bits/atomic_base.h:501
    #2 cancelled /vedit-build/src/workspace/search/global_search.cpp:82
    #3 run /vedit-build/src/workspace/search/global_search.cpp:68
    #4 GlobalSearch::runScan(...) /vedit-build/src/workspace/search/global_search.cpp:330
    ...
freed by thread T0 here:
    #1 GlobalSearch::~GlobalSearch() /vedit-build/src/workspace/search/global_search.cpp:264
    #2 runSlowLifecycleProbe /vedit-build/src/workspace/search/search_selftest.cpp:924
...
SLOWLIFECYCLE delete-returned elapsedMs=10000 released-marker=0 stallMs=11500
CHECK slowlifecycle.destructor-waits-for-stalled-read FAIL
```

## 3. 修法（所有权 / 生命周期，不是掩蔽）

| 改动 | 文件 | 为什么这是“所有权/生命周期”手段 |
|---|---|---|
| 每次扫描一个 `std::shared_ptr<std::atomic_bool>` 取消令牌；worker **按值**捕获并交给 `Scanner`（`Scanner(options, *cancelTag, …)`） | `global_search.h`、`global_search.cpp` | worker 不再引用 QObject 的任何成员；令牌的寿命由 shared_ptr 决定，与 `GlobalSearch` 解耦。 |
| 析构：`cancel()` 后 **`thread_->wait()` 无上限**（等 worker 真正从 `runScan()` 返回） | `global_search.cpp` | 生命周期顺序被钉死：**worker 先退出，成员才被销毁**。旧的 10 s 上限就是 UAF 窗口本身。 |
| 析构/`start()` 中确定性回收 `QThread`：`disconnect(finished→deleteLater)` + `removePostedEvents(thread_)` + `delete thread_` | `global_search.cpp` | 明确 `QThread` 对象的回收路径，不再依赖“事件循环迟早会跑 deleteLater”；也避免删除后残留 deferred-delete。 |
| 每次 `start()` 生成新令牌，旧令牌先置位再等旧 worker | `global_search.cpp` | 一次扫描一个令牌，不存在“新扫描把旧 worker 的取消标志复位”之类共享状态。 |
| 保留 `generation_` 过滤与 `removePostedEvents(this)`（现在保证在 join 之后，不可能再有新投递） | 不变 | 事件投递顺序有保证，不靠“判空继续”。 |

**没有做的事**：没有 try/catch 包住回调；没有 `if (this) continue` 这类判空掩盖；没有把计时器/回调
留在悬空状态靠运气；没有降低任何断言。

## 4. 红 → 绿（同一 harness、同一注入）

| | 修前（`01`/`02`） | 修后（`03`/`04`） |
|---|---|---|
| 非 ASan 子进程 | `delete` 10 001 ms 返回、marker=0、`destructor-waits-for-stalled-read` **FAIL**、随后 SIGSEGV(exit=11) | `delete` 11 297 ms 返回、marker=1、**PASS**、exit=0 |
| ASan 子进程 | `ERROR: AddressSanitizer: heap-use-after-free`（`cancelled()` @ `global_search.cpp:82`，free @ `~GlobalSearch()` :264） | 无任何 sanitizer 报告，`WORKSPACE-SEARCH-SLOWLIFECYCLE ok=1 checks=4 failures=0`，exit=0 |
| 完整 ASan 自检 | — | `WORKSPACE-SEARCH-SELFTEST ok=1 checks=84 failures=0 skipped=2`（`05`） |

## 5. 我实际运行的命令与观察（headless）

```sh
# 1) RED：lane 独立构建（无 shell），修前代码 + 新用例
cmake -S src/workspace/search -B target/search-standalone -G Ninja
cmake --build target/search-standalone --target vedit-workspace-search-selftest
QT_QPA_PLATFORM=offscreen ./target/search-standalone/vedit-workspace-search-selftest --selfcheck
# 观察（01）：lifecycle.* 前 4 条 PASS；SLOWLIFECYCLE delete-returned elapsedMs=10001 released-marker=0；
#            destructor-waits-for-stalled-read FAIL；子进程 exit=11；SELFTEST-EXIT=1（checks=84 failures=1）

# 2) RED under ASan（修前）
cmake -S src/workspace/search -B target/search-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address
cmake --build target/search-asan --target vedit-workspace-search-selftest
LD_PRELOAD="$(g++ -print-file-name=libasan.so):$PWD/target/search-standalone/libvedit_search_slowio.so" \
  ASAN_OPTIONS=detect_leaks=0 ./target/search-asan/vedit-workspace-search-selftest --lifecycle-slow
# 观察（02）：heap-use-after-free（栈见 §2.3）

# 3) GREEN：同一命令重建（修后）
cmake --build target/search-standalone --target vedit-workspace-search-selftest
QT_QPA_PLATFORM=offscreen ./target/search-standalone/vedit-workspace-search-selftest --selfcheck
# 观察（03）：SLOWLIFECYCLE delete-returned elapsedMs=11297 released-marker=1；
#            WORKSPACE-SEARCH-SELFTEST ok=1 checks=84 failures=0 skipped=2；SELFTEST-EXIT=0

# 4) GREEN under ASan（修后，全量自检）
ASAN_OPTIONS=detect_leaks=0:verify_asan_link_order=0 ./target/search-asan/.../vedit-workspace-search-selftest --selfcheck
# 观察（05）：checks=84 failures=0；无 AddressSanitizer 输出；SELFTEST-EXIT=0

# 5) 根构建 + 根 ctest（当前基线 16/16）
cmake --build target/qt6-build --parallel
ctest --test-dir target/qt6-build --output-on-failure
# 观察（06）：100% tests passed, 0 tests failed out of 16；search 自检 11.84 s；CTEST-EXIT=0

# 6) 根构建自检明细（T41 的点击跳转不得回退）
QT_QPA_PLATFORM=offscreen ./target/qt6-build/src/app/workspace-lane-search/vedit-workspace-search-selftest --selfcheck
# 观察（07）：jump.product.* 13 条全 PASS；lifecycle/stall-child 全 PASS；
#            WORKSPACE-SEARCH-SELFTEST ok=1 checks=102 failures=0 skipped=2（基线 95 → 102，未降）

# 7) 仓库 typecheck gate（自己实跑）
VEDIT_SKIP_IMAGE_BUILD=1 bash build/gates/typecheck.sh
# 观察（08）：[qt6] PASS ...；[typecheck] PASS (pinned image vedit-build:local)；TYPECHECK-EXIT=0
```

实测数字（不是估计）：析构返回耗时修前 **10 001 ms**（两次：10 000/10 001），修后
**11 297 / 11 298 / 11 299 ms**（三次，均 > 注入的 11 500 ms 减调度误差）；根 ctest 全部 **16/16**，
总耗时 18.41 s（整理文档后复跑 18.38 s），其中 search 自检 11.84 s（多出的 11.5 s 就是故意注入的阻塞读）。

## 6. 未验证 / 局限（如实说明）

- **ASan 只覆盖 lane 独立构建（无 app shell）**：root 构建的 `vedit-app` 需要 KTextEditor/KF6，
  本轮没有为整机做 sanitizer 构建；被修的代码（`global_search.cpp`）在两种构建里是同一份。
- **阻塞是注入的（模拟慢盘），不是真实慢盘**：垫片只拦截 `read()` 对 `*.slowio` 路径。
  真实产品里触发条件等价于“单次阻塞读超过 10 s（旧上限）”，例如网络盘/慢盘/巨大目录。
- **析构改为无上限等待是有意的活跃性取舍**：卡死的文件系统会拖慢关面板/退出。换来的是
  “worker 活着时绝不释放它脚下的对象”。若将来要恢复可中断性，正确做法是让 worker 完全自持
  （结果投递到一个由 shared_ptr 拥有的 relay，而不是 QObject 自己），不能靠缩小 wait 超时。
- **没有做多线程并发压力测试**：`start()`/`cancel()`/析构的并发调用序列没有做 fuzz；
  本轮只覆盖“扫描中析构”这一条（也就是 M2 点名的路径）。
- **没有显示服务器**：没有任何 GUI 视觉结论。
- `start()` 在旧扫描 5 s 内不停止时仍返回 false（拒绝双扫描）——这是既有的调度语义，本次未改。

## 7. 证据清单（`evidence/search-uaf/`）

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 采集时间、HEAD、镜像 id、本任务写域 `git status` |
| `01-before-red-standalone.log` | 修前 RED：FIFO 尝试及其失败原因（`worker-blocked FAIL`、elapsed 0 ms）；slowio 复现（10 001 ms、marker=0、FAIL、子进程 SIGSEGV exit=11） |
| `02-before-red-asan.log` | 修前 ASan：`heap-use-after-free` 完整栈（`cancelled()` @ :82，free @ `~GlobalSearch()` :264） |
| `03-after-green-standalone.log` | 修后 GREEN：11 297 ms、marker=1、PASS，`checks=84 failures=0 skipped=2` |
| `04-after-green-asan.log` | 修后 ASan 子进程：无报告、`checks=4 failures=0`、exit=0 |
| `05-after-full-asan-selfcheck.log` | 修后**全量** ASan 自检：`checks=84 failures=0`、无 sanitizer 输出 |
| `06-root-build-ctest.log` | 根构建 + `ctest`：`100% tests passed, 0 tests failed out of 16` |
| `07-root-selftest.log` | 根构建自检明细：`checks=102 failures=0 skipped=2`，T41 `jump.product.*` 全 PASS |
| `08-typecheck-gate.log` | `bash build/gates/typecheck.sh`：`[typecheck] PASS`，`TYPECHECK-EXIT=0` |
| `09-source-sha256.txt` | 被测源码 / 垫片 / CMake 的 sha256 |

## 8. 提交

未 commit/push（按任务要求由 Lead 统一提交）。新增/修改文件：`src/workspace/search/global_search.{h,cpp}`、
`search_selftest.cpp`、`CMakeLists.txt`、`README.md`，新增 `src/workspace/search/tools/slowio/slowio.cpp`
（仅测试垫片）与本证据目录。
