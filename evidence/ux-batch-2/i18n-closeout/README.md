# i18n close-out (T31) — RM-9's strings + the batch-wide zh_CN sweep

Branch `feat/ux-batch-2`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
Write scope: `translations/vedit_zh_CN.ts` + this evidence directory. No product code was touched.

**Result.** 12 source texts that `lupdate` extracted had no zh_CN entry; all 12 now have one.
6 entries were genuinely obsolete and were removed with a one-line reason each.
2 existing translations were reworded for terminology consistency.
`bash build/gates/lrelease.sh` → **PASS: 1244 translations (1244 finished, 0 unfinished, 0 untranslated)**.
Zero-loss proof against the pre-extraction backup: **ADDED 12, REMOVED 6 (intended), CHANGED 0, LOST 0**.

Every command below was run from the workspace root; the raw output of each step is the numbered
file next to this README.

---

## 1. Backup (00-before.ts)

```console
$ cp translations/vedit_zh_CN.ts evidence/ux-batch-2/i18n-closeout/00-before.ts
$ sha256sum evidence/ux-batch-2/i18n-closeout/00-before.ts
4d7c8552caf8e72e0c21e918342dad47195ecdfafb833de60fd4565d9ed04918  …/00-before.ts
```

State of the backup: **1239 `<message>` elements** (1238 unique `(context, source)` pairs — two
entries share one pair), **0 `type="unfinished"`, 0 `type="vanished"`**, i.e. the catalogue was
green before this pass — it simply did not contain the strings the batch added to `src/`.

## 2. Extraction (01-lupdate.log)

The same procedure the batch used twice before: `lupdate` inside the pinned image, workspace mounted
at `/vedit-build`, absolute locations.

```console
$ docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
    /usr/lib/qt6/bin/lupdate -locations absolute -recursive src -ts translations/vedit_zh_CN.ts
Scanning directory 'src'...
Updating 'translations/vedit_zh_CN.ts'...
    Found 1244 source text(s) (12 new and 1232 already existing)
    Kept 6 obsolete entries
```

Note on the flag: Qt6's own help for `-locations absolute` says "Source file path is relative to
target file", and that is exactly what the catalogue stores (`filename="../src/…​"`, relative to
`translations/`). No absolute host paths leaked into the file (`grep -c 'filename="/'` → 0).

A re-check on a **copy** after the close-out (07-sync-recheck.log) proves the delivered catalogue is
in sync with the current `src/`: `Found 1244 source text(s) (0 new and 1244 already existing)`, and
the copy's non-`<location>` content is byte-identical to the delivered file (only the relative-path
prefix differs, because the copy lived one directory deeper).

## 3. Zero-loss proof (02, 05)

`compare.py` parses both catalogues into `{(context, source): (translation, type)}`, XML-unescapes
both sides, and compares the finished translation of every pair. A missing or altered translation is
a hard failure.

Immediately after extraction — and before anything was translated — the proof was already clean
(**no translation was lost by `lupdate`**):

```text
old messages: 1238  new messages: 1250
old finished+translated: 1238
new finished+translated: 1232
ADDED: 12  REMOVED: 0  CHANGED: 0  LOST: 0
```

Final state, after the 6 intentional removals:

```text
old messages: 1238  new messages: 1244
old finished+translated: 1238
new finished+translated: 1244
ADDED: 12  REMOVED: 6  CHANGED: 0  LOST: 0
```

The 6 `REMOVED` are the entries listed in §5; the script carries them in an explicit
`ALLOWED_REMOVED` set, so a future run cannot use this path to drop a live translation silently.

## 4. The 12 new entries

All in context `VEdit`; all placeholders (`%1`) preserved, order unchanged.

| # | source file:line | English source | zh_CN |
|---|---|---|---|
| 1 | `src/editor/kernel/document_io.cpp:73` | cannot open %1: the editor could not load the file | 无法打开 %1：编辑器无法加载该文件 |
| 2 | `src/workspace/filetree/file_tree_model.cpp:182` | Files imported through a .f filelist; they live outside this folder tree | 通过 .f filelist 导入的文件；它们位于此目录树之外 |
| 3 | `src/workspace/filetree/file_tree_model.cpp:722` | From filelist (%1) | 来自 filelist（%1） |
| 4 | `src/workspace/search/search_jump_target.cpp:102` | absolute path opened as given; files outside the workspace are allowed | 绝对路径按原样打开；工作区之外的文件同样允许 |
| 5 | `src/workspace/search/search_jump_target.cpp:111` | relative path resolved under the workspace root | 相对路径已按工作区根目录解析 |
| 6 | `src/workspace/search/search_jump_target.cpp:114` | relative path, not found under the workspace root | 相对路径，在工作区根目录下未找到 |
| 7 | `src/workspace/search/search_jump_target.cpp:118` | relative path and the workspace root is unknown | 相对路径，且工作区根目录未知 |
| 8 | `src/workspace/module_tree/catalog_client.cpp:436` | the module catalog service did not answer within 300 s | 模块目录服务在 300 秒内没有响应 |
| 9 | `src/common/file_status.h:78` | Has errors | 有错误 |
| 10 | `src/common/file_status.h:80` | Warnings only | 仅有警告 |
| 11 | `src/common/file_status.h:82` | Analysed: no problems found | 已分析：未发现问题 |
| 12 | `src/common/file_status.h:84` | Not analysed yet (queued, or excluded because the buffer has unsaved changes) | 尚未分析（排队中，或因缓冲区有未保存的更改而排除） |

Terminology follows the catalogue's existing choices (`错误` / `警告` / `已分析` as in
`错误：%1 · 警告：%2` and `%1——已分析 %2`), and 「filelist」 stays Latin exactly as in the
user-approved 「来自 filelist」 group label (`evidence/ux-batch-2/REAL-MACHINE-FINDINGS.md:134`).

## 5. Removals — 6 `type="vanished"` entries (04)

Each was checked against the current `src/` before removal; none of the source texts exists anymore.

| context | source | reason |
|---|---|---|
| `VEdit` | KTextEditor::Document::setText() failed for %1 | replaced: `document_io.cpp:134` now uses `KTextEditor::Document::setText() failed` (no `%1`) |
| `VEdit` | No variables are used by this project yet | dead: the empty-table copy was replaced by `Project variables unchanged` |
| `VEdit` | Project variables updated; re-expanding | replaced: `project_panel.cpp:496/501` now say `Project variables unchanged` / `Saved %1 variable(s); re-expanding` |
| `VEdit` | the module catalog service did not answer within 30 s | replaced: the timeout is 300 s (`catalog_client.cpp:436`) |
| `SettingsPanel` | Shortcut | dead: the shortcuts page now uses the `Command / Effective shortcut / Default / State` headers |
| `SettingsPanel` | Clear | replaced: the shortcut buttons are `Reset to Default` / `Reset All to Defaults` |

Two 「文件清单」 renderings were aligned to 「filelist」 in the same write
(`From filelist (%1)`: 来自文件清单（%1）→ 来自 filelist（%1）; `Files imported through a .f
filelist…`: 通过 .f 文件清单导入的文件… → 通过 .f filelist 导入的文件…), so the tree group header
matches the user's own wording.

## 6. Second-writer record (unlogged write to a single-writer file)

**What happened.** At **17:30:11** `translations/vedit_zh_CN.ts` was modified by the **RM-4b lane
(senior-engineer-1)**, which hand-edited the XML by `<source>` text. It did **not** run `lupdate`,
so no `<location>` lines were rewritten and no ordering was touched — my extraction pass is therefore
sound. My own apply script was created at 17:30:13, ran at 17:30:13, and refused with
`12 target(s) did not match exactly once` / `count=0` because the 12 entries had just been filled by
that other writer (03-apply-refused-second-writer.txt). The RM-4b lane self-reported; this is recorded,
not smoothed over.

**Review of those rows (I am the wording owner).** Of RM-4b's own 6 rows: the 4 path-rule strings are
among the 12 in §4 and the 2 `interface_panel.cpp` rows (`%1: the catalog reports no usable position
(file=%2, line=%3, column=%4)…`, `<none>` → `<无>`) were already present and translated in
00-before; all 6 were reviewed — placeholder order correct — and **kept as-is**. Of the 8 cross-lane
drafts it also filled, **2 were reworded** (the two 「文件清单」→「filelist」 alignments above) and
**6 were kept** (`cannot open %1…`, `the module catalog service did not answer within 300 s`,
`Has errors`, `Warnings only`, `Analysed: no problems found`, `Not analysed yet…`).

**Hash ledger** (all from `sha256sum`):

| when | sha256 | note |
|---|---|---|
| 17:28:54 backup | `4d7c8552caf8e72e0c21e918342dad47195ecdfafb833de60fd4565d9ed04918` | 00-before.ts |
| 17:28:57 after my `lupdate` | not captured | overwritten by the 17:30:11 second writer before a hash was taken; that exact state is what 02-zero-loss-proof.txt certifies (17:29:19) |
| 17:30–17:33 (checked twice) | `ff66206cc55ad2de8de85d0c71114a6957c5c013cfc05d22e82ec687cdc51f00` | the state holding RM-4b's 12 hand-filled rows; stable across ≥6 s |
| after my removal/alignment write | `42048f97aa7c5da03fbb9f7b8f33baeb5664b5467510cfd3c3b089233576f1c5` | current; re-checked unchanged after the gate and after the sync re-check |

## 7. Sweep — every batch lane that added user-visible strings

The extraction pass is the mechanical proof: `lupdate` now finds every `tr()` / `translate()`
string in `src/` in the catalogue, and the gate below proves all 1244 are translated. This table
records the per-lane check the row asked for.

| lane | user-visible strings it added | entry exists? | translated? | note |
|---|---|---|---|---|
| rm9-dots | 6: `From filelist (%1)`, `Files imported through a .f filelist…`, `Has errors`, `Warnings only`, `Analysed: no problems found`, `Not analysed yet (…)` | 6/6 | 6/6 | the RM-9 disclosure; 2 reworded for the filelist term (§5) |
| rm5b-shortcuts | 14: `Command`, `Effective shortcut`, `Default`, `State`, `Assign to selected command`, `Reset to Default`, `Reset All to Defaults`, `New shortcut:`, `unbound`, `Shortcut %1 is already used by %2 — nothing was changed.`, `Shortcut for '%1' set to %2…`, `Shortcut for '%1' cleared (default restored).`, `%1 (%2)`, `%1 (the editor's own action)` | 14/14 | 14/14 | e.g. 命令 / 生效快捷键 / 默认 / 状态 / 未绑定 / 恢复默认 / 全部恢复默认 |
| rm2-project-switch | 1: `S&witch Project…` | 1/1 | 1/1 | 切换工程(&W)… (toolbar + File menu) |
| rm3-variables | 11: `Add variable`, `Delete`, `name`, `Enter a variable name`, the name-validation line, `This name appears more than once`, `Removed %1`, `Removed %1 — the project still needs it and will ask again`, the two purpose/rule sentences, `Project variables unchanged` | 11/11 | 11/11 | 添加变量 / 删除 / 名称 / 该名称出现了多次 / 已删除 %1 … |
| rm4-jump | 5: `%1: no jump target is registered (the editor lane is not installed), so nothing was opened`, `%1: the editor rejected the jump to %2 — %3`, `%1: the jump request (%2) was refused — %3`, `no reason was reported`, `setCursorPosition(%1:%2) failed` | 5/5 | 5/5 | placeholders intact, `—— %3` reason suffix kept |
| rm4b-columns | 6: 4 path-rule strings (`search_jump_target.cpp`), `%1: the catalog reports no usable position (file=%2, line=%3, column=%4), so nothing was opened`, `<none>` | 6/6 | 6/6 | hand-added by the lane, reviewed and kept (§6); the 4 path-rule rows are part of the 12 in §4 |
| rm11-hdl-filter | none — predicate/registry lane; the file-dialog filter strings it relies on already existed | n/a | n/a | filter row already translated (`…所有文件 (*)`) |
| rm12-reset-layout | none new — the confirmation dialog reuses the existing text; the menu labels it dispatches (`&Reset Layout`, `&New Empty Layout`, `&Mixed Split (demo)`) already existed | 3/3 existing | 3/3 | 重置布局(&R) / 新建空布局(&N) / 混合分屏(&M)（演示） |
| rm6-gestures | none — input-matrix behaviour only | n/a | n/a | — |
| rm7-disabled-look, rm7b-disabled-followups | none — palette/theme only | n/a | n/a | — |
| rm5-undo | none — document open path only | n/a | n/a | — |
| rm8-manual | none — the manual is Chinese Markdown embedded via `user_manual.qrc`, not a compiled catalogue string | n/a | n/a | — |

**One finding, reported not fixed (outside my write scope):** `src/app/editor_area.cpp:249` —
`activeBadge_->setText(QStringLiteral("active"))` paints a user-visible English word in the pane
header under zh. It is not wrapped in `tr()`/`translate()`, so `lupdate` can never see it. It
predates this batch (introduced by `aaf07de`), and the catalogue has no `active` entry. The lane that
owns `editor_area.cpp` should wrap it; I did not touch product code.

## 8. lrelease gate (06)

```console
$ bash build/gates/lrelease.sh
[lrelease] counts: 1244 translation(s) (1244 finished, 0 unfinished, 0 untranslated)
[lrelease] qm: 132494 bytes sha256=276ccfa2c60850f64fee0af708572fec8c142d5f542de84484d9daf2d863864d
[lrelease] PASS: 0 unfinished and 0 untranslated in …/translations/vedit_zh_CN.ts
```

## 9. Files here

| file | what |
|---|---|
| `00-before.ts` | catalogue backup taken before the extraction pass |
| `01-lupdate.log` | the `lupdate` run and its "Found … (12 new and 1232 already existing)" line |
| `02-zero-loss-proof.txt` | zero-loss proof immediately after extraction (ADDED 12 / LOST 0 / CHANGED 0) |
| `03-apply-refused-second-writer.txt` | my apply script's refusal, which is the second-writer evidence |
| `04-vanished-removed.txt` | the 6 removals with reasons + the 2 term alignments |
| `05-zero-loss-final.txt` | final zero-loss proof (ADDED 12 / REMOVED 6 intended / LOST 0 / CHANGED 0) |
| `06-lrelease-gate.txt` | the gate's full output |
| `07-sync-recheck.log` | `lupdate` on a copy of the delivered file: 0 new, non-location content identical |
| `compare.py`, `apply-translations.py`, `remove-vanished.py` | the three scripts, re-runnable |
