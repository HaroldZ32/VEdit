# U3b repair (T40) — evidence

- **Task**: T40 = the three leftovers from `evidence/ux-batch-2/review-u3-final-r2/REPORT.md`:
  U3r2-F2 (Nit: editor context-menu fallback for Format), U3r2-F3 (Nit: §3 format mechanism)
  and the U3r2-F1 **quote check** (§15.3's Chinese quote must match the shipped catalogue).
- **Author**: Senior Engineer (`senior-engineer-5`), 2026-11-06.
- **Prerequisite**: U3r2-F1 was the i18n lane's catalogue update; it landed
  (`translations/vedit_zh_CN.ts`, mtime 12:01:31, 0 unfinished), so the ship wording exists and
  was the reference for the quote check.

## 0. Write scope — declared vs touched

Card scope: `src/app/`, `docs/USER_GUIDE.md`, `evidence/ux-batch-2/u3-repair2/`.

| File | Why it is outside the listed scope | Authorisation |
|---|---|---|
| `src/editor/kernel/lsp_actions.cpp` | U3r2-F2's target **is** `editorActionShortcut(EditorAction::Format)` — the display fallback lives here | the task names that function |
| `src/workspace/diagnostics/diagnostics_integration.cpp` | the single-source requirement: the registry registration must consume the kernel default instead of repeating the literal | "single source of truth — do not duplicate the literal a third time" |

`translations/vedit_zh_CN.ts` was **not** edited by this task (the i18n lane's update is the
reference); no test or gate was edited; no full gate was run.

---

## 1. U3r2-F2 — the context-menu Format fallback now agrees with the registry (one source)

**What was wrong:** `editorActionShortcut(EditorAction::Format)` still returned `Ctrl+Alt+F`
while the registry action (`diagnostics.format`) was `Ctrl+Shift+F`. The context menu shows the
wired shell action's sequence when it can resolve it and falls back to this function otherwise,
so a degraded/lane-less shell could display the old, now-colliding key.

**What changed:**

1. `src/editor/kernel/lsp_actions.cpp:372-379` — the Format case returns `Ctrl+Shift+F` and
   documents that this function is the ONE default.
2. `src/workspace/diagnostics/diagnostics_integration.cpp` — the registration no longer repeats
   the literal: it calls the kernel function through a small compile-time helper
   (`formatDefaultShortcut()`, defined next to `installDiagnostics`):

```cpp
namespace {
QKeySequence formatDefaultShortcut()
{
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
    return vedit::editor::editorActionShortcut(vedit::editor::EditorAction::Format);
#else
    return QKeySequence();   // no editor kernel => no editor to format, no context menu
#endif
}
} // namespace
```

and the registration becomes `addCommand("diagnostics.format", …, formatDefaultShortcut(), 63)`.

**Single-source proof** — the literal exists exactly once in the product source:

```
$ grep -rn 'Ctrl+Shift+F' src/ --include=*.cpp --include=*.h
src/editor/kernel/lsp_actions.cpp:378:        return QKeySequence(QStringLiteral("Ctrl+Shift+F"));
```

(The `#else` arm deliberately carries **no** sequence — a build without the editor kernel has
neither a format path nor an editor context menu, so there is nothing that could display a
disagreeing key.)

**Menu entry and toolbar show the same sequence — code path:**

- `src/app/app_shell.cpp` `buildMenuBar()` adds `commands_->actionsInMenu(menuId)` — the
  registry's own `QAction`s;
- `src/app/app_shell.cpp` `buildToolBar()` resolves the same objects (`QAction *action =
  commands_->action(id); mainToolBar_->addAction(action);`, and `"diagnostics.format"` is in
  `kToolBarOrder`);
- `src/editor/kernel/editor_context_menu.cpp:78-83` `appendItem()` prefers the wired shell
  action's shortcut and falls back to `editorActionShortcut(action)`:

```cpp
const QAction *wired = shellActionFor(view->window(), action);
QString text = wired != nullptr && !wired->text().isEmpty() ? wired->text() : editorActionLabel(action);
QKeySequence shortcut = wired != nullptr && !wired->shortcut().isEmpty()
                            ? wired->shortcut()
                            : editorActionShortcut(action);
```

⇒ menu, toolbar and (wired or degraded) context menu all resolve to the same value.

**Machine proof (selfcheck, new check `shortcut.format.registry-equals-fallback`):**

```
red  (02-selfcheck-red-fallback.log):352
  CHECK shortcut.format.registry-equals-fallback FAIL registry=Ctrl+Shift+F fallback=Ctrl+Alt+F
  APP-SHELL-SELFTEST ok=0 checks=319 failures=1

green (04-selfcheck-green.log):352
  CHECK shortcut.format.registry-equals-fallback PASS registry=Ctrl+Shift+F fallback=Ctrl+Shift+F
  APP-SHELL-SELFTEST ok=1 checks=319 failures=0
```

The red run was produced by adding the check **before** changing the fallback (the build logs
`01-red-build.log` / `03-green-build.log` are in `logs/`). The check lives in the product
selfcheck (`src/app/main.cpp`, inside the lane-installed menu block) and compares the registry
action's effective shortcut with `editorActionShortcut(EditorAction::Format)`.

---

## 2. U3r2-F3 — §3's format row now describes the shipped mechanism

**What the shipped code does:** `BackendWiring::formatDocument()`
(`src/workspace/diagnostics/diagnostics_integration.cpp:1528-1566`) calls
`diagnostics::formatVerilog(state_.toolchain().veribleFormat.path, original, …)` — the
**external `verible-verilog-format`** process — and reports `"Formatted with
verible-verilog-format"`. `grep -rn documentFormatting src/` returns **no match**: the Qt6
product makes no LSP `textDocument/formatting` request. (The old GTK3 product used LSP
documentFormatting.)

**Manual change** (`docs/USER_GUIDE.md` §3):

```
| 一键格式化 | `Ctrl+Shift+F` | 执行**外部** `verible-verilog-format`（不是 LSP 的 documentFormatting），**一次撤销**即可回退 |
```

plus one sentence after the table that states what is LSP-backed and what is not:

> 表中除「一键格式化」外，实时诊断、跳转定义、查找引用、符号大纲与重命名都由
> `verible-verilog-ls`（LSP）提供；**一键格式化不走 LSP**——Qt6 直接调用随包的
> `verible-verilog-format` 外部进程，没有 `documentFormatting` 请求。

(The five LSP-backed rows match the shipped wiring: diagnostics / definition / references /
outline / rename all go through the verible LSP session; hover is described in §13.4.)

---

## 3. U3r2-F1 quote check — §15.3 matches the shipped Chinese catalogue

The i18n lane's update landed before this task (catalogue mtime 12:01:31, `0 unfinished`,
`lrelease` 1211 finished / 0 unfinished). The autofix sentence the manual quotes is:

```
source:      Active engine. --ruleset, --rules, --rules_config and --waiver_files are applied to
             the lint job; --autofix is stored only (the lint job's C ABI has no field for it).
translation: 当前引擎。--ruleset、--rules、--rules_config 与 --waiver_files 会应用到 Lint 任务；
             --autofix 仅保存（Lint 任务的 C ABI 没有对应字段）。
```

**Before:** the manual quoted a paraphrase, **"只保存、不会发送"**, which is not in the
catalogue (the shipped zh UI shows the sentence above).

**After** (`docs/USER_GUIDE.md` §15.3):

```
  唯一例外是 verible 的 `--autofix`：值会保存，但页面顶部的状态行写明
  **「--autofix 仅保存（Lint 任务的 C ABI 没有对应字段）」**，不会以静默无效参数发出。
```

**Character-for-character verification** (extracted from the manual and compared to the
catalogue translation):

```
$ python3 …   # quote extracted from the manual, translation extracted from the .ts
MANUAL QUOTE : '--autofix 仅保存（Lint 任务的 C ABI 没有对应字段）'
SHIPPED ZH   : '当前引擎。--ruleset、--rules、--rules_config 与 --waiver_files 会应用到 Lint 任务；--autofix 仅保存（Lint 任务的 C ABI 没有对应字段）。'
QUOTE IS EXACT SUBSTRING OF SHIPPED TRANSLATION: True
QUOTE LENGTH: 36   translation length: 105
```

The quote is a verbatim 36-character substring of the shipped translation (no `%1`/`%2`
placeholders are involved); the paraphrase is gone from the manual.

---

## 4. Docs gate

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
EXIT=0
```

---

## 5. Bounds — what is NOT proven

- **No full gate** (`build/gates/qt6-cxx.sh` / `scripts/gates.sh`); only `vedit-app` was
  rebuilt for the selfcheck, and `docs.sh` was run. The captain re-runs the full gate.
- **No runtime zh screenshot**: the quote check compares the manual against the SHIPPED
  catalogue entry (and `lrelease` accepts it), not against a rendered Preferences page.
- **The selfcheck's lane shell is in-process**: `shortcut.format.registry-equals-fallback`
  proves the registry value and the kernel fallback are equal; the menu/toolbar identity is
  proven by the shared `QAction` code path quoted in §1, not by clicking.
- Line numbers are as observed on 2026-11-06; other lanes may shift them (symbols are
  authoritative).

## 6. Reproduce

```sh
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 \
  vedit-qt6-build:local bash -c 'ninja -C target/qt6-build vedit-app'
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp -e LANG=C.UTF-8 \
  -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e VEDIT_QUIET=1 \
  vedit-qt6-build:local bash -c 'mkdir -p /tmp/xdg-runtime && target/qt6-build/src/app/vedit-app --selfcheck'
bash build/gates/docs.sh
grep -rn 'Ctrl+Shift+F' src/ --include=*.cpp --include=*.h      # exactly one hit (the kernel table)
grep -n '一键格式化' docs/USER_GUIDE.md
```

Logs: `logs/01-red-build.log`, `logs/02-selfcheck-red-fallback.log`,
`logs/03-green-build.log`, `logs/04-selfcheck-green.log`.
