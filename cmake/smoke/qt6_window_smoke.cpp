// VEdit M0 build-base smoke check (task T10) plus the self-built
// KSyntaxHighlighting probe (task T60, plan B').
//
// One process that exercises what the M0 build base must deliver:
//   1. a Qt6 Widgets window (G1),
//   2. a working KTextEditor editor core + document (G2),
//   3. KSyntaxHighlighting definitions + a theme loaded through the public
//      repository API, including the custom-search-path mechanism C6 requires
//      (G3, C6),
//   4. the tree-sitter runtime linked and usable (G3a, C18).
//
// T60 mode (`--ksh-selfbuild`) additionally proves that the SELF-BUILT
// KSyntaxHighlighting (QRC_SYNTAX=OFF, NO_STANDARD_PATHS=ON) is ABI-compatible
// with the distro KTextEditor 6.24.0 / katepart: the editor core loads from the
// overridden library, the built-in definition set is empty, and a whitelisted
// definition loaded via addCustomSearchPath highlights Verilog by name.
//
// It prints one machine-readable `M0-SMOKE` line with the versions it was
// compiled against, the KSyntaxHighlighting library path the process actually
// loaded and the libc path (the A0c self-check hook T11 reuses), then exits 0
// only if every hard check passed. SystemVerilog is REPORTED but not asserted:
// C7's licence disposal is still open, so "the build image can load it" must
// not be mistaken for "the deliverable ships it".
//
// Usage:
//   vedit-m0-smoke [--selfcheck] [--testdata <dir>] [--expect-libc-prefix <dir>]
//                  [--hold <ms>]
//   vedit-m0-smoke --ksh-selfbuild [--expect-empty-syntax]
//                  [--custom-syntax-dir <dir>] [--highlight-check]
//                  [--expect-kshlib-prefix <dir>]

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QMainWindow>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QtGlobal>

#include <KTextEditor/Document>
#include <KTextEditor/Editor>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/FoldingRegion>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

#include <tree_sitter/api.h>

namespace {

// Path this process actually mapped for the first file whose file name matches
// `libraryName` (e.g. libc.so.6, libKF6SyntaxHighlighting.so.6); empty when
// /proc/self/maps is unavailable or nothing matched.
QString resolvedMappedLibrary(const QString &libraryName)
{
    QFile maps(QStringLiteral("/proc/self/maps"));
    if (!maps.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QRegularExpression re(QStringLiteral(R"(^\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(.+?/%1[^/]*)$)").arg(QRegularExpression::escape(libraryName)));
    const QStringList lines = QString::fromUtf8(maps.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const auto match = re.match(line);
        if (match.hasMatch()) {
            return match.captured(1);
        }
    }
    return QString();
}

QString firstThemeName(const KSyntaxHighlighting::Repository &repo)
{
    const auto themes = repo.themes();
    return themes.isEmpty() ? QString() : themes.first().name();
}

int validDefinitionCount(const KSyntaxHighlighting::Repository &repo)
{
    int valid = 0;
    const auto definitions = repo.definitions();
    for (const auto &definition : definitions) {
        if (definition.isValid()) {
            ++valid;
        }
    }
    return valid;
}

// Counts the format ranges the engine produces for one line: the observable
// "the definition really highlights" signal, without needing a QTextLayout.
class CountingHighlighter : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    void run(QStringView text)
    {
        highlightLine(text, KSyntaxHighlighting::State());
    }

    int formatCount() const
    {
        return m_formats;
    }

protected:
    void applyFormat(int, int, const KSyntaxHighlighting::Format &) override
    {
        ++m_formats;
    }

    void applyFolding(int, int, KSyntaxHighlighting::FoldingRegion) override
    {
    }

private:
    int m_formats = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-m0-smoke"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("selfcheck"), QStringLiteral("Run the non-interactive checks (default).")});
    parser.addOption({QStringLiteral("testdata"), QStringLiteral("Directory holding the project-owned test syntax definition."),
                      QStringLiteral("dir"), QStringLiteral(VEDIT_M0_TESTDATA_DIR)});
    parser.addOption({QStringLiteral("expect-libc-prefix"),
                      QStringLiteral("Fail unless the resolved libc.so.6 lives under this prefix (A0c hook)."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("hold"), QStringLiteral("Keep the window up for this many ms (visual check)."),
                      QStringLiteral("ms"), QStringLiteral("0")});
    // --- T60 (self-built KSyntaxHighlighting) mode ---------------------------
    parser.addOption({QStringLiteral("ksh-selfbuild"),
                      QStringLiteral("T60 mode: probe the self-built KSyntaxHighlighting + KTextEditor integration.")});
    parser.addOption({QStringLiteral("expect-empty-syntax"),
                      QStringLiteral("Fail unless the repository has zero VALID built-in definitions (QRC_SYNTAX=OFF).")});
    parser.addOption({QStringLiteral("custom-syntax-dir"),
                      QStringLiteral("Whitelist directory added with Repository::addCustomSearchPath (<dir>/syntax)."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("highlight-check"),
                      QStringLiteral("Highlight a Verilog snippet with the loaded definition and require formats.")});
    parser.addOption({QStringLiteral("expect-kshlib-prefix"),
                      QStringLiteral("Fail unless the loaded libKF6SyntaxHighlighting.so.6 lives under this prefix."),
                      QStringLiteral("dir")});
    parser.process(app);

    QStringList failures;
    const auto check = [&failures](bool ok, const QString &what) {
        if (!ok) {
            failures.append(what);
        }
        return ok;
    };
    const bool selfbuild = parser.isSet(QStringLiteral("ksh-selfbuild"));

    // 1. Qt6 Widgets window.
    QMainWindow window;
    window.setWindowTitle(QStringLiteral("VEdit M0 smoke"));
    window.resize(640, 480);
    window.show();
    QCoreApplication::processEvents();
    const bool windowVisible = check(window.isVisible(), QStringLiteral("QMainWindow is not visible"));
    const QString platform = qEnvironmentVariable("QT_QPA_PLATFORM", QStringLiteral("(default)"));

    // 2. KTextEditor editor core + a real document round-trip.
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    bool editorOk = editor != nullptr;
    bool documentOk = false;
    if (editorOk) {
        KTextEditor::Document *document = editor->createDocument(nullptr);
        if (document != nullptr) {
            document->setText(QStringLiteral("module top;\n  initial $display(\"m0\");\nendmodule\n"));
            documentOk = document->text().contains(QStringLiteral("endmodule"));
            delete document;
        }
    }
    check(editorOk, QStringLiteral("KTextEditor::Editor::instance() is null"));
    check(documentOk, QStringLiteral("KTextEditor document text round-trip failed"));

    // 3. KSyntaxHighlighting: what the loaded library itself provides, and the
    //    C6 custom-search-path mechanism with a whitelisted definition.
    KSyntaxHighlighting::Repository repository;
    const int builtinValidDefinitions = validDefinitionCount(repository);
    const int themeCount = repository.themes().size();
    const bool expectEmptySyntax = parser.isSet(QStringLiteral("expect-empty-syntax"));
    bool builtinVerilogOk = false;
    if (expectEmptySyntax) {
        check(builtinValidDefinitions == 0,
              QStringLiteral("expected zero valid built-in definitions (QRC_SYNTAX=OFF), found %1").arg(builtinValidDefinitions));
    } else {
        builtinVerilogOk = check(repository.definitionForName(QStringLiteral("Verilog")).isValid(),
                                 QStringLiteral("KSyntaxHighlighting::Definition \"Verilog\" is invalid"));
    }
    const auto systemVerilog = repository.definitionForName(QStringLiteral("SystemVerilog"));
    check(themeCount > 0, QStringLiteral("KSyntaxHighlighting repository has no themes"));

    const QString testdataDir = parser.value(QStringLiteral("testdata"));
    repository.addCustomSearchPath(testdataDir);
    const auto custom = repository.definitionForName(QStringLiteral("VEditSmoke"));
    const bool customPathOk = check(custom.isValid(),
                                    QStringLiteral("custom search path %1 did not provide the VEditSmoke definition").arg(testdataDir));

    // 3b. Whitelisted real definition (T60): load a Verilog definition from a
    //     private path by NAME (.v would resolve to "V"/vlang, see
    //     evidence/m0-build/REPORT.md 4.9), then prove it actually highlights.
    const QString customSyntaxDir = parser.value(QStringLiteral("custom-syntax-dir"));
    bool whitelistVerilogOk = false;
    int highlightFormats = -1;
    if (!customSyntaxDir.isEmpty()) {
        repository.addCustomSearchPath(customSyntaxDir);
        const auto verilog = repository.definitionForName(QStringLiteral("Verilog"));
        whitelistVerilogOk = check(verilog.isValid(),
                                   QStringLiteral("whitelist %1 did not provide the Verilog definition").arg(customSyntaxDir));
        if (parser.isSet(QStringLiteral("highlight-check")) && verilog.isValid()) {
            CountingHighlighter highlighter;
            highlighter.setDefinition(verilog);
            highlighter.run(QStringLiteral("module top; reg q; always @(posedge clk) q <= 1'b0; endmodule"));
            highlightFormats = highlighter.formatCount();
            check(highlightFormats > 0,
                  QStringLiteral("the Verilog definition produced no highlight formats"));
        }
    }

    // 3c. KTextEditor's own repository (T60 integration probe). It is reachable
    //     only as a const reference, so under NO_STANDARD_PATHS=ON the question
    //     "can the editor see our whitelist?" is answered by measurement, not
    //     assumption. The const_cast below is PROBE-ONLY and reported as such.
    int kteBuiltinValidDefinitions = -1;
    bool kteWhitelistVisible = false;
    if (selfbuild && editorOk) {
        kteBuiltinValidDefinitions = validDefinitionCount(editor->repository());
        if (!customSyntaxDir.isEmpty()) {
            auto &editableRepository = const_cast<KSyntaxHighlighting::Repository &>(editor->repository());
            editableRepository.addCustomSearchPath(customSyntaxDir);
            kteWhitelistVisible = editor->repository().definitionForName(QStringLiteral("Verilog")).isValid();
        }
    }

    // 4. tree-sitter runtime.
    TSParser *tsParser = ts_parser_new();
    const bool treeSitterOk = check(tsParser != nullptr, QStringLiteral("ts_parser_new() returned null"));
    if (tsParser != nullptr) {
        ts_parser_delete(tsParser);
    }

    // A0c hook: report (and optionally assert) the libc this process resolved,
    // and (T60) which libKF6SyntaxHighlighting it loaded.
    const QString libcPath = resolvedMappedLibrary(QStringLiteral("libc.so.6"));
    const QString expectLibcPrefix = parser.value(QStringLiteral("expect-libc-prefix"));
    if (!expectLibcPrefix.isEmpty()) {
        check(libcPath.startsWith(expectLibcPrefix),
              QStringLiteral("libc.so.6 resolved to %1, outside the expected prefix %2").arg(libcPath, expectLibcPrefix));
    }
    const QString kshLibPath = resolvedMappedLibrary(QStringLiteral("libKF6SyntaxHighlighting.so.6"));
    const QString expectKshLibPrefix = parser.value(QStringLiteral("expect-kshlib-prefix"));
    if (!expectKshLibPrefix.isEmpty()) {
        check(kshLibPath.startsWith(expectKshLibPrefix),
              QStringLiteral("libKF6SyntaxHighlighting.so.6 resolved to %1, outside the expected prefix %2")
                  .arg(kshLibPath, expectKshLibPrefix));
    }

    const bool ok = failures.isEmpty();
    const QString summary =
        QStringLiteral("M0-SMOKE ok=%1 platform=%2 qt=%3 kf6=%4 ktexteditor=%5 ksynhl=%6 tree_sitter=%7 "
                       "window=%8 editor=%9 document=%10 definitions=%11 themes=%12 verilog=%13 "
                       "systemverilog=%14 custom_path=%15 tree_sitter_parser=%16 "
                       "builtin_valid_defs=%17 whitelist_verilog=%18 highlight_formats=%19 "
                       "kshlib=%20 libc=%21")
            .arg(ok ? 1 : 0)
            .arg(platform)
            .arg(QStringLiteral(VEDIT_M0_QT_VERSION))
            .arg(QStringLiteral(VEDIT_M0_KF6_VERSION))
            .arg(editorOk ? 1 : 0)
            .arg(QStringLiteral(VEDIT_M0_KSYNTAXHIGHLIGHTING_VERSION))
            .arg(QStringLiteral(VEDIT_M0_TREE_SITTER_VERSION))
            .arg(windowVisible ? 1 : 0)
            .arg(editorOk ? 1 : 0)
            .arg(documentOk ? 1 : 0)
            .arg(builtinValidDefinitions + (customPathOk ? 1 : 0) + (whitelistVerilogOk ? 1 : 0))
            .arg(themeCount)
            .arg(builtinVerilogOk ? 1 : 0)
            .arg(systemVerilog.isValid() ? QStringLiteral("loadable(not-asserted,C7-pending)") : QStringLiteral("absent"))
            .arg(customPathOk ? 1 : 0)
            .arg(treeSitterOk ? 1 : 0)
            .arg(builtinValidDefinitions)
            .arg(whitelistVerilogOk ? 1 : 0)
            .arg(highlightFormats)
            .arg(kshLibPath.isEmpty() ? QStringLiteral("<unknown>") : kshLibPath)
            .arg(libcPath.isEmpty() ? QStringLiteral("<unknown>") : libcPath);

    QTextStream out(stdout);
    out << summary << Qt::endl;
    out << QStringLiteral("M0-SMOKE theme=%1").arg(firstThemeName(repository)) << Qt::endl;
    if (selfbuild) {
        out << QStringLiteral("M0-SMOKE ksh-selfbuild ktexteditor_repo_valid_defs_before=%1 "
                              "ktexteditor_repo_whitelist_visible=%2 (const_cast probe only)")
                   .arg(kteBuiltinValidDefinitions)
                   .arg(kteWhitelistVisible ? 1 : 0)
            << Qt::endl;
    }
    for (const QString &failure : failures) {
        out << QStringLiteral("M0-SMOKE FAIL: ") << failure << Qt::endl;
    }

    const int holdMs = parser.value(QStringLiteral("hold")).toInt();
    if (holdMs > 0) {
        QEventLoop loop;
        QTimer::singleShot(holdMs, &loop, &QEventLoop::quit);
        loop.exec();
    }

    return ok ? 0 : 1;
}
