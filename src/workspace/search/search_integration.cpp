#include "search_integration.h"

#include "app_shell.h"
#include "panel_registry.h"
#include "search_panel.h"

#include <QString>

namespace vedit::workspace {

void installSearch(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    if (shell.panels() == nullptr) {
        return;
    }
    const QString projectRoot = options.projectRoot;
    shell.panels()->setFactory(QStringLiteral("search"),
                               [projectRoot]() { return search::createSearchPanel(projectRoot); });
}

} // namespace vedit::workspace
