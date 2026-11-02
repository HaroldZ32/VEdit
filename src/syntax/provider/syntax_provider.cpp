// Default implementation of the optional range-limited highlight query.
//
// The syntax seam stays source-compatible for providers that only know how to
// enumerate the whole tree: the default filters the full result. Callers get the
// same contract either way -- every span that intersects the requested byte
// range, ordered by start byte.
#include "syntax_provider.h"

#include <algorithm>

namespace vedit::syntax {

std::vector<HighlightSpan>
SyntaxProvider::highlightSpans(const ParseTreePtr &tree, const Range &range)
{
    std::vector<HighlightSpan> spans = highlightSpans(tree);
    spans.erase(std::remove_if(spans.begin(), spans.end(),
                               [&range](const HighlightSpan &span) {
                                   return span.range.end_byte <= range.start_byte
                                       || range.end_byte <= span.range.start_byte;
                               }),
                spans.end());
    return spans;
}

} // namespace vedit::syntax
