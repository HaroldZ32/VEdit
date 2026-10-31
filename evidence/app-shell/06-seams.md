# src/app seams — what the other lanes plug into

The shell (task T13) owns the window, menus, docks, splits, persistence and Qt
palette. It owns **no** editor and **no** panel content. Everything the other
lanes need enters through four seams, all declared under `src/app/`.

## 1. EditorProvider — src/editor (edit-core)

`src/app/editor_provider.h`

```cpp
class EditorProvider {
public:
    virtual QString id() const = 0;            // persisted in the session layout
    virtual QString displayName() const = 0;   // pane header / About
    virtual QWidget *createEditor(QWidget *parent) = 0;
};
```

Register once before the first pane is created (or before opening new panes):

```cpp
auto *shell = new ApplicationShell(options);
shell->setEditorProvider(std::make_unique<KTextEditorProvider>());
```

* The shell creates one editor widget per pane; the provider must return a
  non-null widget whose lifetime the shell manages.
* The provider id is written into every leaf of the saved layout. Restoring a
  layout with a different provider keeps the layout and records a diagnostic
  (`pane pane-3 was saved by provider 'demo', restored with 'ktexteditor'`).
* The shell does **not** link KTextEditor (`readelf -d` NEEDED count for
  KTextEditor is 0, see `03-elf.txt`), so the editor lane chooses its own build
  dependencies. `DemoEditorProvider` is a QPlainTextEdit placeholder used until
  the seam is connected — never the product editor.

## 2. PanelRegistry — workspace / tools / vcs / settings lanes

`src/app/panel_registry.h`

```cpp
panels()->setFactory("file-tree", [] { return new FileTreeWidget(); });
panels()->showPanel("file-tree", shell);   // inserts the dock on demand
```

Registered ids, titles, owners and default dock areas:

| id | title | owner lane | area |
|---|---|---|---|
| `file-tree` | Files | workspace | left |
| `search` | Search | workspace | bottom |
| `terminal` | Terminal | tools | bottom |
| `vcs` | Source Control | vcs | left |
| `settings` | Settings | settings | right |
| `outline` | Outline | editor/syntax | right |

Until a factory is registered the shell inserts a labelled placeholder (empty
state, no crash). `toggle()` inserts on demand and flips visibility; the View
menu and persisted session use the same ids ("on demand" is the default: only
`file-tree` and `outline` are opened at first start).

## 3. IShortcutStore — settings-ui

`src/app/command_registry.h`

```cpp
class IShortcutStore {
public:
    virtual QKeySequence shortcut(const QString &commandId, const QKeySequence &fallback) const = 0;
    virtual void setShortcut(const QString &commandId, const QKeySequence &sequence) = 0;
    virtual void clearShortcut(const QString &commandId) = 0;
    virtual bool save(QString *error) = 0;
    virtual QString location() const = 0;
};
```

The shell applies the effective sequence to every `QAction` at startup and on
`CommandRegistry::refreshShortcuts()`; `JsonShortcutStore` is a plain default
(`<state-dir>/shortcuts.json`). **The rebinding UI and conflict detection belong
to settings-ui**; it implements this interface (or wraps the JSON store) and
calls `refreshShortcuts()` after edits. Command ids are stable and identical to
the action object names (`action-view.split.horizontal` →
`view.split.horizontal`); list them with `CommandRegistry::ids()`.

## 4. ThemeManager — editor/syntax lanes

```cpp
themes()->syntaxThemeName();   // e.g. "Breeze Light" / "Atom One Dark"
```

The shell loads the theme catalogue from `KSyntaxHighlighting::Repository`
(the product's syntax engine) and applies the selected theme as the **Qt
application palette** at startup (window, docks, menus, placeholder editor).
The theme name is also what the editor lane should hand to KTextEditor when it
applies its syntax schema, so both layers agree. An unknown theme name is
recoverable: the palette is left untouched and a diagnostic is recorded (the
settings lane turns this into user-facing diagnostics).

## 5. Session file (no lane owns it; the shell writes it)

`<state-dir>/session.json` (default `QStandardPaths::AppConfigLocation`), atomic
`QSaveFile` writes, restored on the next start:

```json
{ "version": 1, "project": "<root or empty>", "theme": "<name>",
  "geometry": "<base64>", "dockState": "<base64>",
  "panels": ["file-tree", "outline"],
  "editorLayout": { "version": 1, "nextPane": 4, "active": "pane-2",
                    "root": { "type": "split", "orientation": "horizontal", "ratio": 0.5,
                              "first": {"type":"leaf","pane":"pane-2","provider":"demo"},
                              "second": { "...": "..." } } } }
```

Malformed files never block startup (they are reported and ignored; see
`CHECK broken.config.survives`). `--state-dir` points the shell at another
directory (used by tests and by the screenshot capture).

## 6. Build integration

The root `CMakeLists.txt` adds `src/app` automatically when
`src/app/CMakeLists.txt` exists (`-DVEDIT_BUILD_APP_SHELL=ON` is the default);
this lane never edits the root build. Targets:

| target | role |
|---|---|
| `vedit_app_shell` | static library (shell implementation) |
| `vedit-app` | shell executable (`--selfcheck`, `--screenshots`) |
| `vedit-app-shell-selftest` | ctest entry (`vedit-app --selfcheck`, offscreen) |
