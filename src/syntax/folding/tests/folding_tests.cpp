// Acceptance test of the folding lane (task T19, AC-S2.2 / AMENDMENTS A1-A3).
//
// It runs against a REAL KTextEditor::View in the pinned Qt6/KF6 image (headless,
// Xvfb or the offscreen platform) and a REAL tree-sitter provider from the syntax
// stack, and it asserts only what AMENDMENTS A1/A2/A3 allow:
//
//   A1: invalid ranges (empty / inverted / out-of-document / zero-line) never
//       fold anything, and never touch the user's selection; a negative control
//       shows why: the `fold` command with an invalid range falls back to the
//       selection, and an inverted range returns true without folding anything;
//   A2: full_count is 0 on the key path (200 characters), exactly 1 per allowed
//       rebuild (tree-sitter toggle), the rebuild is <= 150 ms and one injection
//       has p95 <= 2 ms;
//   A1f/A3: the tree-sitter on/off states are observed ONLY through
//       firstDisplayedLine(RealLine/VisibleLine) and scroll_max, against an
//       expectation computed independently from the sample's construction (never
//       from the injector's registry).
//
// Output: CHECK lines (machine readable), SNAPSHOT/MEASURE/A1F lines with the
// numbers, and one summary line. Exit code 0 iff every check passed.
#include "fold_injector.h"
#include "fold_range.h"
#include "fold_source.h"
#include "ksyntax_fold_source.h"
#include "syntax_fold_source.h"
#include "syntax_service.h"

#include <KTextEditor/Command>
#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QScrollBar>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using vedit::folding::FoldInjector;
using vedit::folding::observeView;
using vedit::folding::FoldRange;
using vedit::folding::RebuildTrigger;
using vedit::folding::StaticFoldSource;
using vedit::folding::SyntaxFoldSource;
using vedit::folding::ViewObservation;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skips = 0;

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

void checkInt(const char *name, long long actual, long long expected)
{
    const bool ok = actual == expected;
    std::printf("CHECK %s %s actual=%lld expected=%lld\n", name, ok ? "PASS" : "FAIL", actual,
                expected);
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::fflush(stdout);
}

void skip(const char *name, const char *why)
{
    ++g_skips;
    std::printf("CHECK %s SKIP (%s)\n", name, why);
    std::fflush(stdout);
}

void pump()
{
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

// A 40-line SystemVerilog module. The plan below is the INDEPENDENT ground truth
// of the A1f expectation: a module covers `kModuleLines` lines, keeps its first
// line visible and hides the other 39 (A3.2: independent computation, never the
// registry).
constexpr int kModuleLines = 40;
constexpr int kModuleCount = 25; // => 1000 lines

QString moduleText(int index)
{
    const QString n = QString::number(index);
    QString block;
    block += QStringLiteral("module m%1 #(parameter W = 8) (\n").arg(n);           // 0
    block += QStringLiteral("  input  logic clk,\n");                               // 1
    block += QStringLiteral("  input  logic rst_n,\n");                             // 2
    block += QStringLiteral("  output logic [W-1:0] q\n");                          // 3
    block += QStringLiteral(");\n");                                                // 4
    block += QStringLiteral("  logic [W-1:0] state, nxt;\n");                       // 5
    block += QStringLiteral("  function automatic logic [W-1:0] inc(input logic [W-1:0] v);\n"); // 6
    block += QStringLiteral("    inc = v + 1'b1;\n");                               // 7
    block += QStringLiteral("  endfunction\n");                                     // 8
    block += QStringLiteral("  always_ff @(posedge clk or negedge rst_n) begin\n"); // 9
    block += QStringLiteral("    if (!rst_n) begin\n");                             // 10
    block += QStringLiteral("      state <= '0;\n");                                // 11
    block += QStringLiteral("    end else begin\n");                                // 12
    block += QStringLiteral("      state <= inc(nxt);\n");                          // 13
    block += QStringLiteral("    end\n");                                           // 14
    block += QStringLiteral("  end\n");                                             // 15
    block += QStringLiteral("  always_comb begin\n");                               // 16
    block += QStringLiteral("    nxt = state + 1'b1;\n");                           // 17
    block += QStringLiteral("  end\n");                                             // 18
    block += QStringLiteral("  generate\n");                                        // 19
    block += QStringLiteral("    for (genvar i = 0; i < W; i = i + 1) begin : g_lane\n"); // 20
    block += QStringLiteral("      assign q[i] = state[i];\n");                     // 21
    block += QStringLiteral("    end\n");                                           // 22
    block += QStringLiteral("  endgenerate\n");                                     // 23
    for (int line = 24; line < kModuleLines - 1; ++line) {
        block += QStringLiteral("  // filler line %1 of module %2\n").arg(line).arg(n);
    }
    block += QStringLiteral("endmodule\n");                                         // 39
    return block;
}

QString systemVerilogSample()
{
    QString text;
    for (int i = 0; i < kModuleCount; ++i) {
        text += moduleText(i);
    }
    return text;
}

struct Fixture {
    KTextEditor::Document *document = nullptr;
    KTextEditor::View *view = nullptr;
};

Fixture makeFixture(KTextEditor::Editor *editor, const QString &text, int width = 1200,
                    int height = 900)
{
    Fixture fixture;
    fixture.document = editor->createDocument(nullptr);
    fixture.document->setText(text);
    fixture.view = fixture.document->createView(nullptr);
    fixture.view->resize(width, height);
    fixture.view->show();
    pump();
    return fixture;
}

std::string snapshotLine(const char *label, const ViewObservation &observation)
{
    char buffer[256];
    std::snprintf(buffer, sizeof buffer,
                  "SNAPSHOT %s first_real=%d last_real=%d first_visible=%d last_visible=%d "
                  "hidden_in_viewport=%d scroll_max=%d cursor_line=%d",
                  label, observation.firstDisplayedRealLine, observation.lastDisplayedRealLine,
                  observation.firstDisplayedVisibleLine, observation.lastDisplayedVisibleLine,
                  observation.hiddenInViewport, observation.scrollMax, observation.cursorLine);
    return buffer;
}

void printSnapshot(const char *label, const ViewObservation &observation)
{
    std::printf("%s\n", snapshotLine(label, observation).c_str());
}

double percentile95(std::vector<double> samples)
{
    if (samples.empty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const double index = 0.95 * static_cast<double>(samples.size() - 1);
    const std::size_t low = static_cast<std::size_t>(std::floor(index));
    const std::size_t high = static_cast<std::size_t>(std::ceil(index));
    if (low == high) {
        return samples[low];
    }
    return samples[low] + (samples[high] - samples[low]) * (index - static_cast<double>(low));
}

// ---------------------------------------------------------------------------
// A1: the model-level checks (no editor needed) - all four A1.1 conditions.
// ---------------------------------------------------------------------------
void testRangeValidation()
{
    using vedit::folding::RejectReason;
    using vedit::folding::validateFoldRange;
    check("a1.valid.accepted", validateFoldRange(FoldRange{3, 9}, 40) == RejectReason::None);
    check("a1.invalid.rejected", validateFoldRange(FoldRange{-1, 5}, 40) == RejectReason::Invalid);
    check("a1.invalid.end-rejected", validateFoldRange(FoldRange{1, -3}, 40) == RejectReason::Invalid);
    check("a1.inverted.rejected", validateFoldRange(FoldRange{9, 3}, 40) == RejectReason::Inverted);
    check("a1.out-of-document.start",
          validateFoldRange(FoldRange{40, 45}, 40) == RejectReason::OutOfDocument);
    check("a1.out-of-document.end",
          validateFoldRange(FoldRange{30, 40}, 40) == RejectReason::OutOfDocument);
    check("a1.zero-lines.rejected", validateFoldRange(FoldRange{10, 10}, 40) == RejectReason::ZeroLines);

    const vedit::folding::Validation validation = vedit::folding::validateFoldRanges(
        {FoldRange{3, 9}, FoldRange{9, 3}}, 40, vedit::folding::SelectionRange{20, 25});
    checkInt("a1.batch.accepted", static_cast<long long>(validation.accepted.size()), 1);
    checkInt("a1.batch.rejected", static_cast<long long>(validation.rejected.size()), 1);
    check("a1.batch.rejection-carries-selection",
          validation.rejected.size() == 1
              && validation.rejected[0].detail.find("selection=[20,25]") != std::string::npos);
}

// ---------------------------------------------------------------------------
// A1.3/A1.4: with a NON-EMPTY selection (lines 20-25), the product path must not
// fold anything for the three invalid inputs, and its observations must not move.
// ---------------------------------------------------------------------------
void testInvalidRangesDoNotFoldSelection(KTextEditor::Editor *editor)
{
    Fixture fixture = makeFixture(editor, systemVerilogSample());
    KTextEditor::View *view = fixture.view;
    view->setCursorPosition(KTextEditor::Cursor(0, 0));
    const ViewObservation baseline = observeView(view);
    printSnapshot("a1.baseline", baseline);

    view->setSelection(KTextEditor::Range(20, 0, 25, 0));
    pump();
    const KTextEditor::Range selection = view->selectionRange();
    check("a1.selection.non-empty",
          selection.isValid() && selection.start().line() != selection.end().line());
    std::printf("SELECTION start_line=%d end_line=%d\n", selection.start().line(),
                selection.end().line());

    FoldInjector injector(view);
    const int injected = injector.inject({
        FoldRange{-1, -1},  // empty / default-constructed range
        FoldRange{30, 20},  // inverted
        FoldRange{5000, 5100}, // out of the document
        FoldRange{10, 10},  // zero lines
    });
    pump();
    checkInt("a1.product.injected-none", injected, 0);
    checkInt("a1.product.inject-count", static_cast<long long>(injector.injectCount()), 0);
    checkInt("a1.product.registry-empty", static_cast<long long>(injector.registry().size()), 0);
    checkInt("a1.product.rejected-count", static_cast<long long>(injector.rejectedCount()), 4);

    const ViewObservation after = observeView(view);
    printSnapshot("a1.after-invalid", after);
    check("a1.product.observation-identical",
          after.firstDisplayedRealLine == baseline.firstDisplayedRealLine
              && after.firstDisplayedVisibleLine == baseline.firstDisplayedVisibleLine
              && after.lastDisplayedRealLine == baseline.lastDisplayedRealLine
              && after.lastDisplayedVisibleLine == baseline.lastDisplayedVisibleLine
              && after.scrollMax == baseline.scrollMax);
    check("a1.product.selection-unchanged",
          view->selectionRange().start().line() == selection.start().line()
              && view->selectionRange().end().line() == selection.end().line());

    const QString diagnostics = injector.diagnostics();
    std::printf("DIAGNOSTICS\n%s\n", diagnostics.toUtf8().constData());
    check("a1.product.diag-reason-invalid", diagnostics.contains(QStringLiteral("reason=invalid")));
    check("a1.product.diag-reason-inverted", diagnostics.contains(QStringLiteral("reason=inverted")));
    check("a1.product.diag-reason-out-of-document",
          diagnostics.contains(QStringLiteral("reason=out-of-document")));
    check("a1.product.diag-reason-zero-lines",
          diagnostics.contains(QStringLiteral("reason=zero-lines")));
    check("a1.product.diag-selection", diagnostics.contains(QStringLiteral("selection=[20,25]")));
}

// ---------------------------------------------------------------------------
// A1 negative control: WITHOUT the validation, the same inputs do something. It
// is what proves A1.2 ("the exec return value is not a success signal").
// ---------------------------------------------------------------------------
void testInvalidRangeHazard(KTextEditor::Editor *editor)
{
    KTextEditor::Command *fold = editor->queryCommand(QStringLiteral("fold"));
    if (fold == nullptr) {
        skip("a1.control.fold-command", "KTextEditor registers no fold command");
        return;
    }

    Fixture fixture = makeFixture(editor, systemVerilogSample());
    KTextEditor::View *view = fixture.view;
    view->setCursorPosition(KTextEditor::Cursor(0, 0));
    view->setSelection(KTextEditor::Range(20, 0, 25, 0));
    pump();
    const ViewObservation before = observeView(view);
    printSnapshot("a1.control.before", before);

    QString message;
    const bool invalidReturned =
        fold->exec(view, QStringLiteral("fold"), message, KTextEditor::Range::invalid());
    pump();
    const ViewObservation afterInvalid = observeView(view);
    printSnapshot("a1.control.after-invalid", afterInvalid);
    std::printf("A1-CONTROL invalid_range exec_returned=%d message='%s' scroll_max_before=%d "
                "scroll_max_after=%d hidden_before=%d hidden_after=%d\n",
                invalidReturned ? 1 : 0, message.toUtf8().constData(), before.scrollMax,
                afterInvalid.scrollMax, before.hiddenInViewport, afterInvalid.hiddenInViewport);
    // AMENDMENTS A1: an invalid range falls back to View::selectionRange(), so the
    // user's selection is folded. The product path must never reach this.
    check("a1.control.invalid-range-folds-selection",
          afterInvalid.scrollMax < before.scrollMax || afterInvalid.hiddenInViewport > before.hiddenInViewport);

    // An inverted range is NOT rejected by the command: it returns true and has
    // no visible effect. This is exactly why A1.2 forbids using the return value.
    Fixture inverted = makeFixture(editor, systemVerilogSample());
    inverted.view->setCursorPosition(KTextEditor::Cursor(0, 0));
    pump();
    const ViewObservation invertedBefore = observeView(inverted.view);
    const bool invertedReturned = fold->exec(inverted.view, QStringLiteral("fold"), message,
                                             KTextEditor::Range(30, 0, 20, 0));
    pump();
    const ViewObservation invertedAfter = observeView(inverted.view);
    std::printf("A1-CONTROL inverted_range exec_returned=%d scroll_max_before=%d scroll_max_after=%d\n",
                invertedReturned ? 1 : 0, invertedBefore.scrollMax, invertedAfter.scrollMax);
    // Measured here (and consistent with A1's rule, stricter than the probe note):
    // the command returns TRUE for an inverted range and KatePart still hides the
    // lines between the two lines, so the return value cannot be used as a check.
    check("a1.control.inverted.exec-returned-true", invertedReturned);
    check("a1.control.inverted.exec-has-effect",
          invertedAfter.scrollMax < invertedBefore.scrollMax
              || invertedAfter.hiddenInViewport > invertedBefore.hiddenInViewport);
}

// ---------------------------------------------------------------------------
// A2.4: counters on the key path, on a toggle, and the per-injection p95.
// ---------------------------------------------------------------------------
void testRebuildPolicyAndCounters(KTextEditor::Editor *editor)
{
    Fixture fixture = makeFixture(editor, systemVerilogSample());
    KTextEditor::Document *document = fixture.document;
    KTextEditor::View *view = fixture.view;
    const ViewObservation baseline = observeView(view);
    printSnapshot("a2.baseline", baseline);

    FoldInjector injector(view);
    SyntaxFoldSource source(QStringLiteral("systemverilog"));
    vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(true);

    const vedit::folding::RebuildOutcome first = injector.rebuildFrom(source, RebuildTrigger::FirstOpen);
    pump();
    std::printf("A2 first-open trigger=%s injected=%d rejected=%d elapsed_ms=%lld full_count=%llu "
                "inject_count=%llu source='%s'\n",
                vedit::folding::rebuildTriggerName(first.trigger), first.injected, first.rejected,
                static_cast<long long>(first.elapsedMs),
                static_cast<unsigned long long>(injector.fullCount()),
                static_cast<unsigned long long>(injector.injectCount()),
                source.lastDiagnostic().toUtf8().constData());
    check("a2.first-open.full-count-1", injector.fullCount() == 1);
    check("a2.first-open.at-least-20-folds", first.injected >= 20);
    check("a2.first-open.within-150ms", first.elapsedMs <= 150);
    const ViewObservation foldedObservation = observeView(view);
    printSnapshot("a2.after-first-open", foldedObservation);
    check("a2.first-open.observable",
          foldedObservation.scrollMax < baseline.scrollMax || foldedObservation.hiddenInViewport > 0);

    // (a) the key path: 200 characters typed, must not rebuild.
    const quint64 fullBefore = injector.fullCount();
    const std::size_t registryBefore = injector.registry().size();
    const int lastLine = document->lines() - 1;
    const int lastColumn = document->lineLength(lastLine);
    for (int i = 0; i < 200; ++i) {
        document->insertText(KTextEditor::Cursor(lastLine, lastColumn + i), QStringLiteral("x"));
        injector.onKeyPress();
    }
    pump();
    std::printf("A2 key-path chars=200 full_count_before=%llu full_count_after=%llu "
                "key_events=%llu\n",
                static_cast<unsigned long long>(fullBefore),
                static_cast<unsigned long long>(injector.fullCount()),
                static_cast<unsigned long long>(injector.keyEventCount()));
    checkInt("a2.key.full-count-delta", static_cast<long long>(injector.fullCount() - fullBefore), 0);
    checkInt("a2.key.events", static_cast<long long>(injector.keyEventCount()), 200);
    check("a2.key.registry-unchanged", injector.registry().size() == registryBefore);

    // (a2) cursor movement and scrolling must not rebuild either (A2.1 names
    // them explicitly); this is also the path a breadcrumb refresh uses.
    for (int line = 0; line < 50; ++line) {
        view->setCursorPosition(KTextEditor::Cursor(line, 0));
        injector.onCursorMoved();
    }
    injector.onScroll();
    pump();
    std::printf("A2 cursor-path moves=50 cursor_events=%llu full_count=%llu\n",
                static_cast<unsigned long long>(injector.cursorEventCount()),
                static_cast<unsigned long long>(injector.fullCount()));
    checkInt("a2.cursor.full-count-delta", static_cast<long long>(injector.fullCount() - fullBefore), 0);
    checkInt("a2.cursor.events", static_cast<long long>(injector.cursorEventCount()), 50);

    // (b) tree-sitter OFF: exactly one full rebuild, <= 150 ms, folds gone.
    vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(false);
    const vedit::folding::RebuildOutcome off =
        injector.rebuildFrom(source, RebuildTrigger::TreeSitterToggled);
    pump();
    std::printf("A2 toggle-off injected=%d elapsed_ms=%lld full_count=%llu tree_sitter_active=%d\n",
                off.injected, static_cast<long long>(off.elapsedMs),
                static_cast<unsigned long long>(injector.fullCount()),
                source.treeSitterActive() ? 1 : 0);
    checkInt("a2.toggle.full-count-delta", static_cast<long long>(injector.fullCount() - fullBefore), 1);
    check("a2.toggle.within-150ms", off.elapsedMs <= 150);
    checkInt("a2.toggle.injected-none", off.injected, 0);
    check("a2.toggle.source-inactive", !source.treeSitterActive());
    const ViewObservation offObservation = observeView(view);
    printSnapshot("a2.after-toggle-off", offObservation);
    check("a2.toggle.folds-gone", offObservation.hiddenInViewport == 0);

    // (c) tree-sitter back ON: one more full rebuild (the A1f on state).
    vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(true);
    const vedit::folding::RebuildOutcome back =
        injector.rebuildFrom(source, RebuildTrigger::TreeSitterToggled);
    pump();
    std::printf("A2 toggle-back injected=%d elapsed_ms=%lld full_count=%llu\n", back.injected,
                static_cast<long long>(back.elapsedMs),
                static_cast<unsigned long long>(injector.fullCount()));
    checkInt("a2.toggle-back.full-count-delta", static_cast<long long>(injector.fullCount() - fullBefore), 2);
    check("a2.toggle-back.folds-back", observeView(view).hiddenInViewport > 0);

    // (d) single injection p95 over 50 injections on a fresh view.
    Fixture p95Fixture = makeFixture(editor, systemVerilogSample());
    FoldInjector p95Injector(p95Fixture.view);
    std::vector<double> samples;
    for (int i = 0; i < 50; ++i) {
        const FoldRange range{i * 18, i * 18 + 9};
        QElapsedTimer timer;
        timer.start();
        p95Injector.inject({range});
        samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1000.0);
    }
    pump();
    const double p95 = percentile95(samples);
    std::printf("MEASURE fold_inject n=50 p95_us=%.2f\n", p95);
    check("a2.inject.p95-le-2ms", p95 <= 2000.0);
    checkInt("a2.inject.count", static_cast<long long>(p95Injector.injectCount()), 50);
}

// ---------------------------------------------------------------------------
// A1f / A3: on/off observed through firstDisplayedLine and scroll_max only,
// against the expectation derived from the sample's construction.
// ---------------------------------------------------------------------------
void testTreeSitterToggleObservable(KTextEditor::Editor *editor)
{
    // The plan: 25 modules of 40 lines; a module fold keeps its first line and
    // hides the other 39. Nested folds (function/always/generate) are inside the
    // module fold, so the union of hidden lines is 25 * 39, independent of the
    // registry and of the provider (A3.2).
    const long long expectedHidden = static_cast<long long>(kModuleCount) * (kModuleLines - 1);

    Fixture fixture = makeFixture(editor, systemVerilogSample());
    KTextEditor::View *view = fixture.view;
    view->setCursorPosition(KTextEditor::Cursor(0, 0));
    const ViewObservation baseline = observeView(view);
    printSnapshot("a1f.baseline", baseline);

    FoldInjector injector(view);
    SyntaxFoldSource source(QStringLiteral("systemverilog"));
    vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(true);
    const vedit::folding::RebuildOutcome on = injector.rebuildFrom(source, RebuildTrigger::FirstOpen);
    pump();
    const ViewObservation onObservation = observeView(view);
    printSnapshot("a1f.on", onObservation);
    const long long onDrop = static_cast<long long>(baseline.scrollMax) - onObservation.scrollMax;
    // scroll_max is (content lines - viewport rows), so its drop saturates at the
    // baseline maximum: expected drop = min(hidden lines, baseline scroll_max).
    // The exact hidden count is asserted through the firstDisplayedLine method.
    const long long expectedDrop = std::min<long long>(expectedHidden, baseline.scrollMax);
    std::printf("A1F state=on tree_sitter_active=%d expected_hidden=%lld expected_scroll_drop=%lld "
                "observed_scroll_drop=%lld hidden_in_viewport=%d ranges=%d\n",
                source.treeSitterActive() ? 1 : 0, expectedHidden, expectedDrop, onDrop,
                onObservation.hiddenInViewport, on.injected);
    check("a1f.on.source-active", source.treeSitterActive());
    check("a1f.on.positive-control", expectedHidden > 0);
    checkInt("a1f.on.scroll-drop-equals-expected", onDrop, expectedDrop);
    checkInt("a1f.on.first-displayed-hidden-equals-expected", onObservation.hiddenInViewport,
             static_cast<long long>(expectedHidden));

    vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(false);
    const vedit::folding::RebuildOutcome off =
        injector.rebuildFrom(source, RebuildTrigger::TreeSitterToggled);
    pump();
    const ViewObservation offObservation = observeView(view);
    printSnapshot("a1f.off", offObservation);
    const long long offDrop = static_cast<long long>(baseline.scrollMax) - offObservation.scrollMax;
    std::printf("A1F state=off tree_sitter_active=%d expected_hidden=0 observed_scroll_drop=%lld "
                "hidden_in_viewport=%d ranges=%d\n",
                source.treeSitterActive() ? 1 : 0, offDrop, offObservation.hiddenInViewport,
                off.injected);
    check("a1f.off.source-inactive", !source.treeSitterActive());
    checkInt("a1f.off.scroll-drop-zero", offDrop, 0);
    checkInt("a1f.off.hidden-in-viewport-zero", offObservation.hiddenInViewport, 0);
    checkInt("a1f.off.injected-none", off.injected, 0);

    // A3.1 method 1 with a positive control: fold exactly one known block and
    // assert the hidden line count inside the viewport, not just "it changed".
    Fixture small = makeFixture(editor, moduleText(0) + moduleText(1));
    small.view->setCursorPosition(KTextEditor::Cursor(0, 0));
    const ViewObservation smallBaseline = observeView(small.view);
    printSnapshot("a1f.small-baseline", smallBaseline);
    FoldInjector smallInjector(small.view);
    const int injected = smallInjector.inject({FoldRange{0, kModuleLines - 1}});
    pump();
    const ViewObservation smallFolded = observeView(small.view);
    printSnapshot("a1f.small-folded", smallFolded);
    const long long expectedSmallDrop =
        std::min<long long>(kModuleLines - 1, smallBaseline.scrollMax);
    std::printf("A1F method1 expected_hidden=%d expected_scroll_drop=%lld observed_hidden_in_viewport=%d observed_scroll_drop=%lld\n",
                kModuleLines - 1, expectedSmallDrop, smallFolded.hiddenInViewport,
                static_cast<long long>(smallBaseline.scrollMax) - smallFolded.scrollMax);
    checkInt("a1f.method1.injected-one", injected, 1);
    checkInt("a1f.method1.hidden-in-viewport", smallFolded.hiddenInViewport, kModuleLines - 1);
    checkInt("a1f.method1.scroll-drop",
             static_cast<long long>(smallBaseline.scrollMax) - smallFolded.scrollMax, expectedSmallDrop);
}

// ---------------------------------------------------------------------------
// A4 / AC-S3.5 remainder: a 1k-line .sv must still fold with tree-sitter OFF,
// using OUR OWN definition's beginRegion rules. The expected ranges below are
// derived from the sample's construction + the XML rule (module/function/begin/
// generate begin-regions), never from the injector registry; the injector's A1
// validation stays in the path (an invalid range is still refused).
// ---------------------------------------------------------------------------
void testA4ClosedStateFolds(KTextEditor::Editor *editor)
{
#if !defined(VEDIT_A4_SYSTEMVERILOG_XML)
    skip("a4.source.loaded", "VEDIT_A4_SYSTEMVERILOG_XML is not defined");
#else
    vedit::folding::KsyntaxFoldSource source(QStringLiteral(VEDIT_A4_SYSTEMVERILOG_XML));
    std::printf("A4 source=%s path=%s diag=%s\n", qPrintable(source.name()),
                qPrintable(source.definitionPath()), qPrintable(source.lastDiagnostic()));
    check("a4.source.loaded", source.isLoaded());
    check("a4.source.identity-is-ours",
          source.definitionName() == QStringLiteral("SystemVerilog")
              && source.definitionSection() == QStringLiteral("Hardware")
              && source.definitionAuthor() == QStringLiteral("VEdit"));
    check("a4.source.region-veditfold", source.regionName() == QStringLiteral("veditFold"));
    const QStringList expectedBegin{QStringLiteral("module"), QStringLiteral("interface"),
                                    QStringLiteral("program"), QStringLiteral("class"),
                                    QStringLiteral("package"), QStringLiteral("generate"),
                                    QStringLiteral("function"), QStringLiteral("task"),
                                    QStringLiteral("case"), QStringLiteral("casex"),
                                    QStringLiteral("casez"), QStringLiteral("randcase"),
                                    QStringLiteral("begin")};
    const QStringList expectedEnd{QStringLiteral("endmodule"), QStringLiteral("endinterface"),
                                  QStringLiteral("endprogram"), QStringLiteral("endclass"),
                                  QStringLiteral("endpackage"), QStringLiteral("endgenerate"),
                                  QStringLiteral("endfunction"), QStringLiteral("endtask"),
                                  QStringLiteral("endcase"), QStringLiteral("end")};
    check("a4.source.begin-list-from-xml", source.beginKeywords() == expectedBegin);
    check("a4.source.end-list-from-xml", source.endKeywords() == expectedEnd);
    check("a4.source.tree-sitter-off", !source.treeSitterActive());

    // The A4 premise: the runtime tree-sitter switch is OFF.
    vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(false);
    check("a4.tree-sitter-switch-off", !vedit::syntax::SyntaxService::instance().treeSitterEnabled());

    const QString sample = systemVerilogSample();
    checkInt("a4.sample.newlines", static_cast<long long>(sample.count(QLatin1Char('\n'))),
             kModuleCount * kModuleLines);
    const std::vector<FoldRange> ranges = source.ranges(sample);
    std::printf("A4 expected_source=%s region=%s ranges=%d unbalanced=%d\n",
                qPrintable(source.definitionPath()), qPrintable(source.regionName()),
                source.lastRangeCount(), source.lastUnbalancedCount());
    checkInt("a4.ranges.from-xml", static_cast<long long>(ranges.size()), kModuleCount * 8);
    checkInt("a4.ranges.unbalanced", source.lastUnbalancedCount(), 0);
    std::printf("A4 ranges total=%d first-module:", source.lastRangeCount());
    for (const FoldRange &range : ranges) {
        if (range.startLine < kModuleLines) {
            std::printf(" (%d,%d)", range.startLine, range.endLine);
        }
    }
    std::printf("\n");

    // Independent expectation for one 40-line module (offsets from moduleText()):
    //   (0,39)  module ... endmodule      (6,8)   function ... endfunction
    //   (9,15)  always_ff begin ... end   (10,12) if begin ... end
    //   (12,14) end else begin ... end    (16,18) always_comb begin ... end
    //   (19,23) generate ... endgenerate  (20,22) for ... begin ... end
    std::vector<FoldRange> expected;
    for (int i = 0; i < kModuleCount; ++i) {
        const int base = i * kModuleLines;
        expected.push_back(FoldRange{base + 0, base + 39});
        expected.push_back(FoldRange{base + 6, base + 8});
        expected.push_back(FoldRange{base + 9, base + 15});
        expected.push_back(FoldRange{base + 10, base + 12});
        expected.push_back(FoldRange{base + 12, base + 14});
        expected.push_back(FoldRange{base + 16, base + 18});
        expected.push_back(FoldRange{base + 19, base + 23});
        expected.push_back(FoldRange{base + 20, base + 22});
    }
    const auto sorted = [](std::vector<FoldRange> value) {
        std::sort(value.begin(), value.end(), [](const FoldRange &a, const FoldRange &b) {
            if (a.startLine != b.startLine) {
                return a.startLine < b.startLine;
            }
            return a.endLine < b.endLine;
        });
        return value;
    };
    check("a4.ranges.match-construction", sorted(ranges) == sorted(expected));
    bool allValid = true;
    for (const FoldRange &range : ranges) {
        if (validateFoldRange(range, kModuleCount * kModuleLines)
            != vedit::folding::RejectReason::None) {
            allValid = false;
        }
    }
    check("a4.ranges.all-valid", allValid);

    // A3.1 observation on the REAL view (firstDisplayedLine Real/Visible +
    // scroll_max), with the expectation derived from the construction: the
    // 25 module folds hide 39 lines each, i.e. 975 lines.
    Fixture fixture = makeFixture(editor, sample);
    fixture.view->setCursorPosition(KTextEditor::Cursor(0, 0));
    const ViewObservation baseline = observeView(fixture.view);
    printSnapshot("a4.baseline", baseline);
    FoldInjector injector(fixture.view);
    const vedit::folding::RebuildOutcome rebuild =
        injector.rebuildFrom(source, RebuildTrigger::FirstOpen);
    pump();
    const ViewObservation folded = observeView(fixture.view);
    printSnapshot("a4.folded", folded);
    const long long expectedHidden = static_cast<long long>(kModuleCount) * (kModuleLines - 1);
    const long long expectedDrop = std::min<long long>(expectedHidden, baseline.scrollMax);
    const long long observedDrop = static_cast<long long>(baseline.scrollMax) - folded.scrollMax;
    std::printf("A4 closed_state tree_sitter_active=%d trigger=%s injected=%d rejected=%d "
                "expected_hidden=%lld observed_hidden=%d scroll_drop=%lld expected_scroll_drop=%lld\n",
                vedit::syntax::SyntaxService::instance().treeSitterEnabled() ? 1 : 0,
                vedit::folding::rebuildTriggerName(rebuild.trigger), rebuild.injected,
                rebuild.rejected,
                expectedHidden, folded.hiddenInViewport, observedDrop, expectedDrop);
    checkInt("a4.rebuild.trigger-first-open",
             static_cast<long long>(rebuild.trigger), static_cast<long long>(RebuildTrigger::FirstOpen));
    checkInt("a4.inject.from-xml", rebuild.injected, kModuleCount * 8);
    checkInt("a4.inject.rejected", rebuild.rejected, 0);
    check("a4.observed.folds-yes", folded.hiddenInViewport > 0);
    checkInt("a4.observed.hidden-equals-expected", folded.hiddenInViewport, expectedHidden);
    checkInt("a4.observed.scroll-drop-equals-expected", observedDrop, expectedDrop);
    checkInt("a4.full-count-one", static_cast<long long>(injector.fullCount()), 1);

    // A1 must not be bypassed by the closed-state path: an invalid range still
    // never reaches the `fold` command and is counted as rejected.
    const unsigned long long rejectsBefore = injector.rejectedCount();
    checkInt("a4.invalid.still-rejected", injector.inject({FoldRange{5, 5}}), 0);
    check("a4.invalid.rejected-counted", injector.rejectedCount() == rejectsBefore + 1);
#endif
}

// F-4 (T30): block comments must not contribute structure. The scanner already
// skips `//` and strings; this test pins the same rule for `/* ... */`, first on
// the pre-fix implementation (it produces an extra range), then after the fix.
void testBlockCommentsDoNotCreateFolds()
{
#if !defined(VEDIT_A4_SYSTEMVERILOG_XML)
    skip("blockcomment.source.loaded", "VEDIT_A4_SYSTEMVERILOG_XML is not defined");
#else
    vedit::folding::KsyntaxFoldSource source(QStringLiteral(VEDIT_A4_SYSTEMVERILOG_XML));
    check("blockcomment.source.loaded", source.isLoaded());

    const QString sample = QStringLiteral(
        "module top;\n"
        "/*\n"
        "module fake;\n"
        "endmodule\n"
        "*/\n"
        "endmodule\n");
    const std::vector<FoldRange> ranges = source.ranges(sample);
    std::printf("A4 BLOCKCOMMENT ranges=%d unbalanced=%d", static_cast<int>(ranges.size()),
                source.lastUnbalancedCount());
    for (const FoldRange &range : ranges) {
        std::printf(" (%d,%d)", range.startLine, range.endLine);
    }
    std::printf("\n");
    checkInt("blockcomment.multiline.no-extra-range", static_cast<long long>(ranges.size()), 1);
    checkInt("blockcomment.multiline.unbalanced", source.lastUnbalancedCount(), 0);
    if (ranges.size() == 1) {
        checkInt("blockcomment.multiline.range.start", ranges.front().startLine, 0);
        checkInt("blockcomment.multiline.range.end", ranges.front().endLine, 5);
    }

    // A begin keyword inside a one-line block comment must not leave the stack
    // unbalanced either (pre-fix: unbalanced=1).
    const QString inlineSample = QStringLiteral(
        "module q;\n"
        "/* module x; */\n"
        "endmodule\n");
    const std::vector<FoldRange> inlineRanges = source.ranges(inlineSample);
    checkInt("blockcomment.inline.no-extra-range", static_cast<long long>(inlineRanges.size()), 1);
    checkInt("blockcomment.inline.unbalanced", source.lastUnbalancedCount(), 0);
#endif
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        std::printf("FOLDING-TESTS ok=0 failures=1 (no KTextEditor editor instance)\n");
        return 1;
    }

    testRangeValidation();
    testInvalidRangesDoNotFoldSelection(editor);
    testInvalidRangeHazard(editor);
    testRebuildPolicyAndCounters(editor);
    testTreeSitterToggleObservable(editor);
    testA4ClosedStateFolds(editor);
    testBlockCommentsDoNotCreateFolds();

    std::printf("FOLDING-TESTS ok=%d checks=%d failures=%d skipped=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures, g_skips);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
