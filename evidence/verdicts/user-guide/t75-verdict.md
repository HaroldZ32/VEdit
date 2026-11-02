# T77 独立评审结论 — T75（USER_GUIDE 矛盾修正）

> **手工建任务、无冻结契约**：T75/T77 在 cell 里没有 split-key 映射/验证环（`cellwork_status` 与
> `.mpd/verify/loops` 均无 T75/T77 的 loop），判据取自 captain 派单书；verdict 按要求落盘于此。
> 评审者 ≠ writer（writer = Junior Engineer）。证据：`evidence/verdicts/user-guide/t77-checks.log`。

- **结论**：**PASS（四件事全部达成）＋ 1 条 Medium 实质缺陷 finding**（用户找不到 Qt6 交付物的构建命令/产物路径——正是用户刚踩的坑；captain 明示“不一定判 FAIL，但必须写”）。
- 回归：`bash build/gates/docs.sh` → `[docs] PASS`，`DOCS-EXIT=0`（我实跑）。

---

## 1. F-1 真的清了 ✅

- `grep -n '打开位置' docs/USER_GUIDE.md` → **0 命中**（全文）。
- §9 表（`:205`）已改为「工程根 = 从**起点目录**向上第一个命中 … 起点目录：Qt6 = `--project <dir>` 指定的目录，旧 GTK3 = 进程启动目录」——与正文一致。
- 全文逐行看 `打开文件|工程根|--project`：
  - `:31` `./VEdit/vedit.sh path/to/top.sv # 直接打开文件` —— CLI 打开文件，不涉及工程根；
  - `:79` 「**不是由打开文件推导**」——显式否定；
  - `:82` 「Loader…接口约定调用方应传被打开文件所在目录」——**明确说明这只是接口契约且两个前端都没按它传**，后面紧跟「实际起点永远是 `--project` 目录/进程启动目录，不是打开的文件」；
  - `:191` 排障表的「打开文件但没有语法高亮」——与工程根无关。
  → **没有任何一处暗示“打开文件决定工程根”。**

## 2. F-2 的分叉真能拦住人 ✅（我自己核了代码，不信转述）

```
cli.rs:101 ->  other if other.starts_with('-') => bail!("unknown option '{other}'\n\n{}", usage()),
main.rs:25 ->  let opts = cli::Options::parse(std::env::args_os().skip(1))?;
```

- `crates/vd-app/src/cli.rs:101` 对任何未知 `-`/`--` 选项 `bail!`；
- `crates/vd-app/src/main.rs:25` 是 `main()` 里**第一件事**（`Options::parse`），在 `app::run_interactive`（GUI）之前
  ⇒ 「旧 GTK3 产物不认识 `--project`、会在 GUI 启动前以 `unknown option` 直接退出」**属实**。
- 文档步骤 3（`:60-63`）把两个前端**并列在开头**：Qt6 → `--project <dir>`；旧 GTK3 → 不认识 `--project`、会 `unknown option` 在 GUI 启动前退出、**请先 `cd` 到工程目录再启动**；§2 开头（`:54-56`）还有一行“先确认手上是哪一版”。分叉在用户执行命令之前就出现，**能拦住人**。

## 3. 我作为读者的实际理解（captain 要求写进来）

- **若我机器上是旧 GTK3 产物**：读到步骤 3 第一句就被告知 `--project` 会 `unknown option` 直接退出、要先 `cd` → **不会误用 `--project`**（我确实不会去传它）。
- **若我机器上是 Qt6 构建**：步骤 3 明确写 Qt6 → `--project <dir>`，我不会多此一举去 `cd`（`cd` 只出现在“旧 GTK3”分支里）。
- **但我读完整份指南，仍然不知道怎样得到“Qt6 构建”**——这正是下面的 finding。

## 4. 写域干净 ✅

`git status --porcelain -- docs evidence/user-guide-split` → 只有

```
 M docs/USER_GUIDE.md
?? evidence/user-guide-split/
```

T75 的 diff（`git diff HEAD -- docs/USER_GUIDE.md`，+16/−7）只动四处：§2 开头版本提示、步骤 3 分叉、
Loader 段落措辞、§9 表行；无 `docs/handover/**`、`THIRD_PARTY_NOTICES.md`、`resources/syntax/**`、
`evidence/verdicts/**` 或任何代码/测试/门禁改动（同期其它文件是 gate/其它 lane 的日志副作用，非 T75）。

---

## Findings

| ID | 严重度 | 症状 | 期望 | 证明来源 |
|---|---|---|---|---|
| **F-1** | **Medium**（实质缺陷，非本次四项判据） | 用户**无法从用户指引找到 Qt6 交付物**：① `docs/USER_GUIDE.md` 全文 **0 次**提到 `build-qt` / `scripts/build.sh` / `vedit-app` / `target/qt6-build`，步骤 3 直接假定“你已经有 Qt6 构建”；② `docs/BUILD.md` §12.2 有 `bash scripts/build-qt.sh`（4 处），但**从未写出产物路径 `target/qt6-build/src/app/vedit-app`**（`grep -c 'vedit-app' docs/BUILD.md` = 0）；③ 仓库顶层 `AGENT.md:36,38` 仍把 **`bash scripts/build.sh`** 写成“Build”（那是旧 Rust/GTK 产品的构建）——用户实际就是照它跑、只得到旧 `vd-app`，没得到 `vedit-app`。 | 在 USER_GUIDE §2（或 §1）加一行构建指引：`bash scripts/build-qt.sh` → 交付物 `target/qt6-build/src/app/vedit-app`；并在 `BUILD.md` §12.2 表格/正文点明该二进制路径；`AGENT.md:36,38` 的 “Build” 需要区分旧 Rust 产品与 Qt6 产品（否则用户还会走错） | `evidence/verdicts/user-guide/t77-checks.log` §6；`grep -c` 结果（USER_GUIDE=0、BUILD.md build-qt=4 / vedit-app=0）；`AGENT.md:36,38`；`scripts/build-qt.sh`、`scripts/build.sh` 并存 |

**通过项（记录）**：F-1 清除 ✅；F-2 分叉（含代码事实）✅；读者判断两向都对 ✅；写域干净 ✅；docs gate exit 0 ✅。

## 未能验证 / 边界

1. **GUI 可见行为**：无显示器，未跑真实窗口；`--project` / `cd` 的实际界面效果未认证。
2. **发布产物实测**：未在有屏机器跑 0.1.1 三件套验证 `--project` 的报错；结论来自 `cli.rs:101` 的显式 `bail!` 与
   `main.rs:25` 的解析位置（解析先于 GUI，行为可判定）。
3. **用户验收对象**：无法确认用户手工验收用的是旧 GTK3 产物还是 Qt6 构建（F-1 的影响与修法取决于此，但两种情况下“怎么得到 Qt6 构建”都该在文档里写清）。
4. **F-1 的归属**：这不是 T75 引入的缺陷（T75 只修矛盾表述），是用户指引长期缺口；是否本轮修由 captain 定。

## 结论

**PASS**：派单书要求的四件事（F-1 清除 / F-2 分叉有效且代码事实成立 / 读者不会误用 `--project`、也不会多此一举 `cd` / 写域干净）**全部达成**，docs gate 绿。
**但必须同时带走 F-1**：文档（尤其 USER_GUIDE）不足以让用户找到 Qt6 交付物的构建命令与产物路径，`AGENT.md` 的 “Build” 仍指向旧 Rust 构建——这正是用户刚踩的坑；建议在用户下一次验收尝试之前补上这几行（或用一条小文档任务修）。
