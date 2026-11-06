# 开工基线快照 — cell-20261102T100600176Z

本轮（AC-0.6）开工基线的唯一基准：区分「本轮改动」与「上轮遗留」。

## 采集元数据

| 项 | 值 |
| --- | --- |
| 采集时间（本地 Asia/Shanghai） | `2026-11-02T18:41:50+08:00` |
| 采集时间（UTC） | `2026-11-02T10:41:50Z` |
| `git rev-parse HEAD` | `4e2d0a57ca50db9568db5a5b040cc66f04c9a05f` |
| 交接文档记录的基线 | `4e2d0a5` → **一致** |
| 脏路径总数（逐行计数） | **157** |
| 快照文件 | `evidence/baseline/git-status-porcelain-v1.txt` |
| 快照 sha256 | `19f6f20728c73ea3fafd46a24b6d6584cd3eacec41a5cd14aa3247c0d4a3b5d3` |

## 按顶层目录分布（157 条合计）

| 顶层目录 | 路径数 |
| --- | --- |
| `src` | 68 |
| `evidence` | 47 |
| `crates` | 33 |
| `docs` | 4 |
| `build` | 4 |
| `probes` | 1 |
| **合计** | **157** |

## 按状态码分布

| porcelain 状态 | 数量 | 含义 |
| --- | --- | --- |
| ` M` | 93 | 已跟踪、未暂存的修改 |
| `??` | 64 | 未跟踪 |
| **合计** | **157** | |

无暂存区改动（`M `/`A `/`D ` 均为 0），无删除、无重命名。

## 自引用说明（计数对照）

快照是在 `evidence/baseline/` 目录创建**之前**采集的，因此快照本体 157 行不含自己。本任务落盘后，
工作区实时计数变为 **158**，多出的唯一一条就是本目录自身的未跟踪条目：

```
?? evidence/baseline/
```

复现与对照命令：

```bash
# 快照行数（= 157）
wc -l evidence/baseline/git-status-porcelain-v1.txt

# 实时全量计数（落盘后 = 158 = 157 + 本目录自引用 1）
git status --porcelain=v1 | wc -l

# 与基线计数一致的对照（排除本任务自身写入的目录，= 157）
git status --porcelain=v1 -- . ':(exclude)evidence/baseline' | wc -l

# 该一致对照的输出与快照字节相同
git status --porcelain=v1 -- . ':(exclude)evidence/baseline' \
  | sha256sum   # 19f6f20728c73ea3fafd46a24b6d6584cd3eacec41a5cd14aa3247c0d4a3b5d3
```

## 只读纪律声明

本次采集只执行了两类只读命令：`git status --porcelain=v1` 与 `git rev-parse HEAD`。
未执行 `add` / `commit` / `branch` / `stash` / `checkout` 等任何写 git 操作，未修改任何已跟踪文件；
写入仅限 `evidence/baseline/` 内的这两个文件。
