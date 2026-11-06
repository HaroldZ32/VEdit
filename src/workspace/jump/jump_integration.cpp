#include "jump_integration.h"

#include "jump_registry.h"

#include <QDebug>

namespace vedit::workspace {

void installJump(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    (void)shell;
    (void)options;

    // A fresh shell must not inherit the previous shell's target: the registry
    // is process-wide and the old target usually belonged to a destroyed shell.
    // The shell/editor lane registers the real target after installPanels().
    jump::clearTarget();

    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo("jump: installJump registry reset; target=%s",
              jump::hasTarget() ? "installed" : "none");
    }
}

} // namespace vedit::workspace
