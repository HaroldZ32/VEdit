// Evidence probe for the diagnostics PANEL (T9).
//
// Drives the product's real DiagnosticsPanel (same source as the lane build)
// with fake backends and asserts, headless:
//   * AC-6.1  errors and warnings are grouped with their own counts;
//   * AC-6.2  the group colors are the editor's own palette and no severity
//             ever lands in the wrong group;
//   * AC-6.3  rows are sorted by file:line;
//   * AC-6.4 / AC-10.3.3  an explicit empty state names the next step;
//   * AC-7.1  double click on an outline row jumps to file:line:column through
//             the application-wide jump registry (NOT a panel-local path);
//   * AC-7.2  double click on a diagnostics row jumps to file:line.
//
// It also writes the grouped-panel screenshot to evidence/d-diagnostics/.
#include "diagnostics_controller.h"
#include "diagnostics_outline_model.h"
#include "diagnostics_panel.h"

#include "jump/jump_registry.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRect>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QtTest/QTest>

#include <cstdio>
#include <memory>
#include <vector>

using namespace vedit::workspace::diagnostics;

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

ToolchainReport foundToolchain()
{
    ToolchainReport report;
    report.verible.kind = ToolKind::Verible;
    report.verible.name = QStringLiteral("verible-verilog-ls");
    report.verible.path = QStringLiteral("/fixture/verible-verilog-ls");
    report.verible.source = ToolSource::Bundled;
    report.slang.kind = ToolKind::Slang;
    report.slang.name = QStringLiteral("slang");
    report.slang.path = QStringLiteral("/fixture/slang");
    report.slang.source = ToolSource::Bundled;
    return report;
}

class FakeLsp final : public LspSessionBackend {
public:
    QVector<DiagnosticEntry> diagnostics;
    QVector<OutlineSymbol> symbols;

    bool start(const QString &,
               const QString &,
               const QString &,
               const QStringList &files,
               QString *) override
    {
        files_ = files;
        statusCalls_ = 0;
        return true;
    }

    void stop() override {}

    SessionSnapshot status() const override
    {
        SessionSnapshot snapshot;
        ++statusCalls_;
        snapshot.state = BackendState::Ready;
        snapshot.filesExpected = files_.size();
        snapshot.filesOpened = files_.size();
        snapshot.message = QStringLiteral("fake session ready");
        return snapshot;
    }

    quint64 requestDefinition(const QString &, int, int) override { return 0; }
    quint64 requestReferences(const QString &, int, int) override { return 0; }

    quint64 requestSymbols(const QString &file) override
    {
        return record(ResponseKind::Symbols, file);
    }

    quint64 requestRename(const QString &, int, int, const QString &) override { return 0; }
    quint64 requestFormat(const QString &) override { return 0; }
    quint64 requestDiagnostics(const QString &file) override
    {
        return record(ResponseKind::Diagnostics, file);
    }

    LspPolled poll(quint64 requestId) override
    {
        if (requestId == 0 || requestId > static_cast<quint64>(kinds_.size())) {
            return LspPolled::failure(QStringLiteral("unknown fake request id"));
        }
        const ResponseKind kind = kinds_.at(static_cast<int>(requestId) - 1);
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = kind;
        if (kind == ResponseKind::Symbols) {
            polled.symbols = symbols;
        } else if (kind == ResponseKind::Diagnostics) {
            polled.diagnostics = diagnostics;
        }
        return polled;
    }

    int statusCalls() const { return statusCalls_; }

private:
    quint64 record(ResponseKind kind, const QString &)
    {
        kinds_.append(kind);
        return static_cast<quint64>(kinds_.size());
    }

    QStringList files_;
    mutable int statusCalls_ = 0;
    QVector<ResponseKind> kinds_;
};

class FakeSlang final : public SlangBackend {
public:
    bool available() const override { return false; }
    QString description() const override { return QStringLiteral("fake slang"); }
    int startLint(const QString &, QString *error) override
    {
        if (error != nullptr) {
            *error = QStringLiteral("fake slang is unavailable");
        }
        return 0;
    }
    LspPolled pollLint(int) override
    {
        return LspPolled::failure(QStringLiteral("fake slang has no tickets"));
    }
};

DiagnosticEntry makeEntry(int line, int column, int severity, const QString &message)
{
    DiagnosticEntry entry;
    entry.line = line;
    entry.column = column;
    entry.severity = severity;
    entry.message = message;
    entry.source = QStringLiteral("fake");
    return entry;
}

void pump(int ms = 30)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

struct JumpRecord {
    QString filePath;
    int line = 0;
    int column = 0;
};

class RecordingTarget final : public vedit::workspace::jump::IJumpTarget {
public:
    bool jumpTo(const QString &filePath, int line, int column) override
    {
        records.push_back(JumpRecord{filePath, line, column});
        return true;
    }

    std::vector<JumpRecord> records;
};

void clickTreeRow(QTreeView *view, int row)
{
    const QModelIndex index = view->model()->index(row, 0);
    view->scrollTo(index);
    pump(30);
    const QRect rect = view->visualRect(index);
    std::printf("DEBUG tree row=%d valid=%d rect=(%d,%d %dx%d) view-visible=%d vp-visible=%d "
                "index-at=%d\n",
                row, rect.isValid() ? 1 : 0, rect.x(), rect.y(), rect.width(), rect.height(),
                view->isVisible() ? 1 : 0, view->viewport()->isVisible() ? 1 : 0,
                view->indexAt(rect.center()).row());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    pump(20);
    QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    pump(40);
}

void clickIssueRow(QTreeWidget *tree, QTreeWidgetItem *item)
{
    tree->scrollToItem(item);
    pump(30);
    const QRect rect = tree->visualItemRect(item);
    std::printf("DEBUG issue item-valid=%d rect=(%d,%d %dx%d) view-visible=%d vp-visible=%d\n",
                rect.isValid() ? 1 : 0, rect.x(), rect.y(), rect.width(), rect.height(),
                tree->isVisible() ? 1 : 0, tree->viewport()->isVisible() ? 1 : 0);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    pump(20);
    QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    pump(40);
}

// --- AC-6.1..6.4: grouping, counts, colours, order, empty state -------------
void testGrouping(const QString &outDir)
{
    auto lsp = std::make_unique<FakeLsp>();
    lsp->diagnostics = {
        makeEntry(10, 4, 1, QStringLiteral("driven signal has no driver")),
        makeEntry(20, 0, 2, QStringLiteral("unused signal")),
        makeEntry(2, 1, 2, QStringLiteral("width mismatch")),
        makeEntry(15, 2, 3, QStringLiteral("style note")),
    };
    DiagnosticsController controller(std::move(lsp), std::make_unique<FakeSlang>());
    controller.setConfiguration(QStringLiteral("verible"), QStringLiteral("/fixture"));
    controller.setToolchainReport(foundToolchain());
    // Reverse order on purpose: the panel must sort by file:line, not by input.
    const QString zzz = QStringLiteral("/tmp/vedit-fixture/zzz/second.sv");
    const QString aaa = QStringLiteral("/tmp/vedit-fixture/aaa/first.sv");
    controller.setFileSet({zzz, aaa});
    for (int round = 0; round < 5; ++round) {
        controller.refresh();
        pump(10);
    }
    std::printf("OBSERVED controller entries=%lld state=%d\n",
                static_cast<long long>(controller.diagnostics().size()),
                static_cast<int>(controller.state()));

    DiagnosticsPanel panel(&controller);
    panel.resize(620, 700);
    panel.show();
    panel.refreshFromController();
    pump(60);

    checkLong("group.errors", panel.errorCount(), 2);
    checkLong("group.warnings", panel.warningCount(), 4);
    checkLong("group.information", panel.informationCount(), 2);
    checkLong("group.rows", panel.issueRowCount(), 8);
    checkString("group.counts-text", panel.countsText(),
                QStringLiteral("Errors: 2 · Warnings: 4 · Information: 2"));

    auto *issues = qobject_cast<QTreeWidget *>(panel.issuesWidget());
    check("group.widget", issues != nullptr);
    if (issues == nullptr) {
        return;
    }
    checkLong("group.top-level", issues->topLevelItemCount(), 3);
    checkString("group.errors.title", issues->topLevelItem(0)->text(0),
                QStringLiteral("Errors (2)"));
    checkString("group.warnings.title", issues->topLevelItem(1)->text(0),
                QStringLiteral("Warnings (4)"));
    checkString("group.information.title", issues->topLevelItem(2)->text(0),
                QStringLiteral("Information (2)"));

    const QColor errorColor = issues->topLevelItem(0)->foreground(0).color();
    const QColor warningColor = issues->topLevelItem(1)->foreground(0).color();
    std::printf("OBSERVED group colours errors=%s warnings=%s editor-error=%s editor-warning=%s\n",
                qPrintable(errorColor.name()), qPrintable(warningColor.name()),
                qPrintable(panel.errorColor().name()), qPrintable(panel.warningColor().name()));
    check("group.error-colour-is-editor-red", errorColor == panel.errorColor());
    check("group.warning-colour-is-editor-orange", warningColor == panel.warningColor());
    check("group.colours-differ", errorColor != warningColor);

    // AC-6.2: every row's severity matches its group - an error can never be
    // listed under Warnings (and vice versa).
    bool severityMatchesGroup = true;
    for (int group = 0; group < issues->topLevelItemCount(); ++group) {
        QTreeWidgetItem *header = issues->topLevelItem(group);
        for (int row = 0; row < header->childCount(); ++row) {
            const int severity = header->child(row)->data(0, DiagnosticsPanel::IssueSeverityRole).toInt();
            if (group == 0 && severity != 1) {
                severityMatchesGroup = false;
            }
            if (group == 1 && severity != 2) {
                severityMatchesGroup = false;
            }
            if (group == 2 && (severity == 1 || severity == 2)) {
                severityMatchesGroup = false;
            }
        }
    }
    check("group.severity-never-mixed", severityMatchesGroup);

    // AC-6.3: sorted by file:line - aaa/first.sv before zzz/second.sv, and the
    // two warnings of one file in ascending line order.
    QTreeWidgetItem *errorsHeader = issues->topLevelItem(0);
    QTreeWidgetItem *warningsHeader = issues->topLevelItem(1);
    check("sort.error.file-order",
          errorsHeader->childCount() == 2
              && errorsHeader->child(0)->text(0).startsWith(QStringLiteral("first.sv:"))
              && errorsHeader->child(1)->text(0).startsWith(QStringLiteral("second.sv:")));
    check("sort.warning.line-order",
          warningsHeader->childCount() == 4
              && warningsHeader->child(0)->text(0).startsWith(QStringLiteral("first.sv:3:"))
              && warningsHeader->child(1)->text(0).startsWith(QStringLiteral("first.sv:21:"))
              && warningsHeader->child(2)->text(0).startsWith(QStringLiteral("second.sv:3:"))
              && warningsHeader->child(3)->text(0).startsWith(QStringLiteral("second.sv:21:")));
    std::printf("OBSERVED rows errors='%s' | '%s' warnings='%s' | '%s' ...\n",
                errorsHeader->child(0)->text(0).toUtf8().constData(),
                errorsHeader->child(1)->text(0).toUtf8().constData(),
                warningsHeader->child(0)->text(0).toUtf8().constData(),
                warningsHeader->child(1)->text(0).toUtf8().constData());

    check("empty.hidden-with-diagnostics", !panel.diagnosticsEmptyStateVisible());

    const QString shot = outDir + QStringLiteral("/panel-error-warning-groups.png");
    const bool saved = panel.grab().save(shot);
    std::printf("SHOT panel '%s' saved=%d\n", shot.toUtf8().constData(), saved ? 1 : 0);
    check("shot.panel-saved", saved);
}

void testEmptyState(const QString &outDir)
{
    auto lsp = std::make_unique<FakeLsp>(); // no diagnostics at all
    DiagnosticsController controller(std::move(lsp), std::make_unique<FakeSlang>());
    controller.setToolchainReport(foundToolchain());
    DiagnosticsPanel panel(&controller);
    panel.show();
    panel.refreshFromController();
    pump(30);

    checkLong("empty.rows", panel.issueRowCount(), 0);
    check("empty.visible", panel.diagnosticsEmptyStateVisible());
    check("empty.mentions-next-step",
          panel.diagnosticsEmptyStateText().contains(QStringLiteral("open a file"))
              || panel.diagnosticsEmptyStateText().contains(QStringLiteral("project folder")));
    checkString("empty.counts-are-zero", panel.countsText(),
                QStringLiteral("Errors: 0 · Warnings: 0 · Information: 0"));
    std::printf("OBSERVED empty-state '%s'\n",
                panel.diagnosticsEmptyStateText().toUtf8().constData());

    panel.resize(620, 320);
    panel.show();
    pump(40);
    const QString shot = outDir + QStringLiteral("/panel-empty-state.png");
    const bool saved = panel.grab().save(shot);
    std::printf("SHOT empty-state '%s' saved=%d\n", shot.toUtf8().constData(), saved ? 1 : 0);
    check("shot.empty-state-saved", saved);
}

// --- AC-7.1 / AC-7.2: double clicks go through the jump registry ------------
void testDoubleClickJumps()
{
    vedit::workspace::jump::Registry::instance().reset();
    RecordingTarget target;
    vedit::workspace::jump::setTarget(&target);

    auto lsp = std::make_unique<FakeLsp>();
    lsp->diagnostics = {makeEntry(5, 3, 1, QStringLiteral("undeclared identifier"))};
    OutlineSymbol top;
    top.name = QStringLiteral("top");
    top.detail = QStringLiteral("module");
    top.kind = 2;
    top.line = 4;
    top.column = 7;
    OutlineSymbol alu;
    alu.name = QStringLiteral("u_alu");
    alu.detail = QStringLiteral("alu");
    alu.kind = 13;
    alu.line = 20;
    alu.column = 2;
    lsp->symbols = {top, alu};

    DiagnosticsController controller(std::move(lsp), std::make_unique<FakeSlang>());
    controller.setConfiguration(QStringLiteral("verible"), QStringLiteral("/fixture"));
    controller.setToolchainReport(foundToolchain());
    const QString file = QStringLiteral("/tmp/vedit-fixture/rtl/top.sv");
    controller.analyzeFile(file, QStringLiteral("module top;\n  logic a b;\nendmodule\n"), true);
    for (int round = 0; round < 5; ++round) {
        controller.refresh();
        pump(10);
    }

    DiagnosticsPanel panel(&controller);
    panel.resize(620, 700);
    panel.show();
    panel.refreshFromController();
    pump(60);
    checkLong("jump.outline-rows", panel.outlineRowCount(), 2);
    checkLong("jump.issue-rows", panel.issueRowCount(), 1);

    auto *outline = qobject_cast<QTreeView *>(panel.outlineWidget());
    auto *issues = qobject_cast<QTreeWidget *>(panel.issuesWidget());
    check("jump.widgets", outline != nullptr && issues != nullptr);
    if (outline == nullptr || issues == nullptr) {
        return;
    }

    // AC-7.1: outline row 0 is the `top` module at zero-based 4:7.
    clickTreeRow(outline, 0);
    checkLong("jump.outline.calls", static_cast<long long>(target.records.size()), 1);
    if (target.records.size() == 1) {
        const JumpRecord &record = target.records[0];
        std::printf("OUTLINE-DOUBLE-CLICK file='%s' line=%d column=%d\n",
                    record.filePath.toUtf8().constData(), record.line, record.column);
        checkString("jump.outline.file", record.filePath, file);
        checkLong("jump.outline.line", record.line, 5);    // 4 zero-based + 1
        checkLong("jump.outline.column", record.column, 8); // 7 zero-based + 1
    }

    // AC-7.2: diagnostics row jumps to its own file:line:column.
    QTreeWidgetItem *errorsHeader = issues->topLevelItem(0);
    check("jump.errors-group", errorsHeader != nullptr && errorsHeader->childCount() == 1);
    if (errorsHeader != nullptr && errorsHeader->childCount() == 1) {
        clickIssueRow(issues, errorsHeader->child(0));
        checkLong("jump.issue.calls", static_cast<long long>(target.records.size()), 2);
        if (target.records.size() == 2) {
            const JumpRecord &record = target.records[1];
            std::printf("DIAGNOSTIC-DOUBLE-CLICK file='%s' line=%d column=%d\n",
                        record.filePath.toUtf8().constData(), record.line, record.column);
            checkString("jump.issue.file", record.filePath, file);
            checkLong("jump.issue.line", record.line, 6);    // 5 zero-based + 1
            checkLong("jump.issue.column", record.column, 4); // 3 zero-based + 1
        }
    }

    // A panel with no registered target is a no-op, never a crash.
    vedit::workspace::jump::clearTarget();
    const std::size_t before = target.records.size();
    clickTreeRow(outline, 1);
    clickIssueRow(issues, errorsHeader->child(0));
    std::printf("NO-TARGET double clicks completed, records=%zu (unchanged=%d)\n",
                target.records.size(), target.records.size() == before ? 1 : 0);
    check("jump.no-target-no-crash", target.records.size() == before);
    check("jump.no-target-registry-empty", !vedit::workspace::jump::hasTarget());
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString outDir =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("evidence/d-diagnostics");
    QDir().mkpath(outDir);

    testGrouping(outDir);
    testEmptyState(outDir);
    testDoubleClickJumps();

    vedit::workspace::jump::Registry::instance().reset();
    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
