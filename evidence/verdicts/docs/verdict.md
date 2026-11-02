# T34 独立评审结论 — T33 `docs-amendments`（C15：AMENDMENTS rev 2 + 许可登记 + ADR 冲突修正）

- **评审者**：Reviewer（cell `cell-20261031T085543161Z` 的第 3 个 Reviewer 实例；verifier ≠ writer `d842e2f9…`）
- **被评审任务**：board `T33`（kind=work，owner=Senior Engineer，status=completed）
- **冻结契约**：`.mpd/cellwork/cell-20261031T085543161Z/contracts/docs-amendments.md`（split revision 2，2026-10-31T09:51:34Z）
- **验证环**：`loop-20261031T095153-b31990`（`mpd_verify_seat` 已绑定本会话）
- **结论**：**PASS**（bound seat；验收判据 1–4 全部满足）
- **非阻塞发现**：2 条 Low（F-1、F-2），见下表；1 条集成提示（F-3，不属于 T33 交付物）
- **证据目录**：`evidence/verdicts/docs/`（本文件 + 01/02/03/04 四份可复跑证据）

> 记录顺序声明：本结论先于任何"writer 推理"形成——我只读了契约、需求/交接文档（documentation band）、
> 以及 writer 的可复跑证据脚本输出；未读 writer 的报告文本，未据实现调整结论。
> 对 `docs/handover/appendix/amendments-rev2.md` 本身的阅读是"被评审的交付物"（文档任务），不是实现。

---

## 1. 实际执行的证据（命令 + 观测输出）

### 1.1 仓库声明的门禁（契约 Criterion 2）

```text
$ mpd_verify_evidence {kind:"gate", gate:"docs"}
gate docs exit 0 -> .mpd/verify/evidence/ev-20261031T101846-bf086f.log
# 日志内容：cmd: "bun run verify:docs" program: "…/.bun/bin/bun" argv: ["run","verify:docs"]
#           [docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md

$ bash build/gates/docs.sh ; echo EXIT=$?
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
EXIT=0

$ git diff --check -- docs THIRD_PARTY_NOTICES.md ; echo EXIT=$?
EXIT=0
```

为何没有跑 `scripts/gates.sh` / `build/gates/{tests,vendor,dist,pack}.sh`：仓库自己的交接文档
`docs/handover/05-build-and-verify.md` §4 明确写「`build/gates/*.sh` 与 `scripts/gates.sh`
**仍然是给旧 Rust/GTK 产品用的**… 对 Qt6 产品完全不覆盖」，而本任务是**纯文档变更**
（`git status` 范围内无代码、构建文件或测试改动，见 1.3）。契约 Criterion 2 要求的是
"the repository's own declared gates (read the project instruction file: AGENTS.md §4, or its equivalent)"
——本项目当前的 equivalent 就是 `05-build-and-verify.md` §4，故 `docs` 门禁 + 范围内空白检查
是本任务可执行的、相关的那一条。**这一取舍我接受，但它意味着该门禁只查"文件存在且非空"，
不校验任何事实主张；事实主张由 1.2 的独立复跑承担。**

### 1.2 对交付物核心事实主张的独立复跑（关键）

**A. 折叠形态探针（§4.2–§4.5 与 §1"0 折叠"结论）** — 我重跑了 writer 的探针（同一 `vedit-qt6-build:local` 镜像、同一脚本）：

```text
$ bash evidence/docs-amendments/probe/run.sh
SAMPLE shipped_has_literal_folding_element=0
CASE literal-element-form: definition_valid=1
RESOLVE Verilog: valid=1 author=Yevgen Voronenko (ysv22@drexel.edu), Ryan Dalzell (ryan@tullyroan.com) section=Hardware license=
CONTROL resolved-Verilog: formats=50 begins=7 ends=7
RESOLVE SystemVerilog: valid=1 author=Sean O'Boyle (seanoboyle@intelligentdv.com) license=GPLv3+
CONTROL resolved-SystemVerilog: formats=51 begins=9 ends=9
CONTROL shipped-SV-attribute-form: definition_valid=1 formats=56 begins=0 ends=0
CONTROL attribute-form: formats=40 begins=0 ends=0
CONTROL literal-element-form: formats=14 begins=0 ends=0
CONTROL corrected-attribute-form: formats=40 begins=6 ends=6
VERDICT literal_element_yields_fold=0 shipped_rules_yield_fold=0 corrected_list_rules_yield_fold=1
EXIT=0
```

→ 与 writer 的 `04-folding-form-probe.log` **逐行一致**。由此独立证实：
① `<folding begin= end=/>` 字面元素在 KSyntaxHighlighting 6.24.0 下 `begins=0`（"定义有效但零折叠"）；
② 交付件的"字面关键字串"写法 `begins=0`（规则被当作未知关键字列表丢弃）；
③ 修正的"关键字列表引用"写法 `begins=6 ends=6`（正控成立）；
④ `definitionForName("Verilog"/"SystemVerilog")` 在带内嵌定义的开发镜像里解析到的是 **KDE 内嵌定义**
（作者 Yevgen Voronenko / Sean O'Boyle，SV 那份 `license=GPLv3+`），且数字 7/7、9/9 与旧
`evidence/syn-kde-data/03-folding.log` 完全吻合 —— §4.4 的"证据归属更正"成立。

**B. KDE-lane 验证器（29/29 与 7/7、9/9）** — 我重跑了 writer 03 号证据的同一条命令：

```text
$ docker run --rm --user 1000:1000 -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp \
    vedit-qt6-build:local target/qt6-build/src/syntax/kde/vedit_syntax_kde_selftest
FOLD verilog formats=50 begins=7 ends=7
FOLD systemverilog formats=51 begins=9 ends=9
SYN-KDE-DATA-SELFTEST ok=1 checks=29 failures=0
EXIT=0
```

**C. 身份/路径/登记核对** — `evidence/verdicts/docs/run-checks.sh`（只读，47 项）：**pass=47 fail=0**。
其中独立复算（不是我引用 writer 的数字）：

| 对象 | 我实测 | 文档主张 | 结论 |
|---|---|---|---|
| `docs/handover/appendix/amendments-rev2.md` | 327 行 / 24482 B / `28ea2a07…` | 01-file-list：同 | ✅ |
| `THIRD_PARTY_NOTICES.md` | 167 行 / 13011 B / `8f482768…` | 01-file-list：同 | ✅ |
| `docs/decisions/r2-license.md` | 261 行 / 23479 B / `ced6734b…` | 01-file-list：同 | ✅ |
| `docs/handover/README.md` | 59 行 / 5343 B / `a3432593…` | 01-file-list：同 | ✅ |
| `resources/syntax/vedit-verilog.xml` | 78 行 / 5172 B / `3d1ec41f…` | C14：同 | ✅ |
| `resources/syntax/vedit-systemverilog.xml` | 89 行 / 6276 B / `dc6e0a47…` | C14：同 | ✅ |
| `third_party/tree-sitter/verilog/{LICENSE,grammar.js,src/parser.c,src/tree_sitter/parser.h,src/node-types.json}` | `92dbb929…`/1082 B、`323c121a…`/133684 B、`a15f78b8…`/46042888 B、`ab104936…`/5378 B、`e99b42ba…`/271904 B | THIRD_PARTY §9.2、amendments §2.2：同 | ✅ |
| `third_party/tree-sitter/PROVENANCE.md` | pin `4457145e795b…` | §2.2：同 | ✅ |
| XML `<language>` 属性 | `author="VEdit" license="LGPL-2.1-or-later"`（两份） | §2.1 / notices §9.1：同 | ✅ |
| 未随包 KDE 文件名 | `resources/syntax/` 下无 `verilog.xml`/`systemverilog.xml` | C11/C14、verify-not-shipped.sh:53-57 | ✅ |
| `<folding` 元素计数 | 0 / 0 | §4.2：0 | ✅ |
| `beginRegion`/`endRegion` 计数 | verilog 9/7；SV 14/11 | C14：同 | ✅ |

**D. 文档内引用指针的抽查**（`run-checks.sh` H 段，全部命中）：

```text
verify-not-shipped.sh:27-28   → KDE_SV=3361d8b4… / KDE_V=7199e367…      ✅
verify-not-shipped.sh:53-57   → for n in systemverilog.xml verilog.xml（文件名黑名单）✅
verify-not-shipped.sh:89-116  → "# --- (2) LIBRARY level ---" 内嵌定义扫描     ✅
assemble-appdir.sh:109-112    → install … usr/share/org.kde.syntax-highlighting/syntax/ ✅
AppRun:33                     → export VEDIT_SYNTAX_DIR=…（同一路径）          ✅
runtime-manifest.txt:195-196  → 两份 XML 的 sha256 与仓库一致                 ✅
amendments-a1-a5.md:140       → A4.1 字面 "<folding begin="…" end="…"/>" 原文存在 ✅
```

**E. 写域与改动形态**

```text
$ git diff --numstat -- THIRD_PARTY_NOTICES.md docs/decisions/r2-license.md docs/handover/README.md
55  0  THIRD_PARTY_NOTICES.md        # 纯新增
41  0  docs/decisions/r2-license.md  # 纯新增（§9 勘误）
1   0  docs/handover/README.md       # 纯新增（一行登记）
$ git status --porcelain -- docs THIRD_PARTY_NOTICES.md evidence/docs-amendments
 M THIRD_PARTY_NOTICES.md / M docs/decisions/r2-license.md / M docs/handover/README.md
?? docs/handover/appendix/amendments-rev2.md / ?? evidence/docs-amendments/
```

T33 声明的改动集 = 5 条路径，全部落在 `docs/` ∪ `THIRD_PARTY_NOTICES.md` ∪ `evidence/docs-amendments/` 内。
工作树里另有 ~90 条改动（`CMakeLists.txt`、`Cargo.toml`、`src/app/main.cpp`、`scripts/*`、
`evidence/m0-build/*` 等），按 mtime 与内容属于并发 lane（板上的 T6/T8/T10/T39/T43/T45 正在 running，
其中 T45 正是"重写 resources/syntax 两份 XML"），**不是** T33 的改动集；判据 1 按"任务改动集"判读。

### 1.3 需求/契约逐条对照

| 契约要求 | 证据 | 结果 |
|---|---|---|
| 交付物存在于 `docs/handover/appendix/amendments-rev2.md` 并登记进 `docs/handover/README.md` | 文件存在（327 行）；README:33 有登记行 | ✅ |
| ① `.sv` 关闭态折叠的最终决定与理由 | §1：决定 (a) 定案、(b) 关闭；引用 requirements 决策记录；并如实写明 **A4.5 仍未满足（交付件 0 折叠）** | ✅ 且**未粉饰** |
| ② 两份 LGPL XML + MIT grammar 按 C14 路径与 sha256 登记 | §2.1/§2.2 表；`THIRD_PARTY_NOTICES.md` §9（+§9.1/§9.2/§9.3）；我的独立复算全部一致 | ✅ |
| ③ ADR 产物命名 vs `verify-not-shipped.sh` 黑名单冲突的处理 | §3.1 冲突事实（§5 A/C 原文经我核对确实互斥且与黑名单互斥）、§3.2 处理（保留黑名单 + 等价改写路径 + 追加 §9 勘误 + 可执行断言） | ✅ 未放宽任何门禁 |
| ④ `<folding begin= end=/>` 与 `beginRegion`/`endRegion` 等价性结论（引用 FoldingProbe 证据） | §4.1–§4.6；引用 `src/syntax/kde/tests/validate_syntax_data.cpp:55-86` 的 FoldingProbe，并用 03/04 证据 + 我复跑的探针给出结论；§4.6 明确"等价实现=关键字列表引用、不得因字面元素缺失判 FAIL、但等价性不能替代交付验收" | ✅ |
| 约束：不阻塞主干、写域仅 `docs/**` 与 `THIRD_PARTY_NOTICES.md`、不改判据/测试/门禁 | 改动形态见 1.2.E；`git status` 无 `tests/`、`build/gates/`、`resources/syntax/` 改动（XML 哈希仍等于 C14 基线） | ✅ |
| 证据 `evidence/docs-amendments/`（文件清单 + sha256 核对输出） | 00–06 + probe/ + verify.sh；我复跑 `verify.sh` → `summary: pass=33 fail=0, EXIT=0` | ✅（C9 完整性见 F-2） |

---

## 2. 发现（findings）

| ID | 严重度 | 症状（symptom） | 期望（expected） | 证明来源（doc source） |
|---|---|---|---|---|
| **F-1** | **Low** | `amendments-rev2.md` §4.3 与 §4.7 称两份 XML 的折叠规则为 **「14 + 14 条」**；实测 verilog **14** 条、SV **23** 条（带 `beginRegion`/`endRegion` 的 `<keyword>` 规则；SV 的总 `beginRegion`/`endRegion` 出现次数 14/11，其中 2 条是注释区 `Detect2Chars`，故 keyword 规则 = 23）。同一节又写「全文共 **37** 条 `Unknown keyword list` 警告」（14+23=37），**自相矛盾**；§4.7 把同一错误数字写进了给修复 lane 的改造指令 | 写成「14 + 23 条」；§4.7 的改造范围同步更正（否则按字面执行会漏改 SV 的 9 条规则） | `requirements.md` **C14**：SV「14 个 `beginRegion` / 11 个 `endRegion`」；`docs/handover/appendix/amendments-rev2.md:194,199,262`；`evidence/docs-amendments/04-folding-form-probe.log`（`ProbeSVAttributeForm` 恰 **23** 条 `Unknown keyword list`） |
| **F-2** | **Low** | `evidence/docs-amendments/` 的 6 份证据文件里，只有 `00-context.txt` 带 `git status --porcelain`；`01-file-list.txt`、`02-sha256-check.txt`、`03-ksh-validator-rerun.log`、`04-folding-form-probe.log`、`06-gates.txt` 的证据头只有 `collected_at` + `git_head` | 按 C9 每个证据文件头都写「采集时间 + `git rev-parse HEAD` + `git status --porcelain`」（`00-context` 的树状态可作为同一轮的整体上下文，但 C9 的字面要求是每份文件都带） | `requirements.md` **C9 证据规则**（rev 3 第 76 行）；实测各文件头（见 01-file-list 第 3-4 行、02 第 3-4 行、03 第 3-4 行、04 第 3-4 行、06 第 3-4 行 vs 00 第 5-10 行） |

**F-3（不是 T33 的缺陷，是板/需求一致性提示，供 captain 处理）**：
`requirements.md` rev 3（AC-S3.5 与 `fix-xml-folding` 登记）与 `amendments-rev2.md` §6 U-2 都要求
「T29（A4 残项）必须依赖 XML 修复（现为板上的 **T45**）」，但板面 **T29 的 `blockedBy` 仍只有 T19**，
没有 T45。若不补，T29 可能在 XML 修好之前开跑，量到 0 折叠或引用失效证据。

---

## 3. 我无法验证的部分（诚实声明）

1. **GUI/视觉与交互**：本机无显示服务器，不具备任何窗口级观测能力。本任务的验收判据不含
   用户可见交互，故没有"通过视觉"的结论；折叠相关的结论全部来自无头探针/自检的**计数**证据，
   不是视觉证据。（C6 也规定真实窗口只能由用户在有屏机器上验。）
2. **F2 交付库形态的直接测量**：我没有自建 `QRC_SYNTAX=OFF`（无内嵌定义）的交付库并跑 A4.5。
   我验证的是"交付的 XML 规则本身产出 0 折叠"（探针用私有语言名加载该文件，与内嵌定义无关）
   以及"修正写法产出 6/6"；由此 §4.4「F2 下会 0 折叠」是**合理推论**而非我亲手测出的 F2 数字。
   这与文档的自我声明一致（它同样没有声称已测 F2 的 A4.5）。
3. **AppDir 产物侧逐文件 sha256**：我只核对了既有 `evidence/m0-runtime/runtime-manifest.txt:195-196`
   与仓库哈希一致；没有重打 AppDir/AppImage（§2.3 已把产物侧复核分配给 `pack-final`）。
4. **GtkSourceView 4.6.1 `.lang` 上游哈希**（`7670dd…`/`9d1f67…`）：只追到既有证据
   `evidence/highlight/03-langspec-provenance.txt` 的 `sha256sum` 输出；上游 tarball 不在仓库里，
   我没有重新下载哈希。

---

## 4. 结论与建议

**PASS**：交付物存在且四处内容齐备、写域干净、仓库声明的 `docs` 门禁 exit 0、四件内容的事实主张
经我独立复算与探针复跑后成立；尤其是它**主动记录了两个对自家不利的事实**（交付件 0 折叠、
旧 03 日志的 7/7 与 9/9 实为 KDE 内嵌 GPLv3+ 定义），这是本轮最有价值的部分，也与我复跑的结果一致。

**建议（非阻塞，不必重开 T34）**：
1. 让 writer 用一次极小改动修 F-1（`14 + 14` → `14 + 23`，两处），并把 F-2 的 `git status --porcelain`
   补进那 5 份证据文件的头部（或并入 T43 的批量 C9 修复范围）。
2. captain 把 T29 的 `blockedBy` 补上 T45（F-3），否则 A4 残项会在错误前提上开跑。

**引用来源（mpd_verify_record.sources）**：本文件、冻结契约
`.mpd/cellwork/cell-20261031T085543161Z/contracts/docs-amendments.md`、
`.mpd/cellwork/cell-20261031T085543161Z/requirements.md`（C9/C11/C14/C15/AC-S3.5）、
`docs/handover/appendix/amendments-rev2.md`、`docs/handover/appendix/amendments-a1-a5.md`（A4.1–A4.5）、
`docs/decisions/r2-license.md`（§5 A/C、§9 勘误）、`THIRD_PARTY_NOTICES.md`、`docs/handover/README.md`、
`docs/handover/05-build-and-verify.md`（§4 门禁语义）、`evidence/docs-amendments/*`、本目录 01–04。
