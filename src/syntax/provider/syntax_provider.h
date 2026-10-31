#pragma once

// VEdit SyntaxProvider — the production language-agnostic syntax seam.
//
// This header is the contract between the editor (src/editor), the overlay
// colouring layer (src/syn-overlay), the folding view and every concrete
// grammar (src/syn-grammars). It must stay free of any concrete grammar type:
// no tree-sitter type, no node kind, no grammar name appears here. Grammars are
// described by an opaque function pointer in grammar_binding.h and are never
// referenced by the interface or by its callers.
//
// Capabilities exposed (contract C15):
//   parse               -> parse()
//   query ranges        -> queryIds() / queryRanges()
//   highlight spans     -> highlightSpans()
//   fold ranges         -> foldRanges()
//   structure breadcrumbs-> breadcrumbs()
//   incremental edit sync-> applyEdit()
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vedit::syntax {

// Opaque parse result; defined only in the implementation translation unit.
struct ParseTree;
using ParseTreePtr = std::shared_ptr<ParseTree>;

struct Range {
    std::uint32_t start_byte = 0;
    std::uint32_t end_byte = 0;
    std::uint32_t start_line = 0; // 0-based
    std::uint32_t end_line = 0;   // 0-based
};

struct QueryRange {
    Range range;
    std::string query_id; // logical query id ("highlights", "folds", "structure", ...)
    std::string capture;  // logical capture name
};

struct HighlightSpan {
    Range range;
    std::string capture;
};

struct FoldRange {
    std::uint32_t start_line = 0; // 0-based, inclusive
    std::uint32_t end_line = 0;   // 0-based, inclusive
};

struct Breadcrumb {
    std::string kind;  // logical capture name of the containing structure node
    std::string name;  // text of the matching "name" capture, if the query has one
    std::uint32_t line = 0;
    std::uint32_t start_byte = 0;
    std::uint32_t end_byte = 0;
};

// One incremental edit. The provider keeps the document text, applies the edit
// to its text and to its parse tree, and reports whether the tree stayed in
// sync. Callers that hold their own text must apply the same edit there.
struct Edit {
    std::uint32_t start_byte = 0;
    std::uint32_t old_end_byte = 0;
    std::uint32_t start_line = 0;
    std::uint32_t start_column = 0;
    std::uint32_t old_end_line = 0;
    std::uint32_t old_end_column = 0;
    std::string new_text;
};

struct Capabilities {
    bool parse = false;
    bool query_ranges = false;
    bool highlight_spans = false;
    bool fold_ranges = false;
    bool breadcrumbs = false;
    bool incremental_edit_sync = false;
};

class SyntaxProvider {
public:
    virtual ~SyntaxProvider() = default;

    virtual std::string id() const = 0;
    virtual std::string displayName() const = 0;
    virtual std::string version() const = 0;
    virtual std::string license() const = 0;
    virtual Capabilities capabilities() const = 0;

    // Full parse of `text`; replaces the previous tree. Never throws: a
    // grammar/runtime problem yields a null tree and hasSyntaxErrors()/the
    // capability flags tell the caller to fall back to the base highlighter.
    virtual ParseTreePtr parse(std::string_view text) = 0;
    virtual ParseTreePtr tree() const = 0;
    virtual bool hasSyntaxErrors(const ParseTreePtr &tree) const = 0;

    virtual std::vector<std::string> queryIds() const = 0;
    virtual std::vector<QueryRange> queryRanges(const ParseTreePtr &tree,
                                                std::string_view query_id) = 0;
    virtual std::vector<HighlightSpan> highlightSpans(const ParseTreePtr &tree) = 0;
    virtual std::vector<FoldRange> foldRanges(const ParseTreePtr &tree) = 0;

    // Structure path (outer -> inner) for the node containing `byte_offset`.
    virtual std::vector<Breadcrumb> breadcrumbs(const ParseTreePtr &tree,
                                                std::uint32_t byte_offset) = 0;

    // Incremental edit sync. Returns false if this provider cannot stay in sync
    // (the caller must then re-parse); it never throws.
    virtual bool applyEdit(const Edit &edit) = 0;

    // The document text currently held by the provider (what applyEdit edits).
    virtual std::string_view text() const = 0;
};

// Always-available no-op provider: every capability reports empty/false.
std::unique_ptr<SyntaxProvider> makeDisabledProvider();

} // namespace vedit::syntax
