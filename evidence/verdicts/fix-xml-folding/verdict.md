# 独立评审 verdict — T46 rev-fix-xml-folding：自有 XML 折叠规则重写（Blocker）

> **依据更正（2026-10-31 19:25，captain 指出后由本评审独立复核）**：本 verdict 的第一版把
> `evidence/syn-kde-data/10/11-folding-*.log` 的 `library=F2` 标签当成了链接事实，**这是错的**。
> 我用 `ldd` 复现了缺陷并**在真 F2 上重测**；本文件是更正后的版本。原 verdict 记录
> `rec-20261031T111415-0bbbdc` 的“F2 上 7/7 both”依据已被取代（记录不可改，以本文件为准）。

- Cell: `cell-20261031T085543161Z` · 评审任务: `T46`（rev-fix-xml-folding）· 被评 work: `T45`（fix-xml-folding，Blocker）
- 冻结契约: `.mpd/cellwork/cell-20261031T085543161Z/contracts/fix-xml-folding.md`（work）+ `contracts/rev-fix-xml-folding.md`（本评审）
- 验证 loop: `loop-20261031T101645-e4d0f6` · 原记录 `rec-20261031T111415-0bbbdc`（PASS，依据由本文件更正）
- gate evidence id: `ev-20261031T111321-59e5f5`（`bun run typecheck`，exit 0）
- 评审人: **Reviewer**（盲审 + 独立复测；未读产品实现源码）
- 评审时刻: 2026-10-31 19:04–19:25（Asia/Shanghai）
- 环境: `vedit-qt6-build:local`；真 F2 交付库 `target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0`
- 风险等级: **High**（证据链曾建立在假标签上；XML 修复本身正确）
- 结论: **PASS（有条件）** — XML 修复经我独立在**真 F2** 上重测成立（Verilog 7/7、SV **11/11**、before 0/0）；但**证据之记录的 10/11 日志不是 F2 测量**、SV 采样分支从未生效；在 **T66** 修好 `run.sh`、加 `ldd` 防呆并重录 10/11 之前，本票只能读作“有条件 PASS / 依据待重录确认”。

---

## 1. 总体评估（更正后）

Blocker 的根因与 XML 修复本身是**对的**：旧写法 `<keyword String="module" beginRegion=...>` 被 KSyntaxHighlighting 6.24 当成关键字列表名，缺 `<list name="module">` → `Unknown keyword list` → 规则被丢弃 → 0 折叠区；新写法把折叠词放进 `veditFoldBegin`/`veditFoldEnd` 两个 `<list>`，再用 `<keyword String="veditFoldBegin" beginRegion="veditFold"/>` / `String="veditFoldEnd" endRegion="veditFold"/>` 引用；覆盖 module/begin/case/casex/casez/function/task/generate（Verilog）与 +interface/program/class/package/randcase（SV）。XML 本身我已逐条复核。

**但第一版评审的证据基础是错的**，captain 的更正成立，我逐条复现：

1. **`folding-probe/run.sh` 的 F2 路径推导错**：`F2_DIR=$(dirname $(dirname $LIB))` 把 `.../install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6.24.0` 截成 `.../install/lib`；`.../install/lib/include` 与 `.../install/lib/lib` 都不存在，编译静默回落到系统头/系统库。`ldd` 铁证：
   - 按 run.sh 参数编译：RUNPATH=`.../install/lib`（不存在），`libKF6SyntaxHighlighting.so.6 => /usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6`（**DISTRO**）。
   - 按正确路径编译（`-I install/include`、`-L install/lib/x86_64-linux-gnu -rpath 同路径`）：`libKF6SyntaxHighlighting.so.6 => /vedit-build/target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6`（**真 F2**）。
   所以 `10/11-folding-*.log` 里的 `library=F2` 只是 shell 依据“找到了 F2 的 .so 文件”打印的**标签**，不是链接事实。我用 flawed 二进制在 after 数据上跑出的数字与那两份日志**逐字一致**：Verilog 名 `18 lines / 7/7`、SV 名 `18 lines / 7/7`——这同时证明日志测的是 DISTRO，也证明 SV 采样分支没生效。
2. **SV 采样分支从未生效**：`probe.cpp` 用 `definitionName == "SystemVerilog"` 选样例，而 runner 传 `VEditProbeSystemVerilog` → 永远走 18 行 Verilog 样例（after 日志两行都是 `text-lines=18`）；SV 的 26 行样例（interface/program/class/package/randcase）从未被测。
3. **我第一版为什么没抓到**：我只核了“F2 那个文件里没有内嵌定义”（`strings | grep -c '<language'` = 0），这**不能证明探针链接的是它**；我也复核了探针拷贝只差 `name=` 一行、author/section 断言有效——这些都对，但都绕过了“实际链接的是哪个库”。正确判据是 `ldd`，我第一版没有跑。这一点我承认。

## 2. 真 F2 上的独立重测（我自己的数字，可复跑）

我的做法：把 `probe.cpp` **复制**到 `target/review-probe/probe-sv.cpp`（gitignored，只改一处：`definitionName == "SystemVerilog"` → `definitionName.contains("SystemVerilog")`，让 SV 样例分支能命中），用正确 flags 链真 F2，并先 `ldd` 确认库路径。仓库里的 `run.sh`/`probe.cpp` 未被改动。

| 测量 | 库（ldd 确认） | 定义/断言 | 结果 |
|---|---|---|---|
| **AFTER**（当前 XML，Verilog 18 行样例） | F2 | `VEditProbeVerilog`，author=VEdit section=Hardware **ours=1** | **begins=7 ends=7** |
| **AFTER**（当前 XML，SV 26 行真样例，修正采样后） | F2 | `VEditProbeSystemVerilog`，**ours=1** | **begins=11 ends=11** |
| **BEFORE**（`git show HEAD:` 旧 XML，Verilog 18 行） | F2 | `VEditProbeVerilog`，ours=1 | **begins=0 ends=0** + `Unknown keyword list "module"` 等 |
| **BEFORE**（旧 XML，SV 26 行） | F2 | `VEditProbeSystemVerilog`，ours=1 | **begins=0 ends=0** + `Unknown keyword list` 多条 |
| 对照：run.sh 原参数（DISTRO） | **DISTRO**（ldd 证明） | 同上 | 7/7、7/7（18 行）——即被作废的日志数字 |

修正后的结论：**前后对照成立，且 SV 比原日志声称的更好（11/11 而非 7/7，覆盖了 SV 专属构造）**。我的数字与 captain 转述的 Reviewer 2（Verilog 7/7、SV 11/11）一致。

## 3. Findings（按严重度）

### E1 [High — 证据链] `10/11-folding-*.log` 不是 F2 测量，且 SV 样例从未被测；原日志必须被重录/标注
- 是什么：见 §1 的 1–2。`run.sh` 的 `F2_DIR` 推导错 + `probe.cpp` 的 SV 采样条件与 runner 传名不匹配。证据：两个 `ldd` 输出、flawed 二进制复现的 7/7+7/7（18 行）、after 日志两行 `text-lines=18`。
- 为什么重要：这正是本项目反复强调的“假绿/假标签”事故形态：`ours=1` 与 author/section 断言只证明“解析到的是我们改名的定义文件”，**不证明链接的是 F2**；DISTRO 库带 KDE 内嵌定义（GPLv3+ 的 SV），用 DISTRO 测“自有 XML 的折叠”会把污染留在证据里。T66 必须修 `run.sh`（正确 multiarch 路径）、加 `ldd` 防呆断言、在真 F2 上重录 `10/11`，并给旧日志加“已被取代”横幅（不许默默覆写）。
- 修复方向（T66，不实施）：① `F2_DIR` 用 `install` 根（或直接拼 `install/lib/x86_64-linux-gnu`）；② 编译后 `ldd` 断言 `libKF6SyntaxHighlighting.so.6` 解析到 F2 路径，否则 FATAL；③ SV 采样由显式参数/环境变量选择，不依赖定义名；④ 重录 before/after（含 SV 26 行样例）；⑤ 旧 10/11 加取代横幅。
- 证据：`evidence/syn-kde-data/folding-probe/run.sh`；`probe.cpp` 的 `definitionName == QStringLiteral("SystemVerilog")`；`10-folding-before.log`/`11-folding-after.log`；我复现的 ldd 与 flawed 运行输出（见 §2/§5）。

### M2 [Medium — 文档/交付登记，owner 不在本 work] 重写后两份 XML 的哈希/大小变了，但 C11/C14、THIRD_PARTY_NOTICES、highlight 证据仍钉旧值
- 是什么：修复后 `vedit-verilog.xml` = sha256 `c6adc763c7b280d7493f48d06e5779d0b1e2068ac82f75fe7f31a07b0788a6c2`（85 行，5,111 B），`vedit-systemverilog.xml` = `2abba3342d0878e71b99d358d96e510f857cd7f232786951be8602f513e0b992`（90 行，5,492 B）；requirements C11/C14、`THIRD_PARTY_NOTICES.md` §9.1、`evidence/highlight-v2/10-licensing-and-embargo.txt` 仍是旧值 `3d1ec41f…`/`dc6e0a47…`。
- 为什么重要：C14 的用法是“执行者按路径与哈希核对”；不更新会让严格执行者认为修复后的文件“不是交付件”。
- 修复方向：planner 改 C11/C14、docs/packaging 改 notices 与 highlight 证据，登记新哈希/大小。

### M3 [Medium — requirements 措辞收尾] rev3 的 C14 仍写“规则已重写…待重测”，重测已完成
- 现在是真 F2 上 Verilog 7/7、SV 11/11（before 0/0）；`FOLDING-fix-T45.md` 已给好正确表述，planner 采用即可（注意把 SV 数字改为 11/11，而不是旧日志的“7/7”）。

### N4 [Nit] 探针样本与 AC-S3.5 的 1k 行 `.sv` 不是同一验收
- 本次只是“折叠规则形态是否真的产出 region”的最小对照（18/26 行）；AC-S3.5 的 1k 行 + A3.1 观测是 T29（`a4-fold-residual`），不因本次 PASS 关闭。

### N5 [Nit — 好的实践] 旧 `03-folding.log` 加更正横幅保留原始输出
- 正确做法；T66 重录 10/11 时也请沿用“保留 + 标注取代”，不要 silent overwrite。

## 4. 我不判的（declined to judge，逐条给理由）

1. **T66 的具体修复质量** — 另一张票，另有评审；本 verdict 只登记“为什么必须修”。
2. **AC-S3.5 的 1k 行 `.sv` / tree-sitter 关闭态 / A3.1 观测** — 属 T29；见 N4。
3. **`<folding>` 字面形态与 `beginRegion` 的等价性结论（C15/AMENDMENTS rev 2）** — 另一个 work 任务。
4. **distro 数据目录里 KDE GPLv3+ 定义在交付件中是否会被意外使用** — 交付件走 F2（0 内嵌定义）+ AppRun 的 `VEDIT_SYNTAX_DIR`；packaging lane 的既有结论，本次未改。
5. **`run.sh` 的其它健壮性（是否 `--user`、XDG 隔离等）** — 与本次判据无关；T66 处理。

## 5. 本次评审的边界

- 我**没有**修改 `run.sh`/`probe.cpp`（T45/T66 的文件）；我的 SV 采样修正只在 `target/review-probe/probe-sv.cpp`（gitignored 的评审工具副本），并在 §2 写明改了什么。
- 我独立复跑了：正确 flags 的真 F2 链接（readelf RUNPATH + ldd 确认）、after/before 两组 × Verilog/SV 两样例、flawed（DISTRO）对照；全部数字见 §2。
- 未重跑 `evidence/syn-kde-data` 的其它日志；未改 requirements/notices。
- 第一版 record `rec-20261031T111415-0bbbdc` 不可编辑；其“F2 上 7/7 both”的依据以本文件为准（更正为真 F2：Verilog 7/7、SV 11/11；旧日志为 DISTRO）。
- 唯一写入是本文件（`evidence/verdicts/fix-xml-folding/verdict.md`）。

## 6. 结论与一句话理由

**Verdict: PASS（有条件）** — 风险 High。

理由：XML 修复本身正确，且经我**在真 F2 上**独立重测成立（Verilog 7/7、SV 11/11、before 0/0）；但该 lane 的**证据之记录**（`10/11-folding-*.log`）实际测的是 DISTRO 库、且 SV 样例从未生效（`ldd` 与 flawed 复现已证明），因此本 PASS 的正式依据必须由 **T66** 修好 `run.sh`、加 `ldd` 防呆并重录 10/11 后才能归档；在那之前请把本票读作“有条件 PASS / 依据待重录确认”，并保留本文件作为真 F2 数字的来源。
