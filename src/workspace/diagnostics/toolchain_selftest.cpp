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
              report.userMessage().contains(QStringLiteral("Diagnostics unavailable"))
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

        // Discovery order: bundled > local checkout > PATH > env. Build one
        // candidate directory per tier with a fake executable and check which
        // tier wins as the earlier ones are removed.
        QTemporaryDir tiers;
        check("local.order.temp-dir", tiers.isValid());
        const QString bundledDir = tiers.path() + QStringLiteral("/bundled");
        const QString localDir = tiers.path() + QStringLiteral("/local");
        const QString pathDir = tiers.path() + QStringLiteral("/path");
        const QString envDir = tiers.path() + QStringLiteral("/env");
        for (const QString &dir : {bundledDir, localDir, pathDir, envDir}) {
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
        const auto resolveOrder = [&](bool withBundled, bool withLocal, bool withPath) {
            ToolchainSearchContext orderContext;
            if (withBundled) {
                orderContext.bundledDirs = QStringList{bundledDir};
            }
            if (withLocal) {
                orderContext.localDirs = QStringList{localDir};
            }
            if (withPath) {
                orderContext.pathDirs = QStringList{pathDir};
            }
            orderContext.env = orderEnv;
            return resolveToolchain(orderContext);
        };
        check("local.order.fake-env-written", writeFake(envDir + QLatin1Char('/') + veribleName));
        check("local.order.env-fallback",
              resolveOrder(false, false, false).verible.sourceLabel().startsWith(
                  QStringLiteral("env:")),
              resolveOrder(false, false, false).verible.describe());
        check("local.order.fake-path-written", writeFake(pathDir + QLatin1Char('/') + veribleName));
        check("local.order.path-over-env",
              resolveOrder(false, false, true).verible.sourceLabel() == QStringLiteral("PATH"),
              resolveOrder(false, false, true).verible.describe());
        check("local.order.fake-local-written", writeFake(localDir + QLatin1Char('/') + veribleName));
        check("local.order.local-over-path",
              resolveOrder(false, true, true).verible.sourceLabel() == QStringLiteral("local"),
              resolveOrder(false, true, true).verible.describe());
        check("local.order.fake-bundled-written",
              writeFake(bundledDir + QLatin1Char('/') + veribleName));
        check("local.order.bundled-over-all",
              resolveOrder(true, true, true).verible.sourceLabel() == QStringLiteral("bundled"),
              resolveOrder(true, true, true).verible.describe());
    }

    std::printf("VER-TOOLCHAIN-SELFTEST expect=%s checks=%d failures=%d\n",
                qPrintable(expect), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
