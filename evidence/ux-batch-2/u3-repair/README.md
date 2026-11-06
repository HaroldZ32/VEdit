# U3 repair (T34) — evidence

- **Task**: T34 = repair the U3 review findings
  (`evidence/ux-batch-2/review-u3-final/REPORT.md`, U3-F1..F4) plus the Lead's four
  additional inputs (A2-M2, A2-N1, B2-r5 disclosure, U4b-F1), after U6/T6 closed.
- **Frozen contract**: `.mpd/plans/ux-batch-2/00-plan.md` §0/§0.1; handover
  `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U3/§8.
- **Author**: Senior Engineer (`senior-engineer-5`), 2026-11-06.
- **U6 prerequisite**: `evidence/ux-batch-2/u6-final/README.md` (the lint wiring shipped:
  five of six knobs reach `VdLintBatchOptions`, `--autofix` stays pending, rules precedence
  `page → legacy tools → discovered → none`).

## 0. Write scope — declared vs actually touched

The card's scope was `src/app/`, `docs/USER_GUIDE.md`, `evidence/ux-batch-2/u3-repair/`.
The product defect in U3-F1 is **registered outside** that list, and the Lead's inputs reach
two more files; the deviations are deliberate and listed here so the verifier sees them:

| File | Why | Authorisation |
|---|---|---|
| `src/workspace/diagnostics/diagnostics_integration.cpp` | the `diagnostics.format` default lives here — the product fix U3-F1 requires it | the T34 card says "Fix the PRODUCT… give diagnostics.format a non-conflicting default" |
| `translations/vedit_zh_CN.ts` | exactly one catalogue row for the A2-N1 "no project" string | the Lead's A2-N1 input ("ONE catalogue row … the ONLY .ts change") |
| `src/syntax/kde/README.md` | U4b-F1 false claim | the Lead's fourth input ("Your scope is extended by exactly: `src/syntax/kde/README.md`") |

No test, gate or generated file was edited to obtain a pass. No full gate was run.

---

## 1. Finding → change matrix

| id | finding | what changed | proof |
|---|---|---|---|
| **U3-F1** (High) | §15.2's `Ctrl+Shift+F` is false; the real `Ctrl+Alt+F` collides with the Files-panel toggle; `CommandRegistry::add` does not detect duplicates | (a) `diagnostics.format` default → **`Ctrl+Shift+F`** (`diagnostics_integration.cpp:2389-2398`); (b) `CommandRegistry::add` warns on a duplicate effective shortcut (`command_registry.cpp:159-173`), and the registry gained `shortcutBindings()` / `duplicateShortcuts()` (`command_registry.h:90,94`; `command_registry.cpp:277,292`); (c) the product selfcheck asserts duplicate-free and prints the bindings dump (`main.cpp:477-485`); (d) the manual states the true values (§3, §14, §15.2) | red→green logs `01`/`02`; dump below |
| **U3-F2** (Medium) | §14's pointer used the GTK3 table (outline `Ctrl+Shift+O`, rename "preview") | §14 now carries a Qt6 keyboard table derived from the registry; §3's table is labelled "Qt6 default" and its outline/rename rows corrected; §4's `Ctrl+Shift+L` is labelled as the old GTK3 binding (no Qt6 slang shortcut) | §2 below; dump |
| **U3-F3** (Medium) | §15.3 and the §9 row were true only until U6 | §15.3 now says the five wired knobs are applied, `--autofix` is stored-only, and the rules precedence including the discovered file; §9's row says the discovered file is the **fallback** | §3 below; U6 code pointers |
| **U3-F4** (Nit) | "有告警时追加一行 ⚠" vs a same-line append | §11.1 now says "在**同一行末尾**追加 ` · ⚠ …`" | §3 below |
| **A2-M2** (Medium) | the product selfcheck never pinned the five menus / their order / where Preferences lives; its Preferences check compared the Edit label with itself | `main.cpp:397-485`: a lane-installed shell (`installPanels` + `startSession`, the product sequence) asserts (a) all five menus incl. Project, (b) the exact menu-bar order, (c) Preferences in Edit via `actionsInMenu("edit")` (+ only-edit, last-in-edit); each proven able to fail | red `03` (three FAILs), green `04` |
| **A2-N1** (Nit) | `项目: no project` in the zh UI | `ApplicationShell::projectLabel()` → `tr("no project")` (`app_shell.cpp:973-975`); one catalogue row `vedit::app::ApplicationShell / no project / 无工程` (`vedit_zh_CN.ts:4569`) | grep + `lrelease` below |
| **B2-r5** (Medium) | §16.4's three replace-bound sentences are false/obsolete after B2b; §12.3 must carry the same reality | §12.3 contained neither of the two false sentences (checked: no "完全相同"/"退化为" in it); it was extended with the whole-line pre-save proof + the truncated-preview refusal. §16.4 rewritten (400-char preview cap, preview-identical insert refused before saving for an unsaved buffer, only a whole-line-identical copy is harmless, no-`bufferText()` truncation refuses, whole-line pre-save comparison named); §12.3's refusal list names the whole-line pre-save comparison and the truncated-preview refusal | greps below (`完全相同` gone, `退化为` only unrelated project-root lines) |
| **U4b-F1** (important) | `src/syntax/kde/README.md` claimed "no silent use of the GPL KDE definition"; the measured truth is that four of five names resolve to KF6's embedded KDE copies in the build/run environment | README now separates the two measured cases (build/host vs packaged AppDir), states which is which, cites `u4b-final/05-definition-probe.log` and `a3-repair/11-f2-isolation.log`, and records the `version=` bump as an **open product decision** not done in this batch | README §"Which definition is ACTIVE" |

---

## 2. Acceptance evidence

### (1) No two commands share a default sequence — the registry dump

The selfcheck's lane-installed shell runs the **product sequence** (`installPanels()` before
`startSession()`, exactly what `main()` does), so the dump is the whole product registry
(shell + all workspace lanes + palette): **23 bindings**.

```
COMMAND-BINDINGS count=23
COMMAND-BINDING Ctrl+,	settings.preferences
COMMAND-BINDING Ctrl+Alt+D	view.panel.diagnostics
COMMAND-BINDING Ctrl+Alt+F	view.panel.file-tree
COMMAND-BINDING Ctrl+Alt+J	view.panel.terminal
COMMAND-BINDING Ctrl+Alt+N	file.new-layout
COMMAND-BINDING Ctrl+Alt+O	view.panel.outline
COMMAND-BINDING Ctrl+Alt+R	view.layout.reset
COMMAND-BINDING Ctrl+Alt+S	view.panel.search
COMMAND-BINDING Ctrl+Alt+V	view.panel.vcs
COMMAND-BINDING Ctrl+O	file.open
COMMAND-BINDING Ctrl+P	file.goto-file
COMMAND-BINDING Ctrl+Q	file.quit
COMMAND-BINDING Ctrl+S	file.save
COMMAND-BINDING Ctrl+Shift+F	diagnostics.format
COMMAND-BINDING Ctrl+Shift+H	view.split.horizontal
COMMAND-BINDING Ctrl+Shift+S	file.save-as
COMMAND-BINDING Ctrl+Shift+V	view.split.vertical
COMMAND-BINDING Ctrl+W	file.close-pane
COMMAND-BINDING F1	help.user-manual
COMMAND-BINDING F12	diagnostics.definition
COMMAND-BINDING F2	diagnostics.rename
COMMAND-BINDING F6	view.focus.next
COMMAND-BINDING Shift+F12	diagnostics.references
```

(verbatim from the final green log `logs/04-selfcheck-final-green.log`; the pre-fix log `01`
contains one extra line, `COMMAND-BINDING Ctrl+Alt+F\tdiagnostics.format` — that is the
collision the red run caught.)

```
CHECK commands.no-duplicate-shortcuts PASS
```

**Number of rows is 23; `Ctrl+Alt+F` now has exactly one owner (the Files-panel toggle) and
`Ctrl+Shift+F` exactly one (Format Document).**

Sequence choice, with the alternatives stated:

- **Rejected `Ctrl+Alt+F`**: it is the Files-panel toggle's default (`app_shell.cpp:601`), the
  exact collision the review found; Qt would treat the sequence as ambiguous and drop both.
- **Chosen `Ctrl+Shift+F`**: free in the full registry (dump above) **and** the historical
  format accelerator (`crates/vd-config/src/model.rs` `DEFAULT_KEY_BINDINGS["format"]`; the
  old-product selftest logs name it too, `tests/e2e/out/*/selftest-all.log`). Keeping the
  value the manual and the old product already used avoids inventing a key users cannot know.
- **Rejected moving the panel toggle instead**: that would steal a documented toggle default
  with no historical alternative; the review's direction assigns the fix to the format
  command.
- **Rejected `Ctrl+Alt+Shift+F` / other three-modifier or F-key candidates**: no historical
  basis; F-keys are already the editor/diagnostic namespace (F1/F2/F3/F6/F8/F12).

The duplicate guard is load-bearing, not decorative: the pre-fix run emitted

```
CommandRegistry: duplicate shortcut 'Ctrl+Alt+F' for 'view.panel.file-tree' and 'diagnostics.format'
CHECK commands.no-duplicate-shortcuts FAIL Ctrl+Alt+F: diagnostics.format, view.panel.file-tree
```

### (2) Every keyboard row of the manual matches the registry

The two tables the finding names (§3's capability table and §14's new keyboard table) were
re-derived from the registry dump. Row → command id → dump:

| manual row | value | registry command id | dump |
|---|---|---|---|
| 一键格式化 / 格式化文档 | `Ctrl+Shift+F` | `diagnostics.format` | `COMMAND-BINDING Ctrl+Shift+F	diagnostics.format` |
| 跳转到定义 | `F12` | `diagnostics.definition` | `COMMAND-BINDING F12	diagnostics.definition` |
| 查找引用 | `Shift+F12` | `diagnostics.references` | `COMMAND-BINDING Shift+F12	diagnostics.references` |
| 符号大纲 | `Ctrl+Alt+O` | `view.panel.outline` | `COMMAND-BINDING Ctrl+Alt+O	view.panel.outline` |
| 重命名符号 | `F2` | `diagnostics.rename` | `COMMAND-BINDING F2	diagnostics.rename` |

The old values are marked as the old GTK3 product's, not as current: outline
`Ctrl+Shift+O` (which in Qt6 is the Files panel's **Open…** action,
`file_tree_widget.cpp:57`), rename "preview" (Qt6 `F2` applies the edits — §13.6 already
said so). §4's `Ctrl+Shift+L` is likewise labelled the old GTK3 binding; Qt6 owns
`Ctrl+Shift+L` in the editor-enhancements filter (`multi_cursor.cpp:984`) and has no
slang lint shortcut (only `project.lint`, no default sequence).

### (3) §15.3 / §9 now match U6's shipped behaviour

Manual (final):

> - 当前引擎**支持**的旋钮都会真正生效：verible 的 `--ruleset`、`--rules`、`--rules_config`、
>   `--waiver_files` 与 slang 的 `-W…`、`--error-limit` 都进入工程 lint 任务的 argv；默认值不发送任何 flag。
>   唯一例外是 verible 的 `--autofix`：值会保存，但 lint 任务的 C ABI 没有这个字段，页面顶部状态行
>   如实写明"只保存、不会发送"，不会以静默无效参数发出。
> - 规则文件的优先级：页面的「规则文件（--rules_config）」→ 工具页的旧 Verible rules 字段 →
>   工程里自动发现的 `.rules.verible_lint` → 都没有则不传 `--rules_config`。

Verified at:

- `src/workspace/settings/lint_options.cpp:102-110` — `Autofix` is `TransportPending`; the other
  knobs are `Supported`.
- `src/workspace/settings/lint_options.h:270-340` — `buildLintJobOptions()` fills
  ruleset/rules/waiverFiles/slangWarnings/slangErrorLimit and the rules precedence
  (`rulesConfig` → legacy tools → `discoveredRulesPath`).
- `src/workspace/diagnostics/diagnostics_controller.cpp:1329` — the project backend composes with
  `settings::currentDiscoveredRulesPath()`.
- `src/workspace/diagnostics/diagnostics_integration.cpp:558` / `:1826` — the project poller
  publishes / clears `VdProjectInfo.rules_path`.
- `evidence/ux-batch-2/u6-final/README.md` §2 (argv echoes:
  `settings.a4.lint.argv.*`, `defaults-no-flag`, `discovered-rules-path`, `hand-typed-wins`) and
  §5 (bounds: the five knobs reach the **project** pass; `--autofix` is never sent).

### (4) Docs gate

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
EXIT=0
```

### (5) B2 disclosure greps (the Lead's required commands)

```
$ grep -n "完全相同" docs/USER_GUIDE.md
(no match)

$ grep -n "退化为" docs/USER_GUIDE.md
103:   `filelist`，都没命中的退化为起始目录 `fallback`）；起点目录的定义见本节末
174:  向上查找，找不到就退化为该目录本身。
```

Both remaining `退化为` lines are the **project-root fallback** prose in §2 — unrelated to
search/replace (the replace-bound sentence is gone). `完全相同` no longer appears anywhere in
the manual.

Note on `evidence/ux-batch-2/b2-repair-r3/README.md:135,138-142`: those lines describe the
superseded r3 bound and are **not edited here** (the Lead's instruction: record the
supersession, do not rewrite another lane's evidence). The superseding statement is
`evidence/ux-batch-2/b2-repair-r4/README.md` §5-§6 and `review-b2-r5/REPORT.md` §3.

### A2-N1 catalogue row

```
$ grep -n -A2 '<location filename="../src/app/app_shell.cpp" line="975"/>' translations/vedit_zh_CN.ts
    <message>
        <location filename="../src/app/app_shell.cpp" line="975"/>
        <source>no project</source>
        <translation>无工程</translation>
    </message>

$ /usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/v.qm
    Generated 1209 translation(s) (1209 finished and 0 unfinished)
```

Only that one message was added; no other catalogue row was touched and `lupdate` was not run.

---

## 3. Red / green proofs (logs in `logs/`)

| log | state | observed |
|---|---|---|
| `01-selfcheck-red-duplicate.log` | **before** the format fix | `CHECK commands.no-duplicate-shortcuts FAIL Ctrl+Alt+F: diagnostics.format, view.panel.file-tree`; `CommandRegistry: duplicate shortcut …` warning; `APP-SHELL-SELFTEST ok=0 checks=318 failures=1` |
| `02-selfcheck-green-after-shortcut-fix.log` | after the product fix | `CHECK commands.no-duplicate-shortcuts PASS`; `APP-SHELL-SELFTEST ok=1 checks=318 failures=0` |
| `03-selfcheck-mutated-checks.log` | M2 checks temporarily inverted | `menus.five.registered FAIL`, `menus.five.order FAIL`, `menus.preferences.in-edit-registry FAIL`; `ok=0 checks=318 failures=3`; the detail proves the real state was correct ("all five", the correct order, `edit-actions=3`) |
| `04-selfcheck-final-green.log` | restored, final binary | `menus.five.registered/order`, `menus.preferences.in-edit-registry/only-edit/last-in-edit`, `commands.no-duplicate-shortcuts`, `empty.no-project-label` all PASS; `APP-SHELL-SELFTEST ok=1 checks=318 failures=0` |
| `03a-mutation-build.log`, `04a-final-build.log` | build logs | incremental `ninja -C target/qt6-build vedit-app`, exit 0 each |

Commands (pinned image; the built `vedit-app` binary was relinked for the selfcheck — this is
a targeted build, not the full gate):

```sh
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 \
  vedit-qt6-build:local bash -c 'ninja -C target/qt6-build vedit-app'
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 \
  -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e VEDIT_QUIET=1 \
  vedit-qt6-build:local bash -c 'mkdir -p /tmp/xdg-runtime && target/qt6-build/src/app/vedit-app --selfcheck'
```

The M2 red proof was produced by temporarily inverting the three expectations, running the
selfcheck (`03`), then restoring them (`04`); the final tree contains the correct expectations
(`main.cpp:432` `missingMenus.isEmpty()`, `:447` `barMenus == expectedBar`, `:456`
`editActions.contains(lanePreferences)`).

---

## 4. Findings outside this task's scope (reported, not edited)

1. **Editor-kernel fallback shortcut.** `src/editor/kernel/lsp_actions.cpp:373`
   (`editorActionShortcut(EditorAction::Format)`) still returns `Ctrl+Alt+F`. The product's
   context menu uses the **wired shell action's** shortcut when present
   (`editor_context_menu.cpp:76-83`), so the real UI shows `Ctrl+Shift+F`; the fallback only
   applies when no shell action is bound. Left for the editor lane (out of scope).
2. **Files-panel widget F2 vs Rename Symbol.** `src/workspace/filetree/file_tree_widget.cpp:69`
   binds the tree's own rename action to `F2`; `diagnostics.rename` is also `F2`. That is a
   **widget-level** QAction, not part of the command registry, so the registry dump cannot see
   it (and the dump is clean). Reported for the file-tree lane; not edited.
3. **U6 status strings are untranslated.** `settings_panel.cpp:2196-2210` now uses the new
   English status texts (`…are applied to the lint job; --autofix is stored only…`, `-W and
   --error-limit are applied…`), but `translations/vedit_zh_CN.ts` still contains only the
   **pre-U6** strings (`--rules_config is applied; the other options are stored but not
   sent`). Under the "ONE row" rule this belongs to the i18n lane; my manual therefore no
   longer quotes the old Chinese status text. Flagged for the Lead.
4. **Pre-existing GTK3-era shortcut prose.** §2's editing shortcut sentence and §10's VCS
   entries were not re-derived (the review explicitly declined to judge §1–§10); §3/§4/§14
   were corrected because the finding makes them actively wrong for a Qt6 reader.
5. **`src/workspace/search/README.md` nits** (`r5` §3 items 3–4: the "扫描之后" bullet and the
   Ctrl+F transient text) are the B2 lane's; not touched.

---

## 5. Bounds — what is NOT proven

- **Historical evidence:** `evidence/ux-batch-2/u3-final/README.md` (the U3 delivery's own
  claim list) still describes the **pre-U6** tree for §15.3/§9 and quotes the old
  `Ctrl+Shift+F` shortcut finding. It is left as the historical record; this repair supersedes
  those rows (the review itself recorded U3-F3 as time-sensitive).

- **No full gate** (`build/gates/qt6-cxx.sh`, `scripts/gates.sh`) and no ctest run: the shared
  build tree only rebuilt `vedit-app` + the selfcheck. The captain runs the single full gate.
- **No real-machine GUI**: the selfcheck is offscreen; the menu structure is asserted on the
  real `ApplicationShell` **object tree**, not by clicking.
- **Chinese rendering not exercised at runtime**: the A2-N1 fix is proven by the source
  `tr()` + the catalogue row + `lrelease` (1209 finished / 0 unfinished). I did not load the
  `.qm` in a running product and screenshot `无工程`.
- **The selfcheck's lane shell is in-process**: it proves the registered command set and the
  menu-bar order of a shell built from the same `installPanels()` sequence `main()` uses, but
  it does not prove a packaged AppImage carries the same binary.
- **U6's argv echoes spawn nothing** (that is U6's own disclosed bound); the manual repeats the
  U6 evidence, it does not re-run the lint tools.
- **Line numbers in this file**: as observed on 2026-11-06 while other lanes were still editing
  the tree; the named symbols are authoritative.
