#include <tree_sitter/api.h>
#include <cstdio>
#include <cstring>
extern "C" const TSLanguage *tree_sitter_verilog(void);
static void dump(TSNode node, int depth)
{
    const TSPoint s = ts_node_start_point(node);
    const TSPoint e = ts_node_end_point(node);
    std::printf("%*s%s%s [%u:%u-%u:%u]\n", depth * 2, "", ts_node_type(node),
                ts_node_is_named(node) ? "" : " (anon)", s.row, s.column, e.row, e.column);
    const uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; ++i) dump(ts_node_child(node, i), depth + 1);
}
static void probe(const char *label, const char *source)
{
    std::printf("\n===== %s\n%s\n", label, source);
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_verilog());
    TSTree *tree = ts_parser_parse_string(parser, nullptr, source, (uint32_t)std::strlen(source));
    dump(ts_tree_root_node(tree), 0);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
}
int main()
{
    probe("after a statement", "module top;\n  initial begin\n    x = 0;\n    foo(1, 2, 3);\n  end\nendmodule\n");
    probe("in expression", "module top;\n  initial x = foo(1, 2, 3);\nendmodule\n");
    probe("single stmt initial", "module top;\n  initial foo(1);\nendmodule\n");
    probe("always_comb", "module top;\n  always_comb begin\n    foo(a);\n  end\nendmodule\n");
    return 0;
}
