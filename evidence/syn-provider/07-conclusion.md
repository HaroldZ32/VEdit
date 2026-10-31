# syn-provider (T15) 结论：SyntaxProvider 生产化 + C16 单一 owner 仲裁

**结论：交付成立。** 语言无关接口、编译期 grammar 注册表 + CMake 选择、运行期整体开关、
C16 仲裁 API + 可查询断言（`POLICY.md` 已定稿）、以及 C18 的 shared/static 两种链接
全部实现并实测：四种构建变体 configure/build 均 exit 0，`external` 与 `static` 两种
变体的单测各 **70/70 CHECK PASS**（二进制 exit 0，ctest exit 0）。

- Board task: `T15` · 契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/syn-provider.md`
- 交付源码: `src/syntax/provider/`（接口头 + 实现 + POLICY.md + 单测）
- 证据目录: `evidence/syn-provider/`
- 构建镜像: `vedit-qt6-build:local`（Ubuntu 26.04 / Qt 6.10.2 / KF6 6.24.0 /
  tree-sitter 0.25.9 / g++ 15.2.0，见 `00-pins.txt`）

## 1. 构建与单测（观察到的命令与退出码）

Lane 自带的门禁（证据脚本，运行在固定镜像内）：

```bash
bash src/syntax/provider/scripts/run-evidence.sh \
     evidence/syn-provider src/syntax/provider target/syn-provider
# 观察到的结果：09-result.txt = failures=0；四个变体 configure/build exit 0；
# external/static 两个变体：ctest exit 0，测试二进制 exit 0，各 70 CHECK PASS
```

| 变体 | 配置 | 结果 |
|---|---|---|
| A `none` | `-DVEDIT_SYNTAX_GRAMMAR=none -DVEDIT_SYNTAX_TREESITTER=ON -DVEDIT_SYNTAX_TS_LINK=shared` | configure/build exit 0 |
| B `external` | `-DVEDIT_SYNTAX_GRAMMAR=external … shared` | **ctest exit 0 / 70 checks PASS** |
| C `static` | `-DVEDIT_SYNTAX_GRAMMAR=external … static` | **ctest exit 0 / 70 checks PASS** |
| D `nots` | `-DVEDIT_SYNTAX_TREESITTER=OFF` | configure/build exit 0（不链接 tree-sitter） |

> 说明：根 `CMakeLists.txt` 目前只自动纳入 `src/app`，尚未纳入本目录（T15 的写范围
> 不允许改根构建）。因此本 lane 的门禁是上面的独立构建 + ctest；`README.md` 的
> “Integration” 一节给出了根构建接入的 3 行片段（与 `src/app` 同款自动纳入方式），
> 由 build-base lane 落地后 `gates`/`typecheck` 才能覆盖本库。此事实已上报 captain。

## 2. 契约 (a)–(d) 逐条对照

### (a) 接口只暴露语言无关能力

`src/syntax/provider/syntax_provider.h`：

| 能力 | 接口 |
|---|---|
| parse | `ParseTreePtr parse(std::string_view)`；`ParseTree` 为不透明前置声明 |
| query ranges | `queryIds()` + `queryRanges(tree, query_id)`（逻辑 query 名，不出现节点类型） |
| highlight spans | `highlightSpans(tree)` |
| fold ranges | `foldRanges(tree)` |
| structure breadcrumbs | `breadcrumbs(tree, byte_offset)`（外层→内层路径） |
| 增量编辑同步 | `applyEdit(const Edit&)`（含新文本，provider 内部增量重解析） |

纯度证据（`04-interface-purity.txt`）：对 4 个公共头文件执行
`grep -nE 'TS[A-Z][A-Za-z]*|tree_sitter|TSTree|TSNode|TSQuery|node_type'` →
**无匹配（exit 1）**；真正 `#include <tree_sitter/api.h>` 的翻译单元只有
`tree_sitter_provider.cpp`（generic backend），接口与调用方均不包含。

### (b) 编译期 grammar 注册表 + CMake option；运行期整体开关在本层

- `grammar_binding.h`：`GrammarBinding`（不透明 `language` 函数指针 + 逻辑 query 列表）
  与 `GrammarRegistry`；grammar lane 用 `VEDIT_SYNTAX_REGISTER_GRAMMAR(id, factory)` 注册。
- CMake：`-DVEDIT_SYNTAX_GRAMMAR=<none|external|id>` 生成
  `generated/grammar_config_generated.h`（构建产物）；`-DVEDIT_SYNTAX_TREESITTER=ON|OFF`
  决定是否编译 tree-sitter 后端（OFF 时为 `tree_sitter_unavailable.cpp`）。
- 换 grammar 只改配置的证明（`03-swap-proof.txt`）：none 与 external 两次构建
  **全部源码 sha256 完全一致**（diff exit 0），差异只有生成的配置头与 CMakeCache 的
  `VEDIT_SYNTAX_GRAMMAR` 行；单测断言 `compiletime.grammar-matches-build-option`
  在两种构建里分别匹配自己的选项值。
- 运行期开关：`SyntaxService::setTreeSitterEnabled(false)` → `available()==false`、
  `createProvider()==nullptr`（回退 KSyntaxHighlighting，A1h 语义）；
  单测 `switch.disabled.*` / `switch.enabled.*` / `switch.deactivated.*` 全 PASS。

### (c) C16 单一 owner 仲裁（策略见 `POLICY.md`）

- `POLICY.md`（已定稿，实现前写下）：同一语言每个字节范围最多一个 owner
  （`Base`=ksyntaxhighlighting / `Overlay`=唯一 overlay id / `None`）；overlay 的
  `claim()` 会“转移”所有权；overlay 之间重叠一律拒绝并报告现有 owner；`release`/`releaseAll`
  回到 Base（或 None）；并写明 base/overlay 各自的绘制规则与失败行为。
- API：`OwnerArbiter::claim/release/releaseAll/ownerSpans/ownerAt/setBaseEnabled`。
- A1g 可查询断言：`assertSingleOwner(language, start, end, &detail)` —— 当且仅当
  `ownerSpans()` 恰好分割该区间（无空洞、无重叠、每段非空）时返回 true，否则给出
  `leading gap` / `trailing gap` / `gap or overlap between spans` / `empty span` 之一。
- 单测覆盖（`arbiter.*`）：默认 Base 所有权；claim 转移；overlay/overlay 冲突并报告
  owner；相邻 claim；部分 release 切分区间；base 关闭 → None 且仍保持单 owner 分割；
  `releaseAll` 后回到 Base；跨语言独立；非法 range。全部 PASS（70/70 中的 17 项）。

### (d) libtree-sitter 链接方式与 C18 一致

- shared 变体：测试二进制 `NEEDED libtree-sitter.so.0.25`（`05-link-mode.txt`）。
- static 变体：`readelf -d` 中**没有** `libtree-sitter` NEEDED 项（`libtree-sitter.a`
  整体链入），脚本对此做硬断言（FAIL 会记失败）。
- C18 语义：静态编译不破坏“编译期可替换性”——替换点仍是 CMake 配置
  （`VEDIT_SYNTAX_GRAMMAR` / 注册表 / 链接的 grammar target），不是运行期动态加载；
  运行期整体开关与编译期选择是两条独立开关（`README.md` Switches 表）。
- `-DVEDIT_SYNTAX_TREESITTER=OFF` 变体完全不链接 tree-sitter（`tree_sitter_unavailable.cpp`
  返回 nullptr，调用方回退底座），证明“整体关闭”也能构建。

## 3. 单测（70 项）覆盖摘要

`CHECK` 行全量见 `02-tests.log`（external 与 static 各 70 条 PASS）：
编译期配置（3）、owner 仲裁（17）、registry（4）、provider 能力/查询/面包屑（13）、
增量编辑同步（6）、坏输入不崩（2）、运行期开关与 disabled 回退（8）、以及各类
provider/service 状态断言。测试用的真实 grammar 是 **测试专用** tree-sitter-json
fixture（MIT，pin `254c42a…`，见 `tests/fixture/PROVENANCE.md`），**不是产品 grammar**。

## 4. 明确未覆盖 / 边界（如实记录）

- **未实现任何产品 grammar**（边界：`src/syn-grammars`）；测试用 json fixture 只为验证
  generic backend。
- **未实现上色**（边界：`src/syn-overlay`）；本 lane 只产出 spans 并仲裁所有权。
- **未改折叠视图**（`edit-folds` lane）；`foldRanges()` 只是数据出口。
- **根构建尚未纳入本目录**（不在 T15 写范围；README 给了接入片段，已上报 captain）。
- 线程模型：`SyntaxService` 有互斥锁，但单个 `SyntaxProvider` 实例与 `OwnerArbiter`
  的调用约定是单线程（UI 线程）；多文档并发使用需每文档一个 provider 实例。
- `applyEdit()` 语义：provider 自己维护文本副本并做增量重解析；调用方必须把同一
  edit 应用到自己的 buffer；坐标不一致时返回 false，调用方应改为全量 parse。
- grammar ABI 固定为镜像内 tree-sitter 0.25.9；换运行时需重测（C18 交付镜像复跑）。

## 5. 证据清单

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 镜像/工具链版本、tree-sitter 版本、源码 hash 清单、fixture hash |
| `01-build.log` | 四个变体的 configure/build 全量输出（exit 0） |
| `02-tests.log` | external/static 的 ctest + 测试二进制全量 CHECK（各 70 PASS） |
| `03-swap-proof.txt` | 生成配置头差异、CMakeCache 差异、源码 manifest 一致（diff exit 0） |
| `03-manifest-A/B.txt` | 两次构建前后的源码 sha256 清单 |
| `04-interface-purity.txt` | 公共头文件 grammar 类型 grep（无匹配，exit 1）+ 唯一 api.h 包含者 |
| `05-link-mode.txt` | shared NEEDED / static 无 NEEDED 的 readelf 证据 |
| `06-runtime-switch.txt` | 运行期开关与 disabled 回退的 CHECK 行 |
| `09-result.txt` | `failures=0` |
