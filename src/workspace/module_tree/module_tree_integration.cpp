#include "module_tree_integration.h"

#include <QCoreApplication>
#include <QObject>
#include <QVariant>

namespace vedit::workspace {

QString moduleTreeSlangBinary(const QObject *shell)
{
    // VEDIT_SLANG keeps the lane's historical precedence: an explicit override
    // always wins (the Rust resolver treats the option it is handed the same way).
    const QString overridePath = qEnvironmentVariable("VEDIT_SLANG");
    if (!overridePath.isEmpty()) {
        return overridePath;
    }
    // The diagnostics lane resolves the Qt toolchain (bundled > local > PATH >
    // env) and publishes the chosen path on the shell. This lane installs
    // before it (src/workspace/integration.cpp), so the property is read when a
    // catalog run starts — never cached in the holder's constructor.
    if (shell != nullptr) {
        const QString resolved = shell->property("vedit.diagnostics.toolchainSlang").toString();
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }
    // Empty lets the Rust catalog service resolve it itself; since the AppDir
    // layout probes `<exe_dir>/bin/slang`, a packaged start works without either.
    return {};
}

} // namespace vedit::workspace

#if defined(VEDIT_MODULE_TREE_HAVE_APP_SHELL)

#include "catalog_client.h"
#include "interface_panel.h"
#include "module_tree_panel.h"

#include "app_shell.h"
#include "editor_area.h"
#include "integration.h"
#include "panel_registry.h"

#include "top_scope.h"

#include <QDir>

#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
#include "project_model.h"
#include "project_state.h"
#endif

#include <QDateTime>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextCursor>
#include <QTimer>
#include <QWidget>

#if defined(VEDIT_MODULE_TREE_HAVE_KTEXTEDITOR)
#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

#include <cstdio>
#include <utility>

namespace vedit::workspace {

namespace {

/// The active editor's file and 0-based caret line. This only READS the editor:
/// the lane never opens or moves a document itself. The demo provider is a
/// QPlainTextEdit; the product editor is a KTextEditor view.
struct CaretContext {
    QString file;
    int line0 = -1;
};

CaretContext activeCaret(vedit::app::ApplicationShell *shell)
{
    CaretContext context;
    if (shell == nullptr || shell->editorArea() == nullptr) {
        return context;
    }
    vedit::app::EditorPane *pane = shell->editorArea()->activePane();
    if (pane == nullptr) {
        return context;
    }
    context.file = pane->currentFilePath();
    QWidget *editor = pane->editorWidget();
    if (auto *text = qobject_cast<QPlainTextEdit *>(editor)) {
        context.line0 = text->textCursor().blockNumber();
        return context;
    }
#if defined(VEDIT_MODULE_TREE_HAVE_KTEXTEDITOR)
    if (auto *view = qobject_cast<KTextEditor::View *>(editor)) {
        context.line0 = view->cursorPosition().line();
    }
#endif
    return context;
}

/// Change token of the project model: `mtime_ms|size` of the state directory's
/// `project-model.json`, empty when the file does not exist. The project lane
/// writes that file atomically when it expands, so the token changes exactly
/// when the effective file set may have changed. The holder compares the token a
/// catalog run read with the token on disk to decide whether ONE follow-up run is
/// owed - never a permanent re-run loop.
QString projectModelStamp(const QString &stateDirectory)
{
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
    const QString path = project::ProjectStateStore(stateDirectory).path();
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        return {};
    }
    return QStringLiteral("%1|%2").arg(info.lastModified().toMSecsSinceEpoch()).arg(info.size());
#else
    Q_UNUSED(stateDirectory);
    return {};
#endif
}

/// The lane's one persistent object: it owns the catalog client, remembers the
/// last catalog and pushes it into whichever panel instances the docks currently
/// hold (a dock can be closed and re-inserted, which recreates the widget).
class ModuleTreeHolder final : public QObject {
    Q_OBJECT
public:
    ModuleTreeHolder(vedit::app::ApplicationShell *shell, QString stateDirectory,
                     QString projectRoot)
        : QObject(shell)
        , shell_(shell)
        , projectRoot_(std::move(projectRoot))
        , stateDirectory_(std::move(stateDirectory))
        , client_(new module_tree::CatalogClient(this))
    {
        // Probe surface: the root this holder catalogues and the project-model
        // watch polls. A Qt property so an acceptance probe can read it without
        // linking this lane's internal class.
        setObjectName(QStringLiteral("module-tree-holder"));
        setProperty("projectRoot", projectRoot_);
        client_->setStateDirectory(stateDirectory_);
        client_->setOnReady([this](const module_tree::ModuleCatalog &catalog) {
            catalog_ = catalog;
            status_ = catalog.summaryLine();
            if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
                std::fprintf(stderr,
                             "module-tree: catalog ready files=%d modules=%d source='%s'\n",
                             catalog_.fileSetSize, static_cast<int>(catalog_.modules.size()),
                             qPrintable(client_->sourceDescription()));
            }
            // Which catalog run produced the state on screen (the test's probe:
            // a follow-up run after an in-flight model write is a NEW generation).
            setProperty("catalogGeneration", catalogRuns_);
            // RM-17: the tree control lists the modules the catalog reports as
            // uninstantiated (learning continues across Auto runs; a scoped run
            // only holds the subtree). The effective value comes from the model.
            if (catalog_.top.isEmpty()) {
                for (const QString &candidate : catalog_.topModuleNames()) {
                    if (!topCandidates_.contains(candidate)) {
                        topCandidates_.append(candidate);
                    }
                }
            }
            pushToTreePanel();
            pushToInterfacePanel();
            publishScope();
            updateCaretContext(true);
            retries_ = 0;
            finishRun(false);
        });
        client_->setOnFailed([this](const QString &message) {
            catalog_ = module_tree::ModuleCatalog{};
            catalog_.error = QCoreApplication::translate("VEdit", "module catalog: %1")
                                 .arg(message);
            status_ = catalog_.error;
            if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
                std::fprintf(stderr, "module-tree: catalog failed: %s\n", qPrintable(message));
            }
            pushToTreePanel();
            pushToInterfacePanel();
            finishRun(true);
        });
        client_->setOnProgress([this](const QString &message) {
            status_ = message;
            if (treePanel_ != nullptr) {
                treePanel_->setStatus(message);
            }
        });

        // A closed/re-inserted dock recreates the panels; attach() pushes the
        // state the client already produced.
        watchTimer_.setInterval(1000);
        QObject::connect(&watchTimer_, &QTimer::timeout, this, [this]() { checkProjectModel(); });
        // Editor/pane switch and caret movement: a cheap READ of the active
        // editor's caret, never a request to slang.
        caretTimer_.setInterval(250);
        QObject::connect(&caretTimer_, &QTimer::timeout, this,
                         [this]() { updateCaretContext(false); });
        QObject::connect(shell, &vedit::app::ApplicationShell::fileOpened, this,
                         [this](const QString &) { updateCaretContext(true); });
        QTimer::singleShot(0, this, [this]() { refresh(); });
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
        watchTimer_.start();
#endif
        caretTimer_.start();
    }

    void attachTree(module_tree::ModuleTreePanel *panel)
    {
        treePanel_ = panel;
        panel->setRefreshHandler([this]() { refresh(); });
        panel->setOnTopChanged([this](const QString &top) { applyTop(top); });
        panel->setOnSelectionChanged([this](const QString &moduleName) {
            selectedModule_ = moduleName;
            if (interfacePanel_ != nullptr) {
                interfacePanel_->setTreeSelection(moduleName);
            }
        });
        pushToTreePanel();
    }

    void attachInterface(module_tree::InterfacePanel *panel)
    {
        interfacePanel_ = panel;
        panel->setTreeSelection(selectedModule_);
        pushToInterfacePanel();
    }

    void refresh()
    {
        // Resolve on every run: the diagnostics lane publishes its report after
        // this lane is installed, and a settings override can change it later.
        client_->setSlangBinary(moduleTreeSlangBinary(shell_));
        client_->setTop(effectiveTop());
        if (projectRoot_.isEmpty()) {
            // Cancel BEFORE clearing: a run for the previous root may still be in
            // flight, and its late answer must never paint under "no project".
            client_->cancel();
            catalog_ = module_tree::ModuleCatalog{};
            selectedModule_.clear();
            status_ = noProjectReason();
            pushToTreePanel();
            pushToInterfacePanel();
            return;
        }
        // The token of the project model THIS run will read: `CatalogClient::start`
        // loads the persisted model synchronously, so the value taken here is the
        // one the run evaluates. Recording it is what lets `finishRun` tell "the
        // model changed while this run was running" from "the model is unchanged",
        // so a change owes exactly one follow-up run and an unchanged model none.
        runModelStamp_ = projectModelStamp(stateDirectory_);
        modelPending_ = false;
        setProperty("modelPending", false);
        ++catalogRuns_;
        setProperty("catalogRuns", catalogRuns_);
        client_->start(projectRoot_);
    }

    /// Probe surface for the acceptance test: the project-model watch ticks once
    /// per second and the failure retry waits 1.5 s, so a regression test that
    /// writes `project-model.json` at an exact moment (first appearance, or while
    /// a catalog run is in flight) must be able to invoke the same check the
    /// timer invokes. Production never calls these.
    Q_INVOKABLE void refreshNow() { refresh(); }
    Q_INVOKABLE void checkProjectModelNow() { checkProjectModel(); }

    /// The workspace root changed at runtime. The new root is the one the next
    /// catalog run starts from and the one the persisted-model watch accepts; the
    /// old catalog is dropped first, so the panels can never keep showing the
    /// previous project's modules under the new root.
    void setProjectRoot(const QString &root)
    {
        if (root == projectRoot_) {
            return;
        }
        projectRoot_ = root;
        setProperty("projectRoot", projectRoot_);
        // RM-17: the model restores the NEW root's own top; read it before the
        // next run so A -> B -> A never carries a top across projects.
        top_ = effectiveTop();
        topCandidates_.clear();
        modelStamp_.clear();
        runModelStamp_.clear();
        modelPending_ = false;
        setProperty("modelPending", false);
        retries_ = 0;
        catalog_ = module_tree::ModuleCatalog{};
        selectedModule_.clear();
        caretFile_.clear();
        caretLine0_ = -1;
        status_ = root.isEmpty() ? noProjectReason()
                                 : QCoreApplication::translate(
                                       "VEdit", "No module catalog yet: open a project and "
                                                "press Refresh.");
        pushToTreePanel();
        pushToInterfacePanel();
        if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
            std::fprintf(stderr, "module-tree: root -> '%s'\n",
                         root.isEmpty() ? "<none>" : qPrintable(root));
        }
        refresh();
    }

    module_tree::ModuleCatalog catalog() const { return catalog_; }

private:
    QString noProjectReason() const
    {
        return QCoreApplication::translate(
            "VEdit", "No project open — open a folder to build the module catalog.");
    }

    /// RM-17: the effective top for this run ("" = Auto). Read from the
    /// project model, which persists it per root; the holder never writes
    /// `project-model.json` itself (the model owns that file).
    QString effectiveTop() const
    {
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
        if (shell_ != nullptr) {
            if (auto *model = shell_->findChild<project::ProjectModel *>()) {
                return model->top();
            }
        }
#endif
        return top_;
    }

    /// Persist one top through the project model (per root) and restart.
    void applyTop(const QString &top)
    {
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
        if (shell_ != nullptr) {
            if (auto *model = shell_->findChild<project::ProjectModel *>()) {
                model->setTop(top);
            } else {
                top_ = top;
            }
        } else {
            top_ = top;
        }
#else
        top_ = top;
#endif
        refresh();
    }

    /// RM-17: publish the ONE derived scope (the catalog's reachable files) to
    /// the diagnostics lane, which filters the lint pass and paints the dots.
    /// With Auto the publish clears the exclusion (the lint covers everything).
    void publishScope()
    {
        const QString top = catalog_.top;
        if (top.isEmpty() || !catalog_.valid) {
            vedit::workspace::publishTopScope(QString(), QStringList{}, QStringList{});
            return;
        }
        const QSet<QString> reachable = [this]() {
            QSet<QString> set;
            for (const QString &file : catalog_.reachableFiles) {
                set.insert(QDir::cleanPath(file));
            }
            return set;
        }();
        QStringList analysed;
        QStringList excluded;
        for (const QString &file : client_->lastFileSet()) {
            const QString clean = QDir::cleanPath(file);
            if (reachable.contains(clean)) {
                analysed.append(clean);
            } else {
                excluded.append(clean);
            }
        }
        vedit::workspace::publishTopScope(top, analysed, excluded);
        if (treePanel_ != nullptr) {
            treePanel_->setScopeNote(
                excluded.isEmpty()
                    ? QString()
                    : QCoreApplication::translate(
                          "VEdit", "%1 file(s) outside the top %2 are not analysed")
                          .arg(excluded.size())
                          .arg(top));
        }
    }

    QString pendingReason() const
    {
        return projectRoot_.isEmpty()
                   ? noProjectReason()
                   : QCoreApplication::translate(
                         "VEdit", "No module catalog yet: open a project and press Refresh.");
    }

    void pushToTreePanel()
    {
        if (treePanel_ == nullptr) {
            return;
        }
        treePanel_->setTopCandidates(topCandidates_);
        treePanel_->setEffectiveTop(effectiveTop());
        if (catalog_.valid) {
            treePanel_->setCatalog(catalog_);
        } else if (!catalog_.error.isEmpty()) {
            treePanel_->setFailure(catalog_.error);
        } else {
            treePanel_->setNoProject(status_.isEmpty() ? pendingReason() : status_);
        }
    }

    void pushToInterfacePanel()
    {
        if (interfacePanel_ == nullptr) {
            return;
        }
        if (catalog_.valid) {
            interfacePanel_->setTreeSelection(selectedModule_);
            interfacePanel_->setCatalog(catalog_);
            interfacePanel_->setCaretContext(caretFile_, caretLine0_);
        } else if (!catalog_.error.isEmpty()) {
            interfacePanel_->setFailure(catalog_.error);
        } else {
            interfacePanel_->setNoProject(status_.isEmpty() ? pendingReason() : status_);
        }
    }

    /// Reads the ACTIVE editor's caret; refreshes the interface panel only when
    /// file/line actually changed (or `force`), so a 250 ms poll is cheap.
    void updateCaretContext(bool force)
    {
        const CaretContext context = activeCaret(shell_);
        if (!force && context.file == caretFile_ && context.line0 == caretLine0_) {
            return;
        }
        caretFile_ = context.file;
        caretLine0_ = context.line0;
        if (interfacePanel_ != nullptr) {
            interfacePanel_->setCaretContext(caretFile_, caretLine0_);
        }
    }

    /// The project model writes `project-model.json` atomically when it expands
    /// (opening a folder, importing a .f, editing -I/-D). Watching that file keeps
    /// the catalog on the WHOLE project file set without a second expansion path.
    ///
    /// The model's FIRST appearance and a change seen while a catalog run is in
    /// flight are both events that owe a follow-up run: on a fresh state dir the
    /// holder's first run is the root-scan fallback (the project lane expands
    /// asynchronously), so consuming the stamp without a re-run would leave the
    /// tree on the fallback file set until a manual Refresh (review H1).
    void checkProjectModel()
    {
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
        const QString stamp = projectModelStamp(stateDirectory_);
        if (stamp.isEmpty() || stamp == modelStamp_) {
            return;
        }
        // Remember the observation FIRST, so a file the run will read is not
        // reported again by the next tick (idempotence).
        modelStamp_ = stamp;
        if (client_->isRunning()) {
            if (stamp != runModelStamp_) {
                modelPending_ = true;
                setProperty("modelPending", true);
            }
            return;
        }
        if (stamp != runModelStamp_) {
            refresh();
        }
#endif
    }

    /// Called when a catalog run answers or fails. One follow-up run is owed when
    /// a model observation differs from the token the run actually read; an
    /// unchanged model never re-runs (no loop). A failed run with no model change
    /// keeps the single 1.5 s retry (the project lane may not have saved yet).
    void finishRun(bool failed)
    {
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
        const QString current = projectModelStamp(stateDirectory_);
        const bool modelChanged = !current.isEmpty() && current != runModelStamp_;
        if (modelChanged) {
            // The follow-up run will read this token: record it so the watch does
            // not schedule a second re-run for the same write.
            modelStamp_ = current;
        }
        if (modelPending_ || modelChanged) {
            modelPending_ = false;
            setProperty("modelPending", false);
            retries_ = 0;
            QTimer::singleShot(0, this, [this]() { refresh(); });
            return;
        }
#else
        Q_UNUSED(failed);
#endif
        if (failed && retries_ == 0) {
            ++retries_;
            QTimer::singleShot(1500, this, [this]() {
                if (treePanel_ != nullptr) {
                    treePanel_->setStatus(QCoreApplication::translate(
                        "VEdit", "module catalog: retrying after the project model…"));
                }
                refresh();
            });
        }
    }

    vedit::app::ApplicationShell *shell_ = nullptr;
    QString projectRoot_;
    QString stateDirectory_;
    module_tree::CatalogClient *client_ = nullptr;
    QPointer<module_tree::ModuleTreePanel> treePanel_;
    QPointer<module_tree::InterfacePanel> interfacePanel_;
    module_tree::ModuleCatalog catalog_;
    QString selectedModule_;
    QString caretFile_;
    int caretLine0_ = -1;
    QString status_;
    QTimer watchTimer_;
    QTimer caretTimer_;
    /// Last observed stamp of `project-model.json` ("" until it appears).
    QString modelStamp_;
    /// The stamp the current/last catalog run read ("" when no model existed).
    QString runModelStamp_;
    /// A model change was observed while a run was in flight.
    bool modelPending_ = false;
    int retries_ = 0;
    QString top_;
    QStringList topCandidates_;
    int catalogRuns_ = 0;
};

} // namespace

void installModuleTree(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    if (shell.panels() == nullptr) {
        return;
    }
    // The shell owns a fixed set of built-in panel specs; a lane registers its own
    // through the public registry seam (PanelRegistry::addPanel) and installs the
    // content through setFactory. The View menu picks the dock up from the same
    // registry (AC-1.3).
    vedit::app::PanelSpec treeSpec;
    treeSpec.id = QStringLiteral("module-tree");
    treeSpec.title = QCoreApplication::translate("VEdit", "Module Tree");
    treeSpec.owner = QStringLiteral("workspace/module-tree");
    treeSpec.area = Qt::RightDockWidgetArea;
    shell.panels()->addPanel(treeSpec);
    shell.panels()->setPlaceholderHint(
        treeSpec.id, QCoreApplication::translate(
                         "VEdit", "Empty state: no module catalog yet. Open a project and "
                                  "press Refresh."));

    vedit::app::PanelSpec interfaceSpec;
    interfaceSpec.id = QStringLiteral("interface-panel");
    interfaceSpec.title = QCoreApplication::translate("VEdit", "Interface");
    interfaceSpec.owner = QStringLiteral("workspace/module-tree");
    interfaceSpec.area = Qt::RightDockWidgetArea;
    shell.panels()->addPanel(interfaceSpec);
    shell.panels()->setPlaceholderHint(
        interfaceSpec.id,
        QCoreApplication::translate(
            "VEdit", "Empty state: the focused module's ports appear here once the module "
                     "catalog is ready."));

    auto *holder = shell.findChild<ModuleTreeHolder *>();
    if (holder == nullptr) {
        // The shell already resolved the persisted session root before the lanes
        // install, so an ordinary start catalogues the restored project.
        holder = new ModuleTreeHolder(&shell, options.stateDir, shell.projectRoot());
        // The file-set polling root follows a runtime switch.
        QObject::connect(&shell, &vedit::app::ApplicationShell::projectRootChanged, &shell,
                         [holder](const QString &root) { holder->setProjectRoot(root); });
    }
    shell.panels()->setFactory(treeSpec.id, [holder]() -> QWidget * {
        auto *panel = new module_tree::ModuleTreePanel();
        holder->attachTree(panel);
        return panel;
    });
    shell.panels()->setFactory(interfaceSpec.id, [holder]() -> QWidget * {
        auto *panel = new module_tree::InterfacePanel();
        holder->attachInterface(panel);
        return panel;
    });
    // Both docks are part of the deliverable, so they are visible on the first
    // run; the session restore happens after installPanels(), so a user who closed
    // one keeps it closed on the next start (dockState wins over this show).
    shell.panels()->showPanel(treeSpec.id, &shell);
    shell.panels()->showPanel(interfaceSpec.id, &shell);
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        std::fprintf(stderr, "module-tree: installed (root='%s' state='%s' docks=module-tree,"
                             "interface-panel)\n",
                     qPrintable(shell.projectRoot()), qPrintable(options.stateDir));
    }
}

} // namespace vedit::workspace

#else // no app shell in this build

namespace vedit::workspace {

void installModuleTree(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    (void)shell;
    (void)options;
}

} // namespace vedit::workspace

#endif

#if defined(VEDIT_MODULE_TREE_HAVE_APP_SHELL)
#include "module_tree_integration.moc"
#endif
