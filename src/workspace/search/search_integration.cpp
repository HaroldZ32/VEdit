#include "search_integration.h"

#include "app_shell.h"
#include "panel_registry.h"
#include "search_jump_target.h"
#include "search_panel.h"

#include <QDebug>
#include <QString>

namespace vedit::workspace {

void installSearch(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    if (shell.panels() == nullptr) {
        return;
    }
    const QString projectRoot = options.projectRoot;

    // The panel only emits jumpRequested() (and forwards it to an installed
    // IJumpTarget); installing the target here is what makes a click on a
    // result open the hit file and put the cursor on its line in vedit-app
    // (verdict ws-search H1). One target per shell: it is a child of the shell,
    // so it outlives every panel instance the factory creates and is shared by
    // them (installSearch may be called again when the dock is re-created).
    auto *jumpTarget = shell.findChild<search::EditorJumpTarget *>();
    if (jumpTarget == nullptr) {
        jumpTarget = new search::EditorJumpTarget(&shell);
    }

    const auto factory = [projectRoot, jumpTarget]() {
        QWidget *panel = search::createSearchPanel(projectRoot);
        if (auto *searchPanel = qobject_cast<search::SearchPanel *>(panel)) {
            searchPanel->setJumpTarget(jumpTarget);
        }
        return panel;
    };
    shell.panels()->setFactory(QStringLiteral("search"), factory);
    qInfo("search: installSearch projectRoot='%s' jumpTarget=editor", qPrintable(projectRoot));
}

} // namespace vedit::workspace
