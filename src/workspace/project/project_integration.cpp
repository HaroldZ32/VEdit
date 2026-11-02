#include "project_integration.h"

#include "app_shell.h"
#include "project_model.h"
#include "project_panel.h"
#include "session_store.h"

#include <QDockWidget>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>
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
    const bool restored = model->restoreFromDisk();

    QString root = options.projectRoot;
    if (root.isEmpty() && restored) {
        root = model->projectRoot();
    }
    if (root.isEmpty()) {
        root = sessionProjectRoot(shell);
    }
    if (root.isEmpty()) {
        // Empty state: no project was opened, so the shell keeps its usual
        // layout instead of carrying an empty panel forever.
        delete model;
        return;
    }
    model->open(root);

    auto *panel = new project::ProjectPanel(model);
    auto *dock = new QDockWidget(QStringLiteral("Project"), &shell);
    dock->setObjectName(QStringLiteral("dock-project"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setWidget(panel);
    shell.addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->show();
}

} // namespace vedit::workspace
