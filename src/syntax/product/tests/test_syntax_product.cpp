// Acceptance checks for the product wiring of the syntax stack (task T12).
//
// These are headless checks of OBSERVABLE FACTS, not of "how it looks":
//   * the compiled grammar id (G3) and the ids the registry really holds;
//   * the bundled definitions the private-path loader really loaded, and that
//     the KDE definitions it must refuse are refused (C7/C11);
//   * the editing kernel is linked and reachable (G2);
//   * a Verilog / SystemVerilog document receives highlighting ATTRIBUTES from
//     the repository (applied format ranges > 0) — the document-level fact the
//     user-visible colours are made of. "Looks right" is not claimed here.
#include "syntax_product.h"

#include <QGuiApplication>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
}

// Count the format ranges KSyntaxHighlighting applies to `text` when the given
// definition highlights it. -1 means "no definition / no repository".
int highlightedRanges(const QString &text, const QString &definitionName)
{
    KSyntaxHighlighting::Repository *repository = vedit::syntax::product::repository();
    if (repository == nullptr) {
        return -1;
    }
    const KSyntaxHighlighting::Definition definition = repository->definitionForName(definitionName);
    if (!definition.isValid()) {
        return -1;
    }
    QTextDocument document;
    document.setPlainText(text);
    KSyntaxHighlighting::SyntaxHighlighter highlighter(&document);
    highlighter.setDefinition(definition);
    highlighter.rehighlight();

    int ranges = 0;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (const QTextLayout *layout = block.layout()) {
            ranges += static_cast<int>(layout->formats().size());
        }
    }
    return ranges;
}

} // namespace

int main(int argc, char **argv)
{
    // The startup hook runs while QGuiApplication is constructed, so the private
    // data directory must be known BEFORE that: the ctest environment sets
    // VEDIT_SYNTAX_DIR, and a manual run falls back to the repository's
    // resources/ (the same files packaging installs).
    if (qEnvironmentVariableIsEmpty("VEDIT_SYNTAX_DIR")) {
        qputenv("VEDIT_SYNTAX_DIR", QByteArray(VEDIT_SYNTAX_RESOURCES_DIR));
    }
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-syntax-product-tests"));

    const vedit::syntax::product::Report &report = vedit::syntax::product::initialize();

    // G3: the baked-in grammar id and what the registry really holds.
    check("g3.compiled-grammar-is-verilog", report.compiled_grammar == QStringLiteral("verilog"));
    check("g3.tree-sitter-backend-compiled", report.backend_compiled);
    check("g3.verilog-registered", report.registered_grammars.contains(QStringLiteral("verilog")));
    check("g3.systemverilog-registered",
          report.registered_grammars.contains(QStringLiteral("systemverilog")));
    check("g3.startup-hook-ran", report.startup_hook_ran);
    check("g3.grammar-activated", report.grammar_activated);
    check("g3.active-grammar-is-verilog", report.active_grammar == QStringLiteral("verilog"));
    check("g3.tree-sitter-backend-available", report.backend_available);
    check("g3.tree-sitter-provider-created", report.provider_created);

    // G2: the editing kernel is linked and reachable from the product object.
    check("g2.editing-kernel-linked", report.editing_kernel.startsWith(QStringLiteral("KTextEditor")));

    // The bundled definitions: ours load, KDE's are refused.
    check("data.loaded", report.syntax_data_ok);
    check("data.own-verilog-xml", report.syntax_private_files.contains(QStringLiteral("vedit-verilog.xml")));
    check("data.own-systemverilog-xml",
          report.syntax_private_files.contains(QStringLiteral("vedit-systemverilog.xml")));
    check("data.verilog-definition", report.verilog_definition);
    check("data.systemverilog-definition", report.systemverilog_definition);
    check("data.no-embargoed-definition", report.embargoed_definitions.isEmpty());
    check("data.themes-available", !report.theme_names.isEmpty());

    // The highlighting attributes a .v / .sv document really receives.
    const int verilogRanges =
        highlightedRanges(QStringLiteral("module top;\n  assign out = 1;\nendmodule\n"),
                          QStringLiteral("Verilog"));
    std::printf("HIGHLIGHT verilog format-ranges=%d\n", verilogRanges);
    check("highlight.verilog-attributes", verilogRanges > 0);

    const int systemVerilogRanges = highlightedRanges(
        QStringLiteral("module top;\n  logic [7:0] bus;\n  always_comb bus = 8'h00;\nendmodule\n"),
        QStringLiteral("SystemVerilog"));
    std::printf("HIGHLIGHT systemverilog format-ranges=%d\n", systemVerilogRanges);
    check("highlight.systemverilog-attributes", systemVerilogRanges > 0);

    if (!report.diagnostics.isEmpty()) {
        std::printf("DIAGNOSTICS %s\n", qPrintable(report.diagnostics.join(QStringLiteral(" | "))));
    }
    std::printf("SYNTAX-PRODUCT-TESTS ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
