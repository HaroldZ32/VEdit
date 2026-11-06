// T11 evidence probe: real backend switch, capability gating, formatting and the
// two lint engines, driven through the SAME product seams (BackendSwitchState,
// DiagnosticsController, the FFI backends) with the checkout's real binaries.
//
//   w5-probe <evidence-dir> [slang-server-path]
//
// Required binaries: verible-verilog-ls / verible-verilog-format /
// verible-verilog-lint (checkout `.mpd/recon/verible/bin`) and slang
// (`build/slang/out`); slang-server is passed explicitly (it is a prebuilt
// spike artifact). Missing pieces are reported as SKIP, never as PASS.
#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "lsp_backend.h"
#include "slang_backend.h"
#include "toolchain.h"

#include <QCoreApplication>

#include <csignal>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <functional>
#include <memory>

using namespace vedit::workspace;
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

void pump(int ms = 20)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
}

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (predicate()) {
            return true;
        }
        QThread::msleep(5);
    }
    return predicate();
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(text.toUtf8()) >= 0;
}

QString firstLine(const QString &text)
{
    return text.section(QLatin1Char('\n'), 0, 0);
}

/// Run the verible formatter directly (the reference output a product-format
/// result must equal).
QString referenceFormat(const QString &formatter, const QString &text)
{
    QProcess process;
    process.start(formatter, {QStringLiteral("-")});
    if (!process.waitForStarted(3000)) {
        return QString();
    }
    process.write(text.toUtf8());
    process.closeWriteChannel();
    if (!process.waitForFinished(10000) || process.exitCode() != 0) {
        return QString();
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

struct Settings {
    QString lsp;
    QString engine;
    QString server;
    QString format;
    QString lint;
    QString slang;
};

Settings settingsFor(const QString &lsp, const QString &engine,
                     const ToolchainReport &toolchain, const QString &slangServer)
{
    Settings settings;
    settings.lsp = lsp;
    settings.engine = engine;
    settings.server = slangServer;
    settings.format = toolchain.veribleFormat.path;
    settings.lint = toolchain.veribleLint.path;
    settings.slang = toolchain.slang.path;
    return settings;
}

DiagnosticsBackendSettings toBackendSettings(const Settings &settings)
{
    DiagnosticsBackendSettings out;
    out.lspBackend = settings.lsp;
    out.lintEngine = settings.engine;
    out.slangServer = settings.server;
    out.veribleFormat = settings.format;
    out.slang = settings.slang;
    return out;
}

void dumpGates(const char *tag, const BackendSwitchState &state)
{
    const BackendCapabilities capabilities = state.capabilities();
    std::printf("CAPABILITIES tag=%s backend=%s basis=%s source='%s' rows=%lld\n", tag,
                qPrintable(capabilities.backend), qPrintable(capabilities.basis),
                qPrintable(capabilities.source),
                static_cast<long long>(capabilities.rows.size()));
    std::printf("CAP-NOTE tag=%s '%s'\n", tag, qPrintable(capabilities.note));
    for (const CapabilityRow &row : capabilities.rows) {
        std::printf("CAP-ROW tag=%s %s supported=%d reason='%s'\n", tag, qPrintable(row.action),
                    row.supported, qPrintable(row.reason));
    }
    for (const QString &action : QStringList{QStringLiteral("definition"),
                                             QStringLiteral("references"),
                                             QStringLiteral("rename"),
                                             QStringLiteral("hover"),
                                             QStringLiteral("completion"),
                                             QStringLiteral("inlayHint"),
                                             QStringLiteral("codeAction"),
                                             QStringLiteral("formatting"),
                                             QStringLiteral("signatureHelp")}) {
        const CapabilityDecision decision = state.gate(action);
        std::printf("CAP-GATE tag=%s %s enabled=%d known=%d source=%s reason='%s'\n", tag,
                    qPrintable(action), decision.enabled ? 1 : 0, decision.known ? 1 : 0,
                    qPrintable(decision.source), qPrintable(decision.reason));
    }
    std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv)
{
    // AGENT.md §T134: this process spawns real LSP children and writes into their
    // stdin. In a C++ host that statically links the Rust service layer, the Rust
    // runtime's own `signal(SIGPIPE, SIG_IGN)` never runs, so a write to a dead
    // child (verible-verilog-ls exiting while a request is in flight) would kill
    // the probe with exit 141 — the first T30 review run died exactly that way at
    // `switch.a.outline`. Ignoring it turns that into EPIPE, which the seams report
    // as a state; the probe must never die of a closed pipe.
    std::signal(SIGPIPE, SIG_IGN);

    QCoreApplication app(argc, argv);
    const QString outDir =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("evidence/w5-backend");
    QDir().mkpath(outDir);

    // --- resolve the toolchain (real checkout artifacts) -------------------
    ToolchainReport toolchain = resolveToolchain(applicationToolchainContext());
    QString slangServer = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                   : qEnvironmentVariable("VEDIT_SLANG_SERVER");
    std::printf("TOOLCHAIN %s %s\n", qPrintable(toolchain.statusLine()),
                qPrintable(toolchain.auxiliaryStatusLine()));
    std::printf("TOOLCHAIN-PATHS verible='%s' slang='%s' format='%s' lint='%s' "
                "slang-server='%s'\n",
                qPrintable(toolchain.verible.path), qPrintable(toolchain.slang.path),
                qPrintable(toolchain.veribleFormat.path), qPrintable(toolchain.veribleLint.path),
                qPrintable(slangServer));
    const bool backendBinaries = toolchain.verible.found() && toolchain.slang.found()
        && toolchain.veribleFormat.found() && toolchain.veribleLint.found();
    const bool slangServerFound = QFileInfo(slangServer).isExecutable();
    check("binaries.verible-toolchain", backendBinaries);
    check("binaries.slang-server", slangServerFound);
    if (!backendBinaries) {
        std::printf("SKIP: the verible/slang checkout artifacts are missing\n");
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return g_failures == 0 ? 0 : 1;
    }

    // --- fixture -----------------------------------------------------------
    QTemporaryDir fixture;
    check("fixture.temp-dir", fixture.isValid());
    const QString file = fixture.path() + QStringLiteral("/undeclared.sv");
    const QString text = QStringLiteral(
        "module top;\n"
        "  logic a b;\n"
        "\tlogic tabbed;\n"
        "  wire w = a "
        "really_long_unknown_signal_name_that_makes_this_line_long_enough_to_trip_the_line_"
        "length_rule_1234567890;\n"
        "endmodule\n");
    check("fixture.written", writeFile(file, text));
    const QFileInfo fileInfo(file);
    const QString root = fileInfo.absolutePath();

    auto lsp = createFfiLspSessionBackend();
    auto lint = createFfiSlangBackend(toolchain.slang.path);
    LspSessionBackend *lspRaw = lsp.get();
    SlangBackend *lintRaw = lint.get();
    DiagnosticsController controller(std::move(lsp), std::move(lint));
    controller.setProjectLintBackend(createFfiProjectLintBackend());
    BackendSwitchState state(lspRaw, lintRaw);

    // The backend the switch is expected to drive: two sessions over one file.
    const Settings veribleSettings = settingsFor(QStringLiteral("verible"), QStringLiteral("slang"),
                                                  toolchain, slangServer);
    const Settings slangSettings = settingsFor(QStringLiteral("slang"), QStringLiteral("verible"),
                                               toolchain, slangServer);

    // --- 1) verible live path ---------------------------------------------
    QElapsedTimer switchTimer;
    QString error;
    switchTimer.start();
    check("switch.a.applied", state.apply(toBackendSettings(veribleSettings), toolchain, &error));
    if (!error.isEmpty()) {
        std::printf("SWITCH-A-ERROR '%s'\n", qPrintable(error));
    }
    controller.setConfiguration(state.lspServerPath(), root, state.rulesPath());
    controller.setToolchainReport(state.toolchain());
    controller.setFileSet({});
    check("switch.a.cleared-before-restart", controller.diagnostics().isEmpty());
    controller.analyzeFile(file, text, false);
    const bool veribleReady = waitUntil([&controller]() {
        controller.refresh();
        return controller.state() == BackendState::Ready
            || controller.state() == BackendState::Degraded
            || controller.state() == BackendState::Failed;
    }, 20000);
    // A few refreshes collect the asynchronously published diagnostics/symbols.
    for (int round = 0; round < 40; ++round) {
        controller.refresh();
        pump(10);
    }
    const qint64 veribleMs = switchTimer.elapsed();
    std::printf("SWITCH-A backend=%s ready=%d ready-ms=%lld session='%s' diagnostics=%lld "
                "outline=%lld\n",
                qPrintable(state.lspBackendName()), veribleReady ? 1 : 0, veribleMs,
                qPrintable(controller.session().message),
                static_cast<long long>(controller.diagnostics().size()),
                static_cast<long long>(controller.outline().size()));
    std::printf("SWITCH-A-ARGV '%s'\n", qPrintable(state.commandLine()));
    check("switch.a.ready", veribleReady);
    check("switch.a.session-names-backend",
          lspRaw->status().message.contains(QStringLiteral("backend=verible")));
    check("switch.a.diagnostics", !controller.diagnostics().isEmpty());
    const QString cleanFile = fixture.path() + QStringLiteral("/clean.sv");
    const QString cleanText = QStringLiteral("module clean_top;\n  logic clk;\n  "
                                             "function automatic logic inc(input logic v);\n"
                                             "    inc = v;\n  endfunction\nendmodule\n");
    writeFile(cleanFile, cleanText);
    controller.analyzeFile(cleanFile, cleanText, false);
    waitUntil([&controller]() {
        controller.refresh();
        return !controller.outline().isEmpty()
            || controller.state() == BackendState::Failed;
    }, 15000);
    for (int round = 0; round < 20; ++round) {
        controller.refresh();
        pump(10);
    }
    std::printf("OUTLINE-A symbols=%lld names='%s' backend='%s'\n",
                static_cast<long long>(controller.outline().size()),
                controller.outline().isEmpty() ? "" : qPrintable(controller.outline().first().name),
                qPrintable(lspRaw->status().message));
    check("switch.a.outline", !controller.outline().isEmpty());
    check("switch.a.command-line",
          state.commandLine().contains(QStringLiteral("verible-verilog-ls")));
    dumpGates("verible", state);

    // --- 2) switch to slang (no restart; time to the new diagnostics) ------
    switchTimer.restart();
    check("switch.b.applied", state.apply(toBackendSettings(slangSettings), toolchain, &error));
    if (!error.isEmpty()) {
        std::printf("SWITCH-B-ERROR '%s'\n", qPrintable(error));
    }
    controller.setConfiguration(state.lspServerPath(), root, state.rulesPath());
    controller.setToolchainReport(state.toolchain());
    controller.setFileSet({});
    const bool clearedOnSwitch = controller.diagnostics().isEmpty();
    std::printf("SWITCH-B cleared-old-rows=%d\n", clearedOnSwitch ? 1 : 0);
    check("switch.b.old-rows-cleared", clearedOnSwitch);
    controller.analyzeFile(file, text, false);
    const bool slangReady = waitUntil([&controller]() {
        controller.refresh();
        return controller.state() == BackendState::Ready
            || controller.state() == BackendState::Degraded
            || controller.state() == BackendState::Failed;
    }, 20000);
    const qint64 readyMs = switchTimer.elapsed();
    const bool slangDiagnostics = waitUntil([&controller, &switchTimer, &slangReady]() {
        if (!slangReady) {
            return true;
        }
        controller.refresh();
        return !controller.diagnostics().isEmpty() || switchTimer.elapsed() > 5000;
    }, 6000);
    for (int round = 0; round < 20; ++round) {
        controller.refresh();
        pump(10);
    }
    const qint64 diagnosticsMs = switchTimer.elapsed();
    std::printf("SWITCH-B lastError='%s' lspMessage='%s' state=%d\n",
                qPrintable(controller.lastError()),
                qPrintable(controller.fileSetReport().lspMessage),
                static_cast<int>(controller.state()));
    std::printf("SWITCH-B backend=%s ready=%d ready-ms=%lld diagnostics-ms=%lld session='%s' "
                "diagnostics=%lld outline=%lld\n",
                qPrintable(state.lspBackendName()), slangReady ? 1 : 0, readyMs, diagnosticsMs,
                qPrintable(controller.session().message),
                static_cast<long long>(controller.diagnostics().size()),
                static_cast<long long>(controller.outline().size()));
    std::printf("SWITCH-B-ARGV '%s'\n", qPrintable(state.commandLine()));
    check("switch.b.ready", slangReady);
    check("switch.b.ready-within-5s", readyMs <= 5000);
    check("switch.b.diagnostics-within-5s", slangDiagnostics && diagnosticsMs <= 5000);
    check("switch.b.session-names-backend",
          lspRaw->status().message.contains(QStringLiteral("backend=slang")));
    check("switch.b.diagnostics", !controller.diagnostics().isEmpty());
    controller.analyzeFile(cleanFile, cleanText, false);
    const bool slangOutline = waitUntil([&controller]() {
        controller.refresh();
        return !controller.outline().isEmpty() || controller.state() == BackendState::Failed;
    }, 15000);
    std::printf("OUTLINE-B symbols=%lld backend='%s'\n",
                static_cast<long long>(controller.outline().size()),
                qPrintable(lspRaw->status().message));
    check("switch.b.outline", slangOutline && !controller.outline().isEmpty());
    check("switch.b.command-line",
          state.commandLine().contains(QStringLiteral("slang-server")));
    dumpGates("slang", state);

    // --- 3) gating rules (formatting + signatureHelp + unknown) ------------
    const CapabilityDecision formatVerible = state.gate(QStringLiteral("formatting"));
    check("gate.formatting-enabled-under-slang", formatVerible.enabled);
    check("gate.formatting-source", formatVerible.source == QStringLiteral("always-verible-format"));
    const CapabilityDecision signatureSlang = state.gate(QStringLiteral("signatureHelp"));
    check("gate.signature-help-editor-synthesised",
          signatureSlang.source == QStringLiteral("editor-synthesised"));
    check("gate.signature-help-not-gated-by-table", !signatureSlang.enabled);
    BackendCapabilities unknownTable;
    unknownTable.backend = QStringLiteral("slang");
    CapabilityRow unknownRow;
    unknownRow.action = QStringLiteral("hover");
    unknownRow.supported = -1;
    unknownRow.reason = QString();
    unknownTable.rows.append(unknownRow);
    const CapabilityDecision unknownGate =
        decideActionGate(unknownTable, QStringLiteral("hover"), true, false);
    std::printf("GATE-UNKNOWN enabled=%d known=%d reason='%s'\n", unknownGate.enabled ? 1 : 0,
                unknownGate.known ? 1 : 0, qPrintable(unknownGate.reason));
    check("gate.unknown-not-claimed", !unknownGate.enabled && !unknownGate.known);
    check("gate.unknown-says-connect-to-know",
          unknownGate.reason.contains(QStringLiteral("connect to know")));

    // Apply verible again to prove formatting is backend-independent.
    check("switch.c.applied", state.apply(toBackendSettings(veribleSettings), toolchain, &error));
    const CapabilityDecision formatVeribleAgain = state.gate(QStringLiteral("formatting"));
    check("gate.formatting-enabled-under-verible", formatVeribleAgain.enabled);

    // --- 4) format with verible-verilog-format (both selections) -----------
    // T50: the previous sample had a SYNTAX ERROR (`wire w=a b;`), so
    // verible-verilog-format wrote no output and `format-input.sv` /
    // `format-output.sv` were byte-identical — a picture of nothing. This input is
    // valid and the formatter really re-indents it (2 spaces, one statement per
    // line), so the evidence files differ and the check has something to assert.
    const QString unformatted =
        QStringLiteral("module top;\n   logic   a ;\n  endmodule\n");
    QString formattedSlang;
    QString formatError;
    check("format.under-slang",
          formatVerilog(toolchain.veribleFormat.path, unformatted, &formattedSlang, &formatError));
    const QString reference = referenceFormat(toolchain.veribleFormat.path, unformatted);
    check("format.matches-verible-cli", !reference.isEmpty() && formattedSlang == reference);
    const QString formatPath = outDir + QStringLiteral("/format-output.sv");
    writeFile(outDir + QStringLiteral("/format-input.sv"), unformatted);
    writeFile(formatPath, formattedSlang);
    std::printf("FORMAT backend=slang formatter='%s' output-first-line='%s'\n",
                qPrintable(toolchain.veribleFormat.path),
                qPrintable(firstLine(formattedSlang)));

    // --- 5) lint engines (project batch path, engine = selected) ----------
    const auto runProjectPass = [&controller, &state](const QString &label, const QString &file) {
        LintPassRequest request;
        request.files = QStringList{file};
        request.engine = static_cast<int>(state.lintKind());
        request.slangPath = state.toolchain().slang.path;
        request.veriblePath = state.toolchain().veribleLint.path;
        request.rulesPath = state.rulesPath();
        QString passError;
        const bool started = controller.startProjectPass(request, &passError);
        const bool done = started && waitUntil([&controller]() {
            controller.refresh();
            return controller.projectPass().terminal();
        }, 30000);
        const LintPassSnapshot snapshot = controller.projectPass();
        std::printf("PROJECT-PASS label=%s engine=%s engine-id=%d started=%d terminal=%d "
                    "phase=%d done=%d/%d tool='%s' cli-diagnostics=%lld message='%s'\n",
                    qPrintable(label), qPrintable(state.lintEngineName()),
                    static_cast<int>(state.lintKind()), started ? 1 : 0, done ? 1 : 0,
                    static_cast<int>(snapshot.phase), snapshot.done, snapshot.total,
                    qPrintable(snapshot.tool),
                    static_cast<long long>(controller.cliDiagnostics().size()),
                    qPrintable(snapshot.message));
        std::fflush(stdout);
        check(qPrintable(QStringLiteral("pass.%1.terminal").arg(label)), done);
        check(qPrintable(QStringLiteral("pass.%1.diagnostics").arg(label)),
              !controller.cliDiagnostics().isEmpty());
        return static_cast<int>(state.lintKind());
    };

    // slang settings select the verible lint engine; verible settings select slang.
    check("switch.d.applied", state.apply(toBackendSettings(slangSettings), toolchain, &error));
    const int veribleEngine = runProjectPass(QStringLiteral("verible-lint"), file);
    checkLong("engine.verible-id", veribleEngine, 1);
    check("switch.e.applied", state.apply(toBackendSettings(veribleSettings), toolchain, &error));
    const int slangEngine = runProjectPass(QStringLiteral("slang"), file);
    checkLong("engine.slang-id", slangEngine, 0);

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
