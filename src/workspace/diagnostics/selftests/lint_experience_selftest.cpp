// UX batch 2 / B3 — the diagnostics experience, proven headlessly.
//
// Contract: .mpd/plans/ux-batch-2/00-plan.md §B3 + §0.1 (R2/R3), items 9/10/12.
//
// Everything here runs offscreen with fakes for the two backends and for the
// project-lint pass, EXCEPT the engine-driven autofix parts, which use the real
// verible-verilog-lint binary and SKIP explicitly when it is absent. The
// assertions are the acceptance matrix's ①..⑥; ⑦ lives in the same binary
// (the notice policy) and ⑧ (rename) has its own real-server binary.
//
//   build:  vedit-lint-experience-selftest [--verible-lint <path>]

#include "autofix.h"
#include "diagnostic_hover.h"
#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "project_lint_notice.h"
#include "project_lint_trigger.h"
#include "symbol_scan.h"

#include "editor_area.h"
#include "editor_provider.h"
#include "file_status_delegate.h"
#include "file_tree_model.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QWidget>

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <cstdio>
#include <functional>
#include <memory>

using namespace vedit::workspace;
using namespace vedit::workspace::diagnostics;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skips = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.isEmpty() ? "" : "  ",
                detail.isEmpty() ? "" : detail.toUtf8().constData());
}

void skip(const char *name, const QString &why)
{
    ++g_skips;
    std::printf("SKIP %s  %s\n", name, why.toUtf8().constData());
}

void section(const char *title)
{
    std::printf("\n## %s\n", title);
}

/// Pump the event loop until `predicate` holds or the deadline expires.
bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return predicate();
}

/// Pump the controller's own poll (the GUI never blocks on it either).
bool waitForRefresh(DiagnosticsController &controller,
                    const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        controller.refresh();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return predicate();
}

/// The editor's own named action (the same walk EditorPane::triggerEditorAction
/// does for Ctrl+Z; KTextEditor keeps `edit_undo` on a child of the view).
QAction *findEditorAction(QWidget *editor, const QString &name)
{
    if (editor == nullptr) {
        return nullptr;
    }
    for (QAction *action : editor->actions()) {
        if (action->objectName() == name) {
            return action;
        }
    }
    for (QWidget *widget : editor->findChildren<QWidget *>()) {
        for (QAction *action : widget->actions()) {
            if (action->objectName() == name) {
                return action;
            }
        }
    }
    for (QAction *action : editor->findChildren<QAction *>()) {
        if (action->objectName() == name) {
            return action;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------

class FakeLsp final : public LspSessionBackend {
public:
    BackendCapabilities capabilities;
    QVector<DiagnosticEntry> rowsForFile; ///< what requestDiagnostics answers
    QStringList startedFiles;
    int startCount = 0;
    bool started = false;
    int renameRequests = 0;

    bool start(const QString &, const QString &, const QString &, const QStringList &files,
               QString *) override
    {
        ++startCount;
        startedFiles = files;
        started = true;
        snapshot.filesExpected = files.size();
        snapshot.filesOpened = files.size();
        snapshot.state = BackendState::Ready;
        return true;
    }
    void stop() override { started = false; }
    SessionSnapshot status() const override { return snapshot; }

    quint64 requestDefinition(const QString &, int, int) override { return ++last_; }
    quint64 requestReferences(const QString &, int, int) override { return ++last_; }
    quint64 requestSymbols(const QString &) override { return ++last_; }
    quint64 requestRename(const QString &, int, int, const QString &) override
    {
        ++renameRequests;
        return ++last_;
    }
    quint64 requestFormat(const QString &) override { return ++last_; }
    quint64 requestDiagnostics(const QString &) override { return ++last_; }
    quint64 requestHover(const QString &, int, int) override { return ++last_; }

    LspPolled poll(quint64 requestId) override
    {
        LspPolled polled;
        if (requestId == 0) {
            return LspPolled::failure(QStringLiteral("unknown request"));
        }
        polled.pending = false;
        polled.ready = true;
        polled.kind = ResponseKind::Diagnostics;
        polled.diagnostics = rowsForFile;
        return polled;
    }

    BackendCapabilities staticCapabilities() const override { return capabilities; }
    BackendCapabilities sessionCapabilities() const override { return capabilities; }
    QString backendName() const override { return QStringLiteral("fake-lsp"); }

private:
    SessionSnapshot snapshot;
    quint64 last_ = 0;
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
    LspPolled pollLint(int) override { return LspPolled::failure(QStringLiteral("no lint")); }
};

/// A project pass that answers immediately with a fixed row set and counts the
/// jobs it started (the "exactly one pass" observable).
class FakeProjectLint final : public ProjectLintBackend {
public:
    QVector<DiagnosticEntry> rows;
    int startCount = 0;
    int cancelCount = 0;
    int releaseCount = 0;
    bool availableFlag = true;
    /// When true the job reports Running once before it finishes: enough for the
    /// single-flight assertion to have something to cancel.
    bool stayRunning = false;
    QStringList filesSeen;
    LintPassSnapshot snapshot;

    bool available() const override { return availableFlag; }
    QString toolName() const override { return QStringLiteral("fake-lint"); }

    quint64 start(const LintPassRequest &request, QString *error) override
    {
        ++startCount;
        filesSeen = request.files;
        if (!availableFlag) {
            if (error != nullptr) {
                *error = QStringLiteral("fake-lint is not installed");
            }
            return 0;
        }
        job_ = static_cast<quint64>(startCount);
        snapshot = LintPassSnapshot{};
        snapshot.phase = stayRunning ? LintPhase::Running : LintPhase::Finished;
        snapshot.total = request.files.size();
        snapshot.done = stayRunning ? 0 : request.files.size();
        snapshot.tool = QStringLiteral("fake-lint");
        return job_;
    }

    LintPassSnapshot progress(quint64) const override { return snapshot; }

    LspPolled poll(quint64) override
    {
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = ResponseKind::Diagnostics;
        polled.diagnostics = rows;
        snapshot.phase = LintPhase::Finished;
        snapshot.done = snapshot.total;
        return polled;
    }

    void cancel(quint64) override
    {
        ++cancelCount;
        snapshot.phase = LintPhase::Cancelled;
    }
    void release(quint64) override { ++releaseCount; }

private:
    quint64 job_ = 0;
};

DiagnosticEntry row(const QString &file, int line, int severity, const QString &code,
                    const QString &message)
{
    DiagnosticEntry entry;
    entry.file = file;
    entry.line = line;
    entry.column = 2;
    entry.endLine = line;
    entry.endColumn = 8;
    entry.severity = severity;
    entry.code = code;
    entry.source = QStringLiteral("cli:fake");
    entry.message = message;
    return entry;
}

QString projectFile(QTemporaryDir &dir, const QString &name, const QString &text)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::printf("FIXTURE-FAILED %s\n", qPrintable(path));
        return QString();
    }
    file.write(text.toUtf8());
    file.close();
    return path;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString veribleLint;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QLatin1String("--verible-lint") && index + 1 < argc) {
            veribleLint = QString::fromLocal8Bit(argv[++index]);
        }
    }

    // =======================================================================
    section("R2 — one layered view (upper project pass, lower per-file/LSP)");
    // =======================================================================
    {
        QTemporaryDir dir;
        const QString a = projectFile(dir, QStringLiteral("a.sv"), QStringLiteral("module a;\n"));
        const QString b = projectFile(dir, QStringLiteral("b.sv"), QStringLiteral("module b;\n"));
        const QString c = projectFile(dir, QStringLiteral("c.sv"), QStringLiteral("module c;\n"));
        Q_UNUSED(b);
        Q_UNUSED(c);

        auto lsp = std::make_unique<FakeLsp>();
        FakeLsp *lspRaw = lsp.get();
        lspRaw->capabilities.backend = QStringLiteral("fake-lsp");
        lspRaw->capabilities.rows.append(CapabilityRow{QStringLiteral("rename"), 1, QString()});
        DiagnosticEntry lspRow;
        lspRow.file = a;
        lspRow.line = 4;
        lspRow.column = 1;
        lspRow.endLine = 4;
        lspRow.endColumn = 6;
        lspRow.severity = 2;
        lspRow.code = QStringLiteral("lsp-warning");
        lspRow.source = QStringLiteral("verible");
        lspRow.message = QStringLiteral("lower-layer warning");
        lspRaw->rowsForFile.append(lspRow);

        auto slang = std::make_unique<FakeSlang>();
        DiagnosticsController controller(std::move(lsp), std::move(slang));
        controller.setConfiguration(QStringLiteral("/fake/verible-ls"), dir.path());
        check("r2.analyze-file", controller.analyzeFile(a, QStringLiteral("module a;\n"), false));
        waitForRefresh(controller, [&controller]() { return !controller.diagnostics().isEmpty(); },
                       2000);
        check("r2.lower-layer-present", controller.diagnostics().size() == 1,
              QStringLiteral("rows=%1").arg(controller.diagnostics().size()));
        check("r2.file-status-lower", controller.statusForFile(a)
                  == DiagnosticsController::FileStatus::Warning);

        // The project pass now reports the SAME file with an error.
        auto backend = std::make_unique<FakeProjectLint>();
        FakeProjectLint *pass = backend.get();
        // The CLI ABI's severity scale is 0 error / 1 warning / 2 note: the
        // controller must convert it to the display scale (1 error / 2 warning)
        // or a project-pass ERROR would be painted as information.
        pass->rows.append(row(a, 7, 0, QStringLiteral("no-trailing-spaces"),
                              QStringLiteral("upper-layer error")));
        pass->rows.append(row(c, 3, 1, QStringLiteral("line-length"),
                              QStringLiteral("upper-layer warning")));
        controller.setProjectLintBackend(std::move(backend));
        controller.setOpenFiles(QStringList());
        LintPassRequest request;
        request.files = QStringList{a, b, c};
        QString error;
        check("r2.pass-starts", controller.startProjectPass(request, &error), error);
        waitForRefresh(controller, [&controller]() { return !controller.projectPassRunning(); },
                       2000);

        const QVector<DiagnosticEntry> merged = controller.mergedDiagnostics();
        int forA = 0;
        int upperForA = 0;
        int lowerForA = 0;
        for (const DiagnosticEntry &entry : merged) {
            if (entry.file != a) {
                continue;
            }
            ++forA;
            if (entry.code == QStringLiteral("no-trailing-spaces")) {
                ++upperForA;
            }
            if (entry.code == QStringLiteral("lsp-warning")) {
                ++lowerForA;
            }
        }
        check("r2.upper-overrides-lower", forA == 1 && upperForA == 1 && lowerForA == 0,
              QStringLiteral("rows=%1 upper=%2 lower=%3").arg(forA).arg(upperForA).arg(lowerForA));

        // ④ the three dot colours, from the SAME merged view.
        check("r2.dot-red", controller.statusForFile(a) == DiagnosticsController::FileStatus::Error);
        check("r2.dot-yellow",
              controller.statusForFile(c) == DiagnosticsController::FileStatus::Warning);
        check("r2.dot-green", controller.statusForFile(b) == DiagnosticsController::FileStatus::Clean,
              QStringLiteral("covered and clean"));
        check("r2.dot-none", controller.statusForFile(dir.filePath(QStringLiteral("nope.sv")))
                  == DiagnosticsController::FileStatus::Unknown);
        check("r2.dot-colors", FileStatusDotDelegate::dotColor(1) == QColor(0xd3, 0x2f, 0x2f)
                  && FileStatusDotDelegate::dotColor(2) == QColor(0xf9, 0xa8, 0x25)
                  && FileStatusDotDelegate::dotColor(3) == QColor(0x2e, 0x7d, 0x32));

        // Counts read the merged view: 3 rows total (1 for a, 1 for c, 0 for b).
        check("r2.merged-count", merged.size() == 2,
              QStringLiteral("rows=%1").arg(merged.size()));
        // The severity SCALE is converted once (CLI 0 = error -> display 1).
        int aSeverity = -1;
        for (const DiagnosticEntry &entry : merged) {
            if (entry.file == a) {
                aSeverity = entry.severity;
            }
        }
        check("r2.cli-severity-scale", aSeverity == 1,
              QStringLiteral("upper row severity=%1 (0 was the ABI's 'error')").arg(aSeverity));

        // ③ RM-9 (requirements change, T20): an edit VOIDS the file's verdict.
        // The lower rows were computed on the text BEFORE the edit, so they must
        // not be shown as if they described the buffer: the file shows the
        // explicit "not analysed / excluded" state and never a stale green. A
        // fresh analysis then restores a real verdict (the user's rule: a fresh
        // verdict wins, dirty buffer or not).
        controller.markFileEdited(a);
        check("r2.edit-drops-upper", !controller.hasUpperLayer(a));
        check("r2.edit-voids-lower", controller.mergedDiagnosticsForFile(a).isEmpty(),
              QStringLiteral("rows=%1").arg(controller.mergedDiagnosticsForFile(a).size()));
        check("r2.edit-status-not-analysed",
              controller.statusForFile(a) == DiagnosticsController::FileStatus::NotAnalyzed,
              QStringLiteral("status=%1").arg(static_cast<int>(controller.statusForFile(a))));
        check("r2.edit-never-green",
              controller.statusForFile(a) != DiagnosticsController::FileStatus::Clean);
        check("r2.edit-reanalyze",
              controller.analyzeFile(a, QStringLiteral("module a;\n"), false));
        waitForRefresh(controller,
                       [&controller]() { return controller.state() != BackendState::Reparsing; },
                       3000);
        check("r2.edit-status-yellow",
              controller.statusForFile(a) == DiagnosticsController::FileStatus::Warning,
              QStringLiteral("status=%1").arg(static_cast<int>(controller.statusForFile(a))));
        // A later pass re-covers it with the fresh verdict.
        pass->rows.clear();
        pass->rows.append(row(a, 9, 0, QStringLiteral("no-trailing-spaces"),
                              QStringLiteral("second pass")));
        controller.startProjectPass(request, &error);
        waitForRefresh(controller, [&controller]() { return !controller.projectPassRunning(); },
                       2000);
        check("r2.repass-recovers", controller.hasUpperLayer(a)
                  && controller.mergedDiagnosticsForFile(a).size() == 1
                  && controller.mergedDiagnosticsForFile(a).first().line == 9);
    }

    // =======================================================================
    section("B3 repair — one count, lower-layer clean dot, rename gate, merge key");
    // =======================================================================
    {
        QTemporaryDir dir;
        const QString a = projectFile(dir, QStringLiteral("a.sv"),
                                      QStringLiteral("module a;\n  logic x;\nendmodule\n"));
        auto lsp = std::make_unique<FakeLsp>();
        auto slang = std::make_unique<FakeSlang>();
        DiagnosticsController controller(std::move(lsp), std::move(slang));
        controller.setConfiguration(QStringLiteral("/fake/verible-ls"), dir.path());
        check("count.analyze", controller.analyzeFile(
                  a, QStringLiteral("module a;\n  logic x;\nendmodule\n"), false));
        waitForRefresh(controller,
                       [&controller]() { return controller.state() != BackendState::Reparsing; },
                       3000);
        const QString beforeMessage = controller.session().message;
        auto countFromMessage = [](const QString &message) {
            const int index = message.indexOf(QStringLiteral("diagnostics "));
            if (index < 0) {
                return -1;
            }
            return message.mid(index + 12).trimmed().section(QLatin1Char(' '), 0, 0).toInt();
        };
        check("count.lower-zero", countFromMessage(beforeMessage) == 0,
              beforeMessage);

        auto backend = std::make_unique<FakeProjectLint>();
        FakeProjectLint *pass = backend.get();
        pass->rows.append(row(a, 1, 0, QStringLiteral("no-trailing-spaces"),
                              QStringLiteral("first upper error")));
        pass->rows.append(row(a, 3, 0, QStringLiteral("line-length"),
                              QStringLiteral("second upper error")));
        controller.setProjectLintBackend(std::move(backend));
        controller.setOpenFiles(QStringList());
        LintPassRequest request;
        request.files = QStringList{a};
        QString error;
        check("count.pass-starts", controller.startProjectPass(request, &error), error);
        waitForRefresh(controller,
                       [&controller]() { return !controller.projectPassRunning(); }, 3000);
        const int merged = controller.mergedDiagnostics().size();
        waitForRefresh(controller,
                       [&controller, merged]() {
                           const int index =
                               controller.session().message.indexOf(QStringLiteral("diagnostics "));
                           return index >= 0
                               && controller.session().message.mid(index + 12)
                                      .trimmed().section(QLatin1Char(' '), 0, 0).toInt()
                                  == merged;
                       },
                       3000);
        check("count.session-uses-merged",
              countFromMessage(controller.session().message) == merged && merged == 2
                  && controller.session().diagnostics == merged,
              QStringLiteral("message='%1' merged=%2 session.diagnostics=%3")
                  .arg(controller.session().message)
                  .arg(merged)
                  .arg(controller.session().diagnostics));
    }
    {
        // d) a file the LOWER layer analysed clean is green, not dot-less.
        QTemporaryDir dir;
        const QString a = projectFile(dir, QStringLiteral("a.sv"),
                                      QStringLiteral("module a;\n  logic x;\nendmodule\n"));
        auto lsp = std::make_unique<FakeLsp>(); // answers with zero rows
        auto slang = std::make_unique<FakeSlang>();
        DiagnosticsController controller(std::move(lsp), std::move(slang));
        controller.setConfiguration(QStringLiteral("/fake/verible-ls"), dir.path());
        check("dot.lower-analyze", controller.analyzeFile(
                  a, QStringLiteral("module a;\n  logic x;\nendmodule\n"), false));
        waitForRefresh(controller,
                       [&controller]() { return controller.state() != BackendState::Reparsing; },
                       3000);
        check("dot.lower-layer-analyzed", controller.lowerLayerAnalyzed(a));
        check("dot.lower-clean-green",
              controller.statusForFile(a) == DiagnosticsController::FileStatus::Clean,
              QStringLiteral("status=%1").arg(static_cast<int>(controller.statusForFile(a))));
        check("dot.lower-clean-map", controller.fileStatuses().value(a) == 3,
              QStringLiteral("value=%1").arg(controller.fileStatuses().value(a, -1)));
        check("dot.never-analyzed-unknown",
              controller.statusForFile(dir.filePath(QStringLiteral("nope.sv")))
                  == DiagnosticsController::FileStatus::Unknown);
    }
    {
        // N1: the CLEAN verdict belongs to the CURRENT analysis. A file the LSP
        // reported with an ERROR must not turn green when the rows that produced
        // the verdict are gone (another file opened, a backend/settings switch):
        // the old lowerAnalyzed_ set outlived its rows and answered Clean.
        QTemporaryDir dir;
        const QString a = projectFile(dir, QStringLiteral("a.sv"),
                                      QStringLiteral("module a;\n  logic x;   \nendmodule\n"));
        const QString b = projectFile(dir, QStringLiteral("b.sv"),
                                      QStringLiteral("module b;\n  logic y;\nendmodule\n"));
        auto lsp = std::make_unique<FakeLsp>();
        FakeLsp *lspRaw = lsp.get();
        DiagnosticEntry errorRow = row(a, 1, 1, QStringLiteral("no-trailing-spaces"),
                                       QStringLiteral("fake LSP error"));
        errorRow.source = QStringLiteral("verible");
        lspRaw->rowsForFile = QVector<DiagnosticEntry>{errorRow};
        auto slang = std::make_unique<FakeSlang>();
        DiagnosticsController controller(std::move(lsp), std::move(slang));
        controller.setConfiguration(QStringLiteral("/fake/verible-ls"), dir.path());
        check("n1.analyze-a",
              controller.analyzeFile(a, QStringLiteral("module a;\n  logic x;   \nendmodule\n"),
                                     false));
        waitForRefresh(controller,
                       [&controller]() { return controller.state() != BackendState::Reparsing; },
                       3000);
        check("n1.a-error-before",
              controller.statusForFile(a) == DiagnosticsController::FileStatus::Error,
              QStringLiteral("status=%1").arg(static_cast<int>(controller.statusForFile(a))));

        // RM-9 (requirements change; the user's words: "不是'打开过才有'，是'正在
        // 打开的文件'才有"). Opening B used to ERASE A's verdict - that was the
        // DEFECT, not the guarantee. The stronger property is asserted now: A
        // keeps its REAL verdict (Error) across the tab switch and when the user
        // comes back. The stale-green defect N1 originally guarded is still
        // guarded: the retained value is the pre-switch ERROR, never a Clean.
        check("n1.analyze-b",
              controller.analyzeFile(b, QStringLiteral("module b;\n  logic y;\nendmodule\n"),
                                     false));
        waitForRefresh(controller,
                       [&controller]() { return controller.state() != BackendState::Reparsing; },
                       3000);
        check("n1.a-retains-error-after-file-switch",
              controller.statusForFile(a) == DiagnosticsController::FileStatus::Error,
              QStringLiteral("status=%1 merged=%2")
                  .arg(static_cast<int>(controller.statusForFile(a)))
                  .arg(controller.mergedDiagnosticsForFile(a).size()));
        check("n1.a-retained-error-in-map",
              controller.fileStatuses().value(a, 0)
                  == static_cast<int>(DiagnosticsController::FileStatus::Error),
              QStringLiteral("value=%1").arg(controller.fileStatuses().value(a, -1)));
        check("n1.analyze-a-back",
              controller.analyzeFile(a, QStringLiteral("module a;\n  logic x;   \nendmodule\n"),
                                     false));
        waitForRefresh(controller,
                       [&controller]() { return controller.state() != BackendState::Reparsing; },
                       3000);
        check("n1.a-error-after-return",
              controller.statusForFile(a) == DiagnosticsController::FileStatus::Error,
              QStringLiteral("status=%1").arg(static_cast<int>(controller.statusForFile(a))));

        // A backend/settings switch clears the project rows and reparses: the
        // controller-side path is clearProjectDiagnostics() + setFileSet({}).
        controller.clearProjectDiagnostics();
        controller.setFileSet(QStringList{});
        waitForRefresh(controller,
                       [&controller]() { return controller.state() != BackendState::Reparsing; },
                       3000);
        const DiagnosticsController::FileStatus afterSwitch = controller.statusForFile(a);
        check("n1.a-not-clean-after-backend-switch",
              afterSwitch != DiagnosticsController::FileStatus::Clean,
              QStringLiteral("status=%1").arg(static_cast<int>(afterSwitch)));
    }
    {
        // a) the request-path gate refuses a NULL/unknown capability row.
        QTemporaryDir dir;
        const QString a = projectFile(dir, QStringLiteral("a.sv"), QStringLiteral("module a;\n"));
        auto lsp = std::make_unique<FakeLsp>();
        FakeLsp *lspRaw = lsp.get();
        lspRaw->capabilities.backend = QStringLiteral("fake-lsp");
        // no rename row at all (the old code only refused row != nullptr && 0)
        auto slang = std::make_unique<FakeSlang>();
        DiagnosticsController controller(std::move(lsp), std::move(slang));
        controller.setConfiguration(QStringLiteral("/fake/verible-ls"), dir.path());
        const quint64 renameId =
            controller.requestRename(a, 0, 0, QStringLiteral("w2"));
        const QString renameReason = controller.lastError();
        check("rename.null-row-refused", renameId == 0 && lspRaw->renameRequests == 0,
              QStringLiteral("requests=%1 reason='%2'")
                  .arg(lspRaw->renameRequests)
                  .arg(renameReason));
        check("rename.null-row-reason", !renameReason.isEmpty(), renameReason);
    }
    {
        // nit: the merge key normalizes two spellings of one file.
        QTemporaryDir dir;
        DiagnosticEntry first;
        first.file = dir.filePath(QStringLiteral("a.sv"));
        first.line = 4;
        first.severity = 1;
        first.message = QStringLiteral("undeclared identifier");
        DiagnosticEntry second = first;
        second.file = dir.path() + QStringLiteral("/./a.sv");
        check("merge.key-normalizes-path",
              diagnosticMergeKey(first, DiagnosticOrigin::Lsp)
                  == diagnosticMergeKey(second, DiagnosticOrigin::Lsp),
              QStringLiteral("'%1' vs '%2'")
                  .arg(diagnosticMergeKey(first, DiagnosticOrigin::Lsp),
                       diagnosticMergeKey(second, DiagnosticOrigin::Lsp)));
    }
    {
        // b) a failed replaceText is reported as a failure, not success.
        KTextEditor::Document *document = KTextEditor::Editor::instance()->createDocument(nullptr);
        check("autofix.replace-doc", document != nullptr);
        if (document != nullptr) {
            document->setText(QStringLiteral("module a;\n  logic x;   \nendmodule\n"));
            document->setReadWrite(false);
            const QString before = document->text();
            const bool applied =
                applyAutofixToDocument(document, QStringLiteral("module a;\n  logic x;\nendmodule\n"));
            check("autofix.replace-refused", !applied && document->text() == before,
                  QStringLiteral("applied=%1 text-unchanged=%2")
                      .arg(applied ? 1 : 0)
                      .arg(document->text() == before ? 1 : 0));
            document->setReadWrite(true);
            const bool appliedAgain = applyAutofixToDocument(
                document, QStringLiteral("module a;\n  logic x;\nendmodule\n"));
            check("autofix.replace-writable", appliedAgain && document->text().contains(
                      QStringLiteral("logic x;\n")));
            delete document;
        }
    }
    if (!veribleLint.isEmpty() && QFileInfo(veribleLint).isExecutable()) {
        // b) the cache key includes the base name and the active rules config.
        QTemporaryDir dir;
        const QString rulesFile = dir.filePath(QStringLiteral("rules.rules"));
        QFile rules(rulesFile);
        if (rules.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            rules.write("\n");
            rules.close();
        }
        AutofixEngine engine(veribleLint);
        const QString text = QStringLiteral("module af;\n  logic x;   \nendmodule\n");
        const QString rule = QStringLiteral("no-trailing-spaces");
        const AutofixOutcome firstProbe =
            engine.probe(rule, dir.filePath(QStringLiteral("a.sv")), text);
        check("autofix.cache-basename-seed", firstProbe.supported, firstProbe.error);
        const quint64 afterFirst = engine.probeCount();
        engine.probe(rule, dir.filePath(QStringLiteral("a.sv")), text);
        check("autofix.cache-hit", engine.probeCount() == afterFirst,
              QStringLiteral("count=%1").arg(engine.probeCount()));
        engine.probe(rule, dir.filePath(QStringLiteral("b.sv")), text);
        check("autofix.cache-basename-in-key", engine.probeCount() == afterFirst + 1,
              QStringLiteral("count=%1 expected=%2")
                  .arg(engine.probeCount())
                  .arg(afterFirst + 1));
        const quint64 afterSecond = engine.probeCount();
        engine.setRulesConfig(rulesFile);
        engine.probe(rule, dir.filePath(QStringLiteral("a.sv")), text);
        check("autofix.cache-rules-config-in-key", engine.probeCount() == afterSecond + 1,
              QStringLiteral("count=%1 expected=%2")
                  .arg(engine.probeCount())
                  .arg(afterSecond + 1));
    } else {
        skip("autofix.cache-key", QStringLiteral("no verible-verilog-lint"));
    }

    // =======================================================================
    section("item 12 repair — the counter sees a RE-SHOW of the same dialog");
    // =======================================================================
    {
        QWidget window;
        QStatusBar bar(&window);
        ProjectLintNotice notice(&window, &bar);
        QVector<DiagnosticEntry> rows;
        DiagnosticEntry entryRow;
        entryRow.file = QStringLiteral("/tmp/a.sv");
        entryRow.line = 1;
        entryRow.severity = 1;
        entryRow.message = QStringLiteral("boom");
        rows.append(entryRow);
        notice.present(rows, ProjectLintNotice::Mode::Explicit);
        const int first = notice.dialogsShown();
        if (notice.dialog() != nullptr) {
            notice.dialog()->hide();
        }
        notice.present(rows, ProjectLintNotice::Mode::Automatic);
        const int afterAutomatic = notice.dialogsShown();
        check("modal.automatic-does-not-show",
              first == 1 && afterAutomatic == 1,
              QStringLiteral("first=%1 afterAutomatic=%2").arg(first).arg(afterAutomatic));
        notice.present(rows, ProjectLintNotice::Mode::Explicit);
        // The object is the SAME one; only show() was called again. Object
        // counting would miss this - the counter must not.
        check("modal.reshow-counted", notice.dialogsShown() == first + 1,
              QStringLiteral("dialogsShown=%1").arg(notice.dialogsShown()));
    }

    // =======================================================================
    section("R3 — three quick saves = exactly ONE project pass");
    // =======================================================================
    {
        QTemporaryDir dir;
        const QString a = projectFile(dir, QStringLiteral("a.sv"), QStringLiteral("module a;\n"));
        auto lsp = std::make_unique<FakeLsp>();
        auto slang = std::make_unique<FakeSlang>();
        DiagnosticsController controller(std::move(lsp), std::move(slang));
        auto backend = std::make_unique<FakeProjectLint>();
        FakeProjectLint *pass = backend.get();
        controller.setProjectLintBackend(std::move(backend));

        ProjectLintTrigger trigger(&controller);
        trigger.setDebounceInterval(120);
        trigger.setRequestBuilder([a]() {
            LintPassRequest request;
            request.files = QStringList{a};
            return request;
        });
        trigger.setOpenFilesProvider([]() { return QStringList(); });

        trigger.noteSaved(a);
        trigger.noteSaved(a);
        trigger.noteSaved(a);
        // The save path must not be delayed by lint: nothing started yet.
        check("r3.non-blocking", trigger.passStarts() == 0 && pass->startCount == 0,
              QStringLiteral("starts=%1").arg(pass->startCount));
        check("r3.coalesced-pending", trigger.pending());
        waitFor([&trigger]() { return trigger.passStarts() > 0; }, 2000);
        // Give a second pass every chance to appear before asserting ONE.
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 300) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        // Let that pass finish (pollProjectPass is what clears passRunning_).
        waitForRefresh(controller, [&controller]() { return !controller.projectPassRunning(); },
                       2000);
        check("r3.exactly-one-pass", trigger.passStarts() == 1 && pass->startCount == 1,
              QStringLiteral("starts=%1 backend=%2 saves=%3")
                  .arg(trigger.passStarts())
                  .arg(pass->startCount)
                  .arg(trigger.saveCount()));

        // Single flight: a running pass is cancelled and replaced, never queued.
        pass->stayRunning = true;
        pass->snapshot.phase = LintPhase::Running;
        LintPassRequest request;
        request.files = QStringList{a};
        controller.setOpenFiles(QStringList());
        controller.startProjectPass(request, nullptr);
        check("r3.running", controller.projectPassRunning());
        trigger.flushPending(); // nothing pending: no-op
        trigger.noteSaved(a);
        trigger.flushPending();
        check("r3.single-flight-cancelled", trigger.cancelledRunning() == 1,
              QStringLiteral("cancelled=%1").arg(trigger.cancelledRunning()));
        check("r3.single-flight-restarted", pass->startCount == 3,
              QStringLiteral("backend starts=%1").arg(pass->startCount));
        pass->stayRunning = false;
        pass->snapshot.phase = LintPhase::Finished;
        waitForRefresh(controller, [&controller]() { return !controller.projectPassRunning(); },
                       2000);
        check("r3.automatic-flag", controller.lastPassWasAutomatic(),
              QStringLiteral("automatic=%1")
                  .arg(controller.lastPassWasAutomatic() ? 1 : 0));
    }

    // =======================================================================
    section("item 12 — the modal dialog is reserved for the explicit run");
    // =======================================================================
    {
        QWidget window;
        QStatusBar bar(&window);
        ProjectLintNotice notice(&window, &bar);
        QVector<DiagnosticEntry> errors;
        errors.append(row(QStringLiteral("/p/a.sv"), 2, 1, QStringLiteral("syntax-error"),
                          QStringLiteral("elaboration failed")));
        errors.append(row(QStringLiteral("/p/b.sv"), 5, 1, QStringLiteral("undeclared"),
                          QStringLiteral("unknown identifier")));

        notice.present(errors, ProjectLintNotice::Mode::Automatic);
        check("elab.automatic-no-dialog", notice.dialogsShown() == 0,
              QStringLiteral("dialogs=%1").arg(notice.dialogsShown()));
        check("elab.automatic-notice", notice.noticeVisible() && !notice.noticeText().isEmpty(),
              notice.noticeText());
        check("elab.automatic-rows", notice.rows().size() == 2);

        int jumps = 0;
        QString jumpedFile;
        QObject::connect(&notice, &ProjectLintNotice::jumpRequested,
                         [&](const QString &file, int, int) {
                             ++jumps;
                             jumpedFile = file;
                         });
        // The notice is CLICKABLE: a click jumps (and never opens the dialog).
        QLabel *label = window.findChild<QLabel *>(QStringLiteral("veditProjectLintNotice"));
        check("elab.notice-widget", label != nullptr);
        if (label != nullptr) {
            emit label->linkActivated(QStringLiteral("jump"));
        }
        check("elab.notice-jumps", jumps == 1 && !jumpedFile.isEmpty(),
              QStringLiteral("jumps=%1 file=%2").arg(jumps).arg(jumpedFile));
        check("elab.notice-still-no-dialog", notice.dialogsShown() == 0);

        notice.present(errors, ProjectLintNotice::Mode::Explicit);
        check("elab.explicit-dialog", notice.dialogsShown() == 1,
              QStringLiteral("dialogs=%1").arg(notice.dialogsShown()));
        QDialog *dialog = notice.dialog();
        QTreeWidget *tree = dialog != nullptr
            ? dialog->findChild<QTreeWidget *>(QStringLiteral("veditElabDialogRows"))
            : nullptr;
        check("elab.dialog-rows", tree != nullptr && tree->topLevelItemCount() == 2,
              QStringLiteral("rows=%1").arg(tree != nullptr ? tree->topLevelItemCount() : -1));
        check("elab.dialog-modal", dialog != nullptr && dialog->isModal());
        if (dialog != nullptr) {
            dialog->hide();
        }
        notice.clear();
        check("elab.clear", !notice.noticeVisible());
    }

    // =======================================================================
    section("item 9 — diagnostic hover content + both fix branches");
    // =======================================================================
    {
        QVector<DiagnosticEntry> rows;
        DiagnosticEntry ranged = row(QStringLiteral("/p/a.sv"), 3, 1,
                                     QStringLiteral("no-trailing-spaces"),
                                     QStringLiteral("Remove trailing spaces."));
        ranged.column = 10;
        ranged.endColumn = 13;
        rows.append(ranged);
        rows.append(row(QStringLiteral("/p/a.sv"), 8, 2, QStringLiteral("line-length"),
                        QStringLiteral("Line too long.")));
        DiagnosticEntry wholeLine = row(QStringLiteral("/p/a.sv"), 12, 2,
                                        QStringLiteral("explicit-param-type"),
                                        QStringLiteral("No end position from the producer."));
        wholeLine.column = 0;
        wholeLine.endColumn = 0;
        rows.append(wholeLine);

        check("hover.range-hit", diagnosticAt(rows, 3, 11) == &rows.at(0));
        check("hover.range-miss", diagnosticAt(rows, 3, 4) == nullptr);
        check("hover.whole-line", diagnosticAt(rows, 12, 40) == &rows.at(2),
              QStringLiteral("a row without an end position owns its whole line"));

        const HoverContent fixable = describeDiagnostic(rows.at(0), true, QString());
        check("hover.fixable-content",
              fixable.found && fixable.fixable
                  && fixable.title.contains(QStringLiteral("no-trailing-spaces"))
                  && fixable.title.contains(QStringLiteral("Error"))
                  && fixable.body == QStringLiteral("Remove trailing spaces."),
              fixable.title);
        const HoverContent notFixable =
            describeDiagnostic(rows.at(1), false, QStringLiteral("line-length has no autofix"));
        check("hover.not-fixable-content", !notFixable.fixable
                  && notFixable.title.contains(QStringLiteral("Warning"))
                  && notFixable.tooltip.contains(QStringLiteral("line-length has no autofix")),
              notFixable.tooltip);

        DiagnosticHoverPopup popup;
        popup.show();
        popup.setContent(fixable);
        check("hover.fixable-button", popup.hasFixButton() && !popup.fixButton()->isHidden());
        bool fixRequested = false;
        QObject::connect(&popup, &DiagnosticHoverPopup::fixRequested,
                         [&fixRequested]() { fixRequested = true; });
        popup.fixButton()->click();
        check("hover.fix-button-emits", fixRequested);
        popup.setContent(notFixable);
        check("hover.not-fixable-no-button", !popup.hasFixButton()
                  && (popup.fixButton() == nullptr || popup.fixButton()->isHidden()));
        check("hover.not-fixable-reason-visible",
              popup.textLabel()->text().contains(QStringLiteral("no autofix")),
              popup.textLabel()->text());
        popup.hide();
    }

    // =======================================================================
    section("item 9 — the engine's autofix (real verible-verilog-lint)");
    // =======================================================================
    {
        if (veribleLint.isEmpty() || !QFile::exists(veribleLint)) {
            skip("autofix.*", QStringLiteral("no verible-verilog-lint at '%1'").arg(veribleLint));
        } else {
            AutofixEngine engine(veribleLint);
            const QString text = QStringLiteral("module a;\n  logic x;   \nendmodule\n");
            const AutofixOutcome fixed =
                engine.probe(QStringLiteral("no-trailing-spaces"), QStringLiteral("/p/a.sv"), text);
            check("autofix.probe-fixable", fixed.ok() && fixed.fixedText != text,
                  QStringLiteral("attempted=%1 supported=%2 error='%3'")
                      .arg(fixed.attempted)
                      .arg(fixed.supported)
                      .arg(fixed.error));
            check("autofix.trailing-space-gone",
                  fixed.fixedText.contains(QStringLiteral("logic x;\n")),
                  fixed.fixedText.left(60).replace(QLatin1Char('\n'), QLatin1Char('|')));

            const QString longLine =
                QStringLiteral("module a;\n  // ") + QString(120, QLatin1Char('x'))
                + QStringLiteral("\nendmodule\n");
            const AutofixOutcome notFixed =
                engine.probe(QStringLiteral("line-length"), QStringLiteral("/p/a.sv"), longLine);
            check("autofix.probe-not-fixable", !notFixed.supported,
                  QStringLiteral("error='%1'").arg(notFixed.error));

            // The cache answers the same question without a second process.
            const quint64 before = engine.probeCount();
            engine.probe(QStringLiteral("no-trailing-spaces"), QStringLiteral("/p/a.sv"), text);
            check("autofix.cache-hit", engine.probeCount() == before,
                  QStringLiteral("probes=%1").arg(engine.probeCount()));
        }
    }

    // =======================================================================
    section("R3 — the save seam fires on success and only then");
    // =======================================================================
    {
        QTemporaryDir dir;
        const QString file = projectFile(dir, QStringLiteral("saved.sv"),
                                         QStringLiteral("module saved;\nendmodule\n"));
        vedit::app::DemoEditorProvider provider; // declared first: outlives the area
        vedit::app::EditorArea area;
        area.setProvider(&provider);
        vedit::app::EditorPane *pane = area.ensurePane();
        check("save.seam-pane", pane != nullptr);
        QStringList saved;
        bool lastWasSaveAs = true;
        area.setOnDocumentSaved([&saved, &lastWasSaveAs](const QString &path, bool saveAs) {
            saved.append(path);
            lastWasSaveAs = saveAs;
        });
        QString openError;
        check("save.seam-open", pane != nullptr && pane->openFile(file, &openError), openError);
        QString saveError;
        check("save.seam-save", pane != nullptr && pane->saveFile(&saveError), saveError);
        check("save.seam-fired", saved == QStringList{file} && !lastWasSaveAs,
              QStringLiteral("calls=%1 path='%2' saveAs=%3")
                  .arg(saved.size())
                  .arg(saved.isEmpty() ? QString() : saved.first())
                  .arg(lastWasSaveAs ? 1 : 0));
        // Save As fires too, with the saveAs flag.
        const QString copy = dir.filePath(QStringLiteral("copy.sv"));
        check("save.seam-save-as", pane != nullptr && pane->saveAsFile(copy, &saveError), saveError);
        check("save.seam-save-as-flag", saved.size() == 2 && lastWasSaveAs
                  && saved.last() == copy,
              QStringLiteral("calls=%1 last='%2' saveAs=%3")
                  .arg(saved.size())
                  .arg(saved.isEmpty() ? QString() : saved.last())
                  .arg(lastWasSaveAs ? 1 : 0));
    }

    // =======================================================================
    section("B5 — a backend without renameProvider is greyed AND refuses");
    // =======================================================================
    {
        // 1) the UI gate reads the backend row.
        BackendCapabilities noRename;
        noRename.backend = QStringLiteral("slang");
        noRename.basis = QStringLiteral("measured");
        noRename.rows.append(CapabilityRow{
            QStringLiteral("rename"), 0,
            QStringLiteral("slang-server has no textDocument/rename")});
        const CapabilityDecision greyed =
            vedit::workspace::decideActionGate(noRename, QStringLiteral("rename"), false, false);
        check("rename.gate-greyed", !greyed.enabled && greyed.reason.contains(QStringLiteral("rename")),
              greyed.reason);

        // 2) the REQUEST path refuses too (not only the menu item).
        auto lsp = std::make_unique<FakeLsp>();
        FakeLsp *lspRaw = lsp.get();
        lspRaw->capabilities = noRename;
        auto slang = std::make_unique<FakeSlang>();
        DiagnosticsController controller(std::move(lsp), std::move(slang));
        controller.setConfiguration(QStringLiteral("/fake/ls"), QStringLiteral("/p"));
        const quint64 refused = controller.requestRename(QStringLiteral("/p/a.sv"), 1, 2,
                                                         QStringLiteral("x"));
        check("rename.request-refused", refused == 0 && !controller.lastError().isEmpty(),
              controller.lastError());
        check("rename.request-never-sent", lspRaw->renameRequests == 0);

        // 3) a backend that DOES rename still goes through.
        lspRaw->capabilities.rows[0].supported = 1;
        lspRaw->capabilities.rows[0].reason.clear();
        const quint64 allowed = controller.requestRename(QStringLiteral("/p/a.sv"), 1, 2,
                                                         QStringLiteral("x"));
        check("rename.request-allowed", allowed != 0 && lspRaw->renameRequests == 1);
        check("rename.keyword-scan", isVerilogKeyword(QStringLiteral("endmodule"))
                  && isVerilogKeyword(QStringLiteral("always_ff"))
                  && !isVerilogKeyword(QStringLiteral("w")));
    }

    // =======================================================================
    section("item 9 / ⑥ — autofix is ONE undoable edit (real KTextEditor)");
    // =======================================================================
    {
        if (veribleLint.isEmpty() || !QFile::exists(veribleLint)) {
            skip("autofix-undo.*", QStringLiteral("no verible-verilog-lint"));
        } else if (KTextEditor::Editor::instance() == nullptr) {
            skip("autofix-undo.*", QStringLiteral("no KTextEditor::Editor"));
        } else {
            KTextEditor::Editor *editor = KTextEditor::Editor::instance();
            KTextEditor::Document *document = editor->createDocument(nullptr);
            QWidget host;
            KTextEditor::View *view = document->createView(&host);
            const QString original = QStringLiteral("module a;\n  logic x;   \nendmodule\n");
            document->setText(original);
            document->setModified(false); // the doc represents the saved file
            check("autofix-undo.document-ready",
                  document->text() == original && !document->isModified());

            AutofixEngine engine(veribleLint);
            const AutofixOutcome outcome =
                engine.fix(QStringLiteral("no-trailing-spaces"), QStringLiteral("/p/a.sv"),
                           document->text());
            check("autofix-undo.engine-produced", outcome.ok());
            const bool applied = applyAutofixToDocument(document, outcome.fixedText);
            const QString fixed = document->text();
            check("autofix-undo.applied",
                  applied && fixed != original && fixed.contains(QStringLiteral("logic x;\n"))
                      && document->isModified(),
                  fixed.left(48).replace(QLatin1Char('\n'), QLatin1Char('|')));

            QAction *undo = findEditorAction(view, QStringLiteral("edit_undo"));
            check("autofix-undo.action", undo != nullptr);
            if (undo != nullptr) {
                undo->trigger();
                QCoreApplication::processEvents();
                // ONE undo restores the EXACT pre-fix text. (KTextEditor marks
                // a document modified after any undo, so isModified() is not the
                // criterion; the text and the line count are.)
                check("autofix-undo.one-undo-reverts",
                      document->text() == original && document->lines() == 4,
                      document->text().left(48).replace(QLatin1Char('\n'), QLatin1Char('|')));
            }
            delete view;
            delete document;
        }
    }

    // =======================================================================
    section("item 10 / ④ — the dot renders between the icon and the name");
    // =======================================================================
    {
        QTemporaryDir dir;
        const QString fileA = projectFile(dir, QStringLiteral("a.sv"), QStringLiteral("module a;\n"));
        const QString fileB = projectFile(dir, QStringLiteral("b.sv"), QStringLiteral("module b;\n"));
        const QString fileC = projectFile(dir, QStringLiteral("c.sv"), QStringLiteral("module c;\n"));
        QHash<QString, int> statuses;
        statuses.insert(fileA, FileStatusDotDelegate::Error);
        statuses.insert(fileB, FileStatusDotDelegate::Warning);
        statuses.insert(fileC, FileStatusDotDelegate::Clean);
        quint64 revision = 1;

        FileTreeModel model;
        model.setFileStatusSource(
            [&statuses](const QString &path) { return statuses.value(path, 0); },
            [&revision]() { return revision; });
        model.setRootPath(dir.path());
        model.ensureFullyLoaded(QModelIndex());
        model.ensureFullyLoaded(model.index(0, 0));
        check("dot.model-rows", model.rowCount(model.index(0, 0)) >= 3,
              QStringLiteral("rows=%1").arg(model.rowCount(model.index(0, 0))));

        const auto paintDot = [&model](const QString &path, FileStatusDotDelegate &delegate,
                                       QImage &image) -> QRect {
            const QModelIndex index = model.indexForPath(path, true);
            image = QImage(260, 24, QImage::Format_ARGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            QStyleOptionViewItem option;
            option.rect = QRect(0, 0, 260, 24);
            option.state = QStyle::State_Enabled;
            option.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
            option.font = QApplication::font();
            option.fontMetrics = QFontMetrics(option.font);
            delegate.paint(&painter, option, index);
            painter.end();
            return delegate.lastDotRect();
        };

        FileStatusDotDelegate delegate;
        QImage image;
        const QRect redRect = paintDot(fileA, delegate, image);
        const QColor red = image.pixelColor(redRect.center());
        check("dot.red", red == QColor(0xd3, 0x2f, 0x2f),
              QStringLiteral("sampled=%1").arg(red.name()));
        check("dot.after-icon", redRect.left() > 0 && redRect.left() < delegate.lastTextRect().left(),
              QStringLiteral("dot=%1..%2 text=%3")
                  .arg(redRect.left())
                  .arg(redRect.right())
                  .arg(delegate.lastTextRect().left()));
        const QRect yellowRect = paintDot(fileB, delegate, image);
        const QColor yellow = image.pixelColor(yellowRect.center());
        check("dot.yellow", yellow == QColor(0xf9, 0xa8, 0x25),
              QStringLiteral("sampled=%1").arg(yellow.name()));
        const QRect greenRect = paintDot(fileC, delegate, image);
        const QColor green = image.pixelColor(greenRect.center());
        check("dot.green", green == QColor(0x2e, 0x7d, 0x32),
              QStringLiteral("sampled=%1").arg(green.name()));

        // The revision is what drives a repaint; an unchanged revision must not
        // re-emit (no project rescan every frame).
        check("dot.revision-first-sync", model.syncFileStatuses());
        check("dot.revision-idle", !model.syncFileStatuses());
        revision = 2;
        statuses.insert(fileA, FileStatusDotDelegate::Clean);
        check("dot.revision-change-repaints", model.syncFileStatuses()
                  && model.statusRepaints() == 2);
        const QRect repainted = paintDot(fileA, delegate, image);
        check("dot.repainted-green",
              image.pixelColor(repainted.center()) == QColor(0x2e, 0x7d, 0x32),
              QStringLiteral("sampled=%1").arg(image.pixelColor(repainted.center()).name()));
    }

    // =======================================================================
    std::printf("\nSUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
