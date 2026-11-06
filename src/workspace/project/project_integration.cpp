#include "project_integration.h"

#include "app_shell.h"
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
#include <QVariantMap>
#include <QStringLiteral>

namespace vedit::workspace {

namespace {

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
    const auto publishInputs = [&shell, model]() {
        QVariantMap inputs;
        inputs.insert(QStringLiteral("root"), model->projectRoot());
        inputs.insert(QStringLiteral("filelists"), model->filelists());
        inputs.insert(QStringLiteral("variables"), model->variables());
        inputs.insert(QStringLiteral("settingsPath"), model->settingsPath());
        inputs.insert(QStringLiteral("expansions"),
                      static_cast<qulonglong>(model->expansionCount()));
        shell.setProperty("vedit.project.inputs", inputs);
    };
    QObject::connect(model, &project::ProjectModel::stateChanged, &shell, publishInputs);
    QObject::connect(model, &project::ProjectModel::filelistsChanged, &shell, publishInputs);
    publishInputs();

    auto *panel = new project::ProjectPanel(model);
    auto *dock = new QDockWidget(QCoreApplication::translate("VEdit", "Project"), &shell);
    dock->setObjectName(QStringLiteral("dock-project"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setWidget(panel);
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
    // early return below: "no project open" is exactly the case where opening a
    // `.f` has to set the root first.
    QObject::connect(
        &shell, &vedit::app::ApplicationShell::fileOpened, model,
        [&shell, model, panel](const QString &path) {
            const project::OpenedFilelistPlan plan =
                project::planOpenedFilelist(path, model->hasProject());
            if (!plan.isFilelist) {
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
    model->restoreFromDisk();
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
