# T42 独立评审结论 — T41 `fix-search-jump`（H1 · High：搜索结果点击真正跳转）

- **评审者**：Reviewer（cell `cell-20261031T085543161Z` 的第 4 个 Reviewer 实例；verifier ≠ writer `Deep Worker (instance 2)`）
- **被评审任务**：board `T41`（kind=work，status=completed），修复对象 `evidence/verdicts/ws-search/verdict.md` §2 **H1**
- **冻结契约**：`.mpd/cellwork/cell-20261031T085543161Z/contracts/fix-search-jump.md`（split revision 3，2026-10-31T10:16:24Z）
- **验证环**：`loop-20261031T101645-4121fe`（`mpd_verify_seat` 已绑定本会话）
- **结论**：**PASS**（bound seat），无阻塞 finding；4 条非阻塞 note 见 §5
- **盲评声明**：结论只依据契约 + 文档 + **我自己重跑的门禁/自检**；除契约强制要求的"产品路径调用方审计"（结构性 grep，见 §1.6）外，未据实现细节调整判据。

---

## 1. 我实际跑过的门禁与观测输出

### 1.1 搜索自检（真实 `ApplicationShell` + `installSearch` 装的产品跳转目标 + 真鼠标点击）

```text
$ docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e XDG_RUNTIME_DIR=/tmp/xdg-runtime \
    -e LANG=C.UTF-8 vedit-qt6-build:local bash -lc \
    'cmake --build /vedit-build/target/qt6-build --target vedit-workspace-search-selftest > /tmp/b.log 2>&1; echo BUILD-EXIT=$?; \
     QT_QPA_PLATFORM=offscreen .../vedit-workspace-search-selftest --selfcheck > /tmp/s.log 2>&1; echo SELFTEST-EXIT=$?'
BUILD-EXIT=0
SELFTEST-EXIT=0
CHECK jump.product.shell-ready PASS
CHECK jump.product.target-installed PASS
CHECK jump.product.initially-no-file PASS
CHECK jump.product.two-hits PASS
CHECK jump.product.click-first PASS
CHECK jump.product.first-current-file PASS
CHECK jump.product.first-loads-bytes PASS
CHECK jump.product.first-cursor-line PASS
CHECK jump.product.first-cursor-column PASS
CHECK jump.product.first-status PASS
CHECK jump.product.click-second PASS
CHECK jump.product.second-current-file PASS
CHECK jump.product.second-cursor-line PASS
WORKSPACE-SEARCH-SELFTEST ok=1 checks=95 failures=0 skipped=2
search: jump opened '/tmp/vedit-workspace-search-selftest-KAYlpe/a.v' line=2 column=3 (cursor line=2)
search: jump opened '/tmp/vedit-workspace-search-selftest-KAYlpe/b.sv' line=4 column=1 (cursor line=4)
```

全量日志：`evidence/verdicts/fix-search-jump/01-selftest-rerun.log`（我自己的重跑，非引用 writer 的日志）。
`search: jump opened …` 是产品代码 `EditorJumpTarget::jumpTo` 的 `qInfo` —— 证明跑的是**产品跳转目标**，不是测试夹具。

### 1.2 根 ctest（集成树）

```text
$ docker ... 'QT_QPA_PLATFORM=offscreen ctest --test-dir /vedit-build/target/qt6-build --output-on-failure'
13/16 Test #13: vedit-workspace-search-selftest ................   Passed    0.33 sec
100% tests passed, 0 tests failed out of 16
```

### 1.3 仓库声明的一键门禁（**本轮真的绿了**，writer 当时被外部阻塞）

```text
$ VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh      # 日志 evidence/verdicts/fix-search-jump/03-build-qt-gate.log
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke inside vedit-qt6-build:local
M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ksynhl=6.24.0 tree_sitter=0.25.9 window=1 editor=1 …
GATE-EXIT=0
```
（writer 报告 §4.5/§6 声明 `vedit-app` 因 edit-core 在飞的 `src/app/main.cpp` 编译错误无法链接。我在评审时先探针
`cmake --build target/qt6-build --target vedit-app` → `APP-BUILD-EXIT=0`，该外部阻塞已被 edit-core lane 清除，
因此补跑了整条 gate。）

### 1.4 仓库声明的文档门禁

```text
$ mpd_verify_evidence {kind:"gate", gate:"docs"}
gate docs exit 0 -> .mpd/verify/evidence/ev-20261031T111632-144528.log
```

### 1.5 测试未被删改/弱化

```text
$ git diff --numstat -- src/workspace/search src/workspace/integration.cpp src/workspace/integration.h
139   0  src/workspace/search/search_selftest.cpp     ← 纯新增（0 删除）
 12   1  src/workspace/search/CMakeLists.txt
 23   2  src/workspace/search/search_integration.cpp
  5   3  src/workspace/search/search_panel.h          ← 注释更新
  6   3  src/workspace/search/search_types.h          ← 注释更新
 20  10  src/workspace/search/README.md               ← 文档
12   0  src/workspace/integration.cpp                 ← 其他 lane 的在途改动（见 §4）
CHECK 总数 82 → 95（`01-before-red.log` 与 `02-after-green.log` 同为 95）
```

### 1.6 产品路径调用方审计（契约强制要求的判据）

我自己重跑（`evidence/verdicts/fix-search-jump/02-caller-audit.txt`）：

```text
$ grep -rn 'setJumpTarget\|EditorJumpTarget' src/ --include=*.cpp --include=*.h
src/workspace/search/search_integration.cpp:26:    auto *jumpTarget = shell.findChild<search::EditorJumpTarget *>();
src/workspace/search/search_integration.cpp:28:        jumpTarget = new search::EditorJumpTarget(&shell);
src/workspace/search/search_integration.cpp:34:            searchPanel->setJumpTarget(jumpTarget);
$ grep -n 'installSearch' src/workspace/integration.cpp src/workspace/search/search_integration.cpp
src/workspace/integration.cpp:35:    installSearch(shell, options);
```

即：`installSearch`（产品安装点）→ 面板工厂里 `setJumpTarget(EditorJumpTarget)`；`integration.cpp` 调用 `installSearch`。
这正是 T23 verdict H1 要求、也否证了"点击没有消费者"的那条链。

---

## 2. 判据逐条（对照 T23 H1 与冻结契约）

| 判据 | 我的观测 | 结果 |
|---|---|---|
| **H1**：点击命中后产品真的打开文件并定位到命中行 | 13 条 `jump.product.*` 全绿：`first/second-current-file`、`first-loads-bytes`、`first/second-cursor-line`、`first-cursor-column`、`first-status`；`a.v:2 → cursor line=2`、`b.sv:4 → cursor line=4` | ✅ |
| 硬约束①：自检断言**真实产品状态**（光标行号/当前文件），不得只断言信号 | 断言读的是 shell 编辑器状态：当前文件路径、载入字节、光标行/列、状态栏；`qInfo` 来自产品 `EditorJumpTarget::jumpTo` | ✅ |
| 硬约束②：不得删除/弱化既有断言、不得改判据 | `search_selftest.cpp` diff = **139 insertions / 0 deletions**；CHECK 82 → 95（只增） | ✅ |
| 硬约束③：不改测试迁就实现 | 测试侧只有新增（含把自家新 helper 放进宏以过 `-Werror=unused-function`），无既有断言改动 | ✅ |
| 契约方向：在 `installSearch` 装入 demo `IJumpTarget`，复用 filetree open seam | `installSearch` 安装 `EditorJumpTarget`；它调用 `vedit::workspace::openFileInEditor(...)`（唯一 open seam），然后定位光标 | ✅ |
| 证据：`evidence/fix-search-jump/`（前后光标行断言 + selftest 输出） | 00–09 + REPORT；我复跑了 01/02 对应的 harness 与 03 gate | ✅ |
| 写域 | 改/增均在 `src/workspace/search/` 与 `evidence/fix-search-jump/`；`integration.cpp` 也在允许写域内（当前 +12 diff 属其他 lane 在途，见 §4） | ✅ |

---

## 3. RED → GREEN 的说明

- RED：`evidence/fix-search-jump/01-before-red.log`（**writer 采集**）：新断言 + 未修复实现 → `checks=95 failures=8`、`SELFTEST-EXIT=1`，
  8 条 `jump.product.*` FAIL，`SEARCH-JUMP … current='' cursorLine=1`。
- GREEN：我自己的重跑（§1.1）与 writer 的 `02-after-green.log` 一致：同一 95 项 harness，`failures=0`。
- 我**没有**为复现 RED 去回滚实现（那等于改被评审的工作）；RED 用 writer 的原始日志，GREEN 用我的独立重跑。

---

## 4. 写域与副作用声明

- 本任务改动：`src/workspace/search/{search_jump_target.h,.cpp（新增）,search_integration.cpp,search_selftest.cpp,search_panel.h,search_types.h,CMakeLists.txt,README.md}` + `evidence/fix-search-jump/`。
- `src/workspace/integration.cpp` 当前 `+12/−0`：`git diff` 里是 **project lane** 的 `installProject` 接线（T6 在 T41 采集之后才落地），
  不是 T41 的改动；该文件本身在 T41 的允许写域内，故不构成越界。
- **我的副作用**：跑仓库声明的一键 gate（`scripts/build-qt.sh`）会刷新它自己的证据目录
  `evidence/ffi-abi/logs/*` 与 `evidence/m0-build/build-logs/*`（gate 生成物，不是我的手改）；除这些 gate 产物外，
  我只写 `evidence/verdicts/fix-search-jump/`，未改任何产品源码/测试/门禁。

---

## 5. Findings / Notes

| ID | 级别 | 内容 | 处理建议 |
|---|---|---|---|
| N-1 | Note | T23 verdict 的 **M2**（`GlobalSearch` 析构期 use-after-free 风险）本轮未修——不在本冻结契约 Deliverable 内 | 建议单独开返修任务，勿静默遗忘 |
| N-2 | Note | 只覆盖 demo 编辑器 seam（shell 的 `QPlainTextEdit`）；KTextEditor/edit-core 的真实文档定位未验证——契约明确允许"demo IJumpTarget 先行，edit-core 后替换"，故不阻塞 | edit-core 落地后按同判据重跑一次 |
| N-3 | Note | 本机无显示服务器：点击→跳转的**视觉**效果不认证；本条 PASS 基于产品状态断言（当前文件/光标行/列/状态栏），不是"看起来对" | 用户在有屏机器做 AC-S1.6 手工验收 |
| N-4 | Note | 失败分支（缺失/不可读/二进制/超大文件）没有逐一 fixture；只验证了成功路径（H1 判据只要求点击真正跳转） | 失败语义由 `openFileInEditor` 既有判定保证，可在后续 lane 补测 |

---

## 6. 未能验证的部分

1. **视觉/鼠标手感**：无显示服务器，不认证。
2. **真实 `vedit-app` 整机点击**：我验证的是（a）产品安装路径（`installSearch`→`setJumpTarget`）、（b）真实 `ApplicationShell` 内的自检、
   （c）`vedit-app` 可构建且全量 ctest 16/16 与一键 gate 绿；但我没有在**运行中的 `vedit-app` 窗口里**用鼠标点一条命中（无显示）。
   writer 声明的"整机冒烟"本轮由我补到"app 链接成功 + xcb 软件渲染窗口 smoke `window=1 editor=1`"，仍不是人工点击。
3. `M2` 生命周期风险与 edit-core 落地后的替换行为。

---

## 7. 结论

**PASS**。H1 的两半都成立：**产品路径确有消费者**（`installSearch` 安装 `EditorJumpTarget`，`integration.cpp` 调用 `installSearch`），
**行为可用**（真鼠标点击后当前文件、载入字节、光标行/列、状态栏全部断言通过，光标落在命中行/列）。
既有断言未被删改（139/0 纯新增），仓库声明的一键 gate 在评审时已可跑通并 `GATE-EXIT=0`。
