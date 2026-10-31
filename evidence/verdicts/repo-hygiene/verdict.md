# 独立评审 verdict — T57 repo-hygiene（生成数据集不入库：`.gitignore` + 确定性生成器）

- Cell: `cell-20261031T085543161Z` · 被评 work: `T57`（repo-hygiene）· 评审: captain 记录为 PASS
- 冻结契约: 无独立冻结契约文件（captain 手工建立）；被评对象为 `.gitignore`、`src/workspace/filetree/tools/generate-testdata.sh` 与 `evidence/repo-hygiene/**`
- 评审人: **Reviewer**（盲审，依据 cell mail 记录）
- 落盘整理: **Lead**（T16）—— 依据 captain 的 cell mail 记录整理成文件，**不是** Reviewer 本人重新签署
- 落盘复核: Lead 于 2026-10-31T11:53Z 回对了下列每条论断的一手证据
- 日期: 2026-10-31（Asia/Shanghai）
- 风险等级: **Low**
- 结论: **PASS**

---

## 背景

filetree 性能 lane 需要一棵 **10,007 文件** 的生成数据集。若入库会污染仓库、且每次生成都可能带来无意义的 diff。T57 的裁决是：**数据集不入库，由脚本确定性生成**。

## 论断逐条复核（一手证据）

| # | 论断 | 复核结果 | 证据 |
|---|---|---|---|
| 1 | `.gitignore` 的 `filetree-testdata/` **生效** | ✅ 成立。`.gitignore:53` 即该规则；本次 T16 的 `git status --porcelain`（148 项）**不含**该目录 | `.gitignore:51-53`；`evidence/slice1-stop/10-pre-run-tree.txt` |
| 2 | **未误伤已跟踪路径** | ✅ 成立。`git rev-list --objects --all \| grep -c filetree-testdata` = **0** —— 历史上从未有该路径对象入库，因此不存在「忽略规则把已跟踪文件从索引里挤掉」的可能 | Lead 直接执行（2026-10-31T11:53Z） |
| 3 | 生成器**两次**产出的 structure/content sha256 与声称值**逐字节一致** | ✅ 成立。三处锚点完全相同：`structure_manifest_sha256=8746ffe0472cfe084d393b59b037900779aa6d2d6c4d3526a14d45e7a7b489ac`、`content_manifest_sha256=9b1cd041e8a37685490d176123b1a6ac4a52fed8ce344a4133a557acf727b0b5`；且与 checked-out residue、entry 自己的 scratch 逐一 `diff` 无输出 | `evidence/repo-hygiene/02-generator-determinism.txt:94,100,107,110-121` |
| 4 | **两个入口**在「无数据」时**自生成**而非 SKIP | ✅ 成立。测试入口独立跑：`FILETREE-TESTS checks=111 failures=0`；demo 入口：`PREPARE ok=1 big_files=10000` | `evidence/repo-hygiene/03-entry-autogen.log:243`、`04-demo-prepare.log` |
| 5 | 生成器脚本语法 | ✅ 成立（记录在案）：`bash -n` ok；`shellcheck` 宿主未安装，故未做 shellcheck | `evidence/repo-hygiene/REPORT.md` §3 |
| 6 | 仓库门禁 `docs` | ✅ 成立（记录在案）：`[docs] PASS`，exit 0 | `evidence/repo-hygiene/REPORT.md` §3 |

## 交接给提交者的口径（已被 T16 遵守）

> 「When committing, stage only `.gitignore`, the generator script (keep mode 100755) and `evidence/repo-hygiene/**`. The dataset itself must never be staged.」

T16 在整个 checkpoint 过程中**从未 stage 过 `filetree-testdata/` 下的任何文件**（该目录也从未出现在任何提交里，见论断 2）。

## 边界（如实）

- 本 verdict 由 Lead 依据 captain 的 cell mail 记录 + 一手证据复核整理落盘；**Reviewer 本人没有二次签署**。
- `03-entry-autogen.log` 里的两行 `SKIPPED running-as-root` 是 DAC 权限用例（rootless docker 实际以 root 跑），**按 SKIPPED 打印、不计为通过**，与数据集无关 —— 已在 `REPORT.md` §6 如实登记。
- 数据集仍留在磁盘上（被忽略），不是删除。
