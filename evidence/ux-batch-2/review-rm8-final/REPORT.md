# RM-8 / RM-8b final independent review — the rewritten user manual

- Task: T19 (independent verification of the manual rewrite, RM-8 + RM-8b), read-only.
- Reviewer: `reviewer-3` (Reviewer).
- Date: 2026-11-06 (Asia/Shanghai).
- **Verdict: FAIL (request changes). Risk: HIGH.**
- Basis: the T19 card (the seven checks), the writer's evidence
  `evidence/ux-batch-2/rm8-manual/` (README sha256 `90906540…`) and
  `evidence/ux-batch-2/rm8b-bindings/` (README sha256 `134f67fc…`), `docs/USER_GUIDE.md`
  (sha256 `87a9f419…`), the shipped UI (code + `translations/vedit_zh_CN.ts` + the running
  registry/gesture handling) as the authority.
- The docs gate and the Help-rendering checks were run by me (`05-provenance.txt`):
  `bash build/gates/docs.sh` → **PASS**; `resources/help/user_manual.qrc:14` embeds
  `../../docs/USER_GUIDE.md`; `src/app/help_viewer.cpp` is `QTextEdit` + `setMarkdown`; the
  forbidden Markdown constructs count is **0**.

---

## 1. What I read / ran (commands and observed output)

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md   (exit 0)

$ grep -n USER_GUIDE resources/help/user_manual.qrc
14:        <file alias="USER_GUIDE.md">../../docs/USER_GUIDE.md</file>

$ grep -c -E '<[a-zA-Z/][^>]*>|\[\^[^]]*\]|^\s*\[[^]]*\]:' docs/USER_GUIDE.md
0

$ grep -n "不能新增\|不能删除\|只能为" docs/USER_GUIDE.md
142: … 目前只能为已经被 filelist 引用、但还没解析出值的变量填值；它不能新增一个尚未被引用的变量名，也不能删除已有条目（见第 10 节）。
553:8. 变量对话框不能增删：它只能为已被 filelist 引用、但还没解析出值的变量填值，不能新增一个尚未被引用的变量名，也不能删除已有条目。
```

I also re-read the shipped authority for the manual's claims: the command registry defaults
(`src/app/app_shell.cpp`, `diagnostics_integration.cpp`, the RM-5c rehome + extra defaults), the
RM-6 gesture predicate (`multi_cursor.cpp`), the RM-9 status store
(`diagnostics_controller.cpp`, `file_tree_model.cpp`), the variables dialog
(`project_variables_dialog.cpp`: `addVariable`, the per-row `Delete` button) and its E2E
(`project_variables_e2e.cpp`; the T8 review's 36/0 run), the packaging/AGENT facts (the
`vedit-app` binary, the AppImage), and the docs gate source (`build/gates/docs.sh`: the four
required files).

---

## 2. The seven checks

| # | Check | Result |
|---|---|---|
| 1 | Reads as a user manual; installation only as a labelled appendix | **PASS.** The document opens with “VEdit 是…” → the six-region overview → every panel → Help → common actions; the ten user chapters run to §10; deployment is the clearly-labelled **附录 A 产物形态与安装** (“本节是部署说明，不影响日常使用”), then B config / C troubleshooting / D VCS. The reading path is user-first; the build/install material is out of it. |
| 2 | No GTK3-era claim survives; 15+ specific statements verified | **FAIL — one stale claim (H1).** The manual still states the variables dialog **cannot add or delete** (§2.5 line 142 and §10 item 8 line 553), which describes the pre-RM-3 product; the shipped RM-3 dialog has Add/Delete (the `project_variables_dialog.cpp` `addVariable()` and the per-row `Delete` button; the T8 review re-ran `vedit-project-variables-e2e` 36/0). The rest of the sampled statements pass (the 20-row table in §3 below). The stale-vocabulary scan (the writer's and mine) finds zero `vd-app`/GTK/GtkSourceView/WenQuanYi/旧网络-驱动-负载 strings; the legacy artifact is only mentioned in **A.3** as “旧界面 / 不在本文范围” (the labelled appendix). |
| 3 | Every panel/control the manual names exists with that label; every feature it describes exists | **PASS except the variables control (H1).** The §1.2 panel table (`Files`/`Project`/`Source Control`/`Search`/`Terminal`/`Module Tree`/`Interface`/`Outline`) matches the shipped `addPanel` set; the §8.3 command table matches the current registry (45 commands, 34 primary + 2 extra + 11 unbound); the §6.5 five dot states match the RM-9 store; the §7.2 gestures match the RM-6 predicate. The **variables dialog** exists with Add/Delete, but the manual says it cannot — the manual is describing a feature that no longer matches the shipped control (the user's contract is wrong, not a promised feature missing). |
| 4 | Shortcut/gesture tables match the shipped registry/gesture handling (RM-5b/RM-6), including primary/secondary Alt + the WM note and explicit unbound rows | **PASS.** §8.3: four columns, `Ctrl+Shift+Z / Ctrl+Y` multi-binding, `(unbound)`/`(none)`/`default`/`custom`/`unbound`, the 11 “无默认键位” rows (Save Session Now, Module Tree/Interface/Project panel toggles, tree-sitter, Lint/Reparse, hover/capabilities/reload, About), the `Ctrl+Alt+T`/`Ctrl+Alt+D` desktop-global rehome explanation, the editor-copy-removed note (Ctrl+Z/Ctrl+Shift+Z/Ctrl+S/Ctrl+Shift+S/Ctrl+P); §7.2: `Shift+Alt+拖拽` / `Ctrl+Alt+点击` primary, Alt secondary, the matching rule, the `Meta+Alt` exception, the WM reason; §7.3 marks the editor's own (non-registry) keys. The writer's `verify_table.py`/`verify_kte.py` and the RM-5c green `BINDING` snapshot support it; the RM-6 review verified the predicate. |
| 5 | The known-limitations section carries the batch's honest bounds | **FAIL — three bounds missing (M1).** §10 carries the cross-file rename (1), RE2 vs PCRE (2), the replace bound (3), VCS local-only (4), the module-tree limits (5), the Alt WM bound (6), the 1.60× CJK (7), the AppImage language (9) and the syntax-definition (10). It does **not** carry: the **no-real-machine-GUI claim** (the panel/UI descriptions are code+translation-derived, not a real click-through — the writer's own “未能证明 1”); the **FIM Rust-side Chinese strings** bound; and the **shadowed-definition disclosure**. The **variables item 8 is also stale** (H1), which makes §10 actively wrong, not just incomplete. |
| 6 | Renders as the built-in Help; same qrc path; no unrenderable Markdown | **PASS.** The qrc still embeds `../../docs/USER_GUIDE.md` (the same file); `help_viewer.cpp` reads `:/help/USER_GUIDE.md` with `QTextEdit` + `setMarkdown`; the manual has no HTML blocks, no `[^…]` footnotes and no link-reference definitions (my grep 0); the tables and `sh` fences are the constructs already rendered by the Qt Markdown viewer (the writer's `04-markdown-shape.log`). |
| 7 | docs gate PASS, quoted by the reviewer | **PASS.** `bash build/gates/docs.sh` → `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md`, exit 0 (`05-provenance.txt`). |

---

## 3. The claim checks (twenty sampled statements, each against the shipped authority)

| # | manual statement (section) | verdict | shipped evidence |
|---|---|---|---|
| 1 | Five fixed menus File/Edit/View/Project/Help (§1.1) | PASS | `ApplicationShell` `setMenuLabel` ×5 (fixed order comment) |
| 2 | Toolbar order New…Open Folder, Switch Project, Save, Save All, Undo, Redo / Search in Files, Lint Project, Format Document (§1.1) | PASS | `kToolBarOrder` in `app_shell.cpp` |
| 3 | Six window regions and the status-bar left/right contents (§1.1) | PASS | `buildUi`/the docks/`updateStatusBar` |
| 4 | `Files` tree: double-click opens; toolbar new/rename/delete/refresh; status dot between icon and name (§1.2) | PASS | `FileTreeWidget`/`FileStatusDotDelegate` |
| 5 | `Project` panel: filelist, expanded files, -I, -D, variables, diagnostics (§1.2) | PASS | `ProjectPanel` (`filelists_`, `fileList_`, include/defines, `status_`) |
| 6 | `Module Tree` / `Interface` names and roles (§1.2) | PASS | `module_tree_integration` `addPanel("Module Tree"/"Interface")` |
| 7 | `Outline` = symbol outline + diagnostics list (§1.2) | PASS | `diagnostics_panel` (`view_`, `issues_`) |
| 8 | F1 / Help → User Manual opens a read-only window with this manual (§1.3) | PASS | `help_viewer.cpp` (`QTextEdit`, `setMarkdown`), the qrc |
| 9 | `Ctrl+O`, `Ctrl+F`, `Ctrl+Alt+S`, `Ctrl+Alt+Shift+D`, F12/Shift+F12/F2 (§1.4) | PASS | the current registry (RM-5c rehome; the diagnostics lane) |
| 10 | `.f` variable expansion `$NAME`/`${}`/`$()`/`$$`/`~`, project variable table > env (§2.5) | PASS | the `vd-config` filelist parser; the shipped tests |
| 11 | **“Variables dialog currently can only fill known/missing names; it cannot add a new name, nor delete an entry” (§2.5, §10.8)** | **FAIL (stale)** | the RM-3 shipped Add/Delete: `project_variables_dialog.cpp` `addVariable()` + the per-row `Delete`; `vedit-project-variables-e2e` 36/0 (T8 rerun) |
| 12 | Undo/Redo are `Ctrl+Z` / `Ctrl+Shift+Z` shell commands; the editor's own copies removed (§3.2) | PASS | `app_shell` commands + `EditorArea::clearOwnedShortcuts` |
| 13 | Split horizontally `Ctrl+Alt+H`; layouts ask before discarding unsaved docs (§3.1) | PASS | RM-5c default; the RM-12 `confirmUnsavedDocuments` handlers |
| 14 | Current-file find = PCRE; project search = RE2 (no backrefs/lookaround) (§4.4, §10.2) | PASS | the Qt regex / the Rust RE2-backed search |
| 15 | Module tree/interface: top roots, ports, depth 64 / 20000 rows (§5, §10.5) | PASS | `module_tree/README.md` limits; the UI |
| 16 | Open/switch project runs one full analysis; save is incremental; verdicts retained per file; `From filelist (N)` dots (§6.2/§6.5) | PASS | the RM-9 store/`statusValueForFile`; the e2e (the T21 review) |
| 17 | Navigation F12/Shift+F12/F2/Ctrl+Shift+F; cross-file rename not promised (§7.1, §10.1) | PASS | the diagnostics lane actions; the lsp_actions defaults |
| 18 | `Shift+Alt+拖拽`/`Ctrl+Alt+点击` primaries, Alt secondaries, Meta+Alt excluded, WM reason (§7.2, §10.6) | PASS | RM-6 `isMultiCursorGesture()` (T14 review) |
| 19 | Settings pages (`Shortcuts`/`Tools`/`FIM`/`Appearance`/`Lint`/`Format`), theme/dots/lint options (§8) | PASS | `SettingsPanel` pages; the RM-7/RM-5b/UI |
| 20 | Single-file AppImage, `vedit-app` build product, `--state-dir`/config paths, VCS bundle/allow-list (§A, §B, §D) | PASS | the packaging/`AGENT.md` VCS facts; `main.cpp` options |

The stale item 11 is the one FAIL; every other sampled claim matches the shipped code/catalogue.
The `vd-app`/GTK3 vocabulary and the “网络/驱动/负载” rows are **absent** (the stale scan 0);
the legacy artifact is only in **A.3**, a labelled appendix line that says the old packaging is
out of scope — allowed by check 1.

---

## 4. Findings (ranked)

### H1 — HIGH — the manual still says the variables dialog cannot add/delete (it can, since RM-3)

**What.** §2.5 and §10 item 8 say the `Filelist variables` dialog “只能为已经被 filelist 引用、
但还没解析出值的变量填值；不能新增一个尚未被引用的变量名，也不能删除已有条目”. The shipped
RM-3 dialog has **Add variable** and a per-row **Delete**, inline validation, the required-name
re-offer and per-root persistence; the T8 review re-ran `vedit-project-variables-e2e` 36/0 and the
panel selftest 153/0. The user's own RM-3 complaint was exactly that add/delete was missing; the
manual now tells them it is still missing after it was fixed.
**Where.** `docs/USER_GUIDE.md` §2.5 (the paragraph after the variable table) and §10 item 8;
`src/workspace/project/project_variables_dialog.cpp` (`addVariable`, the row `Delete` button);
`evidence/ux-batch-2/rm3-variables/README.md`; the T8 review.
**Why it matters.** The manual is the user's contract; a stale limitation on a feature the user
asked for is worse than an omission — it hides the fix. It is also a feature description the
shipped control contradicts (check 3), and it survived both the RM-8 rewrite and the RM-8b
finalisation.
**Direction (described, not applied).** Replace §2.5's paragraph and §10 item 8 with the shipped
contract: Add appends an editable name/value row, Delete removes it, inline validation refuses
empty/malformed/duplicate names, a name a filelist still references becomes unresolved again and
the dialog re-offers it, and the values return per root after A→B→A (the RM-3 README §2/§3).

### M1 — MEDIUM — three of the batch's known-limitation bounds are missing from §10

**What.** §10 has ten items but not: (a) the **no-real-machine-GUI claim** (the UI/panel
descriptions come from code + the packaged translation catalogue, not a real click-through — the
writer's own “未能证明 1”); (b) the **FIM Rust-side Chinese strings** bound; (c) the
**shadowed-definition disclosure**. The T19 card lists these among the bounds the known-limitation
section must carry.
**Where.** `docs/USER_GUIDE.md` §10; the writer's `rm8-manual` README “未能证明”; §8.5 (FIM) and
§7.1 (definition) for the two feature contexts.
**Why it matters.** The user must not read the manual as a real-machine GUI proof; the FIM's
Chinese-side ghost strings and the shadowed-definition edge are user-visible caveats the batch
promised to disclose.
**Direction.** Add three §10 items (the GUI-claim bound, the FIM Chinese-string bound, the
shadowed-definition disclosure) using the same wording the batch's evidence uses.

### M2 — LOW — the stale variables claim appears twice (§2.5 and §10.8)

**What.** H1's sentence is duplicated in the chapter and the limitation list; a fix must update both
or the manual will still contradict itself.
**Where.** `docs/USER_GUIDE.md` §2.5 line 142; §10 item 8 line 553.

### N1 — NIT — the writer's evidence sizes/line numbers lag the current file

**What.** The RM-8 README says 604 lines / 42,837 bytes; the current manual is 664 lines (the RM-8b
and later lane edits); the RM-8b README's 17:42 state is the newer. The current file is the manual;
the writer's counts are the snapshot. The docs gate and the qrc path are current.
**Where.** `evidence/ux-batch-2/rm8-manual/README.md` §3; `rm8b-bindings/README.md` §6.

---

## 5. Declined to judge (reason on the same line)

- **A real desktop click-through of every panel** — the manual is a document; the shipped authority is the code + translation catalogue; the writer states the bound (and it must be in §10, M1).
- **The AppImage's actual Chinese input behaviour** — the manual §9.2 states it depends on the system/IME and the AppImage's narrowed QT_PLUGIN_PATH; the writer's measurement; not re-tested here.
- **Rebuilding the AppImage** — the qrc path is unchanged; the writer/captain own the packaging; the manual text is current.
- **The unrelated RM-12 `Reset Layout`/`New Empty Layout` data-loss finding** — the writer's §7.2/§3.1 warning is in the manual; the fix belongs to its own lane; noted, not judged here.
- **The full workspace gate** — the captain; I ran the docs gate.
- **All 664 lines' prose** — I sampled 20 specific statements (the card asks at least 15) plus the §10 list, §8.3 table, §7.2/§6.5/§8.5; a full proofread is not a code claim.
- **The RM-10 hover (T23)** — a separate review; not merged here.
- **The writer's `verify_table.py`/`verify_kte.py` arithmetic beyond the sampled rows** — the table matches the current registry at the sampled rows; the scripts are reproducible.

---

## 6. Bounds of this review (what only a real desktop adds)

- The manual review is by **reading** the document and checking each claim against the shipped
  code/catalogue and the writers' evidence; **no real GUI click-through** of every panel (the
  writer's bound, now a required §10 item — M1).
- The **Help render** is checked at the qrc path + the `QTextEdit`/`setMarkdown` code + the
  forbidden-construct grep; I did not rebuild the AppImage and open the Help window on a real
  desktop (the writer's bound; the path is unchanged).
- The **shortcut/gesture tables** are checked at the sampled rows against the current registry and
  the RM-5c/RM-6 reports; the writer's `verify_table.py`/`verify_kte.py` cover the whole table; the
  full RM-5b/RM-6 real-key runs are the reviewers' separate evidence.
- The **AppImage/Chinese input** behaviour is the writer's disclosed bound.
- I did not edit any product file; the only writes are this report and `05-provenance.txt` under
  `evidence/ux-batch-2/review-rm8-final/`.

---

## 7. Verdict

**FAIL — request changes. Risk HIGH.**

The rewrite is a real user manual: it opens on “what is this / what do I see / how do I do X”, the
install/build material is a labelled appendix, the GTK3-era vocabulary is gone (the legacy artifact
only in A.3), the panel/command/status-dot/gesture tables match the shipped UI at the sampled
claims, the Help path/qrc/Markdown constructs are clean and the docs gate passes. But it fails the
user's contract on one shipped feature: the manual still says the variables dialog **cannot add or
delete** (§2.5 and §10.8), while RM-3 shipped exactly that (the dialog's Add/Delete, the 36/0 E2E,
the T8 re-run, the per-root restore). That is a **High** stale claim the RM-8b finalisation should
have caught, and §10 is also missing three of the batch's honest bounds (the no-real-machine-GUI
claim, the FIM Rust-side Chinese strings, the shadowed-definition disclosure). Fix the two variable
paragraphs to the shipped contract and add the three §10 items; the rest of the manual passes.
