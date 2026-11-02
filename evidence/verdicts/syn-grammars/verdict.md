# 独立评审 verdict — T17 syn-grammars（Verilog/SystemVerilog tree-sitter grammar 绑定 + 三类 query）

- Cell: `cell-20261031T085543161Z` · Board task: `T17` · 评审任务: `T35`（AC-0.6）
- 冻结契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/syn-grammars.md`（T17）
- 评审人: **Reviewer**（独立只读；未修改任何源码/测试/门禁）
- 评审对象: 分支 `feat/qt6-ktexteditor-migration`，HEAD `9957f5f`；评审开始时工作树干净（`git status --porcelain` 空）；评审结束时其他 lane 的未提交改动已出现（`Cargo.toml`、`crates/*`、`evidence/*` 等），不在本评审范围
- 日期: 2026-10-31（Asia/Shanghai）
- 环境: 宿主 Linux 7.0.0-38-generic x86_64；pinned 镜像 `vedit-qt6-build:local`（Ubuntu 26.04.1 / Qt 6.10.2 / KF6 6.24.0 / glibc 2.43 / g++ 15.2.0 / cmake 4.2.3）
- 风险等级: **Medium**
- 结论: **PASS（附带 findings）** — 契约 (a)–(d) 成立且被独立复核；产品消费（链接/激活）不在本 lane 契约内，属 AC-S1.2/G2/G3，仍开放。

---

## 1. 总体评估

T17 交付的是一份“库级”成果：MIT grammar 身份与 provenance 可逐字节核对，绑定层保持了对 provider 接口的分层隔离，三组 query（highlights/folds/structure）在真实 `.v`/`.sv` 样例上逐条断言，四种配置的构建清单与换装证明齐全。我独立重跑了当前根构建中的语法测试（79/79，exit 0）、在锁定镜像内复跑了全仓 ctest（9/9，含 `vedit-syn-grammars-tests`），并独立核对了 vendored `parser.c` 与 `LICENSE` 的 sha256，均与 `third_party/tree-sitter/PROVENANCE.md` 一致；`src/terminal` 与 grammar 库均已在根构建里真实编译（见 `evidence/build-registration/`，我复核了 `ninja` 目标与 `nm` 符号）。因此本 lane 的 L1/L2 成立，L3 由本 verdict 补上。真正的缺口在“进产品”：`vedit-app` 里 grammar 符号为 0，根构建的 `VEDIT_SYNTAX_GRAMMAR` 实际是 `none`——这两条由 AC-S1.2（G2/G3）承担，不属于 T17 的冻结契约，但必须在后续 lane 关闭前跟踪，不能被本 PASS 掩盖。

## 2. Findings（按严重度）

### M1 [Medium — 项目级，owner 不在本 lane] 根构建的 grammar id 实际是 `none`，lane 的“默认 verilog”在根构建里被静默忽略
- 是什么：T17 的换装证明只证明了“独立构建”的四种配置；根构建里 `target/qt6-build/CMakeCache.txt` 记录 `VEDIT_SYNTAX_GRAMMAR:STRING=none`，`evidence/m0-build/build-logs/31-qt6-configure.log` 打印 `syn-grammars: compiled grammar id='none'`。`src/syntax/provider/CMakeLists.txt` 先以 `none` 写入 CMake cache，`src/syntax/grammars/CMakeLists.txt` 的 `verilog` 默认值因此失效（cache 首写者赢）。
- 为什么重要：一旦 AC-S1.2 把 grammar 链进 `vedit-app` 而不显式激活，产品会“编译了 grammar 但选中 none”，呈现为“有库无高亮”的假绿——与本项目已经栽过的 T63 同类。当前 app 尚未链接 grammar，因此暂无用户可见影响，但这是接线时必踩的雷。
- 修复方向（描述，不实施）：把 `VEDIT_SYNTAX_GRAMMAR` 的默认值收敛到单一 owner（provider 或根构建），并在**根构建**里加一条断言（例如测试 `grammar_config_generated.h` 的 id == 期望值），让 id 错配在门禁里变红，而不是只靠人工读日志。
- 证据：`target/qt6-build/CMakeCache.txt`；`evidence/m0-build/build-logs/31-qt6-configure.log`；`evidence/build-registration/README.md` 遗留发现 1。复跑：`grep '^VEDIT_SYNTAX_GRAMMAR' target/qt6-build/CMakeCache.txt`。

### M2 [Medium — 项目级，owner 不在本 lane] grammar 没有进 `vedit-app`：L4 的“链接层”未达成
- 是什么：`nm -C target/qt6-build/src/app/vedit-app` 中 `vedit::syntax::grammars` / `tree_sitter_verilog` 符号计数为 **0**；`src/app/CMakeLists.txt` 只链接了 `vedit_app_shell`、`vedit_terminal` 与 workspace 三库，没有 `vedit_syntax_grammars`。
- 为什么重要：requirements C8 把 L4 定义为“编译且链接进 `vedit-app`”。T17 自己的冻结契约没有这条（写域也不含 `src/app`），所以不据此判本 lane FAIL；但项目级的“grammar 完成”仍不成立，必须在 AC-S1.2 关闭。
- 修复方向：AC-S1.2 里加链接 + 显式激活（并与 M1 一起修）；不要为让链接“看起来有”而只加库不激活。
- 证据：本评审实测 `nm -C target/qt6-build/src/app/vedit-app | grep -cE 'vedit::syntax::grammars|tree_sitter_verilog'` → `0`；`src/app/CMakeLists.txt` 的 `target_link_libraries(vedit-app ...)` 段。复跑命令同上。

### M3 [Medium — 证据纪律 C9] 本 lane 证据没有记录采集时刻的 `git rev-parse HEAD` 与 `git status --porcelain`
- 是什么：`evidence/syn-grammars/` 下所有文件都没有 HEAD/工作树状态（`grep -rl 'HEAD\|porcelain' evidence/syn-grammars` 无命中）；`00-pins.txt` 只有时间、宿主、工具链与哈希。C9 要求证据文件头写“采集时间 + HEAD + git status”。
- 为什么重要：证据无法自我绑定到某个源码修订；本次评审只能靠“工作树干净 + 五个 lane 的源码自 76e1ba5 后未变 + 时间戳”间接对齐，属可避免的追溯成本。
- 修复方向：补一个证据头模板（time / HEAD / porcelain），由证据脚本自动写入；下轮重采时一并补齐。
- 证据：`evidence/syn-grammars/00-pins.txt`；C9 原文见 `docs/handover/appendix/requirements-rev2.md` 的 C9。

### N1 [Nit] vendored `parser.c` 46,042,888 B 入库
- 是什么：`third_party/tree-sitter/verilog/src/parser.c` 是上游生成物，单文件 46 MB。选择理由（无需 tree-sitter CLI 即可构建）在 `PROVENANCE.md` §1.1 已说明，不是缺陷。
- 为什么提：它让仓库体积与 diff 噪音偏大；后续若换 commit 会有一次 46 MB 的 blob 变更。
- 方向：维持现状可接受；如要在 `THIRD_PARTY_NOTICES.md` 外再降噪，可考虑 `.gitattributes` 标记为 binary/linguist-vendored。不改判据。

### N2 [Nit] 本 lane 过去不在根 gate 内，现已纳入
- 是什么：`evidence/syn-grammars/07-conclusion.md` §6.5 自己披露“根构建尚未纳入本 lane”。`abfe963` 已把 `src/syntax/grammars` 加入 `VEDIT_LANE_DIRECTORIES`，我复跑的根 ctest 里 `vedit-syn-grammars-tests` 已通过。此条已关闭，仅作为历史留痕。
- 证据：`CMakeLists.txt` 的 `VEDIT_LANE_DIRECTORIES`；`evidence/build-registration/01-build.txt`/`02-targets.txt`；本评审的容器内 ctest（9/9）。

## 3. 我不判的（declined to judge，逐条给理由）

1. **上游 commit 的“真身”是否确实是该 GitHub 仓库/commit** — 评审环境无网络，无法向 GitHub 复核；我核对的是“入库字节与 `PROVENANCE.md` 的 sha256 一致”，不是上游身份。身份由 `evidence/r2-license/report.md`（另有评审 T39）承担。
2. **query 覆盖是否足以覆盖全部 SystemVerilog 新构造** — 上游 grammar 是 2022 年的，`06-open-issues` 已登记；样例只有 58/50 行。`A1f` 的 1k 行 SV 折叠一致性属于 overlay/fold lane，不在 T17 契约内。
3. **`.v` 被 KSyntaxHighlighting 认成 V(vlang)** — 这是 KDE 数据层问题，T17 按 binding id 选择，不受影响（`07-conclusion.md` §6.6 已说明）。
4. **大文件/多视图下 query 的耗时** — 本 lane 未测，`07-conclusion.md` §6.7 已如实登记；属性能风险，不是本契约判据。
5. **46 MB parser.c 是否应改用 git-lfs** — 与判据无关，仅工程取舍。

## 4. 我实际复跑的（命令 + 观察，原文可复跑）

```sh
# 1) 根构建里的 grammar 测试（锁定镜像内）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  target/qt6-build/src/syntax/grammars/vedit_syn_grammars_tests
# 观察：SYN-GRAMMARS-TESTS checks=79 failures=0（exit 0）

# 2) 根 ctest（锁定镜像内）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  ctest --test-dir target/qt6-build --output-on-failure
# 观察：100% tests passed, 0 tests failed out of 9（含 Test #4 vedit-syn-grammars-tests）

# 3) 许可/身份字节核对
sha256sum third_party/tree-sitter/verilog/src/parser.c third_party/tree-sitter/verilog/LICENSE
# 观察：a15f78b8…（与 PROVENANCE 一致）/ 92dbb929…（与 PROVENANCE 一致）；LICENSE 首行 “MIT License”

# 4) 是否进产品
nm -C target/qt6-build/src/app/vedit-app | grep -cE 'vedit::syntax::grammars|tree_sitter_verilog'
# 观察：0
```

gate 结论：T17 的契约 gate（独立构建 + ctest + 测试二进制）exit 0（`evidence/syn-grammars/09-result.txt` = `failures=0`；我重跑 79/79）；根构建 gate 现覆盖本 lane（9/9）。

## 5. 本次评审的边界

- 没有在“独立四配置”下重跑 237 条 CHECK 的全部组合（只重跑了根构建的 79 条）；`02-tests.log` 的三组 79/79 是读证据，不是本次实测。
- 没有网络，无法复核上游仓库/commit 身份。
- 没有测试 overlay/运行期开关/回退（A1g/A1h）——那些属于 T18/T20，不在 T17 契约内。
- 没有修改任何源码、测试、门禁；本文件是唯一写入。

- 未运行 `bash build/gates/qt6-cxx.sh` 本体：它会重写 tracked 的 `evidence/m0-build/build-logs/*`，超出本次只读写域；改在锁定镜像内直接复跑 ctest / lane 自测（命令与观察见 §4）。

## 6. 结论与一句话理由

**Verdict: PASS（附带 findings）** — 风险 Medium。

理由：冻结契约 (a)–(d) 逐条有可复跑证据且被我独立复核（79/79 重跑、许可哈希重算、分层 grep 成立）；“进产品”的两条缺口（`none` 与未链接）属 AC-S1.2/G2/G3 的明确归属，不构成本 lane 的契约失败，但必须在 Slice 1 关闭前修掉，且 M1 必须先于链接修复。
