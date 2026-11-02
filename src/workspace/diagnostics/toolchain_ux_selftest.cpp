// Headless UX acceptance for the missing-toolchain experience (task T85).
//
// The user's bare build had no verible/slang: the panels showed a wall of text
// (every probed candidate path) instead of one panel-level status, and the
// "diagnostics" surface repeated it. This binary asserts the acceptance:
//
//   * MISSING toolchain: the panel has exactly ONE concise status line, ONE
//     one-line outline empty state, ZERO outline rows and ZERO fabricated
//     diagnostics entries (a missing environment is not a file error);
//   * repeated reparses keep that one line (no per-file/per-request pile);
//   * AVAILABLE toolchain: the checkout's own artifacts are discovered (`local`
//     source), the missing status disappears, and a real Verible session
//     produces real outline symbols through the C ABI (FFI builds only).
//
// Everything is offscreen; GUI appearance is out of scope (no display server).
#include "app_shell.h"
#include "diagnostics_controller.h"
#include "diagnostics_panel.h"
#include "lsp_backend.h"
#include "slang_backend.h"
#include "toolchain.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include <chrono>
#include <cstdio>
#include <memory>

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
#include <vd_ffi.h>
#endif

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
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
    std::fflush(stdout);
}

void skip(const char *name, const QString &reason)
{
    ++g_skips;
    std::printf("SKIP %s  %s\n", name, reason.toUtf8().constData());
    std::fflush(stdout);
}

bool writeFixture(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(QByteArrayLiteral("module top;\n  assign out = 1'b1;\nendmodule\n")) > 0;
}

int countOccurrences(const QString &text, const QString &needle)
{
    int count = 0;
    int from = 0;
    while (true) {
        const int at = text.indexOf(needle, from);
        if (at < 0) {
            break;
        }
        ++count;
        from = at + needle.size();
    }
    return count;
}

// ---------------------------------------------------------------------------
// 1) the missing-toolchain experience
// ---------------------------------------------------------------------------
void missingScenario()
{
    ToolchainSearchContext empty; // no bundled/local/PATH/env candidates at all
    const ToolchainReport report = resolveToolchain(empty);
    check("missing.both-tools", report.missing().size() == 2,
          report.missing().join(QLatin1Char(',')));
    const QString message = report.userMessage();
    std::printf("UX-MISSING-STATUS %s\n", qPrintable(message));

    check("missing.message-single-line", !message.contains(QLatin1Char('\n')), message);
    check("missing.message-concise", message.size() <= 500, QString::number(message.size()));
    check("missing.message-no-probe-wall",
          !message.contains(QStringLiteral("probed"))
              && !message.contains(QStringLiteral("/usr/local/sbin")),
          message);
    check("missing.message-names-both-tools",
          message.contains(QStringLiteral("verible-verilog-ls"))
              && message.contains(QStringLiteral("slang")));
    check("missing.message-names-impact",
          message.contains(QStringLiteral("Impact:"))
              && message.contains(QStringLiteral("outline")));
    check("missing.message-names-three-fixes",
          message.contains(QStringLiteral("bin/"))
              && message.contains(QStringLiteral("PATH"))
              && message.contains(QStringLiteral("VEDIT_VERIBLE_LS"))
              && message.contains(QStringLiteral("VEDIT_SLANG"))
              && message.contains(QStringLiteral("scripts/fetch-verible.sh"))
              && message.contains(QStringLiteral("build/slang/build.sh")),
          message);
    check("missing.message-once",
          countOccurrences(message, QStringLiteral("Diagnostics unavailable")) == 1, message);
    check("missing.probe-detail-separate",
          report.probeDetail().contains(QStringLiteral("probed"))
              && !message.contains(report.probeDetail()));

    // The panel the user reads: one status line + one empty-state line, no rows.
    DiagnosticsController controller(nullptr, nullptr);
    controller.setToolchainReport(report);
    QTemporaryDir scratch;
    check("missing.scratch", scratch.isValid());
    QStringList files;
    for (int index = 0; index < 3; ++index) {
        const QString path = scratch.path() + QStringLiteral("/f%1.v").arg(index);
        if (writeFixture(path)) {
            files.append(path);
        }
    }
    controller.setFileSet(files);
    controller.refresh();

    DiagnosticsPanel panel(&controller);
    panel.show();
    QCoreApplication::processEvents();
    const QString panelStatus = panel.statusText();
    std::printf("UX-PANEL-STATUS %s\n", qPrintable(panelStatus));
    std::printf("UX-PANEL-OUTLINE rows=%d empty-state=\"%s\"\n", panel.outlineRowCount(),
                qPrintable(panel.emptyStateText()));

    check("panel.status-single-line", !panelStatus.contains(QLatin1Char('\n')), panelStatus);
    check("panel.status-concise", panelStatus.size() <= 500, QString::number(panelStatus.size()));
    check("panel.status-mentions-both-tools",
          panelStatus.contains(QStringLiteral("verible-verilog-ls"))
              && panelStatus.contains(QStringLiteral("slang")));
    check("panel.status-no-probe-wall", !panelStatus.contains(QStringLiteral("probed")));
    check("panel.outline-zero-rows", panel.outlineRowCount() == 0,
          QString::number(panel.outlineRowCount()));
    check("panel.empty-state-visible", panel.emptyStateVisible());
    check("panel.empty-state-single-line",
          !panel.emptyStateText().contains(QLatin1Char('\n')) && !panel.emptyStateText().isEmpty(),
          panel.emptyStateText());
    check("panel.empty-state-mentions-missing",
          panel.emptyStateText().contains(QStringLiteral("No symbols"))
              && panel.emptyStateText().contains(QStringLiteral("verible"))
              && panel.emptyStateText().contains(QStringLiteral("slang")),
          panel.emptyStateText());
    check("panel.no-fabricated-diagnostics", controller.diagnostics().isEmpty(),
          QString::number(controller.diagnostics().size()));
    bool anyErrorEntry = false;
    for (const DiagnosticEntry &entry : controller.diagnostics()) {
        if (entry.severity == 1) {
            anyErrorEntry = true;
        }
    }
    check("panel.no-error-entries", !anyErrorEntry);
    check("panel.degraded-not-crash", controller.state() == BackendState::Failed
                                          || controller.state() == BackendState::Degraded);

    // Repeated reparses must not multiply the status or add entries.
    const QString before = panel.statusText();
    for (int round = 0; round < 3; ++round) {
        controller.reparse();
        controller.refresh();
        panel.refreshFromController();
    }
    QCoreApplication::processEvents();
    check("repeat.status-unchanged", panel.statusText() == before, panel.statusText());
    check("repeat.still-single-line", !panel.statusText().contains(QLatin1Char('\n')));
    check("repeat.no-extra-entries", controller.diagnostics().isEmpty(),
          QString::number(controller.diagnostics().size()));
    check("repeat.outline-still-empty", panel.outlineRowCount() == 0);

    // The "diagnostics" surface of the shell: the same concise line, exactly
    // once (the shell deduplicates), and never the probe wall.
    vedit::app::ShellOptions shellOptions;
    shellOptions.stateDir = scratch.path() + QStringLiteral("/shell-state");
    shellOptions.restoreSession = false;
    shellOptions.showDefaultPanels = false;
    vedit::app::ApplicationShell shell(shellOptions);
    const int diagnosticsBefore = shell.diagnostics().size();
    shell.addShellDiagnostic(report.userMessage());
    shell.addShellDiagnostic(report.userMessage()); // duplicate must be ignored
    check("shell.diagnostics-single-entry", shell.diagnostics().size() == diagnosticsBefore + 1,
          QString::number(shell.diagnostics().size()));
    const QString shellLine = shell.diagnostics().isEmpty() ? QString()
                                                            : shell.diagnostics().last();
    check("shell.diagnostics-concise", shellLine.size() <= 500, QString::number(shellLine.size()));
    check("shell.diagnostics-no-probe-wall",
          !shellLine.contains(QStringLiteral("probed"))
              && !shellLine.contains(QStringLiteral("/usr/local/sbin")),
          shellLine);
    check("shell.diagnostics-single-line", !shellLine.contains(QLatin1Char('\n')), shellLine);
}

// ---------------------------------------------------------------------------
// 2) the available-toolchain path (local checkout discovery)
// ---------------------------------------------------------------------------
QString firstExistingFile(const QStringList &directories, const QString &name)
{
    for (const QString &directory : directories) {
        const QFileInfo candidate(directory + QLatin1Char('/') + name);
        if (candidate.isFile() && candidate.isExecutable()) {
            return candidate.absoluteFilePath();
        }
    }
    return QString();
}

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
bool waitForTerminal(DiagnosticsController &controller, int maxRounds)
{
    for (int round = 0; round < maxRounds; ++round) {
        controller.refresh();
        const BackendState state = controller.state();
        if (state == BackendState::Ready || state == BackendState::Degraded
            || state == BackendState::Failed) {
            return true;
        }
        QCoreApplication::processEvents();
        QThread::msleep(25);
    }
    return false;
}

bool waitForPolled(DiagnosticsController &controller, LspPolled *out, int maxRounds)
{
    for (int round = 0; round < maxRounds; ++round) {
        const LspPolled polled = controller.pollNavigation();
        if (!polled.pending) {
            *out = polled;
            return true;
        }
        QCoreApplication::processEvents();
        QThread::msleep(25);
    }
    return false;
}
#endif

void availableScenario(const QString &fixtureRoot)
{
    const ToolchainSearchContext application = applicationToolchainContext();
    check("available.local-candidates-known", !application.localDirs.isEmpty(),
          application.localDirs.join(QLatin1Char(',')));

    // Resolve with PATH and env emptied: only bundled + the checkout's own
    // artifacts remain, so a "local" hit proves the bare-build path works.
    ToolchainSearchContext localOnly;
    localOnly.localDirs = application.localDirs;
    const ToolchainReport report = resolveToolchain(localOnly);
    std::printf("UX-AVAILABLE-STATUS %s\n", qPrintable(report.statusLine()));

    const QString slangPath = firstExistingFile(application.localDirs, QStringLiteral("slang"));
    const QString veriblePath =
        firstExistingFile(application.localDirs, QStringLiteral("verible-verilog-ls"));
    if (!slangPath.isEmpty()) {
        check("available.slang-from-local",
              report.slang.found() && report.slang.sourceLabel() == QStringLiteral("local"),
              report.slang.describe());
    } else {
        skip("available.slang-from-local", QStringLiteral("build/slang/out/slang not built here"));
    }
    if (!veriblePath.isEmpty()) {
        check("available.verible-from-local",
              report.verible.found() && report.verible.sourceLabel() == QStringLiteral("local"),
              report.verible.describe());
    } else {
        skip("available.verible-from-local",
             QStringLiteral("run scripts/fetch-verible.sh in a checkout to enable this"));
    }

    if (!slangPath.isEmpty() && !veriblePath.isEmpty()) {
        check("available.no-missing", report.missing().isEmpty(),
              report.missing().join(QLatin1Char(',')));
        check("available.no-user-message", report.userMessage().isEmpty(),
              report.userMessage());
        DiagnosticsController controller(nullptr, nullptr);
        controller.setToolchainReport(report);
        DiagnosticsPanel panel(&controller);
        panel.show();
        QCoreApplication::processEvents();
        check("available.panel-no-unavailable-text",
              !panel.statusText().contains(QStringLiteral("unavailable")),
              panel.statusText());
    }

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
    if (veriblePath.isEmpty() || fixtureRoot.isEmpty() || !QDir(fixtureRoot).exists()) {
        skip("available.real-outline-symbols",
             QStringLiteral("no local verible-verilog-ls / fixture"));
        return;
    }
    const QString top = fixtureRoot + QStringLiteral("/rtl/top.sv");
    DiagnosticsController controller(createFfiLspSessionBackend(), createFfiSlangBackend());
    controller.setToolchainReport(report);
    controller.setConfiguration(veriblePath, fixtureRoot);
    controller.setFileSet(QStringList{top});
    const bool terminal = waitForTerminal(controller, 200);
    check("available.real-session-terminal", terminal, QString::number(static_cast<int>(controller.state())));
    check("available.real-session-ready-not-failed", controller.state() != BackendState::Failed,
          controller.lastError());
    const quint64 request = controller.requestSymbols(top);
    check("available.real-symbols-queued", request != 0);
    LspPolled polled;
    const bool answered = waitForPolled(controller, &polled, 200);
    check("available.real-symbols-answered",
          answered && polled.ready && polled.kind == ResponseKind::Symbols,
          polled.failed ? polled.error : QString());
    check("available.real-symbols-non-empty", !controller.outline().isEmpty(),
          QString::number(controller.outline().size()));
    DiagnosticsPanel panel(&controller);
    panel.show();
    QCoreApplication::processEvents();
    check("available.real-panel-has-symbols", panel.outlineRowCount() > 0,
          QString::number(panel.outlineRowCount()));
    check("available.real-panel-no-unavailable-text",
          !panel.statusText().contains(QStringLiteral("unavailable")), panel.statusText());
#else
    skip("available.real-outline-symbols",
         QStringLiteral("this build has no Rust service layer (FFI)"));
#endif
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-toolchain-ux-selftest"));
    QString fixtureRoot;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--fixture") && index + 1 < argc) {
            fixtureRoot = QString::fromLocal8Bit(argv[++index]);
        }
    }

    missingScenario();
    availableScenario(fixtureRoot);

    std::printf("TOOLCHAIN-UX-SELFTEST ok=%d checks=%d failures=%d skipped=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
