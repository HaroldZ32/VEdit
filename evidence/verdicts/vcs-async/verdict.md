# T102 独立评审结论 — T99 VCS 异步化 + 真非阻塞断言 + 双 free UB 修复

- **评审者**：Reviewer 4（cell `cell-20261031T085543161Z`；verifier ≠ writer `Senior Engineer 2`）
- **被评审任务**：board `T99`（VCS 收口：T24 N-1/N-2）
- **HEAD sha（评审时）**：`48569602d0c86fb402dc88155593d1dc06445c90`（`feat/qt6-ktexteditor-migration`）
- **验证环**：该 cell 中 T99/T102 **没有 verify loop**（`.mpd/verify/loops/*` 无 `taskId: T99|T102`）；按任务书执行，verdict 落在本目录
- **结论**：**PASS**。四个核心命题全部由我**自己复跑 + 源码审计 + 一次 ASan 真 FFI 运行**证实；无阻塞 finding。

---

## 1. 核心命题一：双 free UB 是真修了（malloc/free 配对 + 分配器匹配）

### 1.1 旧缺陷（T23 遗留，报告 §4）
`FfiVcsSource` 旧实现用 `take()` 先 `std::free()` 每个字符串，再调 `vd_vcs_*_free()` 对**同一批指针二次释放**；
且 Rust `CString` 的内存用 `std::free` 释放 → 分配器不匹配。stub-only 面板测试从没走真 FFI，所以一直没爆。

### 1.2 新路径的所有权流转（逐块恰好释放一次）

| 分配（Rust） | 释放（Rust） | 配对 |
|---|---|---|
| `CString::into_raw`（branch/revision/note/path/text 等字符串） | `free_string()` = `CString::from_raw`（`capi.rs:285-289`） | 1:1，同一分配器（Rust 全局分配器 = 系统 malloc） ✓ |
| `Box::into_raw(items.into_boxed_slice())`（changes 数组） | `Box::from_raw(slice_from_raw_parts_mut(...))`（`vcs.rs:171`），每个元素的 `path` 再 `free_string` | 1:1 ✓ |
| `FfiVcsSource::copyText()`（`vcs_source.cpp:97`，**只拷贝**成 std::string） | 不释放原指针（拷贝后由 `vd_vcs_*_free` 释放一次） | 不重复 ✓ |

- 我 grep 复核：`vcs_source.cpp` 里 `std::free` = **0**；`take(` 只出现在注释里（`:94`）。
- `vd_vcs_*_free` 在每个取结果处**恰好调用一次**：info `:276/:288`、status `:303/:315`、diff `:330/:345`（异步 poll 路径），同步 seam `:381/:411`。
- 异步 job 的所有权：`JobValue::VcsInfo(VcsInfoData)` / `VcsDiff(VcsDiffData)`（`service.rs:59/61`）持有**Rust 拥有的 String**；被新请求替换/丢弃时由 Rust drop（不泄漏、不会留在 raw 指针里）；`poll` 仅在 READY 时把结果转成 raw 交给调用方 → 调用方 free 一次。
- 同步 seam 方法（`info/status/diff`）仍是 start+bounded-wait 的一次**拷贝**，同样由 `vd_vcs_*_free` 释放一次；面板不使用它们（见 §3）。

### 1.3 ASan 真 FFI 运行（我自己跑的）

```text
$ cmake -S src/workspace/vcs -B target/vcs-asan -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
CONFIGURE-EXIT=0 ; BUILD-EXIT=0
$ QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=0 target/vcs-asan/vedit-workspace-vcs-selftest --selfcheck
RUN-EXIT=0
AddressSanitizer / double-free / attempting-free 命中数：0
WORKSPACE-VCS-SELFTEST ok=1 checks=35 failures=0
```
该 lane 构建自动链入 `target/release/libvd_ffi.a`（`VEDIT_VCS_HAVE_FFI=1`），35 条检查包含真实 FFI 的
status/info/diff job + free；**ASan 无 double-free / 无分配器错配报告**。日志：`evidence/verdicts/vcs-async/asan-selftest.log`。
（边界：Rust 静态库未用 ASan 插桩，但 malloc/free 由 ASan 全局拦截，double-free/错配会被抓到；leak 检测关闭以免 Rust 静态库噪声。）

## 2. 核心命题二：非阻塞断言真能抓住同步实现（灵敏度对照组有效）

我自己复跑（根构建）：

```text
CHECK vcs.nonblocking.start-returns-fast PASS start took 0 ms while the fake git sleeps 400 ms
CHECK vcs.nonblocking.event-loop-ticks   PASS ticks=12
CHECK vcs.nonblocking.job-still-pending  PASS polls=24
CHECK vcs.nonblocking.result-delivered   PASS git repository has no commit yet
CHECK vcs.nonblocking.metric-detects-block PASS timer fired at 300 ms after scheduling (expected late)
WORKSPACE-VCS-SELFTEST ok=1 checks=35 failures=0
```

- **装置**（我读的代码）：假 `git` = `#!/bin/sh; sleep 0.4; exec <real git> "$@"` 放进 PATH 最前；
  临时目录建 `.git` 标记，使 backend 判定为 Git 并**真的 spawn** `git rev-parse`（两次，各 0.4 s）；
  无标记时只做 marker walk、job 瞬间完成（writer 的第一版因此红——这本身证明断言依赖真子进程）。
- **灵敏度对照组是真校准，不是恒 PASS**（`vcs_selftest.cpp:345-356`）：它**故意阻塞主线程 300 ms** 后处理事件，断言同一个
  20 ms 定时器 `firedAtMs >= 250`。若计时度量不敏感（永远报 ~20 ms）这条会红；若实现是旧的同步版，
  `start-returns-fast`（<150 ms）与 `event-loop-ticks`（≥3）都会红。→ 三件事共同说明这套断言能抓住同步实现。
- **真 spawn 的证据**：`result-delivered` 的 note 来自真 git（"git repository has no commit yet"），且总耗时 = 两次 0.4 s 慢 git；
  如果是假等待/无子进程，job 会瞬间完成（第一版现象的对照）。

## 3. 核心命题三：只读闸门未变松

- `git status --porcelain -- crates/vd-vcs` → **空**（零改动）✓。
- 生成头 `crates/vd-ffi/include/vd_ffi.h` 的 `vd_vcs_*` 符号全集：
  `vd_vcs_status / vd_vcs_poll / vd_vcs_status_free / vd_vcs_info_job / vd_vcs_info_poll / vd_vcs_info_free /
  vd_vcs_diff_job / vd_vcs_diff_poll / vd_vcs_diff_free` —— **写形状符号数 = 0** ✓。
- 三个 job 仍走同一 `ProcessVcsService → SystemRunner → ensure_read_only`（spawn 前拒绝写子命令，`crates/vd-vcs` 未改）。
- 面板只调异步 `requestStatus/requestInfo/requestDiff`（`vcs_panel.cpp:74/95/136`），不再在 GUI 线程调同步方法 ✓。
- stub 的 `writeAttempts()==0` 断言仍在（`vcs_selftest.cpp:116/160`），原 24 条检查名**一条不少**（我做了集合比对：24 ⊆ 35，新增 11 条）✓。

## 4. 核心命题四：SVN stub-only 声明诚实

- 我自己 `command -v svn svnadmin`：**宿主：无；pinned 镜像：无**（证据 `07-svn-stub-only.md` 的描述与此一致）。
- 因此 (a) 真 svn 冒烟在本环境**跑不了**；writer 选 (b) 并在 `AGENT.md` 新增 VCS 段明写：
  "**SVN is stub-only**: neither the host nor the pinned image ships `svn`/`svnadmin` … Do not describe SVN support as verified"。
  这不是偷懒，是诚实；关闭路径（装 subversion + 真工作副本冒烟）也写清了。

## 5. 回归门禁（我自己实跑）

```text
$ mpd_verify_evidence {kind:"gate", gate:"typecheck"}
gate typecheck exit 0 -> .mpd/verify/evidence/ev-20261101T065200-25d655.log

$ ctest --test-dir target/qt6-build --output-on-failure
22/22 Test #22: vedit-workspace-vcs-selftest ... Passed 1.14 sec
100% tests passed, 0 tests failed out of 22              ROOT-CTEST-EXIT=0

$ cargo test -p vd-vcs -p vd-ffi        （pinned vedit-build:local）
TEST-EXIT=0 （vd-ffi 7 unit + 12 abi；vd-vcs 6 unit + 9 readonly；0 failed）

$ …/workspace-lane-vcs/vedit-workspace-vcs-selftest --selfcheck
WORKSPACE-VCS-SELFTEST ok=1 checks=35 failures=0
```

## 6. Findings / Notes

| ID | 严重度 | 内容 | 处理建议 |
|---|---|---|---|
| — | — | **无阻塞 finding** | — |
| N-1 | Info | seam 仍保留同步 `info/status/diff`（start + 有界等待）方法（为接口完整性/测试）；面板已改 request*，产品路径不阻塞。将来若有人误用同步方法会再引入 N-1 | 保持注释警示；或后续移到测试专用 |
| N-2 | Info | 我的 ASan 覆盖面：standalone lane + 静态 Rust 库（未插桩），double-free 由全局拦截器捕获，leak 检测关闭；未在整机 AppDir 上做 ASan | 可接受；如需整机 ASan 另开 |
| N-3 | Info | 灵敏度对照组是"故意阻塞 300 ms 校准计时度量"，它验证的是**度量灵敏度**而非产品路径本身；产品路径由 start/ticks/pending/delivered 四条覆盖 | 无 |
| N-4 | Info | 宿主与镜像都无 `svn`/`svnadmin`，SVN 端到端确实不可能；已在 AGENT.md 明写 stub-only | 若要支持 SVN，需新增镜像依赖 + 冒烟任务 |
| N-5 | Info | 该 cell 里 T99/T102 没有 verify loop（无法 `mpd_verify_record`），结论只落 board note + 本文件 | 记录形态问题，供 captain 参考 |

## 7. 未能验证的部分

1. **GUI 帧率/观感**：本机无显示器；非阻塞证据是事件循环 tick 计数 + 慢子进程计时，不是帧率。
2. **真实大仓/网络盘**的 `git diff` 延迟体验未压测（容器内无显示服务器）。
3. **SVN 真子进程路径**：环境无 svn，按 (b) 明确 stub-only。
4. ASan 未覆盖 Rust 栈的 leak（只验证 double-free/分配器错配），也未对整机 AppDir 做 sanitizer。

## 8. 结论

**PASS**。双 free UB 的所有权链路是干净的（Rust 分配→Rust free，C++ 只拷贝，`vd_vcs_*_free` 恰好一次），
我用 ASan 在**真 FFI 路径**上复跑无任何 double-free/分配器错配报告；非阻塞断言不仅数字正确，灵敏度对照组
（故意阻塞 300 ms → 定时器 300 ms 迟到）证明它**能抓住同步实现**；只读闸门零改动、头文件写符号为 0、stub 写计数断言仍在；
SVN 的 stub-only 声明与环境的 `command -v` 一致且在 AGENT.md 明写。22/22 根 ctest、typecheck exit 0、
`cargo test -p vd-vcs -p vd-ffi` exit 0、35 checks 全部由我实跑。
