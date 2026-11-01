#include "settings_integration.h"

#include "app_shell.h"
#include "panel_registry.h"
#include "settings_panel.h"

#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace vedit::workspace {

void installSettings(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    if (shell.panels() == nullptr || shell.themes() == nullptr || shell.commands() == nullptr) {
        return;
    }
    QString directory = options.stateDir;
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (directory.isEmpty()) {
        directory = QDir::homePath() + QStringLiteral("/.config/vedit");
    }
    const QString path = directory + QStringLiteral("/settings.toml");
    vedit::app::ApplicationShell *shellPtr = &shell;
    const auto factory = [shellPtr, path]() {
        return new settings::SettingsPanel(shellPtr->themes(), shellPtr->commands(), path);
    };
    shell.panels()->setFactory(QStringLiteral("settings"), factory);
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo("settings: installSettings file='%s'", qPrintable(path));
    }
}

} // namespace vedit::workspace
