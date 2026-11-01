// T96 independent harness: range-query semantics of T92.
//  1) default implementation (legacy provider): exact filter, whole-tree called once
//  2) per-range result == filter(whole-tree) as an exact interval multiset
//  3) union of range partitions == whole-tree (exact intervals)
//  4) multi-line node coverage (long block comment, ifdef blocks, long line)
//  5) fixed-window range cost vs document size; large-window scaling
#include "bindings.h"
#include "syntax_provider.h"
#include "syntax_service.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace vedit::syntax;

using Key = std::tuple<std::uint32_t, std::uint32_t, std::string>;
using Clock = std::chrono::steady_clock;

static std::vector<Key> keysOf(const std::vector<HighlightSpan> &spans)
{
    std::vector<Key> keys;
    keys.reserve(spans.size());
    for (const auto &s : spans) {
        keys.emplace_back(s.range.start_byte, s.range.end_byte, s.capture);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

static std::vector<HighlightSpan> filterIntersect(const std::vector<HighlightSpan> &full,
                                                  std::uint32_t start, std::uint32_t end)
{
    std::vector<HighlightSpan> out;
    for (const auto &s : full) {
        if (s.range.end_byte > start && end > s.range.start_byte) {
            out.push_back(s);
        }
    }
    return out;
}

static void compare(const std::vector<Key> &got, const std::vector<Key> &expected, long *missing,
                    long *extra)
{
    std::size_t i = 0;
    std::size_t j = 0;
    long matched = 0;
    while (i < got.size() && j < expected.size()) {
        if (got[i] == expected[j]) {
            ++matched;
            ++i;
            ++j;
        } else if (got[i] < expected[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    *missing = static_cast<long>(expected.size()) - matched;
    *extra = static_cast<long>(got.size()) - matched;
}

class LegacyMock final : public SyntaxProvider
{
public:
    std::string id() const override { return "legacy-mock"; }
    std::string displayName() const override { return "legacy mock"; }
    std::string version() const override { return "1"; }
    std::string license() const override { return "test"; }
    Capabilities capabilities() const override { return Capabilities{}; }
    ParseTreePtr parse(std::string_view) override { return nullptr; }
    ParseTreePtr tree() const override { return nullptr; }
    bool hasSyntaxErrors(const ParseTreePtr &) const override { return false; }
    std::vector<std::string> queryIds() const override { return {}; }
    std::vector<QueryRange> queryRanges(const ParseTreePtr &, std::string_view) override { return {}; }
    std::vector<HighlightSpan> highlightSpans(const ParseTreePtr &) override
    {
        ++whole_tree_calls;
        return spans;
    }
    std::vector<FoldRange> foldRanges(const ParseTreePtr &) override { return {}; }
    std::vector<Breadcrumb> breadcrumbs(const ParseTreePtr &, std::uint32_t) override { return {}; }
    bool applyEdit(const Edit &) override { return false; }
    std::string_view text() const override { return {}; }
    std::vector<HighlightSpan> spans;
    int whole_tree_calls = 0;
};

static std::string stressDocument()
{
    std::string text;
    text += "// metadata: file top.sv\n";
    text += "// metadata: T96 range-query export test\n";
    text += "/* multi-line license block\n";
    for (int i = 0; i < 57; ++i) {
        text += "   comment line " + std::to_string(i) + " with more text\n";
    }
    text += "*/\n";
    text += "`ifdef SYNTHESIS\n";
    text += "`ifdef CLOCK_GATE\n";
    text += "`define DEPTH 16\n";
    text += "  /* nested block comment inside ifdef */\n";
    text += "`endif\n";
    text += "`endif\n";
    text += "package pkg;\n";
    text += "  typedef enum logic [1:0] { A, B, C } state_t;\n";
    text += "  class helper;\n";
    text += "    int count;\n";
    text += "  endclass\n";
    text += "endpackage\n";
    text += "module top #(parameter WIDTH = 8) (input logic clk, input logic rst);\n";
    text += "  ";
    for (int i = 0; i < 200; ++i) {
        text += "assign w" + std::to_string(i) + " = " + std::to_string(i % 2) + "; ";
    }
    text += "\n";
    text += "  string s0 = \"a single-line string literal\";\n";
    for (int i = 0; i < 40; ++i) {
        text += "  always_ff @(posedge clk) begin\n";
        text += "    if (!rst) counter" + std::to_string(i) + " <= 0;\n";
        text += "    else counter" + std::to_string(i) + " <= counter" + std::to_string(i) + " + 1;\n";
        text += "  end\n";
    }
    text += "endmodule\n";
    return text;
}

static std::string perfDocument(int units)
{
    std::string text;
    for (int index = 0; index < units; ++index) {
        text += "module m" + std::to_string(index) + ";\n";
        text += "  logic [7:0] a" + std::to_string(index) + ";\n";
        text += "  logic [7:0] b" + std::to_string(index) + ";\n";
        text += "  always_comb begin\n";
        text += "    case (sel)\n";
        text += "      1'b0: a" + std::to_string(index) + " = b" + std::to_string(index) + ";\n";
        text += "      default: a" + std::to_string(index) + " = 8'h00;\n";
        text += "    endcase\n";
        text += "  end\n";
        text += "  generate\n";
        text += "    if (1) begin : g" + std::to_string(index) + "\n";
        text += "      assign b" + std::to_string(index) + " = a" + std::to_string(index) + ";\n";
        text += "    end\n";
        text += "  endgenerate\n";
        text += "  function automatic integer f" + std::to_string(index) + ";\n";
        text += "    f" + std::to_string(index) + " = 0;\n";
        text += "  endfunction\n";
        text += "  task t" + std::to_string(index) + ";\n";
        text += "  endtask\n";
        text += "endmodule\n";
    }
    return text;
}

static double medianMs(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

static double microsOf(Clock::time_point a, Clock::time_point b)
{
    return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(b - a).count());
}

int main()
{
    std::printf("T96-RANGE-EQUIV start\n");
    vedit::syntax::grammars::registerBuiltinGrammars();
    SyntaxService::instance().resetForTests();
    const bool activated = SyntaxService::instance().activate("systemverilog");
    std::unique_ptr<SyntaxProvider> provider = SyntaxService::instance().createProvider();
    std::printf("activated=%d provider=%s\n", activated ? 1 : 0,
                provider ? provider->id().c_str() : "<null>");
    if (!provider) {
        std::printf("FATAL no provider\n");
        return 2;
    }
    int hard_failures = 0;

    // ---- 1. default implementation on a legacy provider --------------------
    LegacyMock legacy;
    legacy.spans = {
        HighlightSpan{Range{10, 50, 0, 0}, "a"},
        HighlightSpan{Range{40, 80, 0, 0}, "b"},
        HighlightSpan{Range{100, 120, 0, 0}, "c"},
    };
    SyntaxProvider &legacyBase = legacy;
    const auto legacyGot = keysOf(legacyBase.highlightSpans(nullptr, Range{30, 60, 0, 0}));
    const auto legacyExpected = keysOf(filterIntersect(legacy.spans, 30, 60));
    const bool default_ok = legacyGot == legacyExpected && legacy.whole_tree_calls == 1;
    std::printf("DEFAULT-IMPL exact=%d whole_tree_calls=%d got=%zu expected=%zu\n",
                default_ok ? 1 : 0, legacy.whole_tree_calls, legacyGot.size(),
                legacyExpected.size());
    if (!default_ok) {
        ++hard_failures;
    }
    {
        const auto empty = keysOf(legacyBase.highlightSpans(nullptr, Range{500, 600, 0, 0}));
        std::printf("DEFAULT-IMPL empty-range-result=%zu (legacy whole_tree_calls=%d)\n",
                    empty.size(), legacy.whole_tree_calls);
    }

    // ---- 2/3/4. range coverage on the real tree-sitter provider ------------
    const std::string text = stressDocument();
    const std::uint32_t size = static_cast<std::uint32_t>(text.size());
    ParseTreePtr tree = provider->parse(text);
    if (!tree) {
        std::printf("FATAL no tree\n");
        return 2;
    }
    const auto full = provider->highlightSpans(tree);
    const std::size_t lines = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
    std::printf("STRESS bytes=%u lines=%zu full_spans=%zu\n", size, lines, full.size());
    std::size_t long_spans = 0;
    std::size_t longest = 0;
    std::uint32_t longest_start = 0;
    std::uint32_t longest_end = 0;
    std::string longest_capture;
    for (const auto &s : full) {
        const std::uint32_t len = s.range.end_byte - s.range.start_byte;
        if (len > 500) {
            ++long_spans;
            if (len > longest) {
                longest = len;
                longest_start = s.range.start_byte;
                longest_end = s.range.end_byte;
                longest_capture = s.capture;
            }
        }
    }
    std::printf("STRESS long-spans(>500B)=%zu longest=%zu capture=%s bytes=[%u,%u)\n",
                long_spans, longest, longest_capture.c_str(), longest_start, longest_end);

    struct Case {
        std::string name;
        std::uint32_t start = 0;
        std::uint32_t end = 0;
        bool strict = true;
    };
    std::vector<Case> cases;
    for (std::uint32_t start = 0; start < size; start += 97) {
        cases.push_back(Case{"byte-97", start, std::min(size, start + 97)});
    }
    for (std::uint32_t start = 0; start < size; start += 512) {
        cases.push_back(Case{"byte-512", start, std::min(size, start + 512)});
    }
    std::uint32_t line_start = 0;
    int line_count = 0;
    for (std::uint32_t i = 0; i <= size; ++i) {
        if (i == size || text[i] == '\n') {
            ++line_count;
            if (line_count % 5 == 0 || i == size) {
                cases.push_back(Case{"line-5", line_start, i});
                line_start = i + 1;
            }
        }
    }
    std::mt19937 rng(20261101u);
    std::uniform_int_distribution<std::uint32_t> offset_dist(0, size - 1);
    std::uniform_int_distribution<std::uint32_t> length_dist(1, 4000);
    for (int i = 0; i < 250; ++i) {
        const std::uint32_t a = offset_dist(rng) % size;
        cases.push_back(Case{"random", a, std::min(size, a + length_dist(rng))});
    }
    // windows INSIDE the longest multi-line node (deep inside a long comment)
    if (longest_end > longest_start + 100) {
        const std::uint32_t len = longest_end - longest_start;
        for (int depth = 1; depth <= 9; ++depth) {
            const std::uint32_t a = longest_start + (len * static_cast<std::uint32_t>(depth)) / 10;
            const std::uint32_t b = std::min(longest_end, a + 40);
            cases.push_back(Case{"inside-longest-node", a, b});
        }
        cases.push_back(Case{"longest-node-cross-start", longest_start - 10, longest_start + 20});
        cases.push_back(Case{"longest-node-exact", longest_start, longest_end});
        cases.push_back(Case{"longest-node-tail", longest_end - 5, longest_end + 200});
    }
    for (std::size_t i = 0; i < full.size() && i < 60; ++i) {
        cases.push_back(Case{"span-bounds", full[i].range.start_byte, full[i].range.end_byte});
    }
    cases.push_back(Case{"full-range", 0, size});
    cases.push_back(Case{"beyond-end", size + 5, size + 100});
    cases.push_back(Case{"zero-length", size / 3, size / 3, false});
    cases.push_back(Case{"inverted", size / 2, size / 2 - 30, false});

    long failing = 0;
    long total_missing = 0;
    long total_extra = 0;
    long zero_len_got = 0;
    long zero_len_expected = 0;
    long inverted_got = 0;
    long inverted_expected = 0;
    for (const Case &c : cases) {
        const auto got = provider->highlightSpans(tree, Range{c.start, c.end, 0, 0});
        const auto expected = filterIntersect(full, c.start, c.end);
        long missing = 0;
        long extra = 0;
        compare(keysOf(got), keysOf(expected), &missing, &extra);
        if (c.name == "zero-length") {
            zero_len_got = static_cast<long>(got.size());
            zero_len_expected = static_cast<long>(expected.size());
            continue;
        }
        if (c.name == "inverted") {
            inverted_got = static_cast<long>(got.size());
            inverted_expected = static_cast<long>(expected.size());
            continue;
        }
        if (missing != 0 || extra != 0) {
            ++failing;
            total_missing += missing;
            total_extra += extra;
            if (failing <= 15) {
                std::printf("CASE-FAIL name=%s bytes=[%u,%u) missing=%ld extra=%ld\n",
                            c.name.c_str(), c.start, c.end, missing, extra);
            }
        }
    }
    std::printf("CASES total=%zu failing=%zu missing_total=%ld extra_total=%ld\n", cases.size(),
                failing, total_missing, total_extra);
    std::printf("EDGE zero-length got=%ld expected=%ld (observation)\n", zero_len_got,
                zero_len_expected);
    std::printf("EDGE inverted got=%ld expected=%ld (observation)\n", inverted_got,
                inverted_expected);
    if (failing != 0) {
        ++hard_failures;
    }

    // ---- 3. union over partitions == whole tree (exact interval sets) ------
    const std::uint32_t partition_steps[] = {97, 512, 4096};
    for (const std::uint32_t step : partition_steps) {
        std::set<Key> union_keys;
        long queries = 0;
        long cross_window_dups = 0;
        for (std::uint32_t start = 0; start < size; start += step) {
            const auto got = provider->highlightSpans(tree, Range{start, std::min(size, start + step), 0, 0});
            for (const Key &key : keysOf(got)) {
                if (!union_keys.insert(key).second) {
                    ++cross_window_dups;
                }
            }
            ++queries;
        }
        const std::vector<Key> full_keys = keysOf(full);
        const std::set<Key> full_set(full_keys.begin(), full_keys.end());
        long missing = 0;
        for (const Key &key : full_set) {
            if (union_keys.find(key) == union_keys.end()) {
                ++missing;
            }
        }
        long extra = 0;
        for (const Key &key : union_keys) {
            if (full_set.find(key) == full_set.end()) {
                ++extra;
            }
        }
        const bool equal = missing == 0 && extra == 0;
        std::printf("UNION step=%u queries=%ld full=%zu union=%zu missing=%ld extra=%ld "
                    "cross-window-dups=%ld equal=%d\n",
                    step, queries, full_set.size(), union_keys.size(), missing, extra,
                    cross_window_dups, equal ? 1 : 0);
        if (!equal) {
            ++hard_failures;
        }
    }
    {
        const auto full_range = provider->highlightSpans(tree, Range{0, size, 0, 0});
        const bool same = keysOf(full_range) == keysOf(full);
        std::printf("FULL-RANGE equals-whole-tree=%d got=%zu full=%zu\n", same ? 1 : 0,
                    full_range.size(), full.size());
        if (!same) {
            ++hard_failures;
        }
    }

    // ---- 5. fixed-window cost vs document size; large-window scaling ------
    std::printf("PERF-BEGIN\n");
    const int units_list[] = {50, 96, 240, 480};
    for (const int units : units_list) {
        const std::string perf_text = perfDocument(units);
        ParseTreePtr perf_tree = provider->parse(perf_text);
        if (!perf_tree) {
            std::printf("PERF units=%d NO-TREE\n", units);
            continue;
        }
        const std::uint32_t psize = static_cast<std::uint32_t>(perf_text.size());
        const std::size_t plines = static_cast<std::size_t>(std::count(perf_text.begin(),
                                                                       perf_text.end(), '\n'));
        std::vector<double> full_runs;
        std::size_t full_spans = 0;
        for (int run = 0; run < 5; ++run) {
            const auto t0 = Clock::now();
            full_spans = provider->highlightSpans(perf_tree).size();
            full_runs.push_back(microsOf(t0, Clock::now()) / 1000.0);
        }
        const auto window_ms = [&](std::uint32_t bytes, std::size_t *spans_out) {
            const std::uint32_t start = psize / 2;
            const std::uint32_t end = std::min(psize, start + bytes);
            std::vector<double> runs;
            std::size_t spans = 0;
            for (int run = 0; run < 20; ++run) {
                const auto t0 = Clock::now();
                spans = provider->highlightSpans(perf_tree, Range{start, end, 0, 0}).size();
                runs.push_back(microsOf(t0, Clock::now()) / 1000.0);
            }
            if (spans_out != nullptr) {
                *spans_out = spans;
            }
            return medianMs(runs);
        };
        std::size_t spans_2k = 0;
        std::size_t spans_16k = 0;
        const double ms2k = window_ms(2048, &spans_2k);
        const double ms16k = window_ms(16384, &spans_16k);
        const double ms_q = window_ms(psize / 4, nullptr); // a large window (25% of the doc)
        // the writer's own sweep window: lines 20..60 (compile-independent approximation:
        // middle 2,000 bytes would be too small); report the measured 2k/16k/25% for comparison
        std::printf("PERF lines=%zu bytes=%u full_ms=%.3f range_2k_ms=%.4f range_16k_ms=%.4f "
                    "range_25pct_ms=%.4f spans_full=%zu spans_2k=%zu spans_16k=%zu\n",
                    plines, psize, medianMs(full_runs), ms2k, ms16k, ms_q, full_spans, spans_2k,
                    spans_16k);
        std::fflush(stdout);
    }
    {
        // Zero-length range INSIDE a known multi-line span: default-vs-override consistency check.
        const std::uint32_t probe = longest_start + 500;
        const auto got = provider->highlightSpans(tree, Range{probe, probe, 0, 0});
        const auto expected = filterIntersect(full, probe, probe);
        std::printf("EDGE zero-length-inside-span point=%u got=%zu expected=%zu "
                    "(default semantics filter)\n",
                    probe, got.size(), expected.size());
    }
    std::printf("T96-RANGE-EQUIV end hard_failures=%d\n", hard_failures);
    return hard_failures == 0 ? 0 : 1;
}
