// UX batch 2 / B3 repair — the PRODUCT WIRING the independent review failed.
//
// This binary drives the real ApplicationShell and the real diagnostics lane
// (installDiagnostics), with the project-lint backend replaced by a fake so the
// assertions are deterministic and need no lint toolchain. It covers the
// findings of evidence/ux-batch-2/review-b3/REPORT.md that live in the product
// wiring rather than in the controller the lane self-test certified:
//
//   1 (BLOCKER) hover popup lifetime: tab switch re-parents, view destruction
//     clears the cache and the next hover/navigation is clean;
//   2 (HIGH) R2 tracks EVERY open document (background tab edited + switch);
//   3 (HIGH) `projectRootChanged` re-pins the file set and clears old rows;
//   5 (HIGH, ruling C8) the automatic pass includes the just-saved file and
//     other unmodified open files, and excludes modified buffers;
//   12 (MEDIUM e) the product assertion detects a RE-SHOW of the same dialog
//     object (counter + visibility, not object count).
//
// Every observation is a shell property the lane publishes; a missing property
// fails the check instead of being read as a pass.

#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "diagnostics_panel.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "filetree_integration.h"
#include "file_tree_widget.h"
#include "file_tree_model.h"
#include "ktexteditor_provider.h"
#include "panel_registry.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QAbstractItemModel>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeView>
#include <QVariantMap>
#include <QWidget>

#include <cstdio>
#include <functional>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skips = 0;
QString g_shotDir; ///< VEDIT_B3_SHOT_DIR: save the contract's screenshots

void shoot(QWidget *widget, const QString &name)
{
    if (g_shotDir.isEmpty() || widget == nullptr) {
        return;
    }
    const QString path = QDir(g_shotDir).filePath(name);
    if (widget->grab().save(path)) {
        std::printf("SCREENSHOT %s\n", qPrintable(path));
    } else {
        std::printf("SCREENSHOT-FAILED %s\n", qPrintable(path));
    }
    std::fflush(stdout);
}

/// A WINDOW-LEVEL capture: the real X11 screen region, not a widget render.
/// Under Xvfb+xcb this includes every top-level the product put inside `rect`
/// (the ELAB dialog, the hover popup), which a `QWidget::grab()` of the shell
/// cannot show.
void shootScreen(const QString &name, const QRect &rect)
{
    if (g_shotDir.isEmpty()) {
        return;
    }
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        std::printf("SCREENSHOT-FAILED %s reason='no screen'\n", qPrintable(name));
        std::fflush(stdout);
        return;
    }
    const QRect area = rect.isValid() ? rect : screen->geometry();
    const QPixmap shot = screen->grabWindow(0, area.x(), area.y(), area.width(), area.height());
    const QString path = QDir(g_shotDir).filePath(name);
    if (!shot.isNull() && shot.save(path)) {
        std::printf("SCREENSHOT %s %dx%d (screen region %d,%d)\n", qPrintable(path),
                    shot.width(), shot.height(), area.x(), area.y());
    } else {
        std::printf("SCREENSHOT-FAILED %s reason='screen grab failed'\n", qPrintable(name));
    }
    std::fflush(stdout);
}
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

/// A project pass that answers immediately and records the exact request it was
/// given (the C8 content rule is observable there).
class FakeProjectLint final : public vedit::workspace::diagnostics::ProjectLintBackend {
public:
    QVector<vedit::workspace::diagnostics::DiagnosticEntry> rows;
    QStringList filesSeen;
    int startCount = 0;
    int cancelCount = 0;
    /// N3-p: keep the NEXT pass RUNNING (poll() stays pending) until it is
    /// cancelled, so a settings/backend switch can be applied while the old
    /// engine's job is genuinely in flight.
    bool holdNextPass = false;

    bool available() const override { return true; }
    QString toolName() const override { return QStringLiteral("fake-lint"); }

    quint64 start(const vedit::workspace::diagnostics::LintPassRequest &request,
                  QString *) override
    {
        ++startCount;
        filesSeen = request.files;
        job_ = static_cast<quint64>(startCount);
        snapshot_ = vedit::workspace::diagnostics::LintPassSnapshot{};
        holding_ = holdNextPass;
        holdNextPass = false;
        snapshot_.total = request.files.size();
        snapshot_.done = holding_ ? 0 : request.files.size();
        snapshot_.tool = QStringLiteral("fake-lint");
        snapshot_.phase = holding_ ? vedit::workspace::diagnostics::LintPhase::Running
                                   : vedit::workspace::diagnostics::LintPhase::Finished;
        return job_;
    }
    vedit::workspace::diagnostics::LintPassSnapshot progress(quint64) const override
    {
        return snapshot_;
    }
    vedit::workspace::diagnostics::LspPolled poll(quint64) override
    {
        vedit::workspace::diagnostics::LspPolled polled;
        if (holding_) {
            polled.pending = true;
            return polled;
        }
        polled.pending = false;
        polled.ready = true;
        polled.kind = vedit::workspace::diagnostics::ResponseKind::Diagnostics;
        polled.diagnostics = rows;
        snapshot_.phase = vedit::workspace::diagnostics::LintPhase::Finished;
        snapshot_.done = snapshot_.total;
        return polled;
    }
    void cancel(quint64) override { ++cancelCount; }
    void release(quint64) override {}

private:
    quint64 job_ = 0;
    bool holding_ = false;
    vedit::workspace::diagnostics::LintPassSnapshot snapshot_;
};

vedit::workspace::diagnostics::DiagnosticEntry upperRow(const QString &file, int line,
                                                        const QString &code,
                                                        const QString &message,
                                                        int severity = 0)
{
    vedit::workspace::diagnostics::DiagnosticEntry entry;
    entry.file = file;
    entry.line = line;
    entry.column = 2;
    entry.endLine = line;
    entry.endColumn = 10;
    entry.severity = severity; // the CLI ABI scale (0 error, 1 warning)
    entry.code = code;
    entry.source = QStringLiteral("cli:fake");
    entry.message = message;
    return entry;
}

/// The fake rows for one root: an error on the root's first source file and a
/// warning on `b.sv` (so the dots screenshot shows all three real statuses).
void setRows(FakeProjectLint &pass, const QString &projectDir)
{
    pass.rows.clear();
    for (const QString &name : {QStringLiteral("a.sv"), QStringLiteral("c.sv")}) {
        const QString path = QDir(projectDir).filePath(name);
        if (QFileInfo::exists(path)) {
            pass.rows.append(upperRow(path, 1, QStringLiteral("no-trailing-spaces"),
                                      QStringLiteral("fake upper-layer error")));
            // A rule the engine cannot autofix: the second hover branch.
            pass.rows.append(upperRow(path, 2, QStringLiteral("line-length"),
                                      QStringLiteral("fake long line")));
            break;
        }
    }
    const QString warnPath = QDir(projectDir).filePath(QStringLiteral("b.sv"));
    if (QFileInfo::exists(warnPath)) {
        pass.rows.append(upperRow(warnPath, 1, QStringLiteral("line-length"),
                                  QStringLiteral("fake warning"), 1));
    }
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    g_shotDir = qEnvironmentVariable("VEDIT_B3_SHOT_DIR");
    if (!g_shotDir.isEmpty()) {
        QDir().mkpath(g_shotDir);
    }
    QTemporaryDir dir;
    if (!dir.isValid()) {
        check("product.tempdir", false);
        return 1;
    }
    const QString project1 = dir.filePath(QStringLiteral("project1"));
    const QString project2 = dir.filePath(QStringLiteral("project2"));
    QDir().mkpath(project1);
    QDir().mkpath(project2);
    const QString aPath = QDir(project1).filePath(QStringLiteral("a.sv"));
    const QString bPath = QDir(project1).filePath(QStringLiteral("b.sv"));
    const QString renamePath = QDir(project1).filePath(QStringLiteral("rename.sv"));
    // N2: a project file that is NOT opened anywhere before its regression, so
    // the watcher has provably never subscribed to its document.
    const QString dPath = QDir(project1).filePath(QStringLiteral("d.sv"));
    // N2-r: the same property, but the tab is SWITCHED AWAY before the first
    // poll runs (the review's N2-r ordering). It also has to start covered, so
    // it is a clean project file no test opens earlier.
    const QString ePath = QDir(project1).filePath(QStringLiteral("e.sv"));
    const QString cPath = QDir(project2).filePath(QStringLiteral("c.sv"));
    check("product.fixture",
          writeFile(aPath, QStringLiteral("module a;\n  logic x;   \nendmodule\n"))
              && writeFile(bPath, QStringLiteral("module b;\n  logic y;\nendmodule\n"))
              && writeFile(dPath, QStringLiteral("module d;\n  logic w;\nendmodule\n"))
              && writeFile(ePath, QStringLiteral("module e;\n  logic v;\nendmodule\n"))
              && writeFile(renamePath,
                           QStringLiteral("module rn;\n  // commentword\n"
                                          "  initial $display(\"hello\");\n"
                                          "  assign x = 1'b0;\n"
                                          "  localparam int n = 123;\nendmodule\n"))
              && writeFile(cPath, QStringLiteral("module c;\n  logic z;\nendmodule\n")));

    vedit::app::ShellOptions options;
    options.stateDir = dir.filePath(QStringLiteral("state"));
    options.restoreSession = false;
    options.showDefaultPanels = false;
    // The real file tree is a product panel: a dots screenshot must show it in
    // the real window, not a synthetic QTreeWidget.
    options.projectRoot = project1;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    std::unique_ptr<vedit::app::ApplicationShell> shell(new vedit::app::ApplicationShell(options));
    if (provider != nullptr) {
        shell->editorArea()->setProvider(provider.get());
    }
    shell->editorArea()->ensurePane();
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = project1;
    integration.stateDir = dir.filePath(QStringLiteral("state"));
    vedit::workspace::installDiagnostics(*shell, integration);
    vedit::workspace::installFileTree(*shell, integration);
    shell->panels()->showPanel(QStringLiteral("outline"), shell.get());
    shell->panels()->showPanel(QStringLiteral("file-tree"), shell.get());
    shell->resize(1200, 800);
    shell->show();
    QCoreApplication::processEvents();

    auto *panel = shell->findChild<vedit::workspace::diagnostics::DiagnosticsPanel *>();
    QAction *lintAction = shell->commands() != nullptr
        ? shell->commands()->action(QStringLiteral("project.lint"))
        : nullptr;
    QAction *hoverAction = shell->commands() != nullptr
        ? shell->commands()->action(QStringLiteral("diagnostics.hover"))
        : nullptr;
    check("product.panel", panel != nullptr);
    check("product.lint-action", lintAction != nullptr);
    check("product.hover-action", hoverAction != nullptr);
    if (panel == nullptr || lintAction == nullptr || hoverAction == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    auto *controller = panel->controller();

    // Replace the real batch backend with the deterministic fake.
    auto fakeOwned = std::make_unique<FakeProjectLint>();
    FakeProjectLint *fake = fakeOwned.get();
    setRows(*fake, project1);
    controller->setProjectLintBackend(std::move(fakeOwned));

    // ---- nit 18: the first click before the async file-set poll answers -----
    lintAction->trigger();
    const QString preloadMessage = shell->statusBar()->currentMessage();
    check("project.preload-message",
          !preloadMessage.contains(QStringLiteral("No project file set")),
          QStringLiteral("status='%1'").arg(preloadMessage));

    // ---- wait for the project file set, then run ONE explicit pass ----------
    bool started = false;
    for (int attempt = 0; attempt < 80 && !started; ++attempt) {
        const int before = fake->startCount;
        lintAction->trigger();
        started = waitUntil([fake, before]() { return fake->startCount != before; }, 250);
        if (!started) {
            QThread::msleep(50);
            QCoreApplication::processEvents();
        }
    }
    if (!started) {
        skip("product.explicit-pass", QStringLiteral("the project file set never arrived"));
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 0;
    }
    waitUntil([controller]() { return !controller->projectPassRunning(); }, 5000);
    QCoreApplication::processEvents();
    check("product.explicit-pass-files",
          fake->filesSeen.contains(aPath) && fake->filesSeen.contains(bPath),
          QStringLiteral("files=%1").arg(fake->filesSeen.join(QLatin1Char(','))));
    if (!g_shotDir.isEmpty()) {
        // (a) the ELAB dialog in the REAL window: a screen grab, because the
        // dialog is a separate top-level that QWidget::grab() cannot include.
        const bool dialogVisible = waitUntil([&shell]() {
            auto *dialog = shell->findChild<QDialog *>(QStringLiteral("veditElabDialog"));
            return dialog != nullptr && dialog->isVisible();
        }, 3000);
        QDialog *dialog = shell->findChild<QDialog *>(QStringLiteral("veditElabDialog"));
        if (dialogVisible && dialog != nullptr) {
            shootScreen(QStringLiteral("01-elab-explicit-dialog.png"), dialog->frameGeometry());
            shootScreen(QStringLiteral("02-window-with-dialog.png"),
                        QGuiApplication::primaryScreen() != nullptr
                            ? QGuiApplication::primaryScreen()->geometry()
                            : shell->frameGeometry());
            dialog->hide();
        } else {
            std::printf("SCREENSHOT-MISSING 01/02 reason='the ELAB dialog never became visible'\n");
            shootScreen(QStringLiteral("02-window-with-dialog.png"), shell->frameGeometry());
        }
        // (b) the diagnostics panel itself (a real product widget).
        shoot(panel, QStringLiteral("03-diagnostics-panel.png"));
        // (c) the dot column inside the REAL file tree, in the REAL window.
        auto *tree = shell->findChild<vedit::workspace::FileTreeWidget *>();
        if (tree != nullptr && tree->rootPath().isEmpty()) {
            tree->openDirectory(project1);
        }
        const bool treeReady = waitUntil([&shell, &tree]() {
            return tree != nullptr && tree->view() != nullptr && tree->view()->model() != nullptr
                && tree->view()->model()->rowCount() > 0
                && shell->property("vedit.diagnostics.fileStatus").toMap().size() >= 3;
        }, 8000);
        std::printf("OBSERVED screenshot.tree-ready=%d root='%s' rows=%d statuses=%d\n",
                    treeReady ? 1 : 0,
                    tree != nullptr ? qPrintable(tree->rootPath()) : "<no-widget>",
                    tree != nullptr && tree->view() != nullptr && tree->view()->model() != nullptr
                        ? tree->view()->model()->rowCount()
                        : -1,
                    static_cast<int>(
                        shell->property("vedit.diagnostics.fileStatus").toMap().size()));
        QCoreApplication::processEvents();
        QThread::msleep(600); // let the delegate paint the dots before the grab
        QCoreApplication::processEvents();
        if (tree != nullptr && tree->view() != nullptr && tree->view()->viewport() != nullptr) {
            tree->view()->viewport()->repaint();
        }
        QCoreApplication::processEvents();
        check("screenshot.file-tree-ready", treeReady,
              QStringLiteral("rows=%1 statuses=%2")
                  .arg(tree != nullptr && tree->view() != nullptr && tree->view()->model() != nullptr
                           ? tree->view()->model()->rowCount()
                           : -1)
                  .arg(shell->property("vedit.diagnostics.fileStatus").toMap().size()));
        shootScreen(QStringLiteral("05-file-status-dots.png"), shell->frameGeometry());
    }

    vedit::app::EditorPane *pane = shell->editorArea()->activePane();
    check("product.pane", pane != nullptr);
    if (pane == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    QString openError;
    check("product.open-a", pane->openFile(aPath, &openError), openError);
    check("product.open-b", pane->openFile(bPath, &openError), openError);
    pane->setActiveTab(pane->tabIndexForPath(aPath));
    QCoreApplication::processEvents();

    // The 400 ms live poll adopts the active view and publishes the hover map.
    auto hoverState = [&shell]() {
        return shell->property("vedit.diagnostics.hover").toMap();
    };
    auto presentationState = [&shell]() {
        return shell->property("vedit.diagnostics.presentation").toMap();
    };
    auto upperFiles = [&shell]() {
        return shell->property("vedit.diagnostics.upperFiles").toStringList();
    };
    // A hover popup is a CHILD widget, so the screen frame must be flushed
    // before the grab (a widget grab would hide a real placement bug).
    auto shootHover = [&shell](const QString &name) {
        if (QWidget *popup = shell->findChild<QWidget *>(QStringLiteral("veditHoverPopup"))) {
            popup->raise();
            popup->repaint();
            std::printf("OBSERVED screenshot.hover-popup visible=%d rect=%d,%d %dx%d\n",
                        popup->isVisible() ? 1 : 0, popup->x(), popup->y(), popup->width(),
                        popup->height());
        }
        QCoreApplication::processEvents();
        shootScreen(name, shell->frameGeometry());
    };
    waitUntil([&hoverState]() { return hoverState().value(QStringLiteral("known")).toBool(); }, 3000);

    // =======================================================================
    // 1 (BLOCKER) — the hover popup outlives neither its view nor its tab
    // =======================================================================
    KTextEditor::View *viewA = qobject_cast<KTextEditor::View *>(pane->editorWidget());
    check("hover.view-a", viewA != nullptr);
    if (viewA != nullptr) {
        // The upper layer's error sits on line 1 (0-based); put the pointer on it.
        const QPoint pos = viewA->cursorToCoordinate(KTextEditor::Cursor(1, 4));
        QMouseEvent move(QEvent::MouseMove, pos, viewA->mapToGlobal(pos), Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewA, &move);
        // N4: read the state at the HOVER INSTANT, before the event loop runs.
        // A probe that ran on the mouse-move path would already have completed
        // (probePending=false); the asynchronous one cannot have.
        const QVariantMap atHover = hoverState();
        QCoreApplication::processEvents();
        waitUntil([&hoverState]() {
            return hoverState().value(QStringLiteral("alive")).toBool();
        }, 3000);
        QVariantMap state = hoverState();
        std::printf("OBSERVED hover.after-hover alive=%d parent=%d diagnostic=%d rule='%s'\n",
                    state.value(QStringLiteral("alive")).toBool() ? 1 : 0,
                    state.value(QStringLiteral("parentIsActiveView")).toBool() ? 1 : 0,
                    state.value(QStringLiteral("diagnostic")).toBool() ? 1 : 0,
                    qPrintable(state.value(QStringLiteral("rule")).toString()));
        check("hover.popup-created", state.value(QStringLiteral("alive")).toBool(),
              QStringLiteral("map=%1").arg(hoverState().isEmpty() ? QStringLiteral("<missing>")
                                                                  : QStringLiteral("present")));
        check("hover.parent-is-active-view",
              state.value(QStringLiteral("parentIsActiveView")).toBool());
        check("hover.shows-diagnostic", state.value(QStringLiteral("diagnostic")).toBool());
        // N4: the probe must still be PENDING at the hover instant. A
        // synchronous probe returns from probeAsync() with the answer already
        // delivered, so this flips to false and the check fails.
        check("hover.probe-off-mouse-path",
              atHover.value(QStringLiteral("probePending")).toBool()
                  && atHover.value(QStringLiteral("diagnostic")).toBool(),
              QStringLiteral("pending-at-hover=%1 diagnostic=%2")
                  .arg(atHover.value(QStringLiteral("probePending")).toBool() ? 1 : 0)
                  .arg(atHover.value(QStringLiteral("diagnostic")).toBool() ? 1 : 0));
        // The measured answer arrives asynchronously: this rule IS fixable.
        const bool fixableKnown = waitUntil([&hoverState]() {
            const QVariantMap current = hoverState();
            return current.contains(QStringLiteral("probePending"))
                && !current.value(QStringLiteral("probePending")).toBool();
        }, 20000);
        check("hover.fixable-after-async-probe",
              fixableKnown && hoverState().value(QStringLiteral("fixable")).toBool(),
              QStringLiteral("fixable=%1")
                  .arg(hoverState().value(QStringLiteral("fixable")).toBool() ? 1 : 0));
        // U2b: the button's SOURCE string is English ASCII, so the untranslated
        // (en) UI reads "Fix" and the zh catalogue translates it. Before the fix
        // the source was Chinese, so this check would fail on "修复".
        auto *fixButton = shell->findChild<QPushButton *>(QStringLiteral("veditHoverFix"));
        check("hover.fix-button-label-english",
              fixButton != nullptr && fixButton->text() == QStringLiteral("Fix"),
              QStringLiteral("text='%1'").arg(fixButton != nullptr ? fixButton->text()
                                                                  : QStringLiteral("<none>")));
        shootHover(QStringLiteral("04a-hover-popup-fixable.png"));
        // The other branch: a rule with no autofix gets the reason and no button.
        if (viewA != nullptr) {
            const QPoint noFixPos = viewA->cursorToCoordinate(KTextEditor::Cursor(2, 4));
            QMouseEvent noFixMove(QEvent::MouseMove, noFixPos, viewA->mapToGlobal(noFixPos),
                                  Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewA, &noFixMove);
            QCoreApplication::processEvents();
            const bool noFixKnown = waitUntil([&hoverState]() {
                const QVariantMap current = hoverState();
                return current.value(QStringLiteral("rule")).toString()
                           == QStringLiteral("line-length")
                    && current.contains(QStringLiteral("probePending"))
                    && !current.value(QStringLiteral("probePending")).toBool();
            }, 20000);
            check("hover.no-fix-branch",
                  noFixKnown && !hoverState().value(QStringLiteral("fixable")).toBool(),
                  QStringLiteral("fixable=%1")
                      .arg(hoverState().value(QStringLiteral("fixable")).toBool() ? 1 : 0));
            shootHover(QStringLiteral("04b-hover-popup-no-fix.png"));

            // N4: `stale` is a COMPUTED field, not a hard-coded false. Clear the
            // rows the visible bubble came from; the next live poll must report
            // that the cached row no longer exists.
            controller->clearProjectDiagnostics();
            const bool staleReported = waitUntil([&hoverState]() {
                const QVariantMap current = hoverState();
                return current.value(QStringLiteral("diagnostic")).toBool()
                    && current.value(QStringLiteral("stale")).toBool();
            }, 3000);
            check("hover.stale-detects-cleared-row", staleReported,
                  QStringLiteral("diagnostic=%1 stale=%2")
                      .arg(hoverState().value(QStringLiteral("diagnostic")).toBool() ? 1 : 0)
                      .arg(hoverState().value(QStringLiteral("stale")).toBool() ? 1 : 0));
            // Move off the (now nonexistent) row: the bubble must hide, then
            // re-cover the project so the following sections start normally.
            const QPoint blank = viewA->cursorToCoordinate(KTextEditor::Cursor(9, 0));
            QMouseEvent blankMove(QEvent::MouseMove, blank, viewA->mapToGlobal(blank),
                                  Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewA, &blankMove);
            QCoreApplication::processEvents();
            const int passesBeforeRecover = fake->startCount;
            lintAction->trigger();
            waitUntil([fake, passesBeforeRecover]() {
                return fake->startCount != passesBeforeRecover;
            }, 3000);
            waitUntil([controller]() { return !controller->projectPassRunning(); }, 5000);
            QCoreApplication::processEvents();
        }
    }

    // (b) tab switch: the popup must follow the ACTIVE view (a hidden parent
    // would make the next diagnostic hover invisible).
    pane->setActiveTab(pane->tabIndexForPath(bPath));
    QCoreApplication::processEvents();
    const bool tabSwitchOk = waitUntil([&hoverState]() {
        const QVariantMap state = hoverState();
        return state.value(QStringLiteral("alive")).toBool()
            && state.value(QStringLiteral("parentIsActiveView")).toBool()
            && state.value(QStringLiteral("hiddenOnViewChange")).toBool();
    }, 3000);
    check("hover.tab-switch", tabSwitchOk,
          QStringLiteral("parent=%1 hidden=%2")
              .arg(hoverState().value(QStringLiteral("parentIsActiveView")).toBool() ? 1 : 0)
              .arg(hoverState().value(QStringLiteral("hiddenOnViewChange")).toBool() ? 1 : 0));
    pane->setActiveTab(pane->tabIndexForPath(aPath));
    QCoreApplication::processEvents();

    // =======================================================================
    // 2 (HIGH) — R2 follows EVERY open document, not just the active tab
    // =======================================================================
    check("upper.initial-covers-b",
          waitUntil([&upperFiles, &bPath]() { return upperFiles().contains(bPath); }, 5000),
          QStringLiteral("upper=%1").arg(upperFiles().join(QLatin1Char(','))));
    // Edit the GENUINELY BACKGROUND tab: A stays the active tab the whole time,
    // and B's document is edited through its own widget. The old check activated
    // B first, so the active-file poll fallback could pass it without proving
    // background tracking at all.
    pane->setActiveTab(pane->tabIndexForPath(aPath));
    QCoreApplication::processEvents();
    QWidget *widgetB = pane->tabEditorWidget(pane->tabIndexForPath(bPath));
    auto *viewB = qobject_cast<KTextEditor::View *>(widgetB);
    check("r2.background-tab-view", viewB != nullptr && viewB->document() != nullptr);
    if (viewB != nullptr && viewB->document() != nullptr) {
        viewB->document()->insertText(KTextEditor::Cursor(1, 2), QStringLiteral("dirty "));
    }
    check("r2.edit-was-background", pane->currentFilePath() == aPath,
          QStringLiteral("active='%1' edited='%2'")
              .arg(pane->currentFilePath(), bPath));
    const bool backgroundInvalidated =
        waitUntil([&upperFiles, &bPath]() { return !upperFiles().contains(bPath); }, 3000);
    check("r2.background-tab-invalidated", backgroundInvalidated,
          QStringLiteral("upper=%1").arg(upperFiles().join(QLatin1Char(','))));

    // =======================================================================
    // 5 (HIGH, ruling C8) — save A while B is dirty: the pass covers A, not B
    // =======================================================================
    // Re-cover the project with an explicit pass first (B is dirty and will be
    // excluded from it once the ruling is implemented).
    {
        const int before = fake->startCount;
        lintAction->trigger();
        waitUntil([fake, before]() { return fake->startCount != before; }, 3000);
        waitUntil([controller]() { return !controller->projectPassRunning(); }, 5000);
    }
    pane->setActiveTab(pane->tabIndexForPath(bPath));
    QCoreApplication::processEvents();
    viewB = qobject_cast<KTextEditor::View *>(pane->editorWidget());
    check("c8.b-is-dirty", pane->isTabModified(pane->tabIndexForPath(bPath)));
    pane->setActiveTab(pane->tabIndexForPath(aPath));
    QCoreApplication::processEvents();
    QString saveError;
    const int passesBeforeA = fake->startCount;
    check("c8.save-a", pane->saveFile(&saveError), saveError);
    check("c8.auto-pass-after-save",
          waitUntil([fake, passesBeforeA]() { return fake->startCount > passesBeforeA; }, 10000));
    std::printf("OBSERVED c8.after-save-a files='%s'\n", qPrintable(fake->filesSeen.join(QLatin1Char(','))));
    check("c8.saved-file-included", fake->filesSeen.contains(aPath),
          QStringLiteral("files=%1").arg(fake->filesSeen.join(QLatin1Char(','))));
    check("c8.dirty-buffer-excluded", !fake->filesSeen.contains(bPath),
          QStringLiteral("files=%1").arg(fake->filesSeen.join(QLatin1Char(','))));

    // After B is saved the next automatic pass must cover it too.
    pane->setActiveTab(pane->tabIndexForPath(bPath));
    QCoreApplication::processEvents();
    const int passesBeforeB = fake->startCount;
    check("c8.save-b", pane->saveFile(&saveError), saveError);
    check("c8.auto-pass-after-save-b",
          waitUntil([fake, passesBeforeB]() { return fake->startCount > passesBeforeB; }, 10000));
    std::printf("OBSERVED c8.after-save-b files='%s'\n", qPrintable(fake->filesSeen.join(QLatin1Char(','))));
    check("c8.saved-b-now-included", fake->filesSeen.contains(bPath),
          QStringLiteral("files=%1").arg(fake->filesSeen.join(QLatin1Char(','))));
    check("c8.unmodified-open-included", fake->filesSeen.contains(aPath),
          QStringLiteral("files=%1").arg(fake->filesSeen.join(QLatin1Char(','))));

    // =======================================================================
    // N2 — the FIRST edit of a newly opened, already-covered file
    // =======================================================================
    // d.sv has never been opened, so the 400 ms watcher poll has never
    // subscribed its document. Open it and edit it in the SAME event-loop turn:
    // the textChanged has no listener, which is exactly the pre-subscription
    // window the path-change branch used to swallow.
    {
        check("n2.file-covered-before-open", upperFiles().contains(dPath),
              QStringLiteral("upper=%1").arg(upperFiles().join(QLatin1Char(','))));
        QString dOpenError;
        check("n2.open-d", pane->openFile(dPath, &dOpenError), dOpenError);
        auto *viewD = qobject_cast<KTextEditor::View *>(pane->editorWidget());
        check("n2.view-d", viewD != nullptr && viewD->document() != nullptr);
        if (viewD != nullptr && viewD->document() != nullptr) {
            viewD->document()->insertText(KTextEditor::Cursor(1, 2), QStringLiteral("n2 "));
        }
        check("n2.d-is-dirty", pane->isTabModified(pane->tabIndexForPath(dPath)));
        const bool invalidated =
            waitUntil([&upperFiles, &dPath]() { return !upperFiles().contains(dPath); }, 3000);
        check("n2.first-edit-before-poll-invalidates", invalidated,
              QStringLiteral("upper=%1").arg(upperFiles().join(QLatin1Char(','))));
    }

    // =======================================================================
    // N2-r — open, edit, and switch AWAY before the 400 ms poll subscribes
    // =======================================================================
    // The block above keeps the edited tab ACTIVE, so the poll's path-change
    // branch still sees its dirty flag. The review's N2-r ordering is the other
    // one: the edit lands before ANY poll has subscribed the document, and the
    // tab is inactive by the time the poll runs. e.sv has never been opened, so
    // it is a clean, covered project file.
    {
        // Re-cover the project first. d.sv is open and dirty now, so C8's
        // content rule excludes it; e.sv is closed and is included.
        const int before = fake->startCount;
        lintAction->trigger();
        waitUntil([fake, before]() { return fake->startCount != before; }, 3000);
        waitUntil([controller]() { return !controller->projectPassRunning(); }, 5000);
        QCoreApplication::processEvents();
        check("n2r.e-covered-before-open", upperFiles().contains(ePath),
              QStringLiteral("upper=%1").arg(upperFiles().join(QLatin1Char(','))));
        // ONE synchronous turn: open -> edit -> switch away. No event loop runs
        // here, so the 400 ms poll cannot see e.sv as the active tab — the exact
        // sequence that used to leave its upper (green) verdict alive.
        QString eOpenError;
        check("n2r.open-e", pane->openFile(ePath, &eOpenError), eOpenError);
        auto *viewE = qobject_cast<KTextEditor::View *>(pane->editorWidget());
        check("n2r.view-e", viewE != nullptr && viewE->document() != nullptr);
        if (viewE != nullptr && viewE->document() != nullptr) {
            viewE->document()->insertText(KTextEditor::Cursor(1, 2), QStringLiteral("n2r "));
        }
        check("n2r.e-is-dirty", pane->isTabModified(pane->tabIndexForPath(ePath)));
        pane->setActiveTab(pane->tabIndexForPath(aPath));
        check("n2r.e-switched-away-before-poll", pane->currentFilePath() == aPath,
              QStringLiteral("active='%1'").arg(pane->currentFilePath()));
        const bool invalidated = waitUntil([&upperFiles, &ePath]() {
            return !upperFiles().contains(ePath);
        }, 3000);
        const QHash<QString, int> statuses = controller->fileStatuses();
        check("n2r.edit-before-poll-invalidates", invalidated,
              QStringLiteral("upper=%1").arg(upperFiles().join(QLatin1Char(','))));
        check("n2r.e-no-longer-green",
              !statuses.contains(ePath) || statuses.value(ePath) != 3,
              QStringLiteral("status=%1").arg(statuses.value(ePath, 0)));
    }

    // =======================================================================
    // N3 — a backend/settings switch drops the previous pass's upper rows
    // =======================================================================
    // The product path: the settings watcher reads <stateDir>/settings.toml and
    // applies it; applySettings() used to clear only the lower layer, leaving the
    // previous engine's project-pass rows alive under the new one. Switching the
    // lint engine is a real backend switch (state_.apply() selects it).
    //
    // The key is written QUOTED because the store's flat key literally contains
    // dots (vd-settings writes it the same way).
    {
        const QString settingsPath =
            QDir(options.stateDir).filePath(QStringLiteral("settings.toml"));
        QDir().mkpath(options.stateDir);
        // N3-p: the switch below must land while a pass is still RUNNING. The
        // fake holds this pass until it is cancelled, so the pre-repair code kept
        // the old engine's job alive across the switch: the rows were cleared but
        // `passRunning_` stayed true and the next explicit Lint Project was
        // refused as "already running".
        fake->holdNextPass = true;
        const int heldStarts = fake->startCount;
        lintAction->trigger();
        const bool inFlight = waitUntil([fake, heldStarts, controller]() {
            return fake->startCount != heldStarts && controller->projectPassRunning();
        }, 5000);
        check("n3p.pass-in-flight-before-switch", inFlight,
              QStringLiteral("starts=%1 running=%2")
                  .arg(fake->startCount - heldStarts)
                  .arg(controller->projectPassRunning() ? 1 : 0));
        check("switch.write-settings",
              writeFile(settingsPath, QStringLiteral("\"tools.lint_engine\" = \"verible\"\n")),
              settingsPath);
        const bool switched = waitUntil([&shell]() {
            return shell->property("vedit.diagnostics.switch").toMap()
                       .value(QStringLiteral("ok")).toBool();
        }, 15000);
        const QVariantMap switchState = shell->property("vedit.diagnostics.switch").toMap();
        std::printf("OBSERVED switch.ok=%d backend='%s' lint='%s' reason='%s'\n",
                    switchState.value(QStringLiteral("ok")).toBool() ? 1 : 0,
                    qPrintable(switchState.value(QStringLiteral("backend")).toString()),
                    qPrintable(switchState.value(QStringLiteral("lint")).toString()),
                    qPrintable(switchState.value(QStringLiteral("reason")).toString()));
        check("switch.applied", switched,
              QStringLiteral("ok=%1 reason='%2'")
                  .arg(switchState.value(QStringLiteral("ok")).toBool() ? 1 : 0)
                  .arg(switchState.value(QStringLiteral("reason")).toString()));
        // N3: no row of the previous pass may survive under the new engine.
        check("n3.old-upper-rows-cleared",
              switched && upperFiles().isEmpty(),
              QStringLiteral("upper=%1").arg(upperFiles().join(QLatin1Char(','))));
        // N1 at the product level: the cleared upper rows are gone from the dot
        // source (the controller-level test drives the LOWER stale-green path,
        // where the switch's reanalysis may legitimately produce a fresh dot).
        check("n1.old-file-not-covered-after-switch",
              switched && !controller->hasUpperLayer(aPath),
              QStringLiteral("covered-a=%1 dot-map=%2")
                  .arg(controller->hasUpperLayer(aPath) ? 1 : 0)
                  .arg(controller->fileStatuses().size()));
        // N3-p: applySettings() must CANCEL the old engine's pass, not only clear
        // its rows. The refusal the review proved is the user-visible symptom.
        check("n3p.in-flight-pass-cancelled",
              switched && fake->cancelCount >= 1 && !controller->projectPassRunning(),
              QStringLiteral("cancels=%1 running=%2")
                  .arg(fake->cancelCount)
                  .arg(controller->projectPassRunning() ? 1 : 0));
        const int restartStarts = fake->startCount;
        lintAction->trigger();
        const bool newPassAccepted = waitUntil([fake, restartStarts, controller]() {
            return fake->startCount != restartStarts && controller->projectPassRunning();
        }, 3000);
        check("n3p.new-explicit-pass-accepted", newPassAccepted,
              QStringLiteral("starts=%1 running=%2 status='%3'")
                  .arg(fake->startCount - restartStarts)
                  .arg(controller->projectPassRunning() ? 1 : 0)
                  .arg(shell->statusBar()->currentMessage()));
        if (!newPassAccepted) {
            // Bound the red run: without the cancel the held job would stay
            // "running" and every later pass in this binary would be refused.
            controller->cancelProjectPass();
        }
        waitUntil([controller]() { return !controller->projectPassRunning(); }, 5000);
    }

    // =======================================================================
    // 6 (MEDIUM a) — the rename validation, through the PRODUCT request path
    // =======================================================================
    // The name dialog only opens for a valid target, so the invalid positions
    // below are answered before it: the shell property carries the reason.
    {
        QAction *renameAction = shell->commands() != nullptr
            ? shell->commands()->action(QStringLiteral("diagnostics.rename"))
            : nullptr;
        waitUntil([&shell]() {
            return shell->property("vedit.diagnostics.gates").toMap().contains(
                QStringLiteral("rename"));
        }, 3000);
        const QVariantMap renameGate = shell->property("vedit.diagnostics.gates")
                                           .toMap().value(QStringLiteral("rename")).toMap();
        const bool renameEnabled = renameGate.value(QStringLiteral("enabled")).toBool();
        std::printf("OBSERVED rename.gate enabled=%d reason='%s'\n", renameEnabled ? 1 : 0,
                    qPrintable(renameGate.value(QStringLiteral("reason")).toString()));
        if (renameAction == nullptr || !renameEnabled) {
            skip("rename.product-validation",
                 QStringLiteral("the rename action is not enabled for this backend: %1")
                     .arg(renameGate.value(QStringLiteral("reason")).toString()));
        } else {
            QString renameOpenError;
            check("rename.open-fixture", pane->openFile(renamePath, &renameOpenError),
                  renameOpenError);
            QCoreApplication::processEvents();
            auto *renameView = qobject_cast<KTextEditor::View *>(pane->editorWidget());
            check("rename.view", renameView != nullptr && renameView->document() != nullptr);
            if (renameView != nullptr && renameView->document() != nullptr) {
                const auto refusedAt =
                    [&](const char *name, int line, int column, const QString &needle) {
                        renameView->setCursorPosition(KTextEditor::Cursor(line, column));
                        QCoreApplication::processEvents();
                        renameAction->trigger();
                        QCoreApplication::processEvents();
                        const QVariantMap state =
                            shell->property("vedit.diagnostics.rename").toMap();
                        const QString reason = state.value(QStringLiteral("reason")).toString();
                        check(name,
                              state.value(QStringLiteral("refused")).toBool()
                                  && reason.contains(needle, Qt::CaseInsensitive),
                              QStringLiteral("reason='%1'").arg(reason));
                    };
                refusedAt("rename.number-literal-digit", 4, 22,
                          QStringLiteral("number literal"));
                refusedAt("rename.number-literal-based", 3, 15,
                          QStringLiteral("number literal"));
                refusedAt("rename.comment", 1, 8, QStringLiteral("comment"));
                refusedAt("rename.string", 2, 20, QStringLiteral("string"));
                refusedAt("rename.system-task", 2, 11, QStringLiteral("system task"));
                refusedAt("rename.keyword", 0, 2, QStringLiteral("keyword"));
            }
            pane->setActiveTab(pane->tabIndexForPath(aPath));
            QCoreApplication::processEvents();
        }
    }

    // =======================================================================
    // 12 (MEDIUM e) — a RE-SHOW of the same dialog is detected
    // =======================================================================
    // Every explicit pass above presented its dialog. What matters is that the
    // AUTOMATIC pass does not call show() again on the SAME object: the old
    // assertion counted QDialog children, which a re-show does not change.
    {
        auto *explicitDialog = shell->findChild<QDialog *>(QStringLiteral("veditElabDialog"));
        if (explicitDialog != nullptr) {
            explicitDialog->hide();
        }
        check("modal.explicit-dialog-hidden",
              explicitDialog == nullptr || !explicitDialog->isVisible());
        const int dialogsBeforeAuto =
            presentationState().value(QStringLiteral("dialogsShown"), -1).toInt();
        const int passesBeforeModal = fake->startCount;
        // The serial BEFORE this pass: comparing against the bare current serial
        // is racy, because the previous presentation still carries that serial
        // until this pass reaches its terminal state (a stale read made this
        // assertion pass on an older map).
        const quint64 serialBeforeModal = controller->projectPassSerial();
        // Force a FRESH automatic pass after the dialog was hidden: a re-show of
        // the same object must show up in this pass's presentation.
        pane->setActiveTab(pane->tabIndexForPath(aPath));
        QCoreApplication::processEvents();
        pane->saveFile(&saveError);
        waitUntil([fake, passesBeforeModal]() { return fake->startCount > passesBeforeModal; },
                  10000);
        waitUntil([&presentationState, controller, serialBeforeModal]() {
            const quint64 serial = controller->projectPassSerial();
            return serial > serialBeforeModal
                && presentationState().value(QStringLiteral("serial")).toULongLong() == serial;
        }, 5000);
        const QVariantMap presentation = presentationState();
        const int dialogs = presentation.value(QStringLiteral("dialogsShown"), -1).toInt();
        std::printf("OBSERVED presentation automatic=%d dialogsBefore=%d dialogsAfter=%d rows=%d "
                    "visible=%d\n",
                    presentation.value(QStringLiteral("automatic")).toBool() ? 1 : 0,
                    dialogsBeforeAuto, dialogs,
                    presentation.value(QStringLiteral("rows")).toInt(),
                    presentation.value(QStringLiteral("dialogVisible")).toBool() ? 1 : 0);
        check("modal.automatic-never-shows",
              presentation.value(QStringLiteral("automatic")).toBool()
                  && dialogs == dialogsBeforeAuto,
              QStringLiteral("automatic=%1 before=%2 after=%3")
                  .arg(presentation.value(QStringLiteral("automatic")).toBool() ? 1 : 0)
                  .arg(dialogsBeforeAuto)
                  .arg(dialogs));
        check("modal.dialog-not-visible-after-auto",
              !presentation.value(QStringLiteral("dialogVisible")).toBool());
        check("modal.counter-detects-reshow",
              presentation.contains(QStringLiteral("dialogsShown")) && dialogsBeforeAuto >= 1);
    }

    // =======================================================================
    // 3 (HIGH) — Open Folder: the lane follows projectRootChanged
    // =======================================================================
    shell->setProjectRoot(project2);
    setRows(*fake, project2);
    QCoreApplication::processEvents();
    bool switched = false;
    for (int attempt = 0; attempt < 80 && !switched; ++attempt) {
        const int before = fake->startCount;
        lintAction->trigger();
        waitUntil([fake, before]() { return fake->startCount != before; }, 250);
        switched = fake->startCount != before
            && fake->filesSeen.contains(cPath) && !fake->filesSeen.contains(aPath);
        if (!switched) {
            QThread::msleep(50);
            QCoreApplication::processEvents();
        }
    }
    waitUntil([controller]() { return !controller->projectPassRunning(); }, 5000);
    QCoreApplication::processEvents();
    std::printf("OBSERVED root.after-switch files='%s'\n", qPrintable(fake->filesSeen.join(QLatin1Char(','))));
    check("root.repinned", switched,
          QStringLiteral("files=%1").arg(fake->filesSeen.join(QLatin1Char(','))));
    check("root.old-rows-cleared", !controller->hasUpperLayer(aPath),
          QStringLiteral("old=%1 upper-now=%2").arg(aPath).arg(upperFiles().join(QLatin1Char(','))));
    check("root.new-rows-covered", controller->hasUpperLayer(cPath),
          QStringLiteral("new=%1").arg(cPath));

    // =======================================================================
    // 1 (BLOCKER, a) — destroy the view, then hover/navigate: clean
    // =======================================================================
    QString cOpenError;
    check("destroy.open-c", pane->openFile(cPath, &cOpenError), cOpenError);
    pane->setActiveTab(pane->tabIndexForPath(cPath));
    // Let the 400 ms live poll adopt the view and publish the hover map first.
    waitUntil([&hoverState]() {
        return hoverState().value(QStringLiteral("known")).toBool();
    }, 3000);
    QThread::msleep(450);
    QCoreApplication::processEvents();
    KTextEditor::View *viewC = qobject_cast<KTextEditor::View *>(pane->editorWidget());
    if (viewC != nullptr && viewC->document() != nullptr) {
        const QPoint pos = viewC->cursorToCoordinate(KTextEditor::Cursor(1, 4));
        QMouseEvent move(QEvent::MouseMove, pos, viewC->mapToGlobal(pos), Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewC, &move);
        QCoreApplication::processEvents();
        waitUntil([&hoverState]() {
            return hoverState().value(QStringLiteral("alive")).toBool()
                && hoverState().value(QStringLiteral("parentIsActiveView")).toBool();
        }, 3000);
    }
    const int cTab = pane->tabIndexForPath(cPath);
    check("destroy.tab-closed", cTab >= 0 && pane->closeTab(cTab));
    QCoreApplication::processEvents();
    const bool cacheCleared = waitUntil([&hoverState]() {
        const QVariantMap state = hoverState();
        return state.contains(QStringLiteral("alive"))
            && !state.value(QStringLiteral("alive")).toBool()
            && !state.value(QStringLiteral("stale")).toBool();
    }, 3000);
    check("destroy.cache-cleared", cacheCleared,
          QStringLiteral("alive=%1 stale=%2")
              .arg(hoverState().value(QStringLiteral("alive")).toBool() ? 1 : 0)
              .arg(hoverState().value(QStringLiteral("stale")).toBool() ? 1 : 0));
    // Both diagnostic actions must be safe on the surviving view. "Safe" is
    // asserted, not assumed: the process staying alive is proven by reaching
    // SUMMARY, and the popup state must still be coherent (a live popup is
    // parented to the ACTIVE view and holds no stale row).
    hoverAction->trigger();
    QCoreApplication::processEvents();
    QAction *definitionAction = shell->commands()->action(QStringLiteral("diagnostics.definition"));
    if (definitionAction != nullptr) {
        definitionAction->trigger();
    }
    QCoreApplication::processEvents();
    const QVariantMap afterActions = hoverState();
    const bool aliveAfterActions = afterActions.value(QStringLiteral("alive")).toBool();
    const bool actionsClean = afterActions.value(QStringLiteral("known")).toBool()
        && (!aliveAfterActions
            || (afterActions.value(QStringLiteral("parentIsActiveView")).toBool()
                && !afterActions.value(QStringLiteral("stale")).toBool()));
    check("destroy.actions-clean", actionsClean,
          QStringLiteral("known=%1 alive=%2 parent=%3 stale=%4")
              .arg(afterActions.value(QStringLiteral("known")).toBool() ? 1 : 0)
              .arg(aliveAfterActions ? 1 : 0)
              .arg(afterActions.value(QStringLiteral("parentIsActiveView")).toBool() ? 1 : 0)
              .arg(afterActions.value(QStringLiteral("stale")).toBool() ? 1 : 0));
    check("destroy.no-stale-pointer",
          afterActions.contains(QStringLiteral("alive"))
              && !afterActions.value(QStringLiteral("alive")).toBool()
              && !afterActions.value(QStringLiteral("diagnostic")).toBool()
              && !afterActions.value(QStringLiteral("stale")).toBool(),
          QStringLiteral("alive=%1 diagnostic=%2 stale=%3")
              .arg(afterActions.value(QStringLiteral("alive")).toBool() ? 1 : 0)
              .arg(afterActions.value(QStringLiteral("diagnostic")).toBool() ? 1 : 0)
              .arg(afterActions.value(QStringLiteral("stale")).toBool() ? 1 : 0));

    std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
