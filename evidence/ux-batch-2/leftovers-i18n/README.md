# T43 — the “active” pane badge + the leftover batch strings (final i18n pass)

Branch `fix/leftovers`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
Write scope: `translations/vedit_zh_CN.ts` + this directory. No product code touched.

**Result.** The `active` badge has its zh_CN entry (`活动`). The extraction pass also found **14 other**
user-visible strings the batch had added with no entry (file-dialog filters, the RM-17 top-scope lines,
the shortcut-owner labels, Next/Previous Tab); all 15 are translated. **3** entries were genuinely
obsolete and removed with reasons. Zero loss: **ADDED 15, REMOVED 3 (intended), CHANGED 0, LOST 0**.
`bash build/gates/lrelease.sh` → **PASS: 1256 translations (1256 finished, 0 unfinished, 0 untranslated)**.

---

## 1. The source string (read from the code, not guessed)

`src/app/editor_area.cpp:253` (the focused-pane badge in the tab row):

```cpp
activeBadge_->setText(QCoreApplication::translate("VEdit", "active"));
```

Context **`VEdit`**, source **`active`**. Before this pass `grep -c '>active<' translations/vedit_zh_CN.ts`
→ **0**, i.e. the badge rendered English under zh.

Wording: the rest of the UI renders “active” as **活动** for exactly this concept
(`no active editor pane` → 没有活动的编辑器窗格, `Save As: the pane has no active document` →
该面板没有活动文档, `Panes: %1 · active: %2` → `窗格：%1 · 活动：%2`) — so the badge is **活动**, not 当前.

## 2. Procedure (same as the two earlier close-outs)

```console
$ cp translations/vedit_zh_CN.ts evidence/ux-batch-2/leftovers-i18n/00-before.ts
$ sha256sum …/00-before.ts
42048f97aa7c5da03fbb9f7b8f33baeb5664b5467510cfd3c3b089233576f1c5
# 1244 <message> elements, 0 unfinished, 0 vanished

$ docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
    /usr/lib/qt6/bin/lupdate -locations absolute -recursive src -ts translations/vedit_zh_CN.ts
Scanning directory 'src'...
Updating 'translations/vedit_zh_CN.ts'...
    Found 1256 source text(s) (15 new and 1241 already existing)
    Kept 3 obsolete entries
    Same-text heuristic provided 1 translation(s)
```

Note: the same-text heuristic reported one copy, but **all 15 new entries were still unfinished** after
the run (03 shows exactly those 15 rewritten) and the zero-loss proof reports **CHANGED 0**, so no
existing translation was altered by it.

## 3. Zero-loss proof (02 and 05)

`compare.py` compares the finished translation of every `(context, source)` pair, XML-unescaped.

Immediately after extraction — no translation lost by `lupdate`:

```text
old messages: 1244  new messages: 1259
old finished+translated: 1244
new finished+translated: 1241        # 1244 − 3 now-vanished
ADDED: 15  REMOVED: 0  CHANGED: 0  LOST: 0
```

After the 3 intentional removals (the script carries them in an explicit `ALLOWED_REMOVED` set):

```text
old messages: 1244  new messages: 1256
old finished+translated: 1244
new finished+translated: 1256
ADDED: 15  REMOVED: 3  CHANGED: 0  LOST: 0
```

## 4. The 15 new entries

| # | context | English source | zh_CN |
|---|---|---|---|
| 1 | `VEdit` | active | 活动 |
| 2 | `VEdit` | Verilog/SystemVerilog/filelist (%1 *.v *.sv *.svh *.vh) | Verilog/SystemVerilog/filelist (%1 *.v *.sv *.svh *.vh) |
| 3 | `VEdit` | Verilog/SystemVerilog (*.v *.sv *.svh *.vh) | Verilog/SystemVerilog (*.v *.sv *.svh *.vh) |
| 4 | `VEdit` | All Files (*) | 所有文件 (\*) |
| 5 | `VEdit` | Verilog filelists (%1) | Verilog filelist (%1) |
| 6 | `VEdit` | All files (*) | 所有文件 (\*) |
| 7 | `VEdit` | %1 file(s) outside the top %2 are not analysed | %1 个文件位于顶层 %2 之外，未被分析 |
| 8 | `VEdit` | · top: %1 | · 顶层：%1 |
| 9 | `VEdit` | Top: | 顶层： |
| 10 | `VEdit` | Auto | 自动 |
| 11 | `VEdit` | Top module for the module tree and the lint scope (empty = Auto) | 模块树与 lint 范围使用的顶层模块（留空 = 自动） |
| 12 | `ApplicationShell` | desktop: %1 | 桌面：%1 |
| 13 | `ApplicationShell` | editor: %1 | 编辑器：%1 |
| 14 | `ApplicationShell` | &Next Tab | 下一个标签页(&N) |
| 15 | `ApplicationShell` | Pre&vious Tab | 上一个标签页(&P) |

Notes on shape:
- **File-dialog filters keep ASCII parentheses.** Both `QFileDialog` and
  `file_dialog_filters.h::filterGlobs()` parse the globs out of the *last* parentheses, so a full-width
  `（）` would break the dialog. The two description-only entries (2, 3) are language-neutral (extension
  set + product names), so their translation is identical by design; the two `All Files/All files`
  entries use 所有文件, matching the existing catalogue wording.
- **Placeholders preserved, order intact** (`%1`, `%2` in 7; `%1` in 2, 5, 8, 12, 13).
- **Terminology**: 顶层 is the catalogue's established word for “top” (`无顶层模块`, `%3 个顶层`,
  `设计顶层`); 自动 is the existing translation of `Auto` (two other contexts); `lint` stays Latin as in
  `工程 lint` / `Lint 任务`.

## 5. Removals — 3 `type="vanished"` entries (04)

Each was checked against the current `src/` before removal.

| context | source | reason |
|---|---|---|
| `VEdit` | Verilog/SystemVerilog/filelist (*.v *.sv *.svh *.vh *.f *.F *.flist);;Verilog/SystemVerilog (*.v *.sv *.svh *.vh);;Filelists (*.f *.F *.flist);;All Files (*) | replaced: File→Open filters now come from `src/common/file_dialog_filters.h` as 4 separate entries (one is the `%1`-globs form above) |
| `VEdit` | Verilog filelists (*.f *.F *.flist);;All files (*) | replaced: the import-filelist dialog now uses `Verilog filelists (%1)` + `All files (*)` |
| `SettingsPanel` | %1 (the editor's own action) | replaced: foreign shortcut owners are labelled by the shell (`desktop: %1` / `editor: %1`); the old suffix is gone from `settings_panel.cpp` |

## 6. Sweep — any other batch string with no entry?

The extraction pass is the mechanical answer: `lupdate` finds every `tr()`/`translate()` string in
`src/`; after translating the 15, the gate reports **0 unfinished and 0 untranslated**, and a re-run of
the extraction on a copy reports **0 new** (§8).

| asked about | result | evidence |
|---|---|---|
| RM-9 states (`From filelist (%1)`, `Has errors`, `Warnings only`, `Analysed: no problems found`, `Not analysed yet…`, the `.f` filelist tree line) | already present and translated (from the 2026-11-06 close-out); 0 removed, 0 changed | `grep -c '<source>…'` = 1 each; 05 shows REMOVED only the 3 intended |
| RM-17 top-scope lines (`Top:`, `Auto` combo entry, the tooltip, ` · top: %1`, `%1 file(s) outside the top %2 are not analysed`) | **were missing** — found by this extraction, now translated (rows 7–11) | 02/03 |
| RM-12 reset-layout prompts | **none new** — the handlers reuse `confirmUnsavedDocuments()` / `promptUnsaved()`, whose strings are already in the catalogue | `src/app/app_shell.cpp:766,795,826,836` call the existing prompt |
| File-dialog filters (`file_dialog_filters.h`, new in this batch) | **were missing** — 5 entries, now translated (rows 2–6) | 02/03 |
| Shortcut-owner labels + Ctrl+Tab commands (`app_shell.cpp:120,133,672,674`) | **were missing** — now translated (rows 12–15) | 02/03 |
| the `active` badge (this row) | **was missing** — now translated (row 1) | 02/03 |

**Nothing else is missing.** `lrelease` reported `Ignored 0 untranslated`; the catalogue has 0
`type="unfinished"` and 0 `type="vanished"` entries.

## 7. Gate (06)

```console
$ bash build/gates/lrelease.sh
    Generated 1256 translation(s) (1256 finished and 0 unfinished)
[lrelease] counts: 1256 translation(s) (1256 finished, 0 unfinished, 0 untranslated)
[lrelease] qm: 133093 bytes sha256=a7cf73a79088aa8fa4be37cc2488497f7c7412a41a0100362520c69ff73e4665
[lrelease] PASS: 0 unfinished and 0 untranslated in …/translations/vedit_zh_CN.ts
```

## 8. Sync re-check (07)

`lupdate` re-run against a **copy** of the delivered catalogue:

```text
Found 1256 source text(s) (0 new and 1256 already existing)
```

and the copy's non-`<location>` content is byte-identical to the delivered file (only the relative
path prefix differs, the copy lived one directory deeper). The catalogue is in sync with `src/` as of
this pass.

## 9. Hash ledger

| when | sha256 | note |
|---|---|---|
| before this pass (00-before.ts) | `42048f97aa7c5da03fbb9f7b8f33baeb5664b5467510cfd3c3b089233576f1c5` | 1244 messages, 0/0 |
| after translating + removing | `5a9b10b68899d7b37232cfd73347a4356ccf9eb461363b99c2f5b7545f854ab3` | 1256 messages, 0 unfinished, 0 vanished |
| after the gate and the sync re-check | `5a9b10b6…` (unchanged) | final |

## 10. Files here

| file | what |
|---|---|
| `00-before.ts` | catalogue backup before this pass |
| `01-lupdate.log` | extraction run (15 new / 1241 existing / 3 obsolete) |
| `02-zero-loss-after-lupdate.txt` | ADDED 15 / LOST 0 / CHANGED 0 straight after extraction |
| `03-translations-applied.txt` | the 15 translations written, 0 unfinished left |
| `04-vanished-removed.txt` | the 3 removals with reasons |
| `05-zero-loss-final.txt` | final proof: ADDED 15 / REMOVED 3 intended / CHANGED 0 / LOST 0 |
| `06-lrelease-gate.txt` | gate output, 1256/0/0 PASS |
| `07-sync-recheck.log` | lupdate on a copy: 0 new, non-location content identical |
| `compare.py`, `apply-translations.py`, `remove-vanished.py` | re-runnable scripts |
