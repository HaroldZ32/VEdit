#include "realtime_module_references.h"

#include <QByteArray>

#include <tree_sitter/api.h>

#include <memory>

extern "C" const TSLanguage *tree_sitter_verilog(void);

namespace vedit::workspace::diagnostics {

LiteralModuleReferences literalModuleReferences(const QString &text)
{
    // The grammar can classify module instances as checkers or interfaces.
    // The caller matches the types against module definitions in the catalog.
    // Requiring an instance name excludes function calls parsed as checkers.
    static constexpr char patterns[] = R"(
      (module_instantiation [(simple_identifier) (escaped_identifier)] @type
        (hierarchical_instance (name_of_instance) @site))
      (checker_instantiation (checker_identifier) @type (name_of_instance) @site)
      (interface_instantiation (interface_identifier) @type
        (hierarchical_instance (name_of_instance) @site))
    )";
    const auto *language = tree_sitter_verilog();
    static const auto query = [] {
        uint32_t errorOffset = 0;
        TSQueryError error = TSQueryErrorNone;
        return std::unique_ptr<TSQuery, decltype(&ts_query_delete)>(
            ts_query_new(tree_sitter_verilog(), patterns, sizeof(patterns) - 1, &errorOffset, &error),
            ts_query_delete);
    }();
    const std::unique_ptr<TSParser, decltype(&ts_parser_delete)> parser(ts_parser_new(), ts_parser_delete);
    if (!query || !ts_parser_set_language(parser.get(), language)) {
        return {};
    }
    const QByteArray bytes = text.toUtf8();
    const std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree(
        ts_parser_parse_string(parser.get(), nullptr, bytes.constData(), static_cast<uint32_t>(bytes.size())),
        ts_tree_delete);
    if (!tree) {
        return {};
    }
    const std::unique_ptr<TSQueryCursor, decltype(&ts_query_cursor_delete)> cursor(
        ts_query_cursor_new(), ts_query_cursor_delete);
    ts_query_cursor_exec(cursor.get(), query.get(), ts_tree_root_node(tree.get()));
    LiteralModuleReferences references;
    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor.get(), &match)) {
        if (match.capture_count != 2) {
            continue;
        }
        const TSNode type = match.captures[0].node;
        const TSNode site = match.captures[1].node;
        const auto start = ts_node_start_byte(type);
        const auto end = ts_node_end_byte(type);
        QString name = QString::fromUtf8(bytes.constData() + start, end - start);
        if (name.startsWith(QLatin1Char('\\'))) {
            name = name.sliced(1);
        }
        const TSPoint point = ts_node_start_point(site);
        const auto lineStart = ts_node_start_byte(site) - point.column;
        const auto column = QString::fromUtf8(bytes.constData() + lineStart, point.column).size();
        if (!name.isEmpty()) {
            references[name].insert({static_cast<int>(point.row), static_cast<int>(column)});
        }
    }
    return references;
}

} // namespace vedit::workspace::diagnostics
