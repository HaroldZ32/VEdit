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
int main()
{
    const char *source =
        "module sub #(parameter int W = 8, parameter int D = 1)\n"
        "  (input logic clk, output logic q);\n"
        "endmodule\n"
        "\n"
        "module top;\n"
        "  logic x;\n"
        "  function void foo(int a, int b);\n"
        "  endfunction\n"
        "  function void bar(int p, int q);\n"
        "  endfunction\n"
        "  sub #(8, 1) u_sub (.clk(clk), .q(q));\n"
        "  initial begin\n"
        "    x = 0;\n"
        "    x = foo(1, 2);\n"
        "    x = foo(1, bar(2, 3), 4);\n"
        "    x = foo(\"a,b\", 2);\n"
        "    $display(\"x\", 1);\n"
        "  end\n"
        "endmodule\n";
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_verilog());
    TSTree *tree = ts_parser_parse_string(parser, nullptr, source, (uint32_t)std::strlen(source));
    dump(ts_tree_root_node(tree), 0);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 0;
}
