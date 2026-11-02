#include "syntax_fold_source.h"

#include "bindings.h"
#include "syntax_service.h"

#include <QByteArray>

#include <string_view>

namespace vedit::folding {

SyntaxFoldSource::SyntaxFoldSource(QString grammarId)
    : grammarId_(std::move(grammarId))
{
}

QString SyntaxFoldSource::name() const
{
    return QStringLiteral("tree-sitter:%1").arg(grammarId_);
}

std::vector<FoldRange> SyntaxFoldSource::ranges(QStringView text)
{
    std::vector<FoldRange> ranges;
    vedit::syntax::SyntaxService &service = vedit::syntax::SyntaxService::instance();
    vedit::syntax::grammars::registerBuiltinGrammars();

    if (!service.treeSitterEnabled()) {
        treeSitterActive_ = false;
        lastRangeCount_ = 0;
        lastDiagnostic_ = QStringLiteral("tree-sitter switch is off");
        return ranges;
    }
    if (service.activeGrammarId() != grammarId_.toStdString()
        && !service.activate(grammarId_.toStdString())) {
        treeSitterActive_ = false;
        lastRangeCount_ = 0;
        lastDiagnostic_ = QStringLiteral("grammar '%1' is not registered").arg(grammarId_);
        return ranges;
    }
    std::unique_ptr<vedit::syntax::SyntaxProvider> provider = service.createProvider();
    if (provider == nullptr) {
        treeSitterActive_ = false;
        lastRangeCount_ = 0;
        lastDiagnostic_ = QStringLiteral("no provider for grammar '%1'").arg(grammarId_);
        return ranges;
    }
    const QByteArray utf8 = text.toString().toUtf8();
    const std::string_view view(utf8.constData(), static_cast<std::size_t>(utf8.size()));
    const vedit::syntax::ParseTreePtr tree = provider->parse(view);
    if (tree == nullptr) {
        treeSitterActive_ = false;
        lastRangeCount_ = 0;
        lastDiagnostic_ = QStringLiteral("parse returned no tree");
        return ranges;
    }
    for (const vedit::syntax::FoldRange &range : provider->foldRanges(tree)) {
        ranges.push_back(FoldRange{static_cast<int>(range.start_line),
                                   static_cast<int>(range.end_line)});
    }
    treeSitterActive_ = true;
    lastRangeCount_ = static_cast<int>(ranges.size());
    lastDiagnostic_ = QStringLiteral("provider '%1' parsed %2 fold ranges")
                          .arg(QString::fromStdString(provider->id()))
                          .arg(lastRangeCount_);
    return ranges;
}

bool SyntaxFoldSource::treeSitterActive() const
{
    return treeSitterActive_;
}

QString SyntaxFoldSource::lastDiagnostic() const
{
    return lastDiagnostic_;
}

} // namespace vedit::folding
