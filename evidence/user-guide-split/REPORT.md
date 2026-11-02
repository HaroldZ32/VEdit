generated: 2026-10-31T14:07:58Z
git_head: 19303ae3a58491767d97815d2fe1e0947816e5b6
git_status_porcelain:
 M docs/USER_GUIDE.md
?? evidence/verdicts/search-jump-kte/
---
# USER_GUIDE frontend-split fixes (T75: T74 F-1 / F-2 / F-3)

Pure documentation change in `docs/USER_GUIDE.md`. Code was read first and the
review's claims were confirmed (nothing had to be contradicted):

- GTK3 rejects unknown options before the GUI starts:
  `crates/vd-app/src/cli.rs:101` — `other if other.starts_with('-') => bail!("unknown option '{other}'\n\n{}", usage())`,
  and parsing is the first thing `main` does: `crates/vd-app/src/main.rs:25`.
- Qt6 registers `--project <dir>`: `src/app/main.cpp:804` (`addOption`) and
  `src/app/main.cpp:824` (`options.projectRoot = parser.value("project")`).

## F-1 (Medium) — §9 table no longer says "从打开位置向上"

§9 `工程配置` row now reads: root = first ancestor-from-**start directory** hitting
`vedit.toml` / `.git` / a direct `*.f`, with start = `--project <dir>` (Qt6) or the
process working directory (GTK3). The whole document was re-swept (02) and the
Loader-contract paragraph was clarified so the only contract mention cannot be read
as "the opened file decides the root": it now says the contract exists but neither
frontend passes the opened file's directory in the current wiring.

## F-2 (Medium) — step 3 now forks by artifact before giving a command

```text
修改前（原 §2 步骤 3，整段）:
3. 打开工程（Qt6）：启动时用 `--project <dir>` 指定工程目录（或恢复上次会话）。工程根
   **从该目录向上逐级查找**：在每一级依次看 `vedit.toml`（优先）→ `.git` → 该目录是否
   **直接包含 `*.f`**，命中的第一级即工程根（来源依次报告为 `vedit_toml` / `git_dir` /
   `filelist`，都没命中的退化为起始目录 `fallback`）。

修改后（现 §2 步骤 3，整段）:
3. 打开工程：先确认手上是哪一版——**Qt6 构建** → 启动时用 `--project <dir>` 指定工程目录
   （或恢复上次会话）；**旧 GTK3 产物** → 该版本不认识 `--project`，会以
   `unknown option '--project'` 在 GUI 启动前直接退出（`crates/vd-app/src/cli.rs:101`；
   解析发生在 GUI 之前，`crates/vd-app/src/main.rs:25`），请先 `cd` 到工程目录再启动。
   工程根**从起点目录向上逐级查找**：在每一级依次看 `vedit.toml`（优先）→ `.git` → 该目录是否
   **直接包含 `*.f`**，命中的第一级即工程根（来源依次报告为 `vedit_toml` / `git_dir` /
   `filelist`，都没命中的退化为起始目录 `fallback`）；起点目录的定义见本节末
   （Qt6 = `--project` 目录，旧 GTK3 = 进程启动目录）。
   所以一个只带 `project.f` 的目录本身也算工程根，即使它位于更大的 git 仓库内部：以该目录为
   起点时根就是它自己（`filelist`），而以它所在的外层仓库为起点时根仍是外层（`git_dir`），
   嵌套的 `.f` 不会把根改到子目录；同一目录同时有 `.git` 与 `*.f` 时根不变，来源按更强的
   `.git` 报 `git_dir`。扩展名大小写不敏感：`.F` 与 `.f` 同样算根
   （`has_f_extension` 用 `eq_ignore_ascii_case("f")`）。在此之上的 `.rules.verible_lint` 与
   `*.f` filelist 会被自动发现；
```

The GTK3 paragraph later in §2 (`先 cd 到期望的工程目录再启动`) is unchanged and now
agrees with the fork instead of contradicting the main instruction.

## F-3 (Low) — §2 opens with a version-check line

新加（§2 开头）:

```text
本文同时标注 **Qt6 产品** 与 **旧 GTK3 产物** 的差异：两者的命令行选项与工程根起点不同，
请先确认手上的产物版本，再照下面的步骤操作。
```

## Before / after: §9 row

修改前:
| 工程配置 | `<工程根>/vedit.toml`（工程根 = 从打开位置向上第一个命中 `vedit.toml` / `.git` / 直接含 `*.f` 的目录；同一级优先 `vedit.toml` > `.git` > `*.f`） |

修改后:
| 工程配置 | `<工程根>/vedit.toml`（工程根 = 从起点目录向上第一个命中 `vedit.toml` / `.git` / 直接含 `*.f` 的目录；起点目录：Qt6 = `--project <dir>` 指定的目录，旧 GTK3 = 进程启动目录；同一级优先 `vedit.toml` > `.git` > `*.f`） |

## Before / after: Loader-contract paragraph (consistency with F-1)

修改前:
直接 `model->open(root)`，全仓没有 file-open → project-root 的连线。
`crates/vd-config` 的 `Loader::new(start_dir)` 契约规定**调用方应传被打开文件所在目录**
（`crates/vd-config/src/lib.rs:93-103`），而 Qt6 当前 shell 传的是 `--project` 的值；
GTK3 前端当前传的是进程启动目录（`crates/vd-app/src/config_glue.rs:63`，与契约不一致）。

修改后:
直接 `model->open(root)`，全仓没有 file-open → project-root 的连线。
`crates/vd-config` 的 `Loader::new(start_dir)` 接口约定**调用方应传被打开文件所在目录**
（`crates/vd-config/src/lib.rs:93-103`），但这只是该接口的契约，**当前两个前端都没有按它传**：
Qt6 当前 shell 传的是 `--project` 的值；GTK3 前端传的是进程启动目录
（`crates/vd-app/src/config_glue.rs:63`）。所以实际起点永远是上面的 `--project` 目录 /
进程启动目录，**不是打开的文件**。

## Verification

- `bash build/gates/docs.sh` → `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md`, exit 0 (`03-docs-gate.txt`).
- sweep: `02-grep-sweep.txt`, every line classified; no line implies the opened file
  decides the root.
- Unified diff: `01-diff.txt` (only the four regions above changed — F-1 §9 row and the
  Loader paragraph, F-2 step 3, F-3 §2 intro; no other section was rewritten, and no
  GUI behaviour is claimed).

## Scope

Changed: `docs/USER_GUIDE.md` only (+ this evidence directory). No code, no other
docs, no tests/gates, no `evidence/verdicts/**`. No commit, no push (the Lead commits).
