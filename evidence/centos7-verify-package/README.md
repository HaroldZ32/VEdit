# 真机验证包（T91 加固后）— 证据

- cell: `cell-20261031T085543161Z` · board task **T91**（承接 T83 的验证包）
- 采集时间：2026-11-01 10:37–11:57 (Asia/Shanghai)
- `git rev-parse HEAD`：见 `build-console.txt` 与 `report-container/verify-report-container.txt`
- 构建镜像：`vedit-qt6-build:local`；验证环境：**干净 stock `docker.m.daocloud.io/library/centos:7`**（glibc 2.17，无 Qt6/KF6，无网络）

## 交付物

| 文件 | 说明 |
|---|---|
| `build/centos7-verify/dist/vedit-centos7-verify-23514fb.tar.gz` | **要拷到真机的 tarball**（104 MB）；sha256 = `e95f9817fec6106dc5259b8c1c9c12ccda9d4826834a5eb05ae0b7c62f43a17c`（见 `tarball-sha256.txt`） |
| `build/centos7-verify/verify-centos7.sh` | 一条命令的验证脚本（18 项，T91 加固后） |
| `build/centos7-verify/STEPS.md` | 中文步骤 + **硬证据/代理指标**分栏 + 四级工具链发现顺序 |
| `build/centos7-verify/build-tarball.sh` | 可复跑构建（AppDir + 样例 + **诊断 fixture** + 脚本 + MANIFEST） |
| `build/centos7-verify/negative-checks.sh` | **负向验证**：5 个破坏用例，证明修复后的检查真能 FAIL |
| `build/centos7-verify/samples/verilog-demo/` | 样例（`.f` + `bad.v` 预置 3 条违规） |
| 包内 `samples/diagnostics-fixture` | 产品诊断 selftest 的 7 文件 `.f` fixture（来自 `tests/fixtures/verilog-project`） |
| `report-container/verify-report-container.txt` | **本 tarball 在干净 centos:7 容器里的完整 18 项报告** |
| `negative-checks.txt` | 5 个负向用例的原样输出 + "仓库未被改动"证明 |

## 1. T89 F-1/F-2/F-3 的修复（逐条）

| 发现 | 修复前（软 PASS） | 修复后（硬证据） |
|---|---|---|
| **F-1a** `toolchain.verible` | `$?` 取到的是 `head` 的退出码；`-x` 存在就 PASS | 真执行 `--version`，取**进程退出码**，且输出必须含 `vX.Y-<release>-g<sha>` 版本标识 |
| **F-1b** `toolchain.slang` | 无条件 PASS（只判 `-x`） | 真执行 `--version`，退出码 0 **且**输出含 `slang version <n>` |
| **F-2** `startup.exit` | 永远 `PASS`，退出码只写进文本 | 判定来自**真实进程退出码**：非 0 → FAIL（崩溃/参数错误都会红） |
| **F-3** `lint.product` | 永远 SKIP（包里没有 fixture） | **选项 (a)**：把 7 文件诊断 fixture 打进包，真跑产品诊断 selftest（随包 `verible-verilog-ls` LSP + FFI slang），断言 `exit 0` **且** `failures=0` |

同时把 `lint.bundled` 从"至少 1 条"收紧为**确定条数**：恰好 **3** 条诊断且 lint `exit 1`。

## 2. 修后完整验证（18 项，本 tarball 实跑）

```
SUMMARY: PASS=16 FAIL=1 SKIP=1
```

| # | 检查 | 结果 | 备注 |
|---|---|---|---|
| 1 | `kernel.is-3.10` | **FAIL** | `actual=7.0.0-38-generic` —— 容器共享宿主内核，**设计如此**；真机应 PASS |
| 2 | `glibc.system` | PASS | `ldd (GNU libc) 2.17` |
| 3 | `package.files` | PASS | 8 个关键文件齐全 |
| 4 | `package.manifest` | PASS | 全部 sha256 一致 |
| 5 | `toolchain.verible` | PASS | `exit 0 | Version v0.0-4298-g085eef05` |
| 6 | `toolchain.slang` | PASS | `exit 0 | slang version 12.1.0+0` |
| 7 | `app.selfcheck` | PASS | `exit 0`（EDIT-CORE 67 / APP-SHELL 176 checks） |
| 8 | `startup.toolchain` | PASS | `TOOLCHAIN source=verible:bundled slang:bundled missing=[]` |
| 9 | `startup.project` | PASS | `installFileTree projectRoot='<解压路径>/samples/verilog-demo'` |
| 10 | `startup.exit` | PASS | 真实退出码 `0` |
| 11 | `highlight.stack` | PASS | `grammar=verilog backend=1 startup-hook=1 private-dir-in-AppDir=yes` |
| 12 | `highlight.verilog-definition` | PASS | `verilog=1 own-xml=[…]`（T83 时的 FAIL 已在当前树修复） |
| 13 | `highlight.mode` | SKIP | 需带窗会话；已写明原因 |
| 14 | `lint.bundled` | PASS | 恰好 **3** 条诊断 + `exit 1` |
| 15 | `lint.product` | PASS | `exit 0 | VER-DIAGNOSTICS-FFI-SELFTEST checks=23 failures=0`（随包 fixture） |
| 16 | `symbols.app` | PASS | `vedit-app needs GLIBC_2.39 / bundled libc provides 2.43` |
| 17 | `loader.used` | PASS | `/proc/<pid>/maps` 含包内 `ld-linux-x86-64.so.2` |
| 18 | `loader.system-not-used` | PASS | 无 `/lib64/ld-linux-x86-64.so.2` |

## 3. 负向验证（每条修过的检查都证明能 FAIL）

方法：**在容器内**解压**出厂 tarball**，只破坏一个条件，跑完整脚本，引用该检查自己的 `RESULT` 行；
仓库文件与 tarball 从不被修改。原样输出见 `negative-checks.txt`。

| 用例 | 破坏方式 | 观察到的结果 |
|---|---|---|
| `verible-wrong-binary` | 用 `/bin/true` 冒充 `bin/verible-verilog-lint` | **FAIL**：`exit 0 | true (GNU coreutils) 8.22` —— 正是"退出 0 但不是 verible"的软 PASS 场景，被版本标识拦住 |
| `slang-not-a-binary` | 写一个文本文件当 `bin/slang` | **FAIL**：`exit 127`（无法执行） |
| `startup-exit-nonzero` | 给启动命令加一个不存在的参数 | **FAIL**：`exit 1`（真实退出码） |
| `lint-count-wrong` | 把期望诊断数 3 改成 4 | **FAIL**：`actual=3 diagnostic(s)` ≠ 期望 4 |
| `lint-product-wrong-fixture` | 把 fixture 指到非 fixture 目录 | **FAIL**：`exit 1 | VER-DIAGNOSTICS-FFI-SELFTEST checks=1 failures=1` |

负向用例全部在容器内的临时副本上执行并以 `rm -rf /work` 丢弃；**仓库侧证明**见 `negative-checks.txt` 末尾：
`sha256sum build/centos7-verify/verify-centos7.sh` 与用例前一致，`git status` 里没有负向用例产生的文件。

## 4. T89 另两条 Low（已修）

- **`THIRD_PARTY_NOTICES.md` §9.3**：新增"显式上游来源（URL）"表 —— Verible
  `https://github.com/chipsalliance/verible`（release `v0.0-4298-g085eef05`，下载 URL 见
  `scripts/versions.env:VERIBLE_URL`）；slang `https://github.com/MikePopoloski/slang`（本仓库从源码构建）。
- **发现链措辞统一为四级**：`bundled > local checkout > PATH > env`。改了
  `packaging/runtime/assemble-appdir.sh` 生成的三处文案（`RUNTIME-NOTICES.txt`、
  `usr/share/vedit/toolchain.txt` 及其注释），`THIRD_PARTY_NOTICES.md` §9.3 同步，`STEPS.md` 写明四级含义。
  产物内新文案已随本次 tarball 重新生成。

## 5. F-4 真机结果归档

见 `evidence/centos7-real-machine/`：已归档用户回贴的 `SUMMARY: PASS=16 FAIL=0 SKIP=2`、
tarball sha256 前缀 `acc58ee0…`，以及"完整 `verify-report-*.txt` 待用户回贴后补入"的说明与补充步骤。
**没有**伪造任何逐项真机数据。

## 6. 边界

- 依然是容器自测：`kernel.is-3.10` 在容器里必然 FAIL（共享宿主内核），真机数据只能由用户回贴（F-4）。
- `highlight.mode` 仍是 SKIP（需要带窗会话），已被 `STEPS.md` 归入"需要窗口才能看全"的代理指标。
- 本目录不含 104 MB 的 tarball 本体（由 `build-tarball.sh` 复跑重建）；证据只放 sha256 与报告原文。
