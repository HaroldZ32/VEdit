// T110 equivalence check: the KDE data loader now calls the ONE shared
// `VEDIT_SYNTAX_DIR` normalisation (src/syntax/common/syntax_directory.h). This
// test proves, through the loader's public report, that its effective search path
// equals the shared resolver's answer for BOTH accepted spellings (T84 data root
// and the legacy `syntax/` subdirectory), and that a rejected root stays rejected.
#include "ksyntax_data.h"
#include "syntax_directory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

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
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE language>\n"
               "<language name=\"Probe\" version=\"1\" kateversion=\"5.0\" section=\"Other\" "
               "extensions=\"*.prb\" mimetype=\"text/x-probe\"><highlighting>"
               "<list name=\"kw\"><item>probe</item></list>"
               "<contexts><context name=\"Normal\" attribute=\"Normal\" lineEndContext=\"#stay\"/>"
               "</contexts><itemDatas><itemData name=\"Normal\" defStyleNum=\"dsNormal\"/>"
               "</itemDatas></highlighting></language>\n");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("SYNTAX-DIRECTORY-EQUIVALENCE ok=0 reason=temporary-directory\n");
        return 1;
    }
    const QString dataRoot = temp.path();
    const QString syntaxDir = dataRoot + QStringLiteral("/syntax");
    QDir().mkpath(syntaxDir);
    writeDefinition(syntaxDir + QStringLiteral("/vedit-systemverilog.xml"));
    writeDefinition(syntaxDir + QStringLiteral("/vedit-verilog.xml"));

    const QString expected = vedit::syntax::resolveSyntaxDefinitionDirectory(dataRoot);
    check("equivalence.resolver-data-root", expected == syntaxDir, expected);

    {
        vedit::kde::KsyntaxData data(vedit::kde::DataConfig{dataRoot, {}});
        const vedit::kde::LoadReport &report = data.load();
        check("equivalence.kde-data-root.search-path", report.search_paths.contains(expected),
              report.search_paths.join(QStringLiteral(", ")));
        check("equivalence.kde-data-root.private-dir", report.private_dir == dataRoot,
              report.private_dir);
    }
    {
        // The legacy spelling must produce the same effective search path.
        vedit::kde::KsyntaxData data(vedit::kde::DataConfig{syntaxDir, {}});
        const vedit::kde::LoadReport &report = data.load();
        check("equivalence.kde-legacy-subdir.search-path", report.search_paths.contains(expected),
              report.search_paths.join(QStringLiteral(", ")));
        check("equivalence.kde-legacy-subdir.private-dir", report.private_dir == dataRoot,
              report.private_dir);
    }
    {
        QTemporaryDir flat;
        writeDefinition(flat.path() + QStringLiteral("/vedit-systemverilog.xml"));
        check("equivalence.resolver-flat-root-rejected",
              vedit::syntax::resolveSyntaxDefinitionDirectory(flat.path()).isEmpty());
        vedit::kde::KsyntaxData data(vedit::kde::DataConfig{flat.path(), {}});
        const vedit::kde::LoadReport &report = data.load();
        check("equivalence.kde-flat-root-no-search-path", report.search_paths.isEmpty(),
              report.search_paths.join(QStringLiteral(", ")));
        bool diagnostic = false;
        for (const QString &line : report.diagnostics) {
            if (line.contains(QStringLiteral("private syntax directory missing"))) {
                diagnostic = true;
            }
        }
        check("equivalence.kde-flat-root-diagnostic", diagnostic,
              report.diagnostics.join(QStringLiteral(" | ")));
    }

    std::printf("SYNTAX-DIRECTORY-EQUIVALENCE ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
