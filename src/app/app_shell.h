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
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <memory>

class QCloseEvent;
class QLabel;

namespace vedit::app {

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
public:
    explicit ApplicationShell(ShellOptions options, QWidget *parent = nullptr);
    ~ApplicationShell() override;

    EditorArea *editorArea() const;
    PanelRegistry *panels() const;
    CommandRegistry *commands() const;
    ThemeManager *themes() const;
    IShortcutStore *shortcutStore() const;
    SessionStore *sessionStore() const;

    // Editor lane hook: the shell creates every new pane with this provider.
    void setEditorProvider(std::unique_ptr<EditorProvider> provider);
    EditorProvider *editorProvider() const;

    bool saveSession(QString *error = nullptr);
    bool restoreSession(QString *error = nullptr);

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

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void registerPanels();
    void registerCommands();
    void buildMenuBar();
    void applyPanelsFromSession(const QStringList &insertedPanels);
    void applyThemeForStartup();
    void updateStatusBar();
    void syncPanelToggleStates();
    void addDiagnostic(const QString &message);

    ShellOptions options_;
    std::unique_ptr<EditorProvider> provider_;
    EditorArea *editorArea_ = nullptr; // owned by QMainWindow (central widget)
    std::unique_ptr<PanelRegistry> panels_;
    std::unique_ptr<CommandRegistry> commands_;
    std::unique_ptr<ThemeManager> themes_;
    std::unique_ptr<IShortcutStore> shortcutStore_;
    std::unique_ptr<SessionStore> session_;
    QLabel *statusProject_ = nullptr;
    QLabel *statusTheme_ = nullptr;
    QLabel *statusLayout_ = nullptr;
    QStringList diagnostics_;
    bool sessionStarted_ = false;
};

} // namespace vedit::app
