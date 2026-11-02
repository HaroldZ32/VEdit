// Validator for the VEdit grammar lane (syn-grammars, task T17).
//
// Parses the committed Verilog and SystemVerilog samples and checks, on the real
// vendored grammar: parse cleanliness, the three query sets (highlights / folds /
// structure), the fold ranges (whole-line, multi-line only), the structure
// breadcrumbs, the registry/binding identity (licence, pinned upstream commit),
// the compiler-supplied build manifest, and the runtime switch (A1h seam).
//
// Plain CHECK harness: prints one line per check and exits non-zero on any
// failure; registered as the ctest target vedit-syn-grammars-tests.
#include "bindings.h"
#include "grammar_binding.h"
#include "syntax_provider.h"
#include "syntax_service.h"
#include "tree_sitter_provider.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace vedit::syntax;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const std::string &name, bool ok, const std::string &detail = {})
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
                detail.empty() ? "" : "  ", detail.c_str());
    std::fflush(stdout);
}

std::string readFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::string current;
    for (const char character : text) {
        if (character == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

int findLine(const std::vector<std::string> &lines, std::string_view needle, int from = 0)
{
    for (int i = std::max(0, from); i < static_cast<int>(lines.size()); ++i) {
        if (lines[static_cast<std::size_t>(i)].find(needle) != std::string::npos) {
            return i;
        }
    }
    return -1;
}

std::uint32_t byteOfLine(const std::vector<std::string> &lines, int index)
{
    std::uint32_t offset = 0;
    for (int i = 0; i < index; ++i) {
        offset += static_cast<std::uint32_t>(lines[static_cast<std::size_t>(i)].size()) + 1U;
    }
    return offset;
}

int countCapture(const std::vector<HighlightSpan> &spans, std::string_view name)
{
    int count = 0;
    for (const HighlightSpan &span : spans) {
        if (span.capture == name) {
            ++count;
        }
    }
    return count;
}

bool hasFold(const std::vector<FoldRange> &folds, int start_line, int end_line)
{
    if (start_line < 0 || end_line < 0) {
        return false;
    }
    for (const FoldRange &fold : folds) {
        if (fold.start_line == static_cast<std::uint32_t>(start_line) &&
            fold.end_line == static_cast<std::uint32_t>(end_line)) {
            return true;
        }
    }
    return false;
}

bool containsAny(std::string_view line, std::initializer_list<std::string_view> needles)
{
    for (const std::string_view needle : needles) {
        if (line.find(needle) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

std::string textOf(const std::string &document, const QueryRange &range)
{
    if (range.range.start_byte > document.size() || range.range.end_byte > document.size() ||
        range.range.start_byte > range.range.end_byte) {
        return {};
    }
    return document.substr(range.range.start_byte, range.range.end_byte - range.range.start_byte);
}

std::vector<std::string> namesOf(const std::string &document, const std::vector<QueryRange> &ranges)
{
    std::vector<std::string> names;
    for (const QueryRange &range : ranges) {
        if (range.capture == "name") {
            names.push_back(textOf(document, range));
        }
    }
    return names;
}

bool hasName(const std::vector<std::string> &names, std::string_view wanted)
{
    return std::find(names.begin(), names.end(), wanted) != names.end();
}

std::string joinNames(const std::vector<std::string> &names)
{
    std::string joined;
    for (const std::string &name : names) {
        if (!joined.empty()) {
            joined += ",";
        }
        joined += name;
    }
    return joined;
}

// A fold must be multi-line and must start on a construct opener and end on the
// matching closer. This is what the fold view needs (whole-line regions).
bool foldsLookSane(const std::vector<FoldRange> &folds, const std::vector<std::string> &lines)
{
    if (folds.empty()) {
        return false;
    }
    for (const FoldRange &fold : folds) {
        if (fold.end_line <= fold.start_line || fold.end_line >= lines.size()) {
            return false;
        }
        if (!containsAny(lines[fold.start_line],
                         {"module", "interface", "package", "class", "program", "function",
                          "task", "generate", "begin", "case", "fork"})) {
            return false;
        }
        if (!containsAny(lines[fold.end_line],
                         {"endmodule", "endinterface", "endpackage", "endclass", "endprogram",
                          "endfunction", "endtask", "endgenerate", "end", "endcase", "join"})) {
            return false;
        }
    }
    return true;
}

int countBeginFolds(const std::vector<FoldRange> &folds, const std::vector<std::string> &lines)
{
    int count = 0;
    for (const FoldRange &fold : folds) {
        if (fold.start_line < lines.size() &&
            lines[fold.start_line].find("begin") != std::string::npos) {
            ++count;
        }
    }
    return count;
}

void testManifestAndConfig()
{
    const CompileTimeConfig config = compileTimeConfig();
    std::printf("MANIFEST compiled_grammar=%s backend_compiled=%d static_backend=%d\n",
                config.compiled_grammar.c_str(), config.backend_compiled ? 1 : 0,
                config.static_backend ? 1 : 0);
    std::printf("MANIFEST grammar=verilog license=%s version=%s enabled=%d\n",
                generated::kVerilog.license, generated::kVerilog.version,
                generated::kVerilog.enabled ? 1 : 0);
    std::printf("MANIFEST grammar=systemverilog license=%s version=%s enabled=%d\n",
                generated::kSystemVerilog.license, generated::kSystemVerilog.version,
                generated::kSystemVerilog.enabled ? 1 : 0);

    check("config.compiled-grammar-matches-build",
          config.compiled_grammar == std::string(VEDIT_EXPECT_COMPILED_GRAMMAR),
          config.compiled_grammar);
    check("config.tree-sitter-backend-compiled", config.backend_compiled);
    check("manifest.verilog-license-mit", std::string(generated::kVerilog.license) == "MIT");
    check("manifest.verilog-version-pinned",
          std::string(generated::kVerilog.version) == "4457145e795b363f072463e697dfe2f6973c9a52",
          generated::kVerilog.version);
}

void testRegistry()
{
    GrammarRegistry &registry = GrammarRegistry::instance();
    // Static initialisers fill the registry when the binding TU is linked; this
    // explicit call makes the test independent of static-library link order and
    // is what an integrator (product) would call once at startup.
    const std::size_t added = grammars::registerBuiltinGrammars();
    std::printf("INFO registry.register-builtin added=%zu ids=%zu\n", added,
                registry.ids().size());

#if VEDIT_EXPECT_VERILOG_ENABLED
    check("registry.has-verilog", registry.has("verilog"));
    const GrammarBinding verilog_binding = registry.binding("verilog");
    check("binding.verilog.valid", verilog_binding.isValid());
    check("binding.verilog.display-name", !verilog_binding.display_name.empty(),
          verilog_binding.display_name);
    check("binding.verilog.license-mit", verilog_binding.license == "MIT", verilog_binding.license);
    check("binding.verilog.version-pinned",
          verilog_binding.version == "4457145e795b363f072463e697dfe2f6973c9a52",
          verilog_binding.version);
    check("binding.verilog.three-queries", verilog_binding.queries.size() == 3);
    check("binding.verilog.query-ids",
          verilog_binding.queries.size() == 3 && verilog_binding.queries[0].id == "highlights" &&
              verilog_binding.queries[1].id == "folds" && verilog_binding.queries[2].id == "structure");
    check("binding.verilog.query-sources-non-empty",
          verilog_binding.queries.size() == 3 && !verilog_binding.queries[0].scm.empty() &&
              !verilog_binding.queries[1].scm.empty() && !verilog_binding.queries[2].scm.empty());
    check("registry.duplicate-rejected",
          !registry.registerBinding("verilog", [] { return GrammarBinding{}; }));
#endif

#if VEDIT_EXPECT_SYSTEMVERILOG_ENABLED
    check("registry.has-systemverilog", registry.has("systemverilog"));
    const GrammarBinding sv_binding = registry.binding("systemverilog");
    check("binding.systemverilog.valid", sv_binding.isValid());
    check("binding.systemverilog.license-mit", sv_binding.license == "MIT", sv_binding.license);
    check("binding.systemverilog.version-pinned",
          sv_binding.version == "4457145e795b363f072463e697dfe2f6973c9a52", sv_binding.version);
    check("binding.systemverilog.three-queries", sv_binding.queries.size() == 3);
#endif
}

std::unique_ptr<SyntaxProvider> providerFor(const std::string &grammar_id)
{
    SyntaxService &service = SyntaxService::instance();
    if (!service.activate(grammar_id)) {
        return nullptr;
    }
    return service.createProvider();
}

void testVerilogSample()
{
#if VEDIT_EXPECT_VERILOG_ENABLED
    std::unique_ptr<SyntaxProvider> provider = providerFor("verilog");
    check("verilog.provider-created", provider != nullptr);
    if (!provider) {
        return;
    }
    const Capabilities caps = provider->capabilities();
    check("verilog.capabilities.parse", caps.parse);
    check("verilog.capabilities.query-ranges", caps.query_ranges);
    check("verilog.capabilities.highlight-spans", caps.highlight_spans);
    check("verilog.capabilities.fold-ranges", caps.fold_ranges);
    check("verilog.capabilities.breadcrumbs", caps.breadcrumbs);
    check("verilog.query-ids-three", provider->queryIds().size() == 3);

    const std::string document = readFile(std::string(VEDIT_GRAMMAR_SAMPLES_DIR) + "/sample.v");
    check("verilog.sample-loaded", document.size() > 500, std::to_string(document.size()) + " bytes");
    const std::vector<std::string> lines = splitLines(document);
    const ParseTreePtr tree = provider->parse(document);
    check("verilog.parse-clean", tree != nullptr && !provider->hasSyntaxErrors(tree));

    const std::vector<HighlightSpan> spans = provider->highlightSpans(tree);
    const int keyword = countCapture(spans, "keyword");
    const int number = countCapture(spans, "number");
    const int comment = countCapture(spans, "comment");
    const int string = countCapture(spans, "string");
    const int op = countCapture(spans, "operator");
    const int type = countCapture(spans, "type");
    const int builtin = countCapture(spans, "function.builtin");
    std::printf("MEASURE highlights.verilog total=%zu keyword=%d number=%d comment=%d "
                "string=%d operator=%d type=%d function.builtin=%d\n",
                spans.size(), keyword, number, comment, string, op, type, builtin);
    check("verilog.highlight.keyword", keyword >= 30, std::to_string(keyword));
    check("verilog.highlight.number", number >= 10, std::to_string(number));
    check("verilog.highlight.comment", comment >= 1, std::to_string(comment));
    check("verilog.highlight.string", string >= 1, std::to_string(string));
    check("verilog.highlight.operator", op >= 10, std::to_string(op));
    check("verilog.highlight.type", type >= 2, std::to_string(type));
    check("verilog.highlight.system-task", builtin >= 1, std::to_string(builtin));
    check("verilog.highlight.total", spans.size() >= 100, std::to_string(spans.size()));

    const std::vector<QueryRange> queried = provider->queryRanges(tree, "highlights");
    check("verilog.query-ranges-highlights-same-count", queried.size() == spans.size(),
          std::to_string(queried.size()) + " vs " + std::to_string(spans.size()));

    const std::vector<FoldRange> folds = provider->foldRanges(tree);
    std::printf("MEASURE folds.verilog count=%zu begin_blocks=%d\n", folds.size(),
                countBeginFolds(folds, lines));
    check("verilog.folds.min-count", folds.size() >= 10, std::to_string(folds.size()));
    check("verilog.folds.look-sane", foldsLookSane(folds, lines));

    const int module_start = findLine(lines, "module counter");
    const int module_end = findLine(lines, "endmodule", module_start);
    check("verilog.fold.module-counter", hasFold(folds, module_start, module_end),
          std::to_string(module_start) + ".." + std::to_string(module_end));

    const int sub_start = findLine(lines, "module sub_module");
    const int sub_end = findLine(lines, "endmodule", sub_start);
    check("verilog.fold.module-sub", hasFold(folds, sub_start, sub_end));

    const int fn_start = findLine(lines, "function ");
    const int fn_end = findLine(lines, "endfunction", fn_start);
    check("verilog.fold.function", hasFold(folds, fn_start, fn_end));

    const int task_start = findLine(lines, "task pulse");
    const int task_end = findLine(lines, "endtask", task_start);
    check("verilog.fold.task", hasFold(folds, task_start, task_end));

    // "  generate" with the indentation: the file's header comment also ends in
    // the word "generate", so a bare substring search would match line 0.
    const int gen_start = findLine(lines, "  generate");
    const int gen_end = findLine(lines, "endgenerate", gen_start);
    check("verilog.fold.generate", hasFold(folds, gen_start, gen_end),
          std::to_string(gen_start) + ".." + std::to_string(gen_end));

    const int case_start = findLine(lines, "case (state)");
    const int case_end = findLine(lines, "endcase", case_start);
    check("verilog.fold.case", hasFold(folds, case_start, case_end));

    check("verilog.fold.begin-blocks", countBeginFolds(folds, lines) >= 4,
          std::to_string(countBeginFolds(folds, lines)));

    const std::vector<QueryRange> structure = provider->queryRanges(tree, "structure");
    const std::vector<std::string> names = namesOf(document, structure);
    std::printf("INFO structure.verilog names=%s\n", joinNames(names).c_str());
    check("verilog.structure.names",
          hasName(names, "counter") && hasName(names, "mask") && hasName(names, "pulse") &&
              hasName(names, "sub_module"),
          joinNames(names));

    const std::uint32_t function_offset = byteOfLine(lines, findLine(lines, "mask = value"));
    const std::vector<Breadcrumb> path = provider->breadcrumbs(tree, function_offset);
    std::string path_text;
    for (const Breadcrumb &crumb : path) {
        path_text += (path_text.empty() ? "" : " > ") + crumb.kind + ":" + crumb.name;
    }
    std::printf("INFO breadcrumbs.verilog path=%s\n", path_text.c_str());
    check("verilog.breadcrumbs.depth", path.size() >= 2, std::to_string(path.size()));
    check("verilog.breadcrumbs.module",
          !path.empty() && path.front().kind == "module" && path.front().name == "counter",
          path_text);
    check("verilog.breadcrumbs.function",
          path.size() >= 2 && path.back().kind == "function" && path.back().name == "mask",
          path_text);

    // Query compilation errors surface through lastError(); a working binding
    // must have compiled all three queries cleanly.
    const GrammarBinding binding = GrammarRegistry::instance().binding("verilog");
    TreeSitterSyntaxProvider direct(binding);
    const ParseTreePtr direct_tree = direct.parse(document);
    (void)direct.highlightSpans(direct_tree);
    (void)direct.foldRanges(direct_tree);
    (void)direct.breadcrumbs(direct_tree, function_offset);
    check("verilog.queries-compile-clean", direct.lastError().empty(), direct.lastError());
    check("verilog.unknown-query-id-empty",
          direct.queryRanges(direct_tree, "does-not-exist").empty());
#endif
}

void testSystemVerilogSample()
{
#if VEDIT_EXPECT_SYSTEMVERILOG_ENABLED
    std::unique_ptr<SyntaxProvider> provider = providerFor("systemverilog");
    check("systemverilog.provider-created", provider != nullptr);
    if (!provider) {
        return;
    }
    const std::string document = readFile(std::string(VEDIT_GRAMMAR_SAMPLES_DIR) + "/sample.sv");
    check("systemverilog.sample-loaded", document.size() > 500,
          std::to_string(document.size()) + " bytes");
    const std::vector<std::string> lines = splitLines(document);
    const ParseTreePtr tree = provider->parse(document);
    check("systemverilog.parse-clean", tree != nullptr && !provider->hasSyntaxErrors(tree));

    const std::vector<HighlightSpan> spans = provider->highlightSpans(tree);
    const int keyword = countCapture(spans, "keyword");
    const int number = countCapture(spans, "number");
    const int comment = countCapture(spans, "comment");
    const int string = countCapture(spans, "string");
    const int op = countCapture(spans, "operator");
    const int type = countCapture(spans, "type");
    std::printf("MEASURE highlights.systemverilog total=%zu keyword=%d number=%d comment=%d "
                "string=%d operator=%d type=%d\n",
                spans.size(), keyword, number, comment, string, op, type);
    check("systemverilog.highlight.keyword", keyword >= 25, std::to_string(keyword));
    check("systemverilog.highlight.number", number >= 10, std::to_string(number));
    check("systemverilog.highlight.comment", comment >= 1, std::to_string(comment));
    check("systemverilog.highlight.string", string >= 1, std::to_string(string));
    check("systemverilog.highlight.operator", op >= 5, std::to_string(op));
    check("systemverilog.highlight.type", type >= 3, std::to_string(type));

    const std::vector<FoldRange> folds = provider->foldRanges(tree);
    std::printf("MEASURE folds.systemverilog count=%zu begin_blocks=%d\n", folds.size(),
                countBeginFolds(folds, lines));
    check("systemverilog.folds.min-count", folds.size() >= 9, std::to_string(folds.size()));
    check("systemverilog.folds.look-sane", foldsLookSane(folds, lines));

    struct ExpectedFold {
        const char *opener;
        const char *closer;
        const char *check_name;
    };
    const ExpectedFold expected[] = {
        {"package alu_pkg", "endpackage", "systemverilog.fold.package"},
        {"interface alu_if", "endinterface", "systemverilog.fold.interface"},
        {"class alu_driver", "endclass", "systemverilog.fold.class"},
        {"function new()", "endfunction", "systemverilog.fold.constructor"},
        {"function void set_op", "endfunction", "systemverilog.fold.function"},
        {"task drive", "endtask", "systemverilog.fold.task"},
        {"module alu_top", "endmodule", "systemverilog.fold.module"},
        {"generate", "endgenerate", "systemverilog.fold.generate"},
        {"case (op)", "endcase", "systemverilog.fold.case"},
    };
    for (const ExpectedFold &fold : expected) {
        const int start = findLine(lines, fold.opener);
        const int end = findLine(lines, fold.closer, start);
        check(fold.check_name, hasFold(folds, start, end),
              std::to_string(start) + ".." + std::to_string(end));
    }
    check("systemverilog.fold.begin-blocks", countBeginFolds(folds, lines) >= 2,
          std::to_string(countBeginFolds(folds, lines)));

    const std::vector<QueryRange> structure = provider->queryRanges(tree, "structure");
    const std::vector<std::string> names = namesOf(document, structure);
    std::printf("INFO structure.systemverilog names=%s\n", joinNames(names).c_str());
    check("systemverilog.structure.names",
          hasName(names, "alu_pkg") && hasName(names, "alu_if") && hasName(names, "alu_driver") &&
              hasName(names, "set_op") && hasName(names, "drive") && hasName(names, "alu_top"),
          joinNames(names));

    const std::uint32_t method_offset = byteOfLine(lines, findLine(lines, "op = v;"));
    const std::vector<Breadcrumb> path = provider->breadcrumbs(tree, method_offset);
    std::string path_text;
    for (const Breadcrumb &crumb : path) {
        path_text += (path_text.empty() ? "" : " > ") + crumb.kind + ":" + crumb.name;
    }
    std::printf("INFO breadcrumbs.systemverilog path=%s\n", path_text.c_str());
    check("systemverilog.breadcrumbs.class",
          !path.empty() && path.front().kind == "class" && path.front().name == "alu_driver",
          path_text);
    check("systemverilog.breadcrumbs.function",
          path.size() >= 2 && path.back().kind == "function" && path.back().name == "set_op",
          path_text);
#endif
}

void testRuntimeSwitch()
{
    SyntaxService &service = SyntaxService::instance();
    (void)service.activate(std::string(VEDIT_EXPECT_COMPILED_GRAMMAR));
    check("switch.on.provider-created", service.createProvider() != nullptr);
    service.setTreeSitterEnabled(false);
    check("switch.off.provider-null", service.createProvider() == nullptr);
    check("switch.off.available-false", !service.available());
    service.setTreeSitterEnabled(true);
    check("switch.on.provider-created-again", service.createProvider() != nullptr);
}

} // namespace

int main()
{
    testManifestAndConfig();
    testRegistry();
    testVerilogSample();
    testSystemVerilogSample();
    testRuntimeSwitch();

    std::printf("SYN-GRAMMARS-TESTS checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
