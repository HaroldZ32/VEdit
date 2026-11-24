// UX batch 2 / item 12 + R3 — the presentation, END TO END in the real shell.
//
// Contract: §0.1 R3 ("the automatic run never opens a modal dialog; only an
// explicit Lint Project does") and item 12 (ELAB errors listed and jumpable).
//
// This binary drives the PRODUCT wiring, not the policy object:
//
//   `project.lint` (menu action)  -> real diagnostics lane -> real vd-deeplint
//                                 -> ProjectLintNotice (MODAL dialog expected)
//   a successful save (save seam) -> 700 ms debounce -> project pass
//                                 -> ProjectLintNotice (NO dialog, notice only)
//
// It SKIPs (exit 0) with a printed reason when the project file set never
// arrives or the lint engine reports nothing to present - a missing toolchain is
// never evidence either way.

#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "diagnostics_panel.h"
#include "project_integration.h"
#include "project_lint_notice.h"
#include "project_lint_panel.h"
#include "project_model.h"

#include "runtime_init.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "panel_registry.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QPushButton>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

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
                detail.isEmpty() ? "" : "  ", detail.isEmpty() ? "" : detail.toUtf8().constData());
    std::fflush(stdout);
}

void skip(const char *name, const QString &why)
{
    ++g_skips;
    std::printf("SKIP %s  %s\n", name, why.toUtf8().constData());
    std::fflush(stdout);
}

bool waitUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(text.toUtf8()) == text.toUtf8().size();
}

int dialogsOf(const QWidget &shell)
{
    int count = 0;
    for (QDialog *dialog : shell.findChildren<QDialog *>(QStringLiteral("veditElabDialog"))) {
        Q_UNUSED(dialog);
        ++count;
    }
    return count;
}

/// G1-a review (finding 1), end to end with the REAL project lane: an expansion
/// that FAILS must reach the Project Lint dock with its reason, and "Run Project
/// Lint" must ASK THE PROJECT LANE to expand again.
///
/// The failure exercised here is the model's hard one: the project root is gone
/// (a moved/deleted project, or a session restored for a folder the user
/// deleted). A malformed `.f` is deliberately NOT the failure trigger - the Rust
/// parser reports bad filelist entries as WARNINGS and still answers, so a "bad
/// `.f`" is not an expansion failure at all (`project_selftest` `bad.files`).
/// A `.f` IS imported (it expands to ZERO files) so the scenario also locks the
/// membership transition that used to leave the dock on "no filelist": the retry
/// proof is "failed with no set" -> "ready with the real set", which can only
/// happen if Run re-drove the project lane's ONE expansion.
void projectLaneFailureScenario()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        check("failure.tempdir", false);
        return;
    }
    const QString root = dir.path() + QStringLiteral("/proj");
    const QString rtl = root + QStringLiteral("/rtl");
    QDir().mkpath(rtl);
    const QString source = rtl + QStringLiteral("/ok.sv");
    const QString filelist = root + QStringLiteral("/sources.f");
    const bool fixture =
        writeFile(source, QStringLiteral("module ok; endmodule\n"))
        && writeFile(filelist, QStringLiteral("# empty: a legitimate 0-file file set\n"));
    check("failure.fixture", fixture);
    if (!fixture) {
        return;
    }

    vedit::app::ShellOptions options;
    options.stateDir = dir.path() + QStringLiteral("/state");
    // `installProject` opens `shell.projectRoot()` (the one root the shell
    // resolved), so the scenario must hand the root to the SHELL, not only to
    // the integration options below.
    options.projectRoot = root;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    std::unique_ptr<vedit::app::ApplicationShell> shell(
        new vedit::app::ApplicationShell(options));
    if (provider != nullptr) {
        shell->editorArea()->setProvider(provider.get());
    }
    shell->editorArea()->ensurePane();

    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = root;
    integration.stateDir = dir.path() + QStringLiteral("/state");
    // The product's own order (src/workspace/integration.cpp): the project lane
    // publishes, this lane consumes.
    vedit::workspace::installProject(*shell, integration);
    vedit::workspace::installDiagnostics(*shell, integration);
    shell->resize(1200, 800);
    shell->show();
    QCoreApplication::processEvents();

    auto *model = vedit::workspace::projectModelForShell(*shell);
    auto *controller = vedit::workspace::diagnosticsControllerForShell(*shell);
    auto *lintPanel =
        shell->findChild<vedit::workspace::diagnostics::ProjectLintPanel *>();
    check("failure.project-model", model != nullptr);
    check("failure.controller", controller != nullptr);
    check("failure.lint-panel", lintPanel != nullptr);
    if (model == nullptr || controller == nullptr || lintPanel == nullptr) {
        return;
    }
    const auto pollerState = [&shell]() {
        return shell->property("vedit.diagnostics.projectFilesState").toMap();
    };

    // An imported EMPTY `.f` is a real 0-file answer; it must be PUBLISHED as
    // `ready` (the membership transition moves the revision), not left as
    // "expanding…"/"No project file set yet".
    QString importError;
    check("failure.import-empty-filelist",
          model->importFilelists({filelist}, &importError), importError);
    const bool emptyReady = waitUntil(
        [&pollerState]() {
            const QVariantMap state = pollerState();
            return state.value(QStringLiteral("phase")).toString() == QStringLiteral("ready")
                && state.value(QStringLiteral("files")).toInt() == 0;
        },
        20000);
    const QVariantMap emptyState = pollerState();
    check("failure.empty-filelist-published", emptyReady,
          QStringLiteral("phase='%1' files=%2")
              .arg(emptyState.value(QStringLiteral("phase")).toString())
              .arg(emptyState.value(QStringLiteral("files")).toInt()));

    // The project goes away. The project lane's own Refresh is the gesture a
    // user makes from the Project panel; its `recordFailure` is the hard failure
    // the diagnostic consumer must now see.
    check("failure.root-removed", QDir(root).removeRecursively());
    model->refresh();
    const bool failedPublished = waitUntil(
        [&pollerState]() {
            return pollerState().value(QStringLiteral("phase")).toString()
                == QStringLiteral("failed");
        },
        15000);
    const QVariantMap failedState = pollerState();
    const QString failureText = failedState.value(QStringLiteral("message")).toString();
    check("failure.state-published",
          failedPublished
              && failedState.value(QStringLiteral("files")).toInt() == 0
              && failureText.contains(QStringLiteral("does not exist")),
          QStringLiteral("phase='%1' message='%2' files=%3")
              .arg(failedState.value(QStringLiteral("phase")).toString())
              .arg(failureText)
              .arg(failedState.value(QStringLiteral("files")).toInt()));
    check("failure.project-model-error",
          model->lastError().contains(QStringLiteral("does not exist")),
          model->lastError());

    // The dock's failed branch is reachable WITH a project lane: reason + the
    // retry action it names.
    auto *emptyLabel = lintPanel->findChild<QLabel *>(QStringLiteral("project-lint-empty"));
    const bool dockShowsFailure = waitUntil(
        [lintPanel, emptyLabel, failureText]() {
            return emptyLabel != nullptr && emptyLabel->isVisibleTo(lintPanel)
                && emptyLabel->text().contains(QStringLiteral("expansion failed"))
                && emptyLabel->text().contains(failureText)
                && emptyLabel->text().contains(QStringLiteral("Run Project Lint"));
        },
        5000);
    check("failure.dock-shows-reason-and-retry", dockShowsFailure,
          emptyLabel != nullptr ? emptyLabel->text() : QString());

    // Make the retry SUCCEED (the root comes back with a real file set): then
    // "the dock moved failed -> ready" can only happen if Run re-drove the
    // project lane's expansion. `expansionCount()` is the model's own probe for
    // a SCHEDULED expansion (RM-2), so the second assertion is not just "the
    // status text changed".
    QDir().mkpath(rtl);
    const bool restored =
        writeFile(source, QStringLiteral("module ok; endmodule\n"))
        && writeFile(filelist, QStringLiteral("rtl/ok.sv\n"));
    check("failure.root-restored", restored);
    if (!restored) {
        return;
    }
    const quint64 expansionsBefore = model->expansionCount();
    QPushButton *run = lintPanel->runButton();
    check("failure.run-button", run != nullptr);
    if (run == nullptr) {
        return;
    }
    run->click();
    QCoreApplication::processEvents();
    const QVariantMap runState =
        shell->property("vedit.diagnostics.projectLintRun").toMap();
    check("failure.run-retried-the-expansion",
          !runState.value(QStringLiteral("started")).toBool()
              && runState.value(QStringLiteral("reason")).toString()
                     == QStringLiteral("project-fileset-failed")
              && runState.value(QStringLiteral("retry")).toString()
                     == QStringLiteral("started"),
          QStringLiteral("started=%1 reason='%2' retry='%3'")
              .arg(runState.value(QStringLiteral("started")).toBool() ? 1 : 0)
              .arg(runState.value(QStringLiteral("reason")).toString())
              .arg(runState.value(QStringLiteral("retry")).toString()));
    check("failure.run-asked-the-project-lane",
          model->expansionCount() > expansionsBefore,
          QStringLiteral("before=%1 after=%2")
              .arg(static_cast<qulonglong>(expansionsBefore))
              .arg(static_cast<qulonglong>(model->expansionCount())));
    const bool recovered = waitUntil(
        [&pollerState, controller]() {
            const QVariantMap state = pollerState();
            return state.value(QStringLiteral("phase")).toString() == QStringLiteral("ready")
                && state.value(QStringLiteral("files")).toInt() == 1
                && controller->projectFiles().size() == 1;
        },
        30000);
    const QVariantMap readyState = pollerState();
    check("failure.retry-reached-ready", recovered,
          QStringLiteral("phase='%1' files=%2 projectFiles=%3")
              .arg(readyState.value(QStringLiteral("phase")).toString())
              .arg(readyState.value(QStringLiteral("files")).toInt())
              .arg(controller->projectFiles().size()));
}

} // namespace

int main(int argc, char **argv)
{
    // T134 / AGENT.md §4: this e2e installs the REAL diagnostics lane
    // (installDiagnostics -> the Rust FFI LSP session), which writes to the
    // verible child's stdin; without the Rust runtime's SIGPIPE default a write
    // to a dead child terminates the test. Must run before Qt and any FFI call.
    vedit_runtime_init();
    QApplication app(argc, argv);
    // G1-a review finding 1 runs BEFORE the lint scenarios: it needs no lint
    // toolchain, so the binary's later "no file set / no engine" early exits
    // must not be able to skip it.
    projectLaneFailureScenario();
    QTemporaryDir dir;
    if (!dir.isValid()) {
        check("product.tempdir", false);
        return 1;
    }
    const QString rtl = dir.path() + QStringLiteral("/rtl");
    QDir().mkpath(rtl);
    // A project with one elaboration/lint error: the pass must have something to
    // present, or "no dialog" would prove nothing.
    const QString broken = rtl + QStringLiteral("/broken.sv");
    const QString good = rtl + QStringLiteral("/good.sv");
    check("product.fixture",
          writeFile(broken, QStringLiteral("module broken;\n  logic a b;\nendmodule\n"))
              && writeFile(good, QStringLiteral("module good;\n  logic a;\nendmodule\n")));

    vedit::app::ShellOptions options;
    options.stateDir = dir.path() + QStringLiteral("/state");
    options.restoreSession = false;
    options.showDefaultPanels = false;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    std::unique_ptr<vedit::app::ApplicationShell> shell(new vedit::app::ApplicationShell(options));
    if (provider != nullptr) {
        shell->editorArea()->setProvider(provider.get());
    }
    shell->editorArea()->ensurePane();
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = dir.path();
    integration.stateDir = dir.path() + QStringLiteral("/state");
    vedit::workspace::installDiagnostics(*shell, integration);
    shell->panels()->showPanel(QStringLiteral("outline"), shell.get());
    shell->resize(1200, 800);
    shell->show();
    QCoreApplication::processEvents();

    auto *panel = shell->findChild<vedit::workspace::diagnostics::DiagnosticsPanel *>();
    check("product.panel", panel != nullptr);
    QAction *lintAction =
        shell->commands() != nullptr ? shell->commands()->action(QStringLiteral("project.lint"))
                                     : nullptr;
    check("product.lint-action", lintAction != nullptr);
    if (panel == nullptr || lintAction == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    auto *controller = panel->controller();

    // ---- AC-4.5 / AC-4.2 (F6): the PANEL the lane installs, widget-level ----
    // The shell always creates the content host, so the "no host" branch is
    // driven straight through the lane's seam: it must refuse (false) and report
    // exactly ONE explicit diagnostic instead of crashing.
    auto *lintPanel = shell->findChild<vedit::workspace::diagnostics::ProjectLintPanel *>();
    check("product.lint-panel", lintPanel != nullptr);
    check("product.lint-panel-published",
          shell->property("vedit.diagnostics.projectLintPanel").toBool());
    check("product.lint-run-button", lintPanel != nullptr && lintPanel->runButton() != nullptr);
    const int shellDiagnosticsBefore = shell->diagnostics().size();
    const bool missingHostRefused =
        !vedit::workspace::installProjectLintContents(nullptr, *shell, controller);
    check("product.lint-host-missing-refused", missingHostRefused);
    const QStringList shellDiagnostics = shell->diagnostics();
    check("product.lint-host-missing-diagnostic",
          shellDiagnostics.size() == shellDiagnosticsBefore + 1
              && shellDiagnostics.last().contains(
                     QStringLiteral("Project Lint dock is not available")),
          shellDiagnostics.isEmpty() ? QString() : shellDiagnostics.last());
    // AC-4.5: an empty panel shows the REASON and the action, never a blank card
    // (this runs before any pass, so there are no rows yet). batch-20261116 L2.2:
    // the reason may be the file-set poller's own state ("being expanded…"), which
    // is the persistent replacement for the old 8 s status-bar-only notice.
    if (lintPanel != nullptr) {
        auto *empty = lintPanel->findChild<QLabel *>(QStringLiteral("project-lint-empty"));
        check("product.lint-empty-state",
              empty != nullptr && empty->isVisibleTo(lintPanel) && !empty->text().isEmpty()
                  && (empty->text().contains(QStringLiteral("No project file set yet"))
                      || empty->text().contains(QStringLiteral("Project file set"))
                      || empty->text().contains(
                          QStringLiteral("No whole-project diagnostics"))),
              empty != nullptr ? empty->text() : QString());
        check("product.lint-no-rows-yet", lintPanel->issueRowCount() == 0,
              QStringLiteral("rows=%1").arg(lintPanel->issueRowCount()));
        check("product.lint-tree-hidden",
              lintPanel->tree() != nullptr && !lintPanel->tree()->isVisibleTo(lintPanel));
    }

    // ---- batch-20261116 L2.1/L2.2: the file-set poller's timeout and state ---
    // The 10 s give-up is gone: the poller publishes the soft timeout it runs
    // under (>= 300 s) and a state map the dock turns into its persistent reason.
    const QVariantMap filesState =
        shell->property("vedit.diagnostics.projectFilesState").toMap();
    check("product.project-files-state-published", !filesState.isEmpty());
    check("product.project-expand-soft-timeout-300s",
          filesState.value(QStringLiteral("softTimeoutMs")).toInt() >= 300000,
          QString::number(filesState.value(QStringLiteral("softTimeoutMs")).toInt()));
    const QStringList knownPhases{QStringLiteral("no-filelist"), QStringLiteral("expanding"),
                                  QStringLiteral("slow"), QStringLiteral("failed"),
                                  QStringLiteral("ready")};
    check("product.project-files-phase-known",
          knownPhases.contains(filesState.value(QStringLiteral("phase")).toString()),
          filesState.value(QStringLiteral("phase")).toString());
    // G1-a: this harness installs the diagnostics lane ALONE (no project lane),
    // so the one expansion there is this lane's own - and it says so. In the
    // product order the same map reports `project-lane` and no expansion runs
    // here at all (`vedit-module-closure-e2e` asserts that side).
    check("product.project-files-source-is-self-expansion",
          filesState.value(QStringLiteral("source")).toString()
              == QStringLiteral("self-expansion"),
          QStringLiteral("source='%1'")
              .arg(filesState.value(QStringLiteral("source")).toString()));

    // RM-9: a project OPEN now starts an AUTOMATIC full pass as soon as the file
    // set is adopted. Wait until the set ARRIVED and every automatic pass it
    // started (including one deferred by the pass timer) is terminal; only then
    // is the explicit click under test certainly the NEXT pass. Otherwise this
    // harness mistakes the automatic open pass for its explicit one
    // (automatic=1, no dialog - the pre-RM-9 shape).
    waitUntil([controller]() {
        return !controller->projectFiles().isEmpty() && !controller->projectPassRunning();
    }, 120000);
    // batch-20261116 L2.1: the arrival is a NAMED poller state, not just a
    // non-empty list - the dock's reason is built from this map.
    const QVariantMap readyState =
        shell->property("vedit.diagnostics.projectFilesState").toMap();
    check("product.project-files-state-ready",
          readyState.value(QStringLiteral("phase")).toString() == QStringLiteral("ready")
              && readyState.value(QStringLiteral("files")).toInt()
                     == controller->projectFiles().size(),
          QStringLiteral("phase='%1' files=%2 project=%3")
              .arg(readyState.value(QStringLiteral("phase")).toString())
              .arg(readyState.value(QStringLiteral("files")).toInt())
              .arg(controller->projectFiles().size()));
    QThread::msleep(700); // let a deferred pass timer fire on a quiet tick
    QCoreApplication::processEvents();
    waitUntil([controller]() { return !controller->projectPassRunning(); }, 120000);

    // The project file set arrives asynchronously (ProjectFileSetPoller). Try the
    // explicit run until a pass actually starts; a first attempt before the set
    // arrives only reports "No project file set".
    quint64 serial = controller->projectPassSerial();
    bool started = false;
    for (int attempt = 0; attempt < 60 && !started; ++attempt) {
        lintAction->trigger();
        started = waitUntil([controller, serial]() {
            return controller->projectPassRunning() || controller->projectPassSerial() != serial;
        }, 250);
        if (!started) {
            if (attempt == 0 || attempt % 10 == 0) {
                std::printf("OBSERVED product.attempt=%d status='%s' pass='%s' files=%d\n",
                            attempt, qPrintable(shell->statusBar()->currentMessage()),
                            qPrintable(controller->projectPass().message),
                            controller->projectPassFileCount());
                std::fflush(stdout);
            }
            QThread::msleep(100);
            QCoreApplication::processEvents();
        }
    }
    if (!started) {
        skip("product.explicit-pass", QStringLiteral("the project file set never arrived"));
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        // G1-a review finding 1: the first scenario (a project lane + a failed
        // expansion) runs BEFORE this skip path, so its failures must not be
        // masked by a later "no lint toolchain" SKIP.
        return g_failures == 0 ? 0 : 1;
    }
    // Wait for that pass to reach a terminal phase AND for the 300 ms presenter
    // poll to act on it. The explicit pass may already have finished above, so
    // the terminal condition is just "not running" plus the dialog wait (the
    // presenter reacts to the terminal SERIAL, not to a snapshot we hold).
    waitUntil([controller]() { return !controller->projectPassRunning(); }, 120000);
    waitUntil([&shell]() { return dialogsOf(*shell) > 0; }, 5000);

    const int dialogsAfterExplicit = dialogsOf(*shell);
    const bool noticeShown = [&shell]() {
        auto *label = shell->findChild<QLabel *>(QStringLiteral("veditProjectLintNotice"));
        return label != nullptr && label->isVisibleTo(label->parentWidget())
            && !label->text().isEmpty();
    }();
    std::printf("OBSERVED product.pass phase=%d errors=%lld automatic=%d dialogs=%d notice=%d "
                "message='%s'\n",
                static_cast<int>(controller->projectPass().phase),
                static_cast<long long>(
                    [&controller]() {
                        int errors = 0;
                        for (const auto &entry : controller->upperDiagnostics()) {
                            if (entry.severity == 1 && controller->hasUpperLayer(entry.file)) {
                                ++errors;
                            }
                        }
                        return errors;
                    }()),
                controller->lastPassWasAutomatic() ? 1 : 0, dialogsAfterExplicit,
                noticeShown ? 1 : 0, qPrintable(controller->projectPass().message));

    if (controller->projectPass().phase != vedit::workspace::diagnostics::LintPhase::Finished) {
        skip("product.explicit-pass-finished", controller->projectPass().message);
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        // G1-a review finding 1: the first scenario (a project lane + a failed
        // expansion) runs BEFORE this skip path, so its failures must not be
        // masked by a later "no lint toolchain" SKIP.
        return g_failures == 0 ? 0 : 1;
    }
    if (controller->upperDiagnostics().isEmpty()) {
        skip("product.elab-content",
             QStringLiteral("the lint engine reported nothing (no toolchain for this fixture?)"));
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        // G1-a review finding 1: the first scenario (a project lane + a failed
        // expansion) runs BEFORE this skip path, so its failures must not be
        // masked by a later "no lint toolchain" SKIP.
        return g_failures == 0 ? 0 : 1;
    }

    // A61-free change of the SAME contract (batch-20261115 AC-4.4): the explicit
    // run's rows are presented in the Project Lint dock plus the status-bar
    // notice, and NO modal dialog may appear - even for a manual run. The old
    // assertion (`dialogs == 1`) encoded the pre-AC-4.4 policy.
    check("product.explicit-no-dialog", dialogsAfterExplicit == 0,
          QStringLiteral("dialogs=%1").arg(dialogsAfterExplicit));
    check("product.explicit-not-automatic", !controller->lastPassWasAutomatic());

    // AC-4.3 data separation: the project rows belong to the project-lint dock.
    // The Outline must list NONE of them (its rows are the realtime lower layer).
    if (auto *panel = shell->findChild<vedit::workspace::diagnostics::DiagnosticsPanel *>()) {
        const QVector<vedit::workspace::diagnostics::DiagnosticEntry> upper =
            controller->upperLayerDiagnostics();
        check("product.explicit-upper-has-rows", !upper.isEmpty(),
              QStringLiteral("upper=%1").arg(upper.size()));
        const QVector<vedit::workspace::diagnostics::DiagnosticEntry> realtime =
            controller->lowerRowsForFile(controller->realtimeDisplayFile());
        int leaked = 0;
        for (const auto &entry : realtime) {
            if (controller->hasUpperLayer(entry.file)) {
                ++leaked;
            }
        }
        check("product.outline-holds-no-project-rows", leaked == 0,
              QStringLiteral("leaked=%1 realtime=%2 upper=%3")
                  .arg(leaked)
                  .arg(realtime.size())
                  .arg(upper.size()));
        // R1 (Review B): "no realtime document => the Outline shows ZERO rows" is
        // the user-visible half of F2, so it is a CHECK on the WIDGET, not an
        // OBSERVED line: a regression to the merged-table fallback would put the
        // project rows back on screen and turn this red.
        const bool outlineEmpty =
            waitUntil([panel]() { return panel->issueRowCount() == 0; }, 3000);
        check("product.outline-widget-rows-empty", outlineEmpty,
              QStringLiteral("panel-rows=%1 realtime='%2' upper=%3")
                  .arg(panel->issueRowCount())
                  .arg(controller->realtimeDisplayFile())
                  .arg(controller->upperLayerDiagnostics().size()));
        std::printf("OBSERVED product.outline panel-rows=%d\n", panel->issueRowCount());
    }

    // ---- AC-4.2 (F6): the project rows are RENDERED in the panel, widget-level --
    // The controller's rows are only useful if the panel draws them: one group per
    // file carrying its issue count, one row per issue, and a summary naming the
    // counts and whether the pass was manual. The panel rebuilds on a 300 ms poll,
    // so wait (bounded) for it to catch up with the finished pass.
    if (lintPanel != nullptr) {
        const QVector<vedit::workspace::diagnostics::DiagnosticEntry> upper =
            controller->upperLayerDiagnostics();
        const bool rendered = waitUntil(
            [lintPanel, &upper]() {
                return lintPanel->issueRowCount() == upper.size()
                    && lintPanel->fileRowCount() > 0;
            },
            8000);
        check("product.lint-rows-rendered", rendered,
              QStringLiteral("panel=%1 upper=%2 files=%3")
                  .arg(lintPanel->issueRowCount())
                  .arg(upper.size())
                  .arg(lintPanel->fileRowCount()));
        QTreeWidget *tree = lintPanel->tree();
        check("product.lint-file-groups",
              tree != nullptr && lintPanel->fileRowCount() > 0
                  && tree->topLevelItemCount() == lintPanel->fileRowCount(),
              QStringLiteral("groups=%1 files=%2")
                  .arg(tree != nullptr ? tree->topLevelItemCount() : -1)
                  .arg(lintPanel->fileRowCount()));
        // The first group header is the file NAME and its Count column is the
        // number of issue rows below it (the panel's grouping contract).
        const QTreeWidgetItem *group = tree != nullptr && tree->topLevelItemCount() > 0
                                           ? tree->topLevelItem(0)
                                           : nullptr;
        QStringList groupedFiles;
        for (const auto &entry : upper) {
            if (!groupedFiles.contains(entry.file)) {
                groupedFiles.append(entry.file);
            }
        }
        std::sort(groupedFiles.begin(), groupedFiles.end());
        check("product.lint-group-header",
              group != nullptr && !groupedFiles.isEmpty()
                  && group->text(0) == QFileInfo(groupedFiles.first()).fileName()
                  && group->text(1).toInt() == group->childCount()
                  && group->childCount() > 0,
              group != nullptr
                  ? QStringLiteral("header='%1' count='%2' children=%3")
                        .arg(group->text(0), group->text(1))
                        .arg(group->childCount())
                  : QString());
        const QTreeWidgetItem *issue = group != nullptr && group->childCount() > 0
                                          ? group->child(0)
                                          : nullptr;
        check("product.lint-row-text",
              issue != nullptr && issue->text(0).contains(QStringLiteral(" — "))
                  && issue->data(0, Qt::UserRole).toString() == groupedFiles.value(0),
              issue != nullptr ? issue->text(0) : QString());
        const QString summary = lintPanel->summaryText();
        check("product.lint-summary",
              summary.contains(QStringLiteral("Project lint:"))
                  && summary.contains(QStringLiteral("file(s)"))
                  && summary.contains(QStringLiteral("error(s)"))
                  && summary.contains(QStringLiteral("manual")),
              summary);
        check("product.lint-empty-hidden",
              [lintPanel]() {
                  auto *empty = lintPanel->findChild<QLabel *>(QStringLiteral("project-lint-empty"));
                  return empty == nullptr || !empty->isVisibleTo(lintPanel);
              }(),
              QStringLiteral("rows=%1").arg(lintPanel->issueRowCount()));
    }
    // AC-4.2 visibility (T11): a restored session can leave the dock HIDDEN, and
    // a result nobody sees is no result. Hide it, run the panel's own button
    // path, and require the dock to be visible with the content page CURRENT.
    if (QWidget *host = shell->projectLintHost()) {
        // The host sits inside the dock's stacked widget (empty state vs content),
        // so walk up instead of assuming the first parent.
        QDockWidget *dock = nullptr;
        for (QWidget *w = host->parentWidget(); w != nullptr; w = w->parentWidget()) {
            if ((dock = qobject_cast<QDockWidget *>(w)) != nullptr) {
                break;
            }
        }
        check("product.dock-found", dock != nullptr);
        if (dock != nullptr) {
            dock->hide();
            check("product.dock-starts-hidden", !dock->isVisible());
            auto *run = host->findChild<QPushButton *>(QStringLiteral("project-lint-run"));
            check("product.dock-run-button", run != nullptr);
            if (run != nullptr) {
                const quint64 beforeRun = controller->projectPassSerial();
                run->click();
                // Visibility propagates on the next event-loop turn(s), so wait
                // for it instead of sampling the same stack frame.
                const bool dockShown = waitUntil([dock]() { return dock->isVisible(); }, 5000);
                check("product.dock-visible-after-run", dockShown);
                check("product.dock-run-started",
                      waitUntil([controller, beforeRun]() {
                          return controller->projectPassSerial() != beforeRun
                              || controller->projectPassRunning();
                      }, 8000));
                const bool contentCurrent = waitUntil(
                    [dock]() { return dock->widget() != nullptr && dock->widget()->isVisible(); },
                    5000);
                check("product.dock-content-current", contentCurrent);
            }
        }
    }
    // AC-4.2 visibility: the result lives in a dock the user can SEE.
    if (QWidget *host = shell->projectLintHost()) {
        check("product.dock-host-present", true);
        check("product.dock-has-panel",
              host->findChild<QWidget *>(QStringLiteral("project-lint-panel")) != nullptr);
    } else {
        skip("product.dock-host-present", QStringLiteral("this build has no project-lint dock"));
    }
    auto *dialog = shell->findChild<QDialog *>(QStringLiteral("veditElabDialog"));
    if (dialog != nullptr) {
        check("product.dialog-modal", dialog->isModal());
        dialog->hide();
    }
    check("product.explicit-notice", noticeShown);

    // ---- A61.1 (batch-20261115): a SAVE starts NO project pass -------------
    vedit::app::EditorPane *pane = shell->editorArea()->activePane();
    check("product.pane", pane != nullptr);
    if (pane == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    QString openError;
    check("product.file-opened", pane->openFile(broken, &openError), openError);
    // My dock section above started one more explicit pass; drain it first, or
    // its serial move would look like "a save started a pass".
    waitUntil([controller]() { return !controller->projectPassRunning(); }, 60000);
    QCoreApplication::processEvents();
    const quint64 beforeSave = controller->projectPassSerial();
    const int dialogsBeforeSave = dialogsOf(*shell);
    QString saveError;
    check("product.save", pane->saveFile(&saveError), saveError);
    // The contract's save policy is MANUAL ONLY (AC-2.4 as amended by A61.1), so
    // this is a NEGATIVE assertion: a save must start neither a pass nor a
    // dialog. Two seconds covers the removed 700 ms debounce plus the 300 ms
    // presenter poll with slack.
    const bool saveStartedPass = waitUntil([controller, beforeSave]() {
        return controller->projectPassSerial() != beforeSave || controller->projectPassRunning();
    }, 2000);
    const int dialogsAfterSave = dialogsOf(*shell);
    std::printf("OBSERVED product.save-quiet started=%d dialogs-before=%d dialogs-after=%d\n",
                saveStartedPass ? 1 : 0, dialogsBeforeSave, dialogsAfterSave);
    check("product.save-no-project-pass", !saveStartedPass,
          QStringLiteral("serial=%1").arg(static_cast<qulonglong>(
              controller->projectPassSerial())));
    check("product.save-no-dialog", dialogsAfterSave == dialogsBeforeSave,
          QStringLiteral("before=%1 after=%2").arg(dialogsBeforeSave).arg(dialogsAfterSave));

    // ---- batch-20261116 L2: an empty file set is a PERSISTENT, named state ---
    // The old poller discarded a slow expansion after 10 s, so `projectFiles_`
    // stayed empty for the whole session and the only notice was an 8 s
    // status-bar flash. This scene uses an EMPTY project root (a deterministic
    // 0-file expansion) and asserts: the dock carries the reason for longer than
    // that flash, the slow/failed timeout states render from the SAME map the
    // poller publishes, and Run with an empty set is deterministically
    // observable instead of silently doing nothing.
    {
        QTemporaryDir emptyDir;
        check("product.empty-scene-tempdir", emptyDir.isValid());
        vedit::app::ShellOptions emptyOptions;
        emptyOptions.stateDir = emptyDir.path() + QStringLiteral("/state");
        emptyOptions.restoreSession = false;
        emptyOptions.showDefaultPanels = false;
        std::unique_ptr<vedit::app::EditorProvider> emptyProvider =
            vedit::editor::makeEditCoreProvider();
        std::unique_ptr<vedit::app::ApplicationShell> emptyShell(
            new vedit::app::ApplicationShell(emptyOptions));
        if (emptyProvider != nullptr) {
            emptyShell->editorArea()->setProvider(emptyProvider.get());
        }
        emptyShell->editorArea()->ensurePane();
        vedit::workspace::IntegrationOptions emptyIntegration;
        emptyIntegration.projectRoot = emptyDir.path();
        emptyIntegration.stateDir = emptyDir.path() + QStringLiteral("/state");
        vedit::workspace::installDiagnostics(*emptyShell, emptyIntegration);
        emptyShell->resize(1200, 800);
        emptyShell->show();
        QCoreApplication::processEvents();

        auto *emptyController = vedit::workspace::diagnosticsControllerForShell(*emptyShell);
        auto *emptyPanel =
            emptyShell->findChild<vedit::workspace::diagnostics::ProjectLintPanel *>();
        check("product.empty-scene-controller", emptyController != nullptr);
        check("product.empty-scene-panel", emptyPanel != nullptr);
        const bool settled = waitUntil(
            [&emptyShell, emptyController]() {
                const QVariantMap state =
                    emptyShell->property("vedit.diagnostics.projectFilesState").toMap();
                return state.value(QStringLiteral("phase")).toString()
                           == QStringLiteral("ready")
                    && emptyController != nullptr && emptyController->projectFiles().isEmpty();
            },
            15000);
        const QVariantMap emptyState =
            emptyShell->property("vedit.diagnostics.projectFilesState").toMap();
        check("product.empty-set-settled", settled,
              QStringLiteral("phase='%1' files=%2")
                  .arg(emptyState.value(QStringLiteral("phase")).toString())
                  .arg(emptyState.value(QStringLiteral("files")).toInt()));
        auto *emptyLabel =
            emptyPanel != nullptr
            ? emptyPanel->findChild<QLabel *>(QStringLiteral("project-lint-empty"))
            : nullptr;
        // The panel rebuilds on its own 300 ms poll, so wait for it to catch up
        // with the poller's `ready` state before judging the line.
        const bool namedReason = waitUntil([emptyLabel, emptyPanel]() {
            return emptyPanel != nullptr && emptyLabel != nullptr
                && emptyLabel->isVisibleTo(emptyPanel)
                && emptyLabel->text().contains(QStringLiteral("0 source files"));
        }, 3000);
        check("product.empty-set-reason-named", namedReason,
              emptyLabel != nullptr ? emptyLabel->text() : QString());
        // Persistence: the dock still carries the reason after the exact 8 s the
        // old code relied on for its status-bar flash.
        QThread::msleep(9000);
        QCoreApplication::processEvents();
        check("product.empty-set-reason-persists",
              emptyLabel != nullptr && emptyLabel->isVisibleTo(emptyPanel)
                  && emptyLabel->text().contains(QStringLiteral("0 source files")),
              emptyLabel != nullptr ? emptyLabel->text() : QString());

        // The slow (soft-timeout) and failed states render from the same poller
        // map the dock reads; no ABI timing is involved in this widget assertion.
        QVariantMap slowState = emptyState;
        slowState.insert(QStringLiteral("phase"), QStringLiteral("slow"));
        slowState.insert(QStringLiteral("softTimeoutMs"), 300000);
        emptyShell->setProperty("vedit.diagnostics.projectFilesState", slowState);
        const bool slowShown = waitUntil([emptyLabel]() {
            return emptyLabel != nullptr
                && emptyLabel->text().contains(QStringLiteral("still running"));
        }, 3000);
        check("product.empty-set-slow-reason", slowShown,
              emptyLabel != nullptr ? emptyLabel->text() : QString());
        QVariantMap failedState = emptyState;
        failedState.insert(QStringLiteral("phase"), QStringLiteral("failed"));
        failedState.insert(QStringLiteral("message"), QStringLiteral("fixture expansion failed"));
        emptyShell->setProperty("vedit.diagnostics.projectFilesState", failedState);
        const bool failedShown = waitUntil([emptyLabel]() {
            return emptyLabel != nullptr
                && emptyLabel->text().contains(QStringLiteral("fixture expansion failed"))
                && emptyLabel->text().contains(QStringLiteral("Run Project Lint"));
        }, 3000);
        check("product.empty-set-failed-reason", failedShown,
              emptyLabel != nullptr ? emptyLabel->text() : QString());
        emptyShell->setProperty("vedit.diagnostics.projectFilesState", emptyState);

        // Run with an empty set: the behavior is a published answer, not a
        // silent no-op (and not only a status-bar line).
        QAction *emptyLint =
            emptyShell->commands() != nullptr
            ? emptyShell->commands()->action(QStringLiteral("project.lint"))
            : nullptr;
        check("product.empty-set-lint-action", emptyLint != nullptr);
        if (emptyLint != nullptr) {
            emptyLint->trigger();
        }
        const QVariantMap runState =
            emptyShell->property("vedit.diagnostics.projectLintRun").toMap();
        check("product.empty-set-run-observable",
              !runState.value(QStringLiteral("started")).toBool()
                  && runState.value(QStringLiteral("reason")).toString()
                         == QStringLiteral("no-project-fileset"),
              QStringLiteral("started=%1 reason='%2'")
                  .arg(runState.value(QStringLiteral("started")).toBool() ? 1 : 0)
                  .arg(runState.value(QStringLiteral("reason")).toString()));
        check("product.empty-set-run-no-pass",
              emptyController != nullptr && !emptyController->projectPassRunning());
    }

    // ---- batch-20261116 L2.1/M1: the soft-timeout MIGRATION -----------------
    // Shorten the soft timeout to 0 ms and the first-poll cadence to 1 ms, so
    // `expanding -> slow` is observed immediately. The regression asserts the
    // migration the old 10 s give-up lacked: a `slow` phase exists, the poller
    // keeps the SAME job (never discards it), the backoff is 1 s, and the file
    // set still arrives. Re-introducing the give-up makes `slow` disappear (or
    // the job/ready checks fail), i.e. this case goes red.
    {
        qputenv("VEDIT_DIAGNOSTICS_EXPAND_SOFT_TIMEOUT_MS", "0");
        qputenv("VEDIT_DIAGNOSTICS_EXPAND_POLL_INTERVAL_MS", "1");
        QTemporaryDir slowDir;
        check("product.slow-scene-tempdir", slowDir.isValid());
        const QString slowRtl = slowDir.path() + QStringLiteral("/rtl");
        QDir().mkpath(slowRtl);
        for (int index = 0; index < 4000; ++index) {
            QFile source(slowRtl + QStringLiteral("/f%1.sv").arg(index));
            if (source.open(QIODevice::WriteOnly)) {
                source.write("module m;\nendmodule\n");
            }
        }
        const QString slowList = slowDir.path() + QStringLiteral("/sources.f");
        check("product.slow-filelist",
              writeFile(slowList, QStringLiteral("rtl/f0.sv\n")));
        vedit::app::ShellOptions slowOptions;
        slowOptions.stateDir = slowDir.path() + QStringLiteral("/state");
        slowOptions.restoreSession = false;
        slowOptions.showDefaultPanels = false;
        std::unique_ptr<vedit::app::EditorProvider> slowProvider =
            vedit::editor::makeEditCoreProvider();
        std::unique_ptr<vedit::app::ApplicationShell> slowShell(
            new vedit::app::ApplicationShell(slowOptions));
        if (slowProvider != nullptr) {
            slowShell->editorArea()->setProvider(slowProvider.get());
        }
        slowShell->editorArea()->ensurePane();
        QVariantMap slowInputs;
        slowInputs.insert(QStringLiteral("root"), slowDir.path());
        slowInputs.insert(QStringLiteral("hasFilelist"), true);
        slowInputs.insert(QStringLiteral("filelists"), QStringList{slowList});
        slowInputs.insert(QStringLiteral("settingsPath"),
                          slowDir.path() + QStringLiteral("/state/settings.toml"));
        slowShell->setProperty("vedit.project.inputs", slowInputs);
        vedit::workspace::IntegrationOptions slowIntegration;
        slowIntegration.projectRoot = slowDir.path();
        slowIntegration.stateDir = slowDir.path() + QStringLiteral("/state");
        vedit::workspace::installDiagnostics(*slowShell, slowIntegration);
        slowShell->resize(1200, 800);
        slowShell->show();
        QCoreApplication::processEvents();

        const auto pollerState = [&slowShell]() {
            return slowShell->property("vedit.diagnostics.projectFilesState").toMap();
        };
        const QVariantMap earlyState = pollerState();
        const qulonglong earlyJob = earlyState.value(QStringLiteral("job")).toULongLong();
        const bool sawSlow = waitUntil([&pollerState]() {
            return pollerState().value(QStringLiteral("phase")).toString()
                == QStringLiteral("slow");
        }, 15000);
        const QVariantMap slowState = pollerState();
        check("product.slow-phase", sawSlow,
              QStringLiteral("phase='%1'")
                  .arg(slowState.value(QStringLiteral("phase")).toString()));
        check("product.slow-job-same",
              earlyJob != 0
                  && slowState.value(QStringLiteral("job")).toULongLong() == earlyJob
                  && slowState.value(QStringLiteral("job")).toULongLong() != 0,
              QStringLiteral("early=%1 slow=%2")
                  .arg(earlyJob)
                  .arg(slowState.value(QStringLiteral("job")).toULongLong()));
        check("product.slow-backoff-1s",
              slowState.value(QStringLiteral("pollIntervalMs")).toInt() == 1000,
              QString::number(slowState.value(QStringLiteral("pollIntervalMs")).toInt()));
        check("product.slow-soft-timeout-shortened",
              slowState.value(QStringLiteral("softTimeoutMs")).toInt() == 0,
              QString::number(slowState.value(QStringLiteral("softTimeoutMs")).toInt()));
        const bool delivered = waitUntil([&pollerState]() {
            return pollerState().value(QStringLiteral("phase")).toString()
                == QStringLiteral("ready");
        }, 60000);
        const QVariantMap readyState = pollerState();
        check("product.slow-same-job-delivers",
              delivered
                  && readyState.value(QStringLiteral("job")).toULongLong() == earlyJob,
              QStringLiteral("ready=%1 job=%2 early=%3")
                  .arg(delivered ? 1 : 0)
                  .arg(readyState.value(QStringLiteral("job")).toULongLong())
                  .arg(earlyJob));
        check("product.slow-file-set-nonempty",
              readyState.value(QStringLiteral("files")).toInt() > 0,
              QString::number(readyState.value(QStringLiteral("files")).toInt()));
        qunsetenv("VEDIT_DIAGNOSTICS_EXPAND_SOFT_TIMEOUT_MS");
        qunsetenv("VEDIT_DIAGNOSTICS_EXPAND_POLL_INTERVAL_MS");
    }

    std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
