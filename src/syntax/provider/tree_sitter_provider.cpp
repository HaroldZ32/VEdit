#include "tree_sitter_provider.h"

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace vedit::syntax {

// Opaque handle promised by syntax_provider.h.
struct ParseTree {
    TSTree *tree = nullptr;
    ~ParseTree()
    {
        if (tree != nullptr) {
            ts_tree_delete(tree);
        }
    }
};

namespace {

const TSLanguage *languageOf(const GrammarBinding &binding)
{
    if (binding.language == nullptr) {
        return nullptr;
    }
    auto *fn = reinterpret_cast<const TSLanguage *(*)(void)>(binding.language);
    return fn();
}

std::uint32_t lineOf(TSNode node)
{
    return ts_node_start_point(node).row;
}

std::uint32_t lastLineOf(TSNode node)
{
    const TSPoint start = ts_node_start_point(node);
    const TSPoint end = ts_node_end_point(node);
    if (end.column == 0 && end.row > start.row) {
        return end.row - 1;
    }
    return end.row;
}

std::string nodeText(const std::string &source, TSNode node)
{
    const std::uint32_t start = ts_node_start_byte(node);
    const std::uint32_t end = ts_node_end_byte(node);
    if (end <= start || end > source.size()) {
        return {};
    }
    return source.substr(start, end - start);
}

std::string_view captureName(const TSQuery *query, std::uint32_t index, std::uint32_t *length)
{
    const char *name = ts_query_capture_name_for_id(query, index, length);
    return name != nullptr ? std::string_view(name, *length) : std::string_view{};
}

} // namespace

struct MatchCapture {
    TSNode node{};
    std::string name;
};
using Match = std::vector<MatchCapture>;

struct TreeSitterSyntaxProvider::Impl {
    GrammarBinding binding;
    const TSLanguage *language = nullptr;
    TSParser *parser = nullptr;
    ParseTreePtr current;
    std::string source;
    std::map<std::string, TSQuery *> queries;
    bool valid = false;
    std::string error;

    ~Impl()
    {
        for (auto &entry : queries) {
            ts_query_delete(entry.second);
        }
        if (parser != nullptr) {
            ts_parser_delete(parser);
        }
    }

    TSQuery *queryFor(std::string_view id)
    {
        const std::string key(id);
        const auto found = queries.find(key);
        if (found != queries.end()) {
            return found->second;
        }
        for (const NamedQuery &query : binding.queries) {
            if (query.id != key) {
                continue;
            }
            std::uint32_t error_offset = 0;
            TSQueryError error_type = TSQueryErrorNone;
            TSQuery *compiled = ts_query_new(language, query.scm.c_str(),
                                             static_cast<std::uint32_t>(query.scm.size()),
                                             &error_offset, &error_type);
            if (compiled == nullptr) {
                error = "query '" + key + "' failed at offset " + std::to_string(error_offset) +
                        " (error " + std::to_string(static_cast<int>(error_type)) + ")";
                queries.emplace(key, nullptr);
                return nullptr;
            }
            queries.emplace(key, compiled);
            return compiled;
        }
        return nullptr;
    }

    std::vector<Match> run(const TSQuery *query, const ParseTreePtr &handle)
    {
        std::vector<Match> matches;
        if (query == nullptr || !handle || handle->tree == nullptr) {
            return matches;
        }
        TSQueryCursor *cursor = ts_query_cursor_new();
        if (cursor == nullptr) {
            return matches;
        }
        ts_query_cursor_exec(cursor, query, ts_tree_root_node(handle->tree));
        TSQueryMatch raw{};
        while (ts_query_cursor_next_match(cursor, &raw)) {
            Match match;
            match.reserve(raw.capture_count);
            for (std::uint32_t i = 0; i < raw.capture_count; ++i) {
                std::uint32_t length = 0;
                const std::string_view name = captureName(query, raw.captures[i].index, &length);
                match.push_back(MatchCapture{raw.captures[i].node, std::string(name)});
            }
            matches.push_back(std::move(match));
        }
        ts_query_cursor_delete(cursor);
        return matches;
    }
};

TreeSitterSyntaxProvider::TreeSitterSyntaxProvider(const GrammarBinding &binding)
    : impl_(std::make_unique<Impl>())
{
    impl_->binding = binding;
    if (!binding.isValid()) {
        impl_->error = "invalid grammar binding (missing language entry point)";
        return;
    }
    impl_->language = languageOf(binding);
    if (impl_->language == nullptr) {
        impl_->error = "grammar entry point returned a null language";
        return;
    }
    impl_->parser = ts_parser_new();
    if (impl_->parser == nullptr) {
        impl_->error = "ts_parser_new() returned null";
        return;
    }
    if (!ts_parser_set_language(impl_->parser, impl_->language)) {
        impl_->error = "tree-sitter rejected the grammar (ABI mismatch, grammar ABI " +
                       std::to_string(ts_language_version(impl_->language)) + ")";
        return;
    }
    impl_->valid = true;
}

TreeSitterSyntaxProvider::~TreeSitterSyntaxProvider() = default;

bool TreeSitterSyntaxProvider::isValid() const
{
    return impl_ != nullptr && impl_->valid;
}

std::string TreeSitterSyntaxProvider::lastError() const
{
    return impl_ != nullptr ? impl_->error : std::string("no implementation");
}

std::string TreeSitterSyntaxProvider::id() const
{
    return impl_->binding.id;
}

std::string TreeSitterSyntaxProvider::displayName() const
{
    return impl_->binding.display_name.empty() ? impl_->binding.id : impl_->binding.display_name;
}

std::string TreeSitterSyntaxProvider::version() const
{
    return impl_->binding.version;
}

std::string TreeSitterSyntaxProvider::license() const
{
    return impl_->binding.license;
}

Capabilities TreeSitterSyntaxProvider::capabilities() const
{
    if (!impl_->valid) {
        return Capabilities{};
    }
    return Capabilities{true, true, true, true, true, true};
}

ParseTreePtr TreeSitterSyntaxProvider::parse(std::string_view text)
{
    if (!impl_->valid) {
        return nullptr;
    }
    auto handle = std::make_shared<ParseTree>();
    handle->tree = ts_parser_parse_string(impl_->parser, nullptr, text.data(),
                                          static_cast<std::uint32_t>(text.size()));
    if (handle->tree == nullptr) {
        impl_->error = "ts_parser_parse_string() returned null";
        return nullptr;
    }
    impl_->source.assign(text);
    impl_->current = handle;
    return handle;
}

ParseTreePtr TreeSitterSyntaxProvider::tree() const
{
    return impl_->current;
}

bool TreeSitterSyntaxProvider::hasSyntaxErrors(const ParseTreePtr &tree) const
{
    if (!tree || tree->tree == nullptr) {
        return true;
    }
    return ts_node_has_error(ts_tree_root_node(tree->tree));
}

std::vector<std::string> TreeSitterSyntaxProvider::queryIds() const
{
    std::vector<std::string> ids;
    ids.reserve(impl_->binding.queries.size());
    for (const NamedQuery &query : impl_->binding.queries) {
        ids.push_back(query.id);
    }
    return ids;
}

std::vector<QueryRange> TreeSitterSyntaxProvider::queryRanges(const ParseTreePtr &tree,
                                                             std::string_view query_id)
{
    std::vector<QueryRange> ranges;
    const TSQuery *query = impl_->queryFor(query_id);
    for (const auto &match : impl_->run(query, tree)) {
        for (const auto &capture : match) {
            const TSPoint start = ts_node_start_point(capture.node);
            const TSPoint end = ts_node_end_point(capture.node);
            ranges.push_back(QueryRange{
                Range{ts_node_start_byte(capture.node), ts_node_end_byte(capture.node), start.row,
                      end.row},
                std::string(query_id), capture.name});
        }
    }
    std::sort(ranges.begin(), ranges.end(), [](const QueryRange &a, const QueryRange &b) {
        if (a.range.start_byte != b.range.start_byte) {
            return a.range.start_byte < b.range.start_byte;
        }
        return a.capture < b.capture;
    });
    return ranges;
}

std::vector<HighlightSpan> TreeSitterSyntaxProvider::highlightSpans(const ParseTreePtr &tree)
{
    std::vector<HighlightSpan> spans;
    const TSQuery *query = impl_->queryFor("highlights");
    for (const auto &match : impl_->run(query, tree)) {
        for (const auto &capture : match) {
            const TSPoint start = ts_node_start_point(capture.node);
            const TSPoint end = ts_node_end_point(capture.node);
            spans.push_back(HighlightSpan{
                Range{ts_node_start_byte(capture.node), ts_node_end_byte(capture.node), start.row,
                      end.row},
                capture.name});
        }
    }
    std::sort(spans.begin(), spans.end(), [](const HighlightSpan &a, const HighlightSpan &b) {
        if (a.range.start_byte != b.range.start_byte) {
            return a.range.start_byte < b.range.start_byte;
        }
        return a.capture < b.capture;
    });
    return spans;
}

std::vector<FoldRange> TreeSitterSyntaxProvider::foldRanges(const ParseTreePtr &tree)
{
    std::vector<FoldRange> folds;
    const TSQuery *query = impl_->queryFor("folds");
    for (const auto &match : impl_->run(query, tree)) {
        for (const auto &capture : match) {
            if (capture.name != "fold") {
                continue;
            }
            const std::uint32_t first = lineOf(capture.node);
            const std::uint32_t last = lastLineOf(capture.node);
            if (last > first) {
                folds.push_back(FoldRange{first, last});
            }
        }
    }
    std::sort(folds.begin(), folds.end(), [](const FoldRange &a, const FoldRange &b) {
        if (a.start_line != b.start_line) {
            return a.start_line < b.start_line;
        }
        return a.end_line > b.end_line;
    });
    folds.erase(std::unique(folds.begin(), folds.end(),
                            [](const FoldRange &a, const FoldRange &b) {
                                return a.start_line == b.start_line && a.end_line == b.end_line;
                            }),
                folds.end());
    return folds;
}

std::vector<Breadcrumb> TreeSitterSyntaxProvider::breadcrumbs(const ParseTreePtr &tree,
                                                             std::uint32_t byte_offset)
{
    std::vector<Breadcrumb> path;
    const TSQuery *query = impl_->queryFor("structure");
    for (const auto &match : impl_->run(query, tree)) {
        const MatchCapture *anchor = nullptr;
        const MatchCapture *name = nullptr;
        for (const auto &capture : match) {
            if (capture.name == "name") {
                name = &capture;
            } else if (anchor == nullptr) {
                anchor = &capture;
            }
        }
        if (anchor == nullptr) {
            continue;
        }
        const std::uint32_t start = ts_node_start_byte(anchor->node);
        const std::uint32_t end = ts_node_end_byte(anchor->node);
        if (byte_offset < start || byte_offset >= end) {
            continue;
        }
        Breadcrumb crumb;
        crumb.kind = anchor->name;
        crumb.name = name != nullptr ? nodeText(impl_->source, name->node) : std::string{};
        crumb.line = lineOf(anchor->node);
        crumb.start_byte = start;
        crumb.end_byte = end;
        path.push_back(std::move(crumb));
    }
    std::sort(path.begin(), path.end(), [](const Breadcrumb &a, const Breadcrumb &b) {
        if (a.start_byte != b.start_byte) {
            return a.start_byte < b.start_byte;
        }
        return a.end_byte > b.end_byte;
    });
    path.erase(std::unique(path.begin(), path.end(),
                           [](const Breadcrumb &a, const Breadcrumb &b) {
                               return a.start_byte == b.start_byte && a.end_byte == b.end_byte &&
                                      a.kind == b.kind && a.name == b.name;
                           }),
               path.end());
    return path;
}

bool TreeSitterSyntaxProvider::applyEdit(const Edit &edit)
{
    if (!impl_->valid || !impl_->current || impl_->current->tree == nullptr) {
        return false;
    }
    if (edit.start_byte > edit.old_end_byte || edit.old_end_byte > impl_->source.size()) {
        return false;
    }
    std::uint32_t new_end_line = edit.start_line;
    std::uint32_t new_end_column = edit.start_column;
    for (char character : edit.new_text) {
        if (character == '\n') {
            ++new_end_line;
            new_end_column = 0;
        } else {
            ++new_end_column;
        }
    }
    const std::uint32_t new_end_byte = edit.start_byte + static_cast<std::uint32_t>(edit.new_text.size());
    TSInputEdit treeEdit{};
    treeEdit.start_byte = edit.start_byte;
    treeEdit.old_end_byte = edit.old_end_byte;
    treeEdit.new_end_byte = new_end_byte;
    treeEdit.start_point = TSPoint{edit.start_line, edit.start_column};
    treeEdit.old_end_point = TSPoint{edit.old_end_line, edit.old_end_column};
    treeEdit.new_end_point = TSPoint{new_end_line, new_end_column};
    ts_tree_edit(impl_->current->tree, &treeEdit);
    impl_->source.replace(edit.start_byte, edit.old_end_byte - edit.start_byte, edit.new_text);
    // Incremental re-parse: tree-sitter only records the edit on the old tree;
    // queries see the new structure after parsing against it. A fresh handle is
    // published so callers still holding the previous tree keep a valid pointer.
    TSTree *reparsed = ts_parser_parse_string(impl_->parser, impl_->current->tree,
                                              impl_->source.data(),
                                              static_cast<std::uint32_t>(impl_->source.size()));
    if (reparsed == nullptr) {
        impl_->error = "incremental re-parse failed";
        return false;
    }
    auto handle = std::make_shared<ParseTree>();
    handle->tree = reparsed;
    impl_->current = handle;
    return true;
}

std::string_view TreeSitterSyntaxProvider::text() const
{
    return impl_->source;
}

std::unique_ptr<SyntaxProvider> makeTreeSitterProvider(const GrammarBinding &binding)
{
    auto provider = std::make_unique<TreeSitterSyntaxProvider>(binding);
    if (!provider->isValid()) {
        return nullptr;
    }
    return provider;
}

} // namespace vedit::syntax
