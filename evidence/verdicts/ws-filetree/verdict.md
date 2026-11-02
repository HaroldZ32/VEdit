# 独立评审 verdict — T21 ws-filetree（文件树面板，A2a）

- Cell: `cell-20261031T085543161Z` · Board task: `T21` · 评审任务: `T35`（AC-0.6）
- 冻结契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/ws-filetree.md`（T21）
- 评审人: **Reviewer**（独立只读；未修改任何源码/测试/门禁）
- 评审对象: 分支 `feat/qt6-ktexteditor-migration`，HEAD `9957f5f`；评审开始时工作树干净（`git status --porcelain` 空）；评审结束时其他 lane 的未提交改动已出现（`Cargo.toml`、`crates/*`、`evidence/*` 等），不在本评审范围
- 日期: 2026-10-31（Asia/Shanghai）
- 环境: 宿主 Linux 7.0.0-38-generic x86_64；pinned 镜像 `vedit-qt6-build:local`（Ubuntu 26.04.1 / Qt 6.10.2 / KF6 6.24.0 / glibc 2.43 / g++ 15.2.0 / cmake 4.2.3）
- 风险等级: **High**
- 结论: **FAIL（请求返修）** — 1 条 High：契约 (a) 的“双击打开文件”在真实产品里不打开文件；其余 (b)(c)(d) 与占位替换均成立。

---

## 1. 总体评估

文件树本身是一份质量相当高的 lane 交付：懒加载模型（250 行/批、不递归、`scanned_dirs=1`）、磁盘为先的 create/rename/delete（失败有诊断、无幽灵行）、六态根状态与权限/缺失的空状态、`.v` 不被误判为 vlang、以及“已插入 dock 的占位控件被替换”这条此前没有 verdict 的问题——我逐条核过代码与证据，**占位替换确实成立**（`filetree_integration.cpp` 的 `installFileTree` 检测 `panel-placeholder-*` 后 `dock->setWidget(panel)` 并 delete 旧件；`evidence/ws-filetree/09-real-app-install.log` 记录 `placeholderReplaced=1`）。111 条 CHECK 与 17 条非 root 权限用例都有原始日志，我还独立重跑了它的独立构建（configure/build/ctest 全 exit 0）。

但契约 (a) 写的是“双击打开文件”，实现的落点只到面板边界：`FileTreeWidget` 发 `openFileRequested`，`installFileTree` 把它接到 shell 状态栏的一行临时文案——**双击之后文件不会在编辑器里打开，用户看到的是转瞬即逝的状态栏提示**。报告自己在 §8.1 承认“真正的‘在编辑器里打开’…属于 edit-core，跨 lane 待接”。这不是 edit-core 的依赖造成的不可做：同一份 shell 里，T22 命令面板已经用 `PaletteIntegration::openInEditor` 把文件内容真正载入到 demo 编辑器（`editorArea()->activePane()->editorWidget()` + `setPlainText`），文件树完全可以复用同一条 demo 路径，或把“打开”定义成一个 shell 级接缝。因此这条是**本 lane 可做而未做**，不是“被依赖阻塞”。按“用户感知”判据，A2a (a) 不成立。

## 2. Findings（按严重度）

### H1 [High] A2a 的“双击打开文件”只在信号边界成立，产品里不打开文件
- 是什么：`src/workspace/filetree/file_tree_widget.cpp` 在双击/activate 时 `emit openFileRequested(path)`；`src/workspace/filetree/filetree_integration.cpp` 的 `installFileTree` 只把该信号接到 `shell.statusBar()->showMessage("Open <path>")`。真实 `vedit-app` 里没有编辑器打开动作。
- 为什么重要：A2a 原文是“文件树可打开目录、展开/折叠、双击打开”。用户双击一个 `.sv`，期望编辑器载入该文件；现在的效果是“什么也没发生（只有一条会被下一条状态栏消息覆盖的文案）”。测试 `tree.dblclick.file-opens` 的名字也高出实际断言：该用例只断言 `openFileRequested` 被发出（`tests/filetree_tests.cpp` 里 `QSignalSpy`），没有断言任何编辑器状态。这正是“测试名承诺了验收没承诺的事”。
- 修复方向（描述，不实施）：在 `installFileTree` 里把 `openFileRequested` 接到与 T22 相同的 demo 打开路径（`shell.editorArea()->activePane()->editorWidget()` → demo editor 载入 + 记录路径属性），并把测试改成断言编辑器内容/路径属性；或者由 shell 提供一个具名的 open seam（`IEditorOpenTarget`），filetree 只依赖该接口。edit-core 落地后替换实现、不改调用点。注意保持“双击文件夹只展开不打开”的现有行为。
- 证据：`src/workspace/filetree/filetree_integration.cpp` `installFileTree`；`src/workspace/filetree/file_tree_widget.cpp` 的 `openFileRequested` 发射点；`src/workspace/palette/palette_integration.cpp` `PaletteIntegration::openInEditor`（同 shell 的反例，证明 demo 打开可行）；`evidence/ws-filetree/REPORT.md` §4(a) 与 §8.1；`evidence/ws-filetree/03a-tests-full.log` 中的 `tree.dblclick.file-opens`。复跑：`docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local bash -lc 'QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --project <fixture> --hold 1000'`（当前日志只会出现状态栏文案）。

### M2 [Medium — 门禁覆盖] 本 lane 的 111 条验收测试不在仓库根 gate 里跑
- 是什么：`src/workspace/filetree/CMakeLists.txt` 的 `VEDIT_BUILD_FILETREE_TESTS` 默认取 `VEDIT_FILETREE_STANDALONE`，根构建下为 OFF；`target/qt6-build/CMakeCache.txt` 实测 `VEDIT_BUILD_FILETREE_TESTS:BOOL=OFF`。根 ctest 的 9 个测试里没有 `vedit-filetree-tests`（我复跑的 9/9 也不含它）。lane 报告里的 gate 证据 `12-gate-build-qt-final.log` / `13-gate-qt6-cxx-final.log` 同样不含该套件。
- 为什么重要：A2a 最完整的回归套件（111 checks，含磁盘一致性）只在“手工跑 lane 脚本”时执行；一次回归可以直接绿着进 PR 而不被门禁发现。契约要求可复跑证据，不要求进根 gate，但 C8/C10 的“PR 必过”精神要求验收测试被自动执行；否则 T21 的回归保护名存实亡。
- 修复方向：在根构建（或 app 构建）里把 `VEDIT_BUILD_FILETREE_TESTS` 打开，或把选项默认改成“跟随是否有 Qt6::Test”，让 `vedit-filetree-tests` 进 ctest；同时保留独立构建路径。
- 证据：`src/workspace/filetree/CMakeLists.txt` 的 `option(VEDIT_BUILD_FILETREE_TESTS ...)`；`target/qt6-build/CMakeCache.txt`；本评审的容器内 `ctest -N`（9 个测试，无 filetree）。

### M3 [Medium — 证据纪律 C9] 证据没有记录采集时刻的 HEAD 与工作树状态
- 是什么：`evidence/ws-filetree/` 下所有文件都没有 `git rev-parse HEAD` / `git status --porcelain` 记录（`grep -rl 'HEAD\|porcelain' evidence/ws-filetree` 无命中）；`REPORT.md` 只写契约/需求/写域/镜像环境，`00-pins.txt` 只有时间与工具链。
- 为什么重要：C9 明确要求；当前只能靠时间戳与“工作树干净”间接对齐到 `9957f5f`。本次我独立重跑了构建，所以结论不受影响；但证据本身不满足项目的证据规则。
- 修复方向：证据脚本统一写“采集时间 + HEAD + porcelain”头部；下轮重采时补齐。
- 证据：`evidence/ws-filetree/00-pins.txt`、`REPORT.md` 头部；C9 原文见 `docs/handover/appendix/requirements-rev2.md`。

### N4 [Nit — 已解决，留痕] “dock 占位控件是否真被替换”此前没有 verdict
- 结论：**是**。`installFileTree` 检测 `panel-placeholder-file-tree` 并 `dock->setWidget(新面板)`；`09-real-app-install.log` 有 `placeholderReplaced=1`。本条作为该悬案的正式回答记录在此，不作为 finding。

### N5 [Nit] 权限用例在 root 下 SKIPPED 的处理是正确的，但复跑需注意身份
- 是什么：`03a-tests-full.log` 有 2 条 root-SKIPPED，真实权限验证在 `06-permissions-nonroot.log`（17 checks）。报告已如实披露。
- 方向：保持；若未来把套件接进 ctest，需显式以非 root 运行权限子集，否则这两条会静默跳过。

## 3. 我不判的（declined to judge，逐条给理由）

1. **10k 大目录的 64 ms 打开耗时是否会在更慢磁盘上退化** — 本 lane 的 A2a 没有数值预算；我用的是证据里的实测，不是本次重测（大目录 fixture 未重建）。
2. **符号链接语义（按链接本身展示、展开跟随目标、不递归）** — 契约未规定；Qt 默认行为，无环风险。
3. **“打开文件”未来用 KTextEditor 的哪个 API** — edit-core 的设计问题，不在本 lane；本 verdict 只要求“当前 demo 级可用 + 具名接缝”。
4. **排序/隐藏文件/大小写规则** — 契约未规定，属实现选择。
5. **`exec`/mount 等极端文件系统下的 readdir 阻塞** — 未测；属后续性能优化（报告 §8.5 已登记）。

## 4. 我实际复跑的（命令 + 观察）

```sh
# 1) lane 独立构建 + ctest（容器内 /tmp，不写工作区）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local bash -lc '
  cmake -S src/workspace/filetree -B /tmp/review-filetree -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build /tmp/review-filetree --parallel 8
  ctest --test-dir /tmp/review-filetree --output-on-failure'
# 观察：configure-exit=0 / build-exit=0 / 100% tests passed, 0 tests failed out of 1（ctest exit 0）

# 2) 根 gate 覆盖检查
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  ctest --test-dir target/qt6-build --output-on-failure
# 观察：9/9 passed；其中没有 filetree 测试（与 VEDIT_BUILD_FILETREE_TESTS=OFF 一致）
grep '^VEDIT_BUILD_FILETREE_TESTS' target/qt6-build/CMakeCache.txt
# 观察：VEDIT_BUILD_FILETREE_TESTS:BOOL=OFF

# 3) 产品内是否存在真正的打开消费者
grep -rn 'openFileRequested' src/ --include=*.cpp --include=*.h | grep -v selftest | grep -v tests
# 观察：只有 file_tree_widget.cpp 的发射点 + filetree_integration.cpp 的状态栏桥
```

证据面：`03a-tests-full.log` = `FILETREE-TESTS checks=111 failures=0`（2 条 root-SKIPPED）；`06-permissions-nonroot.log` = 17 checks；`09-real-app-install.log` = `placeholderReplaced=1`；`05-screenshots.log` = 8 张 PNG；`12-gate-build-qt-final.log` / `13-gate-qt6-cxx-final.log` = exit 0（7/7，不含本套件）。这些数字与报告一致。

## 5. 本次评审的边界

- 只重跑了 lane 的独立构建/ctest（1/1）与根 ctest（9/9），没有重跑 111 条 CHECK 的完整输出（读了 `03a-tests-full.log`，未二次执行）；没有重跑 10k fixture/8 张截图。
- 没有在有屏环境以真实鼠标交互验证双击（无显示；报告的双击证据是 `QTest::mouseDClick`，属 L2）。
- 没有进入 edit-core/KTextEditor 的打开路径（不存在）。
- 没有修改任何源码、测试、门禁；本文件是唯一写入。

- 未运行 `bash build/gates/qt6-cxx.sh` 本体：它会重写 tracked 的 `evidence/m0-build/build-logs/*`，超出本次只读写域；改在锁定镜像内直接复跑 ctest / lane 自测（命令与观察见 §4）。

## 6. 结论与一句话理由

**Verdict: FAIL（请求返修）** — 风险 High。

理由：A2a (a) 的“双击打开文件”在真实产品里只落到状态栏一行文案、不打开任何文件，且测试只断言了信号而非用户可见结果；这是本 lane 可做而未做的验收缺口（同 shell 的 T22 已证明 demo 打开路径可行），其余 (b)(c)(d)、占位替换与懒加载证据均成立。返修 H1（把打开接到 demo/具名接缝并改测试为断言可见结果）并重跑套件后，本 verdict 可改判 PASS；M2/M3 建议同批修掉。
