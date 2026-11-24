#include "project_integration.h"

#include "app_shell.h"
#include "panel_registry.h"
#include "project_filelist.h"
#include "project_model.h"
#include "project_panel.h"
#include "project_subscription.h"
#include "session_store.h"

#include <QDockWidget>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>
#include <QStatusBar>
#include <QVariantMap>
#include <QStringLiteral>

#include <memory>

namespace vedit::workspace {

namespace {

/// G1-a: the ONE expansion result this lane publishes
/// (`vedit.project.files` + `vedit.project.filesRevision`), plus the identity it
/// was published from, so a `stateChanged` that did not move the set costs no
/// revision. The revision is what consumers follow: it moves when the set (or
/// the `-I`/`-D` that produced it) actually changed, and ALSO when the project
/// membership (`hasFilelist`) or the failure reason moved - a consumer that
/// re-reads on every signal cannot loop or thrash, while a transition that means
/// something to the user (an empty filelist import, an expansion that FAILED)
/// always reaches it (G1-a review, finding 1).
struct PublishedFileSet {
    quint64 revision = 0;
    QString root;
    bool hasFilelist = false;
    bool pending = false;
    QString error;
    QStringList files;
    QStringList includeDirs;
    QStringList defines;
};

// The directory this lane persists into: the shell's own state directory, so a
// `--state-dir` run keeps everything together.
QString resolveStateDirectory(const vedit::app::ApplicationShell &shell,
                              const IntegrationOptions &options)
{
    if (!options.stateDir.isEmpty()) {
        return options.stateDir;
    }
    const auto *store = shell.sessionStore();
    if (store == nullptr) {
        return QString();
    }
    return QFileInfo(store->path()).absolutePath();
}

// Fallback for the first start after an upgrade: the shell has persisted the
// project root in its session for a long time; the lane's own state file only
// appears once this panel has expanded a project once.
QString sessionProjectRoot(const vedit::app::ApplicationShell &shell)
{
    const auto *store = shell.sessionStore();
    if (store == nullptr) {
        return QString();
    }
    QString error;
    const QJsonObject session = store->load(&error);
    return session.value(QStringLiteral("project")).toString();
}

} // namespace

void installProject(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    const QString stateDirectory = resolveStateDirectory(shell, options);

    auto *model = new project::ProjectModel(stateDirectory, &shell);

    // RM-2: publish the model's EXPANSION INPUTS through the shell-property
    // seam for the lanes that must expand the SAME file set (diagnostics
    // lint/LSP, the module tree). Before this, those lanes expanded the bare
    // root with no imported `.f` lists, no variables table and no settings
    // store, so "the project" meant two different things depending on who
    // asked. The property is re-published on every input change; a consumer
    // must compare `root` before trusting the rest, so a project still loading
    // never contributes another project's lists.
    // `expansionInputs()` owns the map (batch-20261114 E2 added the referenced
    // set + layers there): the publisher, its consumers and the tests all read the
    // SAME builder, so the property cannot drift from the model. AC-1.5: the
    // panel's own editable `-I`/`-D` entries are an INPUT of the expansion, so
    // every consumer that expands the same project (the diagnostics lint/LSP lane,
    // the Preferences → Environment candidate filter) must see them and must
    // re-expand when they change.
    const auto publishInputs = [&shell, model]() {
        shell.setProperty("vedit.project.inputs", model->expansionInputs());
    };
    QObject::connect(model, &project::ProjectModel::stateChanged, &shell, publishInputs);
    QObject::connect(model, &project::ProjectModel::filelistsChanged, &shell, publishInputs);
    publishInputs();

    // G1-a (contract §10): the project file set is expanded ONCE - here - and
    // every other lane READS the result. The diagnostics lane used to expand the
    // same inputs itself (`vd_project_expand_v3` in its own poller), so "the
    // project file set" could mean two different lists in one process: the
    // module tree catalogued one and the realtime LSP session was bounded by the
    // other. This publisher is the single source: the UI, the tests and any
    // consumer read `vedit.project.files` + its revision, and the exact
    // `-I`/`-D` the expansion resolved are published beside it, so a consumer
    // never has to re-expand to learn them.
    //
    // `filesRevision` is the "a publisher EXISTS" marker as well as the change
    // token: it is set from revision 1 on, so a lane that installs into a shell
    // with no project lane (a standalone harness) sees the property absent and
    // keeps its own expansion - there, its expansion is the only one.
    // The model itself is published beside it for the RETRY seam: the
    // diagnostics lane owns no expansion any more, so "Run Project Lint" can
    // only retry by asking the lane that does. A `QObject*` under
    // `vedit.project.model` (the same shape `vedit.diagnosticsController` uses)
    // keeps the call dynamic: this lane does not have to link the diagnostics
    // lane, and a shell without one still reads the properties above.
    auto published = std::make_shared<PublishedFileSet>();
    const auto publishFileSet = [&shell, model, published]() {
        PublishedFileSet next;
        next.root = model->projectRoot();
        next.hasFilelist = model->hasFilelist();
        next.pending = model->isPending();
        next.files = model->files();
        next.includeDirs = model->includeDirs();
        next.defines = model->defines();
        next.error = model->lastError();
        // The revision is the CHANGE TOKEN consumers follow: it moves when the
        // expanded ANSWER (the set, or the -I/-D that produced it) changed, when
        // the project MEMBERSHIP changed (an imported empty `.f` produces no new
        // answer, but "no project" -> "project with 0 files" is a state the dock
        // must show), or when the FAILURE reason changed. G1-a review finding 1:
        // without the last two, a failed expansion (and an empty-filelist
        // import) left the revision untouched, so the diagnostics poller's
        // early return kept the dock on a stale "expanding…"/"No project file
        // set yet" forever.
        const bool answerChanged = next.files != published->files
            || next.includeDirs != published->includeDirs
            || next.defines != published->defines;
        const bool membershipChanged = next.hasFilelist != published->hasFilelist;
        const bool failureChanged = next.error != published->error;
        const quint64 revision = published->revision
            + (published->revision == 0 || answerChanged || membershipChanged
                       || failureChanged
                   ? 1
                   : 0);
        *published = next;
        published->revision = revision;
        shell.setProperty("vedit.project.files", published->files);
        shell.setProperty("vedit.project.includeDirs", published->includeDirs);
        shell.setProperty("vedit.project.defines", published->defines);
        shell.setProperty("vedit.project.hasFilelist", published->hasFilelist);
        shell.setProperty("vedit.project.filesPending", published->pending);
        shell.setProperty("vedit.project.filesError", published->error);
        shell.setProperty("vedit.project.filesRoot", published->root);
        shell.setProperty("vedit.project.filesRevision",
                          QVariant::fromValue<qulonglong>(published->revision));
    };
    QObject::connect(model, &project::ProjectModel::stateChanged, &shell, publishFileSet);
    publishFileSet();
    shell.setProperty("vedit.project.model", QVariant::fromValue<QObject *>(model));

    auto *panel = new project::ProjectPanel(model);
    auto *dock = new QDockWidget(QCoreApplication::translate("VEdit", "Project"), &shell);
    dock->setObjectName(QStringLiteral("dock-project"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setWidget(panel);
    // 这张 dock 由 lane 自己创建，绕过了 PanelRegistry::insert()；补上同一套
    // 卡片外观（veditDockCard 属性 + 投影/描边 + 调色板监听）。
    vedit::app::PanelRegistry::applyCardChrome(dock);
    shell.addDockWidget(Qt::LeftDockWidgetArea, dock);

    // The dock exists from the start (so opening a folder has somewhere to
    // appear) but is visible only while a project is open (AC-2.1).
    dock->hide();
    QObject::connect(model, &project::ProjectModel::stateChanged, dock, [dock, model]() {
        dock->setVisible(model->hasProject());
    });

    // The file tree announces the folder the user opened; that folder becomes
    // the project root. Name-based binding keeps this lane independent
    // (project_subscription.h).
    project::subscribeDirectoryOpened(&shell, model);

    // FIX 2: ONE lane-agnostic app signal, emitted for every successful document
    // open (File > Open, CLI, Files tree, Ctrl+P, search jump, project-panel
    // double click). This lane decides what a filelist extension means; the app
    // layer never learns a lane or an extension, so the direction stays src/app
    // -> no project lane. The connection is installed BEFORE the empty-state
    // early return below: "no project open" is exactly the case where importing
    // a `.f` has to set the root first.
    //
    // G1-a (contract §10; AGENT.md "Project mode is filelist-gated"): the project
    // file set is only ever widened by an EXPLICIT import. Opening a `.f` is
    // therefore NEVER an import on its own:
    //   * a project that already has a filelist -> the document opens as text and
    //     the model is not touched at all (user feedback: an old `.f` opened by
    //     accident polluted the file set for good);
    //   * a project with no filelist yet -> ONE question, whose default answer is
    //     "Open Only"; only an explicit "Import" sets the root and imports.
    // The existing explicit action (Project ▸ Import Filelist… -> the panel's
    // button, ProjectPanel::importFilelists) is unchanged.
    QObject::connect(
        &shell, &vedit::app::ApplicationShell::fileOpened, model,
        [&shell, model, panel](const QString &path) {
            const project::OpenedFilelistPlan plan =
                project::planOpenedFilelist(path, model->hasProject());
            if (!plan.isFilelist) {
                return;
            }
            const QString hint = QCoreApplication::translate(
                "VEdit", "Opened %1 as text; use Project \u25b8 Import Filelist to import it")
                                     .arg(plan.absolutePath);
            if (model->hasFilelist()
                || panel->confirmFilelistImport(plan.absolutePath)
                    != project::FilelistOpenChoice::Import) {
                // Non-modal: the status bar for the user right now, and one shell
                // diagnostic so the decision is visible after the message fades
                // (and in --selfcheck output). Nothing is imported, nothing is
                // re-rooted - `filelists` and `files` stay byte-identical.
                shell.addShellDiagnostic(hint);
                shell.statusBar()->showMessage(hint, 8000);
                return;
            }
            if (plan.needsRoot) {
                // The filelist's own directory becomes the project root
                // (RootSource::Filelist). The shell broadcast opens it on the
                // model (projectRootChanged below) BEFORE the import runs, so
                // importFilelists() already sees the root.
                shell.setProjectRoot(plan.root);
            }
            // Parse + diagnostics stay in ProjectModel::importFilelists (the Rust
            // parser). The panel is the existing surface for the result and for
            // the `filelist.*` diagnostics; the app layer shows no modal. The
            // import is idempotent: a second open of the same `.f` is a no-op.
            panel->importFilelists({plan.absolutePath});
        });

    // AC-1.2 (batch 1): a root switched from anywhere else (File -> Open Folder,
    // the shell API) is the same event. Guarded on the model's current root, so
    // the file tree's own directoryOpened — which already opened it through the
    // subscription above — is never expanded twice.
    QObject::connect(&shell, &vedit::app::ApplicationShell::projectRootChanged, &shell,
                     [model](const QString &root) {
                         if (root.isEmpty() || root == model->projectRoot()) {
                             return;
                         }
                         model->openRoot(root);
                     });

    // Restore before choosing a root: a persisted project is listed immediately,
    // then the live expansion replaces it.
    model->restoreFromDisk(nullptr, !options.startWithLiveProjectFiles);
    // AC-1.1 (batch 1): shell.projectRoot() is the ONE root — the shell resolved
    // --project else the persisted session archive BEFORE the lanes installed, so
    // an ordinary start expands the restored project on this very pass. The lane
    // state and the raw session key stay as fallbacks for a shell that started
    // without restore.
    QString root = shell.projectRoot();
    if (root.isEmpty() && model->hasProject()) {
        root = model->projectRoot();
    }
    if (root.isEmpty()) {
        root = sessionProjectRoot(shell);
    }
    if (root.isEmpty()) {
        // Empty state: the panel stays empty and hidden until a folder is opened
        // from the file tree.
        return;
    }
    dock->show();
    model->open(root);
}

project::ProjectModel *projectModelForShell(const vedit::app::ApplicationShell &shell)
{
    return shell.findChild<project::ProjectModel *>();
}

} // namespace vedit::workspace
