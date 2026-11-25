#include "../../common/runtime_init.h"
#include "pty_session.h"
#include "settings_dialog.h"
#include "settings_panel.h"
#include "vd_ffi.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {

using vedit::terminal::PtySession;
using vedit::workspace::settings::SettingsDialog;

bool check(const char *name, bool passed)
{
    std::printf("CHECK host-shell.%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}

bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(content) == content.size();
}

bool checkPty(const char *name, const QString &cwd, const QString &shell, bool environment = false)
{
    PtySession session;
    QByteArray output;
    QEventLoop wait;
    QObject::connect(&session, &PtySession::outputReady, &wait,
                     [&output](const QByteArray &data) { output += data; });
    QObject::connect(&session, &PtySession::finished, &wait, &QEventLoop::quit);
    const QString command = environment
        ? QStringLiteral("exec /bin/sh -c 'test \"$PATH\" = \"$HOST_TOOLS_TEST_PATH\" && "
                         "test \"$HOST_TOOLS_TEST_MARKER\" = fixture-import && "
                         "test -z \"${LD_LIBRARY_PATH+x}${LOCPATH+x}\" && printf HOST-TOOLS-PTY-OK'")
        : QStringLiteral("exit 0");
    QString error;
    const bool started = session.start({}, cwd, {QStringLiteral("-c"), command}, 80, 24, &error);
    if (started) {
        QTimer::singleShot(5000, &wait, &QEventLoop::quit);
        wait.exec();
    }
    return check(name, started && session.shellPath() == shell && session.exitKnown()
        && session.exitCode() == 0 && (!environment || output.contains("HOST-TOOLS-PTY-OK")));
}

} // namespace

int main(int argc, char **argv)
{
    vedit_runtime_init();
    QApplication app(argc, argv);
    QTemporaryDir fixture;
    if (!fixture.isValid()) {
        return 1;
    }
    const QString root = fixture.path();
    const QString store = fixture.filePath(QStringLiteral("settings.toml"));
    const QString appDir = fixture.filePath(QStringLiteral("AppDir"));
    const QString appBin = appDir + QStringLiteral("/bin");
    const QString hostBin = fixture.filePath(QStringLiteral("host bin"));
    const QString importedBin = fixture.filePath(QStringLiteral("imported bin"));
    const QString packagedShell = appBin + QStringLiteral("/bash");
    const QString nonExecutable = importedBin + QStringLiteral("/zsh");
    if (!QDir().mkpath(appBin) || !QDir().mkpath(hostBin) || !QDir().mkpath(importedBin)
        || !QFile::link(QStringLiteral("/bin/bash"), packagedShell)
        || !writeFile(nonExecutable, "not executable\n")) {
        return 1;
    }
    qputenv("SHELL", "/bin/bash");
    qputenv("APPDIR", QFile::encodeName(appDir));
    qputenv("PATH", QFile::encodeName(appBin));
    qputenv("VEDIT_HOST_PATH", QFile::encodeName(hostBin));
    qputenv("LD_LIBRARY_PATH", QFile::encodeName(appDir));
    qputenv("LOCPATH", QFile::encodeName(appDir));
    vd_host_use_settings_store(QFile::encodeName(store).constData());

    qputenv("HOST_TOOLS_TEST_PATH", QFile::encodeName(hostBin));
    qputenv("HOST_TOOLS_TEST_MARKER", "fixture-import");
    bool passed = checkPty("auto-restores-host-path", root, QStringLiteral("/bin/bash"), true);
    qunsetenv("HOST_TOOLS_TEST_MARKER");
    const QJsonArray imported{
        QJsonObject{{"name", "PATH"}, {"value", appBin + QLatin1Char(':') + importedBin}},
        QJsonObject{{"name", "HOST_TOOLS_TEST_PATH"}, {"value", importedBin}},
        QJsonObject{{"name", "HOST_TOOLS_TEST_MARKER"}, {"value", "fixture-import"}},
        QJsonObject{{"name", "LD_LIBRARY_PATH"}, {"value", "imported-loader"}},
        QJsonObject{{"name", "LOCPATH"}, {"value", "imported-locale"}},
    };
    const QString selection = fixture.filePath(QStringLiteral("environment.json"));
    auto *service = vd_service_create();
    const bool selected = writeFile(selection, QJsonDocument(QJsonObject{
        {"version", 1}, {"global", imported}, {"projects", QJsonArray{}},
    }).toJson()) && vd_env_apply_selection(service, QFile::encodeName(selection).constData(), nullptr, nullptr) == 1;
    vd_service_destroy(service);
    passed &= check("import-layer", selected);

    const QString manual = QFileInfo(QStringLiteral("/bin/tcsh")).isExecutable()
        ? QStringLiteral("/bin/tcsh") : QStringLiteral("/bin/sh");
    const auto comboOf = [](SettingsDialog &dialog) {
        dialog.panel()->tabs()->setCurrentWidget(dialog.panel()->toolsPage());
        return dialog.findChild<QComboBox *>(QStringLiteral("settings-tools-host-shell"));
    };
    {
        SettingsDialog dialog(nullptr, nullptr, store);
        if (!dialog.panel()->flush()) {
            return 1;
        }
        auto *combo = comboOf(dialog);
        if (combo == nullptr || combo->findData(manual) < 0) {
            return 1;
        }
        passed &= check("discovery", combo->currentData().toString().isEmpty()
            && combo->findData(packagedShell) < 0 && combo->findData(nonExecutable) < 0);
        combo->setCurrentIndex(combo->findData(manual));
        dialog.buttons()->button(QDialogButtonBox::Cancel)->click();
        passed &= check("cancel-keeps-auto", !QFileInfo::exists(store));
    }
    {
        SettingsDialog dialog(nullptr, nullptr, store);
        if (!dialog.panel()->flush()) {
            return 1;
        }
        auto *combo = comboOf(dialog);
        combo->setCurrentIndex(combo->findData(manual));
        passed &= checkPty("preview-keeps-auto", root, QStringLiteral("/bin/bash"), true);
        dialog.buttons()->button(QDialogButtonBox::Apply)->click();
        passed &= checkPty("apply-used-by-next-session", root, manual, true);
        combo->setCurrentIndex(0);
        dialog.buttons()->button(QDialogButtonBox::Apply)->click();
        passed &= checkPty("repeated-apply-uses-auto", root, QStringLiteral("/bin/bash"), true);
        combo->setCurrentIndex(combo->findData(manual));
        dialog.buttons()->button(QDialogButtonBox::Apply)->click();
        const QString screenshotDir = qEnvironmentVariable("VEDIT_HOST_TOOLS_SCREENSHOTS");
        if (!screenshotDir.isEmpty() && QApplication::platformName() == QStringLiteral("xcb")) {
            dialog.show();
            QApplication::processEvents();
            dialog.contentScroll()->ensureWidgetVisible(combo);
            QApplication::processEvents();
            passed &= check("xcb-screenshot", QDir().mkpath(screenshotDir)
                && dialog.grab().save(QDir(screenshotDir).filePath(QStringLiteral("settings-host-shell.png"))));
        }
    }
    vd_host_use_settings_store(nullptr);
    vd_host_use_settings_store(QFile::encodeName(store).constData());
    {
        SettingsDialog dialog(nullptr, nullptr, store);
        if (!dialog.panel()->flush()) {
            return 1;
        }
        passed &= check("reopen-restores-manual", comboOf(dialog)->currentData().toString() == manual);
    }
    passed &= checkPty("restart-restores-manual", root, manual, true);

    if (!writeFile(store, "\"tools.host_shell\" = \"/missing/native/bash\"\n")) {
        return 1;
    }
    {
        SettingsDialog dialog(nullptr, nullptr, store);
        if (!dialog.panel()->flush()) {
            return 1;
        }
        auto *combo = comboOf(dialog);
        passed &= check("stale-choice-shown", combo->currentText().contains(QStringLiteral("unavailable")));
        PtySession session;
        QString error;
        passed &= check("stale-choice-actionable", !session.start({}, root, {}, 80, 24, &error)
            && error.contains(QStringLiteral("/missing/native/bash")) && error.contains(QStringLiteral("Preferences")));
        combo->setCurrentIndex(0);
        dialog.buttons()->button(QDialogButtonBox::Ok)->click();
    }
    passed &= checkPty("ok-recovers-auto", root, QStringLiteral("/bin/bash"), true);
    return passed ? 0 : 1;
}
