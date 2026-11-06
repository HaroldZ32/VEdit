# T15 — editor right-click menu · LSP action channel · F8 / Shift+F8

Board task **T17** (`editor-t15`), cell `cell-20261102T100600176Z`, owner **se-editor**
(Senior Engineer). Frozen contract: `.mpd/cellwork/cell-20261102T100600176Z/contracts/editor-t15.md`.
Acceptance: **AC-3.1** (menu items complete, each with its expected effect, real shell
probe + screenshots) and **AC-3.2** (LSP action channel: definition / references /
rename / format / hover; F8 / Shift+F8 next / previous diagnostic).

Result: **implemented and observed end to end** in a real `ApplicationShell` with a real
KTextEditor view, the real diagnostics lane and a real `verible-verilog-ls` session.
Build `-Werror` green, `ctest` 37/37, product `--selfcheck` 245 checks / 0 failures,
probe 67/67 (offscreen) and 68/68 (Xvfb + xcb). One limitation is stated in §8.

## 1. What changed (all inside the contract's write scope)

| path | change |
|---|---|
| `src/editor/kernel/lsp_actions.h` / `.cpp` | **new** — the LSP action channel and the F8/Shift+F8 navigator |
| `src/editor/kernel/editor_context_menu.h` / `.cpp` | **new** — the editor's right-click menu |
| `src/editor/kernel/ktexteditor_provider.cpp` | installs the three objects on every editor view (3 lines + comment) |
| `src/editor/CMakeLists.txt` | the two new sources join `VEDIT_EDIT_CORE_SOURCES` |
| `evidence/e4-actions/**` | probe, runner scripts, logs, screenshots, this report |

No file outside the granted scopes was written. No test assertion was changed, no gate
was edited, `crates/vd-ffi` was **not** touched (its seam was not needed; see §8).

## 2. Design: one channel, three facts per action

`LspActionChannel` (one instance per view, a child of it, installed by the editor
provider) answers `availability(action)` from three independent facts and **never**
keeps its own copy of a backend capability:

1. **the capability gate the diagnostics lane publishes** — `vedit.diagnostics.gates`
   on the top-level window, a `QVariantMap` of `action -> {enabled, known, source,
   reason}` filled from the C ABI table by `BackendSwitchState::gate()` (T11). The menu
   re-reads it on every right-click.
2. **whether THIS build has a request path** for the action — the four LSP requests the
   diagnostics lane wires as shell commands (`diagnostics.definition`, `.references`,
   `.rename`, `.format`). **hover has none** in this build (§8).
3. **whether the view can serve it now** — a file is open (`vedit.openFilePath`), the
   wired shell action exists and is enabled, and for F8 at least one diagnostic is
   attached.

`dispatch(action)` is the single entry point: it either triggers the wired request path
and writes one `ACTION … dispatched=1` line, or refuses with a reason and writes one
`ACTION-REFUSED … reason='…'` line. Nothing is silent; an action is never `undefined`.

Hover — when a backend does advertise it — is handled by fact 2 and refused with:
`slang(synthetic) advertises hover, but this build has no hover request in the LSP
action channel` (asserted in the probe with an injected supported gate row).

## 3. The menu (AC-3.1)

KTextEditor's **own** automatic context menu does not exist in this product — it is
served by the KatePart plugin container `ktexteditor_popup`, created only by a KTextEditor
plugin host, which vedit does not run. Measured against the v6.24.0 source:
`ViewPrivate::contextMenu()` returns `nullptr` without that container, so
`KateViewInternal::contextMenuEvent()` shows nothing (it calls `view()->contextMenu()`
and bails on null). The kernel therefore owns the menu:

* the standard editing actions come from KTextEditor's documented
  `View::defaultContextMenu(menu)` — the view's own `QAction`s, no second copy;
* a `Language Server` section and a `Diagnostics` section come from the channel;
* the popup is built on every right-click by an event filter on the view's widget tree
  (the kernel's key rule: the real event receiver is `KateViewInternal`, not the public
  `View`), and the object also subscribes to `View::contextMenuAboutToShow` for a
  composition that does provide the KatePart shared menu.

Per-item observed evidence (offscreen log `03-probe.log`, xcb log `04-probe-xcb.log`):

| item | gate (verible) | observed effect |
|---|---|---|
| Go to Definition `F12` | enabled | `ACTION action=definition dispatched=1 via='diagnostics.definition'` → `DEFINITION backend=verible file='…/nav.sv' line=1 column=8` → the recorded jump target receives `nav.sv:1` (the module declaration) |
| Find References `Shift+F12` | enabled | `REFERENCES backend=verible count=1` and the status bar shows `1 reference(s) from verible` |
| Rename Symbol… `F2` | enabled | dialog filled by the probe → `RENAME-PREVIEW backend=verible edits=2 files=0`, status bar `Rename preview: 2 edit(s) from verible` |
| Format Document `Ctrl+Alt+F` | enabled (formatter found) | `FORMAT backend=verible formatter=…/verible-verilog-format changed=1 in-bytes=59 out-bytes=73`; the document text differs after the action |
| Show Hover | **greyed** | tooltip `hoverProvider = false in the initialize result (measured)`; dispatch → `ACTION-REFUSED action=hover … reason='hoverProvider = false …'` |
| Next / Previous Diagnostic | greyed on a clean file, **enabled** on a file with 3 errors | `no diagnostics in this file` vs. enabled with `F8` / `Shift+F8` |

Screenshots (real popup, xcb platform):

* `10-context-menu-xcb.png` — right-click on `nav.sv`: Undo/Redo/Cut/Copy/Paste/Select
  All/Bookmarks (KTextEditor) + **Language Server** (Definition, References, Rename,
  Format, greyed **Show Hover**) + **Diagnostics** (greyed Next/Previous).
* `12-menu-diagnostics-enabled-xcb.png` — the same menu on `broken.sv` (3 errors):
  **Next Diagnostic F8 / Previous Diagnostic Shift+F8 are enabled**, Hover stays greyed.
* `10-context-menu.png`, `11-f8-diagnostic.png`, `12-menu-diagnostics-enabled.png` — the
  same assertions under `QT_QPA_PLATFORM=offscreen`.

The real-event proof is not only the screenshot: the probe sends a `QContextMenuEvent`
of type `Mouse` to the widget under the cursor — `OBSERVED menu.event-target=KateViewInternal`
— and asserts the menu the editor produced: `menu.real-right-click-opens-menu
handled=1 menu=1`, `menu.real-menu-has-default-actions items=27` (xcb: 28),
`menu.real-menu-has-our-items`, and under xcb `menu.real-popup-visible`.

## 4. F8 / Shift+F8 (AC-3.2)

* The filter is installed on the view's widget tree and claims `ShortcutOverride` for
  `Key_F8` exactly (the kernel's key rule), so the key is not stolen by a `QAction`.
* The data are the view's inline diagnostics — `DiagnosticsAnnotations::forView(view)`
  (the producer is the diagnostics lane's `AnnotationBridge`), ordered by
  `(line, column)`; the selection rule is a pure function (`nextDiagnostic` /
  `previousDiagnostic`) with wrap-around in both directions.
* Effects: the cursor moves to the diagnostic, the line is scrolled into view when it
  was off-screen, the status bar shows `Diagnostic 2/3 - error: …`, and one `DIAG-NAV …`
  machine line is printed. With no diagnostics the cursor does **not** move and the
  status bar says `No diagnostics in this file`.

Observed (real key events on the widget that really has focus):

```
ok   f8.rule-ordering / f8.rule-next / f8.rule-next-wraps / f8.rule-previous / f8.rule-previous-wraps / f8.rule-empty-no-move
ok   f8.key-next-lands-on-diagnostic - cursor=1:10 expected=1:10
ok   f8.key-next-advances - cursor=2 expected=2
ok   f8.key-shift-previous - cursor=1 expected=1
ok   f8.navigator-steps - 3
ok   f8.empty-no-move - cursor=1:10
ok   f8.empty-status-message
DIAG-NAV action=next count=3 index=0 from=0:0 to=1:10 wrapped=0 severity=error message='syntax error at "b"'
```

`11-f8-diagnostic-xcb.png` shows the result: `broken.sv` with 3 gutter errors, the
caret on line 2 column 11, status bar `Diagnostic 1/3 - error: syntax error at "b"`, and
the backend badge `LSP backend=verible · lint=slang · live`.

## 5. Commands and observed exit codes

All commands run in the pinned image `vedit-qt6-build:local` from the workspace root.

| command | observed |
|---|---|
| `cmake --build target/qt6-build --parallel $(nproc)` (kernel target carries `-Wall -Wextra -Wpedantic -Werror`) | `BUILD_EXIT=0` — `evidence/e4-actions/01-build.log`, `01-build-exit.txt` |
| `ctest --output-on-failure` in `target/qt6-build` | `CTEST_EXIT=0`, `100% tests passed, 0 tests failed out of 37` — `02-ctest.log`, `02-ctest-exit.txt` |
| `QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/e4-selfcheck` | exit `0`; `EDIT-CORE-SELFTEST ok=1 checks=67 failures=0` + `APP-SHELL-SELFTEST ok=1 checks=245 failures=0` — `05-selfcheck.log`, `05-selfcheck-exit.txt` |
| `bash evidence/e4-actions/probe/build.sh` | `PROBE_BUILD_OK` — `06-probe-build.log` |
| `bash evidence/e4-actions/probe/run.sh` (offscreen) | exit `0`, `SUMMARY checks=67 failures=0 skips=0` — `03-probe.log` |
| `bash evidence/e4-actions/probe/run-xcb.sh` (Xvfb + xcb, `--require-popup`) | exit `0`, `SUMMARY checks=68 failures=0 skips=0` — `04-probe-xcb.log` |

### Reproduce

```bash
cd /home/haroldzhao/MyProj/DshProj/VEdit
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash -lc 'cmake --build /vedit-build/target/qt6-build --parallel $(nproc)'
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash -lc 'bash evidence/e4-actions/probe/build.sh'
bash evidence/e4-actions/probe/run.sh          # offscreen: 67 checks
bash evidence/e4-actions/probe/run-xcb.sh      # Xvfb+xcb: 68 checks, real popup + screenshots
```

The probe (`evidence/e4-actions/probe/editor_actions_probe.cpp`) is an out-of-tree
binary linked from ninja's own `src/app/vedit-app` compile/link lines — the same product
static libraries and Qt6/KF6 set (repository probe convention, `AGENT.md` §4), plus
`Qt6Test` for the key events. It creates a temporary project (`nav.sv`, `format.sv`,
`broken.sv`), starts the real diagnostics lane with the local verible checkout
(`VEDIT_VERIBLE_LS_DIR`, `VEDIT_VERIBLE_FORMAT` = `.mpd/recon/verible/bin/...`), opens
the files in the real provider and drives the menu, the actions and the keys.

## 6. Not proven / limits (read before judging)

1. **Hover has no request path in this build.** `crates/vd-lsp`'s `SessionRequest` has
   definition / references / documentSymbol / rename / formatting / diagnostics — no
   hover — and `crates/vd-ffi` can only forward those. Both crates are **outside** this
   task's write scope (`crates/vd-ffi` was granted, `crates/vd-lsp` and the diagnostics
   controller were not), so hover cannot be made to talk to the server here. The item is
   present, and it is greyed with an explicit reason — the backend's own reason under
   verible, and `no hover request in the LSP action channel` under a backend that
   advertises it. This satisfies "an unsupported action is greyed with its reason, never
   silently undefined"; it does **not** satisfy "hover works against slang". Landing it
   needs a `SessionRequest::Hover` + ABI kind in `crates/vd-lsp`/`vd-ffi` and a
   `requestHover` entry point in `src/workspace/diagnostics`.
2. **The jump lands in the process-wide registry, not in a pane.** The product's real
   `IJumpTarget` (open the target file and move the caret) is T20's remaining item; the
   probe installs a recording target — the same convention the T37 module-tree probe
   uses — and asserts the exact `file:line` the definition action produced. The
   `DEFINITION …` line proves the LSP answer; the recording target proves the jump call.
3. **Formatter gating needs the lane's settings pass.** The formatting row is enabled by
   `state_.toolchain().veribleFormat.found()`, which only gets filled when the
   diagnostics lane's `SettingsWatcher` runs `apply()`. On a fresh state directory with
   no `settings.toml` the lane never runs it, so the item starts disabled
   (`verible-verilog-format not found …`) even though the formatter is present. The probe
   writes an empty `settings.toml` to trigger the documented path and waits for the gate
   (`gate.formatting-enabled`); this is an **observation about the diagnostics lane**, not
   a change made here (its files are out of scope).
4. **Platform coverage.** The popup is asserted visible under Xvfb + xcb; under
   `QT_QPA_PLATFORM=offscreen` the menu object and its items are asserted, but no popup
   window is materialised by that platform. No test ran under a real desktop window
   manager (none is available in the image).
5. **No new lane selftest file was added** (the frozen scope lists no `*_selftest.cpp`),
   so the runnable checks live in the product probe. The pure rules (ordering, next /
   previous / wrap / empty) are asserted there as separate checks.

## 7. Evidence index

```
evidence/e4-actions/
  01-build.log 01-build-exit.txt        product build (‑Werror)
  02-ctest.log 02-ctest-exit.txt        ctest 37/37
  05-selfcheck.log 05-selfcheck-exit.txt product --selfcheck (67 + 245 checks)
  03-probe.log                          offscreen probe, 67 checks
  04-probe-xcb.log                      Xvfb+xcb probe, 68 checks, real popup
  06-probe-build.log                    probe build (binary sha256)
  10-context-menu.png / -xcb.png        real right-click menu
  11-f8-diagnostic.png / -xcb.png       post-F8 editor + status bar
  12-menu-diagnostics-enabled.png / -xcb.png  menu with live diagnostics
  probe/build.sh run.sh run-xcb.sh editor_actions_probe.cpp
```
