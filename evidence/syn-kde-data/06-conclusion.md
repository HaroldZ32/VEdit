# syn-kde-data (T16) 结论：私有路径加载 + 随包 Verilog/SystemVerilog（LGPL）

**结论：交付成立。** 私有路径加载、我们的 LGPL Verilog/SystemVerilog 定义（含 A4 要求的
折叠区）、`.sv` 缺失时的可诊断降级、以及与 packaging `--syntax` 插槽一致的数据目录约定，
全部实现并实测：validator exit 0、**29/29 CHECK PASS**、`09-result.txt` = failures=0。

- Board task: `T16` · 契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/syn-kde-data.md`
- 交付: `src/syntax/kde/`（loader + validator + README）、`resources/syntax/`（数据 + README）
- 证据: `evidence/syn-kde-data/`

## (a) C6：从私有路径加载定义与主题

- **选择**：`Repository::addCustomSearchPath(VEDIT_SYNTAX_DIR)`（运行时、按 repository、可回退，
  与 T11 的 `--syntax <dir>` 插槽一致）。
- **未选** `XDG_DATA_DIRS`（全局 env 泄漏到无关进程）与编译期 `NO_STANDARD_PATHS`
  （需重建库，开发期还会吞掉用户定义）——理由写在 `src/syntax/kde/README.md §Strategy`。
- 实测（`01-load.log`）：`LOAD strategy=Repository::addCustomSearchPath(...)`、
  `search_paths=.../resources/syntax`、`definitions=451 themes=30`、
  `private_definition_files=vedit-systemverilog.xml, vedit-verilog.xml`。

## (b) 随包 verilog 定义（自有 LGPL）+ 主题

- `resources/syntax/vedit-verilog.xml`（`<language name="Verilog">`）与
  `vedit-systemverilog.xml`（`<language name="SystemVerilog">`），
  **LGPL-2.1-or-later**（`LICENSE-LGPL-2.1`），条文/关键字经 GtkSourceView 4.6.1
  （`scripts/versions.env` 锁定 tarball）交叉核对；**没有复制 KDE 文件**。
- 主题来自同一 Repository（内置 30 个），随包主题目录可用同样 addCustomSearchPath 机制扩展。
- 文件名刻意不同于 `verilog.xml`/`systemverilog.xml`；loader 另按 sha256 检测并拒绝
  KDE 的两份禁止定义（`04-licensing.txt`：0 hits）。

## (c) SystemVerilog：随包我们自己的 LGPL 移植，缺失时降级可诊断

- 用户裁决 A4(a)：我们自己的 `vedit-systemverilog.xml` 必须含 `<folding>` 语义的
  `<keyword beginRegion/endRegion>` 区，覆盖 module/interface/program/class/function/
  task/generate/case/begin-end（另有 package）。
- 实测（`03-folding.log`）：Verilog 样例 **7 begin / 7 end**，SystemVerilog 样例
  **9 begin / 9 end**；`FOLD ... formats>0` 证明着色也生效。

  > **更正（T45，2026-10-31）**：`03-folding.log` 量到的 **不是**我们自己的 XML。
  > 那一轮解析到的定义 author 是 `Yevgen Voronenko (ysv22@drexel.edu), Ryan Dalzell
  > (ryan@tullyroan.com)`、version=8，即 **KDE 内嵌的 Verilog 定义**（SystemVerilog 那份
  > 是 KDE 的 **GPLv3+** 定义）。当时没有断言 author/section，所以 7/7、9/9 只能证明
  > “KDE 的定义能折叠”，**不能作为自有 XML 的证据**。自有 XML 当时的真实测量是
  > **begins=0 ends=0**，且伴随 `Unknown keyword list "module"` 等 loader 警告。
  > T45 修复后的正确测量见 `10-folding-before-F2.log` / `11-folding-after-F2.log`
  > （**真 F2 库，ldd 证明**、author=VEdit 断言；before 0/0 → after
  > **Verilog 7/7、SystemVerilog 11/11**，SV 用 26 行真样例）。
  > 首版 `10`/`11` 日志已被 T66 证明测的是 DISTRO 库且 SV 分支未生效，文件保留并加
  > `# INVALID (T66)` 横幅，以 `-F2` 版为准。
- 缺失场景（`02-degradation.log`，validator 在临时目录复制一份不含 systemverilog 的数据）：
  `LOAD degraded.definitions=451 sv_available=0`，`DIAG ... SystemVerilog definition not shipped
  (C7 pending) ... degradable ... not a crash`，`resolveLanguageForFile("a.sv") == "Verilog"`，
  Verilog 仍可用；CHECK `degrade.*` 全 PASS。
- 明确不使用镜像库内嵌的 KDE SystemVerilog（GPL）：loader 记录
  “definition exists only inside the syntax library ... deliberately not used”。

## (d) 数据目录约定与 packaging 一致

```
repo:    resources/syntax/*.xml
shipped: <AppDir>/share/ksyntaxhighlighting/syntax/*.xml
VEDIT_SYNTAX_DIR = <AppDir>/share/ksyntaxhighlighting
loader:  addCustomSearchPath(VEDIT_SYNTAX_DIR)   # loads <dir>/syntax
```
与 `packaging/runtime/assemble-appdir.sh --syntax` 的安装路径一致；本任务不改打包脚本。

## 附加：`.v` → "V" 陷阱

实测 `probe_v=V`（内置 vlang 定义也认领 `*.v`）。loader 不依赖文件名解析：
`languageNameForExtension()` 显式映射 `.v/.vh/.vlg→Verilog`、`.sv/.svh→SystemVerilog`，
并把陷阱写入 diagnostics 与 CHECK（`load.v-resolved-by-name-not-filename`,
`load.extension-map-*`）。

## 限制 / 未覆盖

- 定义是“可用且许可干净”的移植级实现，不是 Verilog/SystemVerilog 全语法；
  compare-to-GtkSourceView 的逐条规则等价性未做（仅关键字/结构/折叠面）。
- 主题随包只验证了内置主题可用；自定义主题目录未单独验证。
- 与 T60 的 `QRC_SYNTAX=OFF` 库组合未在本 lane 复跑（本镜像仍带内嵌定义；我们的
  loader 已按“只用私有目录数据”的语义工作，`degrade.*` 用文件级模拟验证）。
- 该项最终交付验收需在 T11 `--syntax resources/syntax` 组装出的 AppDir 上复跑
  `verify-not-shipped.sh`（本 lane 只提供了与其一致的数据与命名/哈希证据）。

## 证据清单

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 镜像/版本、命令、退出码、文件清单 |
| `01-load.log` | 私有路径加载 + 451 定义/30 主题 + 我们的两文件 + CHECK |
| `02-degradation.log` | 缺失 SystemVerilog 的降级诊断与回退（无崩溃） |
| `03-folding.log` | 旧日志：7/7、9/9 来自 KDE 内嵌定义（非自有 XML），已加更正横幅；自有 XML 见 `-F2` 日志（Verilog 7/7、SV 11/11） |
| `04-licensing.txt` | 文件名/语言名/sha256/许可头、禁止 KDE 定义 0 命中、数据目录约定 |
| `07-validator-full.log` | validator 全量输出（29/29 CHECK，exit 0） |
| `09-result.txt` | `failures=0` |
