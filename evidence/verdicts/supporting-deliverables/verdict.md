# T89 独立评审结论 — T83（真机验证包）+ T82（verible/slang 随包）+ T86（`.gitignore` 卫生）

- **评审者**：Reviewer 4（cell `cell-20261031T085543161Z`）
- **任务**：T89（手工评审任务；**无冻结契约、无 verify loop**，按 captain 指示把 verdict 落在 `evidence/verdicts/**`）
- **结论**：**PASS（带 findings）**。三笔交付都真实、可复跑；四件用户点名要查的东西（loader / highlight / lint.bundled / manifest）都是**证据级**的；
  `.gitignore` 零误伤；sha256 pin 与许可证均实测核对。
  **但 `verify-centos7.sh` 有 3 条可由坏件仍然 PASS 的软检查**（F-1/F-2），`lint.product` 恒 SKIP（F-3）——
  所以"16/0/2"目前的构成是 **13 条证据级 PASS + 3 条软 PASS**，建议在把 16/0/2 当 airtight 结论前修掉。

---

## 1. 对象 A（T83）：验证脚本能否自欺？

### 1.1 用户点名的四条：都是真取证

| 检查 | 实现（`build/centos7-verify/verify-centos7.sh`） | 判定 |
|---|---|---|
| `loader.used` / `loader.system-not-used` | **真读 `/proc/<pid>/maps`**：直接用 bundled loader 启动 app（`:297-317`，刻意不用函数+`&`，注释说明否则 `$!` 是子 shell），`grep $LOADER_ABS` → PASS；`grep /lib64/ld-linux-x86-64.so.2` → FAIL（`:320-331`） | ✅ 证据级 |
| `highlight.verilog-definition` | 不只看进程起来：要求 `verilog=1` **且** `own-xml != []`（`:229-236`）；容器报告里真的因 `own-xml=[] verilog=0` 判 FAIL | ✅ 证据级 |
| `lint.bundled` | 真跑 bundled verible lint，`grep -c '\[.*\]$'` **数诊断条目**，`DIAGS>0` 才 PASS（`:247-256`）；容器报告 `3 diagnostic(s), lint exit 1`（带违规返回 1 也 PASS，语义正确） | ✅ 证据级 |
| `package.manifest` | `sha256sum -c MANIFEST.sha256` 逐文件比对（`:140-148`）；我独立复跑：**211 个文件全部 OK，0 FAILED**（见 §4） | ✅ 证据级 |

### 1.2 SKIP 的合理性

- `highlight.mode`：需要带窗 KTextEditor 会话，offscreen 观测不到 `highlightingMode` —— **真实边界**（本机也无显示器）。
- `lint.product`：包内的 `vedit-diagnostics-ffi-selftest` 退出 1，因为需要它的 FFI fixture 目录（包里未带）——
  **原因是真的**（容器报告原文 `VER-DIAGNOSTICS-FFI-SELFTEST checks=1 failures=1`，T83 报告也承认）。
  但见 **F-3**：这条检查因此**永远 SKIP**，而且"非零退出"被转成 SKIP 而不是 FAIL。

### 1.3 失败不中断：有真实证据

脚本 `set -u`（无 `set -e`），`result()` 只累加计数（`:30-38`），末尾按 FAILS 退出（`:347-357`）。
容器报告实测：2 条 FAIL 之后其余检查照跑，`SUMMARY: PASS=14 FAIL=2 SKIP=2`，`SCRIPT_EXIT=1` —— **实测有效**。

### 1.4 检查项数与 `PASS=16 FAIL=0 SKIP=2`

健康路径逐条数（`result` 可命中次数）：

| # | 检查 | 真实机（3.10 内核）预期 |
|---|---|---|
| 1 | kernel.is-3.10 | PASS |
| 2 | glibc.system | PASS |
| 3 | package.files | PASS |
| 4 | package.manifest | PASS |
| 5 | toolchain.verible | PASS（软，见 F-1） |
| 6 | toolchain.slang | PASS（软，见 F-1） |
| 7 | app.selfcheck | PASS |
| 8 | startup.toolchain | PASS |
| 9 | startup.project | PASS |
| 10 | startup.exit | PASS（软，见 F-2） |
| 11 | highlight.stack | PASS |
| 12 | highlight.verilog-definition | PASS（T84 修复后） |
| 13 | highlight.mode | **SKIP** |
| 14 | lint.bundled | PASS |
| 15 | lint.product | **SKIP** |
| 16 | symbols.app | PASS |
| 17 | loader.used | PASS |
| 18 | loader.system-not-used | PASS |

→ **18 条 = 16 PASS + 0 FAIL + 2 SKIP**，与用户真机得到的数字**逐条对得上**。
旁证链：T83 tarball（`6ecf90a2…`）容器 14/2/2（kernel + highlight FAIL）→ T84 修复后重建
tarball（`acc58ee0…`，即当前 `build/centos7-verify/dist/` 的 sha）容器 **15/1/2**，唯一 FAIL 是 `kernel.is-3.10`
（容器共享宿主内核，设计上真机才 PASS）→ 真机 3.10 内核 → **16/0/2**。数字自洽。

### 1.5 A 的 findings（自欺面）

| ID | 严重度 | 症状 | 期望 | 证明来源 |
|---|---|---|---|---|
| **F-1** | **Medium** | `toolchain.slang` **无条件 PASS**（只要 `-x`，`:163-168` 没有任何退出/输出校验）；`toolchain.verible` 的 `$?` 是 `VER=$(… \| head -1)` 里 **head 的退出码**（`:152-158`），二进制跑不起来也 PASS（错误文本会显示在 actual 里） | 捕获二进制自身状态（如 `if VER=$("$bin" --version 2>&1); then`），slang 同样验证 `--version`/退出码 | `verify-centos7.sh:152-168`；当前包实测 `Version v0.0-4298-g085eef05` / `slang version 12.1.0+0` 是靠 actual 文本，不是靠判定 |
| **F-2** | **Medium** | `startup.exit` **无条件 PASS**：只要 app 与 samples 存在就 `result PASS … "exit $START_RC"`（`:187-205`），启动段崩溃（非 0 退出）也记 PASS | `START_RC != 0` 时判 FAIL | `verify-centos7.sh:202` |
| **F-3** | **Medium** | `lint.product` 把**非零退出的包内二进制转成 SKIP**（`:261-274`），而包不携带 selftest 需要的 FFI fixture → 该检查**永远 SKIP**；诊断链路一旦回归，只会在 SKIP 里消失 | 二进制/前置缺失才 SKIP；已随包的二进制非零退出应 FAIL（或明确标 N/A 并从 PASS/FAIL 总数里剔除） | `verify-centos7.sh:261-274`；容器报告 `RESULT SKIP lint.product … exit 1 \| VER-DIAGNOSTICS-FFI-SELFTEST checks=1 failures=1` |
| **F-4** | Low | 用户的**真机** `PASS=16 FAIL=0 SKIP=2` 报告不在 `evidence/`（归档的只有容器 14/2/2 与 T84 的 15/1/2）；真机报告目前只在任务描述/用户手里 | 把真机报告原文归档（tarball sha `acc58ee0…` 已与 T84 after-run 对齐，链路可闭合） | `evidence/centos7-verify-package/report-container/`；`evidence/appdir-syntax-dir/05-tarball-sha-after.txt` |

> 说明：F-1/F-2 是"未来坏件检测"的缺口，**不是当前 PASS 值造假**——当前包的真实版本字符串、`app.selfcheck`、
> `lint.bundled` 等互相独立地佐证了这些组件确实能跑。故记为 findings 而非 FAIL。

---

## 2. 对象 B（T82）：sha256 / 许可证 / 发现链

### 2.1 sha256 真的钉住（我实测）

```text
$ sha256sum build/centos7-verify/AppDir/bin/{verible-verilog-lint,verible-verilog-ls,slang}
b9d56f9a180c33fb57f601174586c81348422ca28e27632074106c130d673b29  verible-verilog-lint   == VERIBLE_LINT_SHA256   ✓
07bf6b456dae27815cc1d6f9952beb232a5d4b3242c5e11a417028b64e67b845  verible-verilog-ls     == VERIBLE_LS_SHA256     ✓
63cb60a05f6ca57f648b8e2ef6de61ee0a3132c1c9d481f831dba52388832252  slang                  == SLANG_BIN_SHA256      ✓
（verible-verilog-format b0a7b1ae… 见 AppDir/usr/share/vedit/toolchain.txt == VERIBLE_FORMAT_SHA256）
```
- `packaging/runtime/assemble-appdir.sh`：每个随包二进制都走 `bundle_binary <name> <pin> <src>`，
  `sha256sum` 不一致直接 `ERROR … sha256 mismatch`（`:173-175`），`--require-toolchain` 缺件即退出（`:213`）；
  verible 上游 tarball 有 `VERIBLE_TARBALL_SHA256`，slang 由本仓库从源码构建、输出被 `SLANG_BIN_SHA256` 钉住。
  `toolchain.txt` 记录每个二进制的 sha256/bytes/source → **没有"下载了但没验"的路径**。

### 2.2 许可证：真核过（不是口头断言）

- 仓库权威文本：`docs/licenses/Apache-2.0.txt`（11,358 B，标准 Apache License 2.0 头）与
  `docs/licenses/slang-LICENSE.txt`（1,066 B，MIT 正文，`Copyright (c) 2015-2026 Michael Popoloski`）。
- AppDir 内实际存在：`usr/share/doc/vedit/licenses/{Apache-2.0.txt,slang-LICENSE.txt}` ✓；
  `usr/share/vedit/toolchain.txt` 记录 `license Apache-2.0=…` / `license MIT=…`；
  `RUNTIME-NOTICES.txt` 有 `Verible (bundled) v0.0-4298-g085eef05 Apache-2.0 …` 与 `slang (bundled) 12.1 MIT …`。
- `THIRD_PARTY_NOTICES.md §9.3`：表格含 组件名 / 版本身份 / 许可证 / 随包路径 / 许可全文位置，sha256 pin 指向 `scripts/versions.env`。
  差一点：**§9.3 没有显式写上游来源 URL**（Verible URL 只在 `versions.env:VERIBLE_URL`；slang 只写"从源码构建"）→ **F-6（Low）**。

### 2.3 发现链（bundled > local > PATH > env）

- 代码实测顺序（`src/workspace/diagnostics/toolchain.cpp`）：`:42` bundled → `:55` local checkout（从二进制向上找
  `scripts/fetch-verible.sh` 标记，找 `.mpd/recon/verible/bin` 与 `build/slang/out/slang`）→ `:68` PATH → `:81` env；
  选中的来源会打印（`TOOLCHAIN source=verible:bundled slang:bundled missing=[…]`），缺件时状态栏/面板给"缺什么/影响什么/怎么修"。
- **文档与代码不一致（F-5, Low）**：T82 报告 §1.2 与 NOTICES §9.3 写的是 **"bundled > PATH > env"（三级）**；
  代码与 AGENT.md 是 **四级（含 local checkout）**。功能上代码正确、且会报告来源，不存在"以为用系统版实际用随包旧版"的静默歧义；
  建议把报告/NOTICES 的措辞统一为四级。

---

## 3. 对象 C（T86）：`.gitignore` 零误伤

```text
$ git ls-files -z | git check-ignore --stdin -z        → 空（0 条）            ✓ 用户指定命令
$ git ls-files -z | git check-ignore --no-index --stdin -z | 分组
      69 build/gui-spike/                             ← 既有规则（.gitignore:42，2026-10-31 34281d6 引入），非 T86 新增
$ git ls-files build/centos7-verify/AppDir build/centos7-verify/stage build/centos7-verify/dist | wc -l  → 0
$ git check-ignore -v build/centos7-verify/{AppDir,stage,dist} → .gitignore:47/48/49 命中
$ git status --porcelain build/ → 空
$ du -sh：AppDir 287M · stage 287M · dist 104M（574M 的 AppDir+stage 已不出现）
```
- 规则只命名 `build/centos7-verify/{AppDir,stage,dist}/` 三个**生成**目录；`samples/verilog-demo`（1.4 KB 手写 fixture）
  与 `build/glibc217-spike/`（`run-spike.sh` 源脚本）**按理由不忽略**，且已作为源文件入库——`git status` 因此干净。
  这与 T86 的论证及 captain 的接受记录一致，我认为**正确**：把源文件 ignore 掉才是真正的误伤。
- `--no-index` 的 69 条 tracked-but-ignored 全部是**既有** `build/gui-spike/` 规则造成的，报告 §4/§8 已披露，captain 已裁定本轮不动 → **F-7（Info，非本任务缺陷）**。

---

## 4. 我实际跑过的命令（汇总）

```text
git ls-files -z | git check-ignore --stdin -z                      → 0 hits
git ls-files -z | git check-ignore --no-index --stdin -z           → 69 hits, 全部 build/gui-spike/
git check-ignore -v build/centos7-verify/{AppDir,stage,dist}       → .gitignore:47/48/49
git ls-files build/centos7-verify/{AppDir,stage,dist} | wc -l      → 0
git status --porcelain build/                                      → 空
du -sh build/centos7-verify/{AppDir,stage,dist}                    → 287M / 287M / 104M
(cd build/centos7-verify/stage/vedit-centos7-verify-c682694 && sha256sum -c MANIFEST.sha256)
                                                                  → 211 files OK, 0 FAILED, exit 0
sha256sum AppDir/bin/{verible-verilog-lint,verible-verilog-ls,slang} → 与 versions.env 三个 pin 逐一相等
grep -c '^RESULT PASS|FAIL|SKIP' report-container/*.txt            → 14 / 2 / 2，与 SUMMARY 一致（总 18）
```

## 5. 未能验证的部分

1. **真机 CentOS 7 / 内核 3.10 本身**：本机是 7.0.0 内核，归档的也只有容器报告（14/2/2 与 T84 的 15/1/2）。
   用户真机的 16/0/2 我只能核对"脚本项数=18、容器 15/1/2 只差 kernel 一条 FAIL"**算术自洽**，不能替用户在 3.10 上复跑。
2. **随包二进制在真 CentOS 7 上的执行**：容器内 glibc 2.17 用户态跑过（含 `--version` 与 lint 实跑），但内核不同。
3. **视觉/GUI**：`highlight.mode` 需要带窗会话，本机无显示器，不认证。
4. **上游来源再下载**：我核的是"pin == 随包字节"与 tarball pin 链，没有重新下载上游 tarball 复算。
5. T83 的 F-1/F-2 里"坏件是否真会假绿"是**代码路径判定**（我读了脚本），没有实际替换一个坏二进制做演练（那会改动包内文件）。

## 6. 结论

**PASS（带 findings）**。三笔交付的核心都真实：验证脚本的四条关键取证（loader 真读 `/proc/<pid>/maps`、
highlight 真查 `own-xml`+`verilog`、lint 真数诊断、manifest 真逐文件 sha256）**都能在坏件上变红**（容器报告 2 FAIL 已实证）；
SKIP 理由真实、失败不中断、16+0+2=18 与脚本逐条对得上；sha256 pin 与 AppDir 实际字节逐一相等；许可证是真实全文并随包；
`.gitignore` 零误伤（0 条 tracked-ignored 来自新规则）。

**需要在放行 Slice 2 前处理的**（都不是"当前证据造假"，是"检测器不够硬"）：
1. F-1/F-2：把 `toolchain.verible` 的退出码来源、`toolchain.slang` 与 `startup.exit` 的判定改成真校验（Medium）。
2. F-3：`lint.product` 要么随包带 fixture 让它能真跑，要么明确 N/A 并从总数里剔除；非零退出别转 SKIP（Medium）。
3. F-4：把用户真机报告归档（Low）；F-5/F-6：文档统一到四级发现链并补上游来源 URL（Low）。
