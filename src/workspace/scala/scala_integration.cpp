#include "scala_integration.h"

#include "scala_run.h"

#include "../../terminal/terminal_panel.h"
#include "../../terminal/terminal_widget.h"

#if defined(VEDIT_SCALA_HAVE_TOOLCHAIN)
#include "../diagnostics/toolchain.h"
#endif

#include "app_shell.h"
#include "command_registry.h"
#include "panel_registry.h"

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QStatusBar>
#include <QString>

namespace vedit::workspace {

namespace {

/// The lane's prefix in the terminal, so the run's output is distinguishable
/// from the interactive shell's own lines around it.
QByteArray banner(const QString &text)
{
    return QStringLiteral("\r\n[scala] %1\r\n").arg(text).toUtf8();
}

/// LF -> CRLF for the VT screen. The child does not run on a tty, so nothing
/// translates its newlines; without this every line would start at the previous
/// line's last column (the classic staircase). A `\r\n` already present is kept.
QByteArray normaliseLineEndings(const QByteArray &bytes)
{
    QByteArray out;
    out.reserve(bytes.size() + 16);
    char previous = '\0';
    for (const char byte : bytes) {
        if (byte == '\n' && previous != '\r') {
            out.append('\r');
        }
        out.append(byte);
        previous = byte;
    }
    return out;
}

/// Append run output to the shell's EXISTING Terminal panel (AC-7.5).
///
/// The dock belongs to the terminal lane; this lane only feeds its screen, which
/// is what "output goes into the terminal panel" means here — no second console
/// is created. A build without the terminal lane reports the text on the status
/// bar instead of swallowing it.
void appendToTerminal(vedit::app::ApplicationShell &shell, const QByteArray &bytes)
{
    shell.panels()->showPanel(QStringLiteral("terminal"), &shell);
    QDockWidget *dock = shell.panels()->dock(QStringLiteral("terminal"));
    auto *panel = dock != nullptr
                      ? qobject_cast<vedit::terminal::TerminalPanel *>(dock->widget())
                      : nullptr;
    if (panel == nullptr || panel->terminal() == nullptr) {
        if (shell.statusBar() != nullptr) {
            shell.statusBar()->showMessage(QString::fromUtf8(bytes).trimmed(), 8000);
        }
        return;
    }
    const QByteArray text = normaliseLineEndings(bytes);
    panel->terminal()->screen().feed(text.constData(), static_cast<std::size_t>(text.size()));
    panel->terminal()->scrollToBottom();
    panel->terminal()->update();
}

/// The toolchain the run needs. The diagnostics lane already owns the ONE
/// resolution order (bundled > local > PATH > env, T12); this lane reuses it
/// instead of growing a second one.
scala::ScalaTools resolveScalaTools()
{
    scala::ScalaTools tools;
#if defined(VEDIT_SCALA_HAVE_TOOLCHAIN)
    const diagnostics::ToolchainSearchContext context =
        diagnostics::applicationToolchainContext();
    const diagnostics::ToolchainReport report = diagnostics::resolveToolchain(context);
    if (report.scalaCli.found()) {
        tools.scalaCli = report.scalaCli.path;
    }
    if (report.sbt.found()) {
        tools.sbt = report.sbt.path;
    }
    if (report.scalaJava.found()) {
        tools.java = report.scalaJava.path;
    }
#endif
    return tools;
}

QString describeCompletion(int stateCode, int exitCode, const QString &program,
                           const QString &error)
{
    switch (static_cast<scala::RunState>(stateCode)) {
    case scala::RunState::Exited:
        return QCoreApplication::translate("VEdit", "%1 exited with code %2.")
            .arg(program)
            .arg(exitCode);
    case scala::RunState::Failed:
        return error.isEmpty()
                   ? QCoreApplication::translate("VEdit", "%1 failed.").arg(program)
                   : QCoreApplication::translate("VEdit", "%1 failed: %2").arg(program, error);
    case scala::RunState::Cancelled:
        return QCoreApplication::translate(
                   "VEdit", "%1 was cancelled; its process tree was killed.")
            .arg(program);
    case scala::RunState::TimedOut:
        return QCoreApplication::translate(
                   "VEdit", "%1 timed out; its process tree was killed.")
            .arg(program);
    case scala::RunState::Idle:
    case scala::RunState::Running:
        break;
    }
    return QString();
}

/// One click: detect the shape, build the plan, show the terminal, start the job.
void runScala(vedit::app::ApplicationShell &shell, scala::ScalaRun &run)
{
    if (run.isRunning()) {
        appendToTerminal(shell, banner(QCoreApplication::translate(
                                        "VEdit", "a Scala run is already active; stop it first.")));
        return;
    }
    const QString root = shell.projectRoot();
    const scala::ShapeReport shape = scala::detectProjectShape(root);
    const scala::RunPlan plan = scala::buildRunPlan(root, shape, resolveScalaTools());

    shell.panels()->showPanel(QStringLiteral("terminal"), &shell);
    if (!plan.ok) {
        appendToTerminal(shell, banner(QCoreApplication::translate(
                                        "VEdit", "cannot run: %1")
                                        .arg(plan.error)));
        if (shell.statusBar() != nullptr) {
            shell.statusBar()->showMessage(plan.error, 8000);
        }
        return;
    }
    appendToTerminal(shell, banner(QStringLiteral("$ ") + plan.argv.join(QLatin1Char(' '))));
    if (!run.start(plan)) {
        appendToTerminal(shell, banner(QCoreApplication::translate(
                                        "VEdit", "cannot run: %1")
                                        .arg(run.error())));
        if (shell.statusBar() != nullptr) {
            shell.statusBar()->showMessage(run.error(), 8000);
        }
    }
}

} // namespace

void installScala(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    (void)options;

    // Owned by the shell: one run slot per window, gone with the window.
    auto *run = new scala::ScalaRun(&shell);
    run->setOutputSink([&shell](const QByteArray &bytes) { appendToTerminal(shell, bytes); });

    QObject::connect(run, &scala::ScalaRun::outputTruncated, &shell, [&shell]() {
        appendToTerminal(shell, banner(QCoreApplication::translate(
                                        "VEdit", "older output was dropped: the run produced "
                                                 "more than the engine keeps.")));
    });
    QObject::connect(
        run, &scala::ScalaRun::finished, &shell,
        [&shell, run](int stateCode, int exitCode, const QString &program) {
            const QString line =
                describeCompletion(stateCode, exitCode, program, run->error());
            appendToTerminal(shell, banner(line));
            if (shell.statusBar() != nullptr) {
                shell.statusBar()->showMessage(line, 8000);
            }
        });

    vedit::app::CommandSpec runSpec;
    runSpec.id = QStringLiteral("scala.run");
    runSpec.text = QCoreApplication::translate("VEdit", "&Run Scala Project");
    runSpec.menu = QStringLiteral("project");
    // No default shortcut: the product does not claim a run key in this batch,
    // and a dead/ambiguous binding is worse than none (RM-5b).
    runSpec.defaultShortcut = QKeySequence();
    runSpec.order = 55;
    if (shell.commands()->add(runSpec) != nullptr) {
        shell.commands()->setHandler(runSpec.id, [&shell, run]() { runScala(shell, *run); });
    }

    vedit::app::CommandSpec stopSpec;
    stopSpec.id = QStringLiteral("scala.stop");
    stopSpec.text = QCoreApplication::translate("VEdit", "&Stop Scala Run");
    stopSpec.menu = QStringLiteral("project");
    stopSpec.defaultShortcut = QKeySequence();
    stopSpec.order = 56;
    if (shell.commands()->add(stopSpec) != nullptr) {
        shell.commands()->setHandler(stopSpec.id, [&shell, run]() {
            if (!run->isRunning()) {
                appendToTerminal(shell, banner(QCoreApplication::translate(
                                                "VEdit", "no Scala run is active.")));
                return;
            }
            appendToTerminal(shell, banner(QCoreApplication::translate(
                                            "VEdit", "cancelling the run (the process tree is "
                                                     "killed)…")));
            run->cancel();
        });
    }
}

} // namespace vedit::workspace
