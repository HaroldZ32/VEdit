#pragma once

// Semantic source of the breadcrumb lane (task T19).
//
// The path at a cursor line comes from the parsed structure (the syntax stack's
// "structure" query through `SyntaxProvider::breadcrumbs()`), never from text
// heuristics and never from the fold registry. A source may cache its parse: a
// cursor move must not reparse, and a reparse happens only when the text changed
// (that is the semantic-update event of AMENDMENTS A2.1 (b), and it must not
// rebuild folds either).
#include "breadcrumb_model.h"

#include <QStringView>

#include <vector>

namespace vedit::breadcrumb {

class Source {
public:
    virtual ~Source() = default;

    // Semantic containers around `line` (0-based), outermost first.
    virtual std::vector<Entry> pathAt(QStringView text, int line) = 0;

    // Whether the parser currently contributes structure (tree-sitter on and a
    // provider available). When false the bar has nothing semantic to show.
    virtual bool semanticActive() const = 0;
};

} // namespace vedit::breadcrumb
