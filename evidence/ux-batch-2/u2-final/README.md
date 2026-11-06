# U2 tail — 21 vanished catalogue entries + terminology spot-check

Task: T2 (`U2 tail: 21 vanished entries + terminology spot-check`), handover
`docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U2 steps 4 and 5.

Writer: Junior Engineer (`junior-engineer-2`), single writer of
`translations/vedit_zh_CN.ts`.
Write scope actually touched: `translations/vedit_zh_CN.ts` and this directory.

## 0. Result in one line

All 21 `<translation type="vanished">` entries were **removed** (none was an
extraction gap — the lupdate scan set is `src/**` only and no vanished source
string is emitted there any more); `lrelease` is **green with 0 unfinished**;
the terminology spot-check needed **no translation change** and produced two
findings that are outside this task's write scope (a Chinese *source* string on
the hover Fix button, and the fact that `translations/` is untracked in git).

## 1. Method

- No `lupdate` was run (it would re-derive locations and undo the close-out);
  no `<source>` element and no other `<message>` was touched.
- The 21 blocks were removed by one exact match on blocks carrying
  `type="vanished"` (`removed blocks: 21`); the verbatim removed XML is in
  `01-removed-vanished-entries.txt` (+ `.json`), so the deletion is reviewable
  without trusting this page.
- Basis for the keep/remove verdict: the Explorer report forwarded by the Lead
  (lupdate scan set = `src/**`; per-entry reachability with `file:line`). Every
  "reworded -> …" target it names was spot-verified to exist in `src` before
  being recorded here (`module_tree_integration.cpp:414/462/463`,
  `module_tree_panel.cpp:93/97/142/185/274/398`, `catalog_client.cpp:351`,
  `vcs_panel.cpp:1145/2401/2516/2589`, `app_shell.cpp:532`).

## 2. The 21 vanished entries — one line each

Legend: **removed** = dead or replaced; **stale duplicate** = the live text is
still emitted but under another context, whose active entry already carries the
translation (kept untouched).

| # | lupdate line | source (English) | prior translation | verdict | reason |
|---|---|---|---|---|---|
| 1 | 312 | `hierarchy: retrying after the project model…` | 层次：工程模型更新后重试… | removed — reworded | → `module catalog: retrying after the project model…` (`module_tree_integration.cpp:414`, `ModuleTreeHolder::finishRun`) |
| 2 | 313 | `No hierarchy yet: open a project and press Refresh.` | 尚无层次：打开工程并点击“刷新”。 | removed — reworded | → `No module catalog yet: open a project and press Refresh.` (`module_tree_panel.cpp:142`, `module_tree_integration.cpp:462`) |
| 3 | 320 | `Empty state: no elaborated project yet. Open a project and press Refresh.` | 空状态：尚无已展开的工程。打开工程并点击“刷新”。 | removed — reworded | → `Empty state: no module catalog yet. Open a project and press Refresh.` (`module_tree_integration.cpp:463`) |
| 4 | 321 | `%1 · no file:line reported` | %1 · 未报告 file:line | removed — reworded | → `this marker row has no location` (`module_tree_panel.cpp:398`) + `%1 has no definition position in the module catalog, so nothing was opened` |
| 5 | 322 | `Re-elaborate the whole project` | 重新展开整个工程 | removed — reworded | → `Re-run the module catalog for the whole project` (`module_tree_panel.cpp:93`) |
| 6 | 323 | `Jump to source` | 跳转到源 | removed — reworded | → `Double-click: open this module's declaration` (`module_tree_panel.cpp:274`) |
| 7 | 324 | `Put the cursor on a signal in the editor, then jump to the construct that drives it` | 将光标放在编辑器中的信号上，然后跳转到驱动它的构造 | removed — dead | signal-driver jump; net/driver/load rows were removed from the module tree by design (`module_tree_panel.h:7`) |
| 8 | 325 | `Jump to target` | 跳转到目标 | removed — dead | old jump-to-target / driver-row semantics removed; only the generic Ctrl+double-click instantiation jump remains (`interface_panel.cpp:32`) |
| 9 | 326 | `Put the cursor on a signal in the editor, then jump to the construct that reads it` | 将光标放在编辑器中的信号上，然后跳转到读取它的构造 | removed — dead | signal-load jump removed together with nets/drivers/loads |
| 10 | 327 | `Filter instance / net / file…` | 过滤实例 / 网络 / 文件… | removed — reworded | → `Filter modules…` (`module_tree_panel.cpp:97`) |
| 11 | 328 | `Instance / Net` | 实例 / 网络 | removed — reworded | → headers `Module` / `Instantiations` / `Definition` (`module_tree_panel.cpp:105-107`) |
| 12 | 329 | `Kind` | 类型 | removed — dead | no `Kind` column or label anywhere in `src` |
| 13 | 331 | `Hierarchy ready — double-click an instance, a net or a driver/load.` | 层次已就绪——双击实例、网络或驱动/负载。 | removed — reworded | → `Module tree ready — double-click a module to open its declaration.` (`module_tree_panel.cpp:185`) |
| 14 | 365 | `elaborating %1 project file(s)…` | 正在展开 %1 个工程文件… | removed — reworded | → `cataloguing %1 project file(s)…` (`catalog_client.cpp:351`) |
| 15 | 697 | `A branch name is required.` | 必须填写分支名称。 | removed — reworded | → `Invalid branch name: %1; nothing was written.` (`vcs_panel.cpp:2401`) |
| 16 | 701 | `A tag name is required.` | 必须填写标签名称。 | removed — reworded | → `Invalid tag name: %1; nothing was written.` (`vcs_panel.cpp:2516`) |
| 17 | 705 | `A remote name and a URL are required.` | 必须填写远程仓库名称和 URL。 | removed — reworded | → `Invalid remote name: %1; nothing was written.` (`vcs_panel.cpp:2589`) + `Invalid URL: %1; nothing was written.` |
| 18 | 725 | `no tags reported by this backend` | 此后端未报告标签 | removed — reworded | → `No tags` (`vcs_panel.cpp:1145`); the active entry is translated (没有标签) |
| 19 | 868 | `&Settings` | 设置(&S) | removed — dead | the Settings menu was removed: `&Edit` menu label + `&Preferences…` item (`app_shell.cpp:532`) |
| 20 | 882 | `Save As failed: %1` | 另存为失败：%1 | removed — stale duplicate | live text still emitted as `QCoreApplication::translate("VEdit", …)` (`app_shell.cpp:1153`); the **active** `VEdit` entry is translated — the old `ApplicationShell` entry was a duplicate |
| 21 | 885 | `Save As refused (headless): %1 exists and cannot be overwritten without confirmation` | 另存为被拒绝（无界面运行）：%1 已存在，未经确认不能覆盖 | removed — stale duplicate | live text emitted as `QCoreApplication::translate("VEdit", …)` (`app_shell.cpp:1138`); the **active** `VEdit` entry is translated — the old `ApplicationShell` entry was a duplicate |

Nothing was "restored": no vanished source string is emitted by `src` any more,
so restoring one would have created an entry lupdate turns obsolete again on
the next run, and the 2 live texts (rows 20–21) already have their active,
translated entries. The two active `VEdit`-context Save As messages were **not
touched** (verified present and translated after the edit, see §5).

## 3. Terminology spot-check (WORK 2)

| item | source string | file | current translation | verdict |
|---|---|---|---|---|
| replace | `Replace` (FindReplaceBar) | `src/editor/kernel/find_replace.cpp:445` | 替换 | OK |
| replace | `Replace` (SearchPanel) | `src/workspace/search/search_panel.cpp:163` | 替换 | OK |
| replace all | `Replace All` (both) | both | 全部替换 | OK — the user's two buttons 替换 / 全部替换 are distinct and consistent |
| fix | `修复` (the hover Fix button) | `diagnostic_hover.cpp:112` | 修复 | **DEFECT, not fixed here** — see finding F1 |
| interface columns | `Name` / `Direction` / `Type` / `Width` / `Location` | `interface_panel.cpp:85-89` | 名称 / 方向 / 类型 / 位宽 / 位置 | OK — matches the user's 名称/方向/类型/位宽 (Location is the extra column the panel really has) |
| module tree columns | `Module` / `Instantiations` / `Definition` | `module_tree_panel.cpp:105-107` | 模块 / 例化次数 / 定义位置 | OK |
| Interface panel title | `Interface` (context `VEdit`) | `module_tree_integration.cpp:468` | 接口 | OK — this is the panel title the user calls 接口面板 |
| Interface (settings) | `Interface` (context `SettingsPanel`) | `settings_panel.cpp:1774` | 界面 | OK — different source instance, the *appearance group box* in Preferences; 界面 is right there and must not be changed to 接口 |
| three dot states | none | `file_status_delegate.*` | — | no user-visible string: the dot is colour-only (red `#d32f2f` = error, amber `#f9a825` = warnings only, green `#2e7d32` = clean, no dot = never reported). Nothing to translate; the legend belongs to the user manual (U3) |

Conclusion: **no translation body had to change**; the only wrong-looking item
is a *source* string (F1), which this task may not touch.

## 4. Findings not fixed (outside this task's write scope)

- **F1 (real defect, i18n, `src/workspace/diagnostics/diagnostic_hover.cpp:112`)** —
  the hover Fix button is created with
  `QCoreApplication::translate("VEdit", "修复")`: the **source** string is
  already Chinese, and the catalogue duly carries
  `<source>修复</source><translation>修复</translation>`. Consequences: in the
  English UI the button still reads 修复, and there is no English source string
  to keep in step. The fix is a source change (`translate("VEdit", "Fix")` plus
  translating `Fix` → 修复 in the catalogue), i.e. the diagnostics lane /
  a follow-up — a writer of `src/**` and one catalogue line together.
- **F2 (repo hygiene)** — `translations/` is **not tracked by git at all**:
  `git status --porcelain translations/` → `?? translations/`,
  `git ls-tree -r HEAD -- translations/` → empty, `git rev-list --all --objects
  | grep -c translations/` → 0 (no branch ever tracked it). It is not ignored
  either (`git check-ignore` → no match). So the acceptance item "git diff stat
  for `translations/`" cannot exist: against `HEAD` the whole directory is new.
  The captain must stage it explicitly (`git add translations/vedit_zh_CN.ts`;
  `qtbase_zh_CN.qm` is a shipped Qt base translation, a separate decision) —
  otherwise none of this work lands in a commit.
- **F3 (locations, no action now)** — the catalogue's `<location>` filenames are
  **relative** (`../src/…`), i.e. the last regeneration did **not** use
  `-locations absolute`, contrary to the project rule (§3.U2 step 4, which also
  records the earlier loss of 6 translations to relative paths). Follow-up: the next **full** regeneration must
  run `lupdate -locations absolute -recursive src`; not fixed here because
  running lupdate now would re-derive locations and undo the close-out.
- The 84 removed lines are also the reason the file size dropped; nothing else
  drifted (message count 1207 → 1186 = exactly the 21 removed).

## 5. Verification (commands and observed output)

```
$ /usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/vedit_zh_CN.qm
Updating '/tmp/vedit_zh_CN.qm'...
    Generated 1186 translation(s) (1186 finished and 0 unfinished)
exit=0
```

```
$ grep -c 'type="vanished"'  translations/vedit_zh_CN.ts   -> 0
$ grep -c 'type="unfinished"' translations/vedit_zh_CN.ts  -> 0
$ grep -c '<message>'         translations/vedit_zh_CN.ts  -> 1186
$ wc -l                       translations/vedit_zh_CN.ts  -> 6195   (before: 6279 = -84 = 21 blocks x 4 lines)

$ xmllint --noout translations/vedit_zh_CN.ts             -> xml ok (exit 0)
```

Round trip through the compiled catalogue (proves nothing was lost by removing
the duplicates): `lrelease` → `lconvert -i /tmp/v.qm -o /tmp/v.ts` → 1186
messages, and the key rows came back translated:

```
'Save As failed: %1'                -> '另存为失败：%1'
'Save As refused (headless): …'     -> '另存为已被拒绝（无界面）：%1 已存在，未经确认无法覆盖'
'Interface' (VEdit)                 -> '接口'
'Interface' (SettingsPanel)         -> '界面'
'Replace' / 'Replace All'           -> '替换' / '全部替换'
'Module' / 'Instantiations' / 'Definition' -> '模块' / '例化次数' / '定义位置'
'Name' / 'Direction' / 'Width'      -> '名称' / '方向' / '位宽'
```

Diff stat caveat (F2): `git diff --stat translations/` prints **nothing** —
the directory is untracked, so there is no HEAD baseline. The equivalent,
from the numbers above:

```
 translations/vedit_zh_CN.ts | 21 blocks / 84 lines deleted, 0 added
 1207 messages -> 1186 messages; 21 vanished -> 0; 0 unfinished -> 0 unfinished
```

Raw logs: `02-lrelease.txt` (lrelease + counts + xmllint in one run).
Removed blocks: `01-removed-vanished-entries.txt` (verbatim XML),
`01-removed-vanished-entries.json` (same rows, machine-readable).
