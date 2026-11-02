# 独立评审 verdict — T13 rev-syntax：语法高亮进产品（G2 + G3）

- Cell: `cell-20261031T085543161Z` · 评审任务: `T13`（rev-syntax）· 被评 work: `T12`（syntax-highlight）
- 冻结契约: `.mpd/cellwork/cell-20261031T085543161Z/contracts/syntax-highlight.md`（work）+ `contracts/rev-syntax.md`（本评审）
- 验证 loop: `loop-20261031T095153-458de0`（已绑定 seat；verdict 记录 `rec-20261031T110340-281d9a`，PASS）
- gate evidence id: `ev-20261031T105425-65455b`（`bun run typecheck`；其 exit 1 是**共享工作树并发构建导致的竞态**，见 §3 M5，不是本 work 缺陷）
- 评审人: **Reviewer**（盲审：先读契约/文档/证据，跑门禁，**先记录 verdict**；未读产品实现源码）
- 评审时刻: 2026-10-31 18:42–19:03（Asia/Shanghai）· 评审开始 HEAD `1f47e56`（work 的源码已在 checkpoint 中提交；`evidence/highlight-v2/` 未跟踪）
- 环境: 宿主 Linux 7.0.0-38-generic；`vedit-qt6-build:local`（Ubuntu 26.04.1 / Qt 6.10.2 / KF6 6.24.0 / tree-sitter 0.25.9）
- 风险等级: **Medium**
- 结论: **PASS（附带 findings）** — G2/G3/高亮属性/C11 逐条成立；用户可见的高亮颜色要等 edit-core/G1，work 已如实声明。

> **后续注记（2026-10-31 19:2x）**：本 verdict 评审后，`fix-xml-folding`（T46）重写了
> `resources/syntax/vedit-verilog.xml` / `vedit-systemverilog.xml`，两份文件的 sha256 已变为
> `c6adc763…` / `2abba334…`（大小 5,111/5,492 B）；本文件 §2 引用的旧哈希 `3d1ec41f…`/`dc6e0a47…`
> 是**评审当时**的 C11 登记值，现已被 T46 M1 记录为待回填的过期登记。T46 本身的 verdict 见
> `evidence/verdicts/fix-xml-folding/verdict.md`。

---

## 1. 总体评估

这是一条真正修掉“编译了但产品用不上”的切片。改动前基线（`00-before.txt`）是 `VEDIT_SYNTAX_GRAMMAR=none`、`vedit-app` 里 0 个 grammar 符号、NEEDED 里没有 tree-sitter/KTextEditor；改动后我独立核到：`vedit-app` 的链接行包含三个 syntax 静态库（`libvedit_syntax_provider.a`、`libvedit_syntax_kde.a`、`libvedit_syntax_grammars.a`）加产品接线库 `libvedit_syntax_product.a`，`readelf -d` 出现 `libtree-sitter.so.0.25`、`libKF6TextEditor.so.6`、`libKF6SyntaxHighlighting.so.6`，`nm -C` 有 174 个 `vedit::syntax|vedit::kde` 符号与 8 个 tree-sitter 符号；`grammar_config_generated.h` 与 `CMakeCache.txt` 都写着 `verilog`，`grammars_manifest.txt` 记录 MIT/`4457145e…`/parser sha256；空的 grammar id 会硬失败（我复现：`CONFIGURE-EXIT=1` + 精确 FATAL 文本）。产品二进制启动时 startup hook 真实执行（`--selfcheck` 打印 `syntax-product: grammar=verilog … active='verilog' startup-hook=1`），在 `VEDIT_SYNTAX_DIR` 指到仓库 resources 时加载了 `own-xml=[vedit-systemverilog.xml,vedit-verilog.xml]`；产品验收测试 19 条 CHECK，含 `.v` 3 个 / `.sv` 4 个高亮 format range。C11 边界也守住：`resources/syntax/` 只有两份自有 XML（sha256 与 C11 逐字节一致）+ LGPL 全文，没有 KDE 的 `systemverilog.xml`/`verilog.xml`，`THIRD_PARTY_NOTICES.md` §9.1/9.2 已登记两份 XML 与 MIT grammar。

我独立跑了声明的门禁 `bash scripts/build-qt.sh`：**exit 0，ctest 12/12**（含新增的 `vedit-app-syntax-startup` 与 `vedit-syntax-product-tests`），xcb/llvmpipe 冒烟 PASS。ledger 的 `typecheck` 在我这里两次 exit 1，但失败点与证据都指向**同一构建树上的并发构建竞态**（`ninja: warning: premature end of file`，两个 `vedit-qt6-build` 容器同时在重建；紧接着同样的 `nm` 断言在容器内通过），不是本 work 的缺陷——细节与证据在 M5。

## 2. 逐条核对

| 条款 | 结论 | 证据 / 我的观察 |
|---|---|---|
| **G2：syntax 三库真链接进 `vedit-app`** | ✅ | `target/qt6-build/build.ninja` 的 `vedit-app` 链接规则含三个 `.a` + `libvedit_syntax_product.a`；`readelf -d` 三条 NEEDED；`nm -C` 174 + 8 个符号；`00-before.txt` 的 0 符号为对照 |
| **G3：`VEDIT_SYNTAX_GRAMMAR=verilog` 且生成头为 verilog** | ✅ | `grammar_config_generated.h`：`kCompiledGrammar = "verilog"`；`CMakeCache`：`VEDIT_SYNTAX_GRAMMAR:STRING=verilog` + `…_OWNER=src/syntax/grammars`；`grammars_manifest.txt`：`grammar=verilog enabled=1 license=MIT version=4457145e…`；负向反例：`-DVEDIT_SYNTAX_GRAMMAR=` → configure exit 1，精确 FATAL（我复现） |
| **AC-S1.2：打开 `.v/.sv` 有高亮属性（selftest + Xvfb，L2）** | ✅（有界） | 产品测试 19/19：`HIGHLIGHT verilog format-ranges=3`、`systemverilog format-ranges=4`、`data.own-verilog-xml`/`own-systemverilog-xml` PASS；真实二进制 startup hook 行；截图仅 L2 且**明确不显示颜色**（demo `QPlainTextEdit`，G1 未落地） |
| **C11：只随包自有 LGPL XML + MIT grammar；KDE 三重禁运不变** | ✅ | 两份 XML sha256 = `3d1ec41f…` / `dc6e0a47…`（与 C11 原文一致）；`resources/syntax/` 无 KDE 文件；`THIRD_PARTY_NOTICES.md` §9.1/§9.2 登记两份 XML + MIT grammar；`packaging/runtime/AppRun:33` 导出 `VEDIT_SYNTAX_DIR`（部署路径，packaging lane，未被本 work 改动） |
| **C6：Xvfb 只算 L2、声明局限** | ✅ | `09-screenshot-L2.log` 明写“不能证明颜色，只证明带栈运行 + hook 执行”；README 的 “Deliberately NOT claimed” 段落一致 |
| **门禁（仓库声明）** | ✅（有一处基础设施竞态，见 M5） | 我跑 `bash scripts/build-qt.sh` → exit 0 / ctest 12/12；work 的 `03-build-qt-gate.log` 同结论；`typecheck` id 因并发竞态红，非 work 缺陷 |

## 3. Findings（按严重度）

### M1 [Medium — 项目级，非本 work 缺陷] AC-S1.2 的“用户看得见高亮”仍未成立，要等 edit-core/G1
- 是什么：产品外壳仍是 `DemoEditorProvider`（`QPlainTextEdit`），不会用 KTextEditor/KSH 上色；`09-screenshot-L2.log` 与 README 都明说截图不能证明颜色。AC-S1.2 的“打开 `.v/.sv` 有高亮属性”在**栈层**成立（19 条 CHECK 的 format-ranges），在**用户可见层**不成立。
- 为什么重要：Slice 1 的最终验收（AC-S1.6 [用户]）要求用户亲眼看到高亮；本 work 只关闭 G2/G3 与栈层属性，不能当成 Slice 1 高亮已完成。work 已如实声明（“No user-visible highlighting is claimed”），属于依赖缺口而非隐瞒。
- 修复方向：这不是本 work 的返修项——由 edit-core（G1/AC-S1.1）把 KTextEditor 注入产品后，在同一语法栈上做一次用户可见验收（AC-S1.6）。
- 证据：`evidence/highlight-v2/09-screenshot-L2.log`；`evidence/highlight-v2/04-syntax-product-tests.log`；`docs/handover/06-open-issues.md` G1。

### M2 [Medium — 证据/交付纪律] `evidence/highlight-v2/` 未入库，C9 的 HEAD 戳落后于当前 checkpoint
- 是什么：评审时 `evidence/highlight-v2/` 全部是 untracked；`00-collect.txt` 记 `HEAD=8fedbdd`，而当前 HEAD 已到 `1f47e56`（期间有 multiple checkpoint 提交）。
- 为什么重要：C9 要求“在冻结树上采集 + 写 HEAD/porcelain”，PR 需要证据入库；否则下一个读者无法把证据固定到某个提交，重跑也会因并发构建而漂移。
- 修复方向（描述，不实施）：PR 前冻结树、停止并发构建，重采一次 `evidence/highlight-v2/`（新 HEAD + porcelain）并整体入库；不要只补单条日志。
- 证据：`git status --porcelain -- evidence/highlight-v2`；`evidence/highlight-v2/00-collect.txt`。

### M3 [Medium — 测试质量] `vedit-app-syntax-startup` 的 `VEDIT_SYNTAX_DIR` 指到一个不存在的路径，且只断言 grammar 行
- 是什么：该 ctest 的 ENVIRONMENT 是 `VEDIT_SYNTAX_DIR=/vedit-build/../../../resources`；该路径规范化后是 `/resources`，容器里 `ls /resources` = No such file。它的 `PASS_REGULAR_EXPRESSION` 只有 `syntax-product: grammar=verilog`，所以自有 XML 的加载（`own-xml=`）根本没有被这条测试覆盖；真正覆盖它的是 `vedit-syntax-product-tests`（正确的 `/vedit-build/resources` + `data.own-*` 断言）。
- 为什么重要：两条测试的名字/意图有重叠，但启动测试的 env 是错的（只是恰好不影响它的窄断言）；将来若有人把 `own-xml` 断言加进这条测试，它会立刻红在错误的位置。
- 修复方向：用 `${CMAKE_SOURCE_DIR}/resources` 之类的真实变量生成 env（而不是未规范化的 `..` 链）；若希望启动测试也覆盖私有数据加载，增加 `own-xml=`/`private-dir=` 的断言，否则把测试名改成只描述 grammar 配置。
- 证据：`ctest --show-only=json-v1` 里 Test #1 的 ENVIRONMENT/PASS_REGULAR_EXPRESSION；容器内 `ls /resources`；`ctest -V -R vedit-app-syntax-startup` 输出。

### M4 [Medium — 门禁基础设施] ledger 的 `typecheck` 在共享树并发构建下会假红
- 是什么：两次 `mpd_verify_evidence gate:typecheck` 都 exit 1，且日志出现 `ninja: warning: premature end of file; recovering`，最后停在 `FATAL: vedit-app does not export vd_abi_version`；但同一构建树在紧接的容器里 `nm -C vedit-app | grep -E ' T vd_abi_version$'` 命中 1 条，我 18:46 跑的完整 `build-qt.sh` 也是 exit 0/12 个 ctest 全绿。竞态期间同时有 2 个 `vedit-qt6-build` 容器在重建同一构建树。
- 为什么重要：这个 cell 的验证者都在同一工作树里跑构建，`build.ninja` 与 `libvd_ffi.a` 被并发改写时，ledger gate 会给出**不可归因于改动的红**；验证者只能靠附加证据排除，成本高且容易误判。
- 修复方向：给 gate 加“构建树锁”（flock）或让每个验证者使用独立 build dir（`-B target/<verifier>/qt6-build`），至少在 `typecheck`/`gates` 入口处串行化；这是门禁基础设施的修复，不属于本 work。
- 证据：`.mpd/verify/evidence/ev-20261031T105425-65455b.log`（`premature end of file` + FATAL）；我 18:46 的 `build-qt.sh` 输出；`nm` 立即复核；`docker ps` 观察到两个并发容器。

### N5 [Nit] evidence README 没写它跑 `--selfcheck` 时设置的 `VEDIT_SYNTAX_DIR`
- 是什么：不设 `VEDIT_SYNTAX_DIR` 直接跑 `vedit-app --selfcheck`，启动行是 `private-dir='' own-xml=[]`（这是 `src/syntax/kde/README.md` 写明的“空 = 仅标准路径（开发态）”行为）；设成 `/vedit-build/resources` 才得到证据 01 里那行 `private-dir='/vedit-build/resources' own-xml=[…]`。
- 方向：在 `highlight-v2/README.md` 的“observable result”段落补一行复跑命令（`VEDIT_SYNTAX_DIR=… vedit-app --selfcheck`），避免复跑者以为坏了。

### N6 [Nit] 证据 01 没有贴“三个 `.a` 在 vedit-app 链接行”的原文
- 是什么：AC-S1.2/G2 的判据是“syntax 三库链接进 vedit-app”；证据 01 展示了 NEEDED（外部库）与 174 个符号，但没有 `build.ninja`/`ninja -t query` 的链接行原文。我独立补了这一步（见 §2 G2）。
- 方向：把链接行摘录加进证据 01；不改判据。

## 4. 我不判的（declined to judge，逐条给理由）

1. **用户可见的高亮颜色/截图观感** — demo 编辑器不消费语法栈；本 work 明确不声称。属 edit-core/G1 与用户有屏验收（AC-S1.6）。
2. **tree-sitter 对真实工程的解析端到端** — 产品里 provider 已创建、grammar 已激活，但 overlay/consumer 是 T17/T18 的范围。
3. **`.v` 被 KSH 按文件名解析成 V(vlang)** — 产品测试已打印该已知陷阱；loader 用显式名字/扩展名映射（T16 的范围），AC-S1.2 不要求按文件名解析。
4. **AppImage 打包后的实际数据目录** — `packaging/runtime/AppRun:33` 已导出 `VEDIT_SYNTAX_DIR`、`assemble-appdir.sh` 装 `resources/syntax/*.xml` 到该目录；打包在 AC-S3.3 验收，本 work 不改 packaging。
5. **KDE GPLv3+ 定义的三重禁运逐条复跑** — 需要构造特定 sha256 的 KDE 文件（不可伪造），我核的是“数据目录无 KDE 文件 + sha256 扫描脚本存在 + loader 启动 `embargoed=[]`”；T16 的既有证据未被本 work 改动。
6. **`vedit-app-syntax-startup` 与 `vedit-syntax-product-tests` 的断言强度取舍** — 见 M3；不构成 FAIL，因为覆盖在另一条测试里存在。
7. **ledger `gates` id** — 需要先构建 slang（本机 artifact 缺失），出题时间与网络成本不成比例；`typecheck` 已覆盖 C++/Rust 静态检查，其余由我手工命令覆盖。

## 5. 本次评审的边界

- **盲审**：先读契约/requirements/docs/`evidence/highlight-v2/` 与构建产物（`nm`/`readelf`/`build.ninja`/生成头/manifest/packaging 脚本），**先记录 verdict**；未读 `src/syntax/` 产品实现源码（唯一接触是一行 grep 结果，用于解释 `VEDIT_SYNTAX_DIR` 的来源，已在 record 里说明）。
- 没有重跑 Xvfb 截图（work 的 4 张截图是 L2，且本 work 不声称颜色）；没有跑 `run-evidence.sh` 类脚本。
- 没有在有屏环境验证颜色（本机无显示，C6）。
- 我的 `build-qt.sh` 复跑按脚本既有行为刷新了 `evidence/m0-build/build-logs/*` 与 `evidence/ffi-abi/logs/*`（gate 输出路径，非我手写）；未覆盖 `evidence/highlight-v2/` 的任何文件。
- 评审期间工作树多人在写、多个构建并发（M4），我的门禁证据带有该环境的竞态痕迹，已如实披露。

## 6. 结论与一句话理由

**Verdict: PASS（附带 findings）** — 风险 Medium。

理由：G2（三个 syntax `.a` 真实进入 `vedit-app` 链接行 + NEEDED + 174 符号）、G3（cache/生成头/manifest 均为 `verilog`，空值硬失败）、高亮属性（19 条 CHECK，`.v` 3 / `.sv` 4 个 range）与 C11（两份自有 XML 的 sha256 与登记、无 KDE 文件）逐条成立，并被我独立复跑（`build-qt.sh` exit 0、ctest 12/12、负向反例 exit 1）；用户可见的高亮颜色要等 edit-core/G1，work 已如实声明，findings 均为非阻塞的证据/测试质量与门禁基础设施问题。
