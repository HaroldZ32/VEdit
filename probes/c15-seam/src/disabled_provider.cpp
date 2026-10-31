// Fallback provider: the seam with every capability switched off.
//
// Compiled instead of tree_sitter_provider.cpp when the build is configured
// with -DSEAM_TREESITTER=OFF, and still reachable in a tree-sitter build
// through the runtime switch (--tree-sitter=off). Callers are identical.
#include "syntax_provider.hpp"

namespace seam {

namespace {

class DisabledProvider final : public SyntaxProvider {
 public:
  std::string name() const override { return "disabled"; }
  std::string version() const override { return "n/a"; }
  std::string license() const override { return "n/a"; }
  Capabilities capabilities() const override { return Capabilities{}; }

  ParseTreePtr parse(std::string_view) const override { return nullptr; }
  std::vector<std::string> queryIds() const override { return {}; }
  std::size_t queryMatchCount(const ParseTreePtr &, std::string_view) const override { return 0; }
  std::vector<HighlightSpan> highlightSpans(const ParseTreePtr &) const override { return {}; }
  std::vector<FoldRegion> foldRegions(const ParseTreePtr &) const override { return {}; }
  std::vector<StructureNode> structure(const ParseTreePtr &) const override { return {}; }
};

}  // namespace

std::unique_ptr<SyntaxProvider> makeDisabledProvider() {
  return std::make_unique<DisabledProvider>();
}

}  // namespace seam
