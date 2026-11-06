// Acceptance test of the clickable breadcrumb bar (AC-7.6).
//
// The path is the REAL semantic path of the breadcrumb lane: the syntax stack's
// structure query over a SystemVerilog fixture (the same source the pane's text
// breadcrumb uses), never a hand-made entry list. The bar is then clicked with
// a real left-button mouse click (QTest), and the assertion is what the shared
// jump registry received: the container's file, its 1-based start line and
// column 1.
//
// Also proved:
//   * a click with NO registered target is an explicit no-op (NoTarget) - the
//     bar never crashes a stand-alone panel;
//   * the target's refusal surfaces as Rejected, not as a silent success;
//   * an empty path hides the bar and an out-of-range activation does nothing.
#include "breadcrumb_bar.h"
#include "syntax_breadcrumb_source.h"

#include "jump/jump_registry.h"

#include <QApplication>
#include <QEvent>
#include <QString>
#include <QTest>
#include <QWidget>

#include <cstdio>
#include <vector>

using vedit::breadcrumb::Bar;
using vedit::breadcrumb::Entry;
using vedit::breadcrumb::SyntaxBreadcrumbSource;
using vedit::workspace::jump::JumpStatus;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

void checkLong(const char *name, long long actual, long long expected)
{
    const bool ok = actual == expected;
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s actual=%lld expected=%lld\n", name, ok ? "PASS" : "FAIL", actual,
                expected);
    std::fflush(stdout);
}

void checkString(const char *name, const QString &actual, const QString &expected)
{
    const bool ok = actual == expected;
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s actual='%s' expected='%s'\n", name, ok ? "PASS" : "FAIL",
                actual.toUtf8().constData(), expected.toUtf8().constData());
    std::fflush(stdout);
}

// Records what the registry delivered (the row a real editor target would open).
struct Delivery {
    QString filePath;
    int line = 0;
    int column = 0;
};

class RecordingTarget final : public vedit::workspace::jump::IJumpTarget {
public:
    bool jumpTo(const QString &filePath, int line, int column) override
    {
        deliveries.push_back(Delivery{filePath, line, column});
        return accept;
    }

    std::vector<Delivery> deliveries;
    bool accept = true;
};

// Same fixture as the lane's semantic test: module m0 at line 0, function inc
// at line 3 (0-based, as the structure query reports it).
QString sampleText()
{
    return QStringLiteral(
        "module m0 #(parameter W = 8) (\n"                 // 0
        "  input logic clk\n"                              // 1
        ");\n"                                             // 2
        "  function automatic logic inc(input logic v);\n" // 3
        "    inc = v + 1'b1;\n"                            // 4
        "  endfunction\n"                                  // 5
        "  always_comb begin\n"                            // 6
        "    inc(1'b0);\n"                                 // 7
        "  end\n"                                          // 8
        "endmodule\n");                                    // 9
}

void printDelivery(const char *prefix, int index, const Bar &bar, const RecordingTarget &target,
                   const Delivery &delivery)
{
    std::printf("%s segment=%d text='%s' line=%d status=%s target-file='%s' target-line=%d "
                "target-column=%d target-calls=%zu\n",
                prefix, index, bar.segmentText(index).toUtf8().constData(),
                bar.segmentLine(index),
                vedit::workspace::jump::jumpStatusName(bar.lastStatus()),
                delivery.filePath.toUtf8().constData(), delivery.line, delivery.column,
                target.deliveries.size());
    std::fflush(stdout);
}

void clickSegment(Bar &bar, int index)
{
    QWidget *segment = bar.segmentWidget(index);
    if (segment != nullptr) {
        QTest::mouseClick(segment, Qt::LeftButton, Qt::NoModifier,
                          segment->rect().center());
        QCoreApplication::processEvents();
    }
}

int runAcceptance()
{
    vedit::workspace::jump::Registry::instance().reset();

    // --- the real semantic path ------------------------------------------
    SyntaxBreadcrumbSource source(QStringLiteral("systemverilog"));
    check("source.semantic-active", source.semanticActive());
    const QString text = sampleText();
    const std::vector<Entry> path = source.pathAt(text, 4); // cursor in inc's body
    checkLong("path.depth", static_cast<long long>(path.size()), 2);
    if (path.size() != 2) {
        return g_failures;
    }
    checkString("path.segment0", QString::fromStdString(path[0].name), QStringLiteral("m0"));
    checkString("path.segment1", QString::fromStdString(path[1].name), QStringLiteral("inc"));
    checkLong("path.segment1.source-line-0-based", path[1].line, 3);

    // --- the bar renders the path and is rebuilt on a semantic update -----
    Bar bar;
    bar.resize(420, 28);
    bar.setPath(QStringLiteral("/tmp/vedit-fixture/top.sv"), path);
    bar.show();
    QCoreApplication::processEvents();
    checkLong("bar.segments", bar.segmentCount(), 2);
    checkString("bar.segment0.text", bar.segmentText(0), QStringLiteral("m0"));
    checkString("bar.segment1.text", bar.segmentText(1), QStringLiteral("inc"));
    checkLong("bar.segment0.contract-line", bar.segmentLine(0), 1);
    checkLong("bar.segment1.contract-line", bar.segmentLine(1), 4);
    check("bar.visible-with-path", bar.isVisible());
    check("bar.segment-widgets", bar.segmentWidget(0) != nullptr && bar.segmentWidget(1) != nullptr);
    check("bar.out-of-range-widget", bar.segmentWidget(2) == nullptr);

    // --- click segment 1 (innermost: the function) -----------------------
    RecordingTarget target;
    vedit::workspace::jump::setTarget(&target);
    clickSegment(bar, 1);
    checkLong("click.function.calls", static_cast<long long>(target.deliveries.size()), 1);
    if (target.deliveries.size() == 1) {
        const Delivery &delivery = target.deliveries[0];
        printDelivery("BREADCRUMB-CLICK", 1, bar, target, delivery);
        checkString("click.function.file", delivery.filePath,
                    QStringLiteral("/tmp/vedit-fixture/top.sv"));
        checkLong("click.function.line", delivery.line, 4);
        checkLong("click.function.column", delivery.column, 1);
        check("click.function.status", bar.lastStatus() == JumpStatus::Delivered);
        checkLong("click.function.segment", bar.lastSegment(), 1);
    }

    // --- click segment 0 (outermost: the module) -------------------------
    clickSegment(bar, 0);
    checkLong("click.module.calls", static_cast<long long>(target.deliveries.size()), 2);
    if (target.deliveries.size() == 2) {
        const Delivery &delivery = target.deliveries[1];
        printDelivery("BREADCRUMB-CLICK", 0, bar, target, delivery);
        checkLong("click.module.line", delivery.line, 1);
    }

    // --- the target's refusal is surfaced, not swallowed -----------------
    target.accept = false;
    clickSegment(bar, 0);
    check("click.rejected.status", bar.lastStatus() == JumpStatus::Rejected);
    checkLong("click.rejected.calls", static_cast<long long>(target.deliveries.size()), 3);

    // --- no target: an explicit no-op, never a crash ---------------------
    vedit::workspace::jump::clearTarget();
    check("noop.bar-has-no-target", !vedit::workspace::jump::hasTarget());
    const bool activated = bar.activateSegment(1);
    std::printf("BREADCRUMB-NO-TARGET segment=1 activated=%d status=%s target-calls=%zu\n",
                activated ? 1 : 0,
                vedit::workspace::jump::jumpStatusName(bar.lastStatus()),
                target.deliveries.size());
    check("noop.status", bar.lastStatus() == JumpStatus::NoTarget);
    check("noop.not-delivered", !activated);
    checkLong("noop.no-new-delivery", static_cast<long long>(target.deliveries.size()), 3);

    // --- empty path hides the bar; invalid index is inert ----------------
    bar.setPath(QString(), std::vector<Entry>());
    check("bar.empty-hidden", !bar.isVisible());
    checkLong("bar.empty-segments", bar.segmentCount(), 0);
    check("bar.empty-activate-false", !bar.activateSegment(0));
    check("bar.negative-activate-false", !bar.activateSegment(-1));

    // --- a semantic update replaces the path (no stale buttons) ----------
    const std::vector<Entry> moduleOnly = source.pathAt(text, 0);
    bar.setPath(QStringLiteral("/tmp/vedit-fixture/top.sv"), moduleOnly);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    checkLong("bar.rebuild.segments", bar.segmentCount(), 1);
    checkString("bar.rebuild.text", bar.segmentText(0), QStringLiteral("m0"));
    check("bar.rebuild.no-stale-segment",
          bar.findChild<QWidget *>(QStringLiteral("breadcrumb-segment-1")) == nullptr);
    check("bar.rebuild.no-stale-separator",
          bar.findChild<QWidget *>(QStringLiteral("breadcrumb-separator")) == nullptr);

    return g_failures;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    runAcceptance();
    vedit::workspace::jump::clearTarget();
    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
