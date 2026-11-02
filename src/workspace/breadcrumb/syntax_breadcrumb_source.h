#pragma once

// Breadcrumb source over the syntax stack's provider (task T19).
//
// `SyntaxProvider::breadcrumbs(tree, byte_offset)` runs the grammar's
// resources/queries/<grammar>/structure.scm and returns the containers of the
// node at that offset, outermost first. This source:
//   * parses ONCE per distinct text (a cursor move reuses the cached tree), and
//   * counts the parses, so the evidence can show that moving the cursor does not
//     reparse and that a semantic update happens exactly once per text change.
#include "breadcrumb_source.h"

#include <QString>
#include <QStringView>

#include <memory>
#include <string>
#include <vector>

namespace vedit::syntax {
class SyntaxProvider;
struct ParseTree;
}

namespace vedit::breadcrumb {

class SyntaxBreadcrumbSource final : public Source {
public:
    explicit SyntaxBreadcrumbSource(QString grammarId = QStringLiteral("verilog"));
    ~SyntaxBreadcrumbSource() override;

    std::vector<Entry> pathAt(QStringView text, int line) override;
    bool semanticActive() const override;

    quint64 parseCount() const;      // full parses performed (text changes)
    quint64 queryCount() const;      // structure queries served from the cached tree
    QString lastDiagnostic() const;

private:
    void ensureParsed(QStringView text);

    QString grammarId_;
    std::unique_ptr<vedit::syntax::SyntaxProvider> provider_;
    std::shared_ptr<vedit::syntax::ParseTree> tree_;
    QString cachedText_;
    std::vector<int> lineStarts_;
    quint64 parseCount_ = 0;
    quint64 queryCount_ = 0;
    QString lastDiagnostic_;
};

} // namespace vedit::breadcrumb
