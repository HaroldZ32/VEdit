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

// VEdit's own extension table; mirrors the `extensions=` attribute of
// resources/syntax/vedit-verilog.xml and vedit-systemverilog.xml.
QString modeFromExtension(const QString &absolutePath)
{
    const QString suffix = QFileInfo(absolutePath).suffix().toLower();
    if (suffix == QLatin1String("v") || suffix == QLatin1String("vh") ||
        suffix == QLatin1String("vlg")) {
        return QStringLiteral("Verilog");
    }
    if (suffix == QLatin1String("sv") || suffix == QLatin1String("svh")) {
        return QStringLiteral("SystemVerilog");
    }
    return {};
}

} // namespace

QString highlightModeForFile(const QString &absolutePath)
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor != nullptr) {
        // What the editor's own repository maps this file name to. In the
        // delivered runtime (AppRun narrows the definition search path) this is
        // VEdit's definition; on a developer host a KDE definition (e.g. "V"
        // for *.v) may win, and then the extension table below is used instead.
        const KSyntaxHighlighting::Definition byFileName =
            editor->repository().definitionForFileName(QFileInfo(absolutePath).fileName());
        if (byFileName.isValid()
            && byFileName.author() == QLatin1String(kVEditAuthor)) {
            return byFileName.name();
        }
    }
    return modeFromExtension(absolutePath);
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
            *diagnostic = QStringLiteral("no document to highlight");
        }
        return false;
    }
    const QString mode = highlightModeForFile(absolutePath);
    if (mode.isEmpty()) {
        if (diagnostic != nullptr) {
            *diagnostic = QStringLiteral("no syntax definition for %1")
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
        *diagnostic = QStringLiteral("'%1' resolves to author '%2' (not VEdit); the delivered "
                                     "runtime narrows the definition search path to VEdit's own")
                          .arg(mode, definition.author());
    }
    if (!document->setHighlightingMode(mode)) {
        if (diagnostic != nullptr) {
            *diagnostic = QStringLiteral("KTextEditor::Document::setHighlightingMode('%1') failed")
                              .arg(mode);
        }
        return false;
    }
    if (document->highlightingMode() != mode) {
        if (diagnostic != nullptr) {
            *diagnostic = QStringLiteral("the document reports highlighting mode '%1' instead of '%2'")
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
