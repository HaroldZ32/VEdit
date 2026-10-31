// Standalone driver for the edit-core selfchecks (Slice 1, task T10).
//
// The product runs the same checks through `vedit-app --selfcheck`; this binary
// makes the kernel testable on its own (ctest) without the shell and without the
// Rust service layer.
//
//   vedit-edit-core-selftest                       # the regular selfchecks
//   vedit-edit-core-selftest --diagnose-highlight  # real KTextEditor highlighting
//                                                  # path, delivery-like XDG data
//                                                  # paths (T81)
//
// The second mode prepares a private "share" directory holding VEdit's two
// syntax definitions in the layout KSyntaxHighlighting looks for
// (`<share>/org.kde.syntax-highlighting/syntax/*.xml`) and points
// XDG_DATA_DIRS/XDG_DATA_HOME at it BEFORE QApplication exists — exactly what the
// delivered AppRun does (it narrows both to the AppDir). That is what makes the
// "author == VEdit" assertion meaningful: on a host with KDE installed the
// editor's repository would otherwise contain KDE's definitions instead.
#include "edit_core_selftest.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>

namespace {

bool copySyntaxDefinitions(const QString &shareDirectory)
{
    const QString target =
        shareDirectory + QStringLiteral("/org.kde.syntax-highlighting/syntax");
    if (!QDir().mkpath(target)) {
        return false;
    }
    const QStringList names{QStringLiteral("vedit-verilog.xml"),
                            QStringLiteral("vedit-systemverilog.xml")};
    for (const QString &name : names) {
        const QString source =
            QStringLiteral(VEDIT_SYNTAX_RESOURCES_DIR) + QStringLiteral("/syntax/") + name;
        const QString destination = target + QLatin1Char('/') + name;
        if (!QFile::exists(source)) {
            std::printf("HIGHLIGHT missing-syntax-file %s\n", qPrintable(source));
            return false;
        }
        if (QFile::exists(destination)) {
            QFile::remove(destination);
        }
        if (!QFile::copy(source, destination)) {
            std::printf("HIGHLIGHT cannot-copy %s -> %s\n", qPrintable(source),
                        qPrintable(destination));
            return false;
        }
    }
    return true;
}

void narrowXdgDataPaths(const QString &shareDirectory)
{
    qputenv("XDG_DATA_DIRS", shareDirectory.toLocal8Bit());
    qputenv("XDG_DATA_HOME", shareDirectory.toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", (shareDirectory + QStringLiteral("/config")).toLocal8Bit());
}

} // namespace

int main(int argc, char **argv)
{
    bool diagnoseHighlight = false;
    bool copyDefinitions = true;
    QString shareDirectory;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--diagnose-highlight")) {
            diagnoseHighlight = true;
        } else if (argument == QStringLiteral("--share") && index + 1 < argc) {
            shareDirectory = QString::fromLocal8Bit(argv[++index]);
            copyDefinitions = false;
        }
    }

    if (diagnoseHighlight) {
        // The temporary share must outlive QApplication (destroyed last below).
        QTemporaryDir temporaryShare;
        if (shareDirectory.isEmpty()) {
            if (!temporaryShare.isValid()) {
                std::printf("HIGHLIGHT-DIAGNOSIS checks=1 failures=1 (no temporary directory)\n");
                return 1;
            }
            shareDirectory = temporaryShare.path();
        }
        if (copyDefinitions && !copySyntaxDefinitions(shareDirectory)) {
            std::printf("HIGHLIGHT-DIAGNOSIS checks=1 failures=1 (syntax definitions not copied)\n");
            return 1;
        }
        narrowXdgDataPaths(shareDirectory);
        std::printf("HIGHLIGHT share='%s'\n", qPrintable(shareDirectory));

        QApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("vedit-edit-core-selftest"));
        const vedit::editor::SelfCheckResult result = vedit::editor::runHighlightDiagnosis();
        return result.failures == 0 ? 0 : 1;
    }

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-edit-core-selftest"));
    const vedit::editor::SelfCheckResult result = vedit::editor::runSelfChecks();
    return result.failures == 0 ? 0 : 1;
}
