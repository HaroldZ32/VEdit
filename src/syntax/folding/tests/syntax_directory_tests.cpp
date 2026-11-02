// Unit test for the ONE shared syntax-directory resolution (task T109).
//
// T84 contract: `VEDIT_SYNTAX_DIR` is the DATA ROOT whose `syntax/` child holds
// the XML files; packaging historically passed the `syntax` subdirectory itself.
// Both spellings must normalise to `<root>/syntax`, and a data root without a
// `syntax/` child is not a valid root (the packaging "double copy" of the XMLs at
// the root is what this contract removes).
#include "syntax_directory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

using namespace vedit::syntax;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
}

void writeDefinition(const QString &path)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write("<language name=\"test\"/>\n");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir root;
    if (!root.isValid()) {
        std::printf("SYNTAX-DIRECTORY-TESTS ok=0 reason=temporary-directory\n");
        return 1;
    }
    const QString dataRoot = root.path();
    const QString syntaxDir = dataRoot + QStringLiteral("/syntax");
    QDir().mkpath(syntaxDir);
    writeDefinition(syntaxDir + QStringLiteral("/vedit-systemverilog.xml"));
    writeDefinition(syntaxDir + QStringLiteral("/vedit-verilog.xml"));

    check("syntax-dir.data-root-normalises-to-subdir",
          resolveSyntaxDefinitionDirectory(dataRoot) == syntaxDir,
          resolveSyntaxDefinitionDirectory(dataRoot));
    check("syntax-dir.subdir-is-accepted-as-is",
          resolveSyntaxDefinitionDirectory(syntaxDir) == syntaxDir,
          resolveSyntaxDefinitionDirectory(syntaxDir));
    check("syntax-dir.missing-path-is-empty",
          resolveSyntaxDefinitionDirectory(QStringLiteral("/nonexistent/t109")).isEmpty());
    check("syntax-dir.empty-string-is-empty", resolveSyntaxDefinitionDirectory(QString()).isEmpty());

    // The packaging double-copy layout: the XMLs sit in the data root with no
    // `syntax/` child. It must NOT be accepted as a definition directory: the
    // root contract requires `<root>/syntax` (this is what lets packaging drop
    // the duplicate copies).
    QTemporaryDir flatRoot;
    const bool flatValid = flatRoot.isValid();
    if (flatValid) {
        writeDefinition(flatRoot.path() + QStringLiteral("/vedit-systemverilog.xml"));
        writeDefinition(flatRoot.path() + QStringLiteral("/vedit-verilog.xml"));
        check("syntax-dir.flat-root-without-syntax-child-rejected",
              resolveSyntaxDefinitionDirectory(flatRoot.path()).isEmpty(),
              resolveSyntaxDefinitionDirectory(flatRoot.path()));
    } else {
        check("syntax-dir.flat-root-without-syntax-child-rejected", false,
              QStringLiteral("cannot create the temporary directory"));
    }

    check("syntax-dir.file-verilog",
          syntaxDefinitionFileName(QStringLiteral("Verilog"))
              == QStringLiteral("vedit-verilog.xml"));
    check("syntax-dir.file-systemverilog",
          syntaxDefinitionFileName(QStringLiteral("SystemVerilog"))
              == QStringLiteral("vedit-systemverilog.xml"));

    // The bare-build fallback: this test binary runs inside the checkout, so the
    // shared helper must find resources/syntax by walking up from the binary.
    const QString application = applicationSyntaxDefinitionDirectory();
    check("syntax-dir.application-fallback",
          !application.isEmpty()
              && QFileInfo(application + QStringLiteral("/vedit-systemverilog.xml")).isFile(),
          application);

    std::printf("SYNTAX-DIRECTORY-TESTS ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
