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
#include "slang_backend.h"

#include <QCoreApplication>
#include <QFile>
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
    controller.setFileSet(files);
    check("a3d.reparsing-observable", controller.state() == BackendState::Reparsing,
          stateName(controller.state()));

    const BackendState state = waitForTerminal(controller, 60);
    check("a3d.terminal-state-within-60s",
          state == BackendState::Ready || state == BackendState::Degraded,
          QStringLiteral("%1 (%2)").arg(stateName(state), controller.session().message));
    const FileSetReport &report = controller.fileSetReport();
    check("a3c.lsp-opened-whole-set", report.lspReceivedWholeSet,
          QStringLiteral("opened=%1/%2").arg(report.lspFilesOpened).arg(files.size()));
    check("a3c.report-carries-the-set", report.files == files);
    check("a3c.slang-got-every-file",
          report.slangRequested == files.size() && report.slangCompleted == files.size(),
          QStringLiteral("requested=%1 completed=%2 failures=%3")
              .arg(report.slangRequested)
              .arg(report.slangCompleted)
              .arg(report.slangFailures));
    std::printf("OBSERVED diagnostics=%lld lsp_message='%s' slang_message='%s'\n",
                static_cast<long long>(controller.diagnostics().size()),
                report.lspMessage.toUtf8().constData(),
                report.slangMessage.toUtf8().constData());
    if (report.slangFailures > 0) {
        std::printf("SKIP-NOTE: SLANG leg failed on %d file(s) (no slang binary on this host); "
                    "the failure is typed and observable, not a crash\n",
                    report.slangFailures);
    }

    const QString top = QStringLiteral("%1/rtl/top.sv").arg(fixtureRoot);
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

    QFile topFile(top);
    QString topText;
    if (topFile.open(QIODevice::ReadOnly)) {
        topText = QString::fromUtf8(topFile.readAll());
    }
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

    // ---- degradation: a missing server is diagnosable, never a crash -------
    {
        auto missingLsp = createFfiLspSessionBackend();
        auto missingSlang = createFfiSlangBackend();
        DiagnosticsController missing(std::move(missingLsp), std::move(missingSlang));
        missing.setConfiguration(QStringLiteral("/nonexistent/verible-verilog-ls"), fixtureRoot);
        missing.setFileSet(QStringList{top});
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
