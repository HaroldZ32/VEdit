#include "app_shell.h"

#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "panel_registry.h"
#include "session_store.h"
#include "theme_manager.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringLiteral>

namespace vedit::app {

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
    setCentralWidget(editorArea_);
    editorArea_->setOnLayoutChanged([this]() { updateStatusBar(); });
    editorArea_->setOnActivePaneChanged([this](EditorPane *) {
        updateStatusBar();
        syncPanelToggleStates();
    });

    buildUi();
    registerPanels();
    registerCommands();
    buildMenuBar();

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
    syncPanelToggleStates();
    updateStatusBar();
}

ApplicationShell::~ApplicationShell() = default;

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

ThemeManager *ApplicationShell::themes() const
{
    return themes_.get();
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
    provider_ = std::move(provider);
    editorArea_->setProvider(provider_.get());
}

EditorProvider *ApplicationShell::editorProvider() const
{
    return provider_.get();
}

void ApplicationShell::buildUi()
{
    auto *project = new QLabel(this);
    project->setObjectName(QStringLiteral("status-project"));
    auto *theme = new QLabel(this);
    theme->setObjectName(QStringLiteral("status-theme"));
    auto *layout = new QLabel(this);
    layout->setObjectName(QStringLiteral("status-layout"));
    statusBar()->addWidget(project);
    statusBar()->addPermanentWidget(theme);
    statusBar()->addPermanentWidget(layout);
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
                    ? QStringLiteral("Empty state: no project open. This panel stays usable and empty.")
                    : QStringLiteral("Empty state: nothing to show for this project yet."));
    };
    addPanel(QStringLiteral("file-tree"), QStringLiteral("Files"), QStringLiteral("workspace"), Qt::LeftDockWidgetArea);
    addPanel(QStringLiteral("search"), QStringLiteral("Search"), QStringLiteral("workspace"), Qt::BottomDockWidgetArea);
    addPanel(QStringLiteral("terminal"), QStringLiteral("Terminal"), QStringLiteral("tools"), Qt::BottomDockWidgetArea);
    addPanel(QStringLiteral("vcs"), QStringLiteral("Source Control"), QStringLiteral("vcs"), Qt::LeftDockWidgetArea);
    addPanel(QStringLiteral("settings"), QStringLiteral("Settings"), QStringLiteral("settings"), Qt::RightDockWidgetArea);
    addPanel(QStringLiteral("outline"), QStringLiteral("Outline"), QStringLiteral("editor/syntax"), Qt::RightDockWidgetArea);
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

    commands_->setMenuLabel(QStringLiteral("file"), QStringLiteral("&File"));
    commands_->setMenuLabel(QStringLiteral("view"), QStringLiteral("&View"));
    commands_->setMenuLabel(QStringLiteral("help"), QStringLiteral("&Help"));

    // File
    addCommand(QStringLiteral("file.new-layout"), QStringLiteral("&New Empty Layout"),
               QStringLiteral("file"), QKeySequence(QStringLiteral("Ctrl+Alt+N")), 10);
    // Save (T71): the entry the editor is actually used through. It goes to the
    // active pane's provider seam, so the real KTextEditor document is saved
    // atomically by the edit-core lane; the demo provider keeps working through
    // its own default implementation of the same seam.
    addCommand(QStringLiteral("file.save"), QStringLiteral("&Save"), QStringLiteral("file"),
               QKeySequence(QStringLiteral("Ctrl+S")), 15);
    addCommand(QStringLiteral("file.save-session"), QStringLiteral("&Save Session Now"),
               QStringLiteral("file"), QKeySequence(), 20);
    addCommand(QStringLiteral("file.close-pane"), QStringLiteral("&Close Pane"),
               QStringLiteral("file"), QKeySequence(QStringLiteral("Ctrl+W")), 30);
    addCommand(QStringLiteral("file.quit"), QStringLiteral("&Quit"), QStringLiteral("file"),
               QKeySequence(QStringLiteral("Ctrl+Q")), 90, false, true);

    // View: layout operations
    addCommand(QStringLiteral("view.split.horizontal"), QStringLiteral("Split Pane &Horizontally"),
               QStringLiteral("view"), QKeySequence(QStringLiteral("Ctrl+Shift+H")), 10);
    addCommand(QStringLiteral("view.split.vertical"), QStringLiteral("Split Pane &Vertically"),
               QStringLiteral("view"), QKeySequence(QStringLiteral("Ctrl+Shift+V")), 20);
    addCommand(QStringLiteral("view.focus.next"), QStringLiteral("&Focus Next Pane"),
               QStringLiteral("view"), QKeySequence(QStringLiteral("F6")), 30);
    addCommand(QStringLiteral("view.layout.mixed"), QStringLiteral("&Mixed Split (demo)"),
               QStringLiteral("view"), QKeySequence(), 40);
    addCommand(QStringLiteral("view.layout.reset"), QStringLiteral("&Reset Layout"),
               QStringLiteral("view"), QKeySequence(QStringLiteral("Ctrl+Alt+R")), 50, false, true);

    const struct {
        const char *id;
        const char *title;
        const char *shortcut;
    } panelCommands[] = {
        {"file-tree", "To&ggle Files Panel", "Ctrl+Alt+F"},
        {"search", "To&ggle Search Panel", "Ctrl+Alt+S"},
        {"terminal", "To&ggle Terminal Panel", "Ctrl+Alt+J"},
        {"vcs", "To&ggle Source Control Panel", "Ctrl+Alt+V"},
        {"settings", "To&ggle Settings Panel", "Ctrl+Alt+P"},
        {"outline", "To&ggle Outline Panel", "Ctrl+Alt+O"},
    };
    int order = 100;
    for (const auto &panelCommand : panelCommands) {
        const QString panelId = QString::fromLatin1(panelCommand.id);
        const QString commandId = QStringLiteral("view.panel.") + panelId;
        const bool separatorBefore = order == 100;
        addCommand(commandId, QString::fromLatin1(panelCommand.title), QStringLiteral("view"),
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

    // Help
    addCommand(QStringLiteral("help.about"), QStringLiteral("&About VEdit"), QStringLiteral("help"),
               QKeySequence(), 10, false, true);

    // Handlers that are not panel toggles.
    commands_->setHandler(QStringLiteral("file.new-layout"), [this]() {
        editorArea_->resetLayout();
        updateStatusBar();
    });
    commands_->setHandler(QStringLiteral("file.save"), [this]() { saveActiveDocument(); });
    commands_->setHandler(QStringLiteral("file.save-session"), [this]() {
        QString error;
        if (!saveSession(&error)) {
            addDiagnostic(error);
        }
        statusBar()->showMessage(QStringLiteral("Session saved (%1)").arg(session_->path()), 4000);
    });
    commands_->setHandler(QStringLiteral("file.close-pane"), [this]() {
        editorArea_->closeActivePane();
        updateStatusBar();
    });
    commands_->setHandler(QStringLiteral("file.quit"), []() { QApplication::quit(); });
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
    commands_->setHandler(QStringLiteral("view.layout.mixed"), [this]() {
        applyMixedSplitPreset();
    });
    commands_->setHandler(QStringLiteral("view.layout.reset"), [this]() {
        editorArea_->resetLayout();
        updateStatusBar();
    });

    const auto about = [this]() {
        QMessageBox::about(
            this, QStringLiteral("About VEdit"),
            QStringLiteral("VEdit application shell (app-shell lane).\n\n"
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
    menuBar()->clear();
    for (const QString &menuId : commands_->menus()) {
        QString label = commands_->menuLabel(menuId);
        if (label == menuId) {
            label = menuId.left(1).toUpper() + menuId.mid(1);
        }
        QMenu *menu = menuBar()->addMenu(label);
        for (QAction *action : commands_->actionsInMenu(menuId)) {
            if (commands_->separatorBefore(action->objectName().mid(QStringLiteral("action-").size()))) {
                menu->addSeparator();
            }
            menu->addAction(action);
        }
    }
}

void ApplicationShell::applyPanelsFromSession(const QStringList &insertedPanels)
{
    for (const QString &id : insertedPanels) {
        if (panels_->contains(id)) {
            panels_->insert(id, this);
        } else {
            addDiagnostic(QStringLiteral("session mentions unknown panel '%1'").arg(id));
        }
    }
}

QString ApplicationShell::projectLabel() const
{
    if (options_.projectRoot.isEmpty()) {
        return QStringLiteral("no project");
    }
    const QString name = QFileInfo(options_.projectRoot).fileName();
    return name.isEmpty() ? options_.projectRoot : name;
}

void ApplicationShell::applyThemeForStartup()
{
    QString error;
    if (!themes_->applyTheme(options_.themeName, &error)) {
        addDiagnostic(QStringLiteral("theme: %1").arg(error));
        QString fallbackError;
        if (!themes_->applyTheme(QString(), &fallbackError)) {
            addDiagnostic(QStringLiteral("theme fallback: %1").arg(fallbackError));
        }
    }
    updateStatusBar();
}

bool ApplicationShell::restoreSession(QString *error)
{
    QJsonObject session;
    if (session_ != nullptr) {
        session = session_->load(error);
    }
    if (session.isEmpty()) {
        return false;
    }

    if (options_.projectRoot.isEmpty()) {
        options_.projectRoot = session.value(QStringLiteral("project")).toString();
    }

    const QString savedTheme = session.value(QStringLiteral("theme")).toString();
    QString themeError;
    if (!options_.themeName.isEmpty()) {
        if (!themes_->applyTheme(options_.themeName, &themeError)) {
            addDiagnostic(QStringLiteral("theme: %1").arg(themeError));
            applyThemeForStartup();
        }
    } else if (savedTheme.isEmpty() || !themes_->applyTheme(savedTheme, &themeError)) {
        if (!themeError.isEmpty()) {
            addDiagnostic(QStringLiteral("theme: %1").arg(themeError));
        }
        applyThemeForStartup();
    }

    QStringList panelIds;
    const QJsonArray panelArray = session.value(QStringLiteral("panels")).toArray();
    for (const QJsonValue &value : panelArray) {
        if (value.isString()) {
            panelIds.append(value.toString());
        }
    }
    applyPanelsFromSession(panelIds);

    const QByteArray dockState =
        QByteArray::fromBase64(session.value(QStringLiteral("dockState")).toString().toLatin1());
    if (!dockState.isEmpty() && !restoreState(dockState)) {
        addDiagnostic(QStringLiteral("dock layout could not be restored (KTextEditor/version mismatch?)"));
    }

    const QJsonObject editorLayout = session.value(QStringLiteral("editorLayout")).toObject();
    if (!editorLayout.isEmpty()) {
        QString layoutError;
        if (!editorArea_->restoreLayout(editorLayout, &layoutError)) {
            addDiagnostic(QStringLiteral("editor layout: %1").arg(layoutError));
        }
    }
    for (const QString &diagnostic : editorArea_->diagnostics()) {
        addDiagnostic(diagnostic);
    }
    if (editorArea_->paneCount() == 0) {
        editorArea_->ensurePane();
    }

    const QByteArray geometry =
        QByteArray::fromBase64(session.value(QStringLiteral("geometry")).toString().toLatin1());
    if (!geometry.isEmpty() && !restoreGeometry(geometry)) {
        addDiagnostic(QStringLiteral("window geometry could not be restored"));
    }
    syncPanelToggleStates();
    updateStatusBar();
    return true;
}

bool ApplicationShell::saveSession(QString *error)
{
    QJsonObject session;
    session.insert(QStringLiteral("version"), 1);
    session.insert(QStringLiteral("project"), options_.projectRoot);
    session.insert(QStringLiteral("theme"), themes_->currentTheme());
    session.insert(QStringLiteral("geometry"),
                   QString::fromLatin1(saveGeometry().toBase64()));
    session.insert(QStringLiteral("dockState"),
                   QString::fromLatin1(saveState().toBase64()));

    QJsonArray panelArray;
    for (const QString &id : panels_->insertedIds()) {
        panelArray.append(id);
    }
    session.insert(QStringLiteral("panels"), panelArray);
    session.insert(QStringLiteral("editorLayout"), editorArea_->saveLayout());

    bool ok = session_->save(session, error);
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
        const QString message = QStringLiteral("Save failed: %1").arg(detail);
        statusBar()->showMessage(message, 8000);
        addDiagnostic(message);
        qWarning().noquote() << "[app-shell]" << message;
    };

    if (pane == nullptr) {
        reportFailure(QStringLiteral("no active editor pane"));
        return false;
    }
    const QString path = pane->currentFilePath();
    QString error;
    if (!pane->saveFile(&error)) {
        reportFailure(error.isEmpty() ? QStringLiteral("the editor provider refused the save")
                                     : error);
        return false;
    }
    statusBar()->showMessage(path.isEmpty() ? QStringLiteral("Saved")
                                            : QStringLiteral("Saved %1").arg(path),
                             4000);
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

void ApplicationShell::updateStatusBar()
{
    if (statusProject_ != nullptr) {
        statusProject_->setText(QStringLiteral("Project: %1").arg(projectLabel()));
    }
    if (statusTheme_ != nullptr) {
        const QString theme = themes_->currentTheme();
        statusTheme_->setText(QStringLiteral("Theme: %1").arg(theme.isEmpty() ? QStringLiteral("(default)") : theme));
    }
    if (statusLayout_ != nullptr) {
        const EditorPane *active = editorArea_ != nullptr ? editorArea_->activePane() : nullptr;
        statusLayout_->setText(QStringLiteral("Panes: %1 · active: %2")
                                   .arg(editorArea_ != nullptr ? editorArea_->paneCount() : 0)
                                   .arg(active != nullptr ? active->paneId() : QStringLiteral("-")));
    }
    setWindowTitle(options_.projectRoot.isEmpty()
                       ? QStringLiteral("VEdit — no project")
                       : QStringLiteral("VEdit — %1").arg(projectLabel()));
}

void ApplicationShell::closeEvent(QCloseEvent *event)
{
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

void ApplicationShell::addDiagnostic(const QString &message)
{
    if (message.isEmpty() || diagnostics_.contains(message)) {
        return;
    }
    diagnostics_.append(message);
    qWarning().noquote() << "[app-shell]" << message;
}

} // namespace vedit::app
