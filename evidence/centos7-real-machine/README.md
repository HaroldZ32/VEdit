# 真机结果归档（CentOS 7 / 内核 3.10）— T91 F-4

## 状态：**部分归档，完整报告待用户回贴**

用户已在**真 CentOS 7 虚拟机（内核 3.10）**上跑我们的验证包，回贴的**唯一一行**是：

```
SUMMARY: PASS=16 FAIL=0 SKIP=2
```

| 项 | 值 | 来源 |
|---|---|---|
| 采集时间 | 2026-11-01（Asia/Shanghai），由 cell captain 转达 | 转达 |
| 虚拟机 | 真 CentOS 7（用户部署，内核 3.10） | 用户/转达 |
| tarball sha256 | `acc58ee0…`（**只有前缀**，完整值未转达） | 转达 |
| 结果 | `PASS=16 FAIL=0 SKIP=2`（18 项） | 用户回贴的一行 |
| 完整 `verify-report-*.txt` | **尚未拿到** | —— |

> 口径：本目录**不**假装有完整日志。除上面这一行摘要与转达元数据外，任何"逐项 actual 值"
> 都不是真机实测，不得作为证据引用。逐项期望见下表，Actual 列留空待补。

## 逐项期望 vs 已知实际（Actual 列待用户回贴后填）

| # | 检查 | 期望（真机） | Actual（真机，待补） |
|---|---|---|---|
| 1 | `kernel.is-3.10` | PASS，`actual=3.10.*` | |
| 2 | `glibc.system` | 记录，CentOS 7 应为 `ldd (GNU libc) 2.17` | |
| 3 | `package.files` | PASS | |
| 4 | `package.manifest` | PASS（211 文件 sha256 一致） | |
| 5 | `toolchain.verible` | PASS，`exit 0` + `v0.0-4298-g085eef05` | |
| 6 | `toolchain.slang` | PASS，`exit 0` + `slang version 12.1` | |
| 7 | `app.selfcheck` | PASS，`exit 0` | |
| 8 | `startup.toolchain` | PASS，`TOOLCHAIN source=verible:bundled slang:bundled missing=[]` | |
| 9 | `startup.project` | PASS，`installFileTree projectRoot='<解压路径>/samples/verilog-demo'` | |
| 10 | `startup.exit` | PASS，`exit 0` | |
| 11 | `highlight.stack` | PASS，`grammar=verilog backend=1 startup-hook=1` | |
| 12 | `highlight.verilog-definition` | PASS，`verilog=1` 且 `own-xml` 非空 | |
| 13 | `highlight.mode` | SKIP（需带窗会话） | |
| 14 | `lint.bundled` | PASS，恰好 3 条诊断 + `lint exit 1` | |
| 15 | `lint.product` | PASS，`exit 0` + `failures=0`（随包 fixture） | |
| 16 | `symbols.app` | PASS（`vedit-app` 需 2.39，随包 libc 提供 2.43） | |
| 17 | `loader.used` | PASS（`/proc/<pid>/maps` 含随包 loader） | |
| 18 | `loader.system-not-used` | PASS（无 `/lib64/ld-linux-x86-64.so.2`） | |

与 `PASS=16 / FAIL=0 / SKIP=2` 的一致性：16 + 0 + 2 = 18，与上表 18 项吻合；
两个 SKIP 预期是 `highlight.mode`（需窗口）与 `lint.product`（**当时代码里它还是 SKIP**；
T91 后 `lint.product` 变成真检查，下一次真机报告应只有 1 个 SKIP）。

## 如何补齐（任何人拿到完整报告后照做）

1. 把用户发回的 `verify-report-<host>-<时间>.txt` **原样**放进本目录
   （建议命名 `02-verify-report-<host>-<yyyyMMdd-HHmmss>.txt`，不要编辑内容）；
2. 把该文件头部的元数据（`tarball sha256` 若报告里没有，就补在 `03-tarball-sha256.txt`）与
   **18 行 `RESULT …`** 摘到 `04-results-table.txt`（`grep '^RESULT' ` 原样输出即可）；
3. 更新本 README：把上表 Actual 列填上，并把"部分归档"改成"完整归档"，写上报告文件名；
4. 如果真机报告与本节转达的摘要不一致，**以报告文件为准**，并在本文件"差异"一节写明。
