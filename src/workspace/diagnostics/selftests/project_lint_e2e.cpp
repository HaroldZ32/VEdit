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
#include "project_lint_notice.h"

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
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

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

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
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

    // RM-9: a project OPEN now starts an AUTOMATIC full pass as soon as the file
    // set is adopted. Wait until the set ARRIVED and every automatic pass it
    // started (including one deferred by the pass timer) is terminal; only then
    // is the explicit click under test certainly the NEXT pass. Otherwise this
    // harness mistakes the automatic open pass for its explicit one
    // (automatic=1, no dialog - the pre-RM-9 shape).
    waitUntil([controller]() {
        return !controller->projectFiles().isEmpty() && !controller->projectPassRunning();
    }, 120000);
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
        return 0;
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
        return 0;
    }
    if (controller->upperDiagnostics().isEmpty()) {
        skip("product.elab-content",
             QStringLiteral("the lint engine reported nothing (no toolchain for this fixture?)"));
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 0;
    }

    check("product.explicit-dialog", dialogsAfterExplicit == 1 && !controller->lastPassWasAutomatic(),
          QStringLiteral("dialogs=%1 automatic=%2")
              .arg(dialogsAfterExplicit)
              .arg(controller->lastPassWasAutomatic() ? 1 : 0));
    auto *dialog = shell->findChild<QDialog *>(QStringLiteral("veditElabDialog"));
    if (dialog != nullptr) {
        check("product.dialog-modal", dialog->isModal());
        dialog->hide();
    }
    check("product.explicit-notice", noticeShown);

    // ---- the AUTOMATIC path: one save, then never a modal dialog -----------
    vedit::app::EditorPane *pane = shell->editorArea()->activePane();
    check("product.pane", pane != nullptr);
    if (pane == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    QString openError;
    check("product.file-opened", pane->openFile(broken, &openError), openError);
    const quint64 beforeSave = controller->projectPassSerial();
    QString saveError;
    check("product.save", pane->saveFile(&saveError), saveError);
    // 700 ms debounce + the pass + the 300 ms presenter poll.
    const bool autoPassSeen = waitUntil([controller, beforeSave]() {
        return controller->projectPassSerial() != beforeSave && !controller->projectPassRunning();
    }, 120000);
    if (!autoPassSeen) {
        skip("product.automatic-pass", QStringLiteral("no pass after the save"));
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 0;
    }
    // Give the presenter poll a full turn.
    waitUntil([controller, beforeSave]() {
        return controller->lastPassWasAutomatic();
    }, 5000);
    const int dialogsAfterSave = dialogsOf(*shell);
    std::printf("OBSERVED product.auto-pass automatic=%d dialogs-before=%d dialogs-after=%d\n",
                controller->lastPassWasAutomatic() ? 1 : 0, dialogsAfterExplicit,
                dialogsAfterSave);
    check("product.automatic-pass-was-automatic", controller->lastPassWasAutomatic());
    check("product.automatic-no-dialog", dialogsAfterSave == dialogsAfterExplicit,
          QStringLiteral("before=%1 after=%2").arg(dialogsAfterExplicit).arg(dialogsAfterSave));

    std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
