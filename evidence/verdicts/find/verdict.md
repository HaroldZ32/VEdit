# T22 独立评审结论 — T21 `search-palette`（命令面板 10k ≤200ms + 全局搜索 Rust 索引服务）

- **评审者**：Reviewer（cell `cell-20261031T085543161Z`；verifier ≠ writer `e1acec86…`）
- **契约**：`.mpd/cellwork/cell-20261031T085543161Z/contracts/search-palette.md`（split revision 2）
- **验证环**：`loop-20261031T095153-4e9ec1`（`mpd_verify_seat` 已绑定本会话）
- **结论**：**PASS**（AC-S2.3 / AC-S2.4 均经我独立复跑达成；索引归属 Rust、Qt 无第二套）
- **非阻塞发现**：1 条 Low（证据里的符号计数/镜像 id 不可复现），见 F-1
- **证据**：`evidence/verdicts/find/01-review-checks.log`（本评审全部实跑输出）

---

## 1. AC-S2.3 —— 命令面板 10k 过滤 ≤200 ms ✅

**我自己重跑**（fresh `/tmp` 构建，与 writer 同一 bench 程序；`--fixture`/`--json` 用 `/tmp` 隔离，不覆盖 writer 证据）：

```text
cmake -S src/workspace/palette -B /tmp/bench-build -G Ninja …        CONFIGURE-EXIT=0
cmake --build /tmp/bench-build --target vedit-workspace-palette-bench BUILD-EXIT=0
vedit-workspace-palette-bench --files 10000 --iterations 200 --limit 100 \
  --fixture /tmp/bench-fixture --json /tmp/bench-review.json --assert-p95-ms 200
BENCH VERDICT PASS p95_ms=1.456 budget_ms=200.000 files=10000 source=disk   BENCH-EXIT=0
fixture file count: 10001
json: files 10000 iters 200 p95_ms 1.463 budget 200 verdict PASS source disk
```

- **机器与环境**：`00-context.txt` 记录 host/CPU（AMD Ryzen 7 7840H）/内存（29Gi）/镜像 id；`01-palette-bench.log` 记录完整可复跑命令 ✓。
- **预算判定**：GUI 口径 overall p95 **≈1.46 ms** vs 预算 200 ms（有 `--assert-p95-ms 200` 机器判定 + JSON 结论，不是肉眼）。
- 冷盘扫描 10k 文件 18 ms 单独计量，不在按键预算内（按键预算测的是候选过滤 + 100 行列表）。
- writer 的 `01-palette-bench.log`（p95 1.471 ms）与我的复跑（1.456 ms）一致。

## 2. AC-S2.4 —— 全局搜索（命中文件+行号、可点击跳转、空状态）✅

**我自己重跑**（非 root，使 unreadable-path 用例真实）：

```text
vedit-workspace-search-selftest --selfcheck   → WORKSPACE-SEARCH-SELFTEST ok=1 checks=111 failures=0 skipped=0
CHECK panel.hits.count / panel.tree.files / panel.tree.one-row-per-hit   PASS
CHECK panel.hit.line-number / panel.hit.column / panel.hit.snippet / panel.hit.unicode-*  PASS
CHECK empty.no-project.* / panel.empty.enter-query / empty.no-results.state/visible/text/tree-hidden  PASS
CHECK empty.unreadable-root.state/text / entries.unreadable.*  PASS
CHECK jump.hit-row-exists / jump.row-visible / jump.click.file / .line / .column / jump.activate  PASS
CHECK jump.product.shell-ready / jump.product.ktexteditor-provider / jump.product.first-* PASS
CHECK limit.results / limit.truncated-flag / many.start.non-blocking / lifecycle.destructor-joins-worker  PASS
```

→ 命中**文件 + 行号（+列+snippet）**、点击命中行发起跳转（含真实 KTextEditor provider 的产品路径）、
无结果/无工程/不可读根都有**明确空状态**；`--selfcheck` 退出 0。与 writer 的 `03-search-selftest.log` 一致。

## 3. 索引归属：Rust 服务，Qt 无第二套 ✅

- `src/workspace/search/global_search.cpp`：**0** 处 `QDirIterator / QDir::walk / entryList / QFile:: / readAll`；
  只有 **2** 处 FFI 调用（`vd_search_tree_query` / `vd_search_tree_poll`）+ 结果映射。
- `crates/vd-search/src/lib.rs`：拥有 `walk()`（目录遍历/剪枝）与 `search_tree()`（匹配/限额/时间预算/汇总）；
  `crates/vd-ffi/src/search.rs`：`vd_search_tree_query`（起 job）→ `vd_search_tree_poll`（返回 `VdSearchResults` + `VdSearchSummary`），
  服务 worker 执行扫描（C2：不阻塞 UI、无每按键跨界调用——selftest 的 `many.start.non-blocking` PASS）。
- **没有 FFI 的构建**：`#else` 分支把状态置为 `SearchState::Failed`，错误文本
  「the Rust search service is not linked in this build; the Qt side does not implement a second search backend」
  → 不生长出 Qt 搜索后端 ✓。
- 产品/自检二进制确实带 Rust 搜索符号（静态链接）：`nm vedit-app | grep -c vd_search` = **105**（`nm -C` = 70），自检二进制同；
  `nm -D` = 0 是**静态链接的预期**，不能读成“没链上”。

## 4. 门禁（契约判据 2）

| 门禁 | 我实跑 | 结果 |
|---|---|---|
| 仓库声明 gate `typecheck`（C++ `-Werror` + pinned 镜像 Rust clippy） | `mpd_verify_evidence {gate:"typecheck", loop:loop-20261031T095153-4e9ec1}` | **exit 0**，证据 `ev-20261101T060821-8c384c` |
| 根 ctest | 容器内 `ctest --test-dir target/qt6-build`（构建树=HEAD `31a7c2d`，search selftest 二进制构建于 14:03，晚于 T21 源码 13:37–13:46） | **100% tests passed, 0 failed out of 20**，`CTEST-EXIT=0`（基线从 18 增至 20 是其它 lane 新增测试） |
| Rust 服务单测 | `cargo test -p vd-search`（pinned 镜像） | **10 passed, 0 failed**，`RUST-TESTS-EXIT=0` |
| Rust 静态检查 | `cargo clippy --workspace --all-targets -- -D warnings` | `CLIPPY-EXIT=0` |

## 5. 写域

T21 的改动集 = `crates/vd-search/src/lib.rs`、`crates/vd-ffi/src/search.rs`、`src/workspace/search/{global_search.cpp,.h,CMakeLists.txt,README.md}`、
`src/workspace/palette/README.md`、`evidence/slice2-find/` —— 全部在允许写域内 ✓；另有 `crates/vd-ffi/include/vd_ffi.h`
（cbindgen/build.rs 生成物，非手改，writer 的 09-scope-check 已注明）。工作树里 `crates/vd-vcs/*`、`src/workspace/breadcrumb/`、`vcs/`
等属于并发 lane（09-scope-check 标 OTHER），不计入 T21。

## 6. Findings

| ID | 严重度 | 症状 | 期望 | 证明来源 |
|---|---|---|---|---|
| **F-1** | **Low**（证据卫生，非判据未达成） | ① `04-search-ffi-rust.txt` 称产品二进制与 selftest 各含 **25** 个 `vd_search` 符号；我用 `nm` 得 **105**、`nm -C` 得 **70**（两个二进制一致）——该计数不可复现（疑似旧 grep/别的过滤条件）。实质结论（Rust 搜索已静态链入产品）成立。② 同一证据集里 `00-context.txt` 记录 qt 镜像 `sha256:28a15c623ce7`，而 `01-palette-bench.log` 记录它实际用的镜像是 `sha256:c1fa0168a4e6`（两 id 不同；context 采集时间晚于 bench 7 分钟，可能期间重建过镜像） | 记录符号计数的确切命令（如 `nm -C \| grep -c vd_search`），或直接引用“符号存在”而非数量；context 里注明镜像重建或对齐到证据采集时的 id | `evidence/slice2-find/04-search-ffi-rust.txt:23-27`、`00-context.txt:7`、`01-palette-bench.log:4`；我的计数见 `01-review-checks.log` §6 |

**非 finding 的提醒**：`nm -D` 对静态链接的 Rust 符号必然为 0，后续评审不要把它当成“没链上”。

## 7. 未能验证 / 边界

1. **GUI 视觉/交互观感**：无显示服务器；命中列表/跳转的界面观感只由 headless QTest 断言（`panel.*`/`jump.*`）覆盖，截图是 L2 级 headless 抓图，未认证“看起来对”。
2. **性能只在记录机器上成立**（Ryzen 7 7840H）；≤200ms 预算的余量约 100×，其它硬件风险低但没有实测。
3. **真实键盘/输入节奏**：bench 是 200 次迭代的合成按键序列，不是真人输入。
4. **大文件/超时分支**：selftest 覆盖了 `large.*`/`many.*`，但没有在真实 10k 大仓库上跑产品级搜索（用的是测试 fixture）。
5. **T21 改动未提交**：工作树还是 uncommitted 状态（HEAD `31a7c2d`，与证据 head 一致）；提交/推送仍属 captain/Lead。

## 8. 结论

**PASS**。AC-S2.3 的 10k/≤200 ms 我在隔离构建里复现（p95≈1.46 ms，JSON 判定 PASS，机器/环境有记录、命令可复跑）；
AC-S2.4 的命中文件+行号、可点击跳转（含真实 KTextEditor provider 路径）、明确空状态由我实跑的 111 条自检覆盖；
索引确实在 Rust（`vd-search` 拥有 walk/match，Qt 后端 0 处目录遍历/文件读取、只有 2 处 FFI 调用，无 FFI 时给 typed Failed 而不是第二套实现）；
门禁 `typecheck` exit 0（`ev-20261101T060821-8c384c`）、根 ctest 20/20、`cargo test -p vd-search` 10/10、clippy 0。
F-1 只是证据里的计数/镜像 id 不可复现，建议随手更正，不影响放行。
