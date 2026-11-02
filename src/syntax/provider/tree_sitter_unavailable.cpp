// Fallback factory compiled when -DVEDIT_SYNTAX_TREESITTER=OFF: the tree-sitter
// backend does not exist in this binary, so resolving a provider always fails
// cleanly and the caller uses the KSyntaxHighlighting base highlighter.
#include "syntax_provider.h"
#include "tree_sitter_provider.h"

namespace vedit::syntax {

std::unique_ptr<SyntaxProvider> makeTreeSitterProvider(const GrammarBinding &)
{
    return nullptr;
}

} // namespace vedit::syntax
