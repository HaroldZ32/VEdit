// Headless self-test of the diagnostics lane (no display, no Verible needed).
//
// It injects fakes through the two backend seams and asserts the acceptance
// properties that CAN be proven without a screen:
//   * A3c — the LSP session and the SLANG backend receive the same effective
//     file set, and the report says so;
//   * A3d — changing the file set advances the generation and the observable
//     state goes Reparsing -> Ready with the new counts;
//   * the five navigation entries (definition, references, symbols, rename,
//     format) answer with typed results and the outline is updated;
//   * a missing/broken backend degrades to a diagnosable state, never a crash.
//
// The real-server proof (verible-verilog-ls through the C ABI) lives in
// diagnostics_ffi_selftest.cpp; it SKIPs explicitly when the binary is absent.

#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "diagnostics_panel.h"
#include "diagnostics_outline_model.h"
#include "autofix.h"
#include "badge_text.h"
#include "realtime_file_scope.h"
#include "toolchain.h"
#include "waiver_actions.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <QtTest/QTest>

#include <cstdio>
#include <memory>

using namespace vedit::workspace::diagnostics;
// T14/AC-3.1: the badge facts live in the lane's entry-point namespace (they are
// the `BackendSwitchState` contract, not controller types).
using vedit::workspace::BackendSwitchState;
using vedit::workspace::BadgeReasonCode;
using vedit::workspace::BadgeState;
using vedit::workspace::badgeReasonCodeName;
using vedit::workspace::DiagnosticsBackendSettings;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n",
                ok ? "PASS" : "FAIL",
                name,
                detail.isEmpty() ? "" : "  ",
                detail.isEmpty() ? "" : detail.toUtf8().constData());
}

class FakeLsp final : public LspSessionBackend {
public:
    QStringList startedFiles;
    int startCount = 0;
    int statusCallsBeforeReady = 0; // 0 => ready on the first status()
    bool failStart = false;
    mutable int statusCalls = 0;
    SessionSnapshot snapshot;

    struct Recorded {
        quint64 id = 0;
        ResponseKind kind = ResponseKind::None;
        QString file;
        int line = 0;
        int character = 0;
        QString newName;
    };
    QVector<Recorded> requests;

    bool start(const QString &,
               const QString &,
               const QString &,
               const QStringList &files,
               QString *error) override
    {
        ++startCount;
        startedFiles = files;
        statusCalls = 0;
        snapshot = SessionSnapshot{};
        snapshot.filesExpected = files.size();
        snapshot.message = QStringLiteral("fake session starting");
        if (failStart) {
            if (error != nullptr) {
                *error = QStringLiteral("fake LSP refused to start (missing binary)");
            }
            return false;
        }
        return true;
    }

    void stop() override { snapshot.state = BackendState::Idle; }

    SessionSnapshot status() const override
    {
        SessionSnapshot copy = snapshot;
        if (!failStart) {
            ++statusCalls;
            if (statusCalls > statusCallsBeforeReady) {
                copy.state = BackendState::Ready;
                copy.filesOpened = copy.filesExpected;
                copy.message = QStringLiteral("fake session ready");
            } else {
                copy.state = BackendState::Reparsing;
                copy.message = QStringLiteral("fake session starting");
            }
        }
        return copy;
    }

    // B5 repair: the request path now refuses a NULL/unknown capability row (the
    // captain's ruling), so a fake that expects its requests to reach the server
    // must advertise the measured table it stands for. Before the repair the
    // controller passed a no-table backend through.
    BackendCapabilities staticCapabilities() const override
    {
        BackendCapabilities capabilities;
        capabilities.backend = QStringLiteral("fake-lsp");
        capabilities.basis = QStringLiteral("measured");
        capabilities.rows.append(CapabilityRow{QStringLiteral("definition"), 1, QString()});
        capabilities.rows.append(CapabilityRow{QStringLiteral("references"), 1, QString()});
        capabilities.rows.append(CapabilityRow{QStringLiteral("documentSymbol"), 1, QString()});
        capabilities.rows.append(CapabilityRow{QStringLiteral("rename"), 1, QString()});
        capabilities.rows.append(CapabilityRow{QStringLiteral("formatting"), 1, QString()});
        capabilities.rows.append(CapabilityRow{QStringLiteral("hover"), 1, QString()});
        return capabilities;
    }
    BackendCapabilities sessionCapabilities() const override { return staticCapabilities(); }
    QString backendName() const override { return QStringLiteral("fake-lsp"); }

    // T14/AC-3.1: the badge regression needs the two halves of a backend switch
    // (a refused slang selection and an accepted one). The base class refuses
    // every non-Verible kind, which is the honest default for a fake Verible
    // session; this flag opens the accepted branch.
    bool acceptSlang = false;
    bool selectBackend(LspBackendKind kind,
                       const QString &,
                       const QStringList &,
                       const QStringList &,
                       QString *error) override
    {
        if (kind == LspBackendKind::Verible || acceptSlang) {
            return true;
        }
        if (error != nullptr) {
            *error = QStringLiteral("fake LSP refuses the slang-server backend");
        }
        return false;
    }

    quint64 request(ResponseKind kind, const QString &file, int line, int character,
                    const QString &newName)
    {
        if (failStart) {
            return 0;
        }
        Recorded recorded;
        recorded.id = requests.size() + 1;
        recorded.kind = kind;
        recorded.file = file;
        recorded.line = line;
        recorded.character = character;
        recorded.newName = newName;
        requests.append(recorded);
        return recorded.id;
    }

    quint64 requestDefinition(const QString &file, int line, int character) override
    {
        return request(ResponseKind::Locations, file, line, character, QString());
    }
    quint64 requestReferences(const QString &file, int line, int character) override
    {
        return request(ResponseKind::Locations, file, line, character, QString());
    }
    quint64 requestSymbols(const QString &file) override
    {
        return request(ResponseKind::Symbols, file, 0, 0, QString());
    }
    quint64 requestRename(const QString &file, int line, int character,
                          const QString &newName) override
    {
        return request(ResponseKind::Edits, file, line, character, newName);
    }
    quint64 requestFormat(const QString &file) override
    {
        return request(ResponseKind::Edits, file, 0, 0, QString());
    }
    quint64 requestDiagnostics(const QString &file) override
    {
        return request(ResponseKind::Diagnostics, file, 0, 0, QString());
    }
    quint64 requestHover(const QString &file, int line, int character) override
    {
        return request(ResponseKind::Hover, file, line, character, QString());
    }
    quint64 requestSignatureHelp(const QString &file, int line, int character) override
    {
        return request(ResponseKind::SignatureHelp, file, line, character, QString());
    }

    // T118: the editor buffer path. The fake records what the controller pushed,
    // and bumps the diagnostics generation like a server that just republished.
    //
    // batch-20261117 (AC-4): a real server publishes ASYNCHRONOUSLY, so a
    // diagnostics poll after a didChange can still answer the PRE-edit rows.
    // `stalePublishPolls` models that window: the next N diagnostics polls answer
    // `staleMessage` without moving the diagnostics generation; a later poll
    // publishes `freshMessage` and bumps the generation - exactly the evidence the
    // controller's settle gate has to observe. `stalePendingPolls` makes the first
    // N stale polls answer PENDING (an asynchronous request) so the no-publish
    // timeout path can be driven deterministically.
    int stalePublishPolls = 0;
    int stalePendingPolls = 0;
    QString staleMessage = QStringLiteral("stale pre-edit diagnostic");
    QString freshMessage = QStringLiteral("fake LSP diagnostic");
    int stalePollsLeft = 0;
    bool freshPublished = true;

    bool changeBuffer(const QString &file, const QString &text, QString *error) override
    {
        if (failChange) {
            if (error != nullptr) {
                *error = QStringLiteral("fake LSP cannot follow buffers");
            }
            return false;
        }
        ++changeCount;
        changedFile = file;
        changedText = text;
        // A publish that raced in with the didChange moved the generation while
        // the payload is still the pre-edit set (the hazard the floor guards).
        ++snapshot.generation;
        stalePollsLeft = stalePublishPolls;
        freshPublished = false;
        return true;
    }
    int changeCount = 0;
    QString changedFile;
    QString changedText;
    bool failChange = false;

    LspPolled poll(quint64 requestId) override
    {
        if (requestId == 0 || requestId > static_cast<quint64>(requests.size())) {
            return LspPolled::failure(QStringLiteral("unknown fake request id"));
        }
        const Recorded &recorded = requests.at(static_cast<int>(requestId) - 1);
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = recorded.kind;
        switch (recorded.kind) {
        case ResponseKind::Locations: {
            LspLocation location;
            location.uri = QStringLiteral("file:///fixture/rtl/alu.sv");
            location.startLine = 3;
            polled.locations.append(location);
            break;
        }
        case ResponseKind::Symbols: {
            OutlineSymbol top;
            top.name = QStringLiteral("top");
            top.detail = QStringLiteral("module");
            top.kind = 2; // SymbolKind.Module
            top.line = 4;
            polled.symbols.append(top);
            OutlineSymbol alu;
            alu.name = QStringLiteral("u_alu");
            alu.detail = QStringLiteral("alu");
            alu.kind = 13; // SymbolKind.Variable
            alu.line = 20;
            polled.symbols.append(alu);
            break;
        }
        case ResponseKind::Edits: {
            LspTextEdit edit;
            edit.uri = QStringLiteral("file:///fixture/rtl/top.sv");
            edit.newText = QStringLiteral("alu_renamed");
            polled.edits.append(edit);
            break;
        }
        case ResponseKind::Diagnostics: {
            // batch-20261117 (AC-4): stale-then-fresh publication. The server
            // answers the snapshot it currently holds; only the publish that
            // carries the post-didChange rows moves the generation.
            if (stalePollsLeft > 0) {
                if (stalePendingPolls > 0) {
                    --stalePendingPolls;
                    return LspPolled::pendingResult();
                }
                --stalePollsLeft;
                DiagnosticEntry entry;
                entry.severity = 2;
                entry.code = QStringLiteral("stale-snapshot");
                entry.message = staleMessage;
                polled.diagnostics.append(entry);
                break;
            }
            if (!freshPublished) {
                freshPublished = true;
                ++snapshot.generation; // the publish of the post-edit rows
            }
            DiagnosticEntry entry;
            entry.severity = 2;
            entry.code = QStringLiteral("fresh-snapshot");
            entry.message = freshMessage;
            polled.diagnostics.append(entry);
            break;
        }
        case ResponseKind::Hover: {
            // T16: the fake answers a hover so the controller pass-through can be
            // asserted without a server.
            polled.hoverText = QStringLiteral("logic [7:0] data");
            break;
        }
        case ResponseKind::SignatureHelp: {
            LspSignature signature;
            signature.label = QStringLiteral("foo(int a, int b)");
            signature.parameters = QStringList{QStringLiteral("int a"), QStringLiteral("int b")};
            polled.signatures.append(signature);
            polled.activeSignature = 0;
            polled.activeParameter = 1;
            break;
        }
        case ResponseKind::None:
            break;
        }
        return polled;
    }
};

class FakeSlang final : public SlangBackend {
public:
    bool availableFlag = true;
    bool failLint = false;
    int pollsBeforeReady = 0;
    QStringList linted;
    mutable int pollCount = 0;

    bool available() const override { return availableFlag; }
    QString description() const override { return QStringLiteral("fake slang"); }

    int startLint(const QString &file, QString *error) override
    {
        if (!availableFlag) {
            if (error != nullptr) {
                *error = QStringLiteral("fake slang is unavailable");
            }
            return 0;
        }
        if (failLint) {
            if (error != nullptr) {
                *error = QStringLiteral("fake slang binary is missing");
            }
            return 0;
        }
        linted.append(file);
        pollCount = 0;
        return linted.size();
    }

    LspPolled pollLint(int ticket) override
    {
        if (ticket <= 0 || ticket > linted.size()) {
            return LspPolled::failure(QStringLiteral("unknown fake slang ticket"));
        }
        if (pollsBeforeReady > 0 && pollCount < pollsBeforeReady) {
            ++pollCount;
            return LspPolled::pendingResult();
        }
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = ResponseKind::Diagnostics;
        DiagnosticEntry entry;
        entry.line = 2;
        entry.severity = 1;
        entry.code = QStringLiteral("UndeclaredIdentifier");
        entry.message = QStringLiteral("fake slang diagnostic");
        polled.diagnostics.append(entry);
        return polled;
    }
};

QString makeExecutable(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    file.write("#!/bin/sh\nexit 0\n");
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}

/// Refresh until the controller reaches a terminal state or the budget ends.
BackendState refreshUntilTerminal(DiagnosticsController &controller, int maxIterations = 200)
{
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        controller.refresh();
        const BackendState state = controller.state();
        if (state != BackendState::Reparsing) {
            return state;
        }
    }
    return controller.state();
}

/// True when `rows` carries a row whose message is exactly `message` (the AC-4
/// race checks compare row sets by their messages).
bool hasRowMessage(const QVector<DiagnosticEntry> &rows, const QString &message)
{
    for (const DiagnosticEntry &row : rows) {
        if (row.message == message) {
            return true;
        }
    }
    return false;
}

/// The messages of one row set, for the CHECK detail line.
QString rowMessages(const QVector<DiagnosticEntry> &rows)
{
    QStringList messages;
    messages.reserve(rows.size());
    for (const DiagnosticEntry &row : rows) {
        messages.append(row.message);
    }
    return messages.isEmpty() ? QStringLiteral("<no rows>") : messages.join(QStringLiteral(" | "));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv); // the panel test needs a QApplication

    // ---- 1. batch-20261116 L1: the PROJECT set is not the realtime session --
    // The reported defect: setFileSet(project set) made the verible session
    // initialize over the WHOLE project, and every refresh() then requested
    // diagnostics for all of it. On a 20k-file project the server never became
    // Ready and the realtime lint produced zero rows. The regression drives the
    // PRODUCT order (project set first, then analyzeFile) and asserts the three
    // properties the fix owes: ONE realtime session over the realtime set, a
    // bounded request set, and rows for the current file.
    auto lsp = std::make_unique<FakeLsp>();
    auto slang = std::make_unique<FakeSlang>();
    auto *lspRaw = lsp.get();
    auto *slangRaw = slang.get();
    {
        DiagnosticsController controller(std::move(lsp), std::move(slang));
        controller.setConfiguration(QStringLiteral("/payload/verible-verilog-ls"),
                                    QStringLiteral("/fixture"));

        // 300 project files: under the old code every one of them was opened in
        // the session and requested from it on each tick. Only `top` and its
        // one-level submodule are the realtime closure.
        QStringList projectFiles;
        for (int index = 0; index < 300; ++index) {
            projectFiles.append(
                QStringLiteral("rtl/file%1.sv").arg(index, 3, 10, QLatin1Char('0')));
        }
        const QString top = QStringLiteral("rtl/top.sv");
        const QString sub = QStringLiteral("rtl/alu.sv");
        projectFiles.prepend(sub);
        projectFiles.prepend(top);
        // setRealtimeSessionFiles() sorts the union, so the session/request order
        // is the sorted one (the controller must not restart for order alone).
        const QStringList realtimeSet{sub, top};

        controller.setRealtimeSessionFiles(realtimeSet);
        controller.setFileSet(projectFiles);

        check("l1.project-set-recorded", controller.projectFiles() == projectFiles,
              QString::number(controller.projectFiles().size()));
        check("l1.project-set-starts-no-session",
              lspRaw->startCount == 0 && controller.lspStartCount() == 0,
              QStringLiteral("starts=%1").arg(lspRaw->startCount));
        check("l1.project-set-issues-no-request",
              lspRaw->requests.isEmpty() && controller.diagnosticsRequestCount() == 0,
              QString::number(lspRaw->requests.size()));
        check("l1.project-set-starts-no-slang", slangRaw->linted.isEmpty(),
              slangRaw->linted.join(QLatin1Char(',')));
        check("l1.project-set-not-the-session",
              controller.realtimeSessionSet().isEmpty()
                  && controller.fileSetReport().files.isEmpty());
        // The B1 recovery entry point must be a NO-OP while no document has been
        // analysed: a project set alone never opens a server (L1.1).
        controller.restartRealtimeSession();
        check("l1.restart-without-document-no-session",
              lspRaw->startCount == 0 && controller.lspStartCount() == 0,
              QStringLiteral("starts=%1").arg(lspRaw->startCount));

        // The product order: the project set has arrived, NOW the editor asks for
        // the current file. Exactly one session, over exactly the realtime set.
        const bool started = controller.analyzeFile(
            top, QStringLiteral("module top;\n  logic a b;\n  sub u_sub ();\nendmodule\n"), false);
        check("l1.analyze-starts-one-session",
              started && lspRaw->startCount == 1 && controller.lspStartCount() == 1,
              QStringLiteral("started=%1 starts=%2")
                  .arg(started ? 1 : 0)
                  .arg(lspRaw->startCount));
        check("l1.session-is-the-realtime-set", lspRaw->startedFiles == realtimeSet,
              lspRaw->startedFiles.join(QLatin1Char(',')));
        check("l1.session-not-the-project-set",
              lspRaw->startedFiles.size() <= 128
                  && !lspRaw->startedFiles.contains(QStringLiteral("rtl/file000.sv")),
              QStringLiteral("session=%1").arg(lspRaw->startedFiles.size()));
        check("l1.realtime-display-file", controller.realtimeDisplayFile() == top,
              controller.realtimeDisplayFile());

        refreshUntilTerminal(controller);
        check("l1.request-set-is-realtime",
              controller.lastDiagnosticsRequestFiles() == realtimeSet,
              controller.lastDiagnosticsRequestFiles().join(QLatin1Char(',')));
        check("l1.request-count-bounded",
              // Each batch is the realtime set; the buffer-settling window may
              // re-ask a bounded number of times (<= 8 ticks at the 300 ms poll).
              // The point is that the number is a multiple of the CLOSURE, never
              // of the 300-file project.
              controller.diagnosticsRequestCount()
                  <= static_cast<quint64>(realtimeSet.size() * 8),
              QString::number(controller.diagnosticsRequestCount()));
        int projectRequests = 0;
        int boundedRequests = 0;
        for (const FakeLsp::Recorded &recorded : lspRaw->requests) {
            if (recorded.kind != ResponseKind::Diagnostics) {
                continue;
            }
            if (!realtimeSet.contains(recorded.file)) {
                ++projectRequests;
            } else {
                ++boundedRequests;
            }
        }
        check("l1.no-project-diagnostics-request", projectRequests == 0,
              QStringLiteral("outside=%1").arg(projectRequests));
        check("l1.current-file-rows", !controller.lowerRowsForFile(top).isEmpty(),
              QStringLiteral("rows=%1").arg(controller.lowerRowsForFile(top).size()));

        // The OTHER startup order: the editor analysed first and the project file
        // set lands LATE (a real expansion takes seconds). It must not restart or
        // clear the live realtime session.
        const int startsBeforeLateProjectSet = lspRaw->startCount;
        controller.setFileSet(projectFiles + QStringList{QStringLiteral("rtl/late.sv")});
        controller.refresh();
        check("l1.late-project-set-keeps-session",
              lspRaw->startCount == startsBeforeLateProjectSet
                  && controller.realtimeSessionSet() == realtimeSet,
              QStringLiteral("starts=%1 set=%2")
                  .arg(lspRaw->startCount)
                  .arg(controller.realtimeSessionSet().join(QLatin1Char(','))));
        check("l1.late-project-set-keeps-rows",
              !controller.lowerRowsForFile(top).isEmpty(),
              QStringLiteral("rows=%1").arg(controller.lowerRowsForFile(top).size()));

        // ---- B1: a backend stop UNDER the controller must be recoverable -----
        // `BackendSwitchState::setProjectEnvironment()` and every settings apply
        // call `selectBackend()`, which stops the server unconditionally while
        // this controller's `lspStarted_`/`files_` stay stale. The explicit
        // restart is what brings the SAME realtime set back; without it
        // analyzeFile() reuses a dead session and the realtime LINT stays off.
        const quint64 startsBeforeForcedRestart = controller.lspStartCount();
        lspRaw->stop(); // exactly the backend state selectBackend() leaves behind
        controller.restartRealtimeSession();
        check("b1.restart-starts-session",
              controller.lspStartCount() == startsBeforeForcedRestart + 1
                  && lspRaw->startedFiles == realtimeSet,
              QStringLiteral("starts=%1 -> %2 set=%3")
                  .arg(static_cast<qulonglong>(startsBeforeForcedRestart))
                  .arg(static_cast<qulonglong>(controller.lspStartCount()))
                  .arg(lspRaw->startedFiles.join(QLatin1Char(','))));
        refreshUntilTerminal(controller);
        check("b1.restart-session-ready",
              controller.session().state == BackendState::Ready
                  && controller.realtimeSessionSet() == realtimeSet,
              QStringLiteral("state=%1 set=%2")
                  .arg(static_cast<int>(controller.session().state))
                  .arg(controller.realtimeSessionSet().join(QLatin1Char(','))));
        check("b1.restart-requests-realtime-set",
              controller.lastDiagnosticsRequestFiles() == realtimeSet,
              controller.lastDiagnosticsRequestFiles().join(QLatin1Char(',')));
        check("b1.restart-keeps-lower-rows", !controller.lowerRowsForFile(top).isEmpty(),
              QStringLiteral("rows=%1").arg(controller.lowerRowsForFile(top).size()));
        check("b1.restart-edit-reanalyzes",
              controller.analyzeFile(top, QStringLiteral("module top;\n  logic a b;\n"
                                                         "  sub u_sub ();\nendmodule\n"),
                                     true));
        refreshUntilTerminal(controller);
        check("b1.restart-edit-new-diagnostics",
              controller.lspStartCount() == startsBeforeForcedRestart + 1
                  && !controller.lowerRowsForFile(top).isEmpty(),
              QStringLiteral("starts=%1 rows=%2")
                  .arg(static_cast<qulonglong>(controller.lspStartCount()))
                  .arg(controller.lowerRowsForFile(top).size()));

        // The DISPLAY surface (Outline issue list) draws the current file's rows.
        {
            DiagnosticsPanel panel(&controller);
            panel.refreshFromController();
            check("l1.panel-shows-current-file-rows", panel.issueRowCount() >= 1,
                  QStringLiteral("panel=%1").arg(panel.issueRowCount()));
        }

        // A buffer edit inside the same session must reuse it; the request set
        // stays the realtime set (never a project-sized loop).
        // A buffer edit inside the same session must reuse it; the request set
        // stays the realtime set (never a project-sized loop). Compare against
        // the count BEFORE the edit: the B1 repair above legitimately restarted
        // the session once.
        const int startsBeforeBufferEdit = lspRaw->startCount;
        check("l1.buffer-edit-starts",
              controller.analyzeFile(top, QStringLiteral("module top;\n  logic a b;\n"
                                                         "  sub u_sub ();\nendmodule\n"),
                                     true));
        refreshUntilTerminal(controller);
        check("l1.buffer-edit-reuses-session", lspRaw->startCount == startsBeforeBufferEdit,
              QStringLiteral("before=%1 after=%2")
                  .arg(startsBeforeBufferEdit)
                  .arg(lspRaw->startCount));
        check("l1.buffer-edit-requests-bounded",
              controller.lastDiagnosticsRequestFiles() == realtimeSet,
              controller.lastDiagnosticsRequestFiles().join(QLatin1Char(',')));
        check("l1.buffer-edit-rows", !controller.lowerRowsForFile(top).isEmpty(),
              QStringLiteral("rows=%1").arg(controller.lowerRowsForFile(top).size()));

        // ---- 2. A3c/A3d: the manual reparse drives BOTH backends over the SAME
        // realtime set. The old guarantee survives on the set that owns it; the
        // project set is no longer a session input.
        const quint64 generationBefore = controller.generation();
        const int startsBeforeReparse = lspRaw->startCount;
        controller.reparse();
        check("a3d.generation-advanced", controller.generation() > generationBefore,
              QString::number(controller.generation()));
        check("a3d.reparse-restarts-session",
              lspRaw->startCount == startsBeforeReparse + 1,
              QStringLiteral("before=%1 after=%2")
                  .arg(startsBeforeReparse)
                  .arg(lspRaw->startCount));
        check("a3c.lsp-received-same-set",
              lspRaw->startCount == startsBeforeReparse + 1
                  && lspRaw->startedFiles == realtimeSet,
              lspRaw->startedFiles.join(QLatin1Char(',')));
        check("a3c.slang-received-same-set", slangRaw->linted == realtimeSet,
              slangRaw->linted.join(QLatin1Char(',')));
        const FileSetReport &report = controller.fileSetReport();
        check("a3c.report-marks-same-set",
              report.lspReceivedWholeSet && report.slangRequested == realtimeSet.size()
                  && report.slangCompleted == realtimeSet.size() && report.slangFailures == 0
                  && report.files == realtimeSet,
              QStringLiteral("lsp=%1 slang=%2/%3 files=%4")
                  .arg(report.lspFilesOpened)
                  .arg(report.slangCompleted)
                  .arg(report.slangRequested)
                  .arg(report.files.size()));
        check("a3d.state-ready-after-reparse",
              refreshUntilTerminal(controller) == BackendState::Ready,
              QString::number(static_cast<int>(controller.state())));
        check("l1.reparse-request-set-is-realtime",
              controller.lastDiagnosticsRequestFiles() == realtimeSet,
              controller.lastDiagnosticsRequestFiles().join(QLatin1Char(',')));

        // ---- 3. six entry points answer with typed results -----------------
        check("entry.definition",
              controller.requestDefinition(QStringLiteral("rtl/top.sv"), 10, 3) != 0);
        const LspPolled definition = controller.pollNavigation();
        check("entry.definition-result",
              definition.ready && definition.kind == ResponseKind::Locations
                  && !definition.locations.isEmpty());
        check("entry.references",
              controller.requestReferences(QStringLiteral("rtl/top.sv"), 10, 3) != 0);
        check("entry.references-result",
              controller.pollNavigation().kind == ResponseKind::Locations);
        check("entry.symbols", controller.requestSymbols(QStringLiteral("rtl/top.sv")) != 0);
        const LspPolled symbols = controller.pollNavigation();
        check("entry.symbols-result",
              symbols.ready && symbols.kind == ResponseKind::Symbols && !symbols.symbols.isEmpty());
        check("entry.symbols-updates-outline",
              controller.outline().size() == symbols.symbols.size()
                  && controller.outline().front().name == QStringLiteral("top"));
        check("entry.rename",
              controller.requestRename(QStringLiteral("rtl/top.sv"), 10, 3,
                                       QStringLiteral("alu_renamed")) != 0);
        const LspPolled rename = controller.pollNavigation();
        check("entry.rename-result",
              rename.ready && rename.kind == ResponseKind::Edits && !rename.edits.isEmpty());
        check("entry.format", controller.requestFormat(QStringLiteral("rtl/top.sv")) != 0);
        check("entry.format-result", controller.pollNavigation().kind == ResponseKind::Edits);
        // T16: the hover / signatureHelp pass-through (the fake answers both; the
        // real FFI path is the same one `requestHover` maps to).
        check("entry.hover", controller.requestHover(QStringLiteral("rtl/top.sv"), 10, 3) != 0);
        const LspPolled hover = controller.pollNavigation();
        check("entry.hover-result",
              hover.ready && hover.kind == ResponseKind::Hover
                  && hover.hoverText == QStringLiteral("logic [7:0] data"));
        check("entry.signature",
              controller.requestSignatureHelp(QStringLiteral("rtl/top.sv"), 10, 3) != 0);
        const LspPolled signature = controller.pollNavigation();
        check("entry.signature-result",
              signature.ready && signature.kind == ResponseKind::SignatureHelp
                  && signature.signatures.size() == 1
                  && signature.signatures.first().label == QStringLiteral("foo(int a, int b)")
                  && signature.activeParameter == 1);
        check("diagnostics.collected", !controller.diagnostics().isEmpty(),
              QString::number(controller.diagnostics().size()));
    }

    // ---- 4. SLANG unavailable: LSP keeps working, state is degraded --------
    {
        auto lsp2 = std::make_unique<FakeLsp>();
        auto slang2 = std::make_unique<FakeSlang>();
        slang2->availableFlag = false;
        DiagnosticsController controller(std::move(lsp2), std::move(slang2));
        controller.setConfiguration(QStringLiteral("/payload/verible-verilog-ls"),
                                    QStringLiteral("/fixture"));
        controller.setFileSet(QStringList{QStringLiteral("rtl/top.sv")});
        // L1.1: the project set alone starts nothing; the file analysis does.
        check("degrade.project-set-alone-starts-nothing",
              controller.lspStartCount() == 0 && controller.state() == BackendState::Idle);
        check("degrade.realtime-analysis-starts",
              controller.analyzeFile(QStringLiteral("rtl/top.sv"),
                                     QStringLiteral("module top;\nendmodule\n"), false));
        // The manual reparse is what engages the SLANG leg (the realtime file
        // scope is LSP-only), so this is the path that must report the missing
        // engine instead of pretending to be healthy.
        controller.reparse();
        refreshUntilTerminal(controller);
        check("degrade.slang-missing-is-observable",
              controller.state() == BackendState::Degraded && !controller.lastError().isEmpty(),
              controller.lastError());
        check("degrade.slang-missing-not-crash",
              controller.fileSetReport().slangAvailable == false);
    }

    // ---- 5. Verible missing: diagnosable failure, no crash -----------------
    {
        auto lsp3 = std::make_unique<FakeLsp>();
        lsp3->failStart = true;
        auto slang3 = std::make_unique<FakeSlang>();
        slang3->availableFlag = false;
        DiagnosticsController controller(std::move(lsp3), std::move(slang3));
        controller.setConfiguration(QStringLiteral("/missing/verible-verilog-ls"),
                                    QStringLiteral("/fixture"));
        controller.setFileSet(QStringList{QStringLiteral("rtl/top.sv")});
        check("degrade.lsp-missing-analysis-refused",
              !controller.analyzeFile(QStringLiteral("rtl/top.sv"),
                                      QStringLiteral("module top;\nendmodule\n"), false));
        refreshUntilTerminal(controller);
        check("degrade.lsp-missing-is-diagnosable",
              controller.state() == BackendState::Failed && !controller.lastError().isEmpty(),
              controller.lastError());
        check("degrade.lsp-missing-request-fails-typed",
              controller.requestDefinition(QStringLiteral("rtl/top.sv"), 0, 0) == 0);
    }

    // ---- 6. No backends at all: failed state with a message ----------------
    {
        DiagnosticsController controller(nullptr, nullptr);
        controller.setFileSet(QStringList{QStringLiteral("rtl/top.sv")});
        check("degrade.no-backend-analysis-refused",
              !controller.analyzeFile(QStringLiteral("rtl/top.sv"),
                                      QStringLiteral("module top;\nendmodule\n"), false));
        refreshUntilTerminal(controller);
        check("degrade.no-backend-is-diagnosable",
              controller.state() == BackendState::Failed && !controller.lastError().isEmpty());
    }

    // ---- batch-20261116 L1.2: every no-analysis path names its reason ------
    // The reported symptom: no squiggles, an empty Outline and NO hint at all.
    // A file this build cannot analyse (no analyzer, above the cap), or one the
    // active top scope excludes, must produce one explicit line.
    {
        auto lsp = std::make_unique<FakeLsp>();
        auto slang = std::make_unique<FakeSlang>();
        DiagnosticsController controller(std::move(lsp), std::move(slang));
        controller.setConfiguration(QStringLiteral("/payload/verible-verilog-ls"),
                                    QStringLiteral("/fixture"));
        DiagnosticsPanel panel(&controller);

        // (i) `.f` / `.md`: the watcher publishes the reason BEFORE it returns.
        const QString manifest = QStringLiteral("/fixture/project.files");
        controller.setDocumentAnalysisAvailability(
            manifest, false, QStringLiteral("no lint analyzer for this file type"));
        check("l1.reason-no-analyzer",
              controller.realtimeReason().contains(QStringLiteral("no lint analyzer")),
              controller.realtimeReason());
        check("l1.reason-no-analyzer-panel",
              panel.diagnosticsEmptyStateText().contains(QStringLiteral("no lint analyzer")),
              panel.diagnosticsEmptyStateText());
        check("l1.reason-no-analyzer-visible", panel.diagnosticsEmptyStateVisible());

        // (ii) an available document clears it (nothing sticks from the manifest).
        controller.setDocumentAnalysisAvailability(manifest, true, QString());
        check("l1.reason-clears", controller.realtimeReason().isEmpty(),
              controller.realtimeReason());

        // (iii) top-scope exclusion is NAMED with the top, not silently skipped.
        const QString top = QStringLiteral("/fixture/rtl/top.sv");
        const QString excluded = QStringLiteral("/fixture/rtl/mem.sv");
        controller.setTopScope(QStringLiteral("chip_top"), QStringList{top},
                               QStringList{excluded});
        check("l1.reason-excluded-analysis-starts",
              controller.analyzeFile(excluded, QStringLiteral("module mem;\nendmodule\n"),
                                     false));
        check("l1.reason-outside-top",
              controller.realtimeReason().contains(QStringLiteral("outside"))
                  && controller.realtimeReason().contains(QStringLiteral("chip_top")),
              controller.realtimeReason());
        check("l1.reason-outside-top-panel",
              panel.diagnosticsEmptyStateText().contains(QStringLiteral("chip_top")),
              panel.diagnosticsEmptyStateText());
        check("l1.reason-outside-top-not-blank",
              !panel.diagnosticsEmptyStateText().trimmed().isEmpty());
    }

    // ---- 7. Toolchain observability: bundled > PATH > env (T82) ------------
    {
        QTemporaryDir tools;
        const QString bundled = tools.path() + QStringLiteral("/bundled");
        const QString pathDir = tools.path() + QStringLiteral("/path");
        QDir().mkpath(bundled);
        QDir().mkpath(pathDir);
        const QString bundledLs = makeExecutable(bundled + QStringLiteral("/verible-verilog-ls"));
        const QString bundledSlang = makeExecutable(bundled + QStringLiteral("/slang"));
        const QString pathLs = makeExecutable(pathDir + QStringLiteral("/verible-verilog-ls"));
        const QString pathSlang = makeExecutable(pathDir + QStringLiteral("/slang"));

        ToolchainSearchContext context;
        context.bundledDirs = QStringList() << bundled;
        context.pathDirs = QStringList() << pathDir;
        context.env.insert(QStringLiteral("VEDIT_VERIBLE_LS"),
                           pathLs + QStringLiteral(".env"));
        context.env.insert(QStringLiteral("VEDIT_SLANG"), pathSlang + QStringLiteral(".env"));

        const ToolchainReport bundledReport = resolveToolchain(context);
        check("toolchain.bundled-wins",
              bundledReport.verible.path == bundledLs
                  && bundledReport.verible.sourceLabel() == QStringLiteral("bundled")
                  && bundledReport.slang.path == bundledSlang
                  && bundledReport.slang.sourceLabel() == QStringLiteral("bundled"),
              bundledReport.statusLine());

        ToolchainSearchContext pathContext = context;
        pathContext.bundledDirs = QStringList() << (tools.path() + QStringLiteral("/absent"));
        const ToolchainReport pathReport = resolveToolchain(pathContext);
        check("toolchain.path-before-env",
              pathReport.verible.path == pathLs
                  && pathReport.verible.sourceLabel() == QStringLiteral("PATH")
                  && pathReport.slang.path == pathSlang
                  && pathReport.slang.sourceLabel() == QStringLiteral("PATH"),
              pathReport.statusLine());

        ToolchainSearchContext envContext = pathContext;
        envContext.pathDirs = QStringList() << (tools.path() + QStringLiteral("/absent-path"));
        envContext.env.insert(QStringLiteral("VEDIT_VERIBLE_LS"), bundledLs);
        envContext.env.insert(QStringLiteral("VEDIT_SLANG"), bundledSlang);
        const ToolchainReport envReport = resolveToolchain(envContext);
        check("toolchain.env-is-last",
              envReport.verible.path == bundledLs
                  && envReport.verible.sourceLabel() == QStringLiteral("env:VEDIT_VERIBLE_LS")
                  && envReport.slang.sourceLabel() == QStringLiteral("env:VEDIT_SLANG"),
              envReport.statusLine());

        ToolchainSearchContext empty;
        empty.bundledDirs = QStringList() << (tools.path() + QStringLiteral("/absent"));
        const ToolchainReport missingReport = resolveToolchain(empty);
        check("toolchain.missing-list",
              missingReport.missing().contains(QStringLiteral("verible"))
                  && missingReport.missing().contains(QStringLiteral("slang"))
                  && missingReport.statusLine().contains(
                         QStringLiteral("missing=[verible, slang]")),
              missingReport.statusLine());
        check("toolchain.missing-message-visible",
              missingReport.userMessage().contains(QCoreApplication::translate("VEdit", "Diagnostics unavailable"))
                  && missingReport.userMessage().contains(QStringLiteral("Fix:")),
              missingReport.userMessage());
    }

    // ---- T118: the editor-buffer path and the panel's own polling ----------
    // Reported defect: deleting a comma in the GUI produced no diagnostic and an
    // empty Outline. The controller must (a) analyze the ACTIVE file without a
    // project, (b) push the BUFFER text into the session, and (c) the panel must
    // show the result without the user touching anything.
    {
        auto raw = std::make_unique<FakeLsp>();
        FakeLsp *fakeLsp = raw.get();
        auto slangRaw = std::make_unique<FakeSlang>();
        DiagnosticsController controller(std::move(raw), std::move(slangRaw));
        controller.setConfiguration(QStringLiteral("verible"), QStringLiteral("/fixture"));
        ToolchainReport found;
        found.verible.kind = ToolKind::Verible;
        found.verible.name = QStringLiteral("verible-verilog-ls");
        found.verible.path = QStringLiteral("/fixture/verible-verilog-ls");
        found.verible.source = ToolSource::Bundled;
        found.slang.kind = ToolKind::Slang;
        found.slang.name = QStringLiteral("slang");
        found.slang.path = QStringLiteral("/fixture/slang");
        found.slang.source = ToolSource::Bundled;
        controller.setToolchainReport(found);
        DiagnosticsPanel panel(&controller);
        const QString staleText = panel.statusText();

        const QString file = QStringLiteral("/fixture/rtl/top.sv");
        const QString brokenBuffer =
            QStringLiteral("module top;\n  logic a b;\nendmodule\n");
        const bool started = controller.analyzeFile(file, brokenBuffer, true);
        check("t118.analyze-starts", started,
              QStringLiteral("analysis count=%1").arg(controller.analysisCount()));
        check("t118.analyze-is-file-scope", fakeLsp->startedFiles == QStringList{file},
              fakeLsp->startedFiles.join(QLatin1Char(',')));
        check("t118.analyze-counted", controller.analysisCount() == 1,
              QString::number(controller.analysisCount()));

        // The panel's own 300 ms timer drives refresh(): no manual call here.
        QTest::qWait(1200);
        check("t118.buffer-pushed", fakeLsp->changeCount >= 1 && fakeLsp->changedFile == file
                                       && fakeLsp->changedText == brokenBuffer,
              QStringLiteral("changes=%1").arg(fakeLsp->changeCount));
        check("t118.panel-refreshed-before",
              !staleText.contains(QStringLiteral("analyzed")), staleText);
        check("t118.panel-analyzed", panel.statusText().contains(QStringLiteral("analyzed")),
              panel.statusText());
        check("t118.panel-source-is-buffer",
              panel.statusText().contains(QStringLiteral("not saved")), panel.statusText());
        check("t118.panel-counts-diagnostics",
              panel.statusText().contains(QStringLiteral("diagnostic(s)")), panel.statusText());
        check("t118.panel-outline-filled", panel.outlineRowCount() > 0,
              QString::number(panel.outlineRowCount()));

        // A backend that cannot follow a buffer must be reported, never silently
        // pretended: the analysis stays alive and the message is observable.
        fakeLsp->failChange = true;
        controller.analyzeFile(file, QStringLiteral("module top;\nendmodule\n"), false);
        QTest::qWait(600);
        check("t118.buffer-refusal-observable",
              controller.analyzeWasDirty() == false
                  && controller.state() != BackendState::Idle,
              QString::number(static_cast<int>(controller.state())));
    }

    // ---- AC-4 (batch-20261117): a pre-edit snapshot must not end the round ---
    // Reported defect: after an edit the FIRST completed poll answered the
    // server's CURRENT snapshot - still the pre-edit rows - the controller marked
    // the buffer "seen" and stopped re-asking, so the Outline froze on the old
    // line set until the next edit. The fake models the race deterministically:
    // the didChange bumps a generation while the payload is still the old set (a
    // publish that raced in with the edit), the next two polls answer those old
    // rows without moving it, and a later poll publishes `freshRow` (the
    // generation moves) and keeps answering it. The fixed controller drops the
    // stale poll, keeps the round open and reaches Ready on the fresh set; on the
    // pre-fix controller the stale poll ends the round, so the old row survives
    // and the fresh row never appears (those checks are red there).
    {
        auto lsp = std::make_unique<FakeLsp>();
        FakeLsp *fakeLsp = lsp.get();
        auto slang = std::make_unique<FakeSlang>();
        DiagnosticsController controller(std::move(lsp), std::move(slang));
        controller.setConfiguration(QStringLiteral("/fixture/verible-verilog-ls"),
                                    QStringLiteral("/fixture"));
        ToolchainReport found;
        found.verible.kind = ToolKind::Verible;
        found.verible.name = QStringLiteral("verible-verilog-ls");
        found.verible.path = QStringLiteral("/fixture/verible-verilog-ls");
        found.verible.source = ToolSource::Bundled;
        found.slang.kind = ToolKind::Slang;
        found.slang.name = QStringLiteral("slang");
        found.slang.path = QStringLiteral("/fixture/slang");
        found.slang.source = ToolSource::Bundled;
        controller.setToolchainReport(found);

        const QString file = QStringLiteral("/fixture/rtl/race.sv");
        const QString oldText = QStringLiteral("module race;\n  logic a b;\nendmodule\n");
        const QString newText = QStringLiteral("module race;\n  logic a;\nendmodule\n");

        // Warm-up: the first analysis opens the session, the manual reparse
        // engages the SLANG leg, so the AC-4 end state can be Ready (a file-scope
        // analysis alone is the LSP fast path and ends Degraded by design).
        controller.setRealtimeSessionFiles(QStringList{file});
        check("ac4.warmup-analyze",
              controller.analyzeFile(file, oldText, true) && fakeLsp->changedText == oldText,
              fakeLsp->changedText);
        refreshUntilTerminal(controller);
        controller.reparse();
        check("ac4.warmup-ready", refreshUntilTerminal(controller) == BackendState::Ready,
              QString::number(static_cast<int>(controller.state())));

        const QString staleRow = QStringLiteral("stale pre-edit diagnostic");
        const QString freshRow = QStringLiteral("fresh post-edit diagnostic");
        fakeLsp->staleMessage = staleRow;
        fakeLsp->freshMessage = freshRow;
        fakeLsp->stalePublishPolls = 2; // two stale polls, then the fresh publish

        check("ac4.edit-starts", controller.analyzeFile(file, newText, true));
        check("ac4.buffer-pushed",
              fakeLsp->changedFile == file && fakeLsp->changedText == newText,
              fakeLsp->changedText);

        // ONE refresh after the edit: the stale poll must be DROPPED, so the round
        // stays open and neither row set is rendered yet.
        controller.refresh();
        const QVector<DiagnosticEntry> afterOneRefresh = controller.lowerRowsForFile(file);
        check("ac4.round-stays-open", controller.state() == BackendState::Reparsing,
              QString::number(static_cast<int>(controller.state())));
        check("ac4.stale-row-not-rendered", !hasRowMessage(afterOneRefresh, staleRow),
              rowMessages(afterOneRefresh));
        check("ac4.fresh-row-not-rendered-yet", !hasRowMessage(afterOneRefresh, freshRow),
              rowMessages(afterOneRefresh));

        // The later publish is picked up by the re-requests: Ready on the fresh
        // rows, and the stale row never reaches the view.
        const BackendState settled = refreshUntilTerminal(controller);
        const QVector<DiagnosticEntry> afterSettle = controller.lowerRowsForFile(file);
        check("ac4.terminal-ready", settled == BackendState::Ready,
              QString::number(static_cast<int>(settled)));
        check("ac4.fresh-row-rendered", hasRowMessage(afterSettle, freshRow),
              rowMessages(afterSettle));
        check("ac4.stale-row-gone", !hasRowMessage(afterSettle, staleRow),
              rowMessages(afterSettle));

        // ---- AC-4 second half: no publish inside the bound -------------------
        // The server keeps answering the pre-edit set (its first stale polls
        // answer PENDING, like the real asynchronous request) and never
        // publishes. The round must settle on the server's current snapshot when
        // the existing 2500 ms bound expires instead of hanging in Reparsing, and
        // it must not have settled before the bound.
        fakeLsp->stalePublishPolls = 1000; // no fresh publish at all
        fakeLsp->stalePendingPolls = 2;
        check("ac4.timeout-edit-starts", controller.analyzeFile(file, oldText, true));
        controller.refresh();
        check("ac4.timeout-stays-open-inside-bound",
              controller.state() == BackendState::Reparsing,
              QString::number(static_cast<int>(controller.state())));
        QTest::qWait(2600); // cross the 2500 ms settle bound
        controller.refresh();
        const BackendState afterTimeout = controller.state();
        const QVector<DiagnosticEntry> timeoutRows = controller.lowerRowsForFile(file);
        check("ac4.timeout-settles-terminal",
              afterTimeout != BackendState::Reparsing && afterTimeout != BackendState::Failed,
              QString::number(static_cast<int>(afterTimeout)));
        check("ac4.timeout-renders-current-snapshot", hasRowMessage(timeoutRows, staleRow),
              rowMessages(timeoutRows));
    }

    // ---- T14 / AC-3.1: the badge regression (root cause + required facts) ---
    // Root-cause evidence: evidence/batch-20261110/badge-diagnosis/REPORT.md.
    // The reported defect: `tools.lsp_backend=slang` could not be applied (no
    // shipped slang-server), the effective backend stayed verible, and the only
    // notice was an 8 s status-bar message - so after 8 s the user could not
    // tell a selected verible from a refused slang. D3 (the Rust spellings) and
    // D4 (dropped loader warnings) live on the same facts. The badge data plane
    // must therefore keep the REQUEST next to the effective backend, name the
    // tool that is missing, normalise the spellings, and carry the warnings;
    // this test is the regression guard that a later refactor cannot quietly drop
    // them back to a single "effective" value.
    {
        auto lsp = std::make_unique<FakeLsp>();
        FakeLsp *fakeLsp = lsp.get();
        auto slang = std::make_unique<FakeSlang>();
        BackendSwitchState state(lsp.get(), slang.get());

        ToolchainReport toolchain;
        toolchain.verible.name = QStringLiteral("verible-verilog-ls");
        toolchain.verible.path = QStringLiteral("/fixture/verible-verilog-ls");
        toolchain.verible.source = ToolSource::Bundled;
        toolchain.slang.name = QStringLiteral("slang");
        toolchain.slang.path = QStringLiteral("/fixture/slang");
        toolchain.slang.source = ToolSource::Bundled;
        // slang-server deliberately absent (REPORT.md D1).

        DiagnosticsBackendSettings settings;
        settings.lspBackend = QStringLiteral("slang");
        QString error;
        const bool refused = state.apply(settings, toolchain, &error);
        check("badge.regression.apply-refused", !refused && !error.isEmpty(), error);
        BadgeState badge = state.badge();
        check("badge.regression.requested-visible",
              badge.lsp.requested == QStringLiteral("slang"), badge.summary());
        check("badge.regression.effective-unchanged",
              badge.lsp.effective == QStringLiteral("verible"), badge.lsp.effective);
        check("badge.regression.reason-code",
              badge.lsp.reason == BadgeReasonCode::ToolMissing,
              badgeReasonCodeName(badge.lsp.reason));
        check("badge.regression.reason-names-tool",
              badge.lsp.reasonMessage.contains(QStringLiteral("slang-server")),
              badge.lsp.reasonMessage);
        check("badge.regression.summary-shows-pair",
              badge.summary().contains(QStringLiteral("slang→verible")), badge.summary());

        // D3: the four Rust spellings mean the same backend on this side too, so
        // the request the badge reports is the canonical token, not the label.
        settings.lspBackend = QStringLiteral("slang-server");
        const bool spelled = state.apply(settings, toolchain, &error);
        badge = state.badge();
        check("badge.regression.spelling-canonical",
              !spelled && badge.lsp.requested == QStringLiteral("slang"), badge.lsp.requested);

        // D4: the store's own explanation travels with the badge facts.
        const QStringList warnings{QStringLiteral("settings.toml: unknown key 'tools.lsp_backend'")};
        state.setLoadWarnings(warnings);
        check("badge.regression.load-warnings", state.badge().warnings == warnings,
              state.badge().warnings.join(QStringLiteral(" | ")));

        // With the server present the SAME request applies and the pair
        // collapses to one value; switching back leaves no stale value (AC-3.6).
        fakeLsp->acceptSlang = true;
        toolchain.slangServer.name = QStringLiteral("slang-server");
        toolchain.slangServer.path = QStringLiteral("/fixture/slang-server");
        toolchain.slangServer.source = ToolSource::Bundled;
        const bool applied = state.apply(settings, toolchain, &error);
        badge = state.badge();
        check("badge.regression.applied",
              applied && badge.lsp.applied && badge.lsp.effective == QStringLiteral("slang"),
              badge.summary());
        check("badge.regression.no-stale-value",
              !badge.summary().contains(QStringLiteral("→")), badge.summary());
        settings.lspBackend = QStringLiteral("verible");
        state.apply(settings, toolchain, &error);
        check("badge.regression.switch-back",
              state.badge().lsp.effective == QStringLiteral("verible"),
              state.badge().summary());
    }

    // ---- AC-N2: badgeText() is the ONE badge renderer (pure, no widget) -----
    {
        // (a) The refused switch above, rendered from the REAL badge facts: the
        // request, the effective selection, the engine, the basis and the
        // machine-readable reason all survive into the one visible line.
        auto lsp = std::make_unique<FakeLsp>();
        auto slang = std::make_unique<FakeSlang>();
        BackendSwitchState state(lsp.get(), slang.get());
        ToolchainReport toolchain;
        toolchain.verible.name = QStringLiteral("verible-verilog-ls");
        toolchain.verible.path = QStringLiteral("/fixture/verible-verilog-ls");
        toolchain.verible.source = ToolSource::Bundled;
        toolchain.slang.name = QStringLiteral("slang");
        toolchain.slang.path = QStringLiteral("/fixture/slang");
        toolchain.slang.source = ToolSource::Bundled;
        DiagnosticsBackendSettings settings;
        settings.lspBackend = QStringLiteral("slang");
        QString error;
        state.apply(settings, toolchain, &error);
        const QString refused = badgeText(state.badge());
        check("badge.text.requested-to-effective",
              refused.contains(QStringLiteral("slang→verible")), refused);
        check("badge.text.lint-engine", refused.contains(QStringLiteral("lint=slang")), refused);
        check("badge.text.basis", refused.contains(QStringLiteral("basis=measured")), refused);
        check("badge.text.reason-token", refused.contains(QStringLiteral("reason=tool-missing")),
              refused);

        // (b) The exact shape on hand-built facts.
        BadgeState healthy;
        healthy.lsp.requested = QStringLiteral("verible");
        healthy.lsp.effective = QStringLiteral("verible");
        healthy.lsp.applied = true;
        healthy.lint.requested = QStringLiteral("slang");
        healthy.lint.effective = QStringLiteral("slang");
        healthy.lint.applied = true;
        healthy.basis = QStringLiteral("measured");
        const QString healthyText =
            QStringLiteral("verible · lint=slang · basis=measured · reason=ok");
        check("badge.text.shape-in-force", badgeText(healthy) == healthyText, badgeText(healthy));
        const BadgeState healthyCopy = healthy;
        check("badge.text.deterministic", badgeText(healthyCopy) == healthyText, healthyText);

        // (c) Both axes refused: one reason per failing axis, fixed lsp-then-lint order.
        BadgeState both;
        both.lsp.requested = QStringLiteral("slang");
        both.lsp.effective = QStringLiteral("verible");
        both.lsp.reason = BadgeReasonCode::ToolMissing;
        both.lint.requested = QStringLiteral("verible");
        both.lint.effective = QStringLiteral("slang");
        both.lint.reason = BadgeReasonCode::NoBackend;
        both.basis = QStringLiteral("declared");
        check("badge.text.both-reasons",
              badgeText(both)
                  == QStringLiteral("slang→verible · lint=verible→slang · basis=declared · "
                                    "reason=tool-missing+no-backend"),
              badgeText(both));

        // (d) Missing values never render as empty claims, and an axis that did
        // not take effect never borrows the in-force token.
        BadgeState empty;
        check("badge.text.empty-basis",
              badgeText(empty).contains(QStringLiteral("basis=unknown")), badgeText(empty));
        check("badge.text.unexplained-axis",
              badgeText(empty).contains(QStringLiteral("reason=not-applied"))
                  && !badgeText(empty).contains(QStringLiteral("+ok")),
              badgeText(empty));
        empty.lsp.requested = QStringLiteral("vhdl_ls");
        empty.lsp.reason = BadgeReasonCode::ToolMissing;
        check("badge.text.missing-effective",
              badgeText(empty).contains(QStringLiteral("vhdl_ls→unavailable")), badgeText(empty));

        // (e) AC-3.5: a VHDL file keeps its own route - vhdl_ls and the VHDL
        // lint pair, never the Verilog backend.
        auto vhdlLsp = std::make_unique<FakeLsp>();
        auto vhdlSlang = std::make_unique<FakeSlang>();
        BackendSwitchState vhdlState(vhdlLsp.get(), vhdlSlang.get());
        ToolchainReport vhdlTools;
        vhdlTools.vhdlLs.name = QStringLiteral("vhdl_ls");
        vhdlTools.vhdlLs.path = QStringLiteral("/fixture/vhdl_ls");
        vhdlTools.vhdlLs.source = ToolSource::Bundled;
        vhdlTools.ghdl.name = QStringLiteral("ghdl");
        vhdlTools.ghdl.path = QStringLiteral("/fixture/ghdl");
        vhdlTools.ghdl.source = ToolSource::Bundled;
        vhdlTools.vsg.name = QStringLiteral("vsg");
        vhdlTools.vsg.path = QStringLiteral("/fixture/vsg");
        vhdlTools.vsg.source = ToolSource::Bundled;
        vhdlState.seedToolchain(vhdlTools);
        const QString vhdl = badgeText(vhdlState.badge(QStringLiteral("vhdl")));
        check("badge.text.vhdl-route",
              vhdl.contains(QStringLiteral("vhdl_ls"))
                  && vhdl.contains(QStringLiteral("lint=ghdl+vsg")),
              vhdl);
    }

    // ---- AC-5.1/5.6/5.8: the waiver data layer ------------------------------
    {
        DiagnosticEntry veribleRow;
        veribleRow.file = QStringLiteral("/project/rtl/top.sv");
        veribleRow.line = 9; // 0-based in the row, 1-based in the waiver
        veribleRow.endLine = 9;
        veribleRow.code = QStringLiteral("no-trailing-spaces");
        veribleRow.severity = 2;
        const WaiverEntry perDiagnostic = planWaiverEntry(LintEngineKind::VeribleLint, veribleRow);
        check("waiver.verible.valid", perDiagnostic.valid, perDiagnostic.reason);
        check("waiver.verible.line-one-based",
              perDiagnostic.line == 10 && perDiagnostic.endLine == 10,
              QStringLiteral("%1..%2").arg(perDiagnostic.line).arg(perDiagnostic.endLine));
        const QString expectedLine = QStringLiteral(
            "waive --rule=no-trailing-spaces --line=10 --location=\"(^|/)top\\.sv$\"");
        check("waiver.verible.per-diagnostic-line", veribleWaiverLine(perDiagnostic) == expectedLine,
              veribleWaiverLine(perDiagnostic));

        DiagnosticEntry range = veribleRow;
        range.line = 3;
        range.endLine = 5;
        const WaiverEntry rangeEntry = planWaiverEntry(LintEngineKind::VeribleLint, range);
        check("waiver.verible.line-range",
              veribleWaiverLine(rangeEntry).contains(QStringLiteral("--line=4:6")),
              veribleWaiverLine(rangeEntry));

        DiagnosticEntry legacy = veribleRow;
        legacy.code.clear();
        const WaiverEntry noRule = planWaiverEntry(LintEngineKind::VeribleLint, legacy);
        check("waiver.verible.no-rule-refused",
              !noRule.valid && noRule.reason.contains(QStringLiteral("rule id")), noRule.reason);

        // A single-file run's rows carry no file: the caller supplies the active
        // one, and no file at all is a refusal rather than a guess.
        DiagnosticEntry fileless = veribleRow;
        fileless.file.clear();
        const WaiverEntry fallback = planWaiverEntry(LintEngineKind::VeribleLint, fileless,
                                                     QString(),
                                                     QStringLiteral("/project/rtl/alu.sv"));
        check("waiver.file-fallback",
              fallback.valid && fallback.file == QStringLiteral("/project/rtl/alu.sv"),
              fallback.file);
        const WaiverEntry unscoped = planWaiverEntry(LintEngineKind::VeribleLint, fileless);
        check("waiver.no-file-refused", !unscoped.valid && !unscoped.reason.isEmpty(),
              unscoped.reason);

        // AC-5.8: a slang row without its warning option name is refused with the
        // whole-scope explanation - never widened into a file-wide waiver.
        DiagnosticEntry errorRow = veribleRow;
        errorRow.severity = 1;
        const WaiverEntry noOption = planWaiverEntry(LintEngineKind::Slang, errorRow);
        check("waiver.slang.no-option-refused",
              !noOption.valid && noOption.reason.contains(QStringLiteral("option name"))
                  && noOption.reason.contains(QStringLiteral("whole scope")),
              noOption.reason);
        const WaiverEntry slangEntry =
            planWaiverEntry(LintEngineKind::Slang, errorRow, QStringLiteral("implicit-net"));
        check("waiver.slang.option-accepted",
              slangEntry.valid && slangEntry.optionName == QStringLiteral("implicit-net")
                  && slangEntry.line == 0,
              slangEntry.reason);

        // AC-5.6: exactly ONE scope key per block, and the scope value is the
        // one the ABI writes.
        const SlangWaiverText absolute =
            slangWaiverBlock(QStringLiteral("/project/rtl/top.sv"), QStringLiteral("implicit-net"));
        check("waiver.slang.block-valid", absolute.valid, absolute.reason);
        check("waiver.slang.exactly-one-scope",
              absolute.content.count(QLatin1String("file = ")) == 1
                  && absolute.content.count(QLatin1String("hier = ")) == 0
                  && absolute.content.count(QLatin1String("[[waivers]]")) == 1,
              absolute.content);
        check("waiver.slang.absolute-scope",
              absolute.content.contains(QStringLiteral("file = \"/project/rtl/top.sv\"")),
              absolute.content);
        check("waiver.slang.block-loadable", validateSlangWaiverBlock(absolute.content).isEmpty(),
              validateSlangWaiverBlock(absolute.content));
        const SlangWaiverText relative =
            slangWaiverBlock(QStringLiteral("rtl/top.sv"), QStringLiteral("implicit-net"));
        check("waiver.slang.relative-scope",
              relative.valid && relative.content.contains(QStringLiteral("file = \"**/rtl/top.sv\"")),
              relative.content);
        check("waiver.slang.no-option-block",
              !slangWaiverBlock(QStringLiteral("/p/top.sv"), QString()).valid, QString());

        // The validator is the grammar slang loads: the engine-side rules are
        // asserted here without a child process.
        check("waiver.validate.both-scopes",
              !validateSlangWaiverBlock(
                   QStringLiteral("[[waivers]]\nfile = \"a.sv\"\nhier = \"top\"\n"
                                  "diagnostic = \"implicit-net\"\n"))
                   .isEmpty(),
              QString());
        check("waiver.validate.no-scope",
              !validateSlangWaiverBlock(QStringLiteral("[[waivers]]\ndiagnostic = \"implicit-net\"\n"))
                   .isEmpty(),
              QString());
        check("waiver.validate.unknown-key",
              validateSlangWaiverBlock(QStringLiteral("[[waivers]]\nfile = \"a.sv\"\nbogus = \"x\"\n"))
                  .contains(QStringLiteral("unknown key")),
              QString());
        check("waiver.validate.regex-key",
              validateSlangWaiverBlock(
                  QStringLiteral("[[waivers]]\nfile = \"a.sv\"\nregex = \"^foo$\"\n"))
                  .isEmpty(),
              QString());
        check("waiver.validate.outside-table",
              !validateSlangWaiverBlock(QStringLiteral("file = \"a.sv\"\n")).isEmpty(), QString());
    }

    // ---- AC-1.4: the autofix MODE validation --------------------------------
    {
        const AutofixModeCheck noFlag = checkAutofixMode(QString());
        check("autofix.mode.no-flag",
              noFlag.valid && noFlag.available && noFlag.mode == AutofixMode::NoFlag
                  && noFlag.effect == AutofixEffect::None && noFlag.reason.isEmpty(),
              noFlag.reason);
        const AutofixModeCheck inplace = checkAutofixMode(QStringLiteral("  Inplace "));
        check("autofix.mode.inplace",
              inplace.valid && inplace.available && inplace.mode == AutofixMode::Inplace
                  && inplace.effect == AutofixEffect::WritesInPlace
                  && inplace.value == QStringLiteral("inplace"),
              inplace.value);
        const AutofixModeCheck patch = checkAutofixMode(QStringLiteral("patch"));
        check("autofix.mode.patch-prints-only",
              patch.valid && patch.available && patch.effect == AutofixEffect::PrintsPatch,
              patch.reason);
        const AutofixModeCheck generate = checkAutofixMode(QStringLiteral("generate-waiver"));
        check("autofix.mode.generate-waiver",
              generate.valid && generate.available
                  && generate.effect == AutofixEffect::GeneratesWaiver,
              generate.reason);
        const AutofixModeCheck interactive =
            checkAutofixMode(QStringLiteral("inplace-interactive"));
        check("autofix.mode.interactive-unavailable",
              interactive.valid && !interactive.available && interactive.interactive
                  && interactive.reason.contains(QStringLiteral("terminal")),
              interactive.reason);
        const AutofixModeCheck patchInteractive =
            checkAutofixMode(QStringLiteral("patch-interactive"));
        check("autofix.mode.patch-interactive-unavailable",
              patchInteractive.valid && !patchInteractive.available, patchInteractive.reason);
        const AutofixModeCheck unknown = checkAutofixMode(QStringLiteral("always"));
        check("autofix.mode.unknown-refused",
              !unknown.valid && !unknown.available
                  && unknown.reason.contains(QStringLiteral("not a verible autofix mode")),
              unknown.reason);
        check("autofix.mode.names",
              autofixModeName(AutofixMode::NoFlag).isEmpty()
                  && autofixModeName(AutofixMode::Patch) == QStringLiteral("patch")
                  && autofixModeName(AutofixMode::GenerateWaiver)
                      == QStringLiteral("generate-waiver"),
              QString());
    }

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
    // ---- AC-5.1/5.6: the waiver ACTIONS really call the Rust engine ---------
    {
        const auto readText = [](const QString &path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                return QString();
            }
            return QString::fromUtf8(file.readAll());
        };
        const auto waitForJob = [](WaiverActions &actions, quint64 job) {
            QElapsedTimer timer;
            timer.start();
            WaiverActions::Result result = actions.poll(job);
            while (result.state == WaiverActions::State::Pending && timer.elapsed() < 10000) {
                QTest::qWait(10);
                result = actions.poll(job);
            }
            return result;
        };

        QTemporaryDir dir;
        check("waiver.action.temp-dir", dir.isValid(), dir.path());
        WaiverActions actions;
        check("waiver.action.available", actions.available(), QString());

        // (1) verible: one line per diagnostic, and the engine's file is exactly
        // the preview - the C++ writer and the Rust writer cannot disagree.
        DiagnosticEntry row;
        row.file = QStringLiteral("/project/rtl/top.sv");
        row.line = 9;
        row.endLine = 9;
        row.code = QStringLiteral("no-trailing-spaces");
        row.severity = 2;
        const WaiverEntry entry = planWaiverEntry(LintEngineKind::VeribleLint, row);
        const QString veriblePath = dir.filePath(QStringLiteral("verible.waiver"));
        QString error;
        const quint64 appendJob = actions.append(veriblePath, entry, false, &error);
        check("waiver.action.append-started", appendJob != 0, error);
        WaiverActions::Result result = waitForJob(actions, appendJob);
        check("waiver.action.verible-written",
              result.state == WaiverActions::State::Ready && result.written, result.error);
        check("waiver.action.verible-content-matches-preview",
              readText(veriblePath) == veribleWaiverLine(entry) + QLatin1Char('\n'),
              readText(veriblePath));

        // Dedupe: the same diagnostic twice writes nothing the second time.
        const quint64 again = actions.append(veriblePath, entry, false, &error);
        result = waitForJob(actions, again);
        check("waiver.action.verible-deduped",
              result.state == WaiverActions::State::Ready && result.deduped && !result.written,
              result.error);

        // (2) slang: one [[waivers]] block with exactly one scope key.
        const QString slangPath = dir.filePath(QStringLiteral("slang-waivers.toml"));
        const WaiverEntry slangEntry =
            planWaiverEntry(LintEngineKind::Slang, row, QStringLiteral("implicit-net"));
        const quint64 slangJob = actions.append(slangPath, slangEntry, false, &error);
        result = waitForJob(actions, slangJob);
        const QString slangText = readText(slangPath);
        check("waiver.action.slang-written",
              result.state == WaiverActions::State::Ready && result.written
                  && slangText == slangWaiverBlock(row.file, QStringLiteral("implicit-net")).content,
              result.error);
        check("waiver.action.slang-one-scope",
              slangText.count(QLatin1String("file = ")) == 1
                  && slangText.count(QLatin1String("hier = ")) == 0,
              slangText);
        check("waiver.action.slang-loadable", validateSlangWaiverBlock(slangText).isEmpty(),
              validateSlangWaiverBlock(slangText));

        // (3) AC-5.8: a row without an option name never reaches the worker, and
        // even a hand-built `valid` entry is refused at the action boundary.
        DiagnosticEntry errorRow = row;
        errorRow.severity = 1;
        const WaiverEntry refusedEntry = planWaiverEntry(LintEngineKind::Slang, errorRow);
        const QString refusedPath = dir.filePath(QStringLiteral("refused.toml"));
        QString refusal;
        check("waiver.action.slang-no-option-rejected",
              actions.append(refusedPath, refusedEntry, false, &refusal) == 0
                  && refusal.contains(QStringLiteral("option name")),
              refusal);
        check("waiver.action.slang-no-option-writes-nothing", !QFileInfo::exists(refusedPath),
              QString());
        WaiverEntry forged;
        forged.valid = true;
        forged.engine = LintEngineKind::Slang;
        forged.file = row.file;
        QString forgedError;
        check("waiver.action.slang-forged-rejected",
              actions.append(dir.filePath(QStringLiteral("forged.toml")), forged, false, &forgedError)
                      == 0
                  && forgedError.contains(QStringLiteral("option name")),
              forgedError);

        // (4) Path validation is the engine's, not a guess: a missing parent
        // directory fails honestly and writes nothing.
        const quint64 badPath =
            actions.append(dir.filePath(QStringLiteral("no/such/dir/waivers.toml")), entry, false,
                           &error);
        result = waitForJob(actions, badPath);
        check("waiver.action.missing-parent-fails",
              result.state == WaiverActions::State::Failed && !result.error.isEmpty(),
              result.error);

        // (5) A TOML file slang would refuse (unknown key) is left untouched: the
        // engine's exactly-one-scope / unknown-key rules apply to the target too.
        const QString bogusPath = dir.filePath(QStringLiteral("bogus.toml"));
        const QString bogusContent =
            QStringLiteral("[[waivers]]\nfile = \"a.sv\"\nbogus = \"x\"\n");
        QFile bogus(bogusPath);
        const bool bogusWritten = bogus.open(QIODevice::WriteOnly | QIODevice::Truncate);
        if (bogusWritten) {
            bogus.write(bogusContent.toUtf8());
            bogus.close();
        }
        check("waiver.action.unloadable-fixture", bogusWritten, bogusPath);
        const quint64 bogusJob = actions.append(bogusPath, slangEntry, false, &error);
        result = waitForJob(actions, bogusJob);
        check("waiver.action.unloadable-target-untouched",
              result.state == WaiverActions::State::Failed && readText(bogusPath) == bogusContent,
              result.error);

        // (6) Registration: the waiver file becomes the one the lint pass uses.
        const QString settingsPath = dir.filePath(QStringLiteral("settings.toml"));
        const quint64 registerJob = actions.registerFile(settingsPath, LintEngineKind::VeribleLint,
                                                        veriblePath, &error);
        result = waitForJob(actions, registerJob);
        check("waiver.action.registered",
              result.state == WaiverActions::State::Ready && result.written
                  && readText(settingsPath).contains(veriblePath),
              result.error);
    }
#endif // VEDIT_DIAGNOSTICS_HAVE_FFI

    // ---- G1-a (batch-20261120): the realtime set is bounded by the GOLDEN set --
    // The reported defect: `realtimeFileSetFor()` started with `{path}` and the
    // session unioned EVERY open tab, so a file the user merely OPENED entered
    // the realtime compile set even though no `.f` ever named it. slang then saw
    // its module definitions and resolved diagnostics/goto to it while the
    // module tree (fed by the project file set) stayed correct. The contract's
    // construction: golden = {a.v}; b.v is NOT a member (and defines modules too).
    {
        const QString a = QDir::cleanPath(QStringLiteral("/proj/rtl/a.v"));
        const QString b = QDir::cleanPath(QStringLiteral("/proj/sim/b.v"));
        const QString aChild = QDir::cleanPath(QStringLiteral("/proj/rtl/a_child.v"));
        const QString bChild = QDir::cleanPath(QStringLiteral("/proj/sim/b_child.v"));
        QVariantMap closure;
        closure.insert(a, QStringList{aChild});
        closure.insert(b, QStringList{bChild});
        // The project file set: a.v and the definition file of the module it
        // instantiates (G1-b: a closure child is itself a file-set member).
        // b.v/b_child.v are NOT members - the file no `.f` ever named.
        const realtime_scope::GoldenFileSet golden =
            realtime_scope::goldenFileSetFrom(QStringList{a, aChild});
        check("g1a.golden-membership",
              realtime_scope::isGoldenFile(golden, a) && realtime_scope::isGoldenFile(golden, aChild)
                  && !realtime_scope::isGoldenFile(golden, b),
              QStringLiteral("a=%1 a_child=%2 b=%3")
                  .arg(realtime_scope::isGoldenFile(golden, a))
                  .arg(realtime_scope::isGoldenFile(golden, aChild))
                  .arg(realtime_scope::isGoldenFile(golden, b)));

        // OUTSIDE the golden set: the file ALONE. The old rule answered
        // {b, bChild} here (its own closure), which is what let b's module
        // definitions be parsed as part of the project.
        const QStringList outsideSet = realtime_scope::realtimeFileSetFor(golden, closure, b, 64);
        check("g1a.outside-realtime-set-is-one-file", outsideSet == QStringList{b},
              QStringLiteral("set=[%1]").arg(outsideSet.join(QLatin1Char(','))));
        check("g1a.outside-set-has-no-project-file",
              !outsideSet.contains(a) && !outsideSet.contains(aChild) && !outsideSet.contains(bChild),
              QStringLiteral("set=[%1]").arg(outsideSet.join(QLatin1Char(','))));

        // INSIDE the golden set: identical to the pre-G1-a behaviour ({a} ∪ its
        // one-level closure, every child a project file).
        const QStringList insideSet = realtime_scope::realtimeFileSetFor(golden, closure, a, 64);
        check("g1a.inside-realtime-set-unchanged", insideSet == QStringList{a, aChild},
              QStringLiteral("set=[%1]").arg(insideSet.join(QLatin1Char(','))));

        // The SESSION (what the Rust worker compiles) must not contain b.v: both
        // documents are "open", only the golden one may contribute.
        int outsideCount = -1;
        bool sessionTruncated = false;
        const QStringList session = realtime_scope::openDocumentSessionSet(
            golden, closure, QStringList{a, b}, 64, 128, &outsideCount, &sessionTruncated);
        check("g1a.session-excludes-outside-document",
              session.size() == 2 && session.contains(a) && session.contains(aChild)
                  && !session.contains(b) && !session.contains(bChild),
              QStringLiteral("session=[%1]").arg(session.join(QLatin1Char(','))));
        check("g1a.session-outside-file-count", outsideCount == 1 && !sessionTruncated,
              QStringLiteral("outside=%1 truncated=%2").arg(outsideCount).arg(sessionTruncated));

        // G1-b defence: a closure entry may not name a file outside the golden
        // set, even when it is keyed under a member (here even aChild is not a
        // member of this minimal set, so the rule drops it too).
        QVariantMap foreignClosure;
        foreignClosure.insert(a, QStringList{b});
        const QStringList defended =
            realtime_scope::realtimeFileSetFor(realtime_scope::goldenFileSetFrom(QStringList{a}),
                                               foreignClosure, a, 64);
        check("g1a.closure-child-outside-golden-dropped", defended == QStringList{a},
              QStringLiteral("set=[%1]").arg(defended.join(QLatin1Char(','))));

        // NO golden set = single-file mode: the historical behaviour is kept (the
        // module tree is gated on a filelist, so no closure can exist there).
        const realtime_scope::GoldenFileSet none;
        check("g1a.no-golden-set-keeps-single-file-rule",
              realtime_scope::realtimeFileSetFor(none, closure, b, 64)
                  == QStringList{b, bChild},
              QStringLiteral("set=[%1]")
                  .arg(realtime_scope::realtimeFileSetFor(none, closure, b, 64)
                           .join(QLatin1Char(','))));

        // Symlinked project root: the two spellings of the SAME file are one key.
        QTemporaryDir linkDir;
        const QString realDirectory = linkDir.filePath(QStringLiteral("real"));
        const QString linkPath = linkDir.filePath(QStringLiteral("link"));
        QDir().mkpath(realDirectory);
        const QString realFile = QDir(realDirectory).absoluteFilePath(QStringLiteral("a.v"));
        QFile realHandle(realFile);
        const bool realWritten =
            realHandle.open(QIODevice::WriteOnly | QIODevice::Truncate) && realHandle.write("x") == 1;
        realHandle.close();
        const bool linked = realWritten && QFile::link(realDirectory, linkPath);
        check("g1a.symlink-fixture", linked, linkPath);
        if (linked) {
            const realtime_scope::GoldenFileSet symlinked =
                realtime_scope::goldenFileSetFrom(QStringList{realFile});
            const QString viaLink = QDir(linkPath).absoluteFilePath(QStringLiteral("a.v"));
            check("g1a.symlink-spelling-is-a-member",
                  realtime_scope::isGoldenFile(symlinked, viaLink),
                  QStringLiteral("golden='%1' query='%2'")
                      .arg(realFile, viaLink));
        }

        // The caps (64 / 128) are untouched: a 70-entry closure still yields 64
        // files and says it truncated, and three such documents fill the session
        // up to exactly 128 and report the cap (the 129th file is dropped).
        const QString a2 = QDir::cleanPath(QStringLiteral("/proj/rtl/a2.v"));
        const QString a3 = QDir::cleanPath(QStringLiteral("/proj/rtl/a3.v"));
        QStringList children;
        QStringList children2;
        QStringList children3;
        for (int index = 0; index < 70; ++index) {
            children.append(QStringLiteral("/proj/rtl/child%1.v").arg(index, 2, 10, QLatin1Char('0')));
            children2.append(
                QStringLiteral("/proj/rtl/child2_%1.v").arg(index, 2, 10, QLatin1Char('0')));
            children3.append(
                QStringLiteral("/proj/rtl/child3_%1.v").arg(index, 2, 10, QLatin1Char('0')));
        }
        QVariantMap wideClosure;
        wideClosure.insert(a, children);
        wideClosure.insert(a2, children2);
        wideClosure.insert(a3, children3);
        const realtime_scope::GoldenFileSet wideGolden = realtime_scope::goldenFileSetFrom(
            QStringList{a, a2, a3} + children + children2 + children3);
        bool wideTruncated = false;
        const QStringList wideSet =
            realtime_scope::realtimeFileSetFor(wideGolden, wideClosure, a, 64, &wideTruncated);
        check("g1a.closure-cap-is-64", wideSet.size() == 64 && wideTruncated,
              QStringLiteral("size=%1 truncated=%2").arg(wideSet.size()).arg(wideTruncated));
        bool wideSessionTruncated = false;
        int wideOutside = -1;
        const QStringList wideSession = realtime_scope::openDocumentSessionSet(
            wideGolden, wideClosure, QStringList{a, a2, a3}, 64, 128, &wideOutside,
            &wideSessionTruncated);
        check("g1a.session-cap-is-128",
              wideSession.size() == 128 && wideSessionTruncated && wideOutside == 0,
              QStringLiteral("size=%1 truncated=%2 outside=%3")
                  .arg(wideSession.size())
                  .arg(wideSessionTruncated)
                  .arg(wideOutside));
    }

    // ---- G1-c (batch-20261120): the shadowed files leave the compiler input ----
    // The catalog publishes the files whose EVERY module definition lost the
    // duplicate pick; a consumer that keeps feeding them to slang lets slang
    // apply its OWN duplicate rule and pick a second winner, which is the
    // reported defect. The subtraction is asserted here without a shell.
    {
        const QString winner = QDir::cleanPath(QStringLiteral("/proj/rtl/uart.v"));
        const QString loser = QDir::cleanPath(QStringLiteral("/proj/sim/uart.v"));
        const QString unrelated = QDir::cleanPath(QStringLiteral("/proj/rtl/other.v"));
        const realtime_scope::ShadowedFileSet shadowed =
            realtime_scope::shadowedFileSetFrom(QStringList{loser});
        check("g1c.shadowed-membership",
              realtime_scope::isShadowedFile(shadowed, loser)
                  && !realtime_scope::isShadowedFile(shadowed, winner)
                  && !realtime_scope::isShadowedFile(shadowed, QString()),
              QStringLiteral("loser=%1 winner=%2")
                  .arg(realtime_scope::isShadowedFile(shadowed, loser))
                  .arg(realtime_scope::isShadowedFile(shadowed, winner)));
        const QStringList dropped =
            realtime_scope::dropShadowedFiles(QStringList{winner, loser, unrelated}, shadowed);
        check("g1c.shadowed-file-is-dropped", dropped == QStringList{winner, unrelated},
              QStringLiteral("set=[%1]").arg(dropped.join(QLatin1Char(','))));
        // The ACTIVE document is kept even when it is the shadowed one: a user who
        // opens the losing file still gets single-file diagnostics, and an empty
        // scope would silently analyse nothing.
        const QStringList kept = realtime_scope::dropShadowedFiles(
            QStringList{loser, unrelated}, shadowed, loser);
        check("g1c.active-document-is-kept", kept == QStringList{loser, unrelated},
              QStringLiteral("set=[%1]").arg(kept.join(QLatin1Char(','))));
        // No catalog -> no shadowed list -> the file set is returned untouched
        // (an absent catalog must never change what the pass lints).
        const QStringList untouched = realtime_scope::dropShadowedFiles(
            QStringList{winner, loser}, realtime_scope::ShadowedFileSet{});
        check("g1c.no-shadowed-list-keeps-the-set",
              untouched == QStringList{winner, loser},
              QStringLiteral("set=[%1]").arg(untouched.join(QLatin1Char(','))));

        // Symlinked project root: the catalog spells the file through the REAL
        // directory, the request through the LINK. The canonical-directory
        // fallback makes them one key, exactly as the golden-set membership does.
        QTemporaryDir shadowDir;
        const QString shadowReal = shadowDir.filePath(QStringLiteral("real"));
        const QString shadowLink = shadowDir.filePath(QStringLiteral("link"));
        QDir().mkpath(shadowReal);
        const QString shadowRealFile = QDir(shadowReal).absoluteFilePath(QStringLiteral("uart.v"));
        QFile shadowHandle(shadowRealFile);
        const bool shadowWritten = shadowHandle.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && shadowHandle.write("x") == 1;
        shadowHandle.close();
        const bool shadowLinked = shadowWritten && QFile::link(shadowReal, shadowLink);
        check("g1c.symlink-fixture", shadowLinked, shadowLink);
        if (shadowLinked) {
            const QString viaLink = QDir(shadowLink).absoluteFilePath(QStringLiteral("uart.v"));
            const realtime_scope::ShadowedFileSet realShadowed =
                realtime_scope::shadowedFileSetFrom(QStringList{shadowRealFile});
            const QStringList linkDropped =
                realtime_scope::dropShadowedFiles(QStringList{viaLink}, realShadowed);
            check("g1c.symlink-spelling-is-dropped", linkDropped.isEmpty(),
                  QStringLiteral("shadowed='%1' query='%2' kept=%3")
                      .arg(shadowRealFile, viaLink)
                      .arg(linkDropped.size()));
        }

        // ---- G1-a/G1-c (batch-20261120, review F1): the ACTIVE document may not
        // widen the compile set. Before this rule the controller PREPENDED the
        // active document to the session, so the language server was started over
        // `session ∪ {active}`: an outside file put its own definitions into the
        // project-wide compile set, and a shadowed loser was compiled right next
        // to the winner it lost to (both `closure_mid` definitions in one build
        // context = the reported defect coming back on "open that file").
        const realtime_scope::GoldenFileSet fixtureGolden =
            realtime_scope::goldenFileSetFrom(QStringList{winner, unrelated});
        const realtime_scope::ShadowedFileSet fixtureShadowed =
            realtime_scope::shadowedFileSetFrom(QStringList{loser});
        check("g1a.active-golden-file-joins-the-session",
              realtime_scope::activeDocumentJoinsProjectSession(fixtureGolden, fixtureShadowed,
                                                               winner),
              winner);
        check("g1a.active-outside-file-is-analysed-alone",
              !realtime_scope::activeDocumentJoinsProjectSession(fixtureGolden, fixtureShadowed,
                                                                 loser),
              loser);
        check("g1c.active-shadowed-file-is-analysed-alone",
              !realtime_scope::activeDocumentJoinsProjectSession(fixtureGolden, fixtureShadowed,
                                                                 loser),
              loser);
        // Single-file mode (no project file set): nothing to decide against, so
        // the historical session applies.
        check("g1a.active-file-in-single-file-mode-joins",
              realtime_scope::activeDocumentJoinsProjectSession(
                  realtime_scope::GoldenFileSet{}, realtime_scope::ShadowedFileSet{},
                  QStringLiteral("/any/where.sv")),
              QStringLiteral("/any/where.sv"));
        check("g1a.empty-path-never-joins",
              !realtime_scope::activeDocumentJoinsProjectSession(fixtureGolden, fixtureShadowed,
                                                                 QString()));
    }

    // ---- G1-a/G1-c (batch-20261120): the COMPILE set is `{active}` alone ----
    // The predicate above is only half the fix; this drives the controller and
    // reads the exact list the language server is started over
    // (`fileSetReport().files`), which is what `slang` is handed. The published
    // analysis properties are NOT consulted: the review's finding was that they
    // looked right while the compile set was the union.
    {
        auto aloneLsp = std::make_unique<FakeLsp>();
        auto *aloneLspRaw = aloneLsp.get();
        auto aloneSlang = std::make_unique<FakeSlang>();
        DiagnosticsController controller(std::move(aloneLsp), std::move(aloneSlang));
        controller.setConfiguration(QStringLiteral("/payload/verible-verilog-ls"),
                                    QStringLiteral("/fixture"));
        const QString winner = QDir::cleanPath(QStringLiteral("/proj/rtl/uart.v"));
        const QString loser = QDir::cleanPath(QStringLiteral("/proj/sim/uart.v"));
        const QString outside = QDir::cleanPath(QStringLiteral("/tmp/outside.sv"));
        // The action is performed BEFORE `check()` on purpose: a function
        // argument's evaluation order is unspecified, so a detail string built
        // from the same call may be computed before the call runs.
        controller.setRealtimeSessionFiles(QStringList{winner});
        // (a) admitted: the session is reused and covers the active document.
        controller.setActiveDocumentJoinsProjectSession(true);
        const bool startedGolden = controller.analyzeFile(
            winner, QStringLiteral("module uart;\nendmodule\n"), false);
        const QStringList compileGolden = aloneLspRaw->startedFiles;
        check("g1a.compile-set-is-the-golden-session",
              startedGolden && compileGolden == QStringList{winner},
              QStringLiteral("compile=[%1]").arg(compileGolden.join(QLatin1Char(','))));
        // (b) an OUTSIDE document: `{active}` only, never `session ∪ {active}`.
        controller.setActiveDocumentJoinsProjectSession(false);
        const int startsBeforeOutside = aloneLspRaw->startCount;
        const bool startedOutside = controller.analyzeFile(
            outside, QStringLiteral("module uart;\nendmodule\n"), false);
        const int startsAfterOutside = aloneLspRaw->startCount;
        const QStringList compileOutside = aloneLspRaw->startedFiles;
        check("g1a.compile-set-outside-file-is-alone",
              startedOutside && startsAfterOutside == startsBeforeOutside + 1
                  && compileOutside == QStringList{outside}
                  && !compileOutside.contains(winner),
              QStringLiteral("starts=%1 -> %2 compile=[%3]")
                  .arg(startsBeforeOutside)
                  .arg(startsAfterOutside)
                  .arg(compileOutside.join(QLatin1Char(','))));
        const QStringList reportedOutside = controller.fileSetReport().files;
        check("g1a.compile-set-is-what-the-report-carries",
              reportedOutside == QStringList{outside},
              reportedOutside.join(QLatin1Char(',')));
        // (c) a SHADOWED document: also alone, so the winner cannot ride along.
        controller.setRealtimeSessionFiles(QStringList{winner});
        const int startsBeforeLoser = aloneLspRaw->startCount;
        const bool startedLoser = controller.analyzeFile(
            loser, QStringLiteral("module uart;\nendmodule\n"), false);
        const int startsAfterLoser = aloneLspRaw->startCount;
        const QStringList compileLoser = aloneLspRaw->startedFiles;
        check("g1c.compile-set-shadowed-file-is-alone",
              startedLoser && startsAfterLoser == startsBeforeLoser + 1
                  && compileLoser == QStringList{loser} && !compileLoser.contains(winner),
              QStringLiteral("starts=%1 -> %2 compile=[%3]")
                  .arg(startsBeforeLoser)
                  .arg(startsAfterLoser)
                  .arg(compileLoser.join(QLatin1Char(','))));
        // (d) back on a golden document: the full golden session is restored.
        controller.setActiveDocumentJoinsProjectSession(true);
        const int startsBeforeReturn = aloneLspRaw->startCount;
        const bool startedReturn = controller.analyzeFile(
            winner, QStringLiteral("module uart;\nendmodule\n"), false);
        const int startsAfterReturn = aloneLspRaw->startCount;
        const QStringList compileReturn = aloneLspRaw->startedFiles;
        check("g1a.compile-set-restores-the-session",
              startedReturn && startsAfterReturn == startsBeforeReturn + 1
                  && compileReturn == QStringList{winner},
              QStringLiteral("compile=[%1]").arg(compileReturn.join(QLatin1Char(','))));
    }

    std::printf("VE-DIAGNOSTICS-SELFTEST checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
