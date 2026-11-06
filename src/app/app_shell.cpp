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
#include "session_store.h"
#include "theme_manager.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringLiteral>
#include <QToolBar>
#include <QVector>
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

ApplicationShell::ApplicationShell(ShellOptions options, QWidget *parent)
    : QMainWindow(parent)
    , options_(std::move(options))
    , provider_(std::make_unique<DemoEditorProvider>())
{
    setObjectName(QStringLiteral("vedit-app-shell"));
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
    setCentralWidget(editorArea_);
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
        // Text buttons: the actions are shared with the menus, and setting an
        // icon on a shared QAction would also repaint the menu entries — the
        // toolbar is the only new surface this change is allowed to touch.
        mainToolBar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
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
        mainToolBar_->addAction(action);
    }
    if (QAction *toggle = commands_->action(QStringLiteral("view.toolbar"))) {
        toggle->setChecked(mainToolBar_->isVisible());
    }
    syncEditorCommandStates();
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
    vedit::app::crash::event("theme startup name=%s", qPrintable(options_.themeName));
    QString error;
    if (!themes_->applyTheme(options_.themeName, &error)) {
        addDiagnostic(QStringLiteral("theme: %1").arg(error));
        QString fallbackError;
        if (!themes_->applyTheme(QString(), &fallbackError)) {
            addDiagnostic(QCoreApplication::translate("VEdit", "theme fallback: %1").arg(fallbackError));
        }
    }
    updateStatusBar();
}

bool ApplicationShell::restoreSession(QString *error)
{
    SessionState state;
    if (session_ != nullptr) {
        state = session_->loadState(error);
    }
    if (!state.isValid()) {
        return false;
    }

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

    QString themeError;
    if (!options_.themeName.isEmpty()) {
        if (!themes_->applyTheme(options_.themeName, &themeError)) {
            addDiagnostic(QStringLiteral("theme: %1").arg(themeError));
            applyThemeForStartup();
        }
    } else if (state.theme.isEmpty() || !themes_->applyTheme(state.theme, &themeError)) {
        if (!themeError.isEmpty()) {
            addDiagnostic(QStringLiteral("theme: %1").arg(themeError));
        }
        applyThemeForStartup();
    }

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
    state.geometry = saveGeometry();
    state.dockState = saveState();
    state.panels = panels_->insertedIds();
    state.editorLayout = editorArea_->saveLayout();

    bool ok = session_->saveState(state, error);
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
        const QString theme = themes_->currentTheme();
        statusTheme_->setText(tr("Theme: %1").arg(theme.isEmpty() ? tr("(default)") : theme));
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
