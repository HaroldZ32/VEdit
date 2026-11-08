#include "app_shell.h"

#include "command_registry.h"
#include "crash_reporter.h"
#include "desktop_shortcuts.h"
#include "file_dialog_filters.h"

#include "editor_area.h"
#include "editor_provider.h"
#include "help_viewer.h"
#include "panel_registry.h"
#include "path_display.h"
#include "product_theme.h"
#include "session_store.h"
#include "theme_manager.h"
#include "themed_icons.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QDateTime>
#include <QFileInfo>
#include <QFrame>
#include <QImage>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringLiteral>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <QCoreApplication>

namespace vedit::app {

QString openFileDialogFilter()
{
    // RM-16: the table is DATA in src/common/file_dialog_filters.h, together
    // with the filelist extension set the project parser recognises. The old
    // literal here advertised `.flist`, which no producer/consumer of a filelist
    // accepts; the shared table cannot drift from the parser or from the
    // import-filelist dialog again.
    return vedit::common::openFileDialogFilters().join(QStringLiteral(";;"));
}

namespace {

// UI-Mosaic T3: one toolbar command -> one SVG in resources/app_icon.qrc. The
// contract (§3.3) names `settings` as the fallback when a command has no
// dedicated drawing, so no button is left blank and the missing mapping stays
// visible in the report instead of being invented here.
QString toolbarIconName(const QString &commandId)
{
    static const QHash<QString, QString> icons{
        {QStringLiteral("file.new"), QStringLiteral("new")},
        {QStringLiteral("file.open"), QStringLiteral("open")},
        {QStringLiteral("file.open-folder"), QStringLiteral("open")},
        // No dedicated "switch project" drawing yet: the folder-open glyph is
        // the closest true meaning (both choose a project folder).
        {QStringLiteral("file.switch-project"), QStringLiteral("open")},
        {QStringLiteral("file.save"), QStringLiteral("save")},
        {QStringLiteral("file.save-all"), QStringLiteral("save")},
        {QStringLiteral("edit.undo"), QStringLiteral("undo")},
        {QStringLiteral("edit.redo"), QStringLiteral("redo")},
        {QStringLiteral("search.focus"), QStringLiteral("find")},
        {QStringLiteral("project.lint"), QStringLiteral("build")},
        // Contract §3.3 fallback: no formatter glyph is shipped this round.
        {QStringLiteral("diagnostics.format"), QStringLiteral("settings")},
    };
    return icons.value(commandId);
}

// The product theme the application palette currently represents. The palette
// is the ONE source both applyUiTheme() and any independent switch (the
// Preferences combo) go through, so a widget that only sees the palette can find
// the theme tokens without a second registration point. `canvas` is unique per
// theme, which makes the match unambiguous; before any theme is applied the
// contract default is the honest answer.
const ProductTheme &paletteProductTheme()
{
    const QPalette palette = QApplication::palette();
    const QColor window = palette.color(QPalette::Window);
    for (const ProductTheme &theme : productThemes()) {
        if (theme.canvas.rgb() == window.rgb()) {
            return theme;
        }
    }
    return defaultProductTheme();
}

// Reads ONE string pair out of the flat Rust settings store, mirroring the
// language reader in i18n.cpp (the store quotes dotted keys, a hand-written file
// may not). This is not a second TOML parser: every other line is ignored.
QString readSettingsString(const QString &settingsPath, const QString &key)
{
    QFile file(settingsPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QRegularExpression pair(
        QStringLiteral("^\\s*(?:\"%1\"|'%1'|%1)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')\\s*(?:#.*)?$")
            .arg(QRegularExpression::escape(key)));
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QRegularExpressionMatch match = pair.match(line);
        if (match.hasMatch()) {
            return match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
        }
    }
    return {};
}

} // namespace

ApplicationShell::ApplicationShell(ShellOptions options, QWidget *parent)
    : QMainWindow(parent)
    , options_(std::move(options))
    , provider_(std::make_unique<DemoEditorProvider>())
{
    // UI-Mosaic T3: the QSS card stylesheet selects the window by this stable
    // objectName (`QMainWindow#veditMainWindow` is not used today, but the
    // contract names the object so a future selector has one anchor).
    setObjectName(QStringLiteral("veditMainWindow"));
    resize(1280, 820);

    if (options_.stateDir.isEmpty()) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        options_.stateDir = base.isEmpty() ? QDir::homePath() + QStringLiteral("/.config/vedit") : base;
    }
    session_ = std::make_unique<SessionStore>(options_.stateDir + QStringLiteral("/session.json"));
    // AC-1.1 (batch 1): the workspace root must be known BEFORE the lanes
    // register their panel factories — main.cpp calls installPanels() after this
    // constructor and before startSession(), so the ordinary start (no
    // --project) has to resolve the persisted root here. restoreSession() later
    // keeps whatever this line produced and still applies the theme/layout.
    // The value is NOT validated against the filesystem: a root that disappeared
    // must surface as a missing-root state in the panels, never be silently
    // replaced by "no project".
    if (options_.projectRoot.isEmpty() && options_.restoreSession) {
        QString sessionError;
        const QJsonObject archive = session_->load(&sessionError);
        const QString savedRoot = archive.value(QStringLiteral("project")).toString();
        if (!savedRoot.isEmpty()) {
            options_.projectRoot = savedRoot;
        }
    }
    // FIX 1: the ONE root is normalised once, here, for every source (--project
    // through ShellOptions, the restored session above, and setProjectRoot()
    // later). A stable absolute spelling is what makes the shared display rule
    // (src/common/path_display.h) answer "inside the root" correctly for a root
    // that arrived with a trailing slash or a "." segment.
    options_.projectRoot = vedit::paths::normalizedRoot(options_.projectRoot);
    auto shortcutStore = std::make_unique<JsonShortcutStore>(options_.stateDir + QStringLiteral("/shortcuts.json"));
    QString shortcutError;
    shortcutStore->load(&shortcutError);
    if (!shortcutError.isEmpty()) {
        addDiagnostic(shortcutError);
    }
    shortcutStore_ = std::move(shortcutStore);

    panels_ = std::make_unique<PanelRegistry>();
    commands_ = std::make_unique<CommandRegistry>(this);
    commands_->setStore(shortcutStore_.get());
    themes_ = std::make_unique<ThemeManager>();

    editorArea_ = new EditorArea(this);
    editorArea_->setProvider(provider_.get());
    // RM-5b: the command registry owns the shell's key bindings; the KTextEditor
    // view ships its own copies of several of them. Qt drops BOTH actions of an
    // ambiguous sequence, which is how a "working" binding silently stops
    // firing. The registry therefore asks the area for the live editor owners
    // (so the Shortcuts page can name them) and, after every rebinding, the
    // shell pushes the effective sequences back as the editor-excluded set.
    commands_->setForeignShortcutProvider([this]() {
        QList<vedit::app::ForeignShortcut> owners;
        // RM-5c (review M1): the DESKTOP is an owner too. A user assignment to a
        // DE-global sequence would be consumed by the desktop before the app sees
        // it, exactly like an editor-owned sequence: the Shortcuts page must
        // refuse it and name the desktop action, and the product self-check uses
        // the same list to prove no shipped default is DE-reserved.
        for (const DesktopShortcut &shortcut : desktopGlobalShortcuts()) {
            ForeignShortcut owner;
            owner.sequence = QString::fromLatin1(shortcut.sequence);
            owner.owner = tr("desktop: %1").arg(QString::fromLatin1(shortcut.owner));
            owners.append(owner);
        }
        if (editorArea_ == nullptr) {
            return owners;
        }
        for (const QString &line : editorArea_->editorShortcutOwners()) {
            const int tab = line.indexOf(QLatin1Char('\t'));
            if (tab <= 0) {
                continue;
            }
            ForeignShortcut owner;
            owner.sequence = line.left(tab);
            owner.owner = tr("editor: %1").arg(line.mid(tab + 1));
            owners.append(owner);
        }
        return owners;
    });
    commands_->setShortcutApplier([this]() { syncOwnedShortcuts(); });
    // FIX 1: panes render paths (the tab tooltip) with the ONE display rule, so
    // they need the current root — and a root switch repaints them below.
    editorArea_->setDisplayRoot(options_.projectRoot);
    // FIX 2: the single open seam. Every way a document can be opened (File >
    // Open, the CLI, the Files tree, Ctrl+P, a search jump, a project-panel
    // double click) ends in EditorPane::openFile, so this callback is the ONE
    // place the lane-agnostic fileOpened() signal is raised.
    editorArea_->setOnFileOpened([this](const QString &absolutePath) {
        emit fileOpened(absolutePath);
    });
    // UI-Mosaic T3: the editor area is the main card. The plain central widget
    // carries the 8px canvas gap (@gap@ in cards.qss) and the QFrame#veditCard
    // inside is what the stylesheet paints with surface/border/radius tokens.
    // QMainWindow keeps the canvas colour behind both.
    auto *centralCanvas = new QWidget(this);
    centralCanvas->setObjectName(QStringLiteral("veditCentralCanvas"));
    auto *centralLayout = new QVBoxLayout(centralCanvas);
    centralLayout->setContentsMargins(8, 8, 8, 8); // must stay equal to @gap@
    centralLayout->setSpacing(0);
    auto *editorCard = new QFrame(centralCanvas);
    editorCard->setObjectName(QStringLiteral("veditCard"));
    auto *editorCardLayout = new QVBoxLayout(editorCard);
    editorCardLayout->setContentsMargins(0, 0, 0, 0);
    editorCardLayout->setSpacing(0);
    editorCardLayout->addWidget(editorArea_);
    centralLayout->addWidget(editorCard);
    setCentralWidget(centralCanvas);
    editorArea_->setOnLayoutChanged([this]() { updateStatusBar(); });
    editorArea_->setOnActivePaneChanged([this](EditorPane *) {
        updateStatusBar();
        syncPanelToggleStates();
        // UX batch 2 (item 7): the toolbar's Undo/Redo follow the ACTIVE editor.
        syncEditorCommandStates();
    });
    // T12: the tab strip is the per-pane document list. A dirty tab close asks
    // the user (AC-8.3) and closing a pane with dirty tabs asks per document
    // (T130 kept). Both go through ONE prompt so the wording is the same.
    editorArea_->setOnTabCloseRequested([this](EditorPane *pane, int index) {
        if (pane == nullptr) {
            return SaveDecision::Cancel;
        }
        const QString path = pane->tabFilePath(index);
        const QString display =
            path.isEmpty() ? QStringLiteral("untitled") : QFileInfo(path).fileName();
        switch (promptUnsaved(display, path)) {
        case CloseChoice::Save: {
            QString error;
            if (!pane->saveTab(index, &error)) {
                const QString message = error.isEmpty()
                                            ? tr("Save failed: %1").arg(display)
                                            : tr("Save failed: %1").arg(error);
                addDiagnostic(message);
                statusBar()->showMessage(message, 8000);
                return SaveDecision::Cancel;
            }
            // Written here, so the pane must not write a second time.
            return SaveDecision::Discard;
        }
        case CloseChoice::Discard:
            return SaveDecision::Discard;
        case CloseChoice::Cancel:
            break;
        }
        statusBar()->showMessage(
            tr("Close blocked: %1 has unsaved changes").arg(display), 8000);
        return SaveDecision::Cancel;
    });
    // T12: every tab switch, open, close and dirty-flag change repaints the
    // window title and the status bar (one source of truth: the document flag).
    editorArea_->setOnStateChanged([this]() {
        updateStatusBar();
        // UX batch 2 (item 7): a keystroke/undo/save changes whether the editor
        // can undo or redo, so the toolbar buttons are re-synced from the same
        // event that repaints the tab strip.
        syncEditorCommandStates();
    });
    editorArea_->setOnClosePaneRequested([this](EditorPane *pane) {
        return confirmPaneClose(pane);
    });

    buildUi();
    registerPanels();
    registerCommands();
    panels_->setVisibilityListener([this](const QString &id, bool visible) {
        if (commands_ == nullptr) {
            return;
        }
        if (QAction *action = commands_->action(QStringLiteral("view.panel.") + id)) {
            action->setChecked(visible);
        }
    });
    buildMenuBar();
    // UX batch 2 (item 7): until a pane exists there is no active view, so the
    // Undo/Redo commands start greyed out instead of pretending to work.
    syncEditorCommandStates();

    if (!options_.deferSessionRestore) {
        startSession();
    }
}

void ApplicationShell::startSession()
{
    if (sessionStarted_) {
        return;
    }
    sessionStarted_ = true;

    bool restored = false;
    if (options_.restoreSession) {
        QString restoreError;
        restored = restoreSession(&restoreError);
        if (!restoreError.isEmpty()) {
            addDiagnostic(restoreError);
            // T19: a corrupt/partial archive is a VISIBLE event, never a silent
            // reset — the user may have just lost the session they expected back,
            // so the reason stays on screen instead of only in the log. A missing
            // file is the ordinary first start and reports nothing.
            statusBar()->showMessage(
                QCoreApplication::translate("VEdit",
                                            "Session could not be restored (%1) — starting clean")
                    .arg(restoreError),
                15000);
        }
    }
    if (!restored) {
        applyThemeForStartup();
        if (options_.showDefaultPanels) {
            panels_->showPanel(QStringLiteral("file-tree"), this);
            panels_->showPanel(QStringLiteral("outline"), this);
        }
        editorArea_->ensurePane();
    }
    if (editorArea_->paneCount() == 0) {
        editorArea_->ensurePane();
    }
    syncDockPanelCommands();
    syncPanelToggleStates();
    // UX batch 2 (item 7): the menu bar and the main toolbar are materialised
    // AFTER the lanes registered their commands (the Project menu and the
    // Build/Format toolbar buttons belong to the diagnostics lane). Rebuilding
    // unconditionally also covers a lane that only added menu commands, without
    // a new dock.
    buildMenuBar();
    buildToolBar();
    // RM-5c (review M2): the format command's primary is pinned by the editor
    // kernel (Ctrl+Shift+F, one source of truth), so VS Code's Shift+Alt+F is
    // added as an EXTRA default. The command only exists when the diagnostics
    // lane is installed, hence the id lookup.
    commands_->addExtraDefaultShortcut(QStringLiteral("diagnostics.format"),
                                       QKeySequence(QStringLiteral("Shift+Alt+F")));
    // RM-5b: after every lane registered its commands, tell the editor which
    // sequences the shell owns (the editor's own copies must not stay ambiguous).
    syncOwnedShortcuts();
    syncEditorCommandStates();
    updateStatusBar();
    // UI-Mosaic R1 test seam: replay a product-theme switch through the REAL
    // View-menu QAction, so "switch -> restart -> read back" can be proven on the
    // shipped binary without GUI automation (the VEDIT_UI_PROBE precedent). Inert
    // unless the variable names one of the six ids.
    const QString scriptedTheme = qEnvironmentVariable("VEDIT_UI_THEME_SWITCH").trimmed();
    if (!scriptedTheme.isEmpty()) {
        applyScriptedThemeSwitch(scriptedTheme);
    }
}

ApplicationShell::~ApplicationShell()
{
    if (panels_ != nullptr) {
        panels_->setVisibilityListener(nullptr);
    }
    // A QDockWidget emits visibilityChanged while the window tears its children
    // down — after this object's members (commands_, panels_) are destroyed. Drop
    // the adopted-dock sync connections here, exactly like the panel registry
    // clears its visibility listener above, so no handler can touch a dead member.
    for (const QMetaObject::Connection &connection : dockToggleConnections_) {
        QObject::disconnect(connection);
    }
    dockToggleConnections_.clear();
}

EditorArea *ApplicationShell::editorArea() const
{
    return editorArea_;
}

PanelRegistry *ApplicationShell::panels() const
{
    return panels_.get();
}

CommandRegistry *ApplicationShell::commands() const
{
    return commands_.get();
}

bool ApplicationShell::openPaths(const QStringList &paths, QString *error)
{
    if (paths.isEmpty()) {
        return true;
    }
    if (editorArea_ == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot open a file: the editor area is not available");
        }
        return false;
    }
    QStringList absolutePaths;
    absolutePaths.reserve(paths.size());
    for (const QString &raw : paths) {
        const QFileInfo info(raw);
        if (!info.exists()) {
            if (error != nullptr) {
                *error = QStringLiteral("cannot open %1: no such file or directory").arg(raw);
            }
            return false;
        }
        if (!info.isFile()) {
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "cannot open %1: not a regular file").arg(raw);
            }
            return false;
        }
        absolutePaths.append(info.absoluteFilePath());
    }
    EditorPane *pane = editorArea_->activePane();
    if (pane == nullptr) {
        pane = editorArea_->ensurePane();
    }
    for (int i = 0; i < absolutePaths.size(); ++i) {
        if (i > 0) {
            pane = editorArea_->splitActive(Qt::Horizontal);
        }
        if (pane == nullptr) {
            if (error != nullptr) {
                *error = QStringLiteral("cannot open %1: no editor pane is available")
                             .arg(absolutePaths.at(i));
            }
            return false;
        }
        QString paneError;
        if (!pane->openFile(absolutePaths.at(i), &paneError)) {
            if (error != nullptr) {
                *error = paneError.isEmpty()
                             ? QCoreApplication::translate("VEdit", "cannot open %1").arg(absolutePaths.at(i))
                             : paneError;
            }
            return false;
        }
        editorArea_->setActivePane(pane);
    }
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo("open.paths: opened %lld file(s): %s", static_cast<long long>(absolutePaths.size()),
              qPrintable(absolutePaths.join(QStringLiteral(", "))));
    }
    statusBar()->showMessage(tr("Opened %1 file(s)").arg(absolutePaths.size()), 5000);
    updateStatusBar();
    return true;
}

void ApplicationShell::setOpenFileDialogProvider(std::function<QString()> provider)
{
    openFileDialogProvider_ = std::move(provider);
}

void ApplicationShell::setPreferencesOpener(std::function<void()> opener)
{
    preferencesOpener_ = std::move(opener);
}

void ApplicationShell::setUiThemePersister(UiThemePersister persister)
{
    uiThemePersister_ = std::move(persister);
}

ThemeManager *ApplicationShell::themes() const
{
    return themes_.get();
}

QString ApplicationShell::projectRoot() const
{
    return options_.projectRoot;
}

void ApplicationShell::setProjectRoot(const QString &root)
{
    // FIX 1: the same normalisation the constructor applies, so a root selected
    // at runtime (File > Open Folder, the file tree, a `.f` import) is the same
    // stable spelling the display rule compares against.
    const QString normalized = vedit::paths::normalizedRoot(root);
    if (normalized == options_.projectRoot) {
        return;
    }
    options_.projectRoot = normalized;
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo().noquote() << "[shell-root] project root ->"
                          << (normalized.isEmpty() ? QStringLiteral("<none>") : normalized);
    }
    // FIX 1: the tab tooltips are painted from the root, so the panes are
    // rebound BEFORE the title/status bar are repainted from the same value.
    if (editorArea_ != nullptr) {
        editorArea_->setDisplayRoot(options_.projectRoot);
    }
    // The persistent label AND the window title are derived from the root, so
    // they are repainted from the one source of truth (never set twice by hand).
    updateStatusBar();
    statusBar()->showMessage(tr("Project: %1").arg(projectLabel()), 5000);
    // AC-1.2: the lanes rebind their LIVE widgets here. Emitted last, so a
    // receiver already sees the new projectRoot().
    emit projectRootChanged(options_.projectRoot);
}

void ApplicationShell::setOpenFolderDialogProvider(std::function<QString()> provider)
{
    openFolderDialogProvider_ = std::move(provider);
}

void ApplicationShell::openProjectFolderDialog()
{
    const QString start = options_.projectRoot.isEmpty() ? QDir::homePath() : options_.projectRoot;
    const QString directory =
        openFolderDialogProvider_
            ? openFolderDialogProvider_()
            : QFileDialog::getExistingDirectory(this, tr("Open Folder"), start);
    if (directory.isEmpty()) {
        return; // cancelled: nothing changes, nothing is persisted
    }
    const QFileInfo info(directory);
    if (!info.exists() || !info.isDir()) {
        const QString message =
            tr("Cannot open %1: not a folder").arg(QDir::toNativeSeparators(directory));
        addDiagnostic(message);
        statusBar()->showMessage(message, 8000);
        return;
    }
    setProjectRoot(info.absoluteFilePath());
}

IShortcutStore *ApplicationShell::shortcutStore() const
{
    return shortcutStore_.get();
}

SessionStore *ApplicationShell::sessionStore() const
{
    return session_.get();
}

void ApplicationShell::setEditorProvider(std::unique_ptr<EditorProvider> provider)
{
    if (!provider) {
        return;
    }
    // T12: the panes unsubscribe their per-tab listeners from the OLD provider
    // while swapping editor widgets, so it must still be alive at that moment.
    // Assigning provider_ first would destroy it and turn that unsubscribe into
    // a use-after-free (the tabs made this observable: one pane now owns several
    // widgets that all have to be handed over).
    std::unique_ptr<EditorProvider> previous = std::move(provider_);
    provider_ = std::move(provider);
    editorArea_->setProvider(provider_.get());
    // UI-Mosaic F2: the syntax theme is rendered by the EDITOR's own theme
    // support now that a product theme owns the application palette. Wire the
    // one consumer — this shell's ThemeManager — and apply the already-chosen
    // theme immediately: the product start applies the theme before the editor
    // lane swaps the provider, so waiting for the next applyTheme() would leave
    // the first panes on KTextEditor's default theme.
    if (themes_ != nullptr) {
        themes_->setSyntaxThemeApplier([this](const QString &name) {
            if (provider_ != nullptr) {
                provider_->applySyntaxTheme(name);
            }
        });
        provider_->applySyntaxTheme(themes_->syntaxThemeName());
    }
    // `previous` dies here, after every pane has stopped using it.
}

EditorProvider *ApplicationShell::editorProvider() const
{
    return provider_.get();
}

void ApplicationShell::buildUi()
{
    // T12/AC-8.1: the unsaved indicator is the first thing on the left of the
    // status bar, so it is impossible to miss while a document is dirty. It is
    // hidden (not blanked) while the active tab is clean.
    auto *dirty = new QLabel(this);
    dirty->setObjectName(QStringLiteral("status-dirty"));
    dirty->setContentsMargins(6, 0, 6, 0);
    dirty->setStyleSheet(QStringLiteral("color: #e8a33d; font-weight: 600;"));
    dirty->setVisible(false);
    auto *project = new QLabel(this);
    project->setObjectName(QStringLiteral("status-project"));
    auto *theme = new QLabel(this);
    theme->setObjectName(QStringLiteral("status-theme"));
    auto *layout = new QLabel(this);
    layout->setObjectName(QStringLiteral("status-layout"));
    // A PERMANENT widget on purpose: a transient status message (e.g. "Opened 3
    // file(s)") hides the temporary area, and the unsaved indicator must never
    // be hidden by a message that has nothing to do with it.
    statusBar()->addPermanentWidget(dirty);
    statusBar()->addWidget(project);
    statusBar()->addPermanentWidget(theme);
    statusBar()->addPermanentWidget(layout);
    statusDirty_ = dirty;
    statusProject_ = project;
    statusTheme_ = theme;
    statusLayout_ = layout;
}

void ApplicationShell::registerPanels()
{
    const auto addPanel = [this](const QString &id, const QString &title, const QString &owner,
                                 Qt::DockWidgetArea area) {
        PanelSpec spec;
        spec.id = id;
        spec.title = title;
        spec.owner = owner;
        spec.area = area;
        // No factory yet: the owning lane registers one through
        // PanelRegistry::setFactory(id, ...); until then the registry inserts
        // the shell placeholder (empty state, never a crash).
        panels_->addPanel(spec);
        panels_->setPlaceholderHint(
            id, options_.projectRoot.isEmpty()
                    ? QCoreApplication::translate("VEdit", "Empty state: no project open. This panel stays usable and empty.")
                    : QCoreApplication::translate("VEdit", "Empty state: nothing to show for this project yet."));
    };
    addPanel(QStringLiteral("file-tree"), QCoreApplication::translate("VEdit", "Files"), QStringLiteral("workspace"), Qt::LeftDockWidgetArea);
    addPanel(QStringLiteral("search"), QCoreApplication::translate("VEdit", "Search"), QStringLiteral("workspace"), Qt::BottomDockWidgetArea);
    addPanel(QStringLiteral("terminal"), QCoreApplication::translate("VEdit", "Terminal"), QStringLiteral("tools"), Qt::BottomDockWidgetArea);
    addPanel(QStringLiteral("vcs"), QCoreApplication::translate("VEdit", "Source Control"), QStringLiteral("vcs"), Qt::LeftDockWidgetArea);
    // T132: settings are a modal Preferences dialog (Settings menu / Ctrl+,), not
    // a dock panel — a dock competed with the editor and read like an ordinary
    // panel. The settings lane installs the dialog through setPreferencesOpener().
    addPanel(QStringLiteral("outline"), QCoreApplication::translate("VEdit", "Outline"), QStringLiteral("editor/syntax"), Qt::RightDockWidgetArea);
}

void ApplicationShell::registerCommands()
{
    const auto addCommand = [this](const QString &id, const QString &text, const QString &menu,
                                   const QKeySequence &shortcut, int order, bool checkable = false,
                                   bool separatorBefore = false) {
        CommandSpec spec;
        spec.id = id;
        spec.text = text;
        spec.menu = menu;
        spec.defaultShortcut = shortcut;
        spec.order = order;
        spec.checkable = checkable;
        spec.separatorBefore = separatorBefore;
        commands_->add(spec);
    };

    // UX batch 2 (item 7): the menu bar is FIXED in this order — File, Edit,
    // View, Project, Help — regardless of the order lanes register their
    // commands in (buildMenuBar() below sorts against kMenuOrder). The old
    // top-level Settings menu is gone: Preferences now lives at the BOTTOM of
    // Edit, which is where every desktop editor puts it.
    commands_->setMenuLabel(QStringLiteral("file"), tr("&File"));
    commands_->setMenuLabel(QStringLiteral("edit"), tr("&Edit"));
    commands_->setMenuLabel(QStringLiteral("view"), tr("&View"));
    commands_->setMenuLabel(QStringLiteral("project"), tr("&Project"));
    commands_->setMenuLabel(QStringLiteral("help"), tr("&Help"));

    // Edit (UX batch 2 item 7): Undo/Redo are real commands. The handler drives
    // the ACTIVE editor's own action (KTextEditor's KStandardAction), so the
    // menu/toolbar entry and the editor context menu do the same thing.
    // RM-5b: they now CARRY the VS Code bindings. The editor's own
    // edit_undo/edit_redo actions also carry Ctrl+Z and Ctrl+Shift+Z, which Qt
    // would treat as ambiguous (both dropped); the shell's authority removes the
    // editor's copies (EditorPane::setOwnedShortcuts), so exactly one action —
    // this command — owns the key and the command drives the editor's undo slot.
    addCommand(QStringLiteral("edit.undo"), tr("&Undo"), QStringLiteral("edit"),
               QKeySequence(QStringLiteral("Ctrl+Z")), 10);
    addCommand(QStringLiteral("edit.redo"), tr("&Redo"), QStringLiteral("edit"),
               QKeySequence(QStringLiteral("Ctrl+Shift+Z")), 20);
    // RM-5c (review M2): a VS Code user on Linux/Windows also presses Ctrl+Y for
    // redo. It is an EXTRA default (the primary stays Ctrl+Shift+Z); a user
    // override replaces the whole set, and the Shortcuts page lists both.
    commands_->addExtraDefaultShortcut(QStringLiteral("edit.redo"),
                                       QKeySequence(QStringLiteral("Ctrl+Y")));
    // Preferences (T132): the modal Preferences dialog, moved here from the
    // removed Settings menu. Ctrl+, is unchanged.
    addCommand(QStringLiteral("settings.preferences"), tr("&Preferences…"),
               QStringLiteral("edit"), QKeySequence(QStringLiteral("Ctrl+,")), 100);

    // File
    // UX batch 2 (item 7): New is a fresh empty document (not "New Empty
    // Layout", which keeps its own command).
    addCommand(QStringLiteral("file.new"), tr("&New"), QStringLiteral("file"),
               QKeySequence(QStringLiteral("Ctrl+N")), 1);
    addCommand(QStringLiteral("file.open"), tr("&Open…"), QStringLiteral("file"),
               QKeySequence(QStringLiteral("Ctrl+O")), 5);
    // AC-1.2 (batch 1): the user-facing entry that switches the workspace root
    // at runtime. It is a real File command (not only the Files panel toolbar
    // button) so the one broadcast path is reachable from the menu bar too.
    // RM-2: `file.switch-project` below is the same reset path under the name
    // the user asked for mid-session; both are in the File menu and toolbar.
    // RM-5b: VS Code uses Ctrl+K Ctrl+O; a two-stroke sequence is awkward in an
    // editable one-field shortcut UI, so the free single-stroke form is used.
    addCommand(QStringLiteral("file.open-folder"), tr("Open F&older…"), QStringLiteral("file"),
               QKeySequence(QStringLiteral("Ctrl+Shift+Alt+O")), 6);
    // RM-2: the user's "switch project from the top" entry. Mid-session the
    // action is a SWITCH, and a top-level label that says so was the missing
    // part; it opens the same folder dialog and goes through the same reset
    // path as Open Folder (`openProjectFolderDialog()` -> `setProjectRoot()`,
    // which re-broadcasts `projectRootChanged` to every lane). It is a real
    // File command AND a main-toolbar button (see `kToolBarOrder`).
    addCommand(QStringLiteral("file.switch-project"), tr("S&witch Project…"),
               QStringLiteral("file"), QKeySequence(QStringLiteral("Ctrl+Shift+Alt+P")), 7);
    addCommand(QStringLiteral("file.new-layout"), tr("&New Empty Layout"),
               QStringLiteral("file"), QKeySequence(QStringLiteral("Ctrl+Alt+N")), 10);
    // Save (T71): the entry the editor is actually used through. It goes to the
    // active pane's provider seam, so the real KTextEditor document is saved
    // atomically by the edit-core lane; the demo provider keeps working through
    // its own default implementation of the same seam.
    addCommand(QStringLiteral("file.save"), tr("&Save"), QStringLiteral("file"),
               QKeySequence(QStringLiteral("Ctrl+S")), 15);
    // AC-1.4 (batch 1b): Save As is the entry a document WITHOUT a file needs;
    // Ctrl+S on an untitled tab goes through the same path (saveActiveDocument).
    addCommand(QStringLiteral("file.save-as"), tr("Save &As…"), QStringLiteral("file"),
               QKeySequence(QStringLiteral("Ctrl+Shift+S")), 16);
    // UX batch 2 (item 7): one write for every modified document that has a
    // file. Untitled dirty documents need Save As and are reported, not dropped.
    // RM-5b: VS Code's Save All is Ctrl+K S; this is the free single-stroke
    // sibling of Ctrl+S / Ctrl+Shift+S that keeps the Ctrl+Shift+Alt family.
    addCommand(QStringLiteral("file.save-all"), tr("Save A&ll"), QStringLiteral("file"),
               QKeySequence(QStringLiteral("Ctrl+Shift+Alt+S")), 17);
    addCommand(QStringLiteral("file.save-session"), tr("&Save Session Now"),
               QStringLiteral("file"), QKeySequence(), 20);
    // T130: the user-facing entry lives in View. The id stays stable so an
    // existing shortcut-store key is not invalidated.
    addCommand(QStringLiteral("file.close-pane"), tr("&Close Pane"),
               QStringLiteral("view"), QKeySequence(QStringLiteral("Ctrl+W")), 60);
    addCommand(QStringLiteral("file.quit"), tr("&Quit"), QStringLiteral("file"),
               QKeySequence(QStringLiteral("Ctrl+Q")), 90, false, true);

    // View: layout operations
    // RM-5b: Ctrl+Shift+H belonged to KTextEditor's "Find Selected Backwards";
    // two actions with one sequence are BOTH dropped by Qt, so this default was
    // dead. Ctrl+Alt+H is free (measured against the live editor's action table,
    // evidence/ux-batch-2/rm5b-shortcuts/probe/log-kte-action-dump.txt).
    addCommand(QStringLiteral("view.split.horizontal"), tr("Split Pane &Horizontally"),
               QStringLiteral("view"), QKeySequence(QStringLiteral("Ctrl+Alt+H")), 10);
    addCommand(QStringLiteral("view.split.vertical"), tr("Split Pane &Vertically"),
               QStringLiteral("view"), QKeySequence(QStringLiteral("Ctrl+Shift+V")), 20);
    addCommand(QStringLiteral("view.focus.next"), tr("&Focus Next Pane"),
               QStringLiteral("view"), QKeySequence(QStringLiteral("F6")), 30);
    addCommand(QStringLiteral("view.layout.mixed"), tr("&Mixed Split (demo)"),
               QStringLiteral("view"), QKeySequence(QStringLiteral("Ctrl+Alt+M")), 40);
    addCommand(QStringLiteral("view.layout.reset"), tr("&Reset Layout"),
               QStringLiteral("view"), QKeySequence(QStringLiteral("Ctrl+Alt+R")), 50, false, true);
    // UX batch 2 (item 7): the toolbar is hideable from the menu (its own
    // toggleViewAction is not part of the command registry, so the menu entry is
    // declared here and kept in sync through QToolBar::visibilityChanged).
    // RM-5c (review M1): Ctrl+Alt+T is the GNOME/Ubuntu "open terminal" global
    // shortcut, so the desktop would consume it before us; Ctrl+Shift+Alt+T is
    // in the family neither the desktop nor KTextEditor claims.
    addCommand(QStringLiteral("view.toolbar"), tr("&Toolbar"), QStringLiteral("view"),
               QKeySequence(QStringLiteral("Ctrl+Shift+Alt+T")), 55, /*checkable=*/true);
    // UX batch 2 (item 7): the toolbar's Search button opens the panel AND puts
    // the caret in the query field. It is a distinct id from the panel toggle so
    // a click can never mean "hide the search panel".
    // RM-5b: Ctrl+Shift+F is already the pinned default of `diagnostics.format`
    // (single source: src/editor/kernel/lsp_actions.cpp editorActionShortcut();
    // changing it is a cross-lane contract change), so the next free
    // project-search sequence is used and the deviation is documented.
    addCommand(QStringLiteral("search.focus"), tr("Search in &Files"), QStringLiteral("view"),
               QKeySequence(QStringLiteral("Ctrl+Shift+Alt+F")), 90);

    // RM-5c (review M2): the user's Ctrl+Tab / Ctrl+Shift+Tab had no command at
    // all. They are REAL commands now, in the View menu next to Focus Next Pane.
    addCommand(QStringLiteral("view.tab.next"), tr("&Next Tab"), QStringLiteral("view"),
               QKeySequence(QStringLiteral("Ctrl+Tab")), 35);
    addCommand(QStringLiteral("view.tab.previous"), tr("Pre&vious Tab"), QStringLiteral("view"),
               QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), 36);

    // UX batch 2 (item 7): the five panel toggles are translated. The struct is
    // initialised with tr() directly, so lupdate extracts every title.
    const struct {
        const char *id;
        QString title;
        const char *shortcut;
    } panelCommands[] = {
        {"file-tree", tr("To&ggle Files Panel"), "Ctrl+Alt+F"},
        {"search", tr("To&ggle Search Panel"), "Ctrl+Alt+S"},
        {"terminal", tr("To&ggle Terminal Panel"), "Ctrl+Alt+J"},
        // RM-5b: Ctrl+Alt+V is KTextEditor's input-mode switch (ambiguous, both
        // dead); Ctrl+Alt+C is free and keeps the Ctrl+Alt panel family.
        {"vcs", tr("To&ggle Source Control Panel"), "Ctrl+Alt+C"},
        {"outline", tr("To&ggle Outline Panel"), "Ctrl+Alt+O"},
    };
    int order = 100;
    for (const auto &panelCommand : panelCommands) {
        const QString panelId = QString::fromLatin1(panelCommand.id);
        const QString commandId = QStringLiteral("view.panel.") + panelId;
        const bool separatorBefore = order == 100;
        addCommand(commandId, panelCommand.title, QStringLiteral("view"),
                   QKeySequence(QString::fromLatin1(panelCommand.shortcut)), order,
                   /*checkable=*/true, separatorBefore);
        ++order;
        commands_->setHandler(commandId, [this, commandId]() {
            const QString id = commandId.mid(QStringLiteral("view.panel.").size());
            const bool visible = panels_->toggle(id, this);
            if (QAction *action = commands_->action(commandId)) {
                action->setChecked(visible);
            }
            updateStatusBar();
        });
    }

    commands_->setHandler(QStringLiteral("settings.preferences"), [this]() {
        if (preferencesOpener_) {
            preferencesOpener_();
        }
    });

    // Help
    // UX batch 2 (item 11): the built-in reader for docs/USER_GUIDE.md, reachable
    // at F1. The Markdown is embedded as a Qt resource (resources/help/), so the
    // manual ships in every packaging form with no extra install rule.
    addCommand(QStringLiteral("help.user-manual"), tr("&User Manual"), QStringLiteral("help"),
               QKeySequence(Qt::Key_F1), 5);
    addCommand(QStringLiteral("help.about"), tr("&About VEdit"), QStringLiteral("help"),
               QKeySequence(), 10, false, true);

    // Handlers that are not panel toggles.
    commands_->setHandler(QStringLiteral("file.open"), [this]() {
        const QString start = options_.projectRoot.isEmpty() ? QDir::homePath()
                                                             : options_.projectRoot;
        QString path;
        if (openFileDialogProvider_) {
            path = openFileDialogProvider_();
        } else {
            // RM-16b: an OBJECT dialog configured by the shared table, so the
            // intended filter is selected AND applied to the file view before the
            // dialog is shown - the static one-shot could display a filter the
            // view had not applied yet (the user's "must click the type once").
            QFileDialog dialog(this, QCoreApplication::translate("VEdit", "Open File"), start);
            dialog.setObjectName(QStringLiteral("file-open-dialog"));
            vedit::common::configureOpenFileDialog(dialog);
            if (dialog.exec() == QDialog::Accepted) {
                path = dialog.selectedFiles().value(0);
            }
        }
        if (path.isEmpty()) {
            return;
        }
        QString error;
        if (!openPaths({path}, &error)) {
            addDiagnostic(error);
            statusBar()->showMessage(error, 8000);
        }
    });
    commands_->setHandler(QStringLiteral("file.open-folder"), [this]() {
        openProjectFolderDialog();
    });
    // RM-2: the same reset path, never a second implementation.
    commands_->setHandler(QStringLiteral("file.switch-project"), [this]() {
        openProjectFolderDialog();
    });
    commands_->setHandler(QStringLiteral("file.new-layout"), [this]() {
        // RM-12: resetLayout() deletes every pane AND the document that dies
        // with it (KTextEditor bounds the document to its view). The user must
        // answer the SAME unsaved-document question the window close asks; a
        // cancelled answer leaves every buffer and the layout untouched.
        if (!confirmUnsavedDocuments()) {
            return;
        }
        editorArea_->resetLayout();
        updateStatusBar();
    });
    commands_->setHandler(QStringLiteral("file.save"), [this]() { saveActiveDocument(); });
    commands_->setHandler(QStringLiteral("file.save-as"), [this]() { saveActiveDocumentAs(); });
    commands_->setHandler(QStringLiteral("file.save-session"), [this]() {
        QString error;
        if (!saveSession(&error)) {
            addDiagnostic(error);
        }
        statusBar()->showMessage(tr("Session saved (%1)").arg(session_->path()), 4000);
    });
    commands_->setHandler(QStringLiteral("file.close-pane"), [this]() {
        if (!editorArea_->closeActivePane()) {
            // The guard already surfaced the unsaved-changes message; a blocked
            // close must not look like a successful action.
            updateStatusBar();
            return;
        }
        updateStatusBar();
    });
    commands_->setHandler(QStringLiteral("file.quit"), [this]() {
        // RM-12 family audit: QApplication::quit() ends the process WITHOUT
        // running closeEvent(), so File > Quit / Ctrl+Q was a second silent
        // data-loss path (every dirty buffer died unasked). Ask the SAME
        // unsaved-document question the window close asks before quitting.
        if (!confirmUnsavedDocuments()) {
            return;
        }
        QApplication::quit();
    });
    commands_->setHandler(QStringLiteral("view.split.horizontal"), [this]() {
        editorArea_->splitActive(Qt::Horizontal);
        updateStatusBar();
    });
    commands_->setHandler(QStringLiteral("view.split.vertical"), [this]() {
        editorArea_->splitActive(Qt::Vertical);
        updateStatusBar();
    });
    commands_->setHandler(QStringLiteral("view.focus.next"), [this]() {
        editorArea_->focusNextPane();
        updateStatusBar();
    });
    const auto switchTab = [this](int delta) {
        EditorPane *pane = editorArea_ != nullptr ? editorArea_->activePane() : nullptr;
        if (pane == nullptr || pane->tabCount() < 2) {
            return;
        }
        const int count = pane->tabCount();
        const int index = ((pane->activeTabIndex() + delta) % count + count) % count;
        pane->setActiveTab(index);
        updateStatusBar();
    };
    commands_->setHandler(QStringLiteral("view.tab.next"), [switchTab]() { switchTab(1); });
    commands_->setHandler(QStringLiteral("view.tab.previous"), [switchTab]() { switchTab(-1); });
    commands_->setHandler(QStringLiteral("view.layout.mixed"), [this]() {
        // RM-12: the mixed preset starts from resetLayout(), i.e. it destroys
        // every pane and document exactly like Reset Layout. Same confirmation;
        // a cancelled answer must not even add panes to the current layout.
        if (!confirmUnsavedDocuments()) {
            return;
        }
        applyMixedSplitPreset();
    });
    commands_->setHandler(QStringLiteral("view.layout.reset"), [this]() {
        // RM-12: see file.new-layout above. Cancel keeps every dirty document
        // and the layout byte-for-byte (the check asserts the layout JSON).
        if (!confirmUnsavedDocuments()) {
            return;
        }
        editorArea_->resetLayout();
        updateStatusBar();
        syncEditorCommandStates();
    });
    // UX batch 2 (item 7): File > New — a new empty document in the active pane.
    commands_->setHandler(QStringLiteral("file.new"), [this]() {
        editorArea_->newDocument();
        updateStatusBar();
        syncEditorCommandStates();
    });
    commands_->setHandler(QStringLiteral("file.save-all"), [this]() { saveAllDocuments(); });
    // UX batch 2 (item 7): Undo/Redo drive the ACTIVE editor's own action, so the
    // toolbar button, the Edit menu entry and the editor's context menu are one
    // operation. The command is enabled only while that action can run.
    commands_->setHandler(QStringLiteral("edit.undo"), [this]() {
        editorArea_->triggerActiveEditorAction(
            {QStringLiteral("edit_undo"), QStringLiteral("undo")}, QStringLiteral("undo"));
        updateStatusBar();
        syncEditorCommandStates();
    });
    commands_->setHandler(QStringLiteral("edit.redo"), [this]() {
        editorArea_->triggerActiveEditorAction(
            {QStringLiteral("edit_redo"), QStringLiteral("redo")}, QStringLiteral("redo"));
        updateStatusBar();
        syncEditorCommandStates();
    });
    commands_->setHandler(QStringLiteral("search.focus"), [this]() { focusSearchPanel(); });
    commands_->setHandler(QStringLiteral("help.user-manual"), [this]() { openUserManual(); });
    commands_->setHandler(QStringLiteral("view.toolbar"), [this]() {
        if (mainToolBar_ == nullptr) {
            buildToolBar();
        }
        if (mainToolBar_ == nullptr) {
            return;
        }
        const bool show = !mainToolBar_->isVisible();
        mainToolBar_->setVisible(show);
        if (QAction *action = commands_->action(QStringLiteral("view.toolbar"))) {
            action->setChecked(show);
        }
    });

    const auto about = [this]() {
        QMessageBox::about(
            this, QCoreApplication::translate("VEdit", "About VEdit"),
            QCoreApplication::translate("VEdit", "VEdit application shell (app-shell lane).\n\n"
                           "Editor provider: %1\nTheme: %2\nPanes: %3\nState: %4\n"
                           "Diagnostics: %5")
                .arg(provider_->displayName(), themes_->currentTheme())
                .arg(editorArea_->paneCount())
                .arg(session_->path())
                .arg(diagnostics_.size()));
    };
    commands_->setHandler(QStringLiteral("help.about"), about);
}

void ApplicationShell::buildMenuBar()
{
    // UX batch 2 (item 7): the top-level order is FIXED — File, Edit, View,
    // Project, Help — even though lanes register their commands in whatever
    // order they are installed (the diagnostics lane's Project commands arrive
    // after the constructor). Unknown menu ids keep their registration order
    // after the five canonical ones so a future lane is never silently hidden.
    static const QStringList kMenuOrder{
        QStringLiteral("file"), QStringLiteral("edit"), QStringLiteral("view"),
        QStringLiteral("project"), QStringLiteral("help")};
    QStringList ordered;
    const QStringList registered = commands_->menus();
    for (const QString &menuId : kMenuOrder) {
        if (registered.contains(menuId)) {
            ordered.append(menuId);
        }
    }
    for (const QString &menuId : registered) {
        if (!ordered.contains(menuId)) {
            ordered.append(menuId);
        }
    }

    menuBar()->clear();
    for (const QString &menuId : ordered) {
        QString label = commands_->menuLabel(menuId);
        if (label == menuId) {
            label = menuId.left(1).toUpper() + menuId.mid(1);
        }
        const QList<QAction *> actions = commands_->actionsInMenu(menuId);
        if (actions.isEmpty()) {
            continue;
        }
        QMenu *menu = menuBar()->addMenu(label);
        menu->setObjectName(QStringLiteral("menu-") + menuId);
        for (QAction *action : actions) {
            if (commands_->separatorBefore(action->objectName().mid(QStringLiteral("action-").size()))) {
                menu->addSeparator();
            }
            menu->addAction(action);
        }
        // UI-Mosaic T3: the product-theme entry is a SUBMENU of View. Adding a
        // sixth top-level menu would break ux_selftest's five-menu contract.
        if (menuId == QStringLiteral("view")) {
            viewThemeMenu_ = createThemeMenu(menu, QStringLiteral("menu-theme"));
            menu->addMenu(viewThemeMenu_);
        }
    }
}

QToolBar *ApplicationShell::mainToolBar() const
{
    return mainToolBar_;
}

void ApplicationShell::buildToolBar()
{
    if (mainToolBar_ == nullptr) {
        mainToolBar_ = new QToolBar(tr("Main Toolbar"), this);
        mainToolBar_->setObjectName(QStringLiteral("vedit-main-toolbar"));
        // Hideable and movable (the two properties the user asked for): the
        // toolbar can be dragged to any dock area and floats when dragged out.
        mainToolBar_->setMovable(true);
        mainToolBar_->setFloatable(true);
        mainToolBar_->setAllowedAreas(Qt::AllToolBarAreas);
        // UI-Mosaic T3: icon + text on the toolbar buttons. The actions are
        // SHARED with the menus, so `setIconVisibleInMenu(false)` below is what
        // keeps the menus text-only without duplicating every action.
        mainToolBar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        // UI-Mosaic T6: the toolbar row is the window width; eleven icon+text
        // buttons at 24px left the theme toolbar only 30px, so its button was
        // hidden as an overflow item. 20px icons free ~44px and keep the button
        // inside the row at the default window size.
        mainToolBar_->setIconSize(QSize(20, 20));
        addToolBar(Qt::TopToolBarArea, mainToolBar_);
        // Keep the View menu check state honest in both directions: the X/hide
        // path and the menu path must agree.
        QObject::connect(mainToolBar_, &QToolBar::visibilityChanged, this, [this](bool visible) {
            if (commands_ != nullptr) {
                if (QAction *action = commands_->action(QStringLiteral("view.toolbar"))) {
                    action->setChecked(visible);
                }
            }
        });
    }
    mainToolBar_->clear();

    // The order the user specified: new, open, open folder, SWITCH PROJECT,
    // save, save all, undo, redo | separator | search, build (Lint Project),
    // format. The switch entry sits next to open-folder by request (RM-2).
    // Each entry holds the command registry's OWN QAction, so a toolbar button
    // and its menu entry can never diverge; an id a lane did not register (the
    // diagnostics lane owns Build/Format) is skipped rather than faked.
    static const char *const kToolBarOrder[] = {
        "file.new",       "file.open",   "file.open-folder", "file.switch-project",
        "file.save",      "file.save-all", "edit.undo",     "edit.redo",
        nullptr, /* separator */ "search.focus", "project.lint", "diagnostics.format",
    };
    for (const char *id : kToolBarOrder) {
        if (id == nullptr) {
            mainToolBar_->addSeparator();
            continue;
        }
        QAction *action = commands_->action(QString::fromLatin1(id));
        if (action == nullptr) {
            continue;
        }
        const QString iconName = toolbarIconName(QString::fromLatin1(id));
        if (!iconName.isEmpty()) {
            action->setIcon(themed_icons::actionIcon(iconName, paletteProductTheme().text));
            // The toolbar button and the menu entry share this QAction; the menu
            // must stay text-only, so the icon is hidden there.
            action->setIconVisibleInMenu(false);
        }
        mainToolBar_->addAction(action);
    }
    // UI-Mosaic T3: the theme entry sits at the RIGHT end of the toolbar row.
    // It cannot be an addWidget() on mainToolBar_: a toolbar widget action is
    // also returned by QToolBar::actions(), and ux_selftest compares that list
    // to the shipped order with an ordered equality. A second toolbar in the
    // same TopToolBarArea shares the row; its expanding spacer pushes the one
    // button to the window's right edge.
    if (themeToolBar_ == nullptr) {
        themeToolBar_ = new QToolBar(tr("Theme"), this);
        themeToolBar_->setObjectName(QStringLiteral("vedit-theme-toolbar"));
        themeToolBar_->setMovable(false);
        themeToolBar_->setFloatable(false);
        themeToolBar_->setAllowedAreas(Qt::TopToolBarArea);
        // Guarantee the one button keeps its width even if the main toolbar
        // grows; without this the layout gave the theme toolbar the 30px that
        // was left and QToolBar hid the button as an overflow item.
        themeToolBar_->setMinimumWidth(56);
        addToolBar(Qt::TopToolBarArea, themeToolBar_);
        auto *spacer = new QWidget(themeToolBar_);
        spacer->setObjectName(QStringLiteral("theme-toolbar-spacer"));
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        themeToolBar_->addWidget(spacer);
        themeButtonMenu_ = createThemeMenu(themeToolBar_, QStringLiteral("menu-theme-button"));
        auto *button = new QToolButton(themeToolBar_);
        button->setObjectName(QStringLiteral("theme-button"));
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        // A fixed size keeps QToolBarLayout from stretching the widget action:
        // without it the layout gave the button a 100px width inside a 56px
        // toolbar, so the toolbar treated it as an overflow item and hid it.
        button->setFixedSize(36, 36);
        button->setIconSize(QSize(22, 22));
        button->setToolTip(tr("Theme"));
        button->setMenu(themeButtonMenu_);
        themeToolBar_->addWidget(button);
        themeButton_ = button;
    }
    refreshToolBarIcons();
    if (QAction *toggle = commands_->action(QStringLiteral("view.toolbar"))) {
        toggle->setChecked(mainToolBar_->isVisible());
    }
    syncEditorCommandStates();
    // UI-Mosaic T6 运行期探针：只在 VEDIT_UI_PROBE=1 时打印。存在的理由：
    // 全透明图标能骗过 `QIcon::isNull()` 断言，只有数不透明像素才看得见；
    // 工具栏是否真的分到宽度、KTextEditor 内部状态行是否被样式表压扁，也只能
    // 在 show 之后量几何。探针本身不改任何状态。
    if (qEnvironmentVariableIsSet("VEDIT_UI_PROBE")) {
        QTimer::singleShot(2500, this, [this]() { logUiProbe(); });
    }
}

void ApplicationShell::logUiProbe()
{
    if (!qEnvironmentVariableIsSet("VEDIT_UI_PROBE") || mainToolBar_ == nullptr) {
        return;
    }
    qWarning("[ui-probe] theme=%s syntax=%s status-theme=%s style=%d toolbarIconSize=%dx%d",
             qPrintable(themes_ != nullptr ? themes_->currentUiTheme() : QString()),
             qPrintable(themes_ != nullptr ? themes_->currentTheme() : QString()),
             statusTheme_ != nullptr ? qPrintable(statusTheme_->text()) : "null",
             int(mainToolBar_->toolButtonStyle()), mainToolBar_->iconSize().width(),
             mainToolBar_->iconSize().height());
    // R1/M2: an ignored/invalid persisted theme must be OBSERVABLE from outside,
    // so the probe prints every startup/switch diagnostic (it changes no state).
    for (const QString &diagnostic : diagnostics_) {
        qWarning("[ui-probe] diagnostic=%s", qPrintable(diagnostic));
    }
    for (QAction *action : mainToolBar_->actions()) {
        if (action == nullptr || action->isSeparator()) {
            continue;
        }
        const QImage image = action->icon().pixmap(24, 24).toImage();
        int opaque = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) > 0) {
                    ++opaque;
                }
            }
        }
        qWarning("[ui-probe] action=%s iconNull=%d opaquePixels=%d",
                 qPrintable(action->objectName()), int(action->icon().isNull()), opaque);
    }
    if (themeButton_ != nullptr) {
        const QImage themeImage = themeButton_->icon().pixmap(24, 24).toImage();
        int opaque = 0;
        for (int y = 0; y < themeImage.height(); ++y) {
            for (int x = 0; x < themeImage.width(); ++x) {
                if (qAlpha(themeImage.pixel(x, y)) > 0) {
                    ++opaque;
                }
            }
        }
        const QPoint pos = themeButton_->mapToGlobal(QPoint(0, 0));
        qWarning("[ui-probe] themeButton global=%d,%d %dx%d visible=%d hint=%dx%d opaquePixels=%d",
                 pos.x(), pos.y(), themeButton_->width(), themeButton_->height(),
                 int(themeButton_->isVisible()), themeButton_->sizeHint().width(),
                 themeButton_->sizeHint().height(), opaque);
    }
    qWarning("[ui-probe] toolbar mainGeom=%d,%d %dx%d themeGeom=%d,%d %dx%d themeVisible=%d",
             mainToolBar_->x(), mainToolBar_->y(), mainToolBar_->width(),
             mainToolBar_->height(),
             themeToolBar_ != nullptr ? themeToolBar_->x() : -1,
             themeToolBar_ != nullptr ? themeToolBar_->y() : -1,
             themeToolBar_ != nullptr ? themeToolBar_->width() : -1,
             themeToolBar_ != nullptr ? themeToolBar_->height() : -1,
             int(themeToolBar_ != nullptr && themeToolBar_->isVisible()));
    if (editorArea_ == nullptr) {
        return;
    }
    QWidget *card = centralWidget() != nullptr
                        ? centralWidget()->findChild<QFrame *>(QStringLiteral("veditCard"))
                        : nullptr;
    QWidget *stack = editorArea_->findChild<QWidget *>(QStringLiteral("editor-pane-stack"));
    qWarning("[ui-probe] geom window=%dx%d central=%d,%d %dx%d card=%d,%d %dx%d editor=%d,%d %dx%d editorMin=%dx%d status=%d,%d %dx%d",
             width(), height(),
             centralWidget() != nullptr ? centralWidget()->x() : -1,
             centralWidget() != nullptr ? centralWidget()->y() : -1,
             centralWidget() != nullptr ? centralWidget()->width() : -1,
             centralWidget() != nullptr ? centralWidget()->height() : -1,
             card != nullptr ? card->x() : -1, card != nullptr ? card->y() : -1,
             card != nullptr ? card->width() : -1, card != nullptr ? card->height() : -1,
             editorArea_->x(), editorArea_->y(), editorArea_->width(), editorArea_->height(),
             editorArea_->minimumSizeHint().width(), editorArea_->minimumSizeHint().height(),
             statusBar() != nullptr ? statusBar()->x() : -1,
             statusBar() != nullptr ? statusBar()->y() : -1,
             statusBar() != nullptr ? statusBar()->width() : -1,
             statusBar() != nullptr ? statusBar()->height() : -1);
    qWarning("[ui-probe] geom stack=%d,%d %dx%d",
             stack != nullptr ? stack->x() : -1, stack != nullptr ? stack->y() : -1,
             stack != nullptr ? stack->width() : -1, stack != nullptr ? stack->height() : -1);
    // 编辑器底部区域（KTextEditor 的 KateViewBar / KateStatusBar + 它的按钮）：
    // 这些控件的文字裁切才是"状态行被压住"的可观测形态。
    int budget = 40;
    for (QWidget *child : editorArea_->findChildren<QWidget *>()) {
        if (budget-- <= 0) {
            break;
        }
        if (child->isWindow()) {
            continue;
        }
        const QPoint pos = child->mapToGlobal(QPoint(0, 0));
        if (pos.y() < 600) {
            continue;
        }
        qWarning("[ui-probe] widget cls=%s obj=%s parentObj=%s global=%d,%d %dx%d hint=%dx%d",
                 child->metaObject()->className(), qPrintable(child->objectName()),
                 child->parentWidget() != nullptr
                     ? qPrintable(child->parentWidget()->objectName())
                     : "null",
                 pos.x(), pos.y(), child->width(), child->height(),
                 child->sizeHint().width(), child->sizeHint().height());
    }
}

QMenu *ApplicationShell::createThemeMenu(QWidget *parent, const QString &objectName)
{
    auto *menu = new QMenu(tr("&Theme"), parent);
    menu->setObjectName(objectName);
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    const QString current =
        themes_ != nullptr ? themes_->currentUiTheme() : defaultProductTheme().id;
    for (const ProductTheme &theme : productThemes()) {
        QAction *action = menu->addAction(theme.displayName);
        action->setObjectName(QStringLiteral("action-theme-") + theme.id);
        action->setCheckable(true);
        action->setChecked(theme.id == current);
        action->setData(theme.id);
        const QString id = theme.id;
        QObject::connect(action, &QAction::triggered, this,
                         [this, id]() { applyProductUiTheme(id, true); });
        group->addAction(action);
    }
    return menu;
}

void ApplicationShell::syncThemeMenuChecks()
{
    if (themes_ == nullptr) {
        return;
    }
    const QString current = themes_->currentUiTheme();
    const QList<QMenu *> menus{viewThemeMenu_, themeButtonMenu_};
    for (QMenu *menu : menus) {
        if (menu == nullptr) {
            continue;
        }
        for (QAction *action : menu->actions()) {
            if (action->isSeparator()) {
                continue;
            }
            action->setChecked(action->data().toString() == current);
        }
    }
}

void ApplicationShell::refreshToolBarIcons()
{
    const ProductTheme &theme = paletteProductTheme();
    if (mainToolBar_ != nullptr) {
        for (QAction *action : mainToolBar_->actions()) {
            if (action->isSeparator()) {
                continue;
            }
            const QString id = action->objectName().mid(QStringLiteral("action-").size());
            const QString iconName = toolbarIconName(id);
            if (iconName.isEmpty()) {
                continue;
            }
            action->setIcon(themed_icons::actionIcon(iconName, theme.text));
            action->setIconVisibleInMenu(false);
        }
    }
    if (themeButton_ != nullptr) {
        themeButton_->setIcon(themed_icons::actionIcon(QStringLiteral("theme"), theme.accent));
    }
}

void ApplicationShell::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event != nullptr && event->type() == QEvent::ApplicationPaletteChange) {
        refreshToolBarIcons();
        syncThemeMenuChecks();
    }
}

void ApplicationShell::syncOwnedShortcuts()
{
    if (editorArea_ == nullptr || commands_ == nullptr) {
        return;
    }
    QStringList sequences;
    for (const QString &id : commands_->ids()) {
        const QKeySequence sequence = commands_->effectiveShortcut(id);
        if (!sequence.isEmpty()) {
            sequences.append(sequence.toString(QKeySequence::PortableText));
        }
    }
    editorArea_->setOwnedShortcuts(sequences);
}

void ApplicationShell::syncEditorCommandStates()
{
    if (commands_ == nullptr || editorArea_ == nullptr) {
        return;
    }
    const QStringList undoNames{QStringLiteral("edit_undo"), QStringLiteral("undo")};
    const QStringList redoNames{QStringLiteral("edit_redo"), QStringLiteral("redo")};
    if (QAction *undo = commands_->action(QStringLiteral("edit.undo"))) {
        undo->setEnabled(editorArea_->activeEditorActionEnabled(undoNames));
    }
    if (QAction *redo = commands_->action(QStringLiteral("edit.redo"))) {
        redo->setEnabled(editorArea_->activeEditorActionEnabled(redoNames));
    }
}

void ApplicationShell::focusSearchPanel()
{
    if (panels_ == nullptr) {
        return;
    }
    panels_->showPanel(QStringLiteral("search"), this);
    QDockWidget *dock = panels_->dock(QStringLiteral("search"));
    QWidget *content = dock != nullptr ? dock->widget() : nullptr;
    // The search lane names its query field "search-query"; the shell finds it by
    // that stable objectName instead of including the lane's header.
    auto *query = content != nullptr
                      ? content->findChild<QLineEdit *>(QStringLiteral("search-query"))
                      : nullptr;
    if (query != nullptr) {
        query->setFocus(Qt::ShortcutFocusReason);
        query->selectAll();
    }
    updateStatusBar();
}

void ApplicationShell::openUserManual()
{
    HelpViewer *viewer = HelpViewer::open(this);
    if (viewer == nullptr) {
        addDiagnostic(tr("The built-in user manual could not be opened"));
        return;
    }
    if (viewer->textView() != nullptr) {
        viewer->textView()->setFocus(Qt::OtherFocusReason);
    }
}

bool ApplicationShell::saveAllDocuments()
{
    if (editorArea_ == nullptr) {
        return false;
    }
    QStringList errors;
    QStringList untitled;
    int saved = 0;
    for (EditorPane *pane : editorArea_->panes()) {
        if (pane == nullptr) {
            continue;
        }
        for (int index = 0; index < pane->tabCount(); ++index) {
            if (!pane->isTabModified(index)) {
                continue;
            }
            const QString path = pane->tabFilePath(index);
            if (path.isEmpty()) {
                // An untitled document has no target; it needs Save As. Reported,
                // never silently discarded.
                untitled.append(pane->tabText(index));
                continue;
            }
            QString error;
            if (pane->saveTab(index, &error)) {
                ++saved;
            } else {
                errors.append(error.isEmpty() ? path : error);
            }
        }
    }
    for (const QString &error : errors) {
        addDiagnostic(error);
    }
    if (!errors.isEmpty()) {
        statusBar()->showMessage(tr("Save All failed for %1 document(s)").arg(errors.size()), 8000);
        updateStatusBar();
        return false;
    }
    if (!untitled.isEmpty()) {
        statusBar()->showMessage(
            tr("Saved %1 document(s); %2 untitled document(s) need Save As")
                .arg(saved)
                .arg(untitled.size()),
            6000);
    } else {
        statusBar()->showMessage(tr("Saved %1 document(s)").arg(saved), 4000);
    }
    updateStatusBar();
    return true;
}

void ApplicationShell::applyPanelsFromSession(const QStringList &insertedPanels)
{
    for (const QString &id : insertedPanels) {
        if (panels_->contains(id)) {
            panels_->insert(id, this);
        } else {
            addDiagnostic(QCoreApplication::translate("VEdit", "session mentions unknown panel '%1'").arg(id));
        }
    }
}

QString ApplicationShell::projectLabel() const
{
    if (options_.projectRoot.isEmpty()) {
        // A2 review N1: the status bar is user-visible, so the empty-project
        // label goes through the translator (catalogue: this class's context).
        return tr("no project");
    }
    const QString name = QFileInfo(options_.projectRoot).fileName();
    return name.isEmpty() ? options_.projectRoot : name;
}

void ApplicationShell::applyThemeForStartup()
{
    applyStartupThemes(QString(), QString());
}

void ApplicationShell::applyStartupThemes(const QString &archiveSyntaxTheme,
                                          const QString &archiveUiTheme)
{
    vedit::app::crash::event("theme startup name=%s", qPrintable(options_.themeName));
    const QString settingsPath = i18n::settingsFilePath(options_.stateDir);

    // UI-Mosaic T3 precedence: explicit seam > settings.toml > archive > yunwu.
    // UI-Mosaic R1/M2: an id no product theme answers is treated as UNSET — the
    // next source gets its turn, so an invalid settings.toml value can no longer
    // swallow a usable archived theme — and the rejection is VISIBLE (diagnostic
    // plus a timed status line), never silent.
    // UI-Mosaic R3/A1: settings.toml and the session archive are TWO writers of the
    // same preference, so when both carry a VALID but DIFFERENT id the newer FILE
    // wins ("last writer wins"); equal mtimes fall back to settings (the persistent
    // preference) and an unreadable/missing mtime falls back to the fixed
    // precedence settings > archive. A normal switch writes both, so this only
    // decides the case a write failed, a file was edited by hand, or the process
    // died between the two writes.
    const QString settingsUiTheme =
        readSettingsString(settingsPath, QStringLiteral("uiTheme")).trimmed();
    const QString archiveUiThemeId = archiveUiTheme.trimmed();
    const QString seamUiTheme = options_.uiTheme.trimmed();
    const bool settingsUiThemeValid =
        !settingsUiTheme.isEmpty() && productThemeById(settingsUiTheme) != nullptr;
    const bool archiveUiThemeValid =
        !archiveUiThemeId.isEmpty() && productThemeById(archiveUiThemeId) != nullptr;

    QString uiThemeId;

    // An unknown persisted id is reported even when the OTHER source wins: a bad
    // value must never disappear silently (R1/M2).
    if (!seamUiTheme.isEmpty() && productThemeById(seamUiTheme) == nullptr) {
        reportUnknownUiTheme(seamUiTheme, QStringLiteral("shell option"));
    }
    if (!settingsUiTheme.isEmpty() && !settingsUiThemeValid) {
        reportUnknownUiTheme(settingsUiTheme, QStringLiteral("settings.toml"));
    }
    if (!archiveUiThemeId.isEmpty() && !archiveUiThemeValid) {
        reportUnknownUiTheme(archiveUiThemeId, QStringLiteral("session archive"));
    }

    if (productThemeById(seamUiTheme) != nullptr) {
        // The seam is an explicit in-process decision (no shipped CLI fills it);
        // it outranks both files.
        uiThemeId = seamUiTheme;
    } else if (settingsUiThemeValid && archiveUiThemeValid
               && settingsUiTheme != archiveUiThemeId) {
        // `session_->path()` is the archive this start actually read; a missing
        // file yields an invalid QDateTime, which falls through to settings.
        const QDateTime settingsMtime = QFileInfo(settingsPath).lastModified();
        const QDateTime archiveMtime =
            session_ != nullptr ? QFileInfo(session_->path()).lastModified() : QDateTime();
        if (settingsMtime.isValid() && archiveMtime.isValid() && archiveMtime > settingsMtime) {
            uiThemeId = archiveUiThemeId;
        } else {
            uiThemeId = settingsUiTheme;
        }
    } else if (settingsUiThemeValid) {
        uiThemeId = settingsUiTheme;
    } else if (archiveUiThemeValid) {
        uiThemeId = archiveUiThemeId;
    }

    // UI-Mosaic T6: syntax theme. An explicit choice (--theme, then Preferences)
    // always wins. Otherwise the archive may still carry a directly applied
    // theme (the shell selfcheck saves one and restores it), but an archive that
    // only holds the DEFAULT is not a choice — the product theme's paired
    // syntaxTheme is the contract default then. A uiTheme configured in
    // settings.toml also re-pairs: it is an external theme decision, and a dark
    // chrome must not keep a light syntax palette.
    QString syntaxTheme = options_.themeName;
    if (syntaxTheme.isEmpty()) {
        syntaxTheme = readSettingsString(settingsPath, QStringLiteral("theme"));
    }
    if (syntaxTheme.isEmpty()) {
        const bool archiveIsChoice = !archiveSyntaxTheme.isEmpty()
                                     && archiveSyntaxTheme != themes_->defaultThemeName();
        // An INVALID settings uiTheme was treated as unset above, so it must not
        // suppress the archive's explicit syntax-theme choice either. R3 keeps this
        // R1 rule unchanged: only an EXTERNAL (settings/seam) valid uiTheme re-pairs;
        // when the archive wins the mtime arbitration its own syntax theme records
        // the same last session, and pairing the winning product theme reproduces it.
        if (archiveIsChoice && !settingsUiThemeValid) {
            syntaxTheme = archiveSyntaxTheme;
        } else {
            const ProductTheme *product = productThemeById(
                uiThemeId.isEmpty() ? defaultProductTheme().id : uiThemeId);
            if (product != nullptr) {
                syntaxTheme = product->syntaxTheme;
            }
        }
    }

    QString error;
    if (!themes_->applyTheme(syntaxTheme, &error)) {
        addDiagnostic(QStringLiteral("theme: %1").arg(error));
        QString fallbackError;
        if (!themes_->applyTheme(QString(), &fallbackError)) {
            addDiagnostic(QCoreApplication::translate("VEdit", "theme fallback: %1").arg(fallbackError));
        }
    }
    applyProductUiTheme(uiThemeId, false);
    updateStatusBar();
}

void ApplicationShell::applyPairedSyntaxTheme()
{
    if (themes_ == nullptr) {
        return;
    }
    // An explicit choice is never overridden (REQUIREMENTS §6.3): the CLI wins,
    // then a `theme` written by the Preferences panel.
    if (!options_.themeName.isEmpty()) {
        return;
    }
    if (!readSettingsString(i18n::settingsFilePath(options_.stateDir),
                            QStringLiteral("theme")).isEmpty()) {
        return;
    }
    const ProductTheme *product = productThemeById(themes_->currentUiTheme());
    if (product == nullptr || product->syntaxTheme.isEmpty()) {
        return;
    }
    QString error;
    if (!themes_->applyTheme(product->syntaxTheme, &error)) {
        addDiagnostic(QStringLiteral("theme: %1").arg(error));
    }
}

void ApplicationShell::applyProductUiTheme(const QString &id, bool reportFailure)
{
    if (themes_ == nullptr) {
        return;
    }
    // The id actually in effect: the requested one, or the contract default when
    // the requested id is empty/unknown. A USER switch persists THIS value, so the
    // settings key, the session archive and the live chrome cannot disagree.
    QString appliedId = id.isEmpty() ? defaultProductTheme().id : id;
    QString error;
    if (!themes_->applyUiTheme(appliedId, &error)) {
        const QString message = tr("Theme '%1' could not be applied: %2").arg(appliedId, error);
        addDiagnostic(message);
        if (reportFailure) {
            // The shell's existing error surface: a permanent-or-transient status
            // line, never a silently swallowed failure.
            statusBar()->showMessage(message, 8000);
        }
        // A stored id that no longer exists must not leave the chrome unstyled.
        QString fallbackError;
        if (!themes_->applyUiTheme(QString(), &fallbackError)) {
            addDiagnostic(QStringLiteral("ui-theme fallback: %1").arg(fallbackError));
        }
        appliedId = themes_->currentUiTheme();
    }
    if (reportFailure) {
        // UI-Mosaic R1/F1: a menu/toolbar switch now persists through the SAME
        // settings store the Preferences dropdown writes. Before this, only the
        // exit-time archive was updated and the next start preferred the OLD
        // settings.toml value, so the switch silently disappeared on restart.
        persistUiTheme(appliedId);
        applyPairedSyntaxTheme();
    }
    refreshToolBarIcons();
    syncThemeMenuChecks();
    updateStatusBar();
}

void ApplicationShell::persistUiTheme(const QString &id)
{
    if (id.isEmpty() || persistingUiTheme_) {
        return;
    }
    persistingUiTheme_ = true;
    // 1) settings.toml — through the seam the product installs from main.cpp,
    // which is the SAME SettingsClient write the Preferences page performs. The
    // shell itself must not include that lane: `vedit_workspace_settings` links
    // `vedit_app_shell`, so the dependency points the other way.
    if (!uiThemePersister_) {
        const QString message =
            tr("Interface theme '%1' was applied but not stored: no settings store is available.")
                .arg(id);
        addDiagnostic(message);
        statusBar()->showMessage(message, 8000);
    } else {
        QString error;
        if (!uiThemePersister_(id, &error)) {
            QString message = tr("Interface theme '%1' was applied but could not be stored.").arg(id);
            if (!error.isEmpty()) {
                message += QStringLiteral(" ") + error;
            }
            addDiagnostic(message);
            statusBar()->showMessage(message, 8000);
        }
    }
    // 2) the session archive — the SECOND source of the startup decision. Updating
    // it now (not only in saveSession at exit) means a crash right after a switch
    // cannot resurrect the previous theme. A missing archive is the ordinary first
    // start; saveSession() writes it at exit with the same id.
    if (session_ != nullptr) {
        QString loadError;
        const QJsonObject archive = session_->load(&loadError);
        if (!archive.isEmpty()) {
            QJsonObject updated = archive;
            updated.insert(QStringLiteral("uiTheme"), id);
            QString saveError;
            if (!session_->save(updated, &saveError) && !saveError.isEmpty()) {
                addDiagnostic(saveError);
            }
        }
    }
    persistingUiTheme_ = false;
}

void ApplicationShell::reportUnknownUiTheme(const QString &id, const QString &source)
{
    const QString message = tr("Ignoring unknown interface theme '%1' (from %2)").arg(id, source);
    addDiagnostic(message);
    // Startup is exactly where a hand-edited settings.toml used to change the look
    // silently; one timed status line makes the fallback visible without a dialog.
    statusBar()->showMessage(message, 12000);
}

void ApplicationShell::applyScriptedThemeSwitch(const QString &id)
{
    QAction *target = nullptr;
    if (viewThemeMenu_ != nullptr) {
        for (QAction *action : viewThemeMenu_->actions()) {
            if (action->data().toString() == id) {
                target = action;
                break;
            }
        }
    }
    if (target == nullptr) {
        reportUnknownUiTheme(id, QStringLiteral("VEDIT_UI_THEME_SWITCH"));
        return;
    }
    // The REAL QAction: exactly the triggered() connection a user's click runs, so
    // the regression proof cannot pass through a private shortcut.
    target->trigger();
}

bool ApplicationShell::restoreSession(QString *error)
{
    // UI-Mosaic T3: the archive is read as ONE raw object so the `uiTheme` key
    // can be taken without changing SessionStore's typed contract. Every other
    // field still goes through SessionState::fromJson(), so tolerance for a v1
    // archive / unknown keys is unchanged.
    const QJsonObject archive = session_ != nullptr ? session_->load(error) : QJsonObject();
    SessionState state = SessionState::fromJson(archive);
    if (!state.isValid()) {
        return false;
    }
    state.uiTheme = archive.value(QStringLiteral("uiTheme")).toString();

    // T19: the project root is restored BEFORE the panels are inserted, so the
    // project lane's install() already sees it on this start and expands the
    // same file set (the file set itself lives in project-model.json, owned by
    // that lane; the session only names the root, which is the key
    // project_integration.cpp reads). AC-1.1 (batch 1): an ordinary start is
    // already resolved in the constructor (before the lanes install), so this is
    // the fallback for a shell that restored explicitly; it broadcasts for the
    // same reason setProjectRoot() does, because panels may already exist.
    if (options_.projectRoot.isEmpty() && !state.projectRoot.isEmpty()) {
        // FIX 1: the archived root goes through the same normalisation as every
        // other root source, and the panes are rebound before the broadcast so a
        // receiver already reads the value the tab tooltips use.
        options_.projectRoot = vedit::paths::normalizedRoot(state.projectRoot);
        if (editorArea_ != nullptr) {
            editorArea_->setDisplayRoot(options_.projectRoot);
        }
        emit projectRootChanged(options_.projectRoot);
    }

    // UI-Mosaic T6: ONE startup decision shared with the no-archive path. The
    // archive contributes its syntax theme (an explicit API-level choice) and its
    // UI theme; the precedence and the product-theme pairing live in
    // applyStartupThemes() so the two start paths cannot drift.
    applyStartupThemes(state.theme, state.uiTheme);

    applyPanelsFromSession(state.panels);

    if (!state.dockState.isEmpty() && !restoreState(state.dockState)) {
        addDiagnostic(QCoreApplication::translate("VEdit", "dock layout could not be restored (KTextEditor/version mismatch?)"));
    }

    if (!state.editorLayout.isEmpty()) {
        QString layoutError;
        if (!editorArea_->restoreLayout(state.editorLayout, &layoutError)) {
            addDiagnostic(QCoreApplication::translate("VEdit", "editor layout: %1").arg(layoutError));
        }
    }
    for (const QString &diagnostic : editorArea_->diagnostics()) {
        addDiagnostic(diagnostic);
    }
    if (editorArea_->paneCount() == 0) {
        editorArea_->ensurePane();
    }

    if (!state.geometry.isEmpty() && !restoreGeometry(state.geometry)) {
        addDiagnostic(QCoreApplication::translate("VEdit", "window geometry could not be restored"));
    }
    syncPanelToggleStates();
    updateStatusBar();
    return true;
}

bool ApplicationShell::saveSession(QString *error)
{
    vedit::app::crash::event("session save");
    // The archive is the typed SessionState (schema v2, src/app/session_store.h):
    // window geometry, dock state, inserted panels, project root, theme and the
    // editor layout WITH every pane's tabs and their caret/scroll. A dirty
    // buffer never reaches this call — closeEvent()/the restart path ask first.
    SessionState state;
    state.projectRoot = options_.projectRoot;
    state.theme = themes_->currentTheme();
    state.uiTheme = themes_->currentUiTheme();
    state.geometry = saveGeometry();
    state.dockState = saveState();
    state.panels = panels_->insertedIds();
    state.editorLayout = editorArea_->saveLayout();

    // The typed writer carries every pre-existing key; `uiTheme` is appended
    // here because SessionStore's serialisation stays outside this lane's frozen
    // write scope (see src/app/session_store.h).
    QJsonObject archive = state.toJson();
    archive.insert(QStringLiteral("uiTheme"), state.uiTheme);
    bool ok = session_->save(archive, error);
    QString shortcutError;
    if (!commands_->saveShortcuts(&shortcutError)) {
        addDiagnostic(shortcutError);
    }
    return ok;
}

bool ApplicationShell::saveActiveDocument()
{
    EditorPane *pane = editorArea_ != nullptr ? editorArea_->activePane() : nullptr;
    const auto reportFailure = [this](const QString &detail) {
        const QString message = QCoreApplication::translate("VEdit", "Save failed: %1").arg(detail);
        statusBar()->showMessage(message, 8000);
        addDiagnostic(message);
        qWarning().noquote() << "[app-shell]" << message;
    };

    if (pane == nullptr) {
        reportFailure(QCoreApplication::translate("VEdit", "no active editor pane"));
        return false;
    }
    // AC-1.4 (batch 1b): an untitled document has nothing to save TO, so Ctrl+S
    // (and File > Save) is the Save As entry for it instead of a failure message.
    if (pane->currentFilePath().isEmpty()) {
        return saveActiveDocumentAs();
    }
    const QString path = pane->currentFilePath();
    QString error;
    if (!pane->saveFile(&error)) {
        reportFailure(error.isEmpty() ? QCoreApplication::translate("VEdit", "the editor provider refused the save")
                                     : error);
        return false;
    }
    statusBar()->showMessage(path.isEmpty()
                                 ? QCoreApplication::translate("VEdit", "Saved")
                                 : QCoreApplication::translate("VEdit", "Saved %1").arg(path),
                             4000);
    return true;
}

void ApplicationShell::setSaveAsDialogProvider(std::function<QString(const QString &)> provider)
{
    saveAsDialogProvider_ = std::move(provider);
}

void ApplicationShell::setSaveAsOverwriteConfirmProvider(std::function<bool(const QString &)> provider)
{
    overwriteConfirmProvider_ = std::move(provider);
}

bool ApplicationShell::confirmOverwrite(const QString &path)
{
    if (overwriteConfirmProvider_) {
        return overwriteConfirmProvider_(path);
    }
    // Headless has nobody to ask, and silently replacing a file the user cannot
    // be warned about is the one answer that must never be assumed: refuse and
    // say why (the same rule promptUnsaved() follows).
    if (QApplication::platformName() == QStringLiteral("offscreen")) {
        addDiagnostic(QCoreApplication::translate(
            "VEdit", "Save As refused (headless): %1 exists and cannot be overwritten without confirmation")
                          .arg(path));
        return false;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Overwrite file?"),
        tr("%1 already exists. Overwrite it?").arg(QDir::toNativeSeparators(path)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer == QMessageBox::Yes;
}

bool ApplicationShell::saveActiveDocumentAs()
{
    EditorPane *pane = editorArea_ != nullptr ? editorArea_->activePane() : nullptr;
    const auto reportFailure = [this](const QString &detail) {
        const QString message = QCoreApplication::translate("VEdit", "Save As failed: %1").arg(detail);
        statusBar()->showMessage(message, 8000);
        addDiagnostic(message);
        qWarning().noquote() << "[app-shell]" << message;
    };
    if (pane == nullptr) {
        reportFailure(QCoreApplication::translate("VEdit", "no active editor pane"));
        return false;
    }

    const QString current = pane->currentFilePath();
    const QString start = !current.isEmpty()
                              ? current
                              : QDir(options_.projectRoot.isEmpty() ? QDir::homePath()
                                                                    : options_.projectRoot)
                                    .filePath(QStringLiteral("untitled.sv"));
    const QString selected =
        saveAsDialogProvider_
            ? saveAsDialogProvider_(start)
            : QFileDialog::getSaveFileName(
                  this, tr("Save As"), start,
                  QCoreApplication::translate("VEdit",
                                              "Verilog/SystemVerilog (*.v *.sv *.svh *.vh);;"
                                              "All Files (*)"));
    if (selected.isEmpty()) {
        // Cancelled: nothing is written and the document keeps its identity.
        statusBar()->showMessage(tr("Save As cancelled"), 4000);
        return false;
    }
    const QFileInfo target(selected);
    if (!target.isAbsolute() || target.fileName().isEmpty()) {
        reportFailure(tr("%1 is not a file path").arg(selected));
        return false;
    }
    if (target.exists() && !confirmOverwrite(target.absoluteFilePath())) {
        // Declined: the existing file is untouched (no truncation, no temp file).
        statusBar()->showMessage(tr("Save As cancelled"), 4000);
        return false;
    }

    QString error;
    if (!pane->saveAsFile(target.absoluteFilePath(), &error)) {
        reportFailure(error.isEmpty() ? QCoreApplication::translate("VEdit", "the editor provider refused the save")
                                     : error);
        return false;
    }
    statusBar()->showMessage(tr("Saved as %1").arg(target.absoluteFilePath()), 5000);
    updateStatusBar();
    return true;
}

void ApplicationShell::applyHorizontalSplitPreset(int paneCount)
{
    editorArea_->resetLayout();
    for (int i = 1; i < paneCount; ++i) {
        editorArea_->splitActive(Qt::Horizontal);
    }
    const auto panes = editorArea_->panes();
    if (!panes.isEmpty()) {
        editorArea_->setActivePane(panes.first());
    }
    updateStatusBar();
}

void ApplicationShell::applyVerticalSplitPreset(int paneCount)
{
    editorArea_->resetLayout();
    for (int i = 1; i < paneCount; ++i) {
        editorArea_->splitActive(Qt::Vertical);
    }
    const auto panes = editorArea_->panes();
    if (!panes.isEmpty()) {
        editorArea_->setActivePane(panes.first());
    }
    updateStatusBar();
}

void ApplicationShell::applyMixedSplitPreset()
{
    editorArea_->resetLayout();
    editorArea_->splitActive(Qt::Horizontal);
    const auto panes = editorArea_->panes();
    if (!panes.isEmpty()) {
        editorArea_->setActivePane(panes.last());
        editorArea_->splitActive(Qt::Vertical);
    }
    const auto all = editorArea_->panes();
    if (!all.isEmpty()) {
        editorArea_->setActivePane(all.first());
    }
    updateStatusBar();
}

void ApplicationShell::insertAllPanelsForDemo()
{
    for (const QString &id : panels_->ids()) {
        panels_->showPanel(id, this);
    }
    syncPanelToggleStates();
    updateStatusBar();
}

void ApplicationShell::syncPanelToggleStates()
{
    if (commands_ == nullptr || panels_ == nullptr) {
        return;
    }
    for (const QString &id : panels_->ids()) {
        if (QAction *action = commands_->action(QStringLiteral("view.panel.") + id)) {
            action->setChecked(panels_->isVisible(id));
        }
    }
}

void ApplicationShell::syncDockPanelCommands()
{
    if (commands_ == nullptr || panels_ == nullptr) {
        return;
    }
    bool added = false;

    // (1) A panel a LANE registered after this constructor (the module tree): the
    // fixed View list in registerCommands() only covers the five built-ins, so
    // without this the dock could be closed and never reopened (AC-1.3).
    const QStringList panelIds = panels_->ids();
    for (int index = 0; index < panelIds.size(); ++index) {
        const QString &id = panelIds.at(index);
        const QString commandId = QStringLiteral("view.panel.") + id;
        if (commands_->action(commandId) != nullptr) {
            continue;
        }
        CommandSpec spec;
        spec.id = commandId;
        // The panel's OWN (translated) title keeps the entry readable in every
        // interface language, with one generic wording for all lane panels.
        spec.text = tr("To&ggle %1 Panel").arg(panels_->title(id));
        spec.menu = QStringLiteral("view");
        spec.order = 100 + index;
        spec.checkable = true;
        if (commands_->add(spec) == nullptr) {
            continue;
        }
        commands_->setHandler(commandId, [this, id, commandId]() {
            const bool visible = panels_->toggle(id, this);
            if (QAction *action = commands_->action(commandId)) {
                action->setChecked(visible);
            }
            updateStatusBar();
        });
        added = true;
    }

    // (2) A dock the shell owns but PanelRegistry does not (the project lane
    // creates its own QDockWidget): the user can close it with its X button, so
    // View must be able to bring it back too. The objectName is the stable key —
    // it is also what QMainWindow::saveState()/restoreState() remembers, so the
    // command means the same thing after a restart.
    const QList<QDockWidget *> docks = findChildren<QDockWidget *>();
    for (QDockWidget *dock : docks) {
        if (dock == nullptr || dock->objectName().isEmpty()) {
            continue;
        }
        bool registered = false;
        for (const QString &id : panelIds) {
            if (panels_->dock(id) == dock) {
                registered = true;
                break;
            }
        }
        if (registered) {
            continue;
        }
        const QString commandId = QStringLiteral("view.dock.") + dock->objectName();
        if (commands_->action(commandId) != nullptr) {
            continue;
        }
        CommandSpec spec;
        spec.id = commandId;
        spec.text = tr("To&ggle %1 Panel").arg(dock->windowTitle());
        spec.menu = QStringLiteral("view");
        spec.order = 200; // after the registered panel toggles
        spec.checkable = true;
        if (commands_->add(spec) == nullptr) {
            continue;
        }
        const QPointer<QDockWidget> guarded(dock);
        commands_->setHandler(commandId, [this, guarded, commandId]() {
            QDockWidget *target = guarded.data();
            if (target == nullptr) {
                return;
            }
            const bool show = !target->isVisible();
            target->setVisible(show);
            if (show) {
                target->raise();
            }
            if (QAction *action = commands_->action(commandId)) {
                action->setChecked(target->isVisible());
            }
            updateStatusBar();
        });
        // The dock's own X (and any programmatic hide/show) keeps the check mark
        // honest in both directions — the same guarantee PanelRegistry gives the
        // registered panels.
        dockToggleConnections_.append(QObject::connect(
            dock, &QDockWidget::visibilityChanged, this, [this, commandId](bool visible) {
                if (QAction *action = commands_->action(commandId)) {
                    action->setChecked(visible);
                }
            }));
        if (QAction *action = commands_->action(commandId)) {
            action->setChecked(dock->isVisible());
        }
        added = true;
    }

    if (added) {
        // The menu bar was materialised from the constructor-time command list;
        // a command added later only becomes a menu item after a rebuild (the
        // registry stores specs, buildMenuBar() turns them into menus).
        buildMenuBar();
    }
}

void ApplicationShell::updateStatusBar()
{
    const EditorPane *active = editorArea_ != nullptr ? editorArea_->activePane() : nullptr;
    const QString path = active != nullptr ? active->currentFilePath() : QString();
    // FIX 1: every path this bar renders goes through the ONE display rule.
    const QString displayedPath = vedit::paths::displayPath(options_.projectRoot, path);
    const bool dirty = active != nullptr && active->isActiveTabModified();

    if (statusDirty_ != nullptr) {
        // AC-8.1: the explicit "there are unsaved changes" line. AC-8.2: it
        // disappears the moment the document's own modified flag clears (the
        // save path sets it), because this is repainted from that flag.
        statusDirty_->setText(dirty ? tr("● Unsaved changes") : QString());
        statusDirty_->setVisible(dirty);
        statusDirty_->setToolTip(path.isEmpty() ? tr("unsaved document") : displayedPath);
    }
    if (statusProject_ != nullptr) {
        statusProject_->setText(tr("Project: %1").arg(projectLabel()));
    }
    if (statusTheme_ != nullptr) {
        // UI-Mosaic T3: ONE label carries both axes — the product theme is what
        // the chrome shows, the syntax theme is what the editor renders. The
        // label already existed; no second status widget is added.
        const ProductTheme *product = themes_ != nullptr
                                          ? productThemeById(themes_->currentUiTheme())
                                          : nullptr;
        const QString productName = product != nullptr ? product->displayName
                                                       : defaultProductTheme().displayName;
        const QString syntax = themes_ != nullptr ? themes_->currentTheme() : QString();
        statusTheme_->setText(tr("Theme: %1 / %2")
                                  .arg(productName, syntax.isEmpty() ? tr("(default)") : syntax));
    }
    if (statusLayout_ != nullptr) {
        const int tabs = active != nullptr ? active->tabCount() : 0;
        statusLayout_->setText(tr("Panes: %1 · active: %2 · tabs: %3")
                                   .arg(editorArea_ != nullptr ? editorArea_->paneCount() : 0)
                                   .arg(active != nullptr ? active->paneId() : QStringLiteral("-"))
                                   .arg(tabs));
    }
    // AC-9.3: the title names the file being edited by its DISPLAY path (FIX 1:
    // relative to the project root when inside it, absolute otherwise); AC-8.1:
    // it carries the standard "[*]" modified placeholder, which Qt renders as
    // the star once windowModified is set and drops when it is not (AC-8.2).
    QString subject;
    if (!path.isEmpty()) {
        // FIX 1: the title names the document by its DISPLAY path (relative to
        // the project root when it is inside it, absolute otherwise), never by
        // the open key.
        subject = displayedPath;
    } else if (!options_.projectRoot.isEmpty()) {
        subject = projectLabel();
    } else {
        subject = QCoreApplication::translate("VEdit", "no project");
    }
    setWindowModified(dirty);
    setWindowTitle(QCoreApplication::translate("VEdit", "VEdit — %1[*]").arg(subject));
    // The check mark in both theme menus follows the live ThemeManager state,
    // including a switch made from the Preferences dialog (this function is
    // called on the next state change, so the menus cannot keep a stale tick).
    syncThemeMenuChecks();
}

ApplicationShell::CloseChoice ApplicationShell::promptUnsaved(const QString &displayName,
                                                             const QString &path)
{
    // Headless (ctest's --selfcheck and the offscreen lane selftests) has nobody
    // to ask: the only safe answer is Cancel, never "discard because no dialog
    // could be shown". The refusal is still visible to the user and in the
    // diagnostics, exactly like the GUI path's blocked close.
    if (QApplication::platformName() == QStringLiteral("offscreen")) {
        addDiagnostic(tr("close refused (headless): %1 has unsaved changes").arg(displayName));
        statusBar()->showMessage(
            tr("Close blocked: %1 has unsaved changes").arg(displayName), 8000);
        return CloseChoice::Cancel;
    }
    QMessageBox box(this);
    box.setObjectName(QStringLiteral("unsaved-close-prompt"));
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Unsaved changes"));
    box.setText(tr("%1 has unsaved changes.").arg(displayName));
    box.setInformativeText(
        path.isEmpty()
            ? tr("This document has no file path yet, so it cannot be saved here.")
                  + QStringLiteral("\n\n") + tr("Close it and discard the changes?")
            : path + QStringLiteral("\n\n")
                  + tr("Save the document before closing, or discard the changes?"));
    QPushButton *saveButton = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    QPushButton *discardButton = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    QPushButton *cancelButton = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    saveButton->setObjectName(QStringLiteral("unsaved-save"));
    discardButton->setObjectName(QStringLiteral("unsaved-discard"));
    cancelButton->setObjectName(QStringLiteral("unsaved-cancel"));
    box.setDefaultButton(cancelButton);
    box.setEscapeButton(cancelButton);
    box.exec();
    if (box.clickedButton() == saveButton) {
        return CloseChoice::Save;
    }
    if (box.clickedButton() == discardButton) {
        return CloseChoice::Discard;
    }
    return CloseChoice::Cancel;
}

bool ApplicationShell::confirmPaneClose(EditorPane *pane)
{
    if (pane == nullptr) {
        return true;
    }
    for (int index = 0; index < pane->tabCount(); ++index) {
        if (!pane->isTabModified(index)) {
            continue;
        }
        const QString path = pane->tabFilePath(index);
        const QString display =
            path.isEmpty() ? QStringLiteral("untitled") : QFileInfo(path).fileName();
        switch (promptUnsaved(display, path)) {
        case CloseChoice::Save: {
            QString error;
            if (!pane->saveTab(index, &error)) {
                const QString message = error.isEmpty()
                                            ? tr("Save failed: %1").arg(display)
                                            : tr("Save failed: %1").arg(error);
                addDiagnostic(message);
                statusBar()->showMessage(message, 8000);
                return false;
            }
            break;
        }
        case CloseChoice::Discard:
            break;
        case CloseChoice::Cancel:
            statusBar()->showMessage(
                QStringLiteral("Close blocked: %1 has unsaved changes").arg(display), 8000);
            return false;
        }
    }
    return true;
}

void ApplicationShell::setUnsavedPromptProvider(
    std::function<CloseChoice(const QStringList &, const QStringList &)> provider)
{
    unsavedPromptProvider_ = std::move(provider);
}

bool ApplicationShell::confirmUnsavedDocuments()
{
    if (editorArea_ == nullptr) {
        return true;
    }
    // Collect EVERY dirty document first (pane + tab index), so several dirty
    // buffers are answered by ONE prompt instead of one dialog per buffer
    // (AC-4.2). The single-document case keeps the T19 prompt exactly as it was.
    struct DirtyDocument {
        EditorPane *pane = nullptr;
        int tab = -1;
        QString display;
        QString path;
    };
    QVector<DirtyDocument> dirty;
    for (EditorPane *pane : editorArea_->panes()) {
        if (pane == nullptr) {
            continue;
        }
        for (int index = 0; index < pane->tabCount(); ++index) {
            if (!pane->isTabModified(index)) {
                continue;
            }
            DirtyDocument document;
            document.pane = pane;
            document.tab = index;
            document.path = pane->tabFilePath(index);
            document.display =
                document.path.isEmpty() ? tr("untitled") : QFileInfo(document.path).fileName();
            dirty.append(document);
        }
    }
    if (dirty.isEmpty()) {
        return true;
    }

    QStringList displays;
    QStringList paths;
    for (const DirtyDocument &document : dirty) {
        displays.append(document.display);
        paths.append(document.path);
    }

    CloseChoice choice = CloseChoice::Cancel;
    if (unsavedPromptProvider_) {
        // Test seam: ONE call for the whole set — that is the "one dialog" rule.
        choice = unsavedPromptProvider_(displays, paths);
    } else if (dirty.size() == 1) {
        choice = promptUnsaved(dirty.first().display, dirty.first().path);
    } else {
        choice = promptUnsavedBatch(displays, paths);
    }

    switch (choice) {
    case CloseChoice::Save:
        for (const DirtyDocument &document : dirty) {
            QString error;
            if (!document.pane->saveTab(document.tab, &error)) {
                const QString message = error.isEmpty()
                                            ? tr("Save failed: %1").arg(document.display)
                                            : tr("Save failed: %1").arg(error);
                addDiagnostic(message);
                statusBar()->showMessage(message, 8000);
                return false;
            }
        }
        return true;
    case CloseChoice::Discard:
        return true;
    case CloseChoice::Cancel:
        break;
    }
    if (dirty.size() > 1) {
        statusBar()->showMessage(
            tr("Close blocked: %1 documents have unsaved changes").arg(dirty.size()), 8000);
    }
    return false;
}

ApplicationShell::CloseChoice ApplicationShell::promptUnsavedBatch(const QStringList &displays,
                                                                   const QStringList &paths)
{
    // Headless has nobody to ask: Cancel (never "discard because no dialog could
    // be shown"), exactly like promptUnsaved().
    if (QApplication::platformName() == QStringLiteral("offscreen")) {
        addDiagnostic(tr("close refused (headless): %1 documents have unsaved changes")
                          .arg(displays.size()));
        statusBar()->showMessage(
            tr("Close blocked: %1 documents have unsaved changes").arg(displays.size()), 8000);
        return CloseChoice::Cancel;
    }
    QMessageBox box(this);
    box.setObjectName(QStringLiteral("unsaved-batch-prompt"));
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Unsaved changes"));
    box.setText(tr("%1 documents have unsaved changes.").arg(displays.size()));
    QStringList lines;
    for (int index = 0; index < displays.size(); ++index) {
        const QString path = index < paths.size() ? paths.at(index) : QString();
        lines.append(path.isEmpty() ? displays.at(index) : path);
    }
    box.setInformativeText(tr("Save all of them before continuing, or discard the changes?")
                           + QStringLiteral("\n\n") + lines.join(QLatin1Char('\n')));
    QPushButton *saveAll = box.addButton(tr("Save All"), QMessageBox::AcceptRole);
    QPushButton *discardAll = box.addButton(tr("Discard All"), QMessageBox::DestructiveRole);
    QPushButton *cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    saveAll->setObjectName(QStringLiteral("unsaved-save-all"));
    discardAll->setObjectName(QStringLiteral("unsaved-discard-all"));
    cancel->setObjectName(QStringLiteral("unsaved-cancel-all"));
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == saveAll) {
        return CloseChoice::Save;
    }
    if (box.clickedButton() == discardAll) {
        return CloseChoice::Discard;
    }
    return CloseChoice::Cancel;
}

i18n::RestartOutcome ApplicationShell::restartForLanguageChange(
    const QString &languageTag, const i18n::PersistLanguage &persistLanguage, QString *error)
{
    const auto fail = [this, error](const QString &message) {
        addDiagnostic(message);
        statusBar()->showMessage(message, 8000);
        if (error != nullptr) {
            *error = message;
        }
        return i18n::RestartOutcome::Failed;
    };

    // 1) The guard T19 made public. A cancelled answer aborts the restart and
    //    NOTHING is written: no language key, no session archive, no relaunch.
    if (!confirmUnsavedDocuments()) {
        const QString message = tr("Language not switched: restart cancelled");
        addDiagnostic(message);
        statusBar()->showMessage(message, 8000);
        if (error != nullptr) {
            *error = message;
        }
        return i18n::RestartOutcome::Cancelled;
    }
    if (!persistLanguage) {
        return fail(tr("No language store is available; the language was not changed."));
    }
    // 2) Only now is the language persisted. A failed write must not restart into
    //    a language the next start would not read.
    QString persistError;
    if (!persistLanguage(&persistError)) {
        return fail(persistError.isEmpty()
                        ? tr("The interface language could not be stored.")
                        : tr("The interface language could not be stored: %1").arg(persistError));
    }
    // 3) T19 owns session.json: this path only calls its saveSession(), never
    //    writes the archive itself, so the next start restores the same session.
    QString saveError;
    if (!saveSession(&saveError)) {
        return fail(saveError.isEmpty()
                        ? tr("The session archive could not be written.")
                        : tr("The session archive could not be written: %1").arg(saveError));
    }
    // 4) Relaunch. restartApplication() quits this process on success; a probe's
    //    handler observes the call instead of spawning a process.
    QString restartError;
    if (!i18n::restartApplication(&restartError)) {
        return fail(restartError.isEmpty()
                        ? tr("The application could not restart.")
                        : tr("The application could not restart: %1").arg(restartError));
    }
    addDiagnostic(tr("Restarting to apply interface language %1").arg(languageTag));
    return i18n::RestartOutcome::Restarted;
}

void ApplicationShell::closeEvent(QCloseEvent *event)
{
    // T19 / AC-10.1.2.2: a dirty buffer is SAVED or explicitly DISCARDED before
    // the archive is written. Cancel keeps the window (and every buffer) open and
    // archives nothing, so a restart can never silently drop an edit.
    if (!confirmUnsavedDocuments()) {
        event->ignore();
        return;
    }
    QString error;
    if (!saveSession(&error)) {
        addDiagnostic(error);
    }
    QMainWindow::closeEvent(event);
}

QStringList ApplicationShell::diagnostics() const
{
    return diagnostics_;
}

void ApplicationShell::addShellDiagnostic(const QString &message)
{
    addDiagnostic(message);
}

void ApplicationShell::addDiagnostic(const QString &message)
{
    if (message.isEmpty() || diagnostics_.contains(message)) {
        return;
    }
    diagnostics_.append(message);
    qWarning().noquote() << "[app-shell]" << message;
}

} // namespace vedit::app
