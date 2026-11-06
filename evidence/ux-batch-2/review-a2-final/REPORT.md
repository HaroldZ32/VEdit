# A2 independent review — top bar / menus / i18n / built-in manual (T29)

Reviewer: **Reviewer C** (read-only lane; this review edited no file under `src/**`,
`resources/**`, `translations/**` or `docs/**` — it wrote only this report and
`lrelease-check.log` in this directory).
Frozen contract: `.mpd/plans/ux-batch-2/00-plan.md` §0 (user decisions) and §5.1
(translation integration), read together with
`docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §1 (requirement matrix) and
§3.U2/U3. The requirement numbers 3 / 7 / 11 used here are the **handover §1
matrix** numbering (the plan's §0 is a decision table of 8 decisions + 附 and has
no item 11 — noted so the acceptance matrix cites the right index).
Scope: A2's user-facing claims only — requirement 3 (right-click menu + top bar
fully translated), requirement 7 (menu re-order + main toolbar + Preferences in
Edit), requirement 11 (a real manual in Help / F1).
Task: T29. This is the independent A2 pass the whole-batch review asked for
(`evidence/ux-batch-2/review-batch-acceptance/REPORT.md` §"Independent review
coverage is also incomplete", lines 49–50, 120–126).

**Verdict: PASS — approve (A2 lane).**
**Risk level: Medium** — all three requirements hold in the current tree by code,
catalogue and recorded checks, but two Medium findings concern **acceptance-evidence
currency** (the A2 numbers describe a pre-U2/U3 tree) and a **product-level
selfcheck gap** (the real binary's selfcheck does not pin menu order, the Project
menu, or Preferences placement). Neither is a shipped-code defect; both belong to
U8 / the final acceptance matrix, not to an A2 repair loop.
**One High is carried, not owned:** the default `Ctrl+Alt+F` collision between the
Files panel toggle and Format Document (H1 below) was found by the U3 review and is
already assigned to the board repair T34/T35; it is carried here because it is
visible on exactly this menu surface and because item 11's manual currently
misstates that key. The A2 lane's code is not the cause and this report does not
send A2 back for repair.

## 0. Assessment (one paragraph)

The A2 lane's three surfaced requirements are genuinely delivered in the tree I
read — not just claimed: the top-level order is a fixed `kMenuOrder` list that is
re-materialised after late lanes register (`ApplicationShell::buildMenuBar`,
`src/app/app_shell.cpp:757-799`); `settings.preferences` is registered with
`menu == "edit"` and `order == 100` (`app_shell.cpp:530-533`); the main toolbar is
built from the command registry's own `QAction` pointers in the order the user
asked for, movable/floatable and hideable via `view.toolbar`
(`app_shell.cpp:806-858`); undo/redo do not stop at the shell — they walk the
active editor for KTextEditor's own `edit_undo`/`edit_redo` actions and trigger
them (`EditorPane::triggerEditorAction` → `findEditorAction`,
`src/app/editor_area.cpp:156-186, 832-856`), and the recorded selftest exercises a
real KTextEditor document (`a2.undo.real-*` / `a2.redo.real-*`, log lines 86–90).
Every string the user named is now `tr()`/`translate()`-wrapped and has a Chinese
row in the current catalogue: the five menu labels, the five panel toggles, the
seven editor context-menu labels (wired into the real context menu via
`editor_context_menu.cpp:78-84`), and the diagnostics command texts; `lrelease` on
the current `translations/vedit_zh_CN.ts` is green at **1186 finished / 0
unfinished** (I ran it, §2). The manual is the qrc-embedded `docs/USER_GUIDE.md`
rendered with `QTextEdit::setMarkdown`, reachable as Help → User Manual and F1, and
the terminal claims plain-F1 `ShortcutOverride` so the key reaches the PTY instead
of the manual. What keeps this at Medium rather than Low is evidence hygiene: the
A2 evidence was frozen before the catalogue was finished (U2) and before the manual
was expanded (U3), so several A2 numbers and screenshots no longer describe the
current tree, and the product binary's selfcheck does not independently assert the
menu structure it claims to gate. One user-visible High also sits on this surface —
`Ctrl+Alt+F` is the default of both the Files panel toggle and Format Document —
but it predates A2, is already FAILed by the U3 review and assigned to T34/T35, so
it is carried as H1 below rather than repaired here.

## 1. What I read and ran (evidence base)

Read (documentation band): `.mpd/plans/ux-batch-2/00-plan.md`,
`docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §1/§3.U2/U3,
`evidence/ux-batch-2/a2/README.md` and every file in `evidence/ux-batch-2/a2/logs/`
and `evidence/ux-batch-2/a2/screenshots/` (the three PNGs were opened with the
image reader), plus the whole-batch reviews for the gap statement only
(`review-batch-acceptance/REPORT.md`, `review-batch-acceptance-r2/CHECKLIST.md`)
and `review-u2-final/REPORT.md` + `review-u2b-final-lrelease.log` as corroboration
for the current catalogue.

Read (implementation, because the assignment names these paths): current
`src/app/app_shell.cpp` / `.h`, `src/app/help_viewer.cpp` / `.h`,
`resources/help/user_manual.qrc`, `src/app/ux_selftest.cpp`,
`src/editor/kernel/lsp_actions.cpp`, `src/editor/kernel/editor_context_menu.cpp`,
`src/terminal/terminal_widget.cpp`, `src/workspace/diagnostics/diagnostics_integration.cpp`,
`src/app/editor_area.cpp` (action walk), `src/app/main.cpp` (selfcheck menu block),
`translations/vedit_zh_CN.ts`, `docs/USER_GUIDE.md`.

Ran:

```sh
/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/a2-final-check.qm
#   Generated 1186 translation(s) (1186 finished and 0 unfinished)   exit=0
grep -c '<message>'      translations/vedit_zh_CN.ts   # 1186
grep -c 'type="unfinished"' / 'type="vanished"' / 'type="obsolete"'  # 0 / 0 / 0
sha256sum translations/vedit_zh_CN.ts                  # 55261d0136028f… (10:38)
```

(the catalogue was being edited by the U2b lane during this review — an earlier
read in the same session hashed `c3fc14a9…`; both reads compiled green at 1186/0.
`lrelease-check.log` is the recorded run.)

```sh
# existing A2 selftest binary, host, offscreen (no rebuild — see Bounds)
LD_LIBRARY_PATH=<docker-image libdir for libtree-sitter.so.0.25> \
QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=/tmp/xdg-ux \
  target/qt6-build/src/app/vedit-app-ux-selftest
```

Observed: `a2.menu.*`, `a2.toolbar.*`, `a2.help.*` and `a2.f1.*` checks PASS;
`SKIP-CHECK a2.i18n.zh no /vedit-build/target/qt6-build/src/app/translations/vedit_zh_CN.qm`
(the binary was compiled with the container path, so the i18n leg cannot run
against the host build tree); the process reached the F1/manual checks and then
timed out (exit 124) in the real-KTextEditor undo segment. This is a partial host
reproduction, not a clean full-suite run; the recorded 53-check log is
`evidence/ux-batch-2/a2/logs/31-ux-selftest.log` and the all-ctest run is
`32-ctest-all.log` (42/42 at A2 time).

## 2. Requirement-by-requirement verification

### Requirement 3 — right-click menu and top bar fully translated: **PASS**

* Five top-level labels are `tr()`-wrapped and translated (`app_shell.cpp:515-519`;
  catalogue context `vedit::app::ApplicationShell`, `.ts:4445-4866`):
  `&File` → 文件(&F) (4448-4449), `&Edit` → 编辑(&E) (4638-4639),
  `&View` → 视图(&V) (4453-4454), `&Project` → 项目(&P) (4643-4644),
  `&Help` → 帮助(&H) (4458-4459). `&Preferences…` → 首选项(&P)… (4463-4464),
  `&User Manual` → 用户手册(&U) (4703-4704).
* Five panel toggles are constructed with `tr()` in the initialiser
  (`app_shell.cpp:594-606`) — every title is extracted, which is the exact root
  cause the plan's F5 recorded — and translated (`.ts:4678-4699`):
  To&ggle Files Panel → 切换文件面板(&G), Search → 切换搜索面板(&G), Terminal →
  切换终端面板(&G), Source Control → 切换源代码管理面板(&G), Outline → 切换大纲面板(&G).
* Seven editor right-click labels go through
  `QCoreApplication::translate("VEdit", …)` (`lsp_actions.cpp:337-361`), and the
  real context menu uses them (`editor_context_menu.cpp:78-84`: the wired shell
  action's text when available, else `editorActionLabel(action)`). Catalogue
  (context `VEdit`): `Go to &Definition` → 跳转到定义(&D) (4133-4134),
  `Find &References` → 查找引用(&R) (2141-2142), `&Rename Symbol...` → 重命名符号(&R)…
  (2147-2148), `&Format Document` → 格式化文档(&F) (2153-2154), `Show &Hover` →
  显示悬停(&H) (2159-2160), `&Next Diagnostic` → 下一个诊断(&N) (2164-2165),
  `&Previous Diagnostic` → 上一个诊断(&P) (2169-2170).
* Diagnostics command texts are `translate()`-wrapped
  (`diagnostics_integration.cpp:2353-2386`) and translated: `&Lint Project` →
  检查工程(&L) (2075-2076), `Show Backend &Capabilities` → 显示后端能力(&C)
  (2080-2081), `Reload Backend Settings` → 重新加载后端设置 (2085-2086),
  `&Diagnostics` → 诊断(&D) (2090-2091), `Re&parse Project` → 重新解析工程(&P)
  (2095-2096).
* Recorded check: `a2.i18n.qm-loaded PASS`, `a2.i18n.menu-count PASS
  文件(&F),编辑(&E),视图(&V),项目(&P),帮助(&H)`, `a2.i18n.preferences PASS 首选项(&P)…`,
  `a2.i18n.panel-toggle PASS 切换文件面板(&G)`, `a2.i18n.context-menu-label PASS
  跳转到定义(&D)` (`31-ux-selftest.log:60-69`); the zh screenshot
  (`12-main-menu-zh.png`) shows all five menus, all toolbar buttons and the
  Preferences item in Chinese.
* No `settings` menu survives: `setMenuLabel` is called only for the five
  (`app_shell.cpp:515-519`), no command registers `menu == "settings"`
  (`grep` = 0 hits), and the selftest asserts `a2.menu.no-settings`.

### Requirement 7 — menu order, main toolbar, Preferences in Edit: **PASS**

* Fixed order: `kMenuOrder{"file","edit","view","project","help"}`
  (`app_shell.cpp:764-778`); `buildMenuBar()` materialises in that order and skips
  empty menus (757-799). It is called in the constructor **and again from
  `startSession()`** (227-235) specifically because the diagnostics lane registers
  the Project menu after the constructor — `main.cpp` installs the workspace panels
  before `shell.startSession()`.
* Preferences is under Edit, last: command `settings.preferences`,
  `menu = "edit"`, `order = 100` (`app_shell.cpp:530-533`); asserted by the selftest
  as `a2.menu.preferences-in-edit` and `a2.menu.preferences-last-in-edit`
  (`ux_selftest.cpp:200-215`, log lines 5-6).
* Toolbar: `buildToolBar()` (`app_shell.cpp:806-858`) creates
  `vedit-main-toolbar`, movable/floatable, addable to all areas, text-only, and
  fills it from `kToolBarOrder` = New, Open, Open Folder, Save, Save All, Undo,
  Redo | separator | Search in Files, Lint Project, Format Document — each entry is
  the registry's own `QAction*` (asserted pointer-equal by
  `a2.toolbar.shares-registry-actions`), so the button and the menu item cannot
  diverge. Hide/show is asserted (`a2.toolbar.hide-and-show`, log 15), and the View
  toggle is kept in sync by `visibilityChanged` (823-829).
* Undo/redo are real: `edit.undo`/`edit.redo` handlers call
  `EditorArea::triggerActiveEditorAction({edit_undo|undo}, …)`
  (`app_shell.cpp:715-726`); `EditorPane::triggerEditorAction` walks the editor
  widget, its child widgets' actions and its `findChildren<QAction*>()`
  (`editor_area.cpp:156-186`), refuses a disabled action, and falls back to the
  provider's own `undo()`/`redo()` slot only when no named action exists
  (832-856). Availability comes from the action's `isEnabled()` and is re-synced on
  every editor state change (`syncEditorCommandStates`, 860-873, wired at 158-164).
  The recorded checks include a real KTextEditor view:
  `a2.undo.real-ktexteditor-view`, `a2.undo.real-enabled-after-edit`,
  `a2.undo.real-changes-document`, `a2.redo.real-enabled-after-undo`,
  `a2.redo.real-restores-document` (log 86-90), plus the demo-provider pass
  (lines 21-36).
* Visual corroboration: `10-main-toolbar-menu.png` shows exactly
  File/Edit/View/Project/Help and the toolbar sequence above.

### Requirement 11 — a real manual in Help (F1): **PASS**

* `resources/help/user_manual.qrc` embeds `../../docs/USER_GUIDE.md` as
  `:/help/USER_GUIDE.md` (qrc lines 13-15); `HelpViewer::manualMarkdown()` opens the
  resource and `text_->setMarkdown(...)` renders it (`help_viewer.cpp:24, 43,
  52-60`); `Q_INIT_RESOURCE(user_manual)` is emitted at global scope for the static
  library (14-17), the exact linkage trap the header documents.
* Entry point: `help.user-manual`, text `tr("&User Manual")`, menu `help`,
  `QKeySequence(Qt::Key_F1)` (`app_shell.cpp:636-637`), handler
  `openUserManual()` (728, 895-905), non-modal viewer with `WA_DeleteOnClose`
  (`help_viewer.cpp:74-80`). `manualTitle()` reads the first `# ` line — the
  current manual starts with `# VEdit 用户指南` (`docs/USER_GUIDE.md:1`).
* Recorded checks: `a2.help.manual-resource-not-empty`, `manual-has-title PASS
  VEdit 用户指南`, `command-exists`, `command-shortcut`, `viewer-opens`,
  `viewer-renders-title` (log 53-59); `11-user-manual.png` shows the rendered
  Chinese manual.
* Terminal direction: plain F1 is claimed via `ShortcutOverride` so the window-level
  F1 shortcut does not fire (`terminal_widget.cpp:526-541`), and the key maps to
  `\x1bOP` (`functionKeySequence`, 50-53) which `keyPressEvent` writes to the PTY.
  Recorded checks: `a2.f1.terminal-claims-shortcut-override`,
  `a2.f1.terminal-f1-reaches-pty` (PTY screen contains `^[OP`), `…-does-not-open-manual`,
  and `a2.f1.manual-opens-outside-terminal` (log 74-83). **Reproducible from the
  evidence alone?** Yes at the recorded level (log lines + test source at
  `ux_selftest.cpp:447-520` show both directions in one test), with the strength
  caveat in finding N2.

## 3. Findings (ranked)

### H1 — High (carried; not A2's to fix) — default `Ctrl+Alt+F` is registered twice on this menu surface

**What/where:** `view.panel.file-tree` defaults to `Ctrl+Alt+F`
(`src/app/app_shell.cpp:601`, the `panelCommands` table), and
`diagnostics.format` defaults to the same `Ctrl+Alt+F`
(`src/workspace/diagnostics/diagnostics_integration.cpp:2368-2371`). Both are
`QAction`s created by `CommandRegistry::add` with `action->setShortcut(...)`
(`src/app/command_registry.cpp`, `add()`), both live on the shell window, and
`add()` has **no duplicate-shortcut detection** (grep for duplicate/ambiguous/
conflict there is empty). Qt therefore sees two enabled actions with one shortcut
on one window: an ambiguous-shortcut overload, so the key is not reliably
delivered to either action.

**Why it matters (reasonable-user standard):** a user reading the View menu sees
`Ctrl+Alt+F` next to both "Toggle Files Panel" and "Format Document"; pressing it
may toggle the panel when they meant to format (the U3 review's exact failure
scenario). Requirement 7 is about a coherent menu/toolbar surface, and requirement
11's manual currently instructs `Ctrl+Shift+F` for formatting — also false on the
Qt6 product — so this is the one High that touches both items 7 and 11.

**Provenance and ownership:** this is **not introduced by A2** (the panel shortcut
predates the lane; the Format command belongs to diagnostics). It was independently
found and raised by `evidence/ux-batch-2/review-u3-final/REPORT.md` **U3-F1
(High)** — that review's verdict is FAIL (request changes) — and the board already
carries the repair as **T34** ("U3 repair: shortcut collision + wrong keyboard
table after U6") with review **T35**. This A2 report carries it so the acceptance
matrix does not sign item 7/11 off as clean while it is open; it does not ask A2
to repair it.

**Direction of fix (described only):** resolve the duplicate at the product level
(give `diagnostics.format` the old `Ctrl+Shift+F` default, or move the file-tree
toggle) and add a duplicate-shortcut guard/ctest on the default table so this
cannot recur; then correct `docs/USER_GUIDE.md` §15.2 (and label §3's table as the
old GTK3 bindings). This is T34's scope, not A2's.

### M1 — Medium — A2's quantitative evidence no longer describes the current tree

**What/where:**
- `evidence/ux-batch-2/a2/README.md` §3 records the `.ts` as **1025 messages /
  1018 finished / 7 vanished** (`logs/11-lrelease-final.log`). The current
  catalogue is **1186 messages / 0 unfinished / 0 vanished / 0 obsolete**, and
  `lrelease` at 2026-11-06T10:38 emits **1186 finished / 0 unfinished**
  (`lrelease-check.log` in this directory). The U2 close-out already fixed and
  independently reviewed this (`review-u2-final/REPORT.md`,
  `review-u2b-final-lrelease.log`), so A2's numbers are stale, not wrong-then.
- `logs/31-ux-selftest.log:53` records `a2.help.manual-resource-not-empty PASS
  14539 bytes`; the current `docs/USER_GUIDE.md` is **40885 bytes**
  (mtime 2026-11-06 10:17, U3 added §11–§16), and the existing
  `vedit-app-ux-selftest` binary (2026-11-05 17:08) reports **14595 bytes** — so
  the detailed 53-check log is not from the current selftest binary either. The
  screenshot `11-user-manual.png` shows the pre-U3 14.5 KB manual.
- `logs/32-ctest-all.log` records **42/42**; the handover §2 says the suite is now
  **50**. This is a suite-growth artefact, not an A2 defect.

**Why it matters:** U8's acceptance matrix must point at evidence that describes
the delivered tree. Citing A2's 1018/14539/42 numbers either understates the
catalogue or attaches the manual claim to a screenshot of an obsolete document.

**Direction of fix (described, not applied):** no A2 code change. At U8, rebuild
and re-run `vedit-app-ux-selftest`, recapture `11-user-manual.png`, and cite the
U2/U3 verdicts (`review-u2-final`, `review-u3-final`) for catalogue and manual
content; keep A2's log for the code-level assertions only, labelled as the A2-time
run.

### M2 — Medium — the product-level selfcheck does not pin the requirement-7 structure

**What/where:** `src/app/main.cpp:370-382` checks only that `menus.file`,
`menus.view`, `menus.edit` and `menus.help` exist — **not** `project`, not the
order, not the toolbar. Its `settings.preferences.menu` check compares
`commands()->menuLabel("edit")` with `translate("&Edit")`, which is the Edit
menu's **own label** and would pass even if `settings.preferences` sat in File or
nowhere. The real structural assertions exist only in the A2 ctest:
`ux_selftest.cpp:185-190` (exact order `&File,&Edit,&View,&Project,&Help`) and
`:200-215` (Preferences is in Edit and is the last entry). That ctest simulates the
diagnostics lane (`addLaneCommands()`, `ux_selftest.cpp:136-150`) rather than using
the real lane, and the visual evidence is the selftest shell (demo provider), not
the KTextEditor product.

**Why it matters:** requirement 7's two structural claims are exactly the ones a
future refactor could silently break, and the gate that runs on the real product
binary would stay green. The risk is bounded because the product installs the
diagnostics lane **before** `startSession()`, and `startSession()` rebuilds the
menu and toolbar (`app_shell.cpp:227-235`) — so the real path is believed correct
from code order — but it is not independently asserted.

**Direction of fix:** add `menus.project`, a menu-bar order assertion over
`menuBar()->actions()` object names, and
`actionsInMenu("edit").contains(preferences)` to the selfcheck. No product code
change.

### N1 — Nit — one visible English string remains in the same window

`ApplicationShell::projectLabel()` returns hard-coded
`QStringLiteral("no project")` (`app_shell.cpp:970-977`), and
`12-main-menu-zh.png` shows `项目: no project` in the status bar. This is outside
the four surfaces the user named (top bar, panel toggles, right-click items,
diagnostics commands), so it does not fail requirement 3; it is the last visible
English in the zh window. Direction: `tr("no project")` + catalogue entry (or a
product decision that the status bar is out of scope).

### N2 — Nit — the F1 terminal evidence is QTest synthetic key events, not a user key press

`ux_selftest.cpp:447-520` sends a synthetic `QEvent::ShortcutOverride` and then
`QTest::keyClick(terminal, Qt::Key_F1)`, reading the PTY screen for `^[OP`; outside
the terminal it does `QTest::keyClick(editor, Qt::Key_F1)` and checks the manual
opened. The README's "两个方向都用真实按键实测过" overstates this: it is the right
Qt mechanism (accepting `ShortcutOverride` is what stops the window shortcut), but
the test does not exercise the real `QShortcutMap`/desktop dispatch. Direction:
word it as "QTest key events", or add a real xcb key-injection check (the repo
already has that style for the fold gutter). Also note N2's reproducibility bound:
my host re-run of the existing binary reproduced the toolbar/help/F1 checks but
`SKIP`ped `a2.i18n.zh` (hard-coded `/vedit-build/...` path) and timed out in the
real-KTextEditor undo segment — so the i18n leg is reproducible from the
catalogue + `lrelease` + the recorded log, not from the host binary.

### N3 — Nit — an A2 evidence log contradicts its own claim

`evidence/ux-batch-2/a2/logs/50-tree-facts.log` is headed
"## main.cpp: only the three authorized assertion lines changed" and then prints
`1 file changed, 429 insertions(+), 13 deletions(-)`. The stat is whole-worktree-vs-
HEAD (it includes earlier batches), so this log cannot substantiate the 3-line
claim. Direction: record the A2 delta against a pre-A2 snapshot, or drop the
subtitle. Not a product defect; it matters because "captain-authorized 3-line
scope" is a trust claim in the README.

### N4 — Nit — the translation catalogue is untracked in git

`git status --porcelain translations/` is `?? translations/` (the whole directory),
and `docs/USER_GUIDE.md` is modified. The plan gives `translations/**` to A2 this
batch and the captain is the only committer; if the final `feat/ux-batch-2`
commit omits it, the delivered tree ships no zh catalogue even though the built
`.qm` exists locally. Direction: stage `translations/` (and the manual) in the U8
commit; this is a delivery checklist item, not an A2 repair.

## 4. Declined to judge (reason on the same line)

- A4 items 6/8 (font/scale/format knobs/two-backend lint page) — separate task
  T30, not read in this pass.
- The LSP behaviour behind the seven context-menu items (definition/references/
  rename/hover dispatch, availability, rename gating) — B3/B4 scope; here I
  verified only that the labels are translated and that the real context menu
  consumes them.
- The manual's editorial content/completeness (§11–§16, shortcut tables) — U3's
  contract, reviewed in T9/`review-u3-final`; I verified only that the qrc embeds
  the current `docs/USER_GUIDE.md`, that the title is correct and that it renders.
  That review is **FAIL (request changes)**: U3-F1 (High) is exactly the
  `Ctrl+Alt+F` collision + the false `Ctrl+Shift+F` formatting claim carried as H1
  here; its repair is T34 and its re-review T35. Item 11's content is therefore
  **not clean until T35 closes** — A2's reader is fine, the document is not yet.
- Translation quality of all 1186 entries — U2's review (T10); I spot-checked the
  named surfaces and re-ran `lrelease`, I did not audit the whole catalogue.
- Visual/DPI/aesthetic quality on a real desktop — no real GUI session available
  here.
- The seven-gate / AppImage / final acceptance status — U8/captain.

## 5. Bounds of this review

- **No build.** Per the shared-build-tree notice I did not run `cmake --build`,
  `build-qt.sh`, `scripts/gates.sh` or any gate. The product binary was being
  rebuilt by other lanes during this review (its mtime moved from 10:04 to 10:32),
  so I make no claim about the current product artifact's contents.
- The host re-run used the **existing** `vedit-app-ux-selftest` binary (built
  2026-11-05 17:08) with `QT_QPA_PLATFORM=offscreen` and a borrowed
  `libtree-sitter.so.0.25` from the docker image snapshot. It passed the
  toolbar/help/F1 checks, skipped `a2.i18n.zh` (binary hard-codes the container
  translation path) and timed out (exit 124) in the real-KTextEditor undo segment.
  A clean full-suite reproduction on the host was **not** achieved.
- **No real desktop GUI.** The three screenshots were inspected as artefacts
  (xcb/Xvfb, 1600×950); I did not click the real menu bar, open the manual in a
  live window, or type into a real terminal.
- I did not run the product `--selfcheck`; I read the recorded 312-PASS log
  (`41-selfcheck-green.log`) and the pre-fix red log. The product binary is
  currently being rebuilt by other lanes.
- I did not re-run A2's ctest suite; I read `30-ctest-affected.log` (7/7) and
  `32-ctest-all.log` (42/42) and re-ran the selftest binary where the host allowed.
- Evidence files predate later lanes; where the tree moved on I said so (M1)
  instead of treating the A2 numbers as current.
- Nothing in this review edited `src/**`, `resources/**`, `translations/**`,
  `docs/**` or the build tree; only `evidence/ux-batch-2/review-a2-final/` was
  written.

## 6. What would change the verdict

- A Blocker/High would appear if, on a rebuild, the Project menu were missing,
  Preferences were not in Edit, undo/redo were dead on a real KTextEditor view, or
  `lrelease` were red. None of those holds in the tree I read.
- H1 (the carried `Ctrl+Alt+F` duplicate + the manual's false formatting shortcut)
  is already High and already scheduled (T34 repair, T35 review). If T34 were
  dropped, item 7 would lose its shortcut coherence and item 11 would remain
  mis-instructing the user; both would then fail acceptance on a reasonable-user
  standard.
- M1 becomes a delivery failure only if U8 ships the pre-U3 manual / pre-U2
  catalogue; that is an U8 checklist item, which is why this is a Medium with a
  named fix rather than a request-changes finding.
- M2 becomes a product risk only if the selfcheck is treated as the sole gate for
  requirement 7; the A2 ctest + code order currently cover it.

**Verdict: PASS / approve — A2 lane (risk Medium; one High carried under T34/T35).**
The three A2 requirements are delivered in the current tree; the Medium/Nit
findings are evidence-currency and verification-strength items to carry into U8,
and H1 is a pre-existing cross-lane shortcut defect already owned by the U3 repair
task — neither sends A2 back for repair.
