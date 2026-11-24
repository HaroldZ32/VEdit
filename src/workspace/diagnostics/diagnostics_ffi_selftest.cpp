// End-to-end headless proof through the Rust C ABI and a REAL Verible server.
//
//   vedit-diagnostics-ffi-selftest <fixture-root> <verible-verilog-ls>
//
// It reads the checked-in expectation (`expected/files.txt`, produced by the
// project model's `.f` semantics), starts a real LSP session over exactly that
// file list, exercises the navigation entries, and proves the degradation path
// with a bogus server path. When the Verible binary is absent the test prints an
// explicit `SKIP:` line and exits 0 — it never pretends the interaction ran.
//
// On this machine `slang` is not installed, so the SLANG leg is expected to
// report typed failures; the test asserts the failure is observable, not that it
// succeeded. A host with slang gets the same test with a healthy SLANG leg.

#include "diagnostics_controller.h"
#include "lsp_backend.h"
#include "runtime_init.h"
#include "slang_backend.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

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
    std::printf("CHECK %s %s%s%s\n",
                ok ? "PASS" : "FAIL",
                name,
                detail.isEmpty() ? "" : "  ",
                detail.isEmpty() ? "" : detail.toUtf8().constData());
}

QString stateName(BackendState state)
{
    switch (state) {
    case BackendState::Idle:
        return QStringLiteral("idle");
    case BackendState::Reparsing:
        return QStringLiteral("reparsing");
    case BackendState::Ready:
        return QStringLiteral("ready");
    case BackendState::Degraded:
        return QStringLiteral("degraded");
    case BackendState::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

QStringList readExpectedFiles(const QString &fixtureRoot)
{
    QFile expected(QStringLiteral("%1/expected/files.txt").arg(fixtureRoot));
    if (!expected.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QStringList files;
    const QByteArray data = expected.readAll();
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty()) {
            files.append(QStringLiteral("%1/%2").arg(fixtureRoot, line));
        }
    }
    return files;
}

BackendState waitForTerminal(DiagnosticsController &controller, int seconds)
{
    for (int tick = 0; tick < seconds * 50; ++tick) {
        controller.refresh();
        if (controller.state() != BackendState::Reparsing) {
            return controller.state();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return controller.state();
}

LspPolled waitForPolled(DiagnosticsController &controller)
{
    for (int tick = 0; tick < 2000; ++tick) { // 20 s
        LspPolled polled = controller.pollNavigation();
        if (!polled.pending) {
            return polled;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return LspPolled::failure(QStringLiteral("the navigation request did not settle in 20 s"));
}

} // namespace

int main(int argc, char **argv)
{
    // T134 / AGENT.md §4: this selftest links the Rust service layer and starts
    // a REAL Verible LSP server through it, so the Rust runtime's
    // `SIGPIPE -> SIG_IGN` default must be installed by the host first.
    vedit_runtime_init();
    QCoreApplication app(argc, argv);
    const QString fixtureRoot = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    QString verible = argc > 2 ? QString::fromLocal8Bit(argv[2])
                               : qEnvironmentVariable("VEDIT_VERIBLE_LS");

    if (fixtureRoot.isEmpty() || !QFileInfo(fixtureRoot).isDir()) {
        std::printf("SKIP: fixture root not given or not a directory: %s\n",
                    fixtureRoot.toUtf8().constData());
        return 0;
    }
    const QFileInfo veribleInfo(verible);
    if (verible.isEmpty() || !veribleInfo.isFile() || !veribleInfo.isExecutable()) {
        std::printf("SKIP: verible-verilog-ls not available (%s); "
                    "run scripts/fetch-verible.sh or set VEDIT_VERIBLE_LS\n",
                    verible.toUtf8().constData());
        return 0;
    }

    const QStringList files = readExpectedFiles(fixtureRoot);
    check("a3c.fixture-file-count", files.size() == 7,
          QStringLiteral("expected/files.txt -> %1 file(s)").arg(files.size()));
    if (files.isEmpty()) {
        std::printf("VER-DIAGNOSTICS-FFI-SELFTEST checks=%d failures=%d\n", g_checks, g_failures);
        return g_failures == 0 ? 0 : 1;
    }

    auto lspBackend = createFfiLspSessionBackend();
    auto slangBackend = createFfiSlangBackend();
    check("ffi.lsp-backend-available", ffiLspSessionBackendAvailable() && lspBackend != nullptr);
    check("ffi.slang-backend-available", ffiSlangBackendAvailable() && slangBackend != nullptr);

    DiagnosticsController controller(std::move(lspBackend), std::move(slangBackend));
    controller.setConfiguration(veribleInfo.absoluteFilePath(), fixtureRoot);
    // batch-20261116 L1.1: the PROJECT file set must NOT initialize the language
    // server. Only the realtime analysis (current file + one-level closure) does.
    controller.setFileSet(files);
    check("l1.project-set-is-not-the-session",
          controller.state() == BackendState::Idle && controller.fileSetReport().files.isEmpty()
              && controller.session().filesExpected == 0,
          QStringLiteral("state=%1 files=%2 expected=%3")
              .arg(stateName(controller.state()))
              .arg(controller.fileSetReport().files.size())
              .arg(controller.session().filesExpected));

    const QString top = QStringLiteral("%1/rtl/top.sv").arg(fixtureRoot);
    QFile topFile(top);
    QString topText;
    if (topFile.open(QIODevice::ReadOnly)) {
        topText = QString::fromUtf8(topFile.readAll());
    }
    check("a3c.realtime-source-present", !topText.isEmpty(), top);
    check("a3c.realtime-analysis-starts", controller.analyzeFile(top, topText, false));
    check("a3d.reparsing-observable", controller.state() == BackendState::Reparsing,
          stateName(controller.state()));

    const BackendState state = waitForTerminal(controller, 60);
    check("a3d.terminal-state-within-60s",
          state == BackendState::Ready || state == BackendState::Degraded,
          QStringLiteral("%1 (%2)").arg(stateName(state), controller.session().message));
    const FileSetReport report = controller.fileSetReport();
    check("a3c.lsp-opened-the-realtime-set",
          report.lspReceivedWholeSet && report.lspFilesOpened == 1
              && report.files == QStringList{top},
          QStringLiteral("opened=%1/%2 files=%3")
              .arg(report.lspFilesOpened)
              .arg(report.files.size())
              .arg(report.files.join(QLatin1Char(','))));
    check("a3c.no-project-wide-slang-sweep", report.slangRequested == 0,
          QString::number(report.slangRequested));
    std::printf("OBSERVED diagnostics=%lld lsp_message='%s' slang_message='%s'\n",
                static_cast<long long>(controller.diagnostics().size()),
                report.lspMessage.toUtf8().constData(),
                report.slangMessage.toUtf8().constData());

    // A3c survives on the set that owns it: the MANUAL reparse (`project.reparse`)
    // drives both backends over the SAME realtime set - one file here, never the
    // 7-file project.
    controller.reparse();
    const BackendState reparseState = waitForTerminal(controller, 60);
    check("a3d.reparse-terminal",
          reparseState == BackendState::Ready || reparseState == BackendState::Degraded,
          QStringLiteral("%1 (%2)").arg(stateName(reparseState), controller.session().message));
    const FileSetReport reparsed = controller.fileSetReport();
    check("a3c.report-carries-the-realtime-set",
          reparsed.lspReceivedWholeSet && reparsed.files == QStringList{top}
              && reparsed.slangRequested == 1,
          QStringLiteral("files=%1 slang=%2/%3")
              .arg(reparsed.files.join(QLatin1Char(',')))
              .arg(reparsed.slangCompleted)
              .arg(reparsed.slangRequested));
    check("a3c.slang-got-the-realtime-set",
          reparsed.slangCompleted == 1 || reparsed.slangFailures == 1,
          QStringLiteral("requested=%1 completed=%2 failures=%3")
              .arg(reparsed.slangRequested)
              .arg(reparsed.slangCompleted)
              .arg(reparsed.slangFailures));
    if (reparsed.slangFailures > 0) {
        std::printf("SKIP-NOTE: SLANG leg failed on %d file(s) (no slang binary on this host); "
                    "the failure is typed and observable, not a crash\n",
                    reparsed.slangFailures);
    }
    check("entry.symbols-queued", controller.requestSymbols(top) != 0);
    const LspPolled symbols = waitForPolled(controller);
    check("entry.symbols-answered", symbols.ready && symbols.kind == ResponseKind::Symbols,
          symbols.failed ? symbols.error : QString());
    bool sawTop = false;
    for (const OutlineSymbol &symbol : controller.outline()) {
        if (symbol.name == QStringLiteral("top")) {
            sawTop = true;
        }
    }
    check("entry.symbols-contains-top", sawTop,
          QStringLiteral("%1 outline entries").arg(controller.outline().size()));

    // `topText` was read above for the realtime analysis and is reused here.
    const int offset = topText.indexOf(QStringLiteral("alu u_alu"));
    check("fixture.alu-instantiation-present", offset > 0);
    if (offset > 0) {
        const int line = topText.left(offset).count(QLatin1Char('\n'));
        const int lineStart = topText.lastIndexOf(QLatin1Char('\n'), offset - 1) + 1;
        const int character = topText.mid(lineStart, offset - lineStart).size();
        check("entry.definition-queued",
              controller.requestDefinition(top, line, character) != 0);
        const LspPolled definition = waitForPolled(controller);
        check("entry.definition-answered",
              definition.ready && definition.kind == ResponseKind::Locations,
              definition.failed ? definition.error : QString());
        std::printf("OBSERVED definition_locations=%lld\n",
                    static_cast<long long>(definition.locations.size()));

        check("entry.references-queued",
              controller.requestReferences(top, line, character) != 0);
        const LspPolled references = waitForPolled(controller);
        check("entry.references-answered",
              references.ready && references.kind == ResponseKind::Locations,
              references.failed ? references.error : QString());

        check("entry.rename-queued",
              controller.requestRename(top, line, character, QStringLiteral("alu_renamed")) != 0);
        const LspPolled rename = waitForPolled(controller);
        check("entry.rename-answered", rename.ready && rename.kind == ResponseKind::Edits,
              rename.failed ? rename.error : QString());
    }
    check("entry.format-queued", controller.requestFormat(top) != 0);
    const LspPolled formatting = waitForPolled(controller);
    check("entry.format-answered", formatting.ready && formatting.kind == ResponseKind::Edits,
          formatting.failed ? formatting.error : QString());

    // ---- T118: the user's exact action - delete one comma in the editor ----
    {
        // A generated file keeps the scenario exact: the deleted comma is in
        // CODE, not in a comment (the fixture's first comma happens to sit in a
        // comment, which is not a syntax error at all).
        QTemporaryDir bufferDir;
        check("t118.temp-dir", bufferDir.isValid());
        const QString file = bufferDir.path() + QStringLiteral("/buffer_edit.sv");
        const QString goodText =
            QStringLiteral("module m;\n  logic a, b;\n  always_comb begin\n    a = b;\n"
                           "  end\nendmodule\n");
        const QString brokenText =
            QStringLiteral("module m;\n  logic a b;\n  always_comb begin\n    a = b;\n"
                           "  end\nendmodule\n");
        {
            QFile fileOnDisk(file);
            check("t118.good-file-written",
                  fileOnDisk.open(QIODevice::WriteOnly)
                      && fileOnDisk.write(goodText.toUtf8()) == goodText.toUtf8().size());
        }

        const bool started = controller.analyzeFile(file, brokenText, true);
        check("t118.buffer-analysis-starts", started);
        const BackendState bufferState = waitForTerminal(controller, 30);
        std::printf("OBSERVED t118 buffer_analysis_state=%s diagnostics=%lld dirty=%d\n",
                    qPrintable(stateName(bufferState)),
                    static_cast<long long>(controller.diagnostics().size()),
                    controller.analyzeWasDirty() ? 1 : 0);
        bool sawError = false;
        QStringList messages;
        for (const DiagnosticEntry &entry : controller.diagnostics()) {
            if (entry.file == file && !entry.message.isEmpty()) {
                messages.append(entry.message);
            }
            if (entry.file == file && entry.severity == 1) {
                sawError = true;
            }
        }
        std::printf("OBSERVED t118 buffer_messages=%s\n",
                    qPrintable(messages.join(QStringLiteral(" | "))));
        check("t118.buffer-syntax-error-reported", sawError,
              messages.join(QStringLiteral(" | ")));

        // Proof the lint ran on the BUFFER: the file on disk still has its comma.
        QFile diskFile(file);
        QString diskText;
        if (diskFile.open(QIODevice::ReadOnly)) {
            diskText = QString::fromUtf8(diskFile.readAll());
        }
        check("t118.disk-file-unchanged", diskText == goodText,
              QStringLiteral("disk=%1 bytes, buffer=%2 bytes")
                  .arg(diskText.size())
                  .arg(brokenText.size()));

        // Putting the comma back in the buffer clears the error.
        check("t118.buffer-restore-starts", controller.analyzeFile(file, goodText, false));
        waitForTerminal(controller, 30);
        int topErrors = 0;
        for (const DiagnosticEntry &entry : controller.diagnostics()) {
            if (entry.file == file && entry.severity == 1) {
                ++topErrors;
            }
        }
        std::printf("OBSERVED t118 after_restore errors=%d diagnostics=%lld\n", topErrors,
                    static_cast<long long>(controller.diagnostics().size()));
        check("t118.restore-clears-error", topErrors == 0,
              QStringLiteral("remaining errors=%1").arg(topErrors));
    }

    // ---- degradation: a missing server is diagnosable, never a crash -------
    {
        auto missingLsp = createFfiLspSessionBackend();
        auto missingSlang = createFfiSlangBackend();
        DiagnosticsController missing(std::move(missingLsp), std::move(missingSlang));
        missing.setConfiguration(QStringLiteral("/nonexistent/verible-verilog-ls"), fixtureRoot);
        missing.setFileSet(QStringList{top});
        // L1.1: the project set starts nothing; the realtime analysis and the
        // manual reparse are what engage the two legs. The FFI start is
        // asynchronous, so its refusal arrives in the state, not in this return.
        missing.analyzeFile(top, topText, false);
        missing.reparse();
        const BackendState missingState = waitForTerminal(missing, 30);
        // The expected state follows DiagnosticsController::finaliseState()
        // (diagnostics_controller.cpp): lspReady && slangComplete -> Ready;
        // exactly one of the two legs -> Degraded; neither -> Failed. A missing
        // verible binary can never make the LSP leg ready, so the observable
        // state depends on the SLANG leg:
        //   * SLANG usable and successful -> Degraded (one leg good, one bad);
        //   * SLANG unusable            -> Failed (both legs bad).
        // Both are the contract's "degraded, diagnosable, never a crash"; the
        // previous Failed-only assertion was coupled to the SLANG-less host and
        // failed (22 checks / 1 failure) wherever slang was available.
        check("degrade.missing-verible-state",
              missingState == BackendState::Degraded || missingState == BackendState::Failed,
              stateName(missingState));
        check("degrade.missing-verible-not-ready", missingState != BackendState::Ready,
              stateName(missingState));
        std::printf("OBSERVED missing-verible-state=%s\n", qPrintable(stateName(missingState)));
        check("degrade.missing-verible-message", !missing.lastError().isEmpty(),
              missing.lastError());
    }

    std::printf("VER-DIAGNOSTICS-FFI-SELFTEST checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
