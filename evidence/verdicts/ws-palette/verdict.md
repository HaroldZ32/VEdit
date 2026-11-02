# 独立评审 verdict — T22 ws-palette（Ctrl+P 命令面板 + 10k 文件模糊跳转，A2b）

- Cell: `cell-20261031T085543161Z` · Board task: `T22` · 评审任务: `T35`（AC-0.6）
- 冻结契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/ws-palette.md`（T22）
- 评审人: **Reviewer**（独立只读；未修改任何源码/测试/门禁）
- 评审对象: 分支 `feat/qt6-ktexteditor-migration`，HEAD `9957f5f`；评审开始时工作树干净（`git status --porcelain` 空）；评审结束时其他 lane 的未提交改动已出现（`Cargo.toml`、`crates/*`、`evidence/*` 等），不在本评审范围
- 日期: 2026-10-31（Asia/Shanghai）
- 环境: 宿主 Linux 7.0.0-38-generic x86_64；pinned 镜像 `vedit-qt6-build:local`（Ubuntu 26.04.1 / Qt 6.10.2 / KF6 6.24.0 / glibc 2.43 / g++ 15.2.0 / cmake 4.2.3）
- 风险等级: **Low**
- 结论: **PASS（附带 findings）** — A2b 成立且被我独立复跑（10k 文件，面板路径 p95 **1.421 ms**，预算 200 ms，exit 0）；findings 均为非阻塞。

---

## 1. 总体评估

命令面板是一份“判据先定、数字可复跑”的交付：`Ctrl+P` 走 shell 的 `CommandRegistry` 并可重绑、模糊过滤大小写不敏感、回车真正把文件内容载入 demo 编辑器、无命中/无工程/空索引各有独立空状态、索引带 `scanCount()`/`revision()` 记账且第二次打开不再扫盘。最关键的数字——10k 文件 200 ms——我没有只看报告：我在锁定镜像里**独立重建**了 palette lane 并复跑 200 样本 × 10 查询的真实磁盘 fixture，得到 GUI 路径 overall p95 = **1.421 ms**、`BENCH VERDICT PASS`、exit 0（报告记录的是 1.404 ms，同一量级），余量约 140 倍，且 `--assert-p95-ms 200` 会在超预算时 exit 3。根 ctest 9/9 也覆盖了 `vedit-workspace-palette-selftest` 与 `vedit-workspace-palette-integration-selftest`（我复跑通过）。因此 A2b 的核心判据成立。

两个非阻塞缺口值得记：契约 (d) 的“与文件树共享文件索引”目前是**接口级**的——`WorkspaceFileIndex::setFiles/addFile/removeFile` 只有定义、没有生产端，ws-filetree 从未把快照发布进来，也没有联合用例；实际行为是 palette 首次打开自己扫一次盘（22 ms，之后复用），所以“不重复扫描”在用户层面无害，但契约 (d) 的联合路径没有被证明。其次是 C9 证据头缺失。两者都不影响 A2b 的数字与用户可见行为。

## 2. Findings（按严重度）

### M1 [Medium] 契约 (d) 的“与文件树共享索引”只有接口、没有接线，也没有联合用例
- 是什么：`src/workspace/palette/file_index.h` 的 `WorkspaceFileIndex` 提供 `setFiles()` / `addFile()` / `removeFile()`，但全仓没有任何调用方——ws-filetree 的 `installFileTree`/`FileTreeModel` 都不发布快照。唯一证明是 selftest 里的 `index.publish-no-scan` 等**单元级**断言（直接调 API），不是“文件树 → palette”的联合路径。
- 为什么重要：契约 (d) 的意图是“别重复扫盘”；现在是“palette 自己扫一次，接口留着等人接”。用户层面 22 ms 无感，但联合路径一旦接上若有 bug（增量更新/重命名后索引陈旧），当前没有测试会发现。报告 §6.2 已如实披露，属“已知未完成”而非隐瞒。
- 修复方向（描述，不实施）：在 `installFileTree` 成功打开目录后把 model 的文件快照 `fileIndex()->setFiles(...)` 发布进 palette，并在文件 create/rename/delete 时走 `addFile/removeFile`；加一条跨 lane 联合用例（两次打开面板后 `scanCount()` 不增加且命中数与树一致）。若本轮不做，建议把 (d) 的措辞改为“接口级共享”，不要留一个看起来已满足的验收项。
- 证据：`src/workspace/palette/file_index.h` `WorkspaceFileIndex`；`src/workspace/filetree/filetree_integration.cpp` `installFileTree`（无发布调用）；`evidence/ws-palette/REPORT.md` §3(d) 与 §6.2；复跑：`grep -rn 'setFiles\|addFile\|removeFile' src/workspace/filetree src/workspace/palette --include=*.cpp --include=*.h | grep -v selftest`。

### M2 [Medium — 证据纪律 C9] 证据没有记录采集时刻的 HEAD 与工作树状态
- 是什么：`evidence/ws-palette/` 下没有 `git rev-parse HEAD` / `git status --porcelain` 记录；`00-pins.txt` 有宿主机、docker、镜像 id 与源码 sha256，但没有修订号与干净度。
- 为什么重要：C9 明确要求；`00-pins.txt` 虽然逐文件记了源码 sha256（很强），但无法直接回答“这是哪个 commit 的树”。本次我独立重建复跑，结论不受影响；证据纪律本身仍不达标。
- 修复方向：证据头统一写 time / HEAD / porcelain；`bench.sh` 生成 `00-pins.txt` 时自动带上。
- 证据：`evidence/ws-palette/00-pins.txt`；C9 原文见 `docs/handover/appendix/requirements-rev2.md`。

### N3 [Nit] 基准记录的“机器与环境”缺 CPU 型号/核数/负载
- 是什么：`00-pins.txt` 记了宿主内核、docker 版本、镜像与工具链，但没有 CPU 型号、核数、采样时的 load average。AC-S2.3 的措辞是“（记录机器与环境）”。
- 为什么提：余量 140 倍，缺 CPU 型号不影响结论；但该要求是明文的，且“在哪台机器上成立”是 06-open-issues 里点名的评审问题。
- 方向：`bench.sh` 的 pins 里加 `lscpu`（型号/核数）与 `cat /proc/loadavg`，不改判据。
- 证据：`evidence/ws-palette/00-pins.txt`；`evidence/ws-palette/31-bench.log` 首行。

### N4 [Nit] 报告头条 p95 用的是 overall（1.404 ms），最差单查询 p95 是 src 的 1.497 ms
- 是什么：`bench.json` 的 `overall_p95_ms=1.404`，但 `per_query.src.p95=1.496995`（core 1.452887）。整体 p95 与分查询 p95 是不同统计口径，二者都可辩护，但头条数字没有取“最差”。
- 为什么提：200 ms 预算下两者都远低；只是评审口径上应取更保守值，避免下一位读者怀疑“挑好看的数字”。
- 方向：报告写“最差单查询 p95 = 1.497 ms；overall p95 = 1.404 ms”即可。
- 证据：`evidence/ws-palette/bench.json`；`evidence/ws-palette/31-bench.log`。

### N5 [Nit] 复跑命令会改写 tracked 证据
- 是什么：`src/workspace/palette/tools/bench.sh` 无条件把 `31-bench.log` / `bench.json` 写进 `evidence/ws-palette/`（tracked 文件）。任何评审者按文档复跑都会把工作树弄脏或制造无关 diff。
- 为什么提：与“证据可被外人重跑”相冲突；本次我因此改走容器内 `--fixture /tmp --json /tmp` 的等价路径（见 §4），而不是文档命令。
- 方向：给 `bench.sh` 加一个输出目录参数（默认仍写 evidence），文档里的“复跑”用临时输出。
- 证据：`src/workspace/palette/tools/bench.sh` 的 `tee "$ROOT/evidence/ws-palette/31-bench.log"` 与容器内 json 路径。

### N6 [Nit — 已披露的边界] “回车打开”当前落在 demo 编辑器，不是真实编辑内核
- 是什么：`PaletteIntegration::openInEditor` 在无 handler 时把内容 `setPlainText` 进 `editorArea()->activePane()` 的 `QPlainTextEdit`，并置 `vedit.openFilePath`；>4 MB 或含 NUL 拒绝。
- 为什么提：A2b 的“打开”在产品里确有可见效果（内容真的换了），所以判据成立；但它是 demo 级，AC-S1.1 会替换。报告 §6.1 已如实说明。
- 方向：保持；edit-core 落地后用 `setFileOpenHandler()` 注入真实打开，调用点不变。

## 3. 我不判的（declined to judge，逐条给理由）

1. **10k 文件在更慢机器上的绝对数** — 我用同一 pinned 镜像独立复跑（1.421 ms），但宿主 CPU 未记录，无法给出“其他机器”的推证；结论只对本次环境成立。
2. **真实显示环境下的弹窗/键盘 grab 行为** — selftest 是 offscreen，报告已声明 `raise()`/grab 的 offscreen 提示不是失败；真实窗口路径由 build-qt.sh 的 xcb 冒烟覆盖（但该冒烟不点击面板）。
3. **`PaletteIntegration::setFileOpenHandler` 未来接 edit-core 的接口形态** — edit-core 的设计问题。
4. **`.v` 扩展名陷阱** — 本面板不做语言过滤，不受影响。
5. **模糊匹配的排序质量（是否 Lapce 同款排序）** — 契约只要求模糊过滤与 200 ms，未规定排序算法。

## 4. 我实际复跑的（命令 + 观察）

```sh
# 独立重建 palette lane 并复跑 10k 基准（容器内 /tmp，不写工作区、不改 tracked 证据）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local bash -lc '
  cmake -S src/workspace/palette -B /tmp/review-palette -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build /tmp/review-palette --parallel 8
  QT_QPA_PLATFORM=offscreen /tmp/review-palette/vedit-workspace-palette-bench \
    --files 10000 --iterations 200 --limit 100 \
    --fixture /tmp/palette-fixture --json /tmp/bench.json --assert-p95-ms 200'
# 观察：configure-exit=0 / build-exit=0
#       BENCH CORE overall median_ms=0.755 p95_ms=1.323 max_ms=1.876
#       BENCH GUI  overall median_ms=0.848 p95_ms=1.421 max_ms=1.820
#       BENCH VERDICT PASS p95_ms=1.421 budget_ms=200.000 files=10000 source=disk
#       bench-exit=0

# 根 ctest（锁定镜像内）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  ctest --test-dir target/qt6-build --output-on-failure
# 观察：9/9 passed（含 Test #7/#8 两条 palette 自检）
```

证据面：`31-bench.log` / `bench.json`（记录值 overall p95 1.404 ms；我复跑 1.421 ms）；`32-ctest-palette.log` = 44/44 + 23/23；`30-build-qt.log` = 7/7 且 exit 0；`33-elf.txt` = 无 KTextEditor/tree-sitter NEEDED。报告数字与日志一致，无手抄漂移。

## 5. 本次评审的边界

- 只重跑了 10k 基准（200 samples × 10 查询）与根 ctest，没有重跑 44+23 条自检的全部明细（读了 `32-ctest-palette.log`）。
- 没有在有屏环境亲手按 Ctrl+P（无显示）；键盘路径由 `QTest` 级自检与集成自检覆盖，属 L2。
- 没有进入 edit-core 的真实打开路径（不存在）。
- 没有修改任何源码、测试、门禁；本文件是唯一写入。

- 未运行 `bash build/gates/qt6-cxx.sh` 本体：它会重写 tracked 的 `evidence/m0-build/build-logs/*`，超出本次只读写域；改在锁定镜像内直接复跑 ctest / lane 自测（命令与观察见 §4）。

## 6. 结论与一句话理由

**Verdict: PASS（附带 findings）** — 风险 Low。

理由：A2b 的 200 ms 判据被我独立复跑（p95 1.421 ms，budget 200 ms，exit 0），Ctrl+P/模糊过滤/回车打开/空状态在产品内均有可见效果，根 gate 覆盖两条自检；剩余问题（共享索引未接线、C9 证据头、机器信息不完整）都不改变 A2b 的成立。
