#include "vcs_integration.h"

#include "app_shell.h"
#include "panel_registry.h"
#include "vcs_panel.h"
#include "vcs_source.h"

#include <QDebug>
#include <QString>

#include <memory>

namespace vedit::workspace {

void installVcs(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    if (shell.panels() == nullptr) {
        return;
    }
    const QString projectRoot = options.projectRoot;
    const auto factory = [projectRoot]() {
        auto *panel = new vcs::VcsPanel();
        panel->setSource(std::make_unique<vcs::FfiVcsSource>());
        panel->setRoot(projectRoot.toStdString());
        panel->refresh();
        return panel;
    };
    shell.panels()->setFactory(QStringLiteral("vcs"), factory);
    qInfo("vcs: installVcs projectRoot='%s'", qPrintable(projectRoot));
}

} // namespace vedit::workspace
