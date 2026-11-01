#pragma once

// Tree-sitter backed implementation of the SyntaxProvider seam.
//
// This translation unit is the ONLY place that includes <tree_sitter/api.h>.
// It is generic over GrammarBinding: no concrete grammar name/kinds appear here.
#include "grammar_binding.h"
#include "syntax_provider.h"

#include <memory>

namespace vedit::syntax {

class TreeSitterSyntaxProvider final : public SyntaxProvider {
public:
    explicit TreeSitterSyntaxProvider(const GrammarBinding &binding);
    ~TreeSitterSyntaxProvider() override;

    TreeSitterSyntaxProvider(const TreeSitterSyntaxProvider &) = delete;
    TreeSitterSyntaxProvider &operator=(const TreeSitterSyntaxProvider &) = delete;

    bool isValid() const;
    std::string lastError() const;

    std::string id() const override;
    std::string displayName() const override;
    std::string version() const override;
    std::string license() const override;
    Capabilities capabilities() const override;

    ParseTreePtr parse(std::string_view text) override;
    ParseTreePtr tree() const override;
    bool hasSyntaxErrors(const ParseTreePtr &tree) const override;
    std::vector<std::string> queryIds() const override;
    std::vector<QueryRange> queryRanges(const ParseTreePtr &tree, std::string_view query_id) override;
    std::vector<HighlightSpan> highlightSpans(const ParseTreePtr &tree) override;
    // T92: the overlay only owns a visible window, so the per-edit rebuild must
    // not pay for a whole-tree query. This override restricts tree-sitter's
    // query cursor to the byte range (widened by a line on each side, because a
    // highlight node can start just outside the window and still overlap it).
    std::vector<HighlightSpan> highlightSpans(const ParseTreePtr &tree,
                                              const Range &range) override;
    std::vector<FoldRange> foldRanges(const ParseTreePtr &tree) override;
    std::vector<Breadcrumb> breadcrumbs(const ParseTreePtr &tree, std::uint32_t byte_offset) override;
    bool applyEdit(const Edit &edit) override;
    std::string_view text() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// nullptr when the binding is invalid (missing language/entry point) or the
// tree-sitter runtime rejects the grammar ABI; never throws.
std::unique_ptr<SyntaxProvider> makeTreeSitterProvider(const GrammarBinding &binding);

} // namespace vedit::syntax
