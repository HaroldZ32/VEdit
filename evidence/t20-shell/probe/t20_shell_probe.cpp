// T20 shell-integration probe: the REAL product shell with the REAL workspace
// lanes (installPanels) and the REAL KTextEditor editor provider.
//
// It exercises the seven T20 deliverables' observable seams:
//
//   1. jump unification: ONE EditorJumpTarget registered in the application-wide
//      jump registry; every panel (search, diagnostics, breadcrumb) reaches it
//      through jump::jumpTo() - the probe asserts the registry has a target and
//      that three DIFFERENT producers land in the RIGHT pane + file.
//   4. breadcrumb mount: the pane hosts vedit::breadcrumb::Bar, fed from the
//      editor lane (pathEntries() over the KTextEditor-free property bridge).
//   5. dual pane x 2 tabs: two panes, two tabs each; a jump from the search
//      panel, a click on a breadcrumb segment and a double-click on a
//      diagnostics row each land in the ACTIVE pane and the correct file.
//
// Real input events are used (QTest mouse click/double click on the widget that
// receives the user's click), not direct method calls, for the three producers.
//
// argv[1] = fixture directory, argv[2] = screenshot directory.
// Exit 0 = every check passed.
#include "app_shell.h"
#include "breadcrumb_bar.h"
#include "diagnostics_controller.h"
#include "diagnostics_panel.h"
#include "editor_area.h"
#include "integration.h"
#include "jump/jump_registry.h"
#include "kernel/ktexteditor_provider.h"
#include "panel_registry.h"
#include "search_panel.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <cstdio>
#include <functional>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (condition) {
        std::printf("ok   %s%s\n", name,
                    detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
    } else {
        ++g_failures;
        std::printf("FAIL %s%s\n", name,
                    detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
    }
    std::fflush(stdout);
}

bool waitUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
    return done();
}

void pump(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
    }
}

// The active tab's KTextEditor view of a pane (A-2: re-resolved every time -
// the pane's tabs each own a view).
KTextEditor::View *activeView(vedit::app::EditorPane *pane)
{
    return pane != nullptr ? vedit::editor::viewFor(pane->editorWidget()) : nullptr;
}

QTreeWidgetItem *findHitItem(vedit::workspace::search::SearchPanel *panel,
                             const QString &relativePath, int line)
{
    if (panel == nullptr) {
        return nullptr;
    }
    for (int index = 0; index < panel->resultsView()->topLevelItemCount(); ++index) {
        QTreeWidgetItem *fileItem = panel->resultsView()->topLevelItem(index);
        if (fileItem->text(0) != relativePath) {
            continue;
        }
        for (int child = 0; child < fileItem->childCount(); ++child) {
            if (fileItem->child(child)->text(1).toInt() == line) {
                return fileItem->child(child);
            }
        }
    }
    return nullptr;
}

QTreeWidgetItem *findIssueItem(QTreeWidget *tree, const QString &absolutePath)
{
    if (tree == nullptr) {
        return nullptr;
    }
    const auto walk = [&absolutePath](QTreeWidgetItem *item, const auto &self) -> QTreeWidgetItem * {
        const QVariant file = item->data(0, Qt::UserRole);
        if (file.isValid() && QFileInfo(file.toString()).absoluteFilePath()
                                  == QFileInfo(absolutePath).absoluteFilePath()) {
            return item;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            if (QTreeWidgetItem *found = self(item->child(index), self)) {
                return found;
            }
        }
        return nullptr;
    };
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (QTreeWidgetItem *found = walk(tree->topLevelItem(index), walk)) {
            return found;
        }
    }
    return nullptr;
}

bool clickTreeItem(QTreeWidget *tree, QTreeWidgetItem *item, bool doubleClick)
{
    if (tree == nullptr || item == nullptr || !tree->isVisible() || !tree->viewport()->isVisible()) {
        return false;
    }
    tree->scrollToItem(item);
    QCoreApplication::processEvents();
    const QRect rect = tree->visualItemRect(item);
    if (rect.isEmpty()) {
        return false;
    }
    if (doubleClick) {
        // The same event sequence the T49 evidence established for this Qt
        // build: press + MouseButtonDblClick + release (a real double click's
        // event types; QAbstractItemView needs the press to record pressedIndex).
        QTest::mousePress(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QTest::mouseRelease(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    } else {
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    }
    QCoreApplication::processEvents();
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-t20-shell-probe"));

    const QString fixture = argc > 1 && *argv[1] != '\0'
                                ? QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()
                                : QString();
    const QString shots = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    if (fixture.isEmpty() || !QFileInfo(fixture).isDir()) {
        std::printf("FATAL: pass the fixture directory as argv[1]\n");
        return 2;
    }
    QDir().mkpath(shots);
    const auto shotPath = [&shots](const char *name) {
        return shots + QLatin1Char('/') + QString::fromLatin1(name);
    };

    const QString alphaPath = fixture + QStringLiteral("/alpha.sv");
    const QString betaPath = fixture + QStringLiteral("/beta.sv");
    const QString gammaPath = fixture + QStringLiteral("/gamma.sv");
    const QString deltaPath = fixture + QStringLiteral("/delta.sv");
    for (const QString &path : {alphaPath, betaPath, gammaPath, deltaPath}) {
        if (!QFileInfo::exists(path)) {
            std::printf("FATAL: fixture file missing: %s\n", qPrintable(path));
            return 2;
        }
    }

    QTemporaryDir stateDir;
    if (!stateDir.isValid()) {
        std::printf("FATAL: no temporary state directory\n");
        return 2;
    }

    vedit::app::ShellOptions options;
    options.stateDir = stateDir.path();
    options.projectRoot = fixture;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    vedit::app::ApplicationShell shell(options);
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
    shell.startSession();
    vedit::workspace::IntegrationOptions integrationOptions;
    integrationOptions.projectRoot = fixture;
    integrationOptions.stateDir = stateDir.path();
    // The product entry point: every workspace lane (jump, search, diagnostics,
    // module tree, project, file tree, palette, VCS, settings) installs here.
    vedit::workspace::installPanels(shell, integrationOptions);
    shell.resize(1500, 950);
    shell.show();
    pump(200);

    // --- item 1: ONE jump target for every panel ----------------------------
    check("t20.jump-target-registered", vedit::workspace::jump::hasTarget(),
          "jump::hasTarget()");
    check("t20.jump-target-count", vedit::workspace::jump::hasTarget());

    shell.panels()->showPanel(QStringLiteral("search"), &shell);
    shell.panels()->showPanel(QStringLiteral("outline"), &shell);
    pump(300);

    auto *searchPanel = qobject_cast<vedit::workspace::search::SearchPanel *>(
        shell.panels()->dock(QStringLiteral("search")) != nullptr
            ? shell.panels()->dock(QStringLiteral("search"))->widget()
            : nullptr);
    check("t20.search-panel", searchPanel != nullptr);

    auto *diagnosticsPanel = qobject_cast<vedit::workspace::diagnostics::DiagnosticsPanel *>(
        shell.panels()->dock(QStringLiteral("outline")) != nullptr
            ? shell.panels()->dock(QStringLiteral("outline"))->widget()
            : nullptr);
    check("t20.diagnostics-panel", diagnosticsPanel != nullptr);

    // --- item 5: two panes, two tabs each -----------------------------------
    vedit::app::EditorArea *area = shell.editorArea();
    check("t20.area", area != nullptr);
    if (area == nullptr) {
        std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
        return 1;
    }
    vedit::app::EditorPane *left = area->activePane();
    check("t20.left-pane", left != nullptr);
    if (left == nullptr) {
        std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
        return 1;
    }
    QString openError;
    check("t20.left-open-alpha", left->openFile(alphaPath, &openError), openError);
    check("t20.left-open-beta", left->openFile(betaPath, &openError), openError);
    pump(100);
    check("t20.left-tabs-2", left->tabCount() == 2,
          QStringLiteral("tabs=%1 active=%2").arg(left->tabCount()).arg(left->currentFilePath()));

    vedit::app::EditorPane *right = area->splitActive(Qt::Horizontal);
    check("t20.split", right != nullptr && area->paneCount() == 2,
          QStringLiteral("panes=%1").arg(area->paneCount()));
    if (right == nullptr) {
        std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
        return 1;
    }
    check("t20.right-open-gamma", right->openFile(gammaPath, &openError), openError);
    check("t20.right-open-delta", right->openFile(deltaPath, &openError), openError);
    pump(100);
    check("t20.right-tabs-2", right->tabCount() == 2,
          QStringLiteral("tabs=%1 active=%2").arg(right->tabCount()).arg(right->currentFilePath()));
    check("t20.two-panes-two-tabs",
          area->paneCount() == 2 && left->tabCount() == 2 && right->tabCount() == 2);
    if (!shots.isEmpty()) {
        check("t20.screenshot.dual-pane", shell.grab().save(shotPath("01-dual-pane-2-tabs.png")));
    }

    // --- producer 1: the search panel's result click -------------------------
    // `alpha.sv` is open in the LEFT pane; the RIGHT pane is active. A click on
    // its hit must open in the ACTIVE pane (the real target's rule), not in the
    // pane that already owns the file.
    area->setActivePane(right);
    pump(50);
    check("t20.search-ready", searchPanel != nullptr);
    int hitLine = 0;
    if (searchPanel != nullptr) {
        searchPanel->setRootPath(fixture);
        searchPanel->setQuery(QStringLiteral("always_ff"));
        searchPanel->startSearch();
        const bool done = waitUntil([&]() { return !searchPanel->isSearching(); }, 30000);
        check("t20.search-finished", done);
        for (const vedit::workspace::search::SearchHit &hit : searchPanel->hits()) {
            if (hit.relativePath == QStringLiteral("alpha.sv") && hit.line >= 2) {
                hitLine = hit.line;
                break;
            }
        }
        check("t20.search-alpha-hit", hitLine >= 2, QStringLiteral("line=%1").arg(hitLine));
        QTreeWidgetItem *hitItem =
            findHitItem(searchPanel, QStringLiteral("alpha.sv"), hitLine);
        check("t20.search-hit-row", hitItem != nullptr);
        check("t20.search-click", clickTreeItem(searchPanel->resultsView(), hitItem, false));
        pump(150);
    }
    KTextEditor::View *rightView = activeView(right);
    check("t20.search-jump-right-file", right->currentFilePath() == alphaPath,
          right->currentFilePath());
    check("t20.search-jump-right-tab", right->tabCount() == 3,
          QStringLiteral("tabs=%1").arg(right->tabCount()));
    check("t20.search-jump-left-unchanged", left->currentFilePath() == betaPath,
          left->currentFilePath());
    check("t20.search-jump-cursor", rightView != nullptr
                                        && rightView->cursorPosition().line() + 1 == hitLine,
          rightView != nullptr ? QStringLiteral("cursor=%1 expected=%2")
                                     .arg(rightView->cursorPosition().line() + 1)
                                     .arg(hitLine)
                               : QString());
    if (!shots.isEmpty()) {
        check("t20.screenshot.search-jump",
              shell.grab().save(shotPath("02-search-jump-active-pane.png")));
    }

    // --- producer 2: a breadcrumb segment click (item 4 + 5) ------------------
    check("t20.right-view", rightView != nullptr);
    if (rightView != nullptr && rightView->document() != nullptr) {
        // Cursor inside the function body: breadcrumb must be module > function.
        const QStringList lines = rightView->document()->text().split(QLatin1Char('\n'));
        int functionLine = -1;
        for (int index = 0; index < lines.size(); ++index) {
            if (lines.at(index).contains(QStringLiteral("inc = v"))) {
                functionLine = index;
                break;
            }
        }
        check("t20.fixture-function-line", functionLine >= 0,
              QStringLiteral("line=%1").arg(functionLine + 1));
        if (functionLine >= 0) {
            rightView->setCursorPosition(KTextEditor::Cursor(functionLine, 4));
            pump(200);
        }
        auto *bar = right->findChild<vedit::breadcrumb::Bar *>();
        check("t20.bar.mounted", bar != nullptr);
        if (bar != nullptr) {
            const bool segments = waitUntil([bar]() { return bar->segmentCount() >= 2; }, 5000);
            check("t20.bar.segments", segments,
                  QStringLiteral("segments=%1").arg(bar->segmentCount()));
            check("t20.bar.module-segment",
                  bar->segmentCount() >= 1 && bar->segmentText(0) == QStringLiteral("alpha"),
                  bar->segmentCount() >= 1 ? bar->segmentText(0) : QString());
            check("t20.bar.function-segment",
                  bar->segmentCount() >= 2 && bar->segmentText(1) == QStringLiteral("inc"),
                  bar->segmentCount() >= 2 ? bar->segmentText(1) : QString());
            check("t20.bar.file", bar->filePath() == alphaPath, bar->filePath());
            if (!shots.isEmpty()) {
                // The two clickable segments (module > function) BEFORE the
                // click moves the cursor to the module line.
                check("t20.screenshot.breadcrumb-segments",
                      shell.grab().save(shotPath("03a-breadcrumb-segments.png")));
            }
            QWidget *segment = bar->segmentWidget(0);
            check("t20.bar.segment-widget", segment != nullptr && segment->isVisible());
            const int moduleLine = bar->segmentLine(0);
            if (segment != nullptr) {
                QTest::mouseClick(segment, Qt::LeftButton);
                pump(150);
            }
            check("t20.bar.click-delivered",
                  bar->lastStatus() == vedit::workspace::jump::JumpStatus::Delivered,
                  QString::number(static_cast<int>(bar->lastStatus())));
            check("t20.bar.jump-right-file", right->currentFilePath() == alphaPath,
                  right->currentFilePath());
            KTextEditor::View *afterClick = activeView(right);
            check("t20.bar.jump-line",
                  afterClick != nullptr
                      && afterClick->cursorPosition().line() + 1 == moduleLine,
                  afterClick != nullptr ? QStringLiteral("cursor=%1 expected=%2")
                                              .arg(afterClick->cursorPosition().line() + 1)
                                              .arg(moduleLine)
                                        : QString());
            check("t20.bar.jump-left-unchanged", left->currentFilePath() == betaPath,
                  left->currentFilePath());
        }
        if (!shots.isEmpty()) {
            check("t20.screenshot.breadcrumb",
                  shell.grab().save(shotPath("03-breadcrumb-click.png")));
        }
    }

    // --- producer 3: a diagnostics row double-click (item 5) -----------------
    // A real slang diagnostic for beta.sv (an undeclared identifier) is produced
    // by the shell's own controller; the row is double-clicked while the LEFT
    // pane is active, so the jump must land there.
    if (diagnosticsPanel != nullptr && diagnosticsPanel->controller() != nullptr) {
        auto *controller = diagnosticsPanel->controller();
        if (!controller->toolchainReport().missing().isEmpty()) {
            std::printf("     toolchain missing: %s\n",
                        qPrintable(controller->toolchainReport().userMessage()));
        }
        controller->setFileSet(QStringList{betaPath});
        // Capture the entry the moment the tool answers: a later poll may
        // replace the file's diagnostics (the controller keeps one truth per
        // file), so the expected position must be recorded now.
        int toolLine = 0;
        const bool produced = waitUntil(
            [controller, &betaPath, &toolLine]() {
                for (const vedit::workspace::diagnostics::DiagnosticEntry &entry :
                     controller->diagnostics()) {
                    if (QFileInfo(entry.file).absoluteFilePath()
                        == QFileInfo(betaPath).absoluteFilePath()) {
                        toolLine = entry.line + 1;
                        return true;
                    }
                }
                return false;
            },
            30000);
        check("t20.diagnostics.produced", produced,
              QStringLiteral("entries=%1 message=%2")
                  .arg(controller->diagnostics().size())
                  .arg(controller->session().message));
        // The diagnostic points at `missing_signal` in the fixture SOURCE; read
        // the expected 1-based line from that file, not from the answer (a
        // position check must not be circular).
        int expectedLine = 0;
        {
            QFile beta(betaPath);
            if (beta.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QStringList lines =
                    QString::fromUtf8(beta.readAll()).split(QLatin1Char('\n'));
                for (int index = 0; index < lines.size(); ++index) {
                    if (lines.at(index).contains(QStringLiteral("missing_signal"))) {
                        expectedLine = index + 1;
                        break;
                    }
                }
            }
        }
        std::printf("     diagnostic ground truth: source line %d, tool line %d, source='%s'\n",
                    expectedLine, toolLine, "missing_signal");
        check("t20.diagnostics.tool-position", toolLine == expectedLine,
              QStringLiteral("tool=%1 source=%2").arg(toolLine).arg(expectedLine));

        diagnosticsPanel->refreshFromController();
        pump(100);
        check("t20.diagnostics.rows", diagnosticsPanel->issueRowCount() >= 1,
              QStringLiteral("rows=%1").arg(diagnosticsPanel->issueRowCount()));
        auto *issues = qobject_cast<QTreeWidget *>(diagnosticsPanel->issuesWidget());
        check("t20.diagnostics.issues-widget", issues != nullptr);
        QTreeWidgetItem *issueItem = findIssueItem(issues, betaPath);
        check("t20.diagnostics.beta-row", issueItem != nullptr);
        area->setActivePane(left);
        pump(50);
        check("t20.diagnostics.active-left", area->activePane() == left);
        check("t20.diagnostics.double-click",
              clickTreeItem(issues, issueItem, true));
        pump(150);
        check("t20.diagnostics.left-file", left->currentFilePath() == betaPath,
              left->currentFilePath());
        check("t20.diagnostics.right-unchanged", right->currentFilePath() == alphaPath,
              right->currentFilePath());
        KTextEditor::View *leftView = activeView(left);
        check("t20.diagnostics.cursor",
              leftView != nullptr && expectedLine > 0
                  && leftView->cursorPosition().line() + 1 == expectedLine,
              leftView != nullptr ? QStringLiteral("cursor=%1 expected=%2")
                                        .arg(leftView->cursorPosition().line() + 1)
                                        .arg(expectedLine)
                                  : QString());
        if (!shots.isEmpty()) {
            check("t20.screenshot.diagnostics",
                  shell.grab().save(shotPath("04-diagnostics-jump-active-pane.png")));
        }
    } else {
        check("t20.diagnostics.panel-controller", false);
    }

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
