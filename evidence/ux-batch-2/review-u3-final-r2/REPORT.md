# U3 repair re-review (r2) — shortcut table, duplicate detector, U6 statements, T38 probe move

Reviewer: independent verifier. READ-ONLY for the reviewed work: no product source, test,
catalogue or generated file in the tree was edited by this review. The probes I compiled ran
against the source tree and the pinned image with their output under
`evidence/ux-batch-2/review-u3-final-r2/` (or `/tmp` inside the image); nothing under
`target/qt6-build` was written, reconfigured or rebuilt, and the captain's AppImage repackaging
was not disturbed.

Contract: my round-1 FAIL `evidence/ux-batch-2/review-u3-final/REPORT.md` (U3-F1 High:
format shortcut false + registry collision; U3-F2 Medium: §14 GTK3 leftovers; U3-F3 Medium:
U6-time-sensitive statements; U3-F4 Nit), plus the Lead's T35 criteria and the T38
probe-source-move judgement.

Evidence reviewed: `evidence/ux-batch-2/u3-repair/README.md` and its logs (`01-selfcheck-red-duplicate.log`,
`02-selfcheck-green-after-shortcut-fix.log`, `03-selfcheck-mutated-checks.log`,
`04-selfcheck-final-green.log`); `docs/USER_GUIDE.md`; `src/app/command_registry.{h,cpp}`,
`app_shell.cpp`, `main.cpp`; `src/workspace/diagnostics/diagnostics_integration.cpp`;
`src/editor/kernel/lsp_actions.cpp`; `src/editor/kernel/finder`/`multi_cursor.cpp`;
`evidence/ux-batch-2/u6-final/README.md` + `logs/04-settings-selftest.log`;
`evidence/ux-batch-2/u8-final/{qt6-lane,selfcheck,docs,lrelease,rust-gates}.log`;
`evidence/ux-batch-2/u8-repair-37/README.md`.

New evidence produced by this review (under `evidence/ux-batch-2/review-u3-final-r2/`):

| File | Content |
|---|---|
| `01-registry-bindings-from-gate.log` | the full binding dump from the FINAL gate selfcheck (`COMMAND-BINDINGS count=23`, every `COMMAND-BINDING` line, `commands.no-duplicate-shortcuts PASS`, menu checks PASS, `APP-SHELL-SELFTEST ok=1 checks=318 failures=0`) |
| `probe_duplicate_shortcut.cpp`, `02-duplicate-detector-probe.log` | controlled probe: two commands registered with the same `Ctrl+Shift+F` → `duplicateShortcuts()` returns the pair and `CommandRegistry::add` emits the duplicate warning (`DETECTOR FIRES`, exit 0) |
| `probe_ktexteditor_shortcuts.cpp`, `03-ktexteditor-shortcuts.log` | a real KTextEditor `Document`/`View` enumerated: 12 actions with shortcuts, `KTEXTEDITOR_HAS_CTRL_SHIFT_F=0`, `KTEXTEDITOR_HAS_CTRL_ALT_F=0` |
| `04-lupdate-extraction-check.log` | `lupdate` in the pinned image DOES extract the new U6 status strings (proves the catalogue gap is a missed lupdate run, not an unextractable string) |
| `05-lupdate-current-sources.ts`, `07-catalogue-delta.txt` | current extraction (1187 sources) vs the shipped `translations/vedit_zh_CN.ts` (1185): 5 user-visible sources missing, 3 stale |
| `06-docs-gate-own-run.log` | my own `bash build/gates/docs.sh` → PASS |

## Assessment

**Risk level: LOW (the repair) / MEDIUM (one integration gap). Verdict: PASS.**

All four round-1 findings are closed and independently reproduced. The registry now has 23
bindings and **no two share a sequence**; the old collision is gone (`diagnostics.format` =
`Ctrl+Shift+F`, `view.panel.file-tree` = `Ctrl+Alt+F`), and the repair's own red log plus my
controlled probe both show the duplicate detector firing on a real duplicate. Every row of the
two keyboard tables re-derives to a registry command (or a documented GTK3 difference) — no row
claims a nonexistent shortcut, and the §14 "rename preview" leftover is now "F2 applies". The
§15.3/§9 lint statements match U6's shipped code and its 240-check settings run, not just the
writer's prose. One new cross-lane gap surfaced while checking those statements: the shipped
translation catalogue was not re-extracted after U6/B2-r5 changed user-visible strings, so five
Chinese UI strings are missing (the zh UI falls back to English for them) and the manual quotes
one of them as if it were rendered. That is the plan §5.1 final `lupdate` step, not a defect of
this repair's changes; the captain should close it before release. The T38 probe-source move is
sound and properly recorded; only stale references remain around it.

## Findings

### U3r2-F1 — Medium (cross-lane integration): the shipped translation catalogue is stale — five user-visible strings are untranslated, one of them quoted by the manual

- What: `translations/vedit_zh_CN.ts` (mtime 11:25) still carries the pre-U6 sentences. Running
  `lupdate` over `src/` extracts 1187 sources, the shipped catalogue has 1185; the delta is
  **5 missing + 3 stale** (`07-catalogue-delta.txt`):
  - `Active engine. --ruleset, --rules, --rules_config and --waiver_files are applied to the lint job; --autofix is stored only (the lint job's C ABI has no field for it).`
  - `Active engine. -W and --error-limit are applied to the lint job.`
  - `Stored but not applied yet (the lint job's C ABI has no field for them): %1. …`
  - the two B2-r5 `Replace refused: …` messages (truncated preview / whole-line proof).
  The stale rows are the old `Active engine. --rules_config is applied; …` pair and the old
  pending hint.
- Where: `src/workspace/settings/settings_panel.cpp` (`refreshLintKnobStates`, the two status
  labels and the pending hint), `src/workspace/search/search_panel.cpp` (the two refusal
  messages) vs `translations/vedit_zh_CN.ts`. The gate's `lrelease` cannot see this: missing
  entries are not "unfinished" (1209 finished / 0 unfinished still passes).
- Why it matters: in the zh UI those five strings render as English source text; the manual
  §15.3 quotes the autofix status line in Chinese ("只保存、不会发送"), which the shipped zh UI
  does not render (it shows the English source, and the matching zh catalogue row is absent).
  The user's item 3 is translation completeness; the plan §5.1 step 1 requires the final
  `lupdate -locations absolute` + `lrelease` after every lane lands.
- Direction: run `lupdate -locations absolute` over `src/`, translate the five new sources
  (translations only; never the source strings), `lrelease` → 0 unfinished, then re-check the
  manual's §15.3 quote. I verified `lupdate` extracts all five (they are not an extraction
  artefact), so this is a bookkeeping step, not a code change.

### U3r2-F2 — Nit: the editor context menu's fallback shortcut for Format is still the old `Ctrl+Alt+F`

- What: `editorActionShortcut(EditorAction::Format)` (`src/editor/kernel/lsp_actions.cpp`)
  still returns `Ctrl+Alt+F`; the context menu uses it only when the wired shell action is not
  resolvable (`editor_context_menu.cpp`, `appendItem` → `shellActionFor`). In the shipped
  product the registry action `diagnostics.format` is found and its `Ctrl+Shift+F` is displayed,
  so the user sees the right key; the fallback is a stale, display-only leftover whose sequence
  is now the Files-panel toggle's. No shortcut binding is created from it.
- Direction: return `Ctrl+Shift+F` there too (or derive the label from the registry action) so a
  degraded/lane-less shell cannot show the wrong key.

### U3r2-F3 — Nit: the §3 format row still describes the GTK3 mechanism ("通过 documentFormatting")

- What: `docs/USER_GUIDE.md` §3 row: `一键格式化 | Ctrl+Shift+F | 通过 documentFormatting，一步撤销即可回退`.
  The shortcut is now correct, but the Qt6 command runs the **external**
  `verible-verilog-format` (`diagnostics_integration.cpp`, `formatDocument` →
  `formatVerilog(state_.toolchain().veribleFormat.path, …)`), which §15.2 itself says; LSP
  `documentFormatting` is the old GTK3 path.
- Direction: say "通过 verible-verilog-format（一次撤销）" or drop the mechanism clause.

### U3r2-F4 — Nit (T38 move): the move is sound, but two stale references were left behind

- What: after the move, `src/workspace/diagnostics/diagnostics_types.h`'s LEGACY comment still
  says the merge helper is kept for "`evidence/w4-lint/lint_orchestration_probe.cpp`" — the live
  consumer is now the lane selftest; and `evidence/w4-lint/README.md`/`run.sh` still describe the
  probe source as living in that evidence directory (the `run.sh` still builds the target by
  name, so it works, but the README's claim about the source is stale).
- Direction: update the comment to the new path and either delete the old copy (captain's call,
  as the U8b note already recommends) or add one line in the old README marking it superseded.

## The shipped default binding table (full list, from the final gate selfcheck)

`COMMAND-BINDINGS count=23`, `commands.no-duplicate-shortcuts PASS`:

```
Ctrl+,        settings.preferences
Ctrl+Alt+D    view.panel.diagnostics
Ctrl+Alt+F    view.panel.file-tree
Ctrl+Alt+J    view.panel.terminal
Ctrl+Alt+N    file.new-layout
Ctrl+Alt+O    view.panel.outline
Ctrl+Alt+R    view.layout.reset
Ctrl+Alt+S    view.panel.search
Ctrl+Alt+V    view.panel.vcs
Ctrl+O        file.open
Ctrl+P        file.goto-file
Ctrl+Q        file.quit
Ctrl+S        file.save
Ctrl+Shift+F  diagnostics.format          ← the repaired command
Ctrl+Shift+H  view.split.horizontal
Ctrl+Shift+S  file.save-as
Ctrl+Shift+V  view.split.vertical
Ctrl+W        file.close-pane
F1            help.user-manual
F12           diagnostics.definition
F2            diagnostics.rename
F6            view.focus.next
Shift+F12     diagnostics.references
```

The previous collision is gone (`Ctrl+Alt+F` = files panel only; format = `Ctrl+Shift+F`).
The repair's red-before log shows the old tree failing exactly there
(`CHECK commands.no-duplicate-shortcuts FAIL Ctrl+Alt+F: diagnostics.format, view.panel.file-tree`),
the green-after log passes; the mutation run makes the new menu checks fail (proving they can).

**Duplicate detector fires (controlled probe).** My probe registers two commands with the same
`Ctrl+Shift+F`: `duplicateShortcuts()` returns `Ctrl+Shift+F: probe.first, probe.second` and
`CommandRegistry::add` emits `duplicate shortcut 'Ctrl+Shift+F' for 'probe.first' and
'probe.second'` (`DETECTOR FIRES`, exit 0). The detector is not dead code.

**The NEW sequence against non-registry bindings.** Checked: (a) the whole repo — `Ctrl+Shift+F`
appears only in `diagnostics_integration.cpp` (the registry command) and comments; (b) the
editor-internal owners: find bar (`Ctrl+F`/`Ctrl+H`/`F3`/`Shift+F3`, `find_replace.cpp`),
editor enhancements (`Ctrl+/`, `Alt+Up/Down`, `Shift+Alt+Down`, `Ctrl+Shift+K/U/L`, `Ctrl+=`,
`Ctrl+-`, `Ctrl+Shift+W`, `multi_cursor.cpp`), FIM (`Alt+\`, Tab, Esc), Files tree
(`Ctrl+Shift+O/N`, `Ctrl+N`, `F2`, `Del`, `F5`, widget-scoped); (c) a real
KTextEditor `Document`/`View` enumerated in the pinned image: 12 actions with shortcuts
(`Ctrl+B`, `Alt+PgDown/Up`, `Alt+Shift+Up/Down`, `Ctrl+Shift+Down/Up`, `Ctrl+Shift+B`, `Ins`,
`Ctrl+Shift+O` spell-check, `Ctrl+8/9`) — **no `Ctrl+Shift+F`, no `Ctrl+Alt+F`**. What I could
not do: enumerate KTextEditor actions that are created lazily on first use (its library carries
no literal shortcut table; the probe found the 12 pre-created ones) — no collision was observed.

## Keyboard-table re-derivation (every row, §3 and §14)

| Table row | Claimed | Registry / evidence | Verdict |
|---|---|---|---|
| §3 实时诊断 | 自动 | no shortcut claimed (auto diagnostics) | OK |
| §3 一键格式化 | `Ctrl+Shift+F` | `diagnostics.format` (dump) | OK (mechanism wording: U3r2-F3) |
| §3 跳转定义 | `F12` | `diagnostics.definition` | OK |
| §3 查找引用 | `Shift+F12` | `diagnostics.references` | OK |
| §3 符号大纲 | `Ctrl+Alt+O` (old GTK3 `Ctrl+Shift+O`) | `view.panel.outline`; GTK3 value in `crates/vd-config/src/model.rs` | OK |
| §3 重命名符号 | `F2`, Qt6 applies / GTK3 previews | `diagnostics.rename`; GTK3 command text "preview … nothing is applied" (`crates/vd-app/src/commands.rs`) | OK |
| §14 跳转到定义 | `F12` | same command | OK |
| §14 查找引用 | `Shift+F12` | same command | OK |
| §14 符号大纲 | `Ctrl+Alt+O` (old GTK3 `Ctrl+Shift+O`) | `view.panel.outline` | OK |
| §14 重命名符号 | `F2`, "会应用" | `diagnostics.rename`; `applyWorkspaceEdit` writes | OK |
| §14 格式化文档 | `Ctrl+Shift+F` | `diagnostics.format` | OK |
| §15.2 编辑器 `Ctrl+Shift+F` = 菜单 Project → 格式化文档 | same command `diagnostics.format` (menu `project`) | dump + zh catalogue "格式化文档" | OK |
| §4 旧 GTK3 `Ctrl+Shift+L`; Qt6 无 slang 独立快捷键 | GTK3 `slang` = `Ctrl+Shift+L` (`model.rs`); Qt6 registry has no `project.lint` sequence (dump); the filter owns `Ctrl+Shift+L` (`multi_cursor.cpp`) | OK |
| §12.1 `Ctrl+F`/`Ctrl+H`; `Ctrl+Alt+S` | find bar owns `Ctrl+F`/`Ctrl+H`/`F3`/`Shift+F3`; `view.panel.search` | OK |

No table row claims a shortcut that does not exist. The "two differences" header is accurate
(outline sequence, rename semantics); format/definition/references are identical in both products.

## U6 statements verified against the shipped code (not the prose)

`docs/USER_GUIDE.md` §15.3/§9 now say: the five engine-supported knobs reach the project-lint
argv; `--autofix` is stored only; rules precedence = page `--rules_config` → legacy tools field →
discovered `.rules.verible_lint` → none. Verified:

- `buildLintJobOptions` (`lint_options.h`) fills `ruleset` / `rules` / `rulesPath` /
  `waiverFiles` for verible and `slangWarnings` / `slangErrorLimit` for slang, with the
  three-step rules precedence; the ABI struct has **no** autofix field
  (`crates/vd-ffi/include/vd_ffi.h`), and `lintKnobState` keeps `Autofix` `TransportPending`.
- `diagnostics_controller.cpp` passes `settings::currentDiscoveredRulesPath()` into the project
  pass; `ffi_backends.cpp` publishes `VdProjectInfo.rules_path`; the project backend composes
  with the same options.
- `evidence/ux-batch-2/u6-final/logs/04-settings-selftest.log`: 240 checks, 0 failures, with
  `argv.ruleset`, `argv.rules`, `argv.waiver-files`, `argv.autofix-not-sent`,
  `argv.slang-warnings`, `argv.slang-error-limit`, `argv.defaults-no-flag`, `argv.two-sets`,
  `argv.discovered-rules-path`, `argv.hand-typed-wins` all PASS.
- The only part of §15.3 that is not true of the zh UI today is the quoted status-line text
  (U3r2-F1): the catalogue lacks the new sentence, so the user sees the English source.

## T38 probe-source move — judgement

**Sound; keep it, clean up the references.** The live test source is now
`src/workspace/diagnostics/selftests/lint_orchestration_probe.cpp`; `src/workspace/diagnostics/CMakeLists.txt`
sets `VEDIT_LINT_PROBE_SOURCE` to it and the ctest id `vedit-lint-orchestration-probe` is
**unchanged** (the gate's build log shows `…/vedit-lint-orchestration-probe.dir/selftests/lint_orchestration_probe.cpp.o`
and the ctest 37/50 pass). The old `evidence/w4-lint/lint_orchestration_probe.cpp` is byte-identical
to its 2026-11-02 state (sha256 `067c5dab…bd636`, mtime 2026-11-02 16:50), is referenced by no
CMake file, and is no longer compiled — it is dead. The move is justified by the repo rule the
writer cites: a living ctest source must not sit under `evidence/`, where editing a closed
artefact is forbidden; the T38 card's write scope (`src/workspace/diagnostics/**`) contains the
new location and the CMake edit, and `u8-repair-37/README.md` §0 records the correction instead of
hiding it. What it costs: the old `evidence/w4-lint/` README/`run.sh` now describe a source that
is no longer built (the recipe builds the *new* test), so the old evidence snapshot is no longer
reproducible from its own directory; the writer left it untouched on purpose and recommends the
captain delete it when consolidating. Should it have been a captain's decision? An explicit
sign-off/board scope note would have been cleaner than a unilateral structural move, but the move
itself is within the write scope, reversible, and honestly recorded — it does not need reverting;
the captain should decide the old copy's deletion and the two stale references (U3r2-F4).

## Declined to judge

- The §2 prose list of editing shortcuts (`Ctrl+N`, `Ctrl+G`, …): not one of the two keyboard
  tables; `Ctrl+N` is the GTK3 "new" accel (`crates/vd-app/src/commands.rs`) and in Qt6 is only
  the widget-scoped Files-tree New File (no registry binding); `Ctrl+G` is a KTextEditor action
  not present among the 12 pre-created actions my probe enumerated. Pre-existing, outside the
  repair's finding set; recommend the docs lane qualify that paragraph by product in a follow-up.
- Deleting the old `evidence/w4-lint/lint_orchestration_probe.cpp` — captain's consolidation call
  (the writer correctly did not edit an older batch's evidence).
- The rest of the 50-test ctest suite and the AppImage package — the final gate is green and the
  captain's packaging chain owns them; I only read its logs.
- The exact wording of every prose sentence in §1–§10 — I re-derived the two keyboard tables and
  the shortcut claims in §2/§4/§12/§15, not the whole manual.

## Bounds of this review

- Used the FINAL gate logs as the shared state: `rust-gates.log` PASS, `qt6-lane.log`
  `[qt6] PASS` with ctest 50/50, `selfcheck.log` `APP-SHELL-SELFTEST ok=1 checks=318 failures=0`,
  `docs.log` PASS, `lrelease.log` 1209 finished / 0 unfinished. The registry dump and menu checks
  are quoted from that selfcheck log.
- My two probes were compiled in the pinned image against source files and an existing build
  tree only to *read* headers; they wrote their binaries/output under `/tmp` inside the image
  (and the sources/logs under the review dir). I did not run `cmake --build`, did not reconfigure,
  and did not touch `target/qt6-build` while the AppImage repackaging was running.
- The catalogue delta is `lupdate` over `src/` into `/tmp` compared with the shipped `.ts`; if the
  captain regenerates the catalogue before release, U3r2-F1 disappears and must be re-checked
  rather than carried.
- The KTextEditor enumeration covers the actions already created on a fresh Document/View; its
  lazily-created actions (find/goto…) were not enumerated, and the library has no literal shortcut
  table to grep.
- Review is against the tree as of 2026-11-06 11:56 (+08:00); the AppImage/package state is U8's.
