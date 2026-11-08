#pragma once

// Application shell (task T13): QMainWindow + panel docks + split editor area +
// menus/shortcuts + theme loading. It is the skeleton the other lanes plug into:
//
//   * editor content  -> EditorProvider seam (src/editor, edit-core)
//   * panel content   -> PanelRegistry factories (workspace/tools/vcs/settings)
//   * key rebinding   -> IShortcutStore seam (settings-ui owns storage + UI)
//   * syntax theme    -> ThemeManager::syntaxThemeName() for the editor lane
//
// The shell never includes KTextEditor: it stays buildable and testable while
// the editor lane is in flight.
#include "i18n.h"

#include <QMainWindow>
#include <QString>
#include <QMetaObject>
#include <QStringList>

#include <functional>
#include <memory>

class QCloseEvent;
class QEvent;
class QLabel;
class QMenu;
class QToolBar;
class QToolButton;

namespace vedit::app {

// The File -> Open name filter, as one testable function instead of a literal
// buried in the dialog call. A filelist (the project recognises the `f`
// extension, case-insensitively) is an openable document: opening one also
// imports it as the project (the project lane subscribes to
// ApplicationShell::fileOpened). The table itself lives in
// src/common/file_dialog_filters.h, so it cannot drift from the parser or from
// the import-filelist dialog. The existing source types stay in front of it, so
// Ctrl+O still offers them first.
QString openFileDialogFilter();

class CommandRegistry;
class EditorArea;
class EditorPane;
class EditorProvider;
class IShortcutStore;
class PanelRegistry;
class SessionStore;
class ThemeManager;

struct ShellOptions {
    QString stateDir;       // empty => QStandardPaths app config location
    QString projectRoot;    // empty => no project (empty state must stay usable)
    QString themeName;      // empty => saved theme, else the default theme
    // UI-Mosaic T3: the product (chrome) theme id, the seam a `--theme-ui` CLI
    // option would fill. Empty means "not forced": settings.toml `uiTheme` wins
    // next, then the session archive, then the contract default (yunwu). The
    // product entry point does not parse such an option yet (main.cpp belongs
    // to the icon-assets lane), so the field is the extension point only.
    QString uiTheme;
    bool restoreSession = true;
    bool showDefaultPanels = true;
    // Defer the session restore (and the no-session defaults) until the caller
    // calls startSession(). A lane registers its panel factory through
    // PanelRegistry::setFactory(), which only affects FUTURE inserts, so a
    // restore that runs before the factories exist leaves every restored dock a
    // shell placeholder (review M1: the terminal and search docks). The product
    // entry point therefore defers, registers the factories, then starts.
    bool deferSessionRestore = false;
};

class ApplicationShell final : public QMainWindow {
    Q_OBJECT

public:
    explicit ApplicationShell(ShellOptions options, QWidget *parent = nullptr);
    ~ApplicationShell() override;

    EditorArea *editorArea() const;
    PanelRegistry *panels() const;
    CommandRegistry *commands() const;
    // T137: the one open path used by positional arguments, File -> Open and
    // (future) recent files. Validates every path before mutating the layout;
    // first file opens in the active pane, each additional file gets a new
    // horizontal split. Returns false with a human-readable *error.
    bool openPaths(const QStringList &paths, QString *error = nullptr);
    // T137 test seam: the default opens QFileDialog; the headless shell
    // selfcheck injects a provider so the File -> Open action itself is
    // exercised without a modal dialog. Clearing it restores QFileDialog.
    void setOpenFileDialogProvider(std::function<QString()> provider);
    ThemeManager *themes() const;

    // T132: the settings lane installs the modal Preferences dialog behind the
    // `Settings -> Preferences...` menu command (Ctrl+,). The shell knows no
    // settings type; it only invokes the opener.
    void setPreferencesOpener(std::function<void()> opener);

    // UI-Mosaic R1/F1: the ONE persistence seam for a USER-initiated product-theme
    // switch (View submenu, toolbar button). The shell cannot include the settings
    // lane (`vedit_workspace_settings` links `vedit_app_shell`, so the dependency
    // points the other way); the product entry point installs the SAME
    // `SettingsClient` write the Preferences page uses. Returning false (with
    // *error) means "applied but not stored" and is reported to the user, never
    // swallowed. A startup application (reportFailure == false) never persists.
    using UiThemePersister = std::function<bool(const QString &id, QString *error)>;
    void setUiThemePersister(UiThemePersister persister);

    // UX batch 2 (item 7): the main toolbar (New/Open/Open Folder/Save/Save All/
    // Undo/Redo + separator + Search/Build/Format). It is created once, then
    // rebuilt from the command registry (it holds the SAME QAction objects, so a
    // toolbar button and its menu entry can never diverge). Movable and
    // hideable; View -> Toolbar toggles it.
    QToolBar *mainToolBar() const;

    // AC-1.1/AC-1.2 (batch 1): the ONE workspace root.
    //
    // The constructor resolves it from the persisted session when --project is
    // absent — i.e. BEFORE any lane registers a panel factory — so an ordinary
    // start already knows the root the panels must show. main.cpp hands that
    // value to installPanels() through IntegrationOptions.
    //
    // File -> Open Folder (and the Files panel's own Open Folder) go through
    // setProjectRoot(): the new root is what the next session archive stores and
    // is broadcast with projectRootChanged, so every LIVE panel rebinds instead
    // of keeping the root it was constructed with.
    QString projectRoot() const;
    void setProjectRoot(const QString &root);

    // Test seam (same pattern as setOpenFileDialogProvider): answers the folder
    // question programmatically, so the headless probe exercises the real
    // File -> Open Folder command without a modal dialog. Clearing it restores
    // QFileDialog::getExistingDirectory.
    void setOpenFolderDialogProvider(std::function<QString()> provider);

    IShortcutStore *shortcutStore() const;
    SessionStore *sessionStore() const;

    // Editor lane hook: the shell creates every new pane with this provider.
    void setEditorProvider(std::unique_ptr<EditorProvider> provider);
    EditorProvider *editorProvider() const;

    bool saveSession(QString *error = nullptr);
    bool restoreSession(QString *error = nullptr);

    // AC-1.4 (batch 1b): the Save As path behind File > Save As and behind Ctrl+S
    // on a document that has no file yet. The shell owns the dialog, the
    // overwrite question and the user-visible result; the write + path adoption
    // happen in the pane (EditorPane::saveAsFile). Returns true only when a file
    // was actually written; a cancelled dialog or a declined overwrite writes
    // NOTHING and returns false.
    bool saveActiveDocumentAs();
    // Test seam (same pattern as setOpenFileDialogProvider): answers the Save As
    // dialog with a path (empty = cancelled) and the overwrite question with a
    // bool. Clearing them restores QFileDialog / QMessageBox.
    void setSaveAsDialogProvider(std::function<QString(const QString &suggestedPath)> provider);
    void setSaveAsOverwriteConfirmProvider(std::function<bool(const QString &path)> provider);

    // T12: what the user answered when a dirty document is being closed.
    //   Save    — write it, then close,  Discard — close as is,  Cancel — keep it.
    // Public because the one-click-restart contract and the lane probes answer
    // the same question; the dialog itself stays private.
    enum class CloseChoice { Save, Discard, Cancel };

    // T18/AC-4.2: the ONE one-click restart path. Strict ordering:
    //   confirmUnsavedDocuments() == false -> abort, write NOTHING (no language
    //                                        write, no session archive, no relaunch)
    //   == true -> persistLanguage(tag) -> saveSession() -> relaunch
    // The i18n code never writes session.json itself; the archive is written by
    // saveSession() (T19 owns that file). A cancelled restart reports the
    // user-visible message "Language not switched: restart cancelled".
    i18n::RestartOutcome restartForLanguageChange(
        const QString &languageTag, const i18n::PersistLanguage &persistLanguage,
        QString *error = nullptr);

    // T18 test seam (same pattern as setOpenFileDialogProvider): answers the
    // unsaved-document question programmatically. It is called EXACTLY ONCE for
    // the whole set of dirty documents, which is how "one dialog for many dirty
    // documents" is asserted without a modal dialog. Clearing restores the
    // real prompts.
    void setUnsavedPromptProvider(
        std::function<CloseChoice(const QStringList &displays, const QStringList &paths)> provider);

    // T19/T18: the one guard a window close AND a one-click restart share. It
    // walks every pane and every tab and returns false when the user cancelled.
    // ONE dirty document keeps the T19 per-document prompt; several dirty
    // documents produce ONE dialog listing all of them (AC-4.2). The restart
    // path MUST call this BEFORE writing the session archive and abort on false —
    // an unsaved buffer is never silently dropped by an automatic restart.
    // Ordering: confirmUnsavedDocuments() -> (false: abort, write nothing)
    //           -> (true: saveSession()) -> restart.
    bool confirmUnsavedDocuments();

    // Saves the document of the ACTIVE pane through the EditorProvider seam
    // (the same call File > Save / Ctrl+S make). Returns false and reports the
    // reason in the status bar, in diagnostics() and through qWarning() when no
    // pane is active or the provider refuses; a failed save never looks like a
    // successful one.
    bool saveActiveDocument();

    // Restores the saved session, or applies the first-start defaults when there
    // is none. The constructor calls it unless ShellOptions::deferSessionRestore
    // asked for it to wait until every panel factory is registered. Idempotent:
    // a second call is a no-op, so a deferred shell can never restore twice.
    void startSession();

    QStringList diagnostics() const;
    /// Append a diagnostic line that is also visible to the user (status bar)
    /// and in `--selfcheck` output. Used by lanes to surface startup problems
    /// such as a missing external toolchain (T82).
    void addShellDiagnostic(const QString &message);
    QString projectLabel() const;

    // Layout presets shared by the View menu, the self-check and the
    // screenshot capture.
    void applyHorizontalSplitPreset(int paneCount);
    void applyVerticalSplitPreset(int paneCount);
    void applyMixedSplitPreset();
    void insertAllPanelsForDemo();

signals:
    // AC-1.2: emitted when the workspace root changes AT RUNTIME (never for the
    // value the shell started with — a lane reads projectRoot() once it exists).
    // An empty string means "no project". Every workspace panel rebinds its live
    // widget here; the root in IntegrationOptions is only the construction-time
    // default.
    void projectRootChanged(const QString &root);

    // ONE lane-agnostic notification, emitted for every successful document
    // open — File > Open and the CLI (openPaths), the Files tree double click,
    // Ctrl+P, a search jump and a project-panel double click all go through
    // EditorPane::openFile, which is where this signal is raised. The path is
    // absolute (the open key); a lane decides what an extension means, so the
    // shell never learns a panel or a project. The project lane uses it to
    // import a `.f` / `.F` filelist as the project; which extensions count is
    // decided by isFilelistPath() in src/common/file_dialog_filters.h — `f`,
    // case-insensitive, so a `.flist` is not a filelist.
    void fileOpened(const QString &absolutePath);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    CloseChoice promptUnsaved(const QString &displayName, const QString &path);
    // Several dirty documents, ONE dialog. Lists every affected document (one
    // line each) and offers Save All / Discard All / Cancel; headless (offscreen)
    // answers Cancel like promptUnsaved() does, never "discard because no dialog
    // could be shown".
    CloseChoice promptUnsavedBatch(const QStringList &displays, const QStringList &paths);
    // T130/T12: every dirty tab of `pane` is offered to the prompt; a Cancel (or
    // a failed save) blocks the pane close. confirmUnsavedDocuments() is the
    // public, all-panes form used by closeEvent() and by the restart path.
    bool confirmPaneClose(EditorPane *pane);

    void buildUi();
    void registerPanels();
    void registerCommands();
    void buildMenuBar();
    // UX batch 2 (item 7): materialise the main toolbar from the command
    // registry. Built in startSession(), after every lane registered its
    // commands, so Build/Format resolve to the diagnostics lane's real actions.
    void buildToolBar();
    // UX batch 2 (item 7): mirror the active editor's own undo/redo availability
    // on the shell's commands (greyed out without an active view / undo step).
    void syncEditorCommandStates();
    // RM-5b: push the registry's EFFECTIVE bindings into the editor area as the
    // shell-owned sequences. Qt triggers neither action of an ambiguous
    // sequence, so the editor's own copies (Ctrl+S, Ctrl+Shift+S, Ctrl+Z,
    // Ctrl+Shift+Z, Ctrl+P …) are removed whenever the registry owns them — and
    // again after every rebinding, which is what makes a user's change effective
    // immediately.
    void syncOwnedShortcuts();
    // UX batch 2 (item 7): the toolbar's Search button — show the search dock and
    // put the caret in its query field (the panel's stable objectName).
    void focusSearchPanel();
    // Help -> User Manual (F1): open the built-in viewer on the embedded guide.
    void openUserManual();
    // File -> Save All: write every modified document that HAS a file. An
    // untitled dirty document is skipped (it needs Save As), never silently
    // dropped.
    bool saveAllDocuments();
    // AC-1.2: the File -> Open Folder command body. Asks the (injectable) folder
    // dialog, validates the answer and calls setProjectRoot().
    void openProjectFolderDialog();
    // AC-1.3 (batch 1b): make sure every dock the shell owns has a View-menu
    // toggle. Two cases the constructor's fixed command list cannot cover: a panel
    // a lane registered in PanelRegistry after the constructor (module tree), and
    // a dock a lane created itself with addDockWidget (the project panel, which
    // is not a registry panel). Called from startSession(), i.e. after the lanes
    // installed and after the dock state was restored.
    void syncDockPanelCommands();
    // AC-1.4: the overwrite question for an existing target. Headless (offscreen)
    // answers NO unless a provider was injected: a save must never silently
    // replace a file it could not ask about.
    bool confirmOverwrite(const QString &path);
    void applyPanelsFromSession(const QStringList &insertedPanels);
    void applyThemeForStartup();
    // UI-Mosaic T6: the startup theme decision in ONE place. The UI theme keeps
    // the T3 precedence (seam > settings.toml > archive > yunwu); the syntax
    // theme is an explicit choice (CLI, then Preferences) or, when the user never
    // made one, the chosen product theme's paired `syntaxTheme`.
    void applyStartupThemes(const QString &archiveSyntaxTheme, const QString &archiveUiTheme);
    // UI-Mosaic T6: a user product-theme switch follows the paired syntax theme
    // unless an explicit choice exists. Never overrides Preferences / --theme.
    void applyPairedSyntaxTheme();
    // UI-Mosaic T3: one product-theme application point. `reportFailure` is true
    // for a user-initiated switch (the status bar says why) and false at startup
    // (a bad persisted id falls back to the default with a diagnostic only).
    void applyProductUiTheme(const QString &id, bool reportFailure);
    // UI-Mosaic R1: persist a USER switch — the settings store (when the product
    // installed the seam) and the session archive, so the three entry points and
    // the startup precedence can never disagree (last writer wins).
    void persistUiTheme(const QString &id);
    // UI-Mosaic R1/M2: one visible line for a persisted id no product theme
    // answers ("treated as unset", never silently replaced by the default).
    void reportUnknownUiTheme(const QString &id, const QString &source);
    // UI-Mosaic R1 test seam: replays a product-theme switch through the real
    // View-menu QAction. Inert unless VEDIT_UI_THEME_SWITCH names an id.
    void applyScriptedThemeSwitch(const QString &id);
    // Builds the SAME six-item, mutually exclusive theme menu for the View
    // submenu and for the toolbar button. Both stay in sync through
    // syncThemeMenuChecks().
    QMenu *createThemeMenu(QWidget *parent, const QString &objectName);
    void syncThemeMenuChecks();
    // Re-colours the toolbar icons after a product-theme change. The colour is
    // read from the CURRENT application palette (matched against productThemes())
    // instead of ThemeManager, because the palette-change event is delivered
    // while applyUiTheme() is still running.
    void refreshToolBarIcons();
    // UI-Mosaic T6: the VEDIT_UI_PROBE=1 runtime probe (transparent icons, real
    // toolbar widths and the KTextEditor status row). Kept in the product because
    // `QIcon::isNull()` cannot observe a rendered-but-empty icon.
    void logUiProbe();
    void updateStatusBar();
    void syncPanelToggleStates();
    void addDiagnostic(const QString &message);

protected:
    // A product theme is applied through the application palette; the toolbar
    // icons must follow it. ApplicationPaletteChange is the Qt 6 replacement of
    // the deprecated QGuiApplication::paletteChanged signal.
    void changeEvent(QEvent *event) override;

private:
    ShellOptions options_;
    std::unique_ptr<EditorProvider> provider_;
    EditorArea *editorArea_ = nullptr; // owned by QMainWindow (central widget)
    std::unique_ptr<PanelRegistry> panels_;
    std::unique_ptr<CommandRegistry> commands_;
    std::function<QString()> openFileDialogProvider_;
    std::function<QString()> openFolderDialogProvider_;
    std::function<QString(const QString &)> saveAsDialogProvider_;
    std::function<bool(const QString &)> overwriteConfirmProvider_;
    // AC-1.3: the dock->action sync connections of the ADOPTED docks (the ones
    // PanelRegistry does not own). They must be dropped before this object's
    // members are destroyed: a QDockWidget emits visibilityChanged while the
    // window's child widgets are being torn down, i.e. AFTER commands_ is gone.
    QList<QMetaObject::Connection> dockToggleConnections_;
    std::unique_ptr<ThemeManager> themes_;
    std::unique_ptr<IShortcutStore> shortcutStore_;
    std::unique_ptr<SessionStore> session_;
    std::function<CloseChoice(const QStringList &, const QStringList &)> unsavedPromptProvider_;
    QLabel *statusProject_ = nullptr;
    QLabel *statusTheme_ = nullptr;
    QLabel *statusLayout_ = nullptr;
    // T12: "● 未保存的更改" — the explicit, always-visible unsaved indicator
    // (AC-8.1); hidden while the active document is clean (AC-8.2).
    QLabel *statusDirty_ = nullptr;
    QStringList diagnostics_;
    std::function<void()> preferencesOpener_;
    // UI-Mosaic R1/F1: installed by the product entry point (src/app/main.cpp)
    // because the settings lane depends on this library, not the other way round.
    UiThemePersister uiThemePersister_;
    // The persister pumps the event loop while the Rust store answers, so a second
    // switch arriving inside that wait must not start a second write.
    bool persistingUiTheme_ = false;
    QToolBar *mainToolBar_ = nullptr;
    // UI-Mosaic T3: the theme entry. It is a SECOND toolbar (instead of an
    // addWidget() on mainToolBar_) because a toolbar widget action also lands in
    // QToolBar::actions(), and ux_selftest compares that list to the shipped
    // order with an ordered equality.
    QToolBar *themeToolBar_ = nullptr;
    QToolButton *themeButton_ = nullptr;
    QMenu *viewThemeMenu_ = nullptr;
    QMenu *themeButtonMenu_ = nullptr;
    bool sessionStarted_ = false;
};

} // namespace vedit::app
