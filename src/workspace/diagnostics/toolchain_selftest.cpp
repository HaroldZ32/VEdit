// Toolchain discovery/lint self-test for the delivery form (T82).
//
//   vedit-toolchain-selftest --appdir <AppDir> --expect bundled|missing
//
// `bundled` : the AppDir's bin/ holds verible-verilog-ls + slang; the resolver
//             must report source=bundled for both, and running the bundled
//             verible-verilog-lint on a deliberately bad file must report a lint
//             diagnostic (proving the binary really works, not just that it exists).
// `missing` : the AppDir has no toolchain binaries (the evidence script moves
//             them away); the resolver must report source=missing for both, a
//             non-empty missing list, and a user message that says what is
//             missing, what it affects and how to fix it — with no crash and no
//             fabricated diagnostics.
//
// Deterministic by construction: PATH and env are NOT consulted in this test
// (pathDirs = {} and env = {}), so only the staged AppDir decides.

#include "toolchain.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>

using namespace vedit::workspace::diagnostics;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n",
                ok ? "PASS" : "FAIL",
                name,
                detail.isEmpty() ? "" : "  ",
                detail.isEmpty() ? "" : detail.toUtf8().constData());
}

QString writeLintFixture(QTemporaryDir &dir)
{
    const QString path = dir.path() + QStringLiteral("/bad_line_length.sv");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    // A line far beyond Verible's default line-length limit (100) is a stable,
    // tool-independent lint violation.
    const QByteArray longComment(140, 'x');
    file.write("module bad_line_length;\n// ");
    file.write(longComment);
    file.write("\nendmodule\n");
    return path;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString appDir;
    QString expect;
    QString lintFile;
    for (int index = 1; index < argc; ++index) {
        const QString arg = QString::fromLocal8Bit(argv[index]);
        if (arg == QStringLiteral("--appdir") && index + 1 < argc) {
            appDir = QString::fromLocal8Bit(argv[++index]);
        } else if (arg == QStringLiteral("--expect") && index + 1 < argc) {
            expect = QString::fromLocal8Bit(argv[++index]);
        } else if (arg == QStringLiteral("--lint-file") && index + 1 < argc) {
            lintFile = QString::fromLocal8Bit(argv[++index]);
        }
    }
    if (appDir.isEmpty() || (expect != QStringLiteral("bundled")
                             && expect != QStringLiteral("missing"))) {
        std::printf("usage: %s --appdir <dir> --expect bundled|missing\n", argv[0]);
        return 2;
    }

    ToolchainSearchContext context;
    context.bundledDirs = QStringList() << appDir << (appDir + QStringLiteral("/bin"));
    // No PATH, no env: the staged AppDir is the only input under test.
    context.pathDirs = QStringList();
    context.env = QHash<QString, QString>();

    const ToolchainReport report = resolveToolchain(context);
    std::printf("RESOLVED %s\n", qPrintable(report.statusLine()));
    std::printf("MESSAGE %s\n", qPrintable(report.userMessage()));

    if (expect == QStringLiteral("bundled")) {
        check("bundled.verible-source",
              report.verible.found() && report.verible.sourceLabel() == QStringLiteral("bundled"),
              report.verible.describe());
        check("bundled.slang-source",
              report.slang.found() && report.slang.sourceLabel() == QStringLiteral("bundled"),
              report.slang.describe());
        check("bundled.no-missing", report.missing().isEmpty(),
              report.missing().join(QLatin1Char(',')));

        QTemporaryDir fixture;
        const QString badFile =
            lintFile.isEmpty() ? writeLintFixture(fixture) : lintFile;
        check("bundled.lint-fixture-written", !badFile.isEmpty() && QFileInfo(badFile).isFile(),
              badFile);
        const QString lintBinary = appDir + QStringLiteral("/bin/verible-verilog-lint");
        check("bundled.lint-binary-present",
              QFileInfo(lintBinary).isFile() && QFileInfo(lintBinary).isExecutable(),
              lintBinary);
        if (!badFile.isEmpty() && QFileInfo(lintBinary).isExecutable()) {
            QProcess lint;
            lint.start(lintBinary, {badFile});
            lint.waitForFinished(30000);
            const QString output = QString::fromUtf8(lint.readAllStandardOutput())
                + QString::fromUtf8(lint.readAllStandardError());
            std::printf("LINT exit=%d output-lines=%d first=%s\n",
                        lint.exitCode(),
                        output.isEmpty() ? 0 : static_cast<int>(output.count(QLatin1Char('\n'))) + 1,
                        qPrintable(output.left(output.indexOf(QLatin1Char('\n')))
                                       .simplified()));
            check("bundled.lint-reports-violation",
                  lint.exitStatus() == QProcess::NormalExit && lint.exitCode() != 0
                      && output.contains(QStringLiteral("line-length")),
                  QStringLiteral("exit=%1").arg(lint.exitCode()));
        }
    } else {
        check("missing.verible", !report.verible.found(),
              report.verible.describe());
        check("missing.slang", !report.slang.found());
        const QStringList missing = report.missing();
        check("missing.list",
              missing.contains(QStringLiteral("verible"))
                  && missing.contains(QStringLiteral("slang")),
              missing.join(QLatin1Char(',')));
        check("missing.status-line",
              report.statusLine().contains(QStringLiteral("source=verible:missing"))
                  && report.statusLine().contains(QStringLiteral("slang:missing"))
                  && report.statusLine().contains(QStringLiteral("missing=[verible, slang]")),
              report.statusLine());
        check("missing.user-message",
              report.userMessage().contains(QCoreApplication::translate("VEdit", "Diagnostics unavailable"))
                  && report.userMessage().contains(QStringLiteral("Fix:")),
              report.userMessage());
        check("missing.no-fabricated-diagnostics", report.verible.path.isEmpty()
                                                       && report.slang.path.isEmpty());

        // T85: the user-facing message is ONE concise line; the probed candidate
        // list is a separate log detail and must never be part of it.
        const QString message = report.userMessage();
        check("missing.user-message-single-line", !message.contains(QLatin1Char('\n')), message);
        check("missing.user-message-concise", message.size() <= 500,
              QString::number(message.size()));
        check("missing.user-message-no-probe-wall",
              !message.contains(QStringLiteral("probed"))
                  && !message.contains(QStringLiteral("/usr/local/sbin")),
              message);
        check("missing.user-message-names-three-paths",
              message.contains(QStringLiteral("bin/")) && message.contains(QStringLiteral("PATH"))
                  && message.contains(QStringLiteral("VEDIT_VERIBLE_LS"))
                  && message.contains(QStringLiteral("VEDIT_SLANG"))
                  && message.contains(QStringLiteral("scripts/fetch-verible.sh"))
                  && message.contains(QStringLiteral("build/slang/build.sh")),
              message);
        check("missing.probe-detail-is-separate",
              report.probeDetail().contains(QStringLiteral("probed"))
                  && !message.contains(report.probeDetail()));

        // The product-wide order: setting -> bundled -> local checkout ->
        // imported project -> imported global -> PATH -> env. Build one
        // candidate directory per stage with a fake executable and check which
        // stage wins as the earlier ones are removed.
        QTemporaryDir tiers;
        check("local.order.temp-dir", tiers.isValid());
        const QString settingDir = tiers.path() + QStringLiteral("/setting");
        const QString bundledDir = tiers.path() + QStringLiteral("/bundled");
        const QString localDir = tiers.path() + QStringLiteral("/local");
        const QString projectDir = tiers.path() + QStringLiteral("/imported-project");
        const QString globalDir = tiers.path() + QStringLiteral("/imported-global");
        const QString pathDir = tiers.path() + QStringLiteral("/path");
        const QString envDir = tiers.path() + QStringLiteral("/env");
        for (const QString &dir : {settingDir,
                                   bundledDir,
                                   localDir,
                                   projectDir,
                                   globalDir,
                                   pathDir,
                                   envDir}) {
            QDir().mkpath(dir);
        }
        const auto writeFake = [](const QString &path) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return false;
            }
            file.write("#!/bin/sh\nexit 0\n");
            file.close();
            return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                   | QFileDevice::ExeOwner);
        };
        const QString veribleName = QStringLiteral("verible-verilog-ls");
        QHash<QString, QString> orderEnv;
        orderEnv.insert(QStringLiteral("VEDIT_VERIBLE_LS"), envDir + QLatin1Char('/') + veribleName);
        const auto resolveStages = [&](bool withSetting, bool withBundled, bool withLocal,
                                       bool withProject, bool withGlobal, bool withPath) {
            ToolchainSearchContext orderContext;
            if (withSetting) {
                orderContext.explicitPaths.insert(
                    veribleName, settingDir + QLatin1Char('/') + veribleName);
            }
            if (withBundled) {
                orderContext.bundledDirs = QStringList{bundledDir};
            }
            if (withLocal) {
                orderContext.localDirs = QStringList{localDir};
            }
            if (withProject) {
                orderContext.importedProjectVars.insert(
                    QStringLiteral("VEDIT_VERIBLE_LS"), projectDir + QLatin1Char('/') + veribleName);
            }
            if (withGlobal) {
                orderContext.importedGlobalVars.insert(
                    QStringLiteral("VEDIT_VERIBLE_LS"), globalDir + QLatin1Char('/') + veribleName);
            }
            if (withPath) {
                orderContext.pathDirs = QStringList{pathDir};
            }
            orderContext.env = orderEnv;
            return resolveToolchain(orderContext);
        };
        check("local.order.fake-env-written", writeFake(envDir + QLatin1Char('/') + veribleName));
        check("local.order.env-fallback",
              resolveStages(false, false, false, false, false, false)
                      .verible.sourceLabel()
                      .startsWith(QStringLiteral("env:")),
              resolveStages(false, false, false, false, false, false).verible.describe());
        check("local.order.fake-path-written", writeFake(pathDir + QLatin1Char('/') + veribleName));
        check("local.order.path-over-env",
              resolveStages(false, false, false, false, false, true).verible.sourceLabel()
                  == QStringLiteral("PATH"),
              resolveStages(false, false, false, false, false, true).verible.describe());
        check("local.order.fake-local-written", writeFake(localDir + QLatin1Char('/') + veribleName));
        check("local.order.local-over-path",
              resolveStages(false, false, true, false, false, true).verible.sourceLabel()
                  == QStringLiteral("local"),
              resolveStages(false, false, true, false, false, true).verible.describe());
        check("imported.order.fake-global-written",
              writeFake(globalDir + QLatin1Char('/') + veribleName));
        check("imported.order.global-over-path",
              resolveStages(false, false, false, false, true, true).verible.sourceLabel()
                  == QStringLiteral("imported-global"),
              resolveStages(false, false, false, false, true, true).verible.describe());
        check("imported.order.fake-project-written",
              writeFake(projectDir + QLatin1Char('/') + veribleName));
        check("imported.order.project-over-global",
              resolveStages(false, false, false, true, true, true).verible.sourceLabel()
                  == QStringLiteral("imported-project"),
              resolveStages(false, false, false, true, true, true).verible.describe());
        check("imported.order.local-over-project",
              resolveStages(false, false, true, true, true, false).verible.sourceLabel()
                  == QStringLiteral("local"),
              resolveStages(false, false, true, true, true, false).verible.describe());
        check("local.order.fake-bundled-written",
              writeFake(bundledDir + QLatin1Char('/') + veribleName));
        check("local.order.bundled-over-all",
              resolveStages(false, true, true, true, true, true).verible.sourceLabel()
                  == QStringLiteral("bundled"),
              resolveStages(false, true, true, true, true, true).verible.describe());
        check("setting.order.fake-setting-written",
              writeFake(settingDir + QLatin1Char('/') + veribleName));
        check("setting.order.setting-over-all",
              resolveStages(true, true, true, true, true, true).verible.sourceLabel()
                  == QStringLiteral("settings"),
              resolveStages(true, true, true, true, true, true).verible.describe());
        // R2: the legacy `VEDIT_VERIBLE_DIR` must resolve in BOTH halves. The
        // Rust resolver honours it (vd-config tools.rs), so the mirror must too.
        const QString legacyDir = tiers.path() + QStringLiteral("/legacy-dir");
        QDir().mkpath(legacyDir);
        check("legacy.order.fake-dir-written", writeFake(legacyDir + QLatin1Char('/') + veribleName));
        const ToolchainReport legacy = [&]() {
            ToolchainSearchContext legacyContext;
            legacyContext.env.insert(QStringLiteral("VEDIT_VERIBLE_DIR"), legacyDir);
            return resolveToolchain(legacyContext);
        }();
        check("legacy.order.VEDIT_VERIBLE_DIR-resolves",
              legacy.verible.sourceLabel().startsWith(QStringLiteral("env:")),
              legacy.verible.describe());

        // A stale imported variable is a failed probe, not a silent miss: the
        // recorded attempts stay ordered and the env override still resolves.
        const ToolchainReport stale = [&]() {
            ToolchainSearchContext staleContext;
            staleContext.importedProjectVars.insert(
                QStringLiteral("VEDIT_VERIBLE_LS"),
                tiers.path() + QStringLiteral("/gone/") + veribleName);
            staleContext.env = orderEnv;
            return resolveToolchain(staleContext);
        }();
        check("imported.order.stale-falls-through-to-env",
              stale.verible.sourceLabel().startsWith(QStringLiteral("env:")),
              stale.verible.describe());
        check("imported.order.stale-probe-recorded",
              stale.verible.searched.contains(tiers.path() + QStringLiteral("/gone/") + veribleName),
              stale.probeDetail());

        // E5 (batch-20261114): the four Scala env names must be registered by the
        // application context. Before the fix the table omitted them, so
        // `env:VEDIT_JAVA` (and the other three) could never resolve even with the
        // variable set. PATH is cleared for the call so the env stage is the only
        // one that can answer; it is restored immediately afterwards.
        const QString fakeJava = tiers.path() + QStringLiteral("/env-java");
        check("application.env.fake-java-written", writeFake(fakeJava));
        qputenv("VEDIT_JAVA", fakeJava.toLocal8Bit());
        const QByteArray savedPath = qgetenv("PATH");
        qputenv("PATH", QByteArray());
        const ToolchainSearchContext application = applicationToolchainContext();
        const ToolchainReport applicationReport = resolveToolchain(application);
        qputenv("PATH", savedPath);
        qunsetenv("VEDIT_JAVA");
        check("application.env.VEDIT_JAVA-registered",
              application.env.contains(QStringLiteral("VEDIT_JAVA")),
              QStringList(application.env.keys()).join(QLatin1Char(',')));
        check("application.env.VEDIT_JAVA-resolves",
              applicationReport.scalaJava.sourceLabel() == QStringLiteral("env:VEDIT_JAVA"),
              applicationReport.scalaJava.describe());
    }

    std::printf("VER-TOOLCHAIN-SELFTEST expect=%s checks=%d failures=%d\n",
                qPrintable(expect), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
