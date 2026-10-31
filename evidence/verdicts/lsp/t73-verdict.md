# T74 独立评审结论 — T73（两张 FAIL 的文本返修）

> **无冻结契约**：本任务（及 T73）在 cell 里没有 split-key 映射/验证环（`cellwork_status` 与
> `.mpd/verify/loops` 均无 T73/T74 的 loop），判据取自 captain 派单书；按派单书要求把 verdict 落盘于此。
> 评审者 ≠ writer（writer = Senior Engineer）。

- **结论**：**FAIL**（A 部分三项全部改对；B 部分代码引用/GTK3/.F 都对，但 **B2 仍有一处“从打开位置向上”的残留**，
  且真实用户流程有风险：发布产物的主步骤给的是 `--project`，而发布产物（旧 GTK3）的 CLI **拒绝未知选项**）
- 证据：`evidence/verdicts/lsp/t73-A-report-audit.log`、`t73-B-guide-vs-code.log`

---

## A 部分 — lsp-v2 报告（全部改对 ✅）

1. **全目录 grep 无矛盾表述**：`grep -rniE 'not prov|不可证|NOT PROVEN|SLANG success is NOT' evidence/lsp-v2/`
   → **0 命中**（不是只看 09）。
2. **残句已重写成通顺两句**：`09-report.md` §4.1 现在是两条并列事实——
   默认环境（无 `VEDIT_SLANG`）→ typed degradation（`slangFailures=7`，LSP 仍 `lsp 7/7`，不崩不造）；
   `VEDIT_SLANG=build/slang/out/slang` → `16-f1` 的 `requested=7 completed=7 failures=0`、
   `ready (lsp 7/7, diagnostics 21)`、`checks=23 failures=0`。
   文末「Unchanged honest limits: SLANG **success is proven** …」通顺、不再自相矛盾。
3. **backfill 标记与两个时刻**：`01-09,10-13` 头部的字面 `backfill:` 已存在，例如
   `# C9 header — backfill: this header block was ADDED AFTER THE FACT by T67` +
   `collected_at_utc=2026-10-31T11:28:59Z` + `git_rev_parse_HEAD=95c496e` +
   `# backfill: original collection window 2026-10-31T10:57:00Z..2026-10-31T11:11:00Z` +
   `# backfill: observed body range 2026-10-31T10:56:38Z..2026-10-31T11:11:06Z`。
   **原始行未改一格**：我逐份比对 T68 评审时记录的原始值（01 `10:57:06Z`、02 `10:57:47Z`、
   03 `10:58:48Z`、04 `10:56:38Z`、05 `11:01:02Z`、06 `11:01:22Z`、07 `11:01:22Z`、08 `10:57:48Z`、
   10 `11:01:37Z`、11 `11:07:25Z`、12 `11:10:08Z`、13 `11:11:06Z`；head 01-10 `1f47e56…`、
   11-13 `95c496e…`）——**全部一致，无篡改**。

## B 部分 — USER_GUIDE 工程根说法（大部分对，两处不过）

### B1 文档与代码逐条核对（我自己读代码，不看转述）✅

| 文档引用 | 代码实测 | 结果 |
|---|---|---|
| `src/app/main.cpp:804/824` | `804: parser.addOption({QStringLiteral("project")…`；`824: options.projectRoot = parser.value("project")` | ✅ 精确 |
| `src/workspace/integration.h:26` | `26: QString projectRoot; // empty => the shell starts without a project` | ✅ |
| `src/workspace/project/project_integration.cpp:56,69` | `56: QString root = options.projectRoot;`；`69: model->open(root);` | ✅ |
| `src/app/app_shell.cpp:390` | `390: options_.projectRoot = session.value("project")…`（会话恢复） | ✅ |
| `crates/vd-config/src/lib.rs:93-103` | Loader 契约：`start_dir` 是"the directory that holds the file the editor just opened … not the process working directory" | ✅ |
| `crates/vd-app/src/app.rs:1475-1480` | `current_dir()` → `Startup::load(&cwd, …)` | ✅（GTK3 用启动目录） |
| `crates/vd-app/src/config_glue.rs:63` | `Loader::new(cwd)` | ✅ |
| "全仓没有 file-open → project-root 的连线" | `src/workspace/project/`、`integration.cpp`、`main.cpp` 无 file-open→root 连接；Qt6 project panel 也没有 GUI 打开工程入口（`--project`/会话恢复是唯一路径） | ✅ |
| `.F` 与 `.f` 同样算根 | `crates/vd-config/src/project.rs:270` `eq_ignore_ascii_case("f")` | ✅ |

### B2 “是否还有地方暗示打开文件决定工程根” ❌

`grep -n '打开文件|工程根|打开位置' docs/USER_GUIDE.md` 逐行看：

- `:57-65` 新正文：`--project <dir>` 起点、**不是**由打开文件推导 ✅；
- `:69-79` 前端差异段：Qt6=`--project`/会话；GTK3=进程启动目录 ✅；
- **`:196`（§9 文件与位置表）仍写「工程根 = 从打开位置向上第一个命中 …」** —— 这是 T52 时代
  “打开位置=被打开文件”的措辞，**没有随 T73 一起改**，与 `:72`「不是由打开文件推导」自相矛盾。

### B3 GTK3 半句 ✅

`:69-79` 保留且正确：「旧 GTK3 前端以**进程启动目录**（`std::env::current_dir()`）为起点」，
并明确「GTK3 前端当前传的是进程启动目录（与 `Loader::new(start_dir)` 契约不一致）」「要先 `cd` 到期望的工程目录再启动」。
`.F` 大小写句（`:64-65`）正确、未被误改。

### 真实用户流程（captain 特别要求判断）❌

USER_GUIDE §1 说明**发布产物是自带 GTK3 运行时的 0.1.1 三件套**；而主步骤 `:57` 直接写
「打开工程（Qt6）：启动时用 `--project <dir>`」。**旧 GTK3 CLI 对未知选项是硬报错**：
`crates/vd-app/src/cli.rs:101` `other if other.starts_with('-') => bail!("unknown option '{other}'…")`
→ 拿发布产物做验收的用户若照主步骤执行 `./VEdit-0.1.1-x86_64.AppImage --project <dir>`，
**程序会以 unknown option 直接退出**，工程打不开；正确做法（`cd` 到工程目录再启动）只在**后面一段**
（`:69-79`）里。若验收对象是 Qt6 构建则主步骤正确——但文档没有在步骤 3 开头就说清“取决于你手上的哪个产物”，
所以按“不知道内部实现的用户”读，存在真实的踩坑路径。

---

## Findings

| ID | 严重度 | 症状 | 期望 | 证明来源 |
|---|---|---|---|---|
| **F-1** | **Medium** | `docs/USER_GUIDE.md:196`（§9 表）仍写「工程根 = **从打开位置向上**第一个命中…」，与 `:72`「不是由打开文件推导」矛盾，仍暗示“打开文件决定工程根” | 改成与正文一致的起点：`--project <dir>` 目录（Qt6）/ 进程启动目录（GTK3） | 派单书 B2；`docs/USER_GUIDE.md:196` vs `:57-79` |
| **F-2** | **Medium** | 步骤 3 的主指令是 Qt6 的 `--project <dir>`，但 §1 的发布产物是旧 GTK3 三件套，其 CLI 对未知选项 **硬报错**（`crates/vd-app/src/cli.rs:101`）→ 用户照主步骤对发布产物执行会直接退出；GTK3 的正确做法（`cd`）只在后一段 | 在步骤 3 开头写明“旧 GTK3 产物：先 `cd`；Qt6 构建：`--project`”，或把两个前端各自的操作并列成子步骤，避免用户先踩未知选项 | 派单书“真实用户流程”；`crates/vd-app/src/cli.rs:56,101`；`docs/USER_GUIDE.md:1-10,57` vs `:69-79` |
| F-3 | Low（提示） | 发布产物/验收对象在文档里没有一句总说明（§1 讲三件套是 GTK3 运行时，正文又出现“Qt6”说明），用户需要自己推断手里是哪一版 | 在 §2 开头加一行“本文同时标注 Qt6 产品与旧 GTK3 产物的差异；请按你手上的产物选择步骤” | `docs/USER_GUIDE.md:1-10` vs `:57-79` |

## 通过项（明确记录）

- **A 部分全部改对**（无矛盾表述 / 残句已修 / `backfill:` + 双时刻 / 原始采集值未被篡改）。
- **B1 代码引用逐条精确**、**B3 GTK3 半句保留且正确**、**B4 `.F` 句正确**、Qt6 侧 `--project`/会话恢复是唯一入口且文档写对。
- **回归**：`bash build/gates/docs.sh` → `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md`，`DOCS-EXIT=0`（我实跑）。

## 未能验证 / 边界

1. **GUI 可见行为**：无显示器，未跑真实窗口；`--project` 的端到端效果只按代码核对（main.cpp→ShellOptions→model->open）。
2. **发布产物实测**：没有在有屏机器跑 0.1.1 AppImage 验证 `--project` 的报错行为；结论来自 `crates/vd-app/src/cli.rs` 的显式 `bail!` 分支（解析发生在 GUI 之前，行为可判定）。
3. **验收对象**：无法确认用户手工验收用的是发布 GTK3 产物还是 Qt6 构建；F-2 的严重度取决于此（若是 Qt6，F-2 降为“表述风险”）。

## 结论

- **A 部分：全部改对，可放行。**
- **B 部分：B1/B3/B4 对；B2（`:196` 残留“从打开位置”）与真实用户流程（步骤 3 对发布产物会报 unknown option）不过 → FAIL**，
  返修只需改这两处文字（外加 F-3 一行总说明），**不需要改代码**；改完复审这两处即可转 PASS。
