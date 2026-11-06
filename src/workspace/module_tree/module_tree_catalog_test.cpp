// Offscreen acceptance test for UX batch 2 lane B1 (items 1 + 2).
//
// It drives the REAL product path: the lane's asynchronous `vd_catalog_*` client
// runs the REAL slang binary against the lane fixture, the panels are the ones
// `installModuleTree()` registers, and every jump is delivered through the
// application-wide `jump::jumpTo()` registry. When the product jump target is
// linked (the search lane owns it in this tree), the cursor is verified inside a
// real KTextEditor document; otherwise the test falls back to recording the
// request and says so.
//
// Acceptance items (the numbers are the plan's §B1 acceptance list):
//   (i)   root set == the set of never-instantiated (`is_top`) modules;
//   (ii)  a module instantiated by two parents appears twice;
//   (iii) double-clicking a module row lands the cursor on its declaration;
//   (iv)  no row represents a net/driver/load;
//   (v)   the interface panel shows the correct direction/type/width;
//   (vi)  port double-click -> declaration, Ctrl+double-click -> instantiation.
//
// Usage: vedit-module-tree-catalog-test <fixture-dir> <slang-binary> [screenshot.png]
// The screenshot is taken only on a real xcb/Xvfb display; on offscreen the test
// prints an explicit SKIP naming the reason instead.
#include "catalog_client.h"
#include "interface_panel.h"
#include "module_catalog.h"
#include "module_hierarchy.h"
#include "module_tree_integration.h"
#include "module_tree_panel.h"

#include "app_shell.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "jump/jump_registry.h"
#include "panel_registry.h"
#include "project/project_state.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStringList>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

#if defined(VEDIT_MODULE_TREE_HAVE_PRODUCT_JUMP)
#include "search_integration.h"
#endif

#if defined(VEDIT_MODULE_TREE_HAVE_EDITOR_CORE)
#include "kernel/ktexteditor_provider.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

namespace {

using namespace vedit::workspace::module_tree;
namespace jump = vedit::workspace::jump;

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

void skip(const char *name, const QString &reason)
{
    std::printf("SKIP %s — %s\n", name, reason.toStdString().c_str());
    std::fflush(stdout);
}

bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 60000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QTest::qWait(25);
    }
    return true;
}

/// Fallback target for a build without the product jump target: it records the
/// request so the acceptance checks still prove the SEAM, and the log marks the
/// cursor-landing checks as skipped.
class RecordingTarget final : public jump::IJumpTarget {
public:
    bool jumpTo(const QString &filePath, int line, int column) override
    {
        ++calls;
        lastFile = filePath;
        lastLine = line;
        lastColumn = column;
        return true;
    }

    int calls = 0;
    QString lastFile;
    int lastLine = 0;
    int lastColumn = 0;
};

QTreeWidgetItem *findItem(QTreeWidgetItem *item, const QString &text)
{
    if (item->text(0) == text) {
        return item;
    }
    for (int index = 0; index < item->childCount(); ++index) {
        if (QTreeWidgetItem *found = findItem(item->child(index), text)) {
            return found;
        }
    }
    return nullptr;
}

QTreeWidgetItem *findItem(QTreeWidget *tree, const QString &text)
{
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (QTreeWidgetItem *found = findItem(tree->topLevelItem(index), text)) {
            return found;
        }
    }
    return nullptr;
}

int countItems(QTreeWidgetItem *item)
{
    int total = 1;
    for (int index = 0; index < item->childCount(); ++index) {
        total += countItems(item->child(index));
    }
    return total;
}

QString cell(QTableWidget *table, int row, int column)
{
    QTableWidgetItem *item = table->item(row, column);
    return item != nullptr ? item->text() : QString();
}

void doubleClick(QWidget *viewport, const QRect &rect, Qt::KeyboardModifiers modifiers)
{
    // A tree row rect spans every column, so its centre can lie outside a narrow
    // dock's viewport; clamp to a point that is still inside the row and the
    // widget (column 0 is stretch, so x near the middle still hits the item).
    QPoint center = rect.center();
    center.setX(std::max(1, std::min(center.x(), viewport->width() - 2)));
    center.setY(std::max(1, std::min(center.y(), viewport->height() - 2)));
    QTest::mousePress(viewport, Qt::LeftButton, modifiers, center);
    QTest::mouseDClick(viewport, Qt::LeftButton, modifiers, center);
    QTest::mouseRelease(viewport, Qt::LeftButton, modifiers, center);
    QCoreApplication::processEvents();
}

QString oneLine(const QString &text)
{
    QString copy = text;
    copy.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return copy.simplified();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const QString fixture = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QString slang = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    const QString screenshot = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();

    if (!QFileInfo(fixture).isDir() || !QFileInfo(slang).isExecutable()) {
        // A missing fixture/slang used to print SKIP and exit 0, so ctest reported
        // "Passed" with ZERO checks (review M3). This test is registered only in
        // the product build, where the fixture is in-tree and slang is a build
        // dependency, so a missing one is a real failure - never a green gate.
        std::printf("FAIL catalog.real — the lane fixture and a built slang are required "
                    "(fixture='%s' slang='%s'); run bash build/slang/build.sh\n",
                    fixture.toStdString().c_str(), slang.toStdString().c_str());
        std::printf("FAIL: 0 check(s), 1 failure(s)\n");
        std::fflush(stdout);
        return 1;
    }
    check("fixture.present", true, fixture);
    check("slang.present", true, slang);
    // The lane resolves VEDIT_SLANG first (moduleTreeSlangBinary), exactly like a
    // user override; the client then hands the path to the ABI.
    qputenv("VEDIT_SLANG", slang.toLocal8Bit());

    QTemporaryDir stateDir;
    check("state.temp-dir", stateDir.isValid());
    if (!stateDir.isValid()) {
        return 1;
    }

    // --- install the PRODUCT pieces -----------------------------------------
    vedit::app::ShellOptions shellOptions;
    shellOptions.stateDir = stateDir.path();
    shellOptions.projectRoot = fixture;
    shellOptions.restoreSession = false;
    shellOptions.showDefaultPanels = false;
    std::unique_ptr<vedit::app::ApplicationShell> shell(
        new vedit::app::ApplicationShell(shellOptions));
#if defined(VEDIT_MODULE_TREE_HAVE_EDITOR_CORE)
    shell->setEditorProvider(vedit::editor::makeEditCoreProvider());
#endif
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = fixture;
    integration.stateDir = stateDir.path();

#if defined(VEDIT_MODULE_TREE_HAVE_PRODUCT_JUMP)
    // The product's ONE jump target is owned by the search lane (the shell has no
    // registration point of its own); installing it here is what the application
    // does in src/workspace/integration.cpp.
    vedit::workspace::installSearch(*shell, integration);
    check("jump.product-target", jump::hasTarget());
#else
    RecordingTarget fallbackTarget;
    jump::setTarget(&fallbackTarget);
    skip("jump.product-target", QStringLiteral("the search lane's EditorJumpTarget is not part "
                                               "of this build; using a recording target"));
#endif

    vedit::workspace::installModuleTree(*shell, integration);
    shell->panels()->showPanel(QStringLiteral("module-tree"), shell.get());
    shell->panels()->showPanel(QStringLiteral("interface-panel"), shell.get());
    if (QDockWidget *dock = shell->panels()->dock(QStringLiteral("module-tree"))) {
        shell->resizeDocks({dock}, {440}, Qt::Horizontal);
        shell->resizeDocks({dock}, {430}, Qt::Vertical);
    }
    if (QDockWidget *dock = shell->panels()->dock(QStringLiteral("interface-panel"))) {
        shell->resizeDocks({dock}, {260}, Qt::Vertical);
    }
    shell->resize(1400, 900);
    shell->show();
    QCoreApplication::processEvents();

    ModuleTreePanel *treePanel = shell->findChild<ModuleTreePanel *>();
    InterfacePanel *interfacePanel = shell->findChild<InterfacePanel *>();
    vedit::app::EditorPane *pane = shell->editorArea() != nullptr
                                       ? shell->editorArea()->activePane()
                                       : nullptr;
    check("shell.panels-present", treePanel != nullptr && interfacePanel != nullptr,
          QStringLiteral("tree=%1 interface=%2")
              .arg(treePanel != nullptr)
              .arg(interfacePanel != nullptr));
    check("shell.two-docks",
          shell->panels()->dock(QStringLiteral("module-tree")) != nullptr
              && shell->panels()->dock(QStringLiteral("interface-panel")) != nullptr);
    check("shell.pane-present", pane != nullptr);
    if (treePanel == nullptr || interfacePanel == nullptr || pane == nullptr) {
        return 1;
    }

    // --- the REAL catalog run ------------------------------------------------
    const bool answered = waitUntil([treePanel]() {
        return treePanel->catalog().valid || !treePanel->catalog().error.isEmpty();
    });
    const ModuleCatalog catalog = treePanel->catalog();
    check("catalog.answered", answered, oneLine(catalog.error));
    check("catalog.valid", catalog.valid, oneLine(catalog.error));
    if (!catalog.valid) {
        std::printf("FAIL: the real catalog run did not answer; see the error above\n");
        return 1;
    }
    QStringList names;
    for (const ModuleDefinition &module : catalog.modules) {
        names.append(module.name);
    }
    check("catalog.module-count", catalog.modules.size() == 6,
          QStringLiteral("modules=[%1]").arg(names.join(QStringLiteral(", "))));
    check("catalog.sorted-and-interface-excluded",
          names == QStringList({QStringLiteral("consumer"), QStringLiteral("orphan"),
                                QStringLiteral("reflink"), QStringLiteral("shared"),
                                QStringLiteral("top_a"), QStringLiteral("top_b")}),
          names.join(QStringLiteral(", ")));
    check("catalog.no-interface-module", catalog.find(QStringLiteral("bus_if")) == nullptr);

    // --- H1 regression A: the project model's FIRST appearance is used ------
    // The state directory starts empty (the normal first open: the project lane
    // saves its model asynchronously), so the catalog above came from the
    // root-scan fallback. The test now writes `project-model.json` with an
    // effective file set that DIFFERS from the root scan (orphan.sv is left out)
    // and asserts the tree converges to it. The pre-repair holder recorded the
    // model stamp BEFORE its `first || isRunning` guard, so the first-ever
    // observation was consumed and no re-run happened.
    CatalogClient *catalogClient = shell->findChild<CatalogClient *>();
    QObject *holder = shell->findChild<QObject *>(QStringLiteral("module-tree-holder"));
    check("h1.probe.client-found", catalogClient != nullptr);
    check("h1.probe.holder-found", holder != nullptr);
    const auto writeProjectModel = [&fixture, &stateDir](const QStringList &names) {
        vedit::workspace::project::ProjectState state;
        state.root = fixture;
        state.capturedAt = vedit::workspace::project::ProjectState::nowUtc();
        for (const QString &name : names) {
            state.files.append(QDir(fixture).absoluteFilePath(name));
        }
        QString error;
        const bool saved =
            vedit::workspace::project::ProjectStateStore(stateDir.path()).save(state, &error);
        return saved ? QString() : error;
    };
    const QStringList withoutOrphan = {QStringLiteral("bus.sv"), QStringLiteral("consumer.sv"),
                                       QStringLiteral("reflink.sv"), QStringLiteral("shared.sv"),
                                       QStringLiteral("top_a.sv"), QStringLiteral("top_b.sv")};
    const QStringList allSources = withoutOrphan + QStringList{QStringLiteral("orphan.sv")};
    if (catalogClient != nullptr && holder != nullptr) {
        const int runsBefore = holder->property("catalogRuns").toInt();
        const QString seedError = writeProjectModel(withoutOrphan);
        check("h1.first-appearance.model-written", seedError.isEmpty(), seedError);
        // The 1 s watch finds it too; invoking the check makes the regression
        // deterministic instead of timing-dependent.
        QMetaObject::invokeMethod(holder, "checkProjectModelNow");
        const bool applied = waitUntil(
            [treePanel]() { return treePanel->catalog().fileSetSize == 6; }, 20000);
        const ModuleCatalog seeded = treePanel->catalog();
        check("h1.first-appearance.file-set", applied,
              QStringLiteral("fileSet=%1 modules=%2")
                  .arg(seeded.fileSetSize)
                  .arg(seeded.modules.size()));
        check("h1.first-appearance.uses-seeded-set",
              seeded.valid && seeded.find(QStringLiteral("orphan")) == nullptr
                  && seeded.find(QStringLiteral("consumer")) != nullptr,
              QStringLiteral("modules=%1").arg(seeded.modules.size()));
        check("h1.first-appearance.source-is-model",
              catalogClient->sourceDescription().contains(QStringLiteral("project model")),
              catalogClient->sourceDescription());
        check("h1.first-appearance.rerun",
              holder->property("catalogRuns").toInt() > runsBefore,
              QStringLiteral("runs %1 -> %2")
                  .arg(runsBefore)
                  .arg(holder->property("catalogRuns").toInt()));
    }

    // --- H1 regression B: a model written while a run is IN FLIGHT ---------
    // Start a real catalog run, write the model that differs, and observe it
    // while that run is still running. The holder must mark the model dirty and
    // run ONCE more when the in-flight run finishes; the pre-repair code
    // recorded the stamp before the `isRunning()` return and swallowed it.
    if (catalogClient != nullptr && holder != nullptr) {
        const int runsBefore = holder->property("catalogRuns").toInt();
        QMetaObject::invokeMethod(holder, "refreshNow");
        check("h1.in-flight.run-started", catalogClient->isRunning());
        const QString allError = writeProjectModel(allSources);
        check("h1.in-flight.model-written", allError.isEmpty(), allError);
        QMetaObject::invokeMethod(holder, "checkProjectModelNow");
        check("h1.in-flight.pending-observed", holder->property("modelPending").toBool(),
              QStringLiteral("modelPending=%1")
                  .arg(holder->property("modelPending").toBool()));
        const bool converged = waitUntil(
            [treePanel, holder, runsBefore]() {
                const ModuleCatalog current = treePanel->catalog();
                // The pre-existing 7-file catalog also satisfies the file-set
                // condition, so require the state to come from run #2 or later:
                // run #2 read the seeded 6-file model, the follow-up run #3 reads
                // the model written mid-flight.
                return holder->property("catalogGeneration").toInt() >= runsBefore + 2
                       && current.fileSetSize == 7
                       && current.find(QStringLiteral("orphan")) != nullptr;
            },
            30000);
        const ModuleCatalog followed = treePanel->catalog();
        check("h1.in-flight.final-file-set", converged,
              QStringLiteral("fileSet=%1 modules=%2 generation=%3 runs=%4")
                  .arg(followed.fileSetSize)
                  .arg(followed.modules.size())
                  .arg(holder->property("catalogGeneration").toInt())
                  .arg(holder->property("catalogRuns").toInt()));
        check("h1.in-flight.final-tree-reflects-model",
              followed.valid && followed.find(QStringLiteral("orphan")) != nullptr
                  && followed.modules.size() == 6,
              QStringLiteral("modules=%1").arg(followed.modules.size()));
        check("h1.in-flight.follow-up-run",
              holder->property("catalogRuns").toInt() >= runsBefore + 2,
              QStringLiteral("runs %1 -> %2")
                  .arg(runsBefore)
                  .arg(holder->property("catalogRuns").toInt()));
        // The follow-up must be ONE run, not a loop: with the model unchanged,
        // two 1 s watch ticks must not start another catalog run.
        const int settledRuns = holder->property("catalogRuns").toInt();
        QTest::qWait(1600);
        QCoreApplication::processEvents();
        check("h1.in-flight.settles",
              holder->property("catalogRuns").toInt() == settledRuns
                  && holder->property("modelPending").toBool() == false,
              QStringLiteral("runs %1 -> %2 pending=%3")
                  .arg(settledRuns)
                  .arg(holder->property("catalogRuns").toInt())
                  .arg(holder->property("modelPending").toBool()));
    }

    const QVector<ModuleTreeNode> roots = treePanel->roots();
    QStringList rootNames;
    for (const ModuleTreeNode &root : roots) {
        rootNames.append(root.moduleName);
    }

    // (i) root set == the never-instantiated modules (AND the fixture's known set)
    check("acceptance.i.roots-equal-is-top", rootNames == catalog.topModuleNames(),
          QStringLiteral("roots=[%1] tops=[%2]")
              .arg(rootNames.join(QStringLiteral(", ")),
                   catalog.topModuleNames().join(QStringLiteral(", "))));
    check("acceptance.i.roots-fixture-set",
          rootNames == QStringList({QStringLiteral("orphan"), QStringLiteral("top_a"),
                                    QStringLiteral("top_b")}),
          rootNames.join(QStringLiteral(", ")));

    // (ii) two parents -> the same module appears twice with distinct paths
    QStringList sharedPaths;
    for (const ModuleTreeNode *node : flattenModuleTree(roots)) {
        if (node->kind == ModuleRowKind::Module && node->moduleName == QLatin1String("shared")) {
            sharedPaths.append(node->instancePath);
        }
    }
    check("acceptance.ii.shared-under-both-parents", sharedPaths.size() == 2,
          sharedPaths.join(QStringLiteral(", ")));
    check("acceptance.ii.distinct-instance-paths",
          sharedPaths == QStringList({QStringLiteral("top_a.u_shared"),
                                      QStringLiteral("top_b.u_shared")}),
          sharedPaths.join(QStringLiteral(", ")));

    // (iv) no net/driver/load row: every row is a module occurrence and the row
    // count is exactly the module tree's row count (7 module rows for this
    // fixture). The removed net names must not appear as rows.
    const QVector<const ModuleTreeNode *> flat = flattenModuleTree(roots);
    bool onlyModules = true;
    for (const ModuleTreeNode *node : flat) {
        onlyModules = onlyModules && node->kind == ModuleRowKind::Module;
    }
    check("acceptance.iv.only-module-rows", onlyModules,
          QStringLiteral("rows=%1").arg(flat.size()));
    check("acceptance.iv.row-count", countModuleTreeRows(roots) == 7,
          QStringLiteral("rows=%1").arg(countModuleTreeRows(roots)));
    int widgetRows = 0;
    for (int index = 0; index < treePanel->tree()->topLevelItemCount(); ++index) {
        widgetRows += countItems(treePanel->tree()->topLevelItem(index));
    }
    check("acceptance.iv.widget-row-count", widgetRows == 7,
          QStringLiteral("rows=%1").arg(widgetRows));
    QStringList rowLabels;
    for (const ModuleTreeNode *node : flat) {
        rowLabels.append(node->moduleName);
    }
    const QStringList netNames = {QStringLiteral("link"), QStringLiteral("clk"),
                                  QStringLiteral("req"), QStringLiteral("net"),
                                  QStringLiteral("driver"), QStringLiteral("load")};
    bool netFree = true;
    for (const QString &net : netNames) {
        netFree = netFree && !rowLabels.contains(net);
    }
    check("acceptance.iv.no-net-labels", netFree, rowLabels.join(QStringLiteral(", ")));

    // (v) the interface panel for a KNOWN module
    QTreeWidgetItem *topARow = findItem(treePanel->tree(), QStringLiteral("top_a"));
    check("panel.top-a-row", topARow != nullptr);
    if (topARow == nullptr) {
        return 1;
    }
    treePanel->tree()->setCurrentItem(topARow);
    QCoreApplication::processEvents();
    check("acceptance.v.focus-from-tree",
          interfacePanel->focusedModuleName() == QLatin1String("top_a"),
          interfacePanel->focusedModuleName());
    check("acceptance.v.port-count", interfacePanel->table()->rowCount() == 3,
          QStringLiteral("rows=%1").arg(interfacePanel->table()->rowCount()));
    check("acceptance.v.port-clk",
          cell(interfacePanel->table(), 0, 0) == QLatin1String("clk")
              && cell(interfacePanel->table(), 0, 1) == QLatin1String("In")
              && cell(interfacePanel->table(), 0, 2) == QLatin1String("logic")
              && cell(interfacePanel->table(), 0, 3) == QLatin1String("1"),
          QStringLiteral("%1 / %2 / %3 / %4")
              .arg(cell(interfacePanel->table(), 0, 0), cell(interfacePanel->table(), 0, 1),
                   cell(interfacePanel->table(), 0, 2), cell(interfacePanel->table(), 0, 3)));
    check("acceptance.v.port-pad",
          cell(interfacePanel->table(), 1, 0) == QLatin1String("pad")
              && cell(interfacePanel->table(), 1, 1) == QLatin1String("InOut")
              && cell(interfacePanel->table(), 1, 3) == QLatin1String("1"),
          QStringLiteral("%1 / %2 / %3")
              .arg(cell(interfacePanel->table(), 1, 0), cell(interfacePanel->table(), 1, 1),
                   cell(interfacePanel->table(), 1, 3)));
    check("acceptance.v.port-y",
          cell(interfacePanel->table(), 2, 0) == QLatin1String("y")
              && cell(interfacePanel->table(), 2, 1) == QLatin1String("Out")
              && cell(interfacePanel->table(), 2, 2) == QLatin1String("logic[3:0]")
              && cell(interfacePanel->table(), 2, 3) == QLatin1String("4"),
          QStringLiteral("%1 / %2 / %3 / %4")
              .arg(cell(interfacePanel->table(), 2, 0), cell(interfacePanel->table(), 2, 1),
                   cell(interfacePanel->table(), 2, 2), cell(interfacePanel->table(), 2, 3)));
    check("acceptance.v.location-column", !cell(interfacePanel->table(), 0, 4).isEmpty(),
          cell(interfacePanel->table(), 0, 4));
    check("acceptance.v.abi-limit-tooltip",
          interfacePanel->table()->toolTip().contains(QStringLiteral("per-connection")),
          interfacePanel->table()->toolTip());

    // --- (iii) double-click a module -> cursor on the declaration -----------
    const ModuleDefinition *topA = catalog.find(QStringLiteral("top_a"));
    check("acceptance.iii.top-a-catalogued", topA != nullptr);
    if (topA != nullptr) {
        treePanel->tree()->scrollToItem(topARow);
        const QRect rect = treePanel->tree()->visualItemRect(topARow);
        check("acceptance.iii.row-visible", rect.isValid() && !rect.isEmpty());
        if (rect.isValid()) {
            doubleClick(treePanel->tree()->viewport(), rect, Qt::NoModifier);
        }
        const QString expected = QFileInfo(topA->file).absoluteFilePath();
        const bool opened = waitUntil([pane, expected]() {
            return QFileInfo(pane->currentFilePath()).absoluteFilePath() == expected;
        }, 15000);
        check("acceptance.iii.file-opened", opened, pane->currentFilePath());
#if defined(VEDIT_MODULE_TREE_HAVE_PRODUCT_JUMP) && defined(VEDIT_MODULE_TREE_HAVE_EDITOR_CORE)
        KTextEditor::View *view = vedit::editor::viewFor(pane->editorWidget());
        KTextEditor::Document *document = view != nullptr ? view->document() : nullptr;
        check("acceptance.iii.ktexteditor-view", view != nullptr && document != nullptr);
        if (view != nullptr && document != nullptr) {
            const int line0 = view->cursorPosition().line();
            const int column0 = view->cursorPosition().column();
            check("acceptance.iii.cursor-on-declaration",
                  line0 == topA->line && column0 == topA->column,
                  QStringLiteral("cursor=%1:%2 expected=%3:%4")
                      .arg(line0 + 1)
                      .arg(column0 + 1)
                      .arg(topA->line + 1)
                      .arg(topA->column + 1));
            check("acceptance.iii.declaration-text",
                  line0 >= 0 && line0 < document->lines()
                      && document->line(line0).contains(QStringLiteral("module top_a")),
                  line0 >= 0 ? document->line(line0) : QString());
        }
#else
        skip("acceptance.iii.cursor-on-declaration",
             QStringLiteral("no KTextEditor provider / product jump target in this build"));
#endif
    }

    // --- the HOLDER's focus wiring (deliverable 3) --------------------------
    // Rule (b) through the product integration: with no tree selection, the
    // interface panel follows the module enclosing the caret. The holder polls
    // the active editor, so this also exercises the editor/pane-switch refresh.
#if defined(VEDIT_MODULE_TREE_HAVE_PRODUCT_JUMP) && defined(VEDIT_MODULE_TREE_HAVE_EDITOR_CORE)
    {
        KTextEditor::View *caretView = vedit::editor::viewFor(pane->editorWidget());
        check("acceptance.focus.caret-view", caretView != nullptr);
        if (caretView != nullptr) {
            treePanel->tree()->setCurrentItem(nullptr);
            treePanel->tree()->clearSelection();
            QCoreApplication::processEvents();
            // top_a.sv line 4 (1-based) is inside top_a's body, i.e. at or below
            // its definition line.
            caretView->setCursorPosition(KTextEditor::Cursor(3, 0));
            QTest::qWait(400);
            QCoreApplication::processEvents();
            check("acceptance.focus.caret-module",
                  interfacePanel->focusedModuleName() == QLatin1String("top_a"),
                  interfacePanel->focusedModuleName());
            if (QTreeWidgetItem *shared = findItem(treePanel->tree(),
                                                   QStringLiteral("shared"))) {
                treePanel->tree()->setCurrentItem(shared);
                QCoreApplication::processEvents();
                check("acceptance.focus.selection-beats-caret",
                      interfacePanel->focusedModuleName() == QLatin1String("shared"),
                      interfacePanel->focusedModuleName());
            }
            // Re-find the row: a catalog answer rebuilds the tree, so an item
            // pointer captured before a wait can be dangling.
            treePanel->tree()->setCurrentItem(findItem(treePanel->tree(),
                                                       QStringLiteral("top_a")));
            QCoreApplication::processEvents();
        }
    }
#endif

    // --- (vi) port double-click -> declaration ------------------------------
    check("acceptance.vi.focus-top-a",
          interfacePanel->focusedModuleName() == QLatin1String("top_a"),
          interfacePanel->focusedModuleName());
    const ModuleDefinition *topAAgain = catalog.find(QStringLiteral("top_a"));
    if (topAAgain != nullptr && topAAgain->ports.size() >= 3) {
        const ModulePort &y = topAAgain->ports.at(2);
        interfacePanel->table()->scrollToItem(interfacePanel->table()->item(2, 0));
        const QRect portRect = interfacePanel->table()->visualItemRect(
            interfacePanel->table()->item(2, 0));
        check("acceptance.vi.port-row-visible", portRect.isValid() && !portRect.isEmpty());
        if (portRect.isValid()) {
            doubleClick(interfacePanel->table()->viewport(), portRect, Qt::NoModifier);
        }
        const bool moved = waitUntil([pane]() { return !pane->currentFilePath().isEmpty(); }, 10000);
        check("acceptance.vi.declaration-file", moved && QFileInfo(pane->currentFilePath())
                                                          .absoluteFilePath()
                                                      == QFileInfo(y.file).absoluteFilePath(),
              pane->currentFilePath());
#if defined(VEDIT_MODULE_TREE_HAVE_PRODUCT_JUMP) && defined(VEDIT_MODULE_TREE_HAVE_EDITOR_CORE)
        KTextEditor::View *view = vedit::editor::viewFor(pane->editorWidget());
        KTextEditor::Document *document = view != nullptr ? view->document() : nullptr;
        check("acceptance.vi.declaration-cursor",
              view != nullptr && document != nullptr
                  && view->cursorPosition().line() == y.line
                  && view->cursorPosition().column() == y.column
                  && document->line(y.line).contains(QStringLiteral("y")),
              view != nullptr ? QStringLiteral("cursor=%1:%2 expected=%3:%4")
                                    .arg(view->cursorPosition().line() + 1)
                                    .arg(view->cursorPosition().column() + 1)
                                    .arg(y.line + 1)
                                    .arg(y.column + 1)
                              : QStringLiteral("no view"));
#else
        skip("acceptance.vi.declaration-cursor",
             QStringLiteral("no KTextEditor provider / product jump target in this build"));
#endif
    }

    // --- (vi) Ctrl+double-click -> the instantiation (chooser for 2 parents) -
    ModuleInstance sharedInTopB;
    const ModuleDefinition *topB = catalog.find(QStringLiteral("top_b"));
    if (topB != nullptr) {
        for (const ModuleInstance &instance : topB->children) {
            if (instance.module == QLatin1String("shared")) {
                sharedInTopB = instance;
            }
        }
    }
    check("acceptance.vi.top-b-instance-found", !sharedInTopB.file.isEmpty(),
          sharedInTopB.path);
    QTreeWidgetItem *sharedRow = findItem(treePanel->tree(), QStringLiteral("shared"));
    check("acceptance.vi.shared-row", sharedRow != nullptr);
    if (sharedRow != nullptr) {
        treePanel->tree()->setCurrentItem(sharedRow);
        QCoreApplication::processEvents();
        check("acceptance.vi.focus-shared",
              interfacePanel->focusedModuleName() == QLatin1String("shared"),
              interfacePanel->focusedModuleName());
        QVector<InstantiationSite> seen;
        interfacePanel->setInstantiationChooser(
            [&seen](const QVector<InstantiationSite> &candidates) {
                seen = candidates;
                for (int index = 0; index < candidates.size(); ++index) {
                    if (candidates.at(index).parentModule == QLatin1String("top_b")) {
                        return index;
                    }
                }
                return -1;
            });
        interfacePanel->table()->scrollToItem(interfacePanel->table()->item(0, 0));
        const QRect instanceRect = interfacePanel->table()->visualItemRect(
            interfacePanel->table()->item(0, 0));
        check("acceptance.vi.shared-port-visible", instanceRect.isValid() && !instanceRect.isEmpty());
        if (instanceRect.isValid()) {
            doubleClick(interfacePanel->table()->viewport(), instanceRect, Qt::ControlModifier);
        }
        check("acceptance.vi.chooser-two-parents", seen.size() == 2,
              QStringLiteral("candidates=%1").arg(seen.size()));
        check("acceptance.vi.chooser-parents",
              seen.size() == 2 && seen.at(0).parentModule == QLatin1String("top_a")
                  && seen.at(1).parentModule == QLatin1String("top_b"),
              QStringLiteral("%1, %2")
                  .arg(seen.value(0).parentModule, seen.value(1).parentModule));
        check("acceptance.vi.instantiation-file",
              !sharedInTopB.file.isEmpty()
                  && QFileInfo(pane->currentFilePath()).absoluteFilePath()
                         == QFileInfo(sharedInTopB.file).absoluteFilePath(),
              pane->currentFilePath());
#if defined(VEDIT_MODULE_TREE_HAVE_PRODUCT_JUMP) && defined(VEDIT_MODULE_TREE_HAVE_EDITOR_CORE)
        KTextEditor::View *view = vedit::editor::viewFor(pane->editorWidget());
        KTextEditor::Document *document = view != nullptr ? view->document() : nullptr;
        check("acceptance.vi.instantiation-cursor",
              view != nullptr && document != nullptr
                  && view->cursorPosition().line() == sharedInTopB.line
                  && document->line(sharedInTopB.line)
                         .contains(QStringLiteral("u_shared")),
              view != nullptr ? QStringLiteral("cursor=%1 expected=%2")
                                    .arg(view->cursorPosition().line() + 1)
                                    .arg(sharedInTopB.line + 1)
                              : QStringLiteral("no view"));
#else
        skip("acceptance.vi.instantiation-cursor",
             QStringLiteral("no KTextEditor provider / product jump target in this build"));
#endif
        check("acceptance.vi.chooser-count-observable",
              interfacePanel->lastChooserCandidates() == 2,
              QStringLiteral("%1").arg(interfacePanel->lastChooserCandidates()));
    }

    // --- (iii, declaration != instantiation) the `shared` module ------------
    // `shared` is instantiated by BOTH top_a (top_a.sv:4) and top_b (top_b.sv:2),
    // so an instance jump would land on one of those two lines. Double-clicking
    // the tree row must land on the DEFINITION in shared.sv instead - the check
    // the top_a case cannot make, because nothing instantiates top_a.
    const ModuleDefinition *sharedDefinition = catalog.find(QStringLiteral("shared"));
    check("acceptance.iii.shared-catalogued", sharedDefinition != nullptr);
    check("acceptance.iii.shared-has-two-instance-sites",
          sharedDefinition != nullptr
              && instantiationsOf(catalog, QStringLiteral("shared")).size() == 2,
          QStringLiteral("sites=%1")
              .arg(instantiationsOf(catalog, QStringLiteral("shared")).size()));
    if (sharedDefinition != nullptr) {
        QTreeWidgetItem *sharedTreeRow = findItem(treePanel->tree(), QStringLiteral("shared"));
        check("acceptance.iii.shared-row-found", sharedTreeRow != nullptr);
        if (sharedTreeRow != nullptr) {
            treePanel->tree()->setCurrentItem(sharedTreeRow);
            treePanel->tree()->scrollToItem(sharedTreeRow);
            const QRect sharedRect = treePanel->tree()->visualItemRect(sharedTreeRow);
            check("acceptance.iii.shared-row-visible",
                  sharedRect.isValid() && !sharedRect.isEmpty());
            if (sharedRect.isValid()) {
                doubleClick(treePanel->tree()->viewport(), sharedRect, Qt::NoModifier);
            }
            const QString expectedShared =
                QFileInfo(sharedDefinition->file).absoluteFilePath();
            const bool openedShared = waitUntil([pane, expectedShared]() {
                return QFileInfo(pane->currentFilePath()).absoluteFilePath() == expectedShared;
            }, 15000);
            check("acceptance.iii.shared-file-opened", openedShared, pane->currentFilePath());
            check("acceptance.iii.shared-not-an-instance-file",
                  QFileInfo(pane->currentFilePath()).fileName() != QLatin1String("top_a.sv")
                      && QFileInfo(pane->currentFilePath()).fileName()
                             != QLatin1String("top_b.sv"),
                  pane->currentFilePath());
#if defined(VEDIT_MODULE_TREE_HAVE_PRODUCT_JUMP) && defined(VEDIT_MODULE_TREE_HAVE_EDITOR_CORE)
            KTextEditor::View *sharedView = vedit::editor::viewFor(pane->editorWidget());
            KTextEditor::Document *sharedDocument =
                sharedView != nullptr ? sharedView->document() : nullptr;
            check("acceptance.iii.shared-ktexteditor-view",
                  sharedView != nullptr && sharedDocument != nullptr);
            if (sharedView != nullptr && sharedDocument != nullptr) {
                const int line0 = sharedView->cursorPosition().line();
                const int column0 = sharedView->cursorPosition().column();
                check("acceptance.iii.shared-cursor-on-definition",
                      line0 == sharedDefinition->line && column0 == sharedDefinition->column,
                      QStringLiteral("cursor=%1:%2 expected=%3:%4")
                          .arg(line0 + 1)
                          .arg(column0 + 1)
                          .arg(sharedDefinition->line + 1)
                          .arg(sharedDefinition->column + 1));
                check("acceptance.iii.shared-declaration-text",
                      line0 >= 0 && line0 < sharedDocument->lines()
                          && sharedDocument->line(line0).contains(QStringLiteral("module shared")),
                      line0 >= 0 ? sharedDocument->line(line0) : QString());
            }
#else
            skip("acceptance.iii.shared-cursor-on-definition",
                 QStringLiteral("no KTextEditor provider / product jump target in this build"));
#endif
        }
    }

    // --- screenshot (only on a real xcb/Xvfb display) -----------------------
    if (!screenshot.isEmpty()) {
        const QString platform = QGuiApplication::platformName();
        if (platform == QLatin1String("offscreen") || qEnvironmentVariableIsEmpty("DISPLAY")) {
            skip("screenshot",
                 QStringLiteral("no xcb/Xvfb display (platform=%1, DISPLAY=%2) — the offscreen "
                                "run cannot take a real screenshot")
                     .arg(platform, qEnvironmentVariableIsEmpty("DISPLAY")
                                        ? QStringLiteral("<unset>")
                                        : qEnvironmentVariable("DISPLAY")));
        } else {
            // Put the panels back into the state worth photographing.
            if (QTreeWidgetItem *row = findItem(treePanel->tree(), QStringLiteral("top_a"))) {
                treePanel->tree()->setCurrentItem(row);
                QCoreApplication::processEvents();
            }
            QDir().mkpath(QFileInfo(screenshot).absolutePath());
            const bool saved = shell->grab().save(screenshot);
            check("screenshot.saved", saved, screenshot);
            std::printf("SHOT %s\n", qPrintable(QFileInfo(screenshot).absoluteFilePath()));
            std::fflush(stdout);
        }
    } else {
        skip("screenshot", QStringLiteral("no screenshot path was requested"));
    }

    // --- deliverable 3: a project-root change refreshes both panels ----------
    // Runtime root switch to "no project": deterministic (no new slang run), and
    // it proves the projectRootChanged seam clears the previous project instead of
    // leaving a stale module/port table on screen.
    shell->setProjectRoot(QString());
    QCoreApplication::processEvents();
    check("acceptance.root-change.tree-empty-state",
          treePanel->statusText().contains(QStringLiteral("No project open")),
          treePanel->statusText());
    check("acceptance.root-change.interface-empty-state",
          interfacePanel->emptyText().contains(QStringLiteral("No project open")),
          interfacePanel->emptyText());

    // Regression: a root switch while a catalog run is IN FLIGHT must cancel it;
    // otherwise the late answer would paint the previous project under the new
    // root. `start()` is synchronous about enqueuing the job, so the in-flight
    // state is deterministic here.
    if (CatalogClient *client = shell->findChild<CatalogClient *>()) {
        shell->setProjectRoot(fixture);
        check("acceptance.root-change.in-flight-started", client->isRunning());
        shell->setProjectRoot(QString());
        QCoreApplication::processEvents();
        check("acceptance.root-change.in-flight-cancelled", !client->isRunning());
        QTest::qWait(1200);
        QCoreApplication::processEvents();
        check("acceptance.root-change.no-late-catalog", !treePanel->catalog().valid
                                                            && treePanel->catalog().error.isEmpty(),
              treePanel->catalog().error);
        check("acceptance.root-change.no-stale-panel",
              treePanel->statusText().contains(QStringLiteral("No project open")),
              treePanel->statusText());
    } else {
        check("acceptance.root-change.client-found", false);
    }

    // A green run must have proven something: zero checks is never a pass.
    check("test.checks-ran", g_checks > 0, QStringLiteral("checks=%1").arg(g_checks));
    std::printf("%s: %d check(s), %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
