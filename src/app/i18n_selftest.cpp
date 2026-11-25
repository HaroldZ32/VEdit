// Packaged-AppDir regressions for translation lookup, restart launcher selection
// and restart environment. The test creates the launcher layouts on disk.
//
// FIX A: <AppDir>/bin/translations is the packaged location because AppRun
//        starts the product through the bundled loader, which makes Qt's
//        applicationDirPath() the AppDir ROOT.
// FIX B: restartApplication() must relaunch $APPIMAGE or <AppDir>/AppRun; in an
//        AppDir, applicationFilePath() is the bundled LOADER and dies on exec.
#include "i18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>

namespace {

bool writeExecutable(const QString &path, const QByteArray &contents = "#!/bin/sh\nexit 0\n")
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    if (file.write(contents) != contents.size() || !file.flush()) {
        return false;
    }
    file.close();
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                           | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                           | QFileDevice::ExeOther);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    int failures = 0;
    const auto check = [&failures](const char *name, bool ok, const QString &detail = QString()) {
        if (ok) {
            std::printf("ok   %s\n", name);
        } else {
            std::printf("FAIL %s%s%s\n", name, detail.isEmpty() ? "" : " — ",
                        detail.toLocal8Bit().constData());
            ++failures;
        }
    };

    QTemporaryDir root;
    if (!root.isValid()) {
        std::printf("FAIL fixture.tempdir\n");
        return 1;
    }

    // --- FIX A: the packaged layout is a real lookup candidate ---------------
    const QStringList candidates = vedit::app::i18n::translationCandidates(root.path());
    check("i18n.candidates.count", candidates.size() == 3,
          QStringLiteral("count=%1").arg(candidates.size()));
    check("i18n.candidates.packaged-first",
          candidates.value(0) == root.path() + QStringLiteral("/bin/translations"),
          candidates.value(0));

    QDir rootDir(root.path());
    check("i18n.fixture.bin-translations", rootDir.mkpath(QStringLiteral("bin/translations")));
    check("i18n.packaged-found",
          vedit::app::i18n::translationsDirectoryFor(root.path())
              == root.path() + QStringLiteral("/bin/translations"),
          vedit::app::i18n::translationsDirectoryFor(root.path()));

    // The build tree / tarball layout keeps the .qm beside the binary.
    rootDir.rmdir(QStringLiteral("bin/translations"));
    rootDir.rmdir(QStringLiteral("bin"));
    check("i18n.fixture.translations", rootDir.mkpath(QStringLiteral("translations")));
    check("i18n.buildtree-found",
          vedit::app::i18n::translationsDirectoryFor(root.path())
              == root.path() + QStringLiteral("/translations"),
          vedit::app::i18n::translationsDirectoryFor(root.path()));

    // A prefix install (appDir/../share/vedit/translations) is still honoured.
    QTemporaryDir prefix;
    const QString prefixAppDir = prefix.path() + QStringLiteral("/AppDir");
    check("i18n.fixture.prefix",
          prefix.isValid()
              && QDir().mkpath(prefixAppDir)
              && QDir().mkpath(prefix.path() + QStringLiteral("/share/vedit/translations")));
    const QString prefixFound = vedit::app::i18n::translationsDirectoryFor(prefixAppDir);
    check("i18n.prefix-found",
          QDir::cleanPath(prefixFound)
              == prefix.path() + QStringLiteral("/share/vedit/translations"),
          prefixFound);

    // --- FIX B: the AppDir launcher is AppRun, never the loader --------------
    QTemporaryDir appDir;
    const QString appRun = appDir.path() + QStringLiteral("/AppRun");
    const QString loader = appDir.path() + QStringLiteral("/ld-linux-x86-64.so.2");
    const QStringList arguments{loader, QStringLiteral("--project"), QStringLiteral("proj")};
    const QStringList forwarded{QStringLiteral("--project"), QStringLiteral("proj")};

    check("restart.fixture.apprun", writeExecutable(appRun));
    const vedit::app::i18n::RestartCommand packaged =
        vedit::app::i18n::restartCommand(QString(), appDir.path(), loader, arguments);
    check("restart.appdir.program", packaged.program == appRun, packaged.program);
    check("restart.appdir.arguments", packaged.arguments == forwarded,
          packaged.arguments.join(QLatin1Char(' ')));

    // A plain build tree has no AppRun, so applicationFilePath() is the program.
    const vedit::app::i18n::RestartCommand plain =
        vedit::app::i18n::restartCommand(QString(), root.path(), loader, arguments);
    check("restart.plain.program", plain.program == loader, plain.program);
    check("restart.plain.arguments", plain.arguments == forwarded,
          plain.arguments.join(QLatin1Char(' ')));

    // The AppImage runtime path wins over the extracted-AppDir probe.
    const QString image = QStringLiteral("/tmp/VEdit-0.2.0-x86_64.AppImage");
    const vedit::app::i18n::RestartCommand appimage =
        vedit::app::i18n::restartCommand(image, appDir.path(), loader, arguments);
    check("restart.appimage.program", appimage.program == image, appimage.program);
    check("restart.appimage.arguments", appimage.arguments == forwarded,
          appimage.arguments.join(QLatin1Char(' ')));
    check("restart.packaged-targets", appimage.packaged && packaged.packaged && !plain.packaged);

    const QString nextMount = root.path() + QStringLiteral("/next mount");
    const QString nextLaunch = nextMount + QStringLiteral("/AppRun");
    check("restart.remount.fixture", QDir().mkpath(nextMount) && writeExecutable(nextLaunch,
        "#!/bin/sh\n"
        "printf 'path='; /usr/bin/printenv PATH || printf '<unset>\\n'\n"
        "printf 'ld=%s\\nlocale=%s\\nappdir=%s\\nslang=%s\\nverible=%s\\narg=%s\\n' "
        "\"${LD_LIBRARY_PATH-<unset>}\" \"${LOCPATH-<unset>}\" \"${APPDIR-<unset>}\" "
        "\"${VEDIT_SLANG-<unset>}\" \"$VEDIT_VERIBLE_LS\" \"$1\"\n"));
    const QByteArray oldMount = QFile::encodeName(appDir.path());
    qputenv("APPDIR", oldMount);
    qputenv("PATH", oldMount + "/bin");
    qputenv("LD_LIBRARY_PATH", oldMount + "/lib:/custom/lib");
    qputenv("LOCPATH", oldMount + "/usr/lib/locale");
    qputenv("VEDIT_SLANG", oldMount + "/bin/slang");
    qputenv("VEDIT_VERIBLE_LS", "/custom/bin/verible with spaces");
    const QString arg = QStringLiteral("argument with spaces;*");
    const auto command = vedit::app::i18n::restartCommand(nextLaunch, appDir.path(), loader,
                                                         {loader, arg});
    const auto runNext = [&](const char *name, const QByteArray &expected) {
        QProcess process;
        vedit::app::i18n::configureRestartProcess(process, command, appDir.path());
        process.start();
        check(name, process.waitForFinished(5000) && process.exitCode() == 0
                        && process.readAllStandardOutput() == expected);
    };
    const QByteArray nextOutput = "\nld=/custom/lib\nlocale=<unset>\nappdir=<unset>\nslang=<unset>\n"
                                  "verible=/custom/bin/verible with spaces\narg=argument with spaces;*\n";
    const QByteArray hostPath = QByteArray("/host path::/usr/bin:/host-") + char(0xff) + ':';
    qputenv("VEDIT_HOST_PATH", hostPath);
    runNext("restart.remount.restores-host-path-and-overrides", "path=" + hostPath + nextOutput);
    qputenv("VEDIT_HOST_PATH", "");
    runNext("restart.remount.preserves-empty-path", "path=" + nextOutput);
    qunsetenv("VEDIT_HOST_PATH");
    runNext("restart.remount.preserves-unset-path", "path=<unset>" + nextOutput);

    // A non-executable AppRun is not a launcher: fall back to the binary.
    const auto readOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner
                          | QFileDevice::ReadGroup | QFileDevice::ReadOther;
    check("restart.fixture.chmod", QFile::setPermissions(appRun, readOnly));
    const vedit::app::i18n::RestartCommand nonexec =
        vedit::app::i18n::restartCommand(QString(), appDir.path(), loader, arguments);
    check("restart.nonexec.falls-back", nonexec.program == loader, nonexec.program);

    // Optional packaged-AppDir probe: pass a real AppDir as argv[1] and both
    // resolvers are checked against it (no GUI, no AppImage needed).
    if (argc > 1) {
        const QString probe = QString::fromLocal8Bit(argv[1]);
        const QString found = vedit::app::i18n::translationsDirectoryFor(probe);
        check("probe.translations", found == probe + QStringLiteral("/bin/translations"), found);
        const QString probeLoader = probe + QStringLiteral("/ld-linux-x86-64.so.2");
        const vedit::app::i18n::RestartCommand relaunch =
            vedit::app::i18n::restartCommand(QString(), probe, probeLoader,
                                             {probeLoader, QStringLiteral("--project"),
                                              QStringLiteral("probe")});
        check("probe.restart-selects-apprun", relaunch.program == probe + QStringLiteral("/AppRun"),
              relaunch.program);
        check("probe.restart-forwards-args",
              relaunch.arguments
                  == QStringList{QStringLiteral("--project"), QStringLiteral("probe")},
              relaunch.arguments.join(QLatin1Char(' ')));
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
