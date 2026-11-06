#include "i18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTranslator>

#include <memory>
#include <utility>

#include <cstdio>
#include <cstdlib>

namespace vedit::app::i18n {

namespace {

// The translator owned by this module. QPointer because the QTranslator is
// parented to the application and is destroyed with it at shutdown.
QPointer<QTranslator> g_translator;
// T38/C-class: Qt's own standard-dialog translations (OK/Cancel/Open/Save in
// QFileDialog, QMessageBox, QDialogButtonBox) live in qtbase_<tag>.qm, which the
// product vendors beside vedit_<tag>.qm. Installed first so a VEdit translation
// wins any (currently none) context collision.
QPointer<QTranslator> g_qtbaseTranslator;

RestartHandler g_restartHandler;
std::function<bool(QString *)> g_restartApplicationHandler;

// Reads the one `ui.language` pair out of the flat settings.toml. The Rust store
// writes dotted keys as quoted TOML keys ("ui.language" = "..."), so both the
// quoted and the bare spelling are accepted; a hand-written file may use either.
// Everything else in the file is ignored — this is a single-key reader, not a
// second TOML parser.
QString readLanguageSetting(const QString &settingsPath)
{
    QFile file(settingsPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    static const QRegularExpression pair(
        QStringLiteral("^\\s*(?:\"ui\\.language\"|'ui\\.language'|ui\\.language)\\s*=\\s*"
                       "(?:\"([^\"]*)\"|'([^']*)')\\s*(?:#.*)?$"));
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QRegularExpressionMatch match = pair.match(line);
        if (match.hasMatch()) {
            return match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
        }
    }
    return {};
}

void removeTranslator()
{
    for (QPointer<QTranslator> *slot : {&g_qtbaseTranslator, &g_translator}) {
        if (slot->isNull()) {
            continue;
        }
        QCoreApplication::removeTranslator(slot->data());
        QTranslator *stale = slot->data();
        *slot = nullptr;
        stale->deleteLater();
    }
}

} // namespace

QString languageSetting(UiLanguage language)
{
    switch (language) {
    case UiLanguage::Auto:
        return QStringLiteral("auto");
    case UiLanguage::English:
        return QStringLiteral("en");
    case UiLanguage::SimplifiedChinese:
        return QStringLiteral("zh_CN");
    }
    return QStringLiteral("auto");
}

QString languageKey()
{
    return QStringLiteral("ui.language");
}

UiLanguage languageFromSetting(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QLatin1String("auto")) {
        return UiLanguage::Auto;
    }
    if (normalized.startsWith(QLatin1String("zh"))) {
        return UiLanguage::SimplifiedChinese;
    }
    if (normalized.startsWith(QLatin1String("en"))) {
        return UiLanguage::English;
    }
    // An unknown value (hand-edited file, or a language this build does not
    // ship) follows the system instead of failing startup.
    return UiLanguage::Auto;
}

QString languageTag(UiLanguage language)
{
    switch (language) {
    case UiLanguage::English:
        return QStringLiteral("en");
    case UiLanguage::SimplifiedChinese:
        return QStringLiteral("zh_CN");
    case UiLanguage::Auto:
        break;
    }
    return QLocale::system().language() == QLocale::Chinese ? QStringLiteral("zh_CN")
                                                            : QStringLiteral("en");
}

QString settingsFilePath(const QString &stateDir)
{
    QString directory = stateDir;
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (directory.isEmpty()) {
        directory = QDir::homePath() + QStringLiteral("/.config/vedit");
    }
    return directory + QStringLiteral("/settings.toml");
}

QStringList translationCandidates(const QString &applicationDirPath)
{
    if (applicationDirPath.isEmpty()) {
        return {};
    }
    // The AppDir ships bin/vd-app and installs the .qm under bin/translations.
    // AppRun starts the product through the bundled loader, so Qt's
    // applicationDirPath() is the AppDir ROOT; the packaged candidate therefore
    // carries the bin/ segment. The build tree and tarball keep the .qm beside
    // the binary (<app>/translations); a prefix install uses ../share.
    return {
        applicationDirPath + QStringLiteral("/bin/translations"),
        applicationDirPath + QStringLiteral("/translations"),
        applicationDirPath + QStringLiteral("/../share/vedit/translations"),
    };
}

QString translationsDirectoryFor(const QString &applicationDirPath)
{
    const QStringList candidates = translationCandidates(applicationDirPath);
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.value(0);
}

QString translationsDirectory()
{
    const QString fromEnvironment = qEnvironmentVariable("VEDIT_TRANSLATIONS_DIR");
    if (!fromEnvironment.isEmpty()) {
        return fromEnvironment;
    }
    return translationsDirectoryFor(QCoreApplication::applicationDirPath());
}

InstallResult installTranslations(const QString &settingsPath, const QString &translationsDir)
{
    removeTranslator();

    InstallResult result;
    result.setting = readLanguageSetting(settingsPath);
    result.selection = languageFromSetting(result.setting);
    // Normalize the reported setting so callers never propagate an unknown
    // spelling; the file itself is only rewritten when the user picks a value.
    result.setting = languageSetting(result.selection);
    result.tag = languageTag(result.selection);

    // English is the source language: no file is needed, and a previous
    // translator has already been removed above.
    if (result.tag == QLatin1String("en")) {
        return result;
    }

    const QString fileName = QStringLiteral("vedit_%1.qm").arg(result.tag);
    // Resolve the directory once so the missing-file diagnostic names what was
    // actually probed. An explicit argument or $VEDIT_TRANSLATIONS_DIR is the
    // only probe in that case; otherwise keep the candidate list for the error.
    QStringList probed;
    QString directory = translationsDir;
    if (directory.isEmpty()) {
        const QString fromEnvironment = qEnvironmentVariable("VEDIT_TRANSLATIONS_DIR");
        if (!fromEnvironment.isEmpty()) {
            directory = fromEnvironment;
        } else {
            const QString appDir = QCoreApplication::applicationDirPath();
            probed = translationCandidates(appDir);
            directory = translationsDirectoryFor(appDir);
        }
    }
    const QString path = QDir(directory).filePath(fileName);
    if (!QFileInfo::exists(path)) {
        if (!probed.isEmpty()) {
            result.diagnostic =
                QStringLiteral("interface language %1 needs %2 but no translations directory "
                               "contains it (searched: %3)")
                    .arg(result.tag, fileName, probed.join(QStringLiteral(", ")));
        } else {
            result.diagnostic =
                QStringLiteral("interface language %1 needs %2 but the file is missing from %3")
                    .arg(result.tag, fileName, directory);
        }
        return result;
    }
    auto *translator = new QTranslator(QCoreApplication::instance());
    if (!translator->load(path)) {
        result.diagnostic =
            QStringLiteral("interface language file could not be loaded: %1").arg(path);
        delete translator;
        return result;
    }

    // T38/C-class: Qt's standard dialogs are translated by Qt itself, not by the
    // VEdit .qm. Load the vendored qtbase_<tag>.qm from the same directory; a
    // missing file leaves the standard buttons in English (not a startup error).
    QTranslator *qtbase = new QTranslator(QCoreApplication::instance());
    const QString qtbasePath =
        QDir(directory).filePath(QStringLiteral("qtbase_%1.qm").arg(result.tag));
    if (QFileInfo::exists(qtbasePath) && qtbase->load(qtbasePath)
        && QCoreApplication::installTranslator(qtbase)) {
        g_qtbaseTranslator = qtbase;
        result.qtbasePath = qtbasePath;
        result.qtbaseLoaded = true;
    } else {
        delete qtbase;
    }

    if (!QCoreApplication::installTranslator(translator)) {
        result.diagnostic =
            QStringLiteral("interface language file could not be installed: %1").arg(path);
        delete translator;
        return result;
    }
    g_translator = translator;
    result.qmPath = path;
    result.translated = true;
    return result;
}

RestartCommand restartCommand(const QString &appImage, const QString &applicationDirPath,
                              const QString &applicationFilePath, const QStringList &arguments)
{
    RestartCommand command;
    command.arguments = arguments.mid(1);
    if (!appImage.isEmpty()) {
        // The AppImage runtime sets $APPIMAGE; relaunching that file re-runs the
        // runtime and mounts a fresh image, which is exactly what a user expects.
        command.program = appImage;
        return command;
    }
    if (!applicationDirPath.isEmpty()) {
        // An extracted AppDir (and the AppImage's mount) starts the product via
        // AppRun -> bundled loader; applicationFilePath() is that loader, which
        // cannot run the program by itself. AppRun is the documented launcher.
        const QString appRun = QDir(applicationDirPath).filePath(QStringLiteral("AppRun"));
        const QFileInfo info(appRun);
        if (info.isFile() && info.isExecutable()) {
            command.program = appRun;
            return command;
        }
    }
    // Build tree / tarball: the executable is the program.
    command.program = applicationFilePath;
    return command;
}

bool restartApplication(QString *error)
{
    if (g_restartApplicationHandler) {
        return g_restartApplicationHandler(error);
    }
    const RestartCommand launch = restartCommand(
        qEnvironmentVariable("APPIMAGE"), QCoreApplication::applicationDirPath(),
        QCoreApplication::applicationFilePath(), QCoreApplication::arguments());
    if (launch.program.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("the application path is empty, so it cannot relaunch itself");
        }
        return false;
    }
    // One line so a packaged relaunch failure is diagnosable: the old code chose
    // the loader silently and the successor exited before it could say why.
    std::fprintf(stderr, "VEDIT-RESTART program=%s args=%s\n", qPrintable(launch.program),
                 qPrintable(launch.arguments.join(QLatin1Char(' '))));
    const QString workingDirectory = QDir::currentPath();
    if (!QProcess::startDetached(launch.program, launch.arguments, workingDirectory)) {
        if (error != nullptr) {
            *error = QStringLiteral("could not start a new instance: %1 %2")
                         .arg(launch.program, launch.arguments.join(QLatin1Char(' ')));
        }
        return false;
    }
    // The successor is running (detached and independent). Exit THIS process
    // immediately: the restart is invoked from the modal Preferences dialog, and
    // QCoreApplication::quit() does not leave a nested QDialog::exec() loop (it is
    // a no-op while the main event loop is not running — measured in the pinned
    // image). Closing the window instead would run closeEvent() -> a SECOND
    // archive write and guard. Nothing is at risk here: the unsaved-document guard
    // already ran and the language + session are already written, so the exit is
    // the "自动退出" half of the one-click restart. _Exit skips destructors on
    // purpose; the crash reporter is not involved because this is not a failure.
    std::fflush(nullptr);
    std::_Exit(EXIT_SUCCESS);
    return true; // unreachable: the default path exits above
}

void setRestartHandler(RestartHandler handler)
{
    g_restartHandler = std::move(handler);
}

void clearRestartHandler()
{
    g_restartHandler = nullptr;
}

RestartOutcome requestRestart(const QString &tag, const PersistLanguage &persist,
                              QString *error)
{
    if (!g_restartHandler) {
        if (error != nullptr) {
            *error = QStringLiteral("no restart handler is installed");
        }
        return RestartOutcome::Failed;
    }
    return g_restartHandler(tag, persist, error);
}

void setRestartApplicationHandler(std::function<bool(QString *)> handler)
{
    g_restartApplicationHandler = std::move(handler);
}

void clearRestartApplicationHandler()
{
    g_restartApplicationHandler = nullptr;
}

} // namespace vedit::app::i18n
