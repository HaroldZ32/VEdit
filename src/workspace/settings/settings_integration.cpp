#include "settings_integration.h"

#include "app_shell.h"
#include "settings_client.h"
#include "settings_dialog.h"
#include "settings_keys.h"

#if defined(VEDIT_SETTINGS_HAVE_FFI)
#include "vd_ffi.h"
#endif

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QString>

#include <cstdio>

namespace vedit::workspace {

namespace {

/// `<stateDir>` with the product's fallback chain: the ONE place this lane turns
/// a state directory into the directory that holds `settings.toml` and its
/// `environment.json` sibling (the Preferences page uses the same rule).
QString settingsDirectory(const QString &stateDir)
{
    QString directory = stateDir;
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (directory.isEmpty()) {
        directory = QDir::homePath() + QStringLiteral("/.config/vedit");
    }
    return directory;
}

#if defined(VEDIT_SETTINGS_HAVE_FFI)
/// The service's last typed reason, for the startup failure line.
QString lastError(VdService *service)
{
    VdError error{};
    if (vd_service_error(service, &error) == 1) {
        return QString::fromUtf8(error.message);
    }
    return QStringLiteral("(no reason recorded)");
}
#endif

void refreshFimPresentation(settings::SettingsClient &client, QWidget &window)
{
    client.load([&window](settings::Snapshot snapshot) {
        const auto value = [&snapshot](const char *key) {
            return settings::SettingsClient::valueOf(snapshot.entries, QString::fromLatin1(key))
                .trimmed();
        };
        const QString ghost = value(settings::keys::kFimGhostText).toLower();
        const bool owned = value(settings::keys::kFimUiOwned) == QStringLiteral("true");
        window.setProperty("vedit.fim.ghostText", snapshot.ok() && owned
            && (ghost == QStringLiteral("true") || ghost == QStringLiteral("1")
                || ghost == QStringLiteral("yes") || ghost == QStringLiteral("on")));
    });
}

} // namespace

void installSettings(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    if (shell.panels() == nullptr || shell.themes() == nullptr || shell.commands() == nullptr) {
        return;
    }
    const QString path = settingsDirectory(options.stateDir) + QStringLiteral("/settings.toml");
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    // batch-20261113 T6: bind the FIM request path to the SAME settings.toml the
    // Preferences dialog writes. Exactly ONE call, at startup, before any FIM
    // request can run. 0 also covers a settings.toml that does not exist YET (a
    // fresh install): the path is bound and the Rust side reads it per request,
    // so the normal case is the plain info line below, never a warning. -1 means
    // the path itself is unusable (NULL / empty / unencodable); startup keeps
    // going on the legacy vedit.toml + VEDIT_FIM_* configuration. No line ever
    // carries a key.
    const QByteArray fimSettingsPath = path.toUtf8();
    if (vd_fim_use_settings_store(fimSettingsPath.constData()) == -1) {
        qWarning("vedit-app: FIM settings store NOT bound: the path %s is unusable; FIM keeps "
                 "reading vedit.toml / VEDIT_FIM_API_KEY",
                 qPrintable(path));
    } else {
        qInfo("vedit-app: FIM settings store bound: %s", qPrintable(path));
    }
#endif
    // T132: settings are a MODAL Preferences dialog (Settings -> Preferences...,
    // Ctrl+,), not a dock panel. The shell owns the menu command and calls the
    // opener installed here; this lane owns the dialog + the Rust store.
    vedit::app::ApplicationShell *shellPtr = &shell;
    auto *fimPresentation = new settings::SettingsClient(path, shellPtr);
    refreshFimPresentation(*fimPresentation, shell);
    shell.setPreferencesOpener([shellPtr, path, fimPresentation]() {
        // batch-20261114 E2: the project lane publishes the filelist's referenced
        // variable set (E1 names + the layer each resolved in) through this
        // property. Taking the snapshot when the dialog OPENS is what makes the
        // candidate list "only what this project's filelist references" — the
        // dialog itself never runs an expansion and never owns a project.
        {
            settings::SettingsDialog dialog(shellPtr->themes(), shellPtr->commands(), path, shellPtr,
                                            shellPtr->property("vedit.project.inputs").toMap());
            dialog.exec();
        }
        refreshFimPresentation(*fimPresentation, *shellPtr);
    });
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo("settings: installSettings file='%s'", qPrintable(path));
    }
}

void applyEnvironmentSelection(const IntegrationOptions &options)
{
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    const QString selectionPath =
        settingsDirectory(options.stateDir) + QStringLiteral("/environment.json");
    VdService *service = vd_service_create();
    if (service == nullptr) {
        std::fprintf(stderr,
                     "vedit-app: environment selection not applied: the Rust service handle "
                     "could not be created\n");
        return;
    }
    const QByteArray path = selectionPath.toUtf8();
    const QByteArray root = options.projectRoot.toUtf8();
    char *warnings = nullptr;
    const int applied = vd_env_apply_selection(service, path.constData(),
                                               root.isEmpty() ? nullptr : root.constData(), &warnings);
    const QString warningText = warnings == nullptr ? QString() : QString::fromUtf8(warnings);
    if (warnings != nullptr) {
        vd_string_free(warnings);
    }
    if (applied != 1) {
        // AC-4.4 / T63: a refusal is never silent, and it names the file.
        std::fprintf(stderr, "vedit-app: environment selection not applied: %s (file: %s)\n",
                     qPrintable(lastError(service)), qPrintable(selectionPath));
    } else if (!warningText.isEmpty()) {
        // A broken `environment.json` degrades to the empty selection; the lint
        // and language-server children simply run without the imported values.
        std::fprintf(stderr, "vedit-app: environment selection warning: %s\n",
                     qPrintable(warningText));
    }
    // batch-20261115 AC-1.1: with the saved selection in place, the ONE automatic
    // csh-family capture runs in the BACKGROUND (the Rust side owns the thread: it
    // spawns the user's shell and must never block a start). It writes its marker
    // beside environment.json, merges only the entries the shell CHANGED into the
    // GLOBAL layer, and re-applies that layer itself — so the tool children, the
    // language server, the terminal and the toolchain resolver read one store.
    vd_env_autocapture_start(service, path.constData(),
                             root.isEmpty() ? nullptr : root.constData());
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        // One machine-readable line: the startup seam and its outcome are
        // observable from outside the product. It is gated like the lane's
        // other debug lines (settings: installSettings file=...) so an ordinary
        // start writes nothing to stdout; the FAILURE paths above are always on.
        std::printf("ENV-APPLY applied=%d selection=%s project_root=%s warnings=%d\n", applied,
                    qPrintable(selectionPath),
                    options.projectRoot.isEmpty() ? "<none>" : qPrintable(options.projectRoot),
                    warningText.isEmpty() ? 0 : 1);
        std::fflush(stdout);
    }
    vd_service_destroy(service);
#else
    (void)options;
#endif
}

} // namespace vedit::workspace
