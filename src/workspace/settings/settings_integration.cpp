#include "settings_integration.h"

#include "app_shell.h"
#include "settings_dialog.h"

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
    // T132: settings are a MODAL Preferences dialog (Settings -> Preferences...,
    // Ctrl+,), not a dock panel. The shell owns the menu command and calls the
    // opener installed here; this lane owns the dialog + the Rust store.
    vedit::app::ApplicationShell *shellPtr = &shell;
    shell.setPreferencesOpener([shellPtr, path]() {
        settings::SettingsDialog dialog(shellPtr->themes(), shellPtr->commands(), path, shellPtr);
        dialog.exec();
    });
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo("settings: installSettings file='%s'", qPrintable(path));
    }
}

} // namespace vedit::workspace
