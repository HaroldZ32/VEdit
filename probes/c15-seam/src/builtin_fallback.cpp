// Builtin provider for a build configured with -DSEAM_TREESITTER=OFF: the seam
// still offers a provider object, it simply has no capabilities. Compiled only
// in that configuration, so the tree-sitter build never sees this file.
#include "syntax_provider.hpp"

namespace seam {

std::unique_ptr<SyntaxProvider> makeBuiltinProvider() {
  return makeDisabledProvider();
}

}  // namespace seam
