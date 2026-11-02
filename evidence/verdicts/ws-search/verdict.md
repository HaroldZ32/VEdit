# 独立评审 verdict — T23 ws-search（全局搜索：命中文件+行号、点击跳转、空状态，A2c）

- Cell: `cell-20261031T085543161Z` · Board task: `T23` · 评审任务: `T35`（AC-0.6）
- 冻结契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/ws-search.md`（T23）
- 评审人: **Reviewer**（独立只读；未修改任何源码/测试/门禁）
- 评审对象: 分支 `feat/qt6-ktexteditor-migration`，HEAD `9957f5f`；评审开始时工作树干净（`git status --porcelain` 空）；评审结束时其他 lane 的未提交改动已出现（`Cargo.toml`、`crates/*`、`evidence/*` 等），不在本评审范围
- 日期: 2026-10-31（Asia/Shanghai）
- 环境: 宿主 Linux 7.0.0-38-generic x86_64；pinned 镜像 `vedit-qt6-build:local`（Ubuntu 26.04.1 / Qt 6.10.2 / KF6 6.24.0 / glibc 2.43 / g++ 15.2.0 / cmake 4.2.3）
- 风险等级: **High**
- 结论: **FAIL（请求返修）** — 1 条 High：A2c 的“点击结果跳转到对应文件与行”在真实产品里没有消费者；另有 1 条 Medium 并发/生命周期风险。

---

## 1. 总体评估

搜索内核本身是可靠的：工作线程扫描、可取消、有 15 s 时间预算与 2000 条结果上限、跳过二进制/超大/不可读并计数、`.git`/`build` 剪枝、UTF-8 路径与列号、每行只报一次、空状态分四种且与结果树互斥；根 ctest 9/9 覆盖 `vedit-workspace-search-selftest`（我复跑通过），自检有 82 checks（root，2 skip）/88 checks（非 root）的原始日志。A2c 的 (a) 返回文件+行号+片段、(c) 空状态、(d) 可取消不冻结 UI 都成立。

缺口在 (b)“点击结果跳转到对应文件与行”：**真实产品里没有任何人消费 `SearchPanel::jumpRequested`**。`installSearch` 只 `setFactory`，`main.cpp` 没接跳转，`SearchPanel::setJumpTarget` 在全仓只有声明与内部使用、没有任何调用方（自检里的 demo target 不算）。所以用户点一条命中，`activateHit` 发出信号后什么也不会发生——文件不打开、行不定位。报告 §7.1 与 §2(b) 都承认“消费端仍是接缝”。与 T21 同理，这不是 edit-core 阻塞到不可做：T22 已用 demo 编辑器证明“打开并定位”在当前产品里可行（`PaletteIntegration::openInEditor` 载入文件 + 记录路径），搜索只需要一个 demo `IJumpTarget`（打开 + 选中行）就能让 A2c (b) 在产品里成立，edit-core 落地后再替换实现。

另有一条与验收无关但真实的生命周期风险：`GlobalSearch` 析构时最多等 10 s，而扫描预算默认 15 s；等待超时后对象被销毁，但 worker 里的 `Scanner` 仍持有对 `GlobalSearch::cancelRequested_` 的引用、并会继续 `QMetaObject::invokeMethod(this, …)`——存在 use-after-free/崩溃窗口。概率低（本地盘通常秒级取消），但这是“关面板/退出时扫描还在跑”的合法路径。

## 2. Findings（按严重度）

### H1 [High] A2c (b)“点击结果跳转”在产品里没有消费者，点击无任何可见效果
- 是什么：`SearchPanel::activateHit` 里 `emit jumpRequested(request)` 后，只有在 `jumpTarget_ != nullptr` 时才调用 `jumpTarget_->jumpTo(request)`；全仓没有 `setJumpTarget` 调用方（`installSearch` 只注册工厂；`src/app/main.cpp` 未接；`src/workspace/integration.cpp` 只转调 install）。自检里用 demo `IJumpTarget` 证明“点击→打开→定位”这条链，但那是测试夹具，不是产品路径。
- 为什么重要：A2c 原文“返回命中文件与行号，可点击跳转”。用户点一条命中期望编辑器打开该文件并跳到该行；现在是“点了没反应”。测试 `jump.click.recorded` / `jump.demo.opened` 断言的是信号与 demo 目标，不是产品可见结果——测试名再次高出产品能力。
- 修复方向（描述，不实施）：在 `installSearch` 里安装一个 demo `IJumpTarget`（打开文件到 `shell.editorArea()->activePane()` 的 demo 编辑器并选中 `line`），或由 shell 提供统一的 open-at-line seam 供 search/filetree 共用；测试改为断言“点击后编辑器内容/光标行 == 命中行”。edit-core 落地后替换 `IJumpTarget` 实现。
- 证据：`src/workspace/search/search_panel.cpp` `SearchPanel::activateHit` / `setJumpTarget`；`src/workspace/search/search_integration.cpp` `installSearch`；`src/workspace/search/search_panel.h` 的注释（“src/editor supplies the real one”）；`evidence/ws-search/REPORT.md` §2(b) 与 §7.1；`04-selfcheck-root.log` 的 `jump.demo.*`。复跑：`grep -rn 'setJumpTarget' src/ --include=*.cpp --include=*.h`（只有 search_panel 声明与 selftest）。

### M2 [Medium — 并发/生命周期] 扫描进行中销毁 `GlobalSearch` 可能触发 use-after-free
- 是什么：`GlobalSearch::~GlobalSearch` 先 `cancel()` 再 `thread_->wait(10000)`；若 10 s 内没结束就直接返回（`thread_` 泄漏）。但 `Scanner` 的成员是 `const std::atomic_bool &cancel_`，直接引用 `GlobalSearch::cancelRequested_`；`runScan` 的结果投递是 `QMetaObject::invokeMethod(this, …)`。默认 `maxDurationMs = 15000` > 10 s 的等待上限；一次阻塞在慢 I/O（大目录 `entryInfoList`、慢盘 `readAll`）的扫描可以让析构先走完，worker 之后继续读已销毁的 atomic、并向已销毁对象投递 queued invoke。
- 为什么重要：崩溃/未定义行为出现在“关闭搜索面板或退出应用时扫描仍在跑”的合法路径；虽然本地盘概率低，但这是并发正确性问题，不是性能偏好。
- 修复方向：把取消令牌改成 `std::shared_ptr<std::atomic_bool>` 按值捕获（worker 不再引用 QObject 成员）；析构等待不设短上限（或等待到 worker 真正退出）；投递用 `QPointer<GlobalSearch>` 判空；确认 `QThread` 对象的回收路径。
- 证据：`src/workspace/search/global_search.cpp` 的 `Scanner`（`const std::atomic_bool &cancel_`、默认 `maxDurationMs 15000`）、`GlobalSearch::~GlobalSearch`、`GlobalSearch::runScan`。复跑（构造性）：把 `maxDurationMs` 调到 > 10 s 并在扫描中销毁 panel，用 ASan 或 `-fsanitize=address` 构建可复现。

### M3 [Medium — 会话恢复] 恢复出来的 Search dock 是占位符
- 是什么：`ApplicationShell::ApplicationShell` 在构造里 `restoreSession()` → `applyPanelsFromSession()` → `panels_->insert(id, this)`，此时 search 的 factory 还没注册（`installPanels` 在 `main.cpp` 的 shell 构造之后才调用）；`installSearch` 只 `setFactory`，不会替换已插入 dock 的占位内容。与终端同一个根因（`evidence/build-registration/README.md` 遗留发现 2 已记录终端一侧）。
- 为什么重要：用户上次开着 Search dock，重启后看到一个写着“Content seam: … lane”的占位 dock；A2c (c) 的“明确空状态”此时也不成立（是占位，不是搜索空状态）。
- 修复方向：shell 侧在恢复后重新应用工厂（或延迟插入到 install 之后）；或让 `installSearch` 像 `installFileTree` 一样替换 `panel-placeholder-search`。
- 证据：`src/app/app_shell.cpp` 构造/`restoreSession`/`applyPanelsFromSession`；`src/app/main.cpp` 的 `installPanels` 调用顺序；`src/workspace/search/search_integration.cpp`；`src/workspace/filetree/filetree_integration.cpp`（反例：filetree 已处理）；`evidence/build-registration/README.md` 遗留发现 2。

### M4 [Medium — 证据纪律 C9] 证据没有记录采集时刻的 HEAD 与工作树状态
- 是什么：`evidence/ws-search/` 下没有 `git rev-parse HEAD` / `git status --porcelain` 记录；logs 里只有时间与命令。
- 为什么重要：C9 明确要求；证据无法自我绑定修订。
- 修复方向：证据头统一写 time / HEAD / porcelain。
- 证据：`evidence/ws-search/` 全部文件；C9 原文见 `docs/handover/appendix/requirements-rev2.md`。

### N5 [Nit] 摘要行的时间/跳过计数是瞬时值，重跑数字会变
- 是什么：截图里 `7 hits in 6 of 6 files — 0 ms (1 too large skipped, 1 binary skipped)` 是运行时值；重跑会有 ±ms 抖动。报告未声称这是固定值，属可接受。
- 方向：无。

## 3. 我不判的（declined to judge，逐条给理由）

1. **`.gitignore` 不被解析** — 契约只要求文本搜索与剪枝到固定目录名；报告 §7.3 已声明，非缺陷。
2. **符号链接跳过** — `Scanner::walk` 明确不跟随链接以防环与越界；契约未要求跟随，安全取舍正确。
3. **正则/整词/跨文件替换** — 非目标 5，明确不做。
4. **单行只报首个命中、无跨行匹配** — A2c 只要求文件+行号；报告已说明。
5. **最大结果 2000 / 15 s 预算的具体阈值** — 未规定，属实现选择；有截断/超时标记与 UI 摘要，可观测。
6. **真实 KTextEditor 的跳转 API 形态** — edit-core 设计问题；本 verdict 只要求“当前 demo 级可用 + 具名接缝”。

## 4. 我实际复跑的（命令 + 观察）

```sh
# 根 ctest（锁定镜像内），含 search 自检
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  ctest --test-dir target/qt6-build --output-on-failure
# 观察：9/9 passed；Test #9 vedit-workspace-search-selftest Passed 0.31 sec

# 产品里是否存在跳转消费者
grep -rn 'setJumpTarget\|jumpRequested' src/ --include=*.cpp --include=*.h | grep -v selftest
# 观察：只有 search_panel.cpp 的定义/发射 + search_panel.h 的注释；无任何安装点
```

证据面：`04-selfcheck-root.log` = `checks=82 failures=0 skipped=2`；`05-selfcheck-nonroot.log` = `checks=88 failures=0`；`10-ctest-root-search.log` = 1/1 passed；`13-integration-proof.log` = NEEDED 无 KTextEditor/tree-sitter + `strings vedit-app` 命中面板文案（说明 lane 确实被链进产品）；`06/07-screenshots.log` = 4/5 张 PNG。数字与报告一致。

## 5. 本次评审的边界

- 只重跑了根 ctest（9/9），没有重跑 82/88 条 CHECK 的完整明细（读了 `04/05-selfcheck` 日志）；没有重拍截图。
- 没有在有屏环境用鼠标点一条命中（无显示）；跳转证据是测试内的 demo target + 信号断言，属 L2。
- 没有用 ASan 实际复现 M2（未构建 sanitizer；结论来自代码路径分析，已在 finding 里标注为“构造性复跑”）。
- 没有进入 edit-core 的真实编辑器路径（不存在）。
- 没有修改任何源码、测试、门禁；本文件是唯一写入。

- 未运行 `bash build/gates/qt6-cxx.sh` 本体：它会重写 tracked 的 `evidence/m0-build/build-logs/*`，超出本次只读写域；改在锁定镜像内直接复跑 ctest / lane 自测（命令与观察见 §4）。

## 6. 结论与一句话理由

**Verdict: FAIL（请求返修）** — 风险 High。

理由：A2c (b) 的“点击跳转”在真实产品里没有消费者（点击无任何可见效果），自检只证明了“信号 + demo 目标”；搜索内核（返回结果/空状态/取消/不冻结 UI）本身成立且可复跑。返修 H1（安装 demo `IJumpTarget` 或统一 open-at-line seam，并把测试改为断言可见结果）后本 verdict 可改判 PASS；M2 的生命周期风险建议同批修，M3/M4 建议记入欠账。
