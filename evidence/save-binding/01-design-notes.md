# Save entry (File > Save / Ctrl+S) — design notes (T71)

## What was missing

T11's Medium: the shell had `EditorPane::saveFile()` (programmatic path only —
proven by the old `editor.pane.save` check) but **no keyboard shortcut and no menu
item**. In the stop-1 acceptance the user edits lines and presses Ctrl+S; nothing
was bound, so the edit could not be saved from the UI.

## What was added (write scope: `src/app/**`)

| file | change |
|---|---|
| `src/app/app_shell.h` | public `bool saveActiveDocument();` (the one entry both paths call) |
| `src/app/app_shell.cpp` | new command `file.save` — text `&Save`, menu `file`, default shortcut `Ctrl+S`, order 15 — plus its handler and `ApplicationShell::saveActiveDocument()` |
| `src/app/main.cpp` | the `--selfcheck` assertions: command/shortcut/menu wiring, demo-provider path, KTextEditor path (shortcut dispatch + menu action), failure visibility, recovery |
| `evidence/save-binding/**` | this evidence |

Nothing else was touched: `src/editor/**` needed no change because the entry goes
through the existing `EditorProvider::saveFile` seam, and the KTextEditor provider
already calls `atomicSaveText` (tmp → fsync → rename → fsync(dir)). The shell
library (`app_shell.h` / `app_shell.cpp`) still does **not** include any
KTextEditor header; only `main.cpp` (which already includes the edit-core provider
header for `makeEditCoreProvider`) adds `<KTextEditor/Document>` to mutate the
document in the self-check.

## How both user paths reach the same code

```
Ctrl+S (QAction shortcut, Qt::WindowShortcut)
File > Save (same QAction in the File menu)
        │
        └── QAction::triggered → CommandRegistry handler
                → ApplicationShell::saveActiveDocument()
                        → EditorArea::activePane()->saveFile(&error)
                                → EditorProvider::saveFile(editor, &error)
                                        ├─ DemoEditorProvider default (QPlainTextEdit, QSaveFile)
                                        └─ EditCoreProvider (KTextEditor document → atomic_save)
```

The command is declared once in `registerCommands()`; `buildMenuBar()` turns every
`file`-menu command into a real `QMenu` item, and `CommandRegistry::add()` binds
the shortcut on the same `QAction`. There is therefore no second save path to
drift: the menu item *is* the shortcut's action.

## Failure visibility

`saveActiveDocument()` reports every failure three ways, and never as success:

* status bar: `Save failed: <provider reason>` (8 s),
* `ApplicationShell::diagnostics()` (also visible to the self-check / reviews),
* `qWarning() << "[app-shell]" ...` in the process log.

On success it shows `Saved <path>` (4 s). A failed save leaves the document
modified (`isModified() == true`) and the file bytes untouched.

## Headless proof (why it is real, not a mock)

* The shortcut is exercised by sending a real `QKeyEvent(QEvent::KeyPress,
  Qt::Key_S, Qt::ControlModifier)` to the shell's window handle, i.e. through Qt's
  shortcut map, not by calling the handler directly.
* The menu path triggers the exact `QAction` found in the File menu of
  `menuBar()`, not a looked-up handler.
* Both run with the **KTextEditor provider installed on a live shell**
  (`makeEditCoreProvider()`), and the assertion is on the bytes on disk, the
  unchanged `currentFilePath()`, and the absence of `*.vedit-tmp-*` leftovers.
* The failure case injects the documented `VEDIT_ATOMIC_FAULT=write` fault into
  the real atomic-save path; the resulting diagnostic text
  `atomic save: injected write failure` proves the save did **not** bypass
  atomic_save.
* Everything runs with `QT_QPA_PLATFORM=offscreen`; no display server is needed.
