// Lane validator for src/syn-overlay (T18): A1g single owner, A1h runtime
// fallback, C12 incremental overlay and the Open Q2 performance budget.
//
//   vedit-syntax-overlay-selftest --selfcheck
//
// Every check prints one machine-readable line; the last line carries the
// measured performance numbers so the evidence file can quote them verbatim.
#if defined(VEDIT_OVERLAY_HAVE_GRAMMARS)
#include "bindings.h"
#endif
#include "overlay.h"
#include "paint.h"
#include "syntax_service.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using vedit::syntax::Edit;
using vedit::syntax::HighlightSpan;
using vedit::syntax::OwnerArbiter;
using vedit::syntax::OwnerKind;
using vedit::syntax::OwnerSpan;
using vedit::syntax::SyntaxService;
using vedit::syntax::overlay::Engine;
using vedit::syntax::overlay::BaseSpan;
using vedit::syntax::overlay::OverlayController;
using vedit::syntax::overlay::PaintPlan;
using vedit::syntax::overlay::PaintSegment;
using vedit::syntax::overlay::State;
using vedit::syntax::overlay::buildPaintPlan;
using vedit::syntax::overlay::describePaintPlan;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skips = 0;

void check(const char *name, bool ok, const std::string &detail = std::string())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.empty() ? "" : " ", detail.c_str());
    std::fflush(stdout);
}

void skip(const char *name, const char *reason)
{
    ++g_checks;
    ++g_skips;
    std::printf("CHECK %s SKIP %s\n", name, reason);
    std::fflush(stdout);
}

// A synthetic SystemVerilog document. `units` modules keep the tree-sitter query
// busy with a realistic number of captures without needing a fixture file.
std::string makeVerilog(int units)
{
    std::string text;
    text.reserve(static_cast<std::size_t>(units) * 320);
    text += "// synthetic document for the overlay validator\n";
    for (int unit = 0; unit < units; ++unit) {
        text += "module unit_" + std::to_string(unit) + " #(parameter int W = 8) (\n";
        text += "    input  logic         clk,\n";
        text += "    input  logic         rst_n,\n";
        text += "    output logic [W-1:0] q\n";
        text += ");\n";
        text += "    logic [W-1:0] next_q;\n";
        text += "    always_comb begin\n";
        text += "        next_q = q + 1'b1;\n";
        text += "    end\n";
        text += "    always_ff @(posedge clk or negedge rst_n) begin\n";
        text += "        if (!rst_n) begin\n";
        text += "            q <= '0;\n";
        text += "        end else begin\n";
        text += "            q <= next_q;\n";
        text += "        end\n";
        text += "    end\n";
        text += "    function automatic logic [W-1:0] inc(input logic [W-1:0] value);\n";
        text += "        return value + 1'b1;\n";
        text += "    endfunction\n";
        text += "endmodule\n\n";
    }
    return text;
}

std::uint32_t lineStart(std::string_view text, std::size_t line)
{
    std::size_t current = 0;
    std::size_t offset = 0;
    while (current < line && offset < text.size()) {
        const std::size_t next = text.find('\n', offset);
        if (next == std::string::npos) {
            return static_cast<std::uint32_t>(text.size());
        }
        offset = next + 1;
        ++current;
    }
    return static_cast<std::uint32_t>(offset);
}

std::uint32_t lineEnd(std::string_view text, std::size_t line)
{
    const std::uint32_t start = lineStart(text, line);
    const std::size_t next = text.find('\n', start);
    return next == std::string::npos ? static_cast<std::uint32_t>(text.size())
                                     : static_cast<std::uint32_t>(next);
}

// Base highlighter stand-in: one style span per line. The overlay claims inside
// those lines, so the clipping path is always exercised (A1g).
std::vector<BaseSpan> baseLineSpans(const std::string &text)
{
    std::vector<BaseSpan> spans;
    std::size_t offset = 0;
    std::uint32_t style = 1;
    while (offset < text.size()) {
        const std::size_t next = text.find('\n', offset);
        const std::size_t end = next == std::string::npos ? text.size() : next;
        if (end > offset) {
            spans.push_back(BaseSpan{static_cast<std::uint32_t>(offset),
                                     static_cast<std::uint32_t>(end), style});
        }
        offset = end + 1;
        style = style % 7 + 1;
    }
    return spans;
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    std::size_t index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    index = std::min(index, values.size() - 1);
    return values[index];
}

// --- A1g: single owner, no double colouring --------------------------------
void checkSingleOwner()
{
    const std::string text = makeVerilog(6);
    OverlayController controller("verilog", "tree-sitter");
    std::string error;
    if (!controller.open(text, &error)) {
        skip("overlay.single-owner", "no tree-sitter provider (see overlay.provider)");
        return;
    }
    const std::uint32_t size = static_cast<std::uint32_t>(text.size());
    const std::size_t claims = controller.injectWindow(0, size);
    check("overlay.inject.claims", claims > 0,
          "claims=" + std::to_string(claims) + " spans=" + std::to_string(controller.spans().size()));
    std::string detail;
    const bool single = controller.assertSingleOwner(0, size, &detail);
    check("overlay.single-owner", single, detail);
    const std::size_t overlay_owners = [&controller, size] {
        std::size_t count = 0;
        for (const OwnerSpan &span : controller.ownerSpans(0, size)) {
            if (span.kind == OwnerKind::Overlay) {
                ++count;
            }
        }
        return count;
    }();
    check("overlay.single-owner.overlay-present", overlay_owners > 0,
          "overlay spans=" + std::to_string(overlay_owners));
    // A1g judgement over a few known ranges (the header of the file, one module
    // body, the tail) rather than "somewhere in the document".
    bool ranges_ok = true;
    for (std::uint32_t line : {0u, 5u, 12u, 60u}) {
        const std::uint32_t from = lineStart(text, line);
        const std::uint32_t to = lineEnd(text, line);
        if (from < to && !controller.assertSingleOwner(from, to, &detail)) {
            ranges_ok = false;
        }
    }
    check("overlay.single-owner.known-ranges", ranges_ok, "lines 0,5,12,60");
}

void checkPaintPlan()
{
    const std::string text = makeVerilog(4);
    OverlayController controller("verilog", "tree-sitter");
    std::string error;
    if (!controller.open(text, &error)) {
        skip("overlay.paint.no-double", "no tree-sitter provider");
        return;
    }
    const std::uint32_t size = static_cast<std::uint32_t>(text.size());
    controller.injectWindow(0, size);
    const std::vector<BaseSpan> base = baseLineSpans(text);
    const PaintPlan plan = buildPaintPlan(base, controller.spans(), 0, size);
    check("overlay.paint.segments", !plan.segments.empty(), describePaintPlan(plan));
    check("overlay.paint.contiguous", plan.contiguous, describePaintPlan(plan));
    check("overlay.paint.partition-covers-the-range",
          plan.base_bytes + plan.overlay_bytes + plan.none_bytes == size,
          "base=" + std::to_string(plan.base_bytes) + " overlay="
              + std::to_string(plan.overlay_bytes) + " none=" + std::to_string(plan.none_bytes));
    // The raw inputs DO overlap (base colours every line, the overlay claims
    // inside it): the test data therefore exercises the clipping (A1g).
    check("overlay.paint.raw-overlap-exercised", plan.raw_overlap_bytes > 0,
          "raw-overlap=" + std::to_string(plan.raw_overlap_bytes));
    // Every overlay claim must be painted by the overlay engine, never by base.
    bool clip_ok = true;
    std::size_t checked_claims = 0;
    for (const HighlightSpan &span : controller.spans()) {
        if (span.range.start_byte >= size) {
            continue;
        }
        const PaintPlan claim_plan = buildPaintPlan(base, controller.spans(), span.range.start_byte,
                                                    span.range.end_byte);
        for (const PaintSegment &segment : claim_plan.segments) {
            if (segment.engine == Engine::Base) {
                clip_ok = false;
            }
        }
        ++checked_claims;
        if (checked_claims >= 40) {
            break;
        }
    }
    check("overlay.paint.claims-never-base", clip_ok,
          "checked claims=" + std::to_string(checked_claims));
    // The paint plan and the arbiter must agree byte-for-byte (one owner each).
    bool agrees = true;
    for (const PaintSegment &segment : plan.segments) {
        const OwnerSpan owner = OwnerArbiter::instance().ownerAt("verilog", segment.start_byte);
        const bool plan_overlay = segment.engine == Engine::Overlay;
        const bool arbiter_overlay = owner.kind == OwnerKind::Overlay;
        if (plan_overlay != arbiter_overlay) {
            agrees = false;
            break;
        }
    }
    check("overlay.paint.arbiter-agrees", agrees, "segment engine == arbiter owner kind");
}

// --- A1h: runtime fallback, never a crash ----------------------------------
void checkFallback()
{
    const std::string text = makeVerilog(4);
    OverlayController controller("verilog", "tree-sitter");
    std::string error;
    if (!controller.open(text, &error)) {
        skip("overlay.fallback.base-owner", "no tree-sitter provider");
        return;
    }
    const std::uint32_t size = static_cast<std::uint32_t>(text.size());
    controller.injectWindow(0, size);
    const std::size_t before = controller.claimCount();
    controller.setEnabled(false);
    check("overlay.fallback.state", controller.state() == State::Disabled,
          controller.stateName() + " | " + controller.lastReason());
    check("overlay.fallback.claims-released", controller.claimCount() == 0,
          "before=" + std::to_string(before) + " after=" + std::to_string(controller.claimCount()));
    bool base_only = true;
    for (const OwnerSpan &span : controller.ownerSpans(0, size)) {
        if (span.kind != OwnerKind::Base) {
            base_only = false;
        }
    }
    check("overlay.fallback.base-owns-everything", base_only, "no overlay owner left");
    // Disabled injection is a no-op, not a crash and not a silent claim.
    check("overlay.fallback.inject-is-noop", controller.injectWindow(0, size) == 0,
          "claims=" + std::to_string(controller.claimCount()));
    // Toggling while a paint pass is in flight: 100 cycles, claims in between.
    bool survived = true;
    for (int cycle = 0; cycle < 100 && survived; ++cycle) {
        controller.setEnabled(true);
        controller.injectWindow(0, size / 2);
        controller.setEnabled(false);
        if (controller.claimCount() != 0) {
            survived = false;
        }
    }
    check("overlay.fallback.toggle-stress", survived, "100 enable/disable cycles");
    controller.setEnabled(true);
    check("overlay.fallback.reenabled", controller.state() == State::Active, controller.stateName());
}

void checkServiceSwitch()
{
    SyntaxService &service = SyntaxService::instance();
    const std::string text = makeVerilog(2);
    service.setTreeSitterEnabled(false);
    OverlayController disabled("verilog", "tree-sitter");
    std::string error;
    const bool opened = disabled.open(text, &error);
    const bool no_provider = disabled.state() == State::NoProvider;
    const std::size_t claims = disabled.injectWindow(0, static_cast<std::uint32_t>(text.size()));
    service.setTreeSitterEnabled(true);
    check("overlay.service-switch.no-provider", !opened && no_provider,
          disabled.stateName() + " | " + error);
    check("overlay.service-switch.no-claims", claims == 0 && disabled.claimCount() == 0, "claims=0");
    // Restoring the switch must bring the overlay back without a re-open crash.
    disabled.open(text, &error);
    check("overlay.service-switch.restored", disabled.state() == State::Active, disabled.stateName());
}

// --- C12: incremental overlay ----------------------------------------------
void checkIncrementalEquivalence()
{
    const std::string text = makeVerilog(20);
    OverlayController controller("verilog", "tree-sitter");
    std::string error;
    if (!controller.open(text, &error)) {
        skip("overlay.incremental.equivalence", "no tree-sitter provider");
        return;
    }
    std::uint32_t window_start = lineStart(controller.text(), 200);
    std::uint32_t window_end = lineEnd(controller.text(), 260);
    controller.injectWindow(window_start, window_end);
    // 20 edits: insert a line inside the window, delete one, and edit outside.
    for (int step = 0; step < 20; ++step) {
        const std::string current = std::string(controller.text());
        Edit edit;
        if (step % 3 == 0) {
            // insert a new line at the window start
            edit.start_byte = window_start;
            edit.old_end_byte = window_start;
            edit.new_text = "    // inserted " + std::to_string(step) + "\n";
        } else if (step % 3 == 1) {
            // delete one byte inside the window
            const std::uint32_t at = std::min<std::uint32_t>(window_start + 8,
                                                             static_cast<std::uint32_t>(current.size()));
            edit.start_byte = at;
            edit.old_end_byte = std::min<std::uint32_t>(at + 1, static_cast<std::uint32_t>(current.size()));
            edit.new_text.clear();
        } else {
            // edit far before the window: every later offset shifts
            edit.start_byte = 0;
            edit.old_end_byte = 0;
            edit.new_text = "// header\n";
        }
        edit.start_line = 0;
        edit.old_end_line = 0;
        window_end = std::min<std::uint32_t>(window_end + 100, static_cast<std::uint32_t>(controller.text().size()));
        controller.applyEdit(edit, window_start, window_end);
        window_start = std::min<std::uint32_t>(window_start + 32, static_cast<std::uint32_t>(controller.text().size()));
    }
    // A fresh controller on the SAME final text is the reference.
    OverlayController reference("verilog", "tree-sitter-reference");
    reference.open(controller.text(), &error);
    const std::uint32_t size = static_cast<std::uint32_t>(controller.text().size());
    // Same visible window in the reference: compare the claimed byte ranges.
    controller.injectWindow(0, size);
    reference.injectWindow(0, size);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> incremental;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> full;
    for (const OwnerSpan &span : controller.ownerSpans(0, size)) {
        if (span.kind == OwnerKind::Overlay) {
            incremental.emplace_back(span.start_byte, span.end_byte);
        }
    }
    for (const OwnerSpan &span : reference.ownerSpans(0, size)) {
        if (span.kind == OwnerKind::Overlay) {
            full.emplace_back(span.start_byte, span.end_byte);
        }
    }
    check("overlay.incremental.equivalence",
          !incremental.empty() && incremental == full,
          "incremental=" + std::to_string(incremental.size()) + " full="
              + std::to_string(full.size()) + " text-bytes=" + std::to_string(size));
}

void checkWindowIsolation()
{
    const std::string text = makeVerilog(40);
    OverlayController controller("verilog", "tree-sitter");
    std::string error;
    if (!controller.open(text, &error)) {
        skip("overlay.scroll.window-isolated", "no tree-sitter provider");
        return;
    }
    const std::uint32_t size = static_cast<std::uint32_t>(text.size());
    bool isolated = true;
    bool nonempty = true;
    for (int scroll = 0; scroll < 50; ++scroll) {
        const std::uint32_t start = lineStart(text, static_cast<std::size_t>(scroll * 8));
        const std::uint32_t end = std::min<std::uint32_t>(lineEnd(text, static_cast<std::size_t>(scroll * 8 + 40)), size);
        const std::size_t claims = controller.injectWindow(start, end);
        if (claims == 0) {
            nonempty = false;
        }
        // claims outside the injected window would mean stale ownership
        for (const OwnerSpan &span : controller.ownerSpans(0, size)) {
            if (span.kind == OwnerKind::Overlay && (span.start_byte < start || span.end_byte > end)) {
                isolated = false;
            }
        }
    }
    check("overlay.scroll.window-isolated", isolated, "50 scroll windows, no claim outside the window");
    check("overlay.scroll.windows-nonempty", nonempty, "each window claimed at least one span");
}

void checkGuards()
{
    OverlayController controller("verilog", "tree-sitter");
    std::string error;
    controller.open("", &error); // empty document
    Edit edit;
    edit.start_byte = 10;
    edit.old_end_byte = 10;
    edit.new_text = "x";
    const std::size_t after = controller.applyEdit(edit, 0, 4);
    check("overlay.guards.edit-out-of-range", after == 0, "no crash, no claim");
    controller.close();
    controller.close(); // double close must be a no-op
    check("overlay.guards.double-close", controller.claimCount() == 0, "claims=0");
    // Disabled provider path with a fresh controller (service switch off).
    SyntaxService::instance().setTreeSitterEnabled(false);
    OverlayController off("verilog", "tree-sitter");
    check("overlay.guards.no-provider-open", !off.open("module x; endmodule\n", &error),
          off.stateName());
    SyntaxService::instance().setTreeSitterEnabled(true);
}

// --- Open Q2: the performance budget --------------------------------------
void checkPerformance()
{
    const std::string text = makeVerilog(50); // ~1.2k lines of ~1k-line scale content: C12/A1f
    OverlayController controller("verilog", "tree-sitter");
    std::string error;
    if (!controller.open(text, &error)) {
        skip("overlay.perf.rebuild-50", "no tree-sitter provider");
        skip("overlay.perf.injection-p95", "no tree-sitter provider");
        return;
    }
    const std::uint32_t size = static_cast<std::uint32_t>(text.size());
    std::uint32_t window_start = lineStart(controller.text(), 20);
    std::uint32_t window_end = lineStart(controller.text(), 60);
    controller.injectWindow(window_start, window_end);

    // Three 50-rebuild batches; the median batch is the verdict (a single batch
    // on a loaded machine is one scheduler hiccup away from a false red).
    std::vector<double> batches;
    batches.reserve(3);
    // Net-zero edits (insert one byte, delete it on the next round) keep the
    // document size constant, so every batch measures the same workload instead
    // of a document that grows with each inserted line.
    for (int batch = 0; batch < 3; ++batch) {
        std::string error2;
        controller.open(text, &error2);
        window_start = lineStart(controller.text(), 20);
        window_end = lineStart(controller.text(), 60);
        controller.injectWindow(window_start, window_end);
        long long rebuild_total_us = 0;
        for (int round = 0; round < 50; ++round) {
            Edit edit;
            edit.start_byte = std::min<std::uint32_t>(window_start + 4, static_cast<std::uint32_t>(controller.text().size()));
            edit.old_end_byte = edit.start_byte;
            edit.new_text = (round % 2 == 0) ? "x" : "";
            if (round % 2 != 0) {
                edit.start_byte = std::max<std::uint32_t>(edit.start_byte - 1, 0);
                edit.old_end_byte = edit.start_byte + 1;
            }
            controller.applyEdit(edit, window_start, window_end);
            rebuild_total_us += controller.lastTiming().rebuild_us;
        }
        batches.push_back(static_cast<double>(rebuild_total_us) / 1000.0);
    }
    const double rebuild_ms = percentile(batches, 0.50);
    check("overlay.perf.rebuild-50", rebuild_ms <= 150.0,
          "50 rebuilds median=" + std::to_string(rebuild_ms) + " ms of 3 batches (best "
              + std::to_string(percentile(batches, 0.0)) + ", worst " + std::to_string(percentile(batches, 1.0))
              + "), budget 150 ms, bytes=" + std::to_string(size));

    // Single injection (one visible window) — p95 budget 2 ms.
    std::vector<double> injections;
    injections.reserve(200);
    for (int round = 0; round < 200; ++round) {
        const std::uint32_t start = lineStart(controller.text(), static_cast<std::size_t>(round % 200));
        const std::uint32_t end = std::min<std::uint32_t>(lineStart(controller.text(), static_cast<std::size_t>(round % 200 + 40)),
                                                          static_cast<std::uint32_t>(controller.text().size()));
        controller.injectWindow(start, end);
        injections.push_back(static_cast<double>(controller.lastTiming().injection_us) / 1000.0);
    }
    const double p50 = percentile(injections, 0.50);
    const double p95 = percentile(injections, 0.95);
    const double worst = percentile(injections, 1.0);
    std::printf("PERF doc_bytes=%zu doc_lines=%zu rebuild50_ms=%.3f injection_p50_ms=%.3f injection_p95_ms=%.3f injection_max_ms=%.3f text_us=%.1f sync_us=%.1f query_us=%.1f\n",
                controller.text().size(), static_cast<std::size_t>(std::count(controller.text().begin(), controller.text().end(), '\n')),
                rebuild_ms, p50, p95, worst, static_cast<double>(controller.lastTiming().text_us),
                static_cast<double>(controller.lastTiming().sync_us),
                static_cast<double>(controller.lastTiming().query_us));
    std::fflush(stdout);
    check("overlay.perf.injection-p95", p95 <= 2.0,
          "p50=" + std::to_string(p50) + " p95=" + std::to_string(p95) + " max=" + std::to_string(worst)
              + " ms (budget 2 ms)");
}

// --- T92: the document-size curve (1k / 2k / 5k / 10k lines) --------------
// T17 registered that a single rebuild spent ~90% of its budget inside the
// provider's WHOLE-TREE highlight query, so the cost grew with the document and
// the 150 ms budget would break somewhere above 1.5k lines. This sweep measures
// the same method at four sizes and prints one machine-readable line per size:
//   * full_query_ms  -- provider->highlightSpans(tree)             (pre-T92 path)
//   * range_query_ms -- provider->highlightSpans(tree, window)     (T92 path)
//   * rebuild50_ms   -- sum of 50 applyEdit rebuilds, median of 3 batches (≤150)
//   * injection_p95  -- one visible window per call, p95 (≤2 ms)
// The budget checks run at EVERY size, including the 10k-line document a real
// SystemVerilog project actually has.
long processRssKb()
{
#if defined(__linux__)
    std::FILE *file = std::fopen("/proc/self/status", "r");
    if (file == nullptr) {
        return -1;
    }
    char line[256];
    long kb = -1;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        if (std::strncmp(line, "VmRSS:", 6) == 0) {
            kb = std::strtol(line + 6, nullptr, 10);
            break;
        }
    }
    std::fclose(file);
    return kb;
#else
    return -1;
#endif
}

void checkPerformanceSweep()
{
    const int unitCounts[] = {50, 96, 240, 480}; // 1k(T17 doc) / 2k / 5k / 10k lines (21/unit)
    for (const int units : unitCounts) {
        const std::string text = makeVerilog(units);
        OverlayController controller("verilog", "tree-sitter");
        std::string error;
        if (!controller.open(text, &error)) {
            skip("overlay.perf-sweep", "no tree-sitter provider");
            return;
        }
        const std::uint32_t size = static_cast<std::uint32_t>(text.size());
        const std::size_t lines =
            static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));

        // (a) query cost on the same tree: whole document vs one visible window.
        std::unique_ptr<vedit::syntax::SyntaxProvider> probe =
            SyntaxService::instance().createProvider();
        const vedit::syntax::ParseTreePtr tree = probe != nullptr ? probe->parse(text) : nullptr;
        double fullQueryMs = -1.0;
        double rangeQueryMs = -1.0;
        std::size_t fullSpans = 0;
        if (tree) {
            std::vector<double> fullRuns;
            fullRuns.reserve(5);
            for (int run = 0; run < 5; ++run) {
                const auto started = std::chrono::steady_clock::now();
                fullSpans = probe->highlightSpans(tree).size();
                fullRuns.push_back(static_cast<double>(std::chrono::duration_cast<
                                                           std::chrono::microseconds>(
                                                           std::chrono::steady_clock::now() - started)
                                                           .count())
                                   / 1000.0);
            }
            fullQueryMs = percentile(fullRuns, 0.5);
            const vedit::syntax::Range window{lineStart(text, 20), lineStart(text, 60), 0, 0};
            std::vector<double> rangeRuns;
            rangeRuns.reserve(50);
            for (int run = 0; run < 50; ++run) {
                const auto started = std::chrono::steady_clock::now();
                probe->highlightSpans(tree, window);
                rangeRuns.push_back(static_cast<double>(std::chrono::duration_cast<
                                                            std::chrono::microseconds>(
                                                            std::chrono::steady_clock::now() - started)
                                                            .count())
                                    / 1000.0);
            }
            rangeQueryMs = percentile(rangeRuns, 0.5);
        }

        // (b) the T17 measurement: 3 batches x 50 rebuilds on the visible window.
        std::uint32_t windowStart = lineStart(controller.text(), 20);
        std::uint32_t windowEnd = lineStart(controller.text(), 60);
        controller.injectWindow(windowStart, windowEnd);
        std::vector<double> batches;
        batches.reserve(3);
        for (int batch = 0; batch < 3; ++batch) {
            std::string reopenError;
            controller.open(text, &reopenError);
            windowStart = lineStart(controller.text(), 20);
            windowEnd = lineStart(controller.text(), 60);
            controller.injectWindow(windowStart, windowEnd);
            long long rebuildTotalUs = 0;
            for (int round = 0; round < 50; ++round) {
                Edit edit;
                edit.start_byte = std::min<std::uint32_t>(
                    windowStart + 4, static_cast<std::uint32_t>(controller.text().size()));
                edit.old_end_byte = edit.start_byte;
                edit.new_text = (round % 2 == 0) ? "x" : "";
                if (round % 2 == 1) {
                    edit.start_byte = std::max<std::uint32_t>(edit.start_byte - 1, 0);
                    edit.old_end_byte = edit.start_byte + 1;
                }
                controller.applyEdit(edit, windowStart, windowEnd);
                rebuildTotalUs += controller.lastTiming().rebuild_us;
            }
            batches.push_back(static_cast<double>(rebuildTotalUs) / 1000.0);
        }
        const double rebuildMs = percentile(batches, 0.50);

        // (c) injection p95 over 100 visible windows (same method as T17).
        std::vector<double> injections;
        injections.reserve(100);
        for (int round = 0; round < 100; ++round) {
            const std::uint32_t start =
                lineStart(controller.text(), static_cast<std::size_t>(round % 200));
            const std::uint32_t end = std::min<std::uint32_t>(
                lineStart(controller.text(), static_cast<std::size_t>(round % 200 + 40)),
                static_cast<std::uint32_t>(controller.text().size()));
            controller.injectWindow(start, end);
            injections.push_back(static_cast<double>(controller.lastTiming().injection_us) / 1000.0);
        }
        const double injectionP95 = percentile(injections, 0.95);

        std::printf("PERF-SWEEP lines=%zu bytes=%zu full_query_ms=%.3f range_query_ms=%.4f "
                    "rebuild50_ms=%.2f injection_p95_ms=%.3f sync_us=%.1f query_us=%.1f "
                    "spans_full=%zu spans_window=%zu span_cache_kb=%zu rss_kb=%ld\n",
                    lines, static_cast<std::size_t>(size), fullQueryMs, rangeQueryMs, rebuildMs,
                    injectionP95, static_cast<double>(controller.lastTiming().sync_us),
                    static_cast<double>(controller.lastTiming().query_us), fullSpans,
                    controller.spans().size(), fullSpans * sizeof(HighlightSpan) / 1024,
                    processRssKb());
        std::fflush(stdout);

        const std::string label = std::to_string(lines);
        check(("overlay.perf-sweep." + label + ".rebuild-50").c_str(), rebuildMs <= 150.0,
              "50 rebuilds median=" + std::to_string(rebuildMs) + " ms (budget 150)");
        check(("overlay.perf-sweep." + label + ".injection-p95").c_str(), injectionP95 <= 2.0,
              "p95=" + std::to_string(injectionP95) + " ms (budget 2)");
        // The range query must stay window-bounded: a whole-tree query at this
        // size is already far above this bound (see full_query_ms).
        check(("overlay.perf-sweep." + label + ".range-query-bounded").c_str(),
              tree != nullptr && rangeQueryMs >= 0.0 && rangeQueryMs < 1.0,
              "range_query_median=" + std::to_string(rangeQueryMs) + " ms");
    }
}

} // namespace

int main(int argc, char **argv)
{
    bool selfcheck = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--selfcheck") == 0) {
            selfcheck = true;
        }
    }
    if (!selfcheck) {
        std::printf("usage: %s --selfcheck\n", argv[0]);
        return 2;
    }

    std::printf("SYN-OVERLAY-SELFTEST start\n");
#if defined(VEDIT_OVERLAY_HAVE_GRAMMARS)
    const std::size_t registered = vedit::syntax::grammars::registerBuiltinGrammars();
    std::printf("grammars: registered=%zu ids=", registered);
    for (const std::string &id : vedit::syntax::GrammarRegistry::instance().ids()) {
        std::printf("%s ", id.c_str());
    }
    std::printf("\n");
    SyntaxService::instance().resetForTests();
    const bool activated = SyntaxService::instance().activate("verilog");
    check("overlay.provider.grammar-activated", activated, "verilog");
#else
    skip("overlay.provider.grammar-activated", "built without vedit_syntax_grammars (standalone lane build)");
#endif

    checkSingleOwner();
    checkPaintPlan();
    checkFallback();
    checkServiceSwitch();
    checkIncrementalEquivalence();
    checkWindowIsolation();
    checkGuards();
    checkPerformance();
    checkPerformanceSweep();

    OwnerArbiter::instance().clear();
    std::printf("SYN-OVERLAY-SELFTEST ok=%d checks=%d failures=%d skipped=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures, g_skips);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
