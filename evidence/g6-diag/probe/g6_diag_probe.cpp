// T29 / AC-8.1 product probe: the Format capability on a TRUE first launch.
//
// "True" means an EMPTY state directory: this probe never creates
// `<state>/settings.toml` - not before the shell is constructed and not after.
// It then drives the REAL product shell (ApplicationShell + installDiagnostics)
// with the checkout's REAL verible-verilog-format and checks, in this order:
//
//   1. the preconditions (state dir empty, no settings.toml) are observed and
//      printed BEFORE the shell exists;
//   2. the toolchain the shell will probe is resolved independently and printed;
//   3. the published `formatting` gate (vedit.diagnostics.gates) and the
//      `diagnostics.format` action agree with that real capability;
//   4. when the formatter is present the action is enabled AND a real trigger
//      reformats the open document to the formatter's own CLI output;
//   5. when it is absent the action is disabled and the reason names the tool
//      and the candidates that were actually probed (no invented reason);
//   6. no settings.toml exists afterwards either (the AC's no-bypass clause).
//
//   g6-diag-probe --expect=available|missing [--evidence-dir=<dir>]
//
// Exit 0 = every check passed. A wrong `--expect` (the toolchain disagrees with
// the requested mode) exits 3 instead of pretending the run is meaningful.
#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "kernel/ktexteditor_provider.h"
#include "panel_registry.h"

#include "diagnostics/diagnostics_controller.h"
#include "diagnostics/diagnostics_integration.h"
#include "diagnostics/toolchain.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

#include <cstdio>
#include <functional>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : " detail='",
                detail.isEmpty() ? "" : qPrintable(detail + QStringLiteral("'")));
    std::fflush(stdout);
}

void observe(const char *name, const QString &detail)
{
    std::printf("OBSERVED %s %s\n", name, qPrintable(detail));
    std::fflush(stdout);
}

bool spinUntil(const std::function<bool()> &done, int timeoutMs)
{
    if (done()) {
        return true;
    }
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(25);
    bool ok = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (done()) {
            ok = true;
            loop.quit();
        }
    });
    poll.start();
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return done() || ok;
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(text.toUtf8()) >= 0;
}

QString sha256Of(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString entryListing(const QString &directory)
{
    const QStringList entries =
        QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    return entries.isEmpty() ? QStringLiteral("<empty>") : entries.join(QLatin1Char(','));
}

QVariantMap gatesOf(vedit::app::ApplicationShell &shell)
{
    return shell.property("vedit.diagnostics.gates").toMap();
}

void printGate(vedit::app::ApplicationShell &shell, const QString &action)
{
    const QVariantMap row = gatesOf(shell).value(action).toMap();
    std::printf("GATE action=%s present=%d enabled=%d known=%d source='%s' reason='%s'\n",
                qPrintable(action), row.isEmpty() ? 0 : 1,
                row.value(QStringLiteral("enabled")).toBool() ? 1 : 0,
                row.value(QStringLiteral("known"), true).toBool() ? 1 : 0,
                qPrintable(row.value(QStringLiteral("source")).toString()),
                qPrintable(row.value(QStringLiteral("reason")).toString()));
    std::fflush(stdout);
}

/// The formatter's own answer for `text`, the reference a product format must equal.
QString referenceFormat(const QString &formatterPath, const QString &text)
{
    QProcess process;
    process.start(formatterPath, {QStringLiteral("-")});
    if (!process.waitForStarted(3000)) {
        return QString();
    }
    process.write(text.toUtf8());
    process.closeWriteChannel();
    if (!process.waitForFinished(15000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return QString();
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    bool expectAvailable = true;
    QString evidenceDir;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--expect=available")) {
            expectAvailable = true;
        } else if (argument == QStringLiteral("--expect=missing")) {
            expectAvailable = false;
        } else if (argument.startsWith(QStringLiteral("--evidence-dir="))) {
            evidenceDir = argument.mid(QStringLiteral("--evidence-dir=").size());
        }
    }
    if (!evidenceDir.isEmpty()) {
        QDir().mkpath(evidenceDir);
    }
    observe("platform", QGuiApplication::platformName());
    observe("expect", expectAvailable ? QStringLiteral("available") : QStringLiteral("missing"));

    QTemporaryDir workspace;
    if (!workspace.isValid()) {
        std::printf("FATAL: no temporary directory\n");
        return 2;
    }
    const QString project = workspace.path() + QStringLiteral("/project");
    const QString stateDir = workspace.path() + QStringLiteral("/state");
    QDir().mkpath(project);
    QDir().mkpath(stateDir);
    const QString settingsPath = stateDir + QStringLiteral("/settings.toml");

    // Deliberately unformatted but valid SystemVerilog: one line where the
    // formatter must produce several.
    const QString formatText = QStringLiteral(
        "module fmt;logic x;always_comb begin\nx=1'b0;\nend\nendmodule\n");
    const QString formatPath = project + QStringLiteral("/format.sv");
    if (!writeFile(formatPath, formatText)) {
        std::printf("FATAL: could not write the fixture\n");
        return 2;
    }

    // --- 1. the false-start precondition, observed BEFORE any shell exists ----
    std::printf("PRECONDITION state-dir='%s' entries=%s settings-toml=%s\n",
                qPrintable(stateDir), qPrintable(entryListing(stateDir)),
                QFileInfo::exists(settingsPath) ? "present" : "absent");
    std::fflush(stdout);
    check("precondition.state-dir-empty", entryListing(stateDir) == QStringLiteral("<empty>"),
          entryListing(stateDir));
    check("precondition.no-settings-toml", !QFileInfo::exists(settingsPath), settingsPath);
    check("precondition.file-exists", QFileInfo::exists(formatPath), formatPath);

    // --- 2. the real capability, resolved exactly as installDiagnostics will ---
    const vedit::workspace::diagnostics::ToolchainSearchContext toolchainContext =
        vedit::workspace::diagnostics::applicationToolchainContext();
    const vedit::workspace::diagnostics::ToolchainReport toolchain =
        vedit::workspace::diagnostics::resolveToolchain(toolchainContext);
    const vedit::workspace::diagnostics::ToolResolution &formatter = toolchain.veribleFormat;
    std::printf("TOOLCHAIN-RESOLVED %s %s\n", qPrintable(toolchain.statusLine()),
                qPrintable(toolchain.auxiliaryStatusLine()));
    std::printf("TOOLCHAIN-FORMAT found=%d source='%s' path='%s' env='%s'\n",
                formatter.found() ? 1 : 0, qPrintable(formatter.sourceLabel()),
                qPrintable(formatter.path), qPrintable(formatter.envName));
    std::fflush(stdout);
    if (formatter.found() != expectAvailable) {
        std::printf("FATAL: --expect=%s but the real toolchain says verible-format "
                    "found=%d (path='%s'); this run would not measure what it claims\n",
                    expectAvailable ? "available" : "missing", formatter.found() ? 1 : 0,
                    qPrintable(formatter.path));
        return 3;
    }
    check("toolchain.formatter-executable-when-available",
          !expectAvailable || QFileInfo(formatter.path).isExecutable(), formatter.path);

    // --- 3. the REAL product shell, installed exactly like the app does -------
    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.projectRoot = project;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    check("probe.edit-core-provider", provider != nullptr);
    if (provider == nullptr) {
        return 1;
    }
    vedit::app::ApplicationShell shell(options);
    shell.editorArea()->setProvider(provider.get());
    shell.editorArea()->ensurePane();
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = project;
    integration.stateDir = stateDir;
    // installDiagnostics prints the product's own `TOOLCHAIN ...` line.
    vedit::workspace::installDiagnostics(shell, integration);
    shell.panels()->showPanel(QStringLiteral("outline"), &shell);
    shell.resize(1280, 860);
    shell.show();
    QCoreApplication::processEvents();

    // --- 4. the gate the user sees, read before anything can "fix" it --------
    std::printf("GATES-READ sequence=after-install-before-file-open\n");
    std::fflush(stdout);
    printGate(shell, QStringLiteral("formatting"));
    printGate(shell, QStringLiteral("definition"));
    printGate(shell, QStringLiteral("hover"));
    const QVariantMap formatting = gatesOf(shell).value(QStringLiteral("formatting")).toMap();
    const bool gateEnabled = formatting.value(QStringLiteral("enabled")).toBool();
    const QString gateReason = formatting.value(QStringLiteral("reason")).toString();
    const QString gateSource = formatting.value(QStringLiteral("source")).toString();
    check("gate.formatting-published", !formatting.isEmpty());
    check("gate.formatting-matches-toolchain", gateEnabled == expectAvailable,
          QStringLiteral("gate=%1 toolchain=%2")
              .arg(gateEnabled ? 1 : 0)
              .arg(formatter.found() ? 1 : 0));
    check("gate.formatting-independent-of-backend", gateSource == QStringLiteral("always-verible-format"),
          gateSource);

    QAction *formatAction = shell.commands() != nullptr
        ? shell.commands()->action(QStringLiteral("diagnostics.format"))
        : nullptr;
    check("action.formatting-exists", formatAction != nullptr);
    std::printf("ACTION action=diagnostics.format enabled=%d reason='%s'\n",
                formatAction != nullptr && formatAction->isEnabled() ? 1 : 0,
                formatAction != nullptr ? qPrintable(formatAction->toolTip()) : "-");
    std::fflush(stdout);
    if (expectAvailable) {
        check("action.formatting-enabled", formatAction != nullptr && formatAction->isEnabled(),
              formatAction != nullptr ? formatAction->toolTip() : QString());
        check("reason.no-missing-claim-when-present",
              !gateReason.contains(QStringLiteral("not found"), Qt::CaseInsensitive)
                  && !gateReason.contains(QStringLiteral("missing"), Qt::CaseInsensitive),
              gateReason);
    } else {
        check("action.formatting-disabled-when-missing", formatAction != nullptr
                                                              && !formatAction->isEnabled());
        check("reason.names-the-missing-tool",
              gateReason.contains(QStringLiteral("verible-verilog-format"))
                  && gateReason.contains(QStringLiteral("missing"), Qt::CaseInsensitive),
              gateReason);
        check("reason.names-the-probed-candidates",
              gateReason.contains(QStringLiteral("probed")), gateReason);
        check("reason.no-fake-local-claim",
              !gateReason.contains(QStringLiteral("verible-format:local")), gateReason);
    }

    // --- 5. a REAL format on the open document -------------------------------
    vedit::app::EditorPane *pane = shell.editorArea()->activePane();
    check("probe.active-pane", pane != nullptr);
    if (pane == nullptr) {
        return 1;
    }
    QString openError;
    check("format.file-opened", pane->openFile(formatPath, &openError), openError);
    spinUntil([]() { return false; }, 300);
    auto *view = qobject_cast<KTextEditor::View *>(pane->editorWidget());
    check("format.ktexteditor-view", view != nullptr);
    if (view == nullptr) {
        return 1;
    }
    check("format.current-file", pane->currentFilePath() == formatPath,
          pane->currentFilePath());
    const QString before = view->document()->text();
    std::printf("OBSERVED format.before bytes=%lld sha256=%s\n",
                static_cast<long long>(before.toUtf8().size()), qPrintable(sha256Of(before)));
    std::fflush(stdout);

    // Triggered on purpose in BOTH modes: in the missing mode this proves the
    // disabled action does not reformat anything.
    if (formatAction != nullptr) {
        formatAction->trigger();
    }
    const bool changed = spinUntil([&]() { return view->document()->text() != before; }, 15000);
    const QString after = view->document()->text();
    std::printf("OBSERVED format.after bytes=%lld sha256=%s changed=%d\n",
                static_cast<long long>(after.toUtf8().size()), qPrintable(sha256Of(after)),
                changed ? 1 : 0);
    std::fflush(stdout);

    if (expectAvailable) {
        check("format.document-changed", changed,
              QStringLiteral("bytes %1 -> %2").arg(before.size()).arg(after.size()));
        const QString reference = referenceFormat(formatter.path, before);
        std::printf("OBSERVED format.reference bytes=%lld sha256=%s\n",
                    static_cast<long long>(reference.toUtf8().size()),
                    qPrintable(sha256Of(reference)));
        std::fflush(stdout);
        check("format.matches-the-formatter-cli", !reference.isEmpty() && after == reference,
              QStringLiteral("reference-bytes=%1").arg(reference.size()));
        check("format.actually-differs", after != before && after != formatText);
    } else {
        check("format.document-unchanged-when-missing", !changed && after == before);
    }

    // --- 6. evidence artifacts and the no-bypass postcondition ---------------
    if (!evidenceDir.isEmpty()) {
        const QString beforePath = evidenceDir + QStringLiteral("/format-before.sv");
        const QString afterPath = evidenceDir + QStringLiteral("/format-after.sv");
        check("evidence.before-written", writeFile(beforePath, before), beforePath);
        check("evidence.after-written", writeFile(afterPath, after), afterPath);
        std::printf("EVIDENCE format-before='%s' format-after='%s'\n",
                    qPrintable(beforePath), qPrintable(afterPath));
        std::fflush(stdout);
    }
    std::printf("POSTCONDITION state-dir='%s' entries=%s settings-toml=%s\n",
                qPrintable(stateDir), qPrintable(entryListing(stateDir)),
                QFileInfo::exists(settingsPath) ? "present" : "absent");
    std::fflush(stdout);
    check("postcondition.no-settings-toml", !QFileInfo::exists(settingsPath), settingsPath);

    std::printf("G6-DIAG-PROBE exit=%d checks=%d failures=%d expect=%s\n",
                g_failures == 0 ? 0 : 1, g_checks, g_failures,
                expectAvailable ? "available" : "missing");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
