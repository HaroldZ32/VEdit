#include "syntax_provider.h"

namespace vedit::syntax {

namespace {

class DisabledSyntaxProvider final : public SyntaxProvider {
public:
    std::string id() const override { return "disabled"; }
    std::string displayName() const override { return "disabled (no syntax provider)"; }
    std::string version() const override { return "n/a"; }
    std::string license() const override { return "n/a"; }
    Capabilities capabilities() const override { return Capabilities{}; }
    ParseTreePtr parse(std::string_view) override { return nullptr; }
    ParseTreePtr tree() const override { return nullptr; }
    bool hasSyntaxErrors(const ParseTreePtr &) const override { return true; }
    std::vector<std::string> queryIds() const override { return {}; }
    std::vector<QueryRange> queryRanges(const ParseTreePtr &, std::string_view) override { return {}; }
    std::vector<HighlightSpan> highlightSpans(const ParseTreePtr &) override { return {}; }
    std::vector<FoldRange> foldRanges(const ParseTreePtr &) override { return {}; }
    std::vector<Breadcrumb> breadcrumbs(const ParseTreePtr &, std::uint32_t) override { return {}; }
    bool applyEdit(const Edit &) override { return false; }
    std::string_view text() const override { return {}; }
};

} // namespace

std::unique_ptr<SyntaxProvider> makeDisabledProvider()
{
    return std::make_unique<DisabledSyntaxProvider>();
}

} // namespace vedit::syntax
