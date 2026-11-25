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
#include <QHash>
#include <QVariantMap>

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

/// batch-20261115 AC-2.1/AC-2.2: is the open folder a PROJECT (root + a
/// filelist)? The project lane publishes the answer it derived once
/// (`vedit.project.inputs.hasFilelist`), so the module tree never re-implements
/// the rule. A build/shell without the project lane publishes nothing; there the
/// catalog keeps its historical "any root may be catalogued" behaviour instead of
/// disabling the whole lane.
bool projectHasFilelist(const QString &projectRoot,
                        const vedit::app::ApplicationShell *shell)
{
    if (projectRoot.isEmpty()) {
        return false;
    }
    if (shell == nullptr) {
        return true;
    }
    const QVariantMap published = shell->property("vedit.project.inputs").toMap();
    if (published.isEmpty()
        || published.value(QStringLiteral("root")).toString() != projectRoot) {
        return true;
    }
    return published.value(QStringLiteral("hasFilelist"), true).toBool();
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
                QSet<QString> known(topCandidates_.cbegin(), topCandidates_.cend());
                for (const QString &candidate : catalog_.topModuleNames()) {
                    if (!known.contains(candidate)) {
                        known.insert(candidate);
                        topCandidates_.append(candidate);
                    }
                }
            }
            pushToTreePanel();
            pushToInterfacePanel();
            publishScope();
            publishClosure();
            updateCaretContext(true);
            finishRun();
        });
        client_->setOnFailed([this](const QString &message,
                                    module_tree::CatalogFailureKind kind) {
            catalog_ = module_tree::ModuleCatalog{};
            catalog_.error = QCoreApplication::translate("VEdit", "module catalog: %1")
                                 .arg(message);
            status_ = catalog_.error;
            // AC M1.1: ONLY an unknown top falls back to Auto. A timeout, a
            // cancelled run or a broken toolchain KEEPS the user's top: the old
            // unconditional `setTop("")` persisted Auto and made the next gesture
            // elaborate the whole project again.
            if (module_tree::mayFallBackToAuto(kind) && !effectiveTop().isEmpty()
                && !invalidTop_) {
                invalidTop_ = true;
                const QString rejected = effectiveTop();
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
                if (shell_ != nullptr) {
                    if (auto *model = shell_->findChild<project::ProjectModel *>()) {
                        model->setTop(QString());
                    }
                }
#endif
                top_ = QString();
                topNote_ = QCoreApplication::translate(
                               "VEdit",
                               "top '%1' could not be elaborated — fell back to Auto")
                               .arg(rejected);
                // ONE follow-up run, through the same single-flight queue as
                // every other request (never a second `slang` while this one is
                // still finishing).
                rerunRequested_ = true;
                setProperty("rerunRequested", true);
            } else if (!module_tree::mayFallBackToAuto(kind)) {
                // The top stays; the sentence the user reads is the tool's own.
                topNote_.clear();
            }
            // AC M1.7: a failed run must not leave the PREVIOUS run's scope and
            // closure feeding the realtime lint. Publishing the empty catalog is
            // what clears them.
            publishScope();
            publishClosure();
            if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
                std::fprintf(stderr, "module-tree: catalog failed (kind=%d): %s\n",
                             static_cast<int>(kind), qPrintable(message));
            }
            pushToTreePanel();
            pushToInterfacePanel();
            if (treePanel_ != nullptr && !topNote_.isEmpty()) {
                treePanel_->setStatus(topNote_);
            }
            finishRun();
        });
        client_->setOnLargeProject([this](int fileCount) {
            // AC M1.5: the run was withheld on purpose. The sentence names both
            // ways forward instead of an empty tree.
            catalog_ = module_tree::ModuleCatalog{};
            status_ = module_tree::largeProjectHint(fileCount);
            if (treePanel_ != nullptr) {
                treePanel_->setStatus(status_);
            }
            publishScope();
            publishClosure();
            pushToTreePanel();
            pushToInterfacePanel();
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
        projectModel();
        watchTimer_.start();
#endif
        caretTimer_.start();
    }

    void attachTree(module_tree::ModuleTreePanel *panel)
    {
        treePanel_ = panel;
        // Both gestures are USER requests (AC M1.5): they may catalogue a very
        // large file set, and they queue behind a run in flight instead of
        // stacking a second `slang` (AC M1.3).
        panel->setRefreshHandler([this]() { refresh(true); });
        panel->setOnCancel([this]() { cancelRun(); });
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

    /// One catalog run, or ONE queued behind the one in flight.
    ///
    /// `userRequested` marks the two explicit gestures (Refresh, a typed top):
    /// they are allowed to catalogue a very large file set (AC M1.5). Everything
    /// automatic - the first run, the project-model watch, a failure follow-up -
    /// keeps the same single-flight rule: while `vd_catalog_*` is running, a new
    /// request only records that ONE more run is owed (AC M1.3). The old code
    /// started a fresh 120 s elaboration per gesture, so 2-3 `slang` children ran
    /// at once on a 20k project.
    void refresh(bool userRequested = false)
    {
        if (userRequested) {
            userRequestQueued_ = true;
        }
        // Resolve on every run: the diagnostics lane publishes its report after
        // this lane is installed, and a settings override can change it later.
        client_->setSlangBinary(moduleTreeSlangBinary(shell_));
        client_->setTop(effectiveTop());
        // AC-2.1 (batch-20261115): opening a FOLDER runs no project. The module
        // tree is the project's module catalog, so it is disabled until the user
        // imports a filelist; the panel then says exactly that instead of
        // cataloguing (and linting) a folder nobody declared a project. The gate
        // is read from the project lane's published answer, so one rule decides
        // for this lane and the diagnostics lane alike.
        const bool isProject = !projectRoot_.isEmpty()
            && projectHasFilelist(projectRoot_, shell_);
        setProperty("projectGate", isProject);
        if (!isProject) {
            // Cancel BEFORE clearing: a run for the previous root may still be in
            // flight, and its late answer must never paint under "no project".
            client_->cancel();
            panelCancellable(false);
            catalog_ = module_tree::ModuleCatalog{};
            selectedModule_.clear();
            topCandidates_.clear();
            status_ = projectRoot_.isEmpty() ? noProjectReason() : noFilelistReason();
            deferred_ = false;
            userRequestQueued_ = false;
            rerunRequested_ = false;
            setProperty("rerunRequested", false);
            publishClosure();
            pushToTreePanel();
            pushToInterfacePanel();
            return;
        }
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
        // AC-2.2 (batch-20261115): the import of a filelist is an ASYNCHRONOUS
        // project expansion that saves `project-model.json` when it answers (with
        // the OLD file set until then). Cataloguing now would spend the run on
        // `vd_project_expand_v2`'s root-scan fallback and need a SECOND run when
        // the model lands - two catalogs (and two different file sets) for one
        // import. Wait for the answer instead; `refreshFinished` resumes exactly
        // once, including on failure, so a broken expansion still falls back
        // instead of hanging.
        if (projectModel() != nullptr && projectModel()->isPending()) {
            // AC M1.6: the user's top is REMEMBERED (not dropped) and the run
            // resumes exactly once when the expansion answers.
            deferred_ = true;
            status_ = QCoreApplication::translate(
                "VEdit", "Importing the project file set…");
            pushToTreePanel();
            pushToInterfacePanel();
            return;
        }
#endif
        deferred_ = false;
        // AC M1.3: single flight. A request that arrives while a catalog run is in
        // flight is remembered as ONE follow-up run, not started.
        if (client_->isRunning()) {
            rerunRequested_ = true;
            setProperty("rerunRequested", true);
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
        // The one-run permission for the large-project gate: consumed by THIS run
        // whether it starts or is withheld (AC M1.5).
        const bool allowLarge = userRequestQueued_;
        userRequestQueued_ = false;
        client_->setAllowLargeCatalog(allowLarge);
        ++catalogRuns_;
        setProperty("catalogRuns", catalogRuns_);
        panelCancellable(true);
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
        if (auto *model = projectModel(); model != nullptr && model->projectRoot() == projectRoot_) {
            client_->startFromFileSet(projectRoot_, model->files(), model->includeDirs(), model->defines());
            return;
        }
#endif
        client_->start(projectRoot_);
    }

    /// The panel's Cancel button (AC M1.4). It KILLS the running `slang` instead
    /// of merely dropping the handle, and it never starts anything.
    void cancelRun()
    {
        if (!client_->isRunning()) {
            status_ = QCoreApplication::translate("VEdit", "No module catalog run to cancel.");
            if (treePanel_ != nullptr) {
                treePanel_->setStatus(status_);
            }
            return;
        }
        client_->cancel();
        rerunRequested_ = false;
        userRequestQueued_ = false;
        setProperty("rerunRequested", false);
        panelCancellable(false);
        // G1-f: the closure the diagnostics lane consumes is re-published from the
        // catalog this holder actually holds. A cancelled run leaves `catalog_`
        // untouched but the shell property pointing at whatever the LAST
        // transition wrote, so the two could disagree (the root switch clears the
        // catalog, and this is the state a cancel then has to publish: empty).
        publishClosure();
        status_ = QCoreApplication::translate("VEdit", "Module catalog cancelled.");
        if (treePanel_ != nullptr) {
            treePanel_->setStatus(status_);
        }
    }

    void panelCancellable(bool on)
    {
        if (treePanel_ != nullptr) {
            treePanel_->setCancelEnabled(on);
        }
    }

#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
    /// The project model that owns the expansion/file set, when the project lane
    /// is installed (it always is in the product; a lane-only build has none).
    /// Cached because the shell owns it for its whole lifetime, and connected
    /// once so a deferred first run is resumed by the expansion's own answer.
    project::ProjectModel *projectModel()
    {
        if (model_ == nullptr && shell_ != nullptr) {
            model_ = shell_->findChild<project::ProjectModel *>();
            if (model_ != nullptr) {
                QObject::connect(model_, &project::ProjectModel::filelistsChanged, this, [this]() {
                    if (model_->projectRoot() == projectRoot_) {
                        client_->cancel();
                        panelCancellable(false);
                        catalog_ = module_tree::ModuleCatalog{};
                        publishScope();
                        publishClosure();
                        status_ = model_->hasFilelist()
                            ? QCoreApplication::translate("VEdit", "Importing the project file set…")
                            : noFilelistReason();
                        pushToTreePanel();
                        pushToInterfacePanel();
                    }
                });
                QObject::connect(model_, &project::ProjectModel::refreshFinished, this,
                                 [this](bool, const QString &) {
                                     // AC M1.6: the top typed while the expansion
                                     // was running is applied ONCE, before the run
                                     // reads the model (the expansion's own save may
                                     // have written the previous value back).
                                     applyPendingTop();
                                     if (!model_->hasFilelist()) {
                                         if (property("projectGate").toBool()) {
                                             refresh();
                                         }
                                         return;
                                     }
                                     const QString stamp = projectModelStamp(stateDirectory_);
                                     if (!deferred_ && (stamp.isEmpty() || stamp == runModelStamp_)) {
                                         return;
                                     }
                                     deferred_ = false;
                                     modelStamp_ = stamp;
                                     QTimer::singleShot(0, this, [this, root = projectRoot_, stamp]() {
                                         if (root == projectRoot_ &&
                                             (stamp.isEmpty() || stamp != runModelStamp_)) {
                                             refresh();
                                         }
                                     });
                                 });
            }
        }
        return model_;
    }
#endif

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
        client_->cancel();
        projectRoot_ = root;
        setProperty("projectRoot", projectRoot_);
        // RM-17: the model restores the NEW root's own top; read it before the
        // next run so A -> B -> A never carries a top across projects.
        top_ = effectiveTop();
        topCandidates_.clear();
        modelStamp_.clear();
        runModelStamp_.clear();
        modelPending_ = false;
        deferred_ = false;
        rerunRequested_ = false;
        hasPendingTop_ = false;
        pendingTop_.clear();
        setProperty("modelPending", false);
        setProperty("rerunRequested", false);
        catalog_ = module_tree::ModuleCatalog{};
        // G1-f: the old root's closure must not survive the switch. `catalog_` is
        // empty now, so this publishes the EMPTY closure (and closureModules = 0)
        // instead of leaving the previous project's file map on the shell for the
        // diagnostics lane's realtime session to pick up.
        publishClosure();
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

    /// AC-2.1 (batch-20261115): the folder is open but it is not a project yet.
    QString noFilelistReason() const
    {
        return QCoreApplication::translate(
            "VEdit",
            "No filelist — import a .f filelist (Project ▸ Import Filelist…) to build the "
            "module tree; until then every tool works on the open file.");
    }

    /// AC-3.1 seam (batch-20261115): the ONE-file-deep module closure, keyed by
    /// the file that DEFINES the module. The diagnostics lane consumes it to pick
    /// the realtime analysis file set (current file + the definition files of the
    /// submodules it directly instantiates) without linking this lane.
    ///
    /// Deliberately plain QVariant data (map of absolute file -> string list) on
    /// the shell property seam: the diagnostics lane builds standalone and must
    /// not learn this lane's classes. An empty map means "no catalog" and the
    /// realtime analysis then stays single-file.
    ///
    /// G1-c publishes the shadowed files here too (`vedit.moduleTree.shadowedFiles`,
    /// a plain QStringList): they are the catalog's answer to "which files must the
    /// compiler not see any more", and this is the one place a catalog result
    /// reaches other lanes.
    void publishClosure()
    {
        QVariantMap closure;
        // The content rule lives in the model (`buildModuleClosure`), where the
        // synthetic selftest can assert it: this holder only publishes it through
        // the shell property seam.
        const QHash<QString, QStringList> byFile = module_tree::buildModuleClosure(catalog_);
        for (auto it = byFile.constBegin(); it != byFile.constEnd(); ++it) {
            closure.insert(it.key(), it.value());
        }
        QStringList includedDefinitions;
        for (const auto &module : catalog_.modules) {
            if (!module.compilationUnit.isEmpty()) {
                includedDefinitions.append(module.file);
            }
        }
        includedDefinitions.removeDuplicates();
        includedDefinitions.sort();
        const bool closureChanged = closure != shell_->property("vedit.moduleTree.closure").toMap()
            || includedDefinitions != shell_->property("vedit.moduleTree.includedDefinitions").toStringList();
        shell_->setProperty("vedit.moduleTree.closure", closure);
        shell_->setProperty("vedit.moduleTree.includedDefinitions", includedDefinitions);
        if (closureChanged) {
            const auto revision = shell_->property("vedit.moduleTree.scopeRevision").toULongLong();
            shell_->setProperty("vedit.moduleTree.scopeRevision", revision + 1);
        }
        shell_->setProperty("vedit.moduleTree.closureModules",
                            catalog_.valid ? static_cast<int>(catalog_.modules.size()) : 0);
        // G1-c: the ONE duplicate-definition authority publishes the files it
        // dropped from the compiler input, so a consumer (the project lint pass,
        // the realtime session) subtracts them instead of letting `slang` decide
        // again. Same seam and same lifetime rules as the closure above: an empty
        // or failed catalog publishes an EMPTY list, never a stale one.
        shell_->setProperty("vedit.moduleTree.shadowedFiles", catalog_.shadowedFiles);
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
        invalidTop_ = false;
        topNote_.clear();
        QString chosen = top.trimmed();
        // AC-7.3 (batch-20261115): a name the catalog does not know cannot be
        // elaborated. Decide it HERE - deterministically, without spending a
        // slang run - and fall back to Auto with the reason on the summary line.
        // AC M1.6: this rejection needs a catalog that actually ANSWERED. With no
        // successful catalog (first run failed, large project withheld, timeout)
        // the name is ACCEPTED and sent as `--top`; the old rule silently cleared
        // it, which is one of the ways the tree stayed empty.
        if (!chosen.isEmpty() && catalog_.valid && !catalog_.modules.isEmpty()
            && catalog_.find(chosen) == nullptr) {
            topNote_ = QCoreApplication::translate(
                           "VEdit",
                           "top '%1' is not in the module catalog — fell back to Auto")
                           .arg(chosen);
            chosen.clear();
        }
        pendingTop_ = chosen;
        hasPendingTop_ = true;
        // AC M1.6: while the project file set is still being imported the model
        // is busy and its own save may write an older top. Remember the choice
        // and apply it ONCE when the expansion answers, instead of dropping it.
        if (projectModel() != nullptr && projectModel()->isPending()) {
            status_ = chosen.isEmpty()
                          ? QCoreApplication::translate(
                                "VEdit",
                                "Auto will be applied when the project file set is ready.")
                          : QCoreApplication::translate(
                                "VEdit", "Top '%1' will be applied when the project file set is "
                                         "ready.")
                                .arg(chosen);
            if (treePanel_ != nullptr) {
                treePanel_->setStatus(status_);
            }
            return;
        }
        applyPendingTop();
        refresh(true);
    }

    /// Writes the remembered top into the project model (or the fallback member
    /// when the project lane is absent) exactly once.
    void applyPendingTop()
    {
        if (!hasPendingTop_) {
            return;
        }
        hasPendingTop_ = false;
        const QString chosen = pendingTop_;
        top_ = chosen;
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
        if (shell_ != nullptr) {
            if (auto *model = shell_->findChild<project::ProjectModel *>()) {
                model->setTop(chosen);
            }
        }
#endif
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
        // AC-2.1: without a filelist there is nothing to choose a top for.
        treePanel_->setTopEnabled(!projectRoot_.isEmpty()
                                  && projectHasFilelist(projectRoot_, shell_));
        treePanel_->setTopNote(topNote_);
        treePanel_->setTopCandidates(topCandidates_);
        treePanel_->setEffectiveTop(effectiveTop());
        if (catalog_.valid) {
            treePanel_->setCatalog(catalog_, true);
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
    void finishRun()
    {
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
        const QString current = projectModelStamp(stateDirectory_);
        const bool modelChanged = !current.isEmpty() && current != runModelStamp_;
        if (modelChanged) {
            // The follow-up run will read this token: record it so the watch does
            // not schedule a second re-run for the same write.
            modelStamp_ = current;
        }
#endif
        // ONE follow-up run at most, and only for a reason:
        //  * a request that arrived while this run was in flight (AC M1.3),
        //  * a project-model write this run did not read (a real change),
        //  * the AC M1.1 unknown-top fallback (queued as `rerunRequested_`).
        //
        // AC M1.2: a FAILED run no longer re-runs itself. The old 1.5 s
        // unconditional retry stacked a second whole-project elaboration on top
        // of a run that had just timed out, which is what turned one timeout into
        // two or three concurrent `slang` children.
        const bool again = rerunRequested_ || modelPending_ || modelChanged;
        rerunRequested_ = false;
        modelPending_ = false;
        setProperty("rerunRequested", false);
        setProperty("modelPending", false);
        if (!again) {
            panelCancellable(false);
            return;
        }
        QTimer::singleShot(0, this, [this]() { refresh(); });
    }

    vedit::app::ApplicationShell *shell_ = nullptr;
    QString projectRoot_;
    QString stateDirectory_;
    module_tree::CatalogClient *client_ = nullptr;
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
    /// Cached project model (the expansion's owner); see `projectModel()`.
    QPointer<project::ProjectModel> model_;
#endif
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
    /// AC-2.2: the first run is waiting for the project lane's expansion answer.
    bool deferred_ = false;
    /// AC M1.3: a request arrived while a catalog run was in flight; exactly one
    /// follow-up run is owed when it ends.
    bool rerunRequested_ = false;
    /// AC M1.5: the next run may exceed the large-project threshold because the
    /// user asked for it (Refresh / a typed top). Consumed by that run.
    bool userRequestQueued_ = false;
    /// AC M1.6: a top typed while the project model was importing is applied once.
    QString pendingTop_;
    bool hasPendingTop_ = false;
    QString top_;
    QStringList topCandidates_;
    int catalogRuns_ = 0;
    /// AC-7.3: the last invalid-top fallback, shown instead of the raw failure.
    QString topNote_;
    /// One fallback per user choice; never a loop.
    bool invalidTop_ = false;
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
