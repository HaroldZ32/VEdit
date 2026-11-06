// Module-tree lane self-test (task T17, rebuilt for UX batch 2 item 1/2).
//
// Everything below is deterministic and needs NO slang process: the model takes
// a synthetic `ModuleCatalog` with the same shape the `vd_catalog_*` C ABI
// returns (0-based positions, ports with verbatim type strings, per-site child
// instantiations). The REAL service path (a slang run against the lane fixture)
// is covered by the separate `vedit-module-tree-catalog-test` ctest.
//
// Groups, all printed as `ok`/`FAIL` lines so the log is the evidence:
//   A. the pure module hierarchy: root set == is_top set, a module with two
//      parents appears twice, cycle guard, depth cap 64, row budget, and the
//      structural fact that NO net/driver/load row exists;
//   B. width derivation (`logic[3:0]` -> 4, no dimension -> 1, parameterised
//      range -> unknown with a tooltip) and the interface focus rule;
//   C. both panels end to end: double-click a module -> its definition,
//      double-click a port -> its declaration, Ctrl+double-click -> the
//      instantiation (chooser for several parents), plus the empty states;
//   D. the slang binary selector (`moduleTreeSlangBinary`).
#include "interface_panel.h"
#include "module_catalog.h"
#include "module_hierarchy.h"
#include "module_tree_integration.h"
#include "module_tree_panel.h"

#include "jump/jump_registry.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <utility>

namespace {

namespace jump = vedit::workspace::jump;
using namespace vedit::workspace::module_tree;

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

/// A jump target that records what the registry delivered (the only place a jump
/// can end up: the panels have no file-opening code of their own).
class RecordingTarget final : public jump::IJumpTarget {
public:
    bool jumpTo(const QString &filePath, int line, int column) override
    {
        ++calls;
        lastFile = filePath;
        lastLine = line;
        lastColumn = column;
        return accept;
    }

    int calls = 0;
    QString lastFile;
    int lastLine = 0;
    int lastColumn = 0;
    bool accept = true;
};

ModulePort port(const QString &name, const QString &direction, const QString &type,
                const QString &file, int line, int column)
{
    ModulePort out;
    out.name = name;
    out.direction = direction;
    out.type = type;
    out.file = file;
    out.line = line;
    out.column = column;
    return out;
}

ModuleInstance instance(const QString &module, const QString &path, const QString &file, int line,
                        int column)
{
    ModuleInstance out;
    out.module = module;
    out.path = path;
    out.file = file;
    out.line = line;
    out.column = column;
    return out;
}

ModuleDefinition definition(const QString &name, const QString &file, int line, int column,
                            bool isTop, QVector<ModulePort> ports = {},
                            QVector<ModuleInstance> children = {})
{
    ModuleDefinition out;
    out.name = name;
    out.file = file;
    out.line = line;
    out.column = column;
    out.isTop = isTop;
    out.ports = std::move(ports);
    out.children = std::move(children);
    return out;
}

/// The same shape the ABI test (crates/vd-ffi/tests/ux_batch_2.rs) uses: two
/// parents instantiate `shared`, `orphan` is never instantiated, `consumer`
/// takes an interface port and `reflink` a ref port.
ModuleCatalog syntheticCatalog()
{
    ModuleCatalog catalog;
    catalog.valid = true;
    catalog.tool = QStringLiteral("slang");
    catalog.elapsedMs = 5;
    catalog.peakRssKb = 19592;
    catalog.fileSetSize = 7;
    catalog.modules = {
        definition(QStringLiteral("consumer"), QStringLiteral("/proj/consumer.sv"), 0, 7, false,
                   {port(QStringLiteral("bus"), QStringLiteral("InOut"), QStringLiteral("bus_if"),
                         QStringLiteral("/proj/consumer.sv"), 0, 25),
                    port(QStringLiteral("clk"), QStringLiteral("In"), QStringLiteral("logic"),
                         QStringLiteral("/proj/consumer.sv"), 0, 33),
                    port(QStringLiteral("y"), QStringLiteral("Out"),
                         QStringLiteral("logic[3:0]"), QStringLiteral("/proj/consumer.sv"), 0, 52)}),
        definition(QStringLiteral("orphan"), QStringLiteral("/proj/orphan.sv"), 0, 7, true,
                   {port(QStringLiteral("a"), QStringLiteral("In"), QStringLiteral("logic"),
                         QStringLiteral("/proj/orphan.sv"), 0, 15),
                    port(QStringLiteral("b"), QStringLiteral("Out"), QStringLiteral("logic"),
                         QStringLiteral("/proj/orphan.sv"), 0, 32)}),
        definition(QStringLiteral("reflink"), QStringLiteral("/proj/reflink.sv"), 0, 7, false,
                   {port(QStringLiteral("x"), QStringLiteral("Ref"), QStringLiteral("logic"),
                         QStringLiteral("/proj/reflink.sv"), 0, 19)}),
        definition(QStringLiteral("shared"), QStringLiteral("/proj/shared.sv"), 2, 7, false,
                   {port(QStringLiteral("a"), QStringLiteral("In"), QStringLiteral("logic"),
                         QStringLiteral("/proj/shared.sv"), 2, 15),
                    port(QStringLiteral("y"), QStringLiteral("Out"),
                         QStringLiteral("logic[3:0]"), QStringLiteral("/proj/shared.sv"), 2, 38)}),
        definition(QStringLiteral("top_a"), QStringLiteral("/proj/top_a.sv"), 0, 7, true,
                   {port(QStringLiteral("clk"), QStringLiteral("In"), QStringLiteral("logic"),
                         QStringLiteral("/proj/top_a.sv"), 0, 10),
                    port(QStringLiteral("pad"), QStringLiteral("InOut"),
                         QStringLiteral("logic"), QStringLiteral("/proj/top_a.sv"), 0, 28),
                    port(QStringLiteral("y"), QStringLiteral("Out"),
                         QStringLiteral("logic[3:0]"), QStringLiteral("/proj/top_a.sv"), 0, 41)},
                   {instance(QStringLiteral("shared"), QStringLiteral("top_a.u_shared"),
                             QStringLiteral("/proj/top_a.sv"), 3, 2),
                    instance(QStringLiteral("consumer"), QStringLiteral("top_a.u_consumer"),
                             QStringLiteral("/proj/top_a.sv"), 4, 2),
                    instance(QStringLiteral("reflink"), QStringLiteral("top_a.u_ref"),
                             QStringLiteral("/proj/top_a.sv"), 5, 2)}),
        definition(QStringLiteral("top_b"), QStringLiteral("/proj/top_b.sv"), 0, 7, true,
                   {port(QStringLiteral("clk"), QStringLiteral("In"), QStringLiteral("logic"),
                         QStringLiteral("/proj/top_b.sv"), 0, 10),
                    port(QStringLiteral("y"), QStringLiteral("Out"),
                         QStringLiteral("logic[3:0]"), QStringLiteral("/proj/top_b.sv"), 0, 41)},
                   {instance(QStringLiteral("shared"), QStringLiteral("top_b.u_shared"),
                             QStringLiteral("/proj/top_b.sv"), 2, 2)}),
    };
    return catalog;
}

QVector<const ModuleTreeNode *> nodesWithModule(const QVector<ModuleTreeNode> &roots,
                                                const QString &name)
{
    QVector<const ModuleTreeNode *> found;
    for (const ModuleTreeNode *node : flattenModuleTree(roots)) {
        if (node->moduleName == name && node->kind == ModuleRowKind::Module) {
            found.append(node);
        }
    }
    return found;
}

QTreeWidgetItem *findTreeItem(QTreeWidgetItem *item, const QString &text)
{
    if (item->text(0) == text) {
        return item;
    }
    for (int index = 0; index < item->childCount(); ++index) {
        if (QTreeWidgetItem *found = findTreeItem(item->child(index), text)) {
            return found;
        }
    }
    return nullptr;
}

QTreeWidgetItem *findTreeItem(QTreeWidget *tree, const QString &text)
{
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (QTreeWidgetItem *found = findTreeItem(tree->topLevelItem(index), text)) {
            return found;
        }
    }
    return nullptr;
}

int countLabel(QTreeWidgetItem *item, const QString &text)
{
    int count = item->text(0) == text ? 1 : 0;
    for (int index = 0; index < item->childCount(); ++index) {
        count += countLabel(item->child(index), text);
    }
    return count;
}

int countLabel(QTreeWidget *tree, const QString &text)
{
    int count = 0;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        count += countLabel(tree->topLevelItem(index), text);
    }
    return count;
}

void doubleClick(QWidget *viewport, const QRect &rect, Qt::KeyboardModifiers modifiers)
{
    QTest::mousePress(viewport, Qt::LeftButton, modifiers, rect.center());
    QTest::mouseDClick(viewport, Qt::LeftButton, modifiers, rect.center());
    QTest::mouseRelease(viewport, Qt::LeftButton, modifiers, rect.center());
    QApplication::processEvents();
}

// --- A. the pure module hierarchy -----------------------------------------

void checkModuleHierarchy()
{
    const ModuleCatalog catalog = syntheticCatalog();
    const QVector<ModuleTreeNode> roots = buildModuleTree(catalog);

    QStringList rootNames;
    for (const ModuleTreeNode &root : roots) {
        rootNames.append(root.moduleName);
    }
    check("tree.roots.equal-is-top", rootNames == catalog.topModuleNames(),
          QStringLiteral("roots=[%1] tops=[%2]")
              .arg(rootNames.join(QStringLiteral(", ")), catalog.topModuleNames().join(QStringLiteral(", "))));
    check("tree.roots.fixture-set",
          rootNames == QStringList({QStringLiteral("orphan"), QStringLiteral("top_a"),
                                    QStringLiteral("top_b")}),
          rootNames.join(QStringLiteral(", ")));

    const QVector<const ModuleTreeNode *> shared = nodesWithModule(roots, QStringLiteral("shared"));
    QStringList sharedPaths;
    for (const ModuleTreeNode *node : shared) {
        sharedPaths.append(node->instancePath);
    }
    check("tree.two-parents.two-occurrences", shared.size() == 2, sharedPaths.join(QStringLiteral(", ")));
    check("tree.two-parents.distinct-path",
          sharedPaths == QStringList({QStringLiteral("top_a.u_shared"),
                                      QStringLiteral("top_b.u_shared")}),
          sharedPaths.join(QStringLiteral(", ")));
    check("tree.two-parents.definitions-agree",
          shared.size() == 2 && shared.at(0)->definitionFile == shared.at(1)->definitionFile
              && shared.at(0)->definitionLine == shared.at(1)->definitionLine);

    const QVector<const ModuleTreeNode *> flat = flattenModuleTree(roots);
    check("tree.row-count.seven", countModuleTreeRows(roots) == 7,
          QStringLiteral("rows=%1").arg(countModuleTreeRows(roots)));
    bool onlyModules = true;
    bool namesPresent = true;
    for (const ModuleTreeNode *node : flat) {
        onlyModules = onlyModules && node->kind == ModuleRowKind::Module;
        namesPresent = namesPresent && !node->moduleName.isEmpty();
    }
    check("tree.no-net-driver-load-rows", onlyModules && namesPresent,
          QStringLiteral("kinds=%1").arg(flat.size()));
    // The explicit negative control: the removed rows were the fixture's nets
    // and drivers; no row may carry those labels any more.
    QStringList labels;
    for (const ModuleTreeNode *node : flat) {
        labels.append(node->moduleName);
    }
    const QStringList forbidden = {QStringLiteral("link"), QStringLiteral("clk"),
                                   QStringLiteral("u_shared"), QStringLiteral("driver"),
                                   QStringLiteral("load"), QStringLiteral("net")};
    bool clean = true;
    for (const QString &label : forbidden) {
        clean = clean && !labels.contains(label);
    }
    check("tree.no-net-labels", clean, labels.join(QStringLiteral(", ")));

    // Cycle protection: A -> B -> A must terminate with a visible marker.
    ModuleCatalog cycle;
    cycle.valid = true;
    cycle.modules = {
        definition(QStringLiteral("A"), QStringLiteral("/proj/cycle.sv"), 0, 7, true, {},
                   {instance(QStringLiteral("B"), QStringLiteral("A.u_b"),
                             QStringLiteral("/proj/cycle.sv"), 2, 2)}),
        definition(QStringLiteral("B"), QStringLiteral("/proj/cycle.sv"), 5, 7, false, {},
                   {instance(QStringLiteral("A"), QStringLiteral("A.u_b.u_a"),
                             QStringLiteral("/proj/cycle.sv"), 7, 2)}),
    };
    const QVector<ModuleTreeNode> cycleRoots = buildModuleTree(cycle);
    const QVector<const ModuleTreeNode *> cycleFlat = flattenModuleTree(cycleRoots);
    bool cycleMarker = false;
    for (const ModuleTreeNode *node : cycleFlat) {
        if (node->kind == ModuleRowKind::Cycle) {
            cycleMarker = true;
            check("tree.cycle.marker-note", !node->note.isEmpty(), node->note);
        }
    }
    check("tree.cycle.visible-marker", cycleMarker,
          QStringLiteral("rows=%1").arg(cycleFlat.size()));
    check("tree.cycle.terminates", cycleFlat.size() == 3,
          QStringLiteral("rows=%1").arg(cycleFlat.size()));

    // Depth cap 64: a 70-module chain must stop at level 64 with a marker.
    ModuleCatalog chain;
    chain.valid = true;
    const int chainLength = 70;
    for (int index = 0; index < chainLength; ++index) {
        const QString name = QStringLiteral("m%1").arg(index, 2, 10, QLatin1Char('0'));
        QVector<ModuleInstance> children;
        if (index + 1 < chainLength) {
            const QString next = QStringLiteral("m%1").arg(index + 1, 2, 10, QLatin1Char('0'));
            children.append(instance(next, QStringLiteral("root.") + next,
                                     QStringLiteral("/proj/chain.sv"), index + 1, 2));
        }
        chain.modules.append(definition(name, QStringLiteral("/proj/chain.sv"), index, 7,
                                        index == 0, {}, children));
    }
    const QVector<ModuleTreeNode> chainRoots = buildModuleTree(chain);
    const QVector<const ModuleTreeNode *> chainFlat = flattenModuleTree(chainRoots);
    int deepestModule = 0;
    int depthMarkers = 0;
    const std::function<void(const ModuleTreeNode &, int)> walk = [&](const ModuleTreeNode &node,
                                                                     int depth) {
        if (node.kind == ModuleRowKind::Module) {
            deepestModule = std::max(deepestModule, depth);
        }
        if (node.kind == ModuleRowKind::DepthLimit) {
            ++depthMarkers;
            check("tree.depth.marker-note", node.note.contains(QStringLiteral("depth limit")),
                  node.note);
        }
        for (const ModuleTreeNode &child : node.children) {
            walk(child, depth + 1);
        }
    };
    for (const ModuleTreeNode &root : chainRoots) {
        walk(root, 1);
    }
    check("tree.depth.capped-at-64", deepestModule == kModuleTreeMaxDepth,
          QStringLiteral("deepest=%1").arg(deepestModule));
    check("tree.depth.visible-marker", depthMarkers == 1,
          QStringLiteral("markers=%1").arg(depthMarkers));
    check("tree.depth.rows-bounded", chainFlat.size() == kModuleTreeMaxDepth + 1,
          QStringLiteral("rows=%1").arg(chainFlat.size()));
}

// --- B. width derivation + focus rule -------------------------------------

void checkWidthsAndFocus()
{
    const auto width = [](const QString &type) { return portWidthFromType(type); };
    check("width.logic-4", width(QStringLiteral("logic[3:0]")).known
                               && width(QStringLiteral("logic[3:0]")).width == 4);
    check("width.no-dimension-1", width(QStringLiteral("logic")).known
                                     && width(QStringLiteral("logic")).width == 1);
    check("width.interface-1", width(QStringLiteral("bus_if")).width == 1);
    check("width.reversed-range-4", width(QStringLiteral("logic[0:3]")).width == 4);
    check("width.multi-dimension-8",
          width(QStringLiteral("logic[1:0][3:0]")).width == 8);
    check("width.spaces-8", width(QStringLiteral("wire [7 : 0]")).width == 8);
    const PortWidth parameterised = width(QStringLiteral("logic[WIDTH-1:0]"));
    check("width.parameterised-unknown", !parameterised.known && !parameterised.problem.isEmpty(),
          parameterised.problem);
    const PortWidth empty = width(QString());
    check("width.empty-unknown", !empty.known && !empty.problem.isEmpty(), empty.problem);

    ModuleCatalog focus;
    focus.valid = true;
    focus.modules = {
        definition(QStringLiteral("m1"), QStringLiteral("/proj/a.sv"), 0, 7, true),
        definition(QStringLiteral("m2"), QStringLiteral("/proj/a.sv"), 10, 7, false),
        definition(QStringLiteral("m3"), QStringLiteral("/proj/b.sv"), 5, 7, false),
    };
    const ModuleDefinition *caret = moduleAtCaret(focus, QStringLiteral("/proj/a.sv"), 5);
    check("focus.nearest-above", caret != nullptr && caret->name == QLatin1String("m1"),
          caret != nullptr ? caret->name : QStringLiteral("<none>"));
    caret = moduleAtCaret(focus, QStringLiteral("/proj/a.sv"), 10);
    check("focus.exact-definition-line", caret != nullptr && caret->name == QLatin1String("m2"),
          caret != nullptr ? caret->name : QStringLiteral("<none>"));
    caret = moduleAtCaret(focus, QStringLiteral("/proj/b.sv"), 3);
    check("focus.before-first-definition-none", caret == nullptr);
    check("focus.other-file-none", moduleAtCaret(focus, QStringLiteral("/proj/c.sv"), 99) == nullptr);

    const ModuleCatalog catalog = syntheticCatalog();
    const QVector<InstantiationSite> sites = instantiationsOf(catalog, QStringLiteral("shared"));
    check("instantiation.two-parents", sites.size() == 2,
          QStringLiteral("sites=%1").arg(sites.size()));
    check("instantiation.sorted",
          sites.size() == 2 && sites.at(0).parentModule == QLatin1String("top_a")
              && sites.at(1).parentModule == QLatin1String("top_b"));
    check("instantiation.top-has-none",
          instantiationsOf(catalog, QStringLiteral("top_a")).isEmpty());

    const ModuleJump jump = jumpFromPosition(QStringLiteral("/proj/top_a.sv"), 10, 6);
    check("jump.one-based", jump.line == 11 && jump.column == 7, jump.file);
    check("jump.describe", describePosition(QStringLiteral("/proj/top_a.sv"), 10, 6)
                               == QLatin1String("top_a.sv:11:7"));
}

// --- C. the panels --------------------------------------------------------

void checkPanels()
{
    jump::Registry::instance().reset();
    RecordingTarget target;
    jump::setTarget(&target);

    const ModuleCatalog catalog = syntheticCatalog();

    ModuleTreePanel treePanel;
    treePanel.setCatalog(catalog);
    treePanel.resize(620, 680);
    treePanel.show();
    QApplication::processEvents();
    check("panel.tree.rows", treePanel.tree()->topLevelItemCount() == 3,
          QStringLiteral("roots=%1").arg(treePanel.tree()->topLevelItemCount()));
    check("panel.tree.shared-twice",
          findTreeItem(treePanel.tree(), QStringLiteral("shared")) != nullptr
              && countLabel(treePanel.tree(), QStringLiteral("shared")) == 2,
          QStringLiteral("occurrences=%1")
              .arg(countLabel(treePanel.tree(), QStringLiteral("shared"))));

    QString selected;
    treePanel.setOnSelectionChanged([&selected](const QString &name) { selected = name; });
    QTreeWidgetItem *topA = findTreeItem(treePanel.tree(), QStringLiteral("top_a"));
    check("panel.tree.top-a-row", topA != nullptr);
    if (topA != nullptr) {
        treePanel.tree()->setCurrentItem(topA);
        QApplication::processEvents();
        check("panel.selection.callback", selected == QLatin1String("top_a"), selected);
        const int before = target.calls;
        check("panel.activate.delivers", treePanel.activateItem(topA));
        check("panel.activate.target", target.calls == before + 1
                                           && target.lastFile == QLatin1String("/proj/top_a.sv")
                                           && target.lastLine == 1 && target.lastColumn == 8,
              QStringLiteral("%1:%2:%3")
                  .arg(target.lastFile)
                  .arg(target.lastLine)
                  .arg(target.lastColumn));

        // A real double-click on the row must take the SAME path.
        const int beforeClick = target.calls;
        const QRect rect = treePanel.tree()->visualItemRect(topA);
        if (rect.isValid()) {
            doubleClick(treePanel.tree()->viewport(), rect, Qt::NoModifier);
        }
        check("panel.double-click.module-definition",
              target.calls > beforeClick && target.lastFile == QLatin1String("/proj/top_a.sv")
                  && target.lastLine == 1,
              QStringLiteral("calls=%1 line=%2").arg(target.calls).arg(target.lastLine));
    }

    // Empty states: one concise line naming the reason, never per-file errors.
    ModuleTreePanel emptyTree;
    check("panel.empty.no-catalog",
          emptyTree.statusText().contains(QStringLiteral("No module catalog")),
          emptyTree.statusText());
    emptyTree.setNoProject(QStringLiteral("No project open — open a folder to build the module "
                                          "catalog."));
    check("panel.empty.no-project",
          emptyTree.statusText().contains(QStringLiteral("No project open")),
          emptyTree.statusText());
    emptyTree.setFailure(QStringLiteral("module catalog: boom\nsecond line"));
    check("panel.empty.failure-one-line",
          emptyTree.statusText() == QLatin1String("module catalog: boom second line"),
          emptyTree.statusText());
    ModuleCatalog emptyCatalog;
    emptyCatalog.valid = true;
    emptyTree.setCatalog(emptyCatalog);
    check("panel.empty.no-modules",
          emptyTree.statusText().contains(QStringLiteral("No modules found")),
          emptyTree.statusText());

    // --- the interface panel ---
    InterfacePanel interface;
    interface.setCatalog(catalog);
    interface.setTreeSelection(QStringLiteral("top_a"));
    interface.resize(560, 300);
    interface.show();
    QApplication::processEvents();
    check("interface.focus.tree-selection",
          interface.focusedModuleName() == QLatin1String("top_a"),
          interface.focusedModuleName());
    check("interface.table.columns", interface.table()->columnCount() == 5);
    check("interface.table.rows", interface.table()->rowCount() == 3,
          QStringLiteral("rows=%1").arg(interface.table()->rowCount()));
    const auto cell = [&interface](int row, int column) {
        QTableWidgetItem *item = interface.table()->item(row, column);
        return item != nullptr ? item->text() : QString();
    };
    check("interface.port.clk", cell(0, 0) == QLatin1String("clk")
                                    && cell(0, 1) == QLatin1String("In")
                                    && cell(0, 2) == QLatin1String("logic")
                                    && cell(0, 3) == QLatin1String("1"),
          QStringLiteral("%1/%2/%3/%4").arg(cell(0, 0), cell(0, 1), cell(0, 2), cell(0, 3)));
    check("interface.port.pad", cell(1, 0) == QLatin1String("pad")
                                    && cell(1, 1) == QLatin1String("InOut")
                                    && cell(1, 3) == QLatin1String("1"),
          QStringLiteral("%1/%2/%3").arg(cell(1, 0), cell(1, 1), cell(1, 3)));
    check("interface.port.y", cell(2, 0) == QLatin1String("y")
                                  && cell(2, 1) == QLatin1String("Out")
                                  && cell(2, 2) == QLatin1String("logic[3:0]")
                                  && cell(2, 3) == QLatin1String("4"),
          QStringLiteral("%1/%2/%3/%4").arg(cell(2, 0), cell(2, 1), cell(2, 2), cell(2, 3)));
    check("interface.location", !cell(0, 4).isEmpty(), cell(0, 4));
    check("interface.tooltip.abi-limit",
          interface.table()->toolTip().contains(QStringLiteral("per-connection")),
          interface.table()->toolTip());
    check("interface.tooltip.static", InterfacePanel::activationTooltip()
                                          .contains(QStringLiteral("instantiation statement")));

    // Unparsable width: blank cell + tooltip, never a guessed number.
    ModuleCatalog weird;
    weird.valid = true;
    weird.modules = {definition(
        QStringLiteral("weird"), QStringLiteral("/proj/weird.sv"), 0, 7, true,
        {port(QStringLiteral("d"), QStringLiteral("In"), QStringLiteral("logic[WIDTH-1:0]"),
              QStringLiteral("/proj/weird.sv"), 0, 20)})};
    InterfacePanel weirdPanel;
    weirdPanel.setCatalog(weird);
    weirdPanel.setTreeSelection(QStringLiteral("weird"));
    check("interface.width.unknown-blank", weirdPanel.table()->rowCount() == 1
                                               && weirdPanel.table()->item(0, 3)->text().isEmpty());
    check("interface.width.unknown-tooltip",
          weirdPanel.table()->item(0, 3)->toolTip().contains(QStringLiteral("cannot be derived")),
          weirdPanel.table()->item(0, 3)->toolTip());

    // Focus rule (b): no tree selection -> the module enclosing the caret.
    InterfacePanel caretPanel;
    caretPanel.setCatalog(catalog);
    caretPanel.setCaretContext(QStringLiteral("/proj/top_a.sv"), 4);
    check("interface.focus.caret", caretPanel.focusedModuleName() == QLatin1String("top_a"),
          caretPanel.focusedModuleName());
    // "Above the first definition in the file" is the only no-module case: every
    // definition at or below the caret wins, and m3 starts at line 5 of b.sv.
    caretPanel.setCaretContext(QStringLiteral("/proj/b.sv"), 3);
    check("interface.focus.caret-above-definition",
          caretPanel.focusedModuleName().isEmpty()
              && caretPanel.emptyText().contains(QStringLiteral("No focused module")),
          caretPanel.focusedModuleName());
    caretPanel.setCaretContext(QStringLiteral("/proj/not-catalogued.sv"), 99);
    check("interface.focus.other-file", caretPanel.focusedModuleName().isEmpty());
    caretPanel.setCaretContext(QStringLiteral("/proj/top_a.sv"), 4);
    caretPanel.setTreeSelection(QStringLiteral("top_b"));
    check("interface.focus.selection-wins",
          caretPanel.focusedModuleName() == QLatin1String("top_b"),
          caretPanel.focusedModuleName());

    // Port activation: double-click -> declaration.
    interface.setTreeSelection(QStringLiteral("top_a"));
    const int beforePort = target.calls;
    check("interface.port.activate", interface.activatePortRow(2, false));
    check("interface.port.declaration-target",
          target.calls == beforePort + 1 && target.lastFile == QLatin1String("/proj/top_a.sv")
              && target.lastLine == 1 && target.lastColumn == 42,
          QStringLiteral("%1:%2:%3")
              .arg(target.lastFile)
              .arg(target.lastLine)
              .arg(target.lastColumn));
    const QRect portRect = interface.table()->visualItemRect(interface.table()->item(2, 0));
    const int beforePortClick = target.calls;
    if (portRect.isValid()) {
        doubleClick(interface.table()->viewport(), portRect, Qt::NoModifier);
    }
    check("interface.port.double-click",
          target.calls == beforePortClick + 1 && target.lastLine == 1,
          QStringLiteral("calls=%1 line=%2").arg(target.calls).arg(target.lastLine));

    // Ctrl+double-click -> instantiation, chooser for the two parents.
    interface.setTreeSelection(QStringLiteral("shared"));
    QVector<InstantiationSite> seen;
    int chosen = -1;
    interface.setInstantiationChooser([&seen, &chosen](const QVector<InstantiationSite> &sites) {
        seen = sites;
        chosen = 1; // top_b (candidates are sorted by parent name)
        return chosen;
    });
    const int beforeInstance = target.calls;
    check("interface.port.ctrl-activate", interface.activatePortRow(0, true));
    check("interface.chooser.candidates",
          interface.lastChooserCandidates() == 2 && seen.size() == 2
              && seen.at(0).parentModule == QLatin1String("top_a")
              && seen.at(1).parentModule == QLatin1String("top_b"),
          QStringLiteral("candidates=%1").arg(seen.size()));
    check("interface.instantiation-target",
          target.calls == beforeInstance + 1 && target.lastFile == QLatin1String("/proj/top_b.sv")
              && target.lastLine == 3 && target.lastColumn == 3,
          QStringLiteral("%1:%2:%3")
              .arg(target.lastFile)
              .arg(target.lastLine)
              .arg(target.lastColumn));
    const QRect sharedRect = interface.table()->visualItemRect(interface.table()->item(0, 0));
    const int beforeCtrlClick = target.calls;
    if (sharedRect.isValid()) {
        doubleClick(interface.table()->viewport(), sharedRect, Qt::ControlModifier);
    }
    check("interface.port.ctrl-double-click",
          target.calls == beforeCtrlClick + 1 && target.lastFile == QLatin1String("/proj/top_b.sv"),
          QStringLiteral("calls=%1 file=%2").arg(target.calls).arg(target.lastFile));

    // A top module has no instantiation: the panel says so instead of guessing.
    interface.setTreeSelection(QStringLiteral("top_a"));
    interface.setInstantiationChooser({});
    const int beforeTop = target.calls;
    check("interface.instantiation.top-none", !interface.activatePortRow(0, true));
    check("interface.instantiation.top-status",
          target.calls == beforeTop && interface.statusText().contains(QStringLiteral("design top")),
          interface.statusText());

    // Interface empty states.
    InterfacePanel emptyInterface;
    check("interface.empty.no-catalog",
          emptyInterface.emptyText().contains(QStringLiteral("No module catalog")),
          emptyInterface.emptyText());
    emptyInterface.setNoProject(QStringLiteral("No project open — open a folder to build the "
                                               "module catalog."));
    check("interface.empty.no-project",
          emptyInterface.emptyText().contains(QStringLiteral("No project open")),
          emptyInterface.emptyText());
    emptyInterface.setCatalog(emptyCatalog);
    check("interface.empty.no-modules",
          emptyInterface.emptyText().contains(QStringLiteral("No modules found")),
          emptyInterface.emptyText());

    jump::clearTargetIf(&target);
    jump::Registry::instance().reset();
}

// --- D. the slang binary selector -----------------------------------------

void checkToolchainWiring()
{
    QTemporaryDir appDir;
    check("toolchain.temp-dir", appDir.isValid());
    if (!appDir.isValid()) {
        return;
    }
    const QString binDir = appDir.path() + QStringLiteral("/bin");
    check("toolchain.bin-dir", QDir().mkpath(binDir), binDir);
    const QString packaged = binDir + QStringLiteral("/slang");
    QFile script(packaged);
    bool installed = false;
    if (script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        script.write("#!/bin/sh\nexit 0\n");
        script.close();
        installed = QFile::setPermissions(packaged, QFileDevice::ReadOwner
                                                       | QFileDevice::WriteOwner
                                                       | QFileDevice::ExeOwner);
    }
    check("toolchain.packaged-slang", installed && QFileInfo(packaged).isExecutable(), packaged);

    QObject shell;
    shell.setProperty("vedit.diagnostics.toolchainSlang", packaged);
    const bool hadOverride = qEnvironmentVariableIsSet("VEDIT_SLANG");
    const QByteArray overrideValue = qgetenv("VEDIT_SLANG");
    qunsetenv("VEDIT_SLANG");
    check("toolchain.published-path", vedit::workspace::moduleTreeSlangBinary(&shell) == packaged,
          vedit::workspace::moduleTreeSlangBinary(&shell));
    QObject bareShell;
    check("toolchain.no-source-empty", vedit::workspace::moduleTreeSlangBinary(&bareShell).isEmpty());
    qputenv("VEDIT_SLANG", packaged.toLocal8Bit());
    check("toolchain.env-override-wins",
          vedit::workspace::moduleTreeSlangBinary(&shell) == packaged);
    if (hadOverride) {
        qputenv("VEDIT_SLANG", overrideValue);
    } else {
        qunsetenv("VEDIT_SLANG");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    checkModuleHierarchy();
    checkWidthsAndFocus();
    checkPanels();
    checkToolchainWiring();
    std::printf("%s: %d check(s), %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
