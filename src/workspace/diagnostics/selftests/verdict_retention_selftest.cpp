// RM-9 (T20) — per-file verdict retention, the visible "not analysed" state
// and the edit invalidation. Fakes only, no GUI, no Rust service, no toolchain:
// this target builds in the lane's standalone configure too, so the acceptance
// property is reproducible without the product shell.
//
// User report this encodes: "刚刚说的点不是'打开过才有'，是'正在打开的文件'才有"
// (the dot appeared only for the document that was being displayed). The old
// controller kept the lower rows of ONE document and dropped them when another
// file was opened, so a file lost its verdict the moment it was not the active
// tab. Every check below fails on that behaviour.

#include "diagnostics_controller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <cstdio>
#include <memory>

using namespace vedit::workspace::diagnostics;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.isEmpty() ? "" : "  ", detail.isEmpty() ? "" : detail.toUtf8().constData());
}

/// A session that answers diagnostics per file. `rowsByFile` empty = the file
/// was analysed clean (a green verdict). `statusCallsBeforeReady` > 0 keeps the
/// session Reparsing, which is how the "not analysed yet" state is observed.
class RetentionFakeLsp final : public LspSessionBackend {
public:
    QHash<QString, QVector<DiagnosticEntry>> rowsByFile;
    int statusCallsBeforeReady = 0;
    mutable int statusCalls = 0;
    int startCount = 0;
    QStringList startedFiles;

    struct Ticket {
        quint64 id = 0;
        QString file;
    };
    QVector<Ticket> tickets;

    bool start(const QString &, const QString &, const QString &, const QStringList &files,
               QString *) override
    {
        ++startCount;
        startedFiles = files;
        statusCalls = 0;
        tickets.clear();
        return true;
    }
    void stop() override {}
    SessionSnapshot status() const override
    {
        SessionSnapshot snapshot;
        snapshot.filesExpected = startedFiles.size();
        ++statusCalls;
        if (statusCalls > statusCallsBeforeReady) {
            snapshot.state = BackendState::Ready;
            snapshot.filesOpened = snapshot.filesExpected;
            snapshot.message = QStringLiteral("fake session ready");
        } else {
            snapshot.state = BackendState::Reparsing;
            snapshot.message = QStringLiteral("fake session starting");
        }
        return snapshot;
    }
    quint64 requestDefinition(const QString &, int, int) override { return 0; }
    quint64 requestReferences(const QString &, int, int) override { return 0; }
    quint64 requestSymbols(const QString &) override { return 0; }
    quint64 requestRename(const QString &, int, int, const QString &) override { return 0; }
    quint64 requestFormat(const QString &) override { return 0; }
    quint64 requestDiagnostics(const QString &file) override
    {
        Ticket ticket;
        ticket.id = static_cast<quint64>(tickets.size()) + 1;
        ticket.file = file;
        tickets.append(ticket);
        return ticket.id;
    }
    LspPolled poll(quint64 requestId) override
    {
        if (requestId == 0 || requestId > static_cast<quint64>(tickets.size())) {
            return LspPolled::failure(QStringLiteral("unknown fake request id"));
        }
        const QString file = tickets.at(static_cast<int>(requestId) - 1).file;
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = ResponseKind::Diagnostics;
        polled.diagnostics = rowsByFile.value(file);
        return polled;
    }
    BackendCapabilities staticCapabilities() const override { return BackendCapabilities{}; }
    QString backendName() const override { return QStringLiteral("retention-fake-lsp"); }
};

/// No lint leg: this binary proves the LSP-derived verdict store, not the CLI
/// pass (the product e2e covers the pass).
class RetentionFakeSlang final : public SlangBackend {
public:
    bool available() const override { return false; }
    QString description() const override { return QStringLiteral("unavailable fake slang"); }
    int startLint(const QString &, QString *error) override
    {
        if (error != nullptr) {
            *error = QStringLiteral("fake slang is unavailable");
        }
        return 0;
    }
    LspPolled pollLint(int) override { return LspPolled::failure(QStringLiteral("no lint")); }
};

DiagnosticEntry diagnostic(int severity, const QString &code)
{
    DiagnosticEntry entry;
    entry.severity = severity;
    entry.code = code;
    entry.message = code;
    return entry;
}

/// Pump the controller's own poll until the session settles (the GUI never
/// blocks on it either).
void settle(DiagnosticsController &controller)
{
    QElapsedTimer timer;
    timer.start();
    while (controller.state() == BackendState::Reparsing && timer.elapsed() < 3000) {
        controller.refresh();
        QCoreApplication::processEvents();
    }
    controller.refresh();
}

int statusInt(const DiagnosticsController &controller, const QString &file)
{
    return static_cast<int>(controller.statusForFile(file));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // -----------------------------------------------------------------------
    // (a) A verdict belongs to the FILE, not to the focused document.
    // -----------------------------------------------------------------------
    {
        QTemporaryDir dir;
        const QString a = dir.filePath(QStringLiteral("a.sv"));
        const QString b = dir.filePath(QStringLiteral("b.sv"));
        auto lsp = std::make_unique<RetentionFakeLsp>();
        RetentionFakeLsp *fake = lsp.get();
        fake->rowsByFile.insert(a, QVector<DiagnosticEntry>{diagnostic(1, QStringLiteral("error-a"))});
        fake->rowsByFile.insert(b, QVector<DiagnosticEntry>{}); // clean
        DiagnosticsController controller(std::move(lsp), std::make_unique<RetentionFakeSlang>());
        controller.setConfiguration(QStringLiteral("/fake/verible-ls"), dir.path());

        check("rm9.analyze-a", controller.analyzeFile(a, QStringLiteral("module a;\n"), false));
        settle(controller);
        check("rm9.a-error-before-switch",
              controller.statusForFile(a) == DiagnosticsController::FileStatus::Error,
              QString::number(statusInt(controller, a)));

        check("rm9.analyze-b", controller.analyzeFile(b, QStringLiteral("module b;\n"), false));
        settle(controller);
        check("rm9.b-clean-after-analysis",
              controller.statusForFile(b) == DiagnosticsController::FileStatus::Clean,
              QString::number(statusInt(controller, b)));
        // THE defect: before the retained store, A answered Unknown here —
        // "only the file being opened has a dot".
        check("rm9.a-retained-after-switch",
              controller.statusForFile(a) == DiagnosticsController::FileStatus::Error,
              QString::number(statusInt(controller, a)));
        check("rm9.a-retained-in-map", controller.fileStatuses().value(a, 0) == 1,
              QString::number(controller.fileStatuses().value(a, -1)));
        check("rm9.a-retained-rows-feed-merged",
              controller.mergedDiagnosticsForFile(a).size() == 1,
              QString::number(controller.mergedDiagnosticsForFile(a).size()));
        check("rm9.count-follows-retained", controller.session().diagnostics == 1,
              QStringLiteral("count=%1").arg(controller.session().diagnostics));

        check("rm9.analyze-a-again", controller.analyzeFile(a, QStringLiteral("module a;\n"), false));
        settle(controller);
        check("rm9.a-still-error-after-return",
              controller.statusForFile(a) == DiagnosticsController::FileStatus::Error,
              QString::number(statusInt(controller, a)));
    }

    // -----------------------------------------------------------------------
    // (b) Every file of the project set gets a verdict, opened or not.
    // -----------------------------------------------------------------------
    {
        QTemporaryDir dir;
        const QString a = dir.filePath(QStringLiteral("a.sv"));
        const QString c = dir.filePath(QStringLiteral("c.sv"));
        const QString d = dir.filePath(QStringLiteral("d.sv"));
        auto lsp = std::make_unique<RetentionFakeLsp>();
        RetentionFakeLsp *fake = lsp.get();
        fake->rowsByFile.insert(a, QVector<DiagnosticEntry>{diagnostic(1, QStringLiteral("error-a"))});
        fake->rowsByFile.insert(c, QVector<DiagnosticEntry>{diagnostic(2, QStringLiteral("warn-c"))});
        fake->rowsByFile.insert(d, QVector<DiagnosticEntry>{});
        DiagnosticsController controller(std::move(lsp), std::make_unique<RetentionFakeSlang>());
        controller.setConfiguration(QStringLiteral("/fake/verible-ls"), dir.path());
        controller.setFileSet(QStringList{a, c, d}); // none of the three is opened
        settle(controller);
        check("rm9.project-every-file-verdict",
              controller.fileStatuses().value(a, 0) == 1
                  && controller.fileStatuses().value(c, 0) == 2
                  && controller.fileStatuses().value(d, 0) == 3,
              QStringLiteral("a=%1 c=%2 d=%3")
                  .arg(controller.fileStatuses().value(a, -1))
                  .arg(controller.fileStatuses().value(c, -1))
                  .arg(controller.fileStatuses().value(d, -1)));
        check("rm9.project-no-file-left-as-no-verdict", controller.fileStatuses().size() == 3,
              QStringLiteral("size=%1").arg(controller.fileStatuses().size()));
        check("rm9.project-never-opened-not-unknown",
              controller.statusForFile(c) != DiagnosticsController::FileStatus::Unknown);
    }

    // -----------------------------------------------------------------------
    // (3) The "no verdict yet" state is VISIBLE and distinct from clean.
    // -----------------------------------------------------------------------
    {
        QTemporaryDir dir;
        const QString slowA = dir.filePath(QStringLiteral("slow-a.sv"));
        const QString slowB = dir.filePath(QStringLiteral("slow-b.sv"));
        auto lsp = std::make_unique<RetentionFakeLsp>();
        RetentionFakeLsp *fake = lsp.get();
        fake->statusCallsBeforeReady = 100000; // never becomes ready
        DiagnosticsController controller(std::move(lsp), std::make_unique<RetentionFakeSlang>());
        controller.setConfiguration(QStringLiteral("/fake/verible-ls"), dir.path());
        controller.setFileSet(QStringList{slowA, slowB});
        controller.refresh();
        check("rm9.pending-a-visible",
              controller.statusForFile(slowA) == DiagnosticsController::FileStatus::NotAnalyzed,
              QString::number(statusInt(controller, slowA)));
        check("rm9.pending-b-in-map", controller.fileStatuses().value(slowB, 0) == 4,
              QString::number(controller.fileStatuses().value(slowB, -1)));
        check("rm9.pending-is-not-clean",
              controller.statusForFile(slowA) != DiagnosticsController::FileStatus::Clean);
        check("rm9.outside-set-still-unknown",
              controller.statusForFile(dir.filePath(QStringLiteral("not-a-project-file.sv")))
                  == DiagnosticsController::FileStatus::Unknown);
    }

    // -----------------------------------------------------------------------
    // (d) + ruling 1: an edit voids the verdict (never a green dot on a voided
    // verdict); a FRESH analysis of the dirty buffer wins.
    // -----------------------------------------------------------------------
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("e.sv"));
        auto lsp = std::make_unique<RetentionFakeLsp>();
        RetentionFakeLsp *fake = lsp.get();
        fake->rowsByFile.insert(file, QVector<DiagnosticEntry>{}); // clean
        DiagnosticsController controller(std::move(lsp), std::make_unique<RetentionFakeSlang>());
        controller.setConfiguration(QStringLiteral("/fake/verible-ls"), dir.path());
        check("rm9.e-analyze", controller.analyzeFile(file, QStringLiteral("module e;\n"), false));
        settle(controller);
        check("rm9.e-clean-before-edit",
              controller.statusForFile(file) == DiagnosticsController::FileStatus::Clean,
              QString::number(statusInt(controller, file)));
        controller.markFileEdited(file);
        check("rm9.e-not-green-after-edit",
              controller.statusForFile(file) == DiagnosticsController::FileStatus::NotAnalyzed,
              QString::number(statusInt(controller, file)));
        check("rm9.e-map-says-not-analysed", controller.fileStatuses().value(file, 0) == 4,
              QString::number(controller.fileStatuses().value(file, 0)));
        check("rm9.e-merged-empty-after-edit",
              controller.mergedDiagnosticsForFile(file).isEmpty(),
              QString::number(controller.mergedDiagnosticsForFile(file).size()));
        check("rm9.e-analyze-dirty",
              controller.analyzeFile(file, QStringLiteral("module e;\n  logic x;\nendmodule\n"),
                                     true));
        settle(controller);
        check("rm9.e-dirty-fresh-verdict-wins",
              controller.statusForFile(file) == DiagnosticsController::FileStatus::Clean,
              QString::number(statusInt(controller, file)));
    }

    std::printf("VE-VERDICT-RETENTION-SELFTEST checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
