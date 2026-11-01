#pragma once

// Fold ranges from the syntax stack's own provider (task T19).
//
// The tree-sitter backend already answers "which whole-line regions are
// foldable" through `vedit::syntax::SyntaxProvider::foldRanges()`, whose query
// is resources/queries/<grammar>/folds.scm (embedded by the grammar lane). This
// source adapts that answer to the injector's FoldRange, and it is the A1f
// "tree-sitter ON" side: while `SyntaxService::treeSitterEnabled()` is false (or
// no provider can be created) it produces no ranges at all, which is exactly the
// observable change the A1f on/off evidence records.
//
// The expected fold set in the acceptance test is NEVER taken from here: it is
// computed independently from the sample text (AMENDMENTS A3.2).
#include "fold_source.h"

#include <QString>

#include <string>

namespace vedit::folding {

class SyntaxFoldSource final : public FoldSource {
public:
    // `grammar_id` names a registered binding ("verilog" / "systemverilog").
    explicit SyntaxFoldSource(QString grammarId = QStringLiteral("verilog"));

    QString name() const override;
    std::vector<FoldRange> ranges(QStringView text) override;
    bool treeSitterActive() const override;

    // Why the last ranges() call produced nothing / how many ranges it parsed.
    QString lastDiagnostic() const;

private:
    QString grammarId_;
    QString lastDiagnostic_;
    int lastRangeCount_ = 0;
    bool treeSitterActive_ = false;
};

} // namespace vedit::folding
