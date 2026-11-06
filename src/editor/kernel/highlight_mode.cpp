#include "highlight_mode.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/FoldingRegion>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>

#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QCoreApplication>

namespace vedit::editor {
namespace {

constexpr auto kVEditAuthor = "VEdit";

// Counts what the highlighting engine applies. Same engine the KTextEditor view
// renders with; AbstractHighlighter is the supported way to observe it.
class RangeCounter final : public KSyntaxHighlighting::AbstractHighlighter {
public:
    void applyFormat(int, int, const KSyntaxHighlighting::Format &) override { ++stats_.formats; }
    void applyFolding(int, int, KSyntaxHighlighting::FoldingRegion region) override
    {
        if (region.type() == KSyntaxHighlighting::FoldingRegion::Begin) {
            ++stats_.foldingBegins;
        } else if (region.type() == KSyntaxHighlighting::FoldingRegion::End) {
            ++stats_.foldingEnds;
        }
    }
    HighlightStats run(const KSyntaxHighlighting::Definition &definition, const QString &text)
    {
        stats_ = HighlightStats{};
        setDefinition(definition);
        KSyntaxHighlighting::State state;
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            state = highlightLine(QStringView(line), state);
        }
        return stats_;
    }

private:
    HighlightStats stats_;
};

struct ExtensionDefinition {
    const char *suffix; // lower case, without the dot
    const char *name;   // KSyntaxHighlighting definition name
};

// VEdit's own extension table. It mirrors the `extensions=` attribute of the
// definitions shipped in resources/syntax (vedit-verilog/vedit-systemverilog/
// vedit-vhdl/vedit-markdown/vedit-tcl.xml) and is consulted BEFORE the editor's
// repository for two reasons:
//   * `.v` must stay "Verilog" even on a developer host where KDE's own "V"
//     (vlang) definition also claims `*.v`;
//   * `.sdc` / `.xdc` are Tcl by the product decision, while the Tcl definition
//     deliberately does not have to claim them in every repository.
// A suffix that is NOT in this table falls through to the repository, which may
// return any valid definition (the author is irrelevant there).
constexpr ExtensionDefinition kExtensionTable[] = {
    {"v", "Verilog"},      {"vh", "Verilog"},   {"vlg", "Verilog"},
    {"sv", "SystemVerilog"}, {"svh", "SystemVerilog"},
    {"vhd", "VHDL"},       {"vhdl", "VHDL"},
    {"md", "Markdown"},    {"markdown", "Markdown"}, {"mdown", "Markdown"}, {"mkd", "Markdown"},
    {"tcl", "Tcl"},        {"tk", "Tcl"},       {"sdc", "Tcl"}, {"xdc", "Tcl"},
};

QString modeFromExtension(const QString &absolutePath)
{
    const QString suffix = QFileInfo(absolutePath).suffix().toLower();
    if (suffix.isEmpty()) {
        return {};
    }
    for (const ExtensionDefinition &entry : kExtensionTable) {
        if (suffix == QLatin1String(entry.suffix)) {
            return QString::fromLatin1(entry.name);
        }
    }
    return {};
}

} // namespace

QString highlightModeForFile(const QString &absolutePath)
{
    // Explicit table FIRST (A3): a table hit is final, so `.v` never becomes the
    // host's "V" definition and `.sdc` / `.xdc` resolve to Tcl by decision.
    const QString byTable = modeFromExtension(absolutePath);
    if (!byTable.isEmpty()) {
        return byTable;
    }
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor != nullptr) {
        // Fallback for extensions VEdit does not own: accept ANY valid definition
        // the editor repository resolves the file name to (A3 dropped the former
        // `author == "VEdit"` requirement, which rejected every host definition).
        const KSyntaxHighlighting::Definition byFileName =
            editor->repository().definitionForFileName(QFileInfo(absolutePath).fileName());
        if (byFileName.isValid()) {
            return byFileName.name();
        }
    }
    return {};
}

KSyntaxHighlighting::Definition editorDefinitionForMode(const QString &mode)
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr || mode.isEmpty()) {
        return {};
    }
    return editor->repository().definitionForName(mode);
}

bool applyHighlightingMode(KTextEditor::Document *document, const QString &absolutePath,
                           QString *diagnostic)
{
    if (diagnostic != nullptr) {
        diagnostic->clear();
    }
    if (document == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = QCoreApplication::translate("VEdit", "no document to highlight");
        }
        return false;
    }
    const QString mode = highlightModeForFile(absolutePath);
    if (mode.isEmpty()) {
        if (diagnostic != nullptr) {
            *diagnostic = QCoreApplication::translate("VEdit", "no syntax definition for %1")
                              .arg(QFileInfo(absolutePath).fileName());
        }
        return false;
    }
    const KSyntaxHighlighting::Definition definition = editorDefinitionForMode(mode);
    if (!definition.isValid()) {
        if (diagnostic != nullptr) {
            *diagnostic = QStringLiteral("the editor repository has no '%1' definition; "
                                         "the view would render without colors")
                              .arg(mode);
        }
        return false;
    }
    if (diagnostic != nullptr && definition.author() != QLatin1String(kVEditAuthor)) {
        // Informational only: A3 accepts any valid definition from the
        // repository. On a developer host this is normally a KDE definition
        // (the delivered runtime narrows the search path to VEdit's own).
        *diagnostic = QStringLiteral("'%1' resolves to author '%2' (not VEdit); accepted as a "
                                     "repository fallback — the delivered runtime narrows the "
                                     "definition search path to VEdit's own")
                          .arg(mode, definition.author());
    }
    if (!document->setHighlightingMode(mode)) {
        if (diagnostic != nullptr) {
            *diagnostic = QCoreApplication::translate("VEdit", "KTextEditor::Document::setHighlightingMode('%1') failed")
                              .arg(mode);
        }
        return false;
    }
    if (document->highlightingMode() != mode) {
        if (diagnostic != nullptr) {
            *diagnostic = QCoreApplication::translate("VEdit", "the document reports highlighting mode '%1' instead of '%2'")
                              .arg(document->highlightingMode(), mode);
        }
        return false;
    }
    return true;
}

HighlightStats measureHighlighting(const KSyntaxHighlighting::Definition &definition,
                                   const QString &text)
{
    if (!definition.isValid()) {
        return HighlightStats{};
    }
    RangeCounter counter;
    return counter.run(definition, text);
}

} // namespace vedit::editor
