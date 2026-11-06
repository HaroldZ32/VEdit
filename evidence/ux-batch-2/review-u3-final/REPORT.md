# U3 review (independent) — user manual + documentation corrections

Reviewer: independent verifier. READ-ONLY for the reviewed work: no product source, manual,
test or gate was edited by this review; the only files written are this report and the raw
check logs in `evidence/ux-batch-2/review-u3-final/`.

Contract: `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U3 (every section the manual
must gain, the four honest bounds of §8, the stale claims to correct), with the user's 14
requirements in §1 and the binding rulings in `.mpd/plans/ux-batch-2/00-plan.md` §0/§0.1.

Evidence reviewed: `evidence/ux-batch-2/u3-final/README.md` (the claim-by-claim list),
`docs/USER_GUIDE.md` (the delivered manual), the four corrected documents
(`docs/handover/04-hard-facts.md`, `src/syntax/kde/README.md`,
`evidence/ux-batch-2/b1/README.md`, `docs/handover/appendix/requirements-rev2.md`,
`docs/handover/CELL-20261102/3-STATUS.md`), and the translations file for the quoted Chinese
UI strings.

New evidence produced by this review (all under `evidence/ux-batch-2/review-u3-final/`):

| File | Content |
|---|---|
| `01-docs-gate.log` | my own `bash build/gates/docs.sh`: PASS, exit 0 |
| `02-corrections-grep.txt` | the four stale claims: current state and the only remaining `builtin_valid_defs=2` occurrence (the frozen handover's own instruction) |
| `03-shortcut-mismatch.txt` | manual shortcut lines vs the Qt6 command registrations vs the old GTK3 bindings |
| `04-claim-checks.txt` | my own 24 claim checks, each with its verdict and the code/evidence basis |

## Assessment

**Risk level: MEDIUM. Verdict: FAIL (request changes).** The manual is a real, task-oriented
Chinese guide and it does answer the 14 items: all required sections exist, the four §8 bounds
are disclosed in substance, the stale claims are corrected, and I verified 17 substantive
claims against the code with matching results (including the ones most likely to be wrong:
Ctrl vs Alt, which click goes to the instantiation, the automatic lint behaviour, the greyed
lint knobs and the extension table). But one user-facing instruction is **false**: §15.2 says
formatting is still `Ctrl+Shift+F`; the Qt6 command is `Ctrl+Alt+F`, and that very binding is
also the default of the file-panel toggle, i.e. the product's default shortcuts **collide**.
A second Qt6-scoped pointer (§14) hands the reader the old GTK3 shortcut table. A manual that
misinstructs the user on the very feature it is describing is a High documentation defect; the
repair is small (fix the product binding or the text, plus one line in §14), but it is not a
PASS.

## Findings

### U3-F1 — High: the Qt6 format shortcut claim is false, and the real binding collides with the file-panel toggle

- What: `docs/USER_GUIDE.md` §15.2 (inside the "§11–§15 only apply to the Qt6 product" block)
  says "编辑器里的 `Ctrl+Shift+F` 仍是一键格式化". In the Qt6 product the Format Document
  command (`diagnostics.format`) is registered with `QKeySequence("Ctrl+Alt+F")`
  (`src/workspace/diagnostics/diagnostics_integration.cpp`, `addCommand("diagnostics.format", …)`).
  `Ctrl+Shift+F` is the **old GTK3** binding (`crates/vd-config/src/model.rs`,
  `DEFAULT_KEY_BINDINGS`), which is why the sentence reads plausibly but is wrong for the
  delivered Qt6 product.
- Additional product defect surfaced while verifying the claim: `Ctrl+Alt+F` is **also** the
  default of the file-tree panel toggle (`src/app/app_shell.cpp`, the `panelCommands` table,
  `{"file-tree", … , "Ctrl+Alt+F"}`). Both actions are registered in the same window's command
  registry and `CommandRegistry::add` (`src/app/command_registry.cpp`) does not detect duplicate
  shortcuts, so the default is ambiguous; `SettingsPanel::conflicts()`
  (`src/workspace/settings/settings_panel.cpp`) would list `Ctrl+Alt+F` for both commands. The
  old GTK3 product used `Ctrl+Shift+F`; assigning it to the Qt6 format command would make the
  manual true and remove the collision.
- Why it matters: the user's requirement #11 is "Help contains a real manual". A user of the
  Qt6 deliverable who follows §15.2 presses `Ctrl+Shift+F` and nothing happens; if they find
  `Ctrl+Alt+F` in Preferences they may toggle the Files panel instead of formatting. The
  manual's §3 table repeats the GTK3 bindings without saying which product it describes, so
  the same misreading reaches a Qt6 reader there.
- Where: `docs/USER_GUIDE.md` §15.2 (and §3's table row "一键格式化"); product side:
  `diagnostics_integration.cpp` (`diagnostics.format`), `app_shell.cpp` (`panelCommands`),
  `command_registry.cpp` (`add`).
- Direction of a fix (described, never applied): either (a) give `diagnostics.format` the
  `Ctrl+Shift+F` default (matching the manual and the old product) and keep `Ctrl+Alt+F` for
  the file panel — this also removes the default collision; or (b) correct §15.2 to the real
  binding **and** first resolve the duplicate, otherwise the manual would document an ambiguous
  shortcut. Also label §3's table as the old GTK3 product's bindings (or give both products'
  bindings) so a Qt6 reader does not take it as current. Adding a duplicate-shortcut guard to
  `CommandRegistry::add` (or a ctest asserting the default table has no duplicates) would keep
  this from recurring.

### U3-F2 — Medium: §14's Qt6 shortcut pointer uses the GTK3 table (symbol outline, rename semantics)

- What: §14 says "跳转定义 / 查找引用 / 符号大纲 / 重命名预览的快捷键仍在 §3 的表里
  （`F12` / `Shift+F12` / `Ctrl+Shift+O` / `F2`）". For the Qt6 product, `Ctrl+Shift+O` is not
  the symbol outline: the outline panel toggle is `Ctrl+Alt+O` (`app_shell.cpp` panelCommands,
  `view.panel.outline`), while `Ctrl+Shift+O` is bound to the file tree's "Open..." toolbar
  action (`src/workspace/filetree/file_tree_widget.cpp`, `openAction_->setShortcut`). The
  `Ctrl+Shift+O` value is the old GTK3 binding (`crates/vd-config/src/model.rs`). §3 also
  describes rename as "只预览…不写文件", while the Qt6 `F2` (`diagnostics.rename`) applies the
  edits (§13.6 says so correctly) — so the pointer imports a second, semantically wrong
  description as well.
- Why it matters: the user will try `Ctrl+Shift+O` for the outline and get the file-tree Open
  dialog (or nothing, depending on focus); the manual's own §14 table is otherwise correct.
- Direction: give the Qt6 bindings in §14 (`Ctrl+Alt+O` for the outline panel; `F2` =
  Rename Symbol, which applies, not a preview) and label §3 as the GTK3 section — or add the
  per-product column to §3.

### U3-F3 — Medium (time-sensitive, disclosed): §15.3 and the §9 row are true only until U6 lands

- What: §15.3 says six lint knobs are "已保存但尚未发送（传输待实现）", that `--rules_config` is
  the only knob that reaches the backend, and that the discovered `.rules.verible_lint` is not
  wired as the default; the §9 row repeats the last point. Those three statements are **true in
  the tree I reviewed**: `ffi_backends.cpp` passes `QString discoveredRulesPath;` (empty) to
  `buildLintJobOptions`, and `lint_options.cpp` marks ruleset/rules/waiver_files/autofix and the
  slang knobs `TransportPending`. But the outstanding U6 task is explicitly chartered to wire
  exactly those values and the discovered path, so a U6 landing before the release makes the
  manual false in the understating direction. The lane disclosed this itself
  (`u3-final/README.md` §5.5) and names the fix sites.
- Why it matters: this is a delivery-ordering risk, not a current lie. If the final acceptance
  matrix quotes §15.3 as-is while U6 changes the behaviour, the manual and the product diverge.
- Direction: after U6 lands (or is dropped), re-read §15.3 and the §9 row against
  `buildLintJobOptions` and the panel's `lintKnobState`; update the one-line statements. The
  acceptance matrix should record which tree the manual was verified against.

### U3-F4 — Nit: the module-tree summary line does not "append a line" for a warning

- What: §11.1 says "有告警时追加一行 `⚠ …`"; `ModuleCatalog::summaryLine()`
  (`src/workspace/module_tree/module_catalog.cpp`) appends `" · ⚠ %1"` to the **same** line,
  not a new line. Cosmetic wording only; the content (warning shown in the summary) is right.
- Direction: say "在汇总行末尾追加 ⚠ 提示" (or make the code wrap — not needed).

## The required sections (all present, task-oriented, Chinese)

| Requirement | Present | Note |
|---|---|---|
| Module tree (roots never instantiated, shared under every parent, no network/driver rows, double-click opens declaration) | Yes | §11.1 |
| Interface panel (columns, follows focus, double-click → declaration, Ctrl+double-click → instantiation statement) | Yes | §11.2, including the honest "no per-connection position" note |
| Search (shared options, scope, two replace buttons 替换/全部替换, RE2-vs-PCRE, save-before-replace + refusal conditions) | Yes | §12.1–12.5 |
| Diagnostics (auto silent project lint after save, ELAB modal only on explicit trigger, hover + conditional fix, red/yellow/green dots, upper-over-lower, one verdict) | Yes | §13.1–13.6 |
| Navigation (Ctrl+click = definition, Alt+click = cursor, Alt+drag = block, split-pane activation) | Yes | §14 (shortcut pointer wrong — F2) |
| Settings (font immediate, scale restart, format defaults send no flag, one Lint page + two stores, greyed knobs with reasons) | Yes | §15.1–15.3 (shortcut wrong — F1; U6 timing — F3) |
| Syntax table including `.sdc/.xdc → Tcl` | Yes | §2, six rows / 15 suffixes; matches `highlight_mode.cpp` `kExtensionTable` |
| 已知限制 with the four §8 bounds in substance | Yes | §16.1–16.4: no real-machine GUI (offscreen/Xvfb, tofu note), cross-file Rename NOT claimed, RE2 vs PCRE, the three residual replace bounds |

The 24 claim checks with their individual verdicts are in `04-claim-checks.txt`. The ones the
brief singled out all came back true: Ctrl vs Alt mapping (Ctrl+click → definition through the
F12 action, Alt+click → cursor), double-click vs Ctrl+double-click on a port, the automatic
pass (700 ms debounce, single-flight cancel, save first, no modal; explicit-only ELAB dialog),
the greyed lint knobs (slang: no ruleset/rules-config/waiver/autofix; verible: no `-W`/
`--error-limit`; transport-pending knobs labelled as stored-not-sent), and the full extension
table (including `.sdc/.xdc → Tcl`).

## Stale claims and corrections

All four corrections landed and I verified them directly (`02-corrections-grep.txt`):
`docs/handover/04-hard-facts.md` now states the five shipped definitions and marks the old
`builtin_valid_defs=2` probe void (the only remaining `builtin_valid_defs=2` string is the
frozen handover's own correction instruction); `src/syntax/kde/README.md` says "All five are
our own LGPL-2.1-or-later definitions"; the b1 README's "Every string is wrapped" paragraph is
gone and replaced by the dated correction; the handover narratives (old instance-hierarchy
module tree, cross-file replace as a non-goal, hover grey-out) carry dated corrections rather
than silently rewritten history.

## Declined to judge

- The manual's pre-existing §1–§10 product descriptions (GTK3 installation, VCS, FIM) —
  outside U3's written scope; only the new/changed text and the §8 bounds were judged.
- The rendered Help view: I did not run `QTextEdit::setMarkdown` (no Qt runtime for the product
  on the host). The added Markdown is the same GFM the file already used; the help-viewer ctest
  only asserts the H1, and the final build is what embeds the file (see bounds).
- The actual zh_CN UI at runtime: I verified the quoted Chinese strings in
  `translations/vedit_zh_CN.ts`, not in a running `.qm`. The translation lane owns that.
- Whether §3/§4 should be rewritten for the Qt6 product as a whole (they are largely GTK3-era);
  I judged only the places where the Qt6 scope makes the text false (§15.2, §14's pointer).
- The product shortcut collision's own fix (a code change) — outside U3's write scope; reported
  as the direction of U3-F1 and flagged for the captain/A2 lane.

## Bounds of this review

- I did not rebuild the product; the **currently built binary still embeds the previous
  `docs/USER_GUIDE.md`** (the build ran before the manual was edited). The final gate/build
  (U8) must rebuild so `Help → User Manual` carries the new text — the qrc
  (`resources/help/user_manual.qrc`) points at the file, so a rebuild is sufficient.
- I did not run the full gate set or ctest; only `build/gates/docs.sh` (PASS) and static reads.
  I did not read every line of §1–§10; the new §11–§16 and the corrected files were read in
  full.
- Line numbers in the U3 README's evidence table may drift while other lanes edit; I verified
  against symbols/files, not those line numbers.
- Review is against the tree as of 2026-11-06 10:33 (+08:00). If U6 (lint wiring) or any
  further manual edit lands, §15.3/§9 and the shortcut findings must be re-checked.
