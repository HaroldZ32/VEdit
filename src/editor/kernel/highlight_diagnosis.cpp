// Runtime diagnosis of the real KTextEditor highlighting path (task T81).
//
// The user reported "no syntax colors at all" on the real machine. T12's
// evidence ("3/4 format ranges") was measured with VEdit's OWN
// KSyntaxHighlighting::Repository on a demo document — not with the repository
// and document that a KTextEditor::View actually renders. This diagnosis looks
// at the REAL path:
//
//   * which definitions KTextEditor::Editor::repository() holds (the editor's
//     repository, not VEdit's),
//   * what document mode / highlighting mode the product open path selects,
//   * how many format ranges and folding regions the definition the editor
//     resolves for that mode produces for the document text.
//
// Run by `vedit-edit-core-selftest --diagnose-highlight`; the caller points the
// XDG data paths at a private share that holds exactly the shipped definitions
// and, for the delivered runtime, the self-built F2 KSyntaxHighlighting library
// is put on LD_LIBRARY_PATH (the distro library embeds KDE definitions that the
// delivered library does not have). With both, this reproduces what the
// AppRun-launched product sees.
#include "edit_core_selftest.h"

#include "document_io.h"
#include "highlight_mode.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>

namespace vedit::editor {
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
    std::fflush(stdout);
}

void fact(const QString &line)
{
    std::printf("HIGHLIGHT %s\n", line.toUtf8().constData());
    std::fflush(stdout);
}

QString describe(const KSyntaxHighlighting::Definition &definition)
{
    return QStringLiteral("name='%1' author='%2' section='%3' version=%4 valid=%5")
        .arg(definition.name(), definition.author(), definition.section())
        .arg(definition.version())
        .arg(definition.isValid() ? 1 : 0);
}

bool writeFixture(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(contents) == contents.size();
}

// Opens one fixture through the product open path (loadFileIntoDocument via
// EditorDocument) and checks the mode and the ranges it would render.
void diagnoseDocument(const QString &scratch, const QString &fileName, const QByteArray &contents,
                      const QString &expectedMode)
{
    const QString path = scratch + QLatin1Char('/') + fileName;
    if (!writeFixture(path, contents)) {
        check("highlight.fixture-written", false, path);
        return;
    }
    QString error;
    std::unique_ptr<EditorDocument> document = EditorDocument::create(&error);
    if (document == nullptr) {
        check("highlight.document-created", false, error);
        return;
    }
    check("highlight.open-file", document->openFile(path, &error),
          QStringLiteral("%1 %2").arg(QFileInfo(path).fileName(), error));
    KTextEditor::Document *editorDocument = document->document();
    const QString mode = editorDocument->highlightingMode();
    fact(QStringLiteral("%1: url='%2' mode='%3' highlightingMode='%4' availableModes=%5")
             .arg(QFileInfo(path).fileName(), editorDocument->url().toLocalFile(),
                  editorDocument->mode(), mode)
             .arg(editorDocument->highlightingModes().size()));

    check("highlight.document-mode-is-expected", mode == expectedMode,
          QStringLiteral("%1: got '%2', expected '%3'")
              .arg(QFileInfo(path).fileName(), mode, expectedMode));

    const KSyntaxHighlighting::Definition definition = editorDefinitionForMode(mode);
    const HighlightStats stats = measureHighlighting(definition, editorDocument->text());
    fact(QStringLiteral("%1: resolved %2 -> formats=%3 begins=%4 ends=%5")
             .arg(QFileInfo(path).fileName(), describe(definition))
             .arg(stats.formats)
             .arg(stats.foldingBegins)
             .arg(stats.foldingEnds));
    check("highlight.definition-authored-by-vedit",
          definition.isValid() && definition.author() == QStringLiteral("VEdit"),
          describe(definition));
    check("highlight.format-ranges-positive", definition.isValid() && stats.formats > 0,
          QStringLiteral("%1: formats=%2").arg(fileName).arg(stats.formats));
    check("highlight.folding-regions-positive",
          definition.isValid() && stats.foldingBegins > 0 && stats.foldingEnds > 0,
          QStringLiteral("%1: begins=%2 ends=%3")
              .arg(fileName)
              .arg(stats.foldingBegins)
              .arg(stats.foldingEnds));
}

} // namespace

SelfCheckResult runHighlightDiagnosis()
{
    g_checks = 0;
    g_failures = 0;

    fact(QStringLiteral("xdg_data_dirs='%1'").arg(QString::fromLocal8Bit(qgetenv("XDG_DATA_DIRS"))));
    fact(QStringLiteral("xdg_data_home='%1'").arg(QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"))));
    fact(QStringLiteral("vedit_syntax_dir='%1'").arg(QString::fromLocal8Bit(qgetenv("VEDIT_SYNTAX_DIR"))));
    fact(QStringLiteral("generic_data_locations='%1'")
             .arg(QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)
                      .join(QLatin1Char(':'))));

    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    check("highlight.editor-instance", editor != nullptr);
    if (editor == nullptr) {
        return SelfCheckResult{g_checks, g_failures};
    }
    const KSyntaxHighlighting::Repository &repository = editor->repository();
    fact(QStringLiteral("editor_repository_definitions=%1 custom_paths=%2")
             .arg(repository.definitions().size())
             .arg(repository.customSearchPaths().size()));
    for (const KSyntaxHighlighting::Definition &definition : repository.definitions()) {
        fact(QStringLiteral("repository-definition %1").arg(describe(definition)));
    }
    const KSyntaxHighlighting::Definition verilog = repository.definitionForName(QStringLiteral("Verilog"));
    const KSyntaxHighlighting::Definition systemverilog =
        repository.definitionForName(QStringLiteral("SystemVerilog"));
    fact(QStringLiteral("definitionForName(Verilog): %1").arg(describe(verilog)));
    fact(QStringLiteral("definitionForName(SystemVerilog): %1").arg(describe(systemverilog)));
    fact(QStringLiteral("definitionForFileName(top.v): %1")
             .arg(describe(repository.definitionForFileName(QStringLiteral("top.v")))));
    fact(QStringLiteral("definitionForFileName(top.sv): %1")
             .arg(describe(repository.definitionForFileName(QStringLiteral("top.sv")))));
    check("highlight.repository-has-verilog", verilog.isValid(), describe(verilog));
    check("highlight.repository-verilog-is-vedit",
          verilog.isValid() && verilog.author() == QStringLiteral("VEdit"), describe(verilog));
    check("highlight.repository-has-systemverilog", systemverilog.isValid(), describe(systemverilog));

    QTemporaryDir scratch;
    check("highlight.scratch-dir", scratch.isValid());
    if (!scratch.isValid()) {
        return SelfCheckResult{g_checks, g_failures};
    }
    diagnoseDocument(scratch.path(), QStringLiteral("diagnose.v"),
                     QByteArrayLiteral("module top;\n  // comment\n  assign out = 1;\n"
                                       "  initial begin\n    out <= 1'b0;\n  end\nendmodule\n"),
                     QStringLiteral("Verilog"));
    diagnoseDocument(scratch.path(), QStringLiteral("diagnose.sv"),
                     QByteArrayLiteral("module top;\n  logic [7:0] q;\n  always_ff @(posedge clk)\n"
                                       "    q <= d;\nendmodule\n"),
                     QStringLiteral("SystemVerilog"));

    std::printf("HIGHLIGHT-DIAGNOSIS checks=%d failures=%d\n", g_checks, g_failures);
    return SelfCheckResult{g_checks, g_failures};
}

} // namespace vedit::editor
