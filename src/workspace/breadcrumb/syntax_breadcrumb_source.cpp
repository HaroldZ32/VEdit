#include "syntax_breadcrumb_source.h"

#include "bindings.h"
#include "syntax_service.h"
#include "syntax_provider.h"

#include <QByteArray>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace vedit::breadcrumb {

SyntaxBreadcrumbSource::SyntaxBreadcrumbSource(QString grammarId)
    : grammarId_(std::move(grammarId))
{
    vedit::syntax::grammars::registerBuiltinGrammars();
    vedit::syntax::SyntaxService &service = vedit::syntax::SyntaxService::instance();
    if (service.activate(grammarId_.toStdString())) {
        provider_ = service.createProvider();
    }
    if (provider_ == nullptr) {
        lastDiagnostic_ = QStringLiteral("no provider for grammar '%1'").arg(grammarId_);
    }
}

SyntaxBreadcrumbSource::~SyntaxBreadcrumbSource() = default;

void SyntaxBreadcrumbSource::ensureParsed(QStringView text)
{
    const QString current = text.toString();
    if (tree_ != nullptr && current == cachedText_) {
        return;
    }
    cachedText_ = current;
    lineStarts_.clear();
    int offset = 0;
    lineStarts_.push_back(0);
    for (const QChar ch : current) {
        if (ch == QLatin1Char('\n')) {
            lineStarts_.push_back(offset + 1);
        }
        ++offset;
    }
    if (provider_ == nullptr) {
        tree_.reset();
        lastDiagnostic_ = QStringLiteral("no provider for grammar '%1'").arg(grammarId_);
        return;
    }
    const QByteArray utf8 = current.toUtf8();
    tree_ = provider_->parse(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
    ++parseCount_;
    if (tree_ == nullptr) {
        lastDiagnostic_ = QStringLiteral("parse returned no tree");
    }
}

std::vector<Entry> SyntaxBreadcrumbSource::pathAt(QStringView text, int line)
{
    std::vector<Entry> entries;
    ensureParsed(text);
    if (tree_ == nullptr || line < 0 || line >= static_cast<int>(lineStarts_.size())) {
        return entries;
    }
    ++queryCount_;
    for (const vedit::syntax::Breadcrumb &crumb :
         provider_->breadcrumbs(tree_, static_cast<std::uint32_t>(lineStarts_[line]))) {
        entries.push_back(Entry{crumb.kind, crumb.name, static_cast<int>(crumb.line)});
    }
    return entries;
}

bool SyntaxBreadcrumbSource::semanticActive() const
{
    // "The parser can contribute structure": a provider for the requested
    // grammar plus the runtime tree-sitter switch. A parse is only performed when
    // a path is actually asked for (pathAt), so this stays cheap before the first
    // cursor position is known.
    return provider_ != nullptr && vedit::syntax::SyntaxService::instance().treeSitterEnabled();
}

quint64 SyntaxBreadcrumbSource::parseCount() const
{
    return parseCount_;
}

quint64 SyntaxBreadcrumbSource::queryCount() const
{
    return queryCount_;
}

QString SyntaxBreadcrumbSource::lastDiagnostic() const
{
    return lastDiagnostic_;
}

} // namespace vedit::breadcrumb
