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
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
#include "project_state.h"
#endif

#include "jump/jump_registry.h"

#include <QLineEdit>
#include <QComboBox>
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
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

    // RM-17: the top control is the ONE place the top is set: Auto / detected
    // candidates / free text, the effective value is shown, and clearing it
    // returns to Auto. The exclusion line states how many files are outside.
    {
        auto *top = treePanel.findChild<QComboBox *>(QStringLiteral("module-tree-top"));
        check("top.control-exists", top != nullptr && top->isEditable(),
              QStringLiteral("combo=%1").arg(top != nullptr));
        treePanel.setTopCandidates({QStringLiteral("top_a"), QStringLiteral("top_b")});
        check("top.candidates-listed", top != nullptr && top->count() == 3,
              QStringLiteral("count=%1").arg(top != nullptr ? top->count() : -1));
        treePanel.setEffectiveTop(QStringLiteral("top_a"));
        check("top.effective-value", treePanel.effectiveTop() == QStringLiteral("top_a"),
              treePanel.effectiveTop());
        QString seen;
        treePanel.setOnTopChanged([&seen](const QString &value) { seen = value; });
        treePanel.setEffectiveTop(QStringLiteral("typed_name"));
        // RE-ANCHORED for batch-20261116 AC M1.3: the commit gesture is Enter
        // (`returnPressed`) or choosing an entry (`activated`). `editingFinished`
        // - i.e. "the field lost focus" - was REMOVED because clicking anywhere
        // else started a whole-project catalog nobody asked for. Same value, same
        // handler, new gesture: the assertion is re-anchored, not weakened.
        emit top->lineEdit()->returnPressed();
        check("top.free-text-emits", seen == QStringLiteral("typed_name"),
              QStringLiteral("seen=%1").arg(seen));
        // Two DELIBERATE gestures are two event-loop turns (the twin-signal guard
        // is cleared at the end of the turn it was raised in).
        QCoreApplication::processEvents();
        treePanel.setEffectiveTop(QString());
        emit top->lineEdit()->returnPressed();
        check("top.cleared-means-auto",
              treePanel.effectiveTop().isEmpty() && seen.isEmpty(),
              QStringLiteral("effective='%1' seen='%2'")
                  .arg(treePanel.effectiveTop(), seen));
        const QString note = QStringLiteral("3 file(s) outside the top top_a are not analysed");
        treePanel.setScopeNote(note);
        check("top.exclusion-line", treePanel.statusText().contains(note),
              treePanel.statusText());
    }

#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
    // RM-17: the setting survives a restart and is remembered PER ROOT. The
    // persisted JSON carries `top` + `topByRoot`, and a reload restores both
    // roots' entries (the model seeds the live root's entry from `top`).
    {
        vedit::workspace::project::ProjectState state;
        state.root = QStringLiteral("/proj/a");
        state.top = QStringLiteral("top_a");
        state.topByRoot.insert(state.root, state.top);
        state.topByRoot.insert(QStringLiteral("/proj/b"), QStringLiteral("top_b"));
        const vedit::workspace::project::ProjectState reloaded =
            vedit::workspace::project::ProjectState::fromJson(state.toJson());
        check("top.state-roundtrip",
              reloaded.top == QStringLiteral("top_a")
                  && reloaded.topByRoot.value(QStringLiteral("/proj/a"))
                         == QStringLiteral("top_a")
                  && reloaded.topByRoot.value(QStringLiteral("/proj/b"))
                         == QStringLiteral("top_b"),
              QStringLiteral("top='%1' a='%2' b='%3'")
                  .arg(reloaded.top, reloaded.topByRoot.value(QStringLiteral("/proj/a")),
                       reloaded.topByRoot.value(QStringLiteral("/proj/b"))));
        check("top.per-root",
              reloaded.topByRoot.value(QStringLiteral("/proj/b")) != reloaded.top,
              QStringLiteral("a='%1' b='%2'")
                  .arg(reloaded.top, reloaded.topByRoot.value(QStringLiteral("/proj/b"))));
    }
#endif

    // U9d / M1: the catalog's warning (an oversize size note or a PARTIAL-tree
    // reason) must reach the panel's summary label, and a clean catalog must not
    // show one. This is the user-visible half of "the warning reaches the panel":
    // the service warning is useless if the module tree renders it nowhere.
    {
        const QString warning =
            QStringLiteral("the elaborated AST JSON is 274 MiB (over the 256 MiB size threshold)");
        ModuleCatalog warned = catalog;
        warned.warning = warning;
        ModuleTreePanel warnedPanel;
        warnedPanel.setCatalog(warned);
        check("panel.summary.shows-warning", warnedPanel.summaryText().contains(warning),
              warnedPanel.summaryText());

        ModuleCatalog clean = catalog;
        clean.warning.clear();
        ModuleTreePanel cleanPanel;
        cleanPanel.setCatalog(clean);
        check("panel.summary.no-warning-on-clean-catalog",
              !cleanPanel.summaryText().contains(warning)
                  && !cleanPanel.summaryText().contains(QStringLiteral("⚠")),
              cleanPanel.summaryText());
    }

    // batch-20261118 B: the duplicate-module sentence the SERVICE sends reaches
    // the panel verbatim (the Rust catalog resolves the collision and puts the
    // sentence into `VdModuleCatalog.warning`), and the C++ model refuses to drop
    // a duplicate silently even if one ever reaches it.
    {
        const QString duplicateWarning = QStringLiteral(
            "module 'uart' is defined in both sim/uart.v and rtl/uart.v; using rtl/uart.v");
        ModuleCatalog duplicateWarned = catalog;
        duplicateWarned.warning = duplicateWarning;
        ModuleTreePanel duplicateWarnedPanel;
        duplicateWarnedPanel.setCatalog(duplicateWarned);
        check("panel.summary.shows-duplicate-module-warning",
              duplicateWarnedPanel.summaryText().contains(duplicateWarning),
              duplicateWarnedPanel.summaryText());

        ModuleCatalog duplicated = catalog;
        duplicated.modules.append(definition(
            QStringLiteral("top_a"), QStringLiteral("/proj/rtl/uart.v"), 0, 7, true,
            {port(QStringLiteral("done"), QStringLiteral("Out"), QStringLiteral("logic"),
                  QStringLiteral("/proj/rtl/uart.v"), 0, 20)}));
        const ModuleDefinition *first = duplicated.find(QStringLiteral("top_a"));
        check("catalog.duplicate.find-keeps-first",
              first != nullptr && first->file == QLatin1String("/proj/top_a.sv"),
              first != nullptr ? first->file : QStringLiteral("<none>"));
        const QStringList notes = duplicated.duplicateNotes();
        const QString note = notes.isEmpty() ? QString() : notes.first();
        check("catalog.duplicate.note-names-both-files",
              notes.size() == 1 && note.contains(QStringLiteral("/proj/rtl/uart.v"))
                  && note.contains(QStringLiteral("/proj/top_a.sv"))
                  && note.endsWith(QStringLiteral("; using /proj/top_a.sv")),
              note);
        ModuleTreePanel duplicatedPanel;
        duplicatedPanel.setCatalog(duplicated);
        check("panel.summary.shows-duplicate-note",
              !note.isEmpty() && duplicatedPanel.summaryText().contains(note),
              duplicatedPanel.summaryText());
    }

    // batch-20261119 R1-c/R1-d, revised in R2: the real-machine shape of the
    // defect. The service can emit a warning of a few THOUSAND characters (a
    // module defined 96 times, every copy's absolute path spelled out). Before R1
    // the two word-wrapped labels paid their whole heightForWidth minimum first and
    // the QTreeWidget was left with ONE visible row. R1 then capped them to one
    // ELIDED line, which the user rejected on the real machine ("the error cannot
    // be read" - a tooltip is not a reading surface). R2 keeps the height bound but
    // shows the FULL text in a fixed-height scroller, so the shape to pin down is:
    // the full text is painted, the bar really moves, the height ignores the text,
    // the full text is still a tooltip, and the table is still >= 6 rows tall.
    {
        QStringList samePath;
        for (int index = 0; index < 96; ++index) {
            samePath.append(QStringLiteral("/proj/rtl/stdcell.v"));
        }
        const QString longWarning =
            QStringLiteral("module 'ANDX2' is defined 96 times (")
            + samePath.join(QStringLiteral(", ")) + QStringLiteral("); using /proj/rtl/stdcell.v");
        ModuleCatalog longWarned = catalog;
        longWarned.warning = longWarning;
        ModuleTreePanel longPanel;
        longPanel.setCatalog(longWarned);
        // A DOCK-SIZED panel, not a full window: the measured pre-fix layout
        // still leaves 6 rows at 620x680, so that size would not discriminate.
        // At 620x360 the word-wrapped summary alone claims 222 px and the
        // viewport falls to 50 px (< 6 * 14), which is the user's screenshot.
        longPanel.resize(620, 360);
        longPanel.show();
        QApplication::processEvents();
        auto *summary = longPanel.findChild<QScrollArea *>(QStringLiteral("module-tree-summary"));
        auto *summaryBody =
            longPanel.findChild<QLabel *>(QStringLiteral("module-tree-summary-text"));
        QTreeWidget *tree = longPanel.tree();
        const int lineSpacing = summary != nullptr ? summary->fontMetrics().lineSpacing() : 0;
        const int scrollBarHeight =
            summary != nullptr ? summary->horizontalScrollBar()->sizeHint().height() : 0;
        const int frameWidth = summary != nullptr ? summary->frameWidth() : 0;
        // R2-b/R2-c: the body paints the FULL text - newlines kept, nothing elided -
        // and that is exactly what summaryText() publishes.
        check("panel.summary.full-text-shown",
              summaryBody != nullptr && summaryBody->text() == longPanel.summaryText()
                  && summaryBody->text().contains(longWarning),
              QStringLiteral("body=%1 chars summary=%2 chars")
                  .arg(summaryBody != nullptr ? summaryBody->text().size() : -1)
                  .arg(longPanel.summaryText().size()));
        // R2-c: the body must be selectable/copyable, not a picture of text. This
        // pins the FLAGS (mouse selection + the focus policy keyboard copy needs);
        // it cannot drive a real Ctrl+C in an offscreen run, which stays a
        // manual/visual item in the report.
        check("panel.summary.selectable",
              summaryBody != nullptr
                  && summaryBody->textInteractionFlags().testFlag(Qt::TextSelectableByMouse)
                  && summaryBody->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard),
              QStringLiteral("flags=0x%1")
                  .arg(static_cast<int>(summaryBody != nullptr
                                            ? summaryBody->textInteractionFlags()
                                            : Qt::NoTextInteraction),
                       0, 16));
        // R2-a: ~2078 characters in a ~600 px wide dock must leave a REAL
        // horizontal range, not a bar that exists and cannot move.
        check("panel.summary.scrollable",
              summary != nullptr && summary->horizontalScrollBar()->maximum() > 0,
              QStringLiteral("maximum=%1 viewport=%2 body=%3")
                  .arg(summary != nullptr ? summary->horizontalScrollBar()->maximum() : -1)
                  .arg(summary != nullptr ? summary->viewport()->width() : -1)
                  .arg(summaryBody != nullptr ? summaryBody->width() : -1));
        check("panel.tree.keeps-height",
              tree != nullptr && tree->viewport()->height() >= 6 * tree->fontMetrics().height(),
              QStringLiteral("viewport=%1 font=%2")
                  .arg(tree != nullptr ? tree->viewport()->height() : -1)
                  .arg(tree != nullptr ? tree->fontMetrics().height() : -1));
        check("panel.summary.tooltip-has-full-text",
              summary != nullptr && summary->toolTip().contains(longWarning),
              QStringLiteral("tooltip=%1 chars")
                  .arg(summary != nullptr ? summary->toolTip().size() : -1));
        // R2-a, replacing R1-c's `panel.summary.single-line` (height <= 2 lines,
        // the shape the user rejected): the height is capped at 3 lines + the
        // reserved bar + slack AND it does not move when the text gets LONGER. The
        // longer text is asserted to have landed, so the equality is not vacuous.
        const int heightBefore = summary != nullptr ? summary->height() : -1;
        ModuleCatalog longerWarned = longWarned;
        longerWarned.warning = longWarning + QStringLiteral(" ") + longWarning;
        longPanel.setCatalog(longerWarned);
        QApplication::processEvents();
        const int heightAfter = summary != nullptr ? summary->height() : -1;
        const int heightCap = 3 * lineSpacing + scrollBarHeight + 2 * frameWidth + lineSpacing / 2;
        check("panel.summary.fixed-height",
              summary != nullptr && heightBefore > 0 && heightAfter == heightBefore
                  && heightAfter >= 2 * lineSpacing && heightAfter <= heightCap
                  && summaryBody != nullptr
                  && summaryBody->text().contains(longerWarned.warning),
              QStringLiteral("height=%1 before=%2 cap=%3 line=%4 bar=%5 body=%6 chars")
                  .arg(heightAfter)
                  .arg(heightBefore)
                  .arg(heightCap)
                  .arg(lineSpacing)
                  .arg(scrollBarHeight)
                  .arg(summaryBody != nullptr ? summaryBody->text().size() : -1));
        // R2-a/R2-b: a summary that carries a newline (`\nroot: …`, `\n<topNote>`)
        // gets THREE lines - never more, never a fold back to one line.
        ModuleCatalog rooted = catalog;
        rooted.projectRoot = QStringLiteral("/proj");
        rooted.warning = longWarning;
        ModuleTreePanel rootedPanel;
        rootedPanel.setCatalog(rooted);
        rootedPanel.resize(620, 360);
        rootedPanel.show();
        QApplication::processEvents();
        auto *rootedSummary =
            rootedPanel.findChild<QScrollArea *>(QStringLiteral("module-tree-summary"));
        auto *rootedBody =
            rootedPanel.findChild<QLabel *>(QStringLiteral("module-tree-summary-text"));
        const int rootedLineSpacing =
            rootedSummary != nullptr ? rootedSummary->fontMetrics().lineSpacing() : 0;
        const int rootedBarHeight = rootedSummary != nullptr
            ? rootedSummary->horizontalScrollBar()->sizeHint().height()
            : 0;
        check("panel.summary.keeps-newlines",
              rootedSummary != nullptr
                  && rootedPanel.summaryText().contains(QStringLiteral("\nroot: /proj"))
                  && rootedBody != nullptr && rootedBody->text().contains(QLatin1Char('\n'))
                  && rootedSummary->height()
                      <= 3 * rootedLineSpacing + rootedBarHeight
                          + 2 * rootedSummary->frameWidth(),
              QStringLiteral("height=%1 cap=%2 root=%3")
                  .arg(rootedSummary != nullptr ? rootedSummary->height() : -1)
                  .arg(3 * rootedLineSpacing + rootedBarHeight)
                  .arg(rootedPanel.summaryText().contains(QStringLiteral("\nroot: /proj"))));
        // The STATUS line is the same container through the same ONE write path;
        // an overlong failure is as unreadable in one elided line as the summary.
        auto *status = longPanel.findChild<QScrollArea *>(QStringLiteral("module-tree-status"));
        longPanel.setStatus(longWarning);
        QApplication::processEvents();
        check("panel.status.scrollable",
              status != nullptr && status->horizontalScrollBar()->maximum() > 0,
              QStringLiteral("maximum=%1")
                  .arg(status != nullptr ? status->horizontalScrollBar()->maximum() : -1));
    }

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

/// batch-20261115 AC-7.1/7.3: the tree's ROOT follows the user's top.
void checkTopScopedRoot()
{
    ModuleCatalog catalog;
    catalog.valid = true;
    catalog.tool = QStringLiteral("slang");
    // A instantiates B; C is independent. Auto => A and C are both roots.
    catalog.modules = {
        definition(QStringLiteral("A"), QStringLiteral("/p/a.sv"), 0, 0, /*isTop=*/true,
                   {}, {[] {
                       ModuleInstance instance;
                       instance.module = QStringLiteral("B");
                       instance.path = QStringLiteral("A.u_b");
                       instance.file = QStringLiteral("/p/a.sv");
                       return instance;
                   }()}),
        definition(QStringLiteral("B"), QStringLiteral("/p/b.sv"), 0, 0, /*isTop=*/false),
        definition(QStringLiteral("C"), QStringLiteral("/p/c.sv"), 0, 0, /*isTop=*/true),
    };
    const QVector<ModuleTreeNode> autoRoots = buildModuleTree(catalog, ModuleTreeOptions{});
    check("ac7.1.auto-roots", autoRoots.size() == 2,
          QString::number(autoRoots.size()));

    // AC-7.1: an explicit top re-roots the tree to THAT module's subtree.
    catalog.top = QStringLiteral("B");
    const QVector<ModuleTreeNode> scopedRoots = buildModuleTree(catalog, ModuleTreeOptions{});
    check("ac7.1.scoped-single-root", scopedRoots.size() == 1,
          QString::number(scopedRoots.size()));
    check("ac7.1.scoped-root-is-top",
          scopedRoots.size() == 1 && scopedRoots.first().moduleName == QStringLiteral("B"),
          scopedRoots.isEmpty() ? QString() : scopedRoots.first().moduleName);
    check("ac7.1.summary-names-top",
          catalog.summaryLine().contains(QStringLiteral("top: B")),
          catalog.summaryLine());

    // AC-7.3: a top the catalog does not define FALLS BACK to is_top instead of
    // producing an empty tree, and never crashes.
    catalog.top = QStringLiteral("NOPE");
    const QVector<ModuleTreeNode> fallbackRoots = buildModuleTree(catalog, ModuleTreeOptions{});
    check("ac7.3.unknown-top-falls-back", fallbackRoots.size() == 2,
          QString::number(fallbackRoots.size()));
}

/// batch-20261115 AC-3.1: the one-level module closure the diagnostics lane
/// consumes (`vedit.moduleTree.closure`). The holder only publishes this map, so
/// the CONTENT rule is asserted here on a synthetic catalog: top.sv -> sub.sv,
/// NEVER the grandchild, plus the empty-entry and invalid-catalog cases.
void checkModuleClosure()
{
    ModuleCatalog catalog;
    catalog.valid = true;
    // `top` (top.sv) instantiates `sub` (sub.sv) and `sibling` (same file);
    // `sub` instantiates `grand` (grand.sv) and an undefined `missing`. The
    // doubled slash on top.sv's own path is there on purpose: the closure keys
    // are cleanPath-normalised because the consumer keys whole documents the
    // same way, and an un-normalised key would silently match nothing.
    ModuleDefinition top = definition(QStringLiteral("top"), QStringLiteral("/p//top.sv"), 0, 0,
                                      /*isTop=*/true);
    top.children = {
        [] {
            ModuleInstance instance;
            instance.module = QStringLiteral("sub");
            instance.file = QStringLiteral("/p/top.sv");
            return instance;
        }(),
        [] {
            ModuleInstance instance;
            instance.module = QStringLiteral("sibling");
            instance.file = QStringLiteral("/p/top.sv");
            return instance;
        }(),
    };
    ModuleDefinition sub = definition(QStringLiteral("sub"), QStringLiteral("/p/sub.sv"), 0, 0,
                                      /*isTop=*/false);
    sub.children = {
        [] {
            ModuleInstance instance;
            instance.module = QStringLiteral("grand");
            instance.file = QStringLiteral("/p/sub.sv");
            return instance;
        }(),
        [] {
            ModuleInstance instance;
            instance.module = QStringLiteral("missing");
            instance.file = QStringLiteral("/p/sub.sv");
            return instance;
        }(),
    };
    catalog.modules = {top, sub,
                       definition(QStringLiteral("sibling"), QStringLiteral("/p/top.sv"), 0, 0,
                                  /*isTop=*/false),
                       definition(QStringLiteral("grand"), QStringLiteral("/p/grand.sv"), 0, 0,
                                  /*isTop=*/false)};

    const QHash<QString, QStringList> closure = buildModuleClosure(catalog);
    const QString topKey = QStringLiteral("/p/top.sv");
    const QString subKey = QStringLiteral("/p/sub.sv");
    const QString grandKey = QStringLiteral("/p/grand.sv");
    check("closure.direct-child", closure.value(topKey) == QStringList{subKey},
          QStringLiteral("top -> [%1]").arg(closure.value(topKey).join(QLatin1Char(','))));
    check("closure.no-grandchild", !closure.value(topKey).contains(grandKey),
          QStringLiteral("top -> [%1]").arg(closure.value(topKey).join(QLatin1Char(','))));
    check("closure.grandchild-under-sub", closure.value(subKey) == QStringList{grandKey},
          QStringLiteral("sub -> [%1]").arg(closure.value(subKey).join(QLatin1Char(','))));
    check("closure.same-file-edge-skipped",
          !closure.value(topKey).contains(topKey)
              && !closure.value(subKey).contains(subKey),
          QStringLiteral("top=[%1] sub=[%2]")
              .arg(closure.value(topKey).join(QLatin1Char(',')),
                   closure.value(subKey).join(QLatin1Char(','))));
    check("closure.keys-cleaned", closure.contains(topKey) && !closure.contains(QStringLiteral("/p//top.sv")),
          QStringLiteral("keys=[%1]").arg(QStringList(closure.keys()).join(QLatin1Char(','))));
    check("closure.childless-file-has-empty-entry",
          closure.contains(grandKey) && closure.value(grandKey).isEmpty(),
          QStringLiteral("grand=[%1]").arg(closure.value(grandKey).join(QLatin1Char(','))));
    check("closure.modules-counted", closure.size() == 3,
          QStringLiteral("files=%1").arg(closure.size()));
    check("closure.invalid-catalog-empty", buildModuleClosure(ModuleCatalog{}).isEmpty());
}

// --- AC M1.1 / M1.5: the failure and large-project RULES --------------------
//
// These are the decisions the fix hangs on, so they are asserted as pure
// functions: a timeout must never be classified as an unknown top (that
// classification is what cleared the persisted top and re-ran the whole
// project), and a large file set must not start an automatic catalog.
void checkFailureAndLargeProjectRules()
{
    check("failure.unknown-top-code-is-typed",
          catalogFailureKind(10) == CatalogFailureKind::UnknownTop);
    check("failure.timeout-is-not-unknown-top",
          catalogFailureKind(9) == CatalogFailureKind::Timeout
              && !mayFallBackToAuto(CatalogFailureKind::Timeout),
          QStringLiteral("kind=%1").arg(static_cast<int>(catalogFailureKind(9))));
    check("failure.cancelled-is-its-own-class",
          catalogFailureKind(4) == CatalogFailureKind::Cancelled);
    check("failure.tool-error-is-the-default",
          catalogFailureKind(3) == CatalogFailureKind::ToolError
              && catalogFailureKind(0) == CatalogFailureKind::ToolError
              && catalogFailureKind(999) == CatalogFailureKind::ToolError);
    check("failure.only-unknown-top-falls-back",
          mayFallBackToAuto(CatalogFailureKind::UnknownTop)
              && !mayFallBackToAuto(CatalogFailureKind::Timeout)
              && !mayFallBackToAuto(CatalogFailureKind::Cancelled)
              && !mayFallBackToAuto(CatalogFailureKind::ToolError));

    check("large.threshold-is-5000", kLargeProjectFileThreshold == 5000);
    check("large.automatic-run-withheld",
          !shouldAutoCatalog(6000, /*hasTop=*/false, kLargeProjectFileThreshold,
                             /*userRequested=*/false));
    check("large.named-top-runs",
          shouldAutoCatalog(6000, /*hasTop=*/true, kLargeProjectFileThreshold, false));
    check("large.explicit-refresh-runs",
          shouldAutoCatalog(6000, /*hasTop=*/false, kLargeProjectFileThreshold,
                            /*userRequested=*/true));
    check("large.small-project-runs",
          shouldAutoCatalog(1200, false, kLargeProjectFileThreshold, false));
    check("large.threshold-zero-disables-the-gate", shouldAutoCatalog(60000, false, 0, false));
    const QString hint = largeProjectHint(6123);
    check("large.hint-names-the-count-and-both-ways-forward",
          hint.contains(QStringLiteral("6123")) && hint.contains(QStringLiteral("Refresh")),
          hint);
}

// --- AC M1.3 / M1.4 / M1.6: the top input and the Cancel button ------------
void checkPanelGestures()
{
    ModuleTreePanel panel;
    panel.resize(900, 600);
    panel.show();
    QCoreApplication::processEvents();
    auto *combo = panel.findChild<QComboBox *>(QStringLiteral("module-tree-top"));
    auto *cancel = panel.findChild<QToolButton *>(QStringLiteral("module-tree-cancel"));
    check("gesture.top-combo-exists", combo != nullptr && combo->isEditable());
    check("gesture.cancel-exists", cancel != nullptr);
    check("gesture.cancel-starts-disabled",
          cancel != nullptr && !cancel->isEnabled() && !panel.cancelEnabled());

    int topChoices = 0;
    QString lastTop;
    panel.setOnTopChanged([&topChoices, &lastTop](const QString &top) {
        ++topChoices;
        lastTop = top;
    });
    int cancels = 0;
    panel.setOnCancel([&cancels]() { ++cancels; });

    combo->lineEdit()->setText(QStringLiteral("my_top"));
    QCoreApplication::processEvents();
    check("gesture.typing-does-not-start-a-run", topChoices == 0,
          QStringLiteral("choices=%1").arg(topChoices));
    // The old code also committed on `editingFinished`, i.e. when the field lost
    // focus — that is the "click anywhere and a whole-project catalog starts"
    // symptom. Move the focus away and assert nothing happened.
    auto *filter = panel.findChild<QLineEdit *>(QStringLiteral("module-tree-filter"));
    if (filter != nullptr) {
        filter->setFocus();
    }
    QCoreApplication::processEvents();
    check("gesture.focus-loss-does-not-start-a-run", topChoices == 0,
          QStringLiteral("choices=%1").arg(topChoices));
    QTest::keyClick(combo->lineEdit(), Qt::Key_Return);
    QCoreApplication::processEvents();
    check("gesture.enter-starts-one-run",
          topChoices == 1 && lastTop == QStringLiteral("my_top"),
          QStringLiteral("choices=%1 top='%2'").arg(topChoices).arg(lastTop));

    panel.setCancelEnabled(true);
    QTest::mouseClick(cancel, Qt::LeftButton);
    QCoreApplication::processEvents();
    check("gesture.cancel-click-invokes-the-handler", cancels == 1,
          QStringLiteral("cancels=%1").arg(cancels));
    panel.setCancelEnabled(false);
    check("gesture.cancel-disabled-again", !panel.cancelEnabled());
}

// --- AC M1.8: a 20k-module project's selection/open path -------------------
//
// The measured defects: every selection ran the focus rule over ALL modules
// (`ModuleCatalog::find` + `moduleAtCaret`) and the caret rule paid two realpath
// syscalls per module (40k on a 20k project), plus the port table was rebuilt on
// every push. This test drives the REAL panels through the same wiring the
// integration uses and asserts the counts and the p95 budget.
void checkOpenLatency20k()
{
    constexpr int kModules = 20000;
    constexpr int kFiles = 200;
    ModuleCatalog catalog;
    catalog.valid = true;
    catalog.tool = QStringLiteral("slang");
    catalog.fileSetSize = kFiles;
    catalog.modules.reserve(kModules);
    for (int index = 0; index < kModules; ++index) {
        const QString file = QStringLiteral("/proj/src/file%1.sv")
                                 .arg(index % kFiles, 3, 10, QLatin1Char('0'));
        catalog.modules.append(definition(QStringLiteral("mod%1").arg(index, 5, 10, QLatin1Char('0')),
                                          file, index % 200, 0, /*isTop=*/true,
                                          {port(QStringLiteral("clk"), QStringLiteral("In"),
                                                QStringLiteral("logic"), file, 1, 0),
                                           port(QStringLiteral("q"), QStringLiteral("Out"),
                                                QStringLiteral("logic[3:0]"), file, 2, 0)}));
    }

    ModuleTreePanel treePanel;
    treePanel.resize(1200, 800);
    treePanel.show();
    InterfacePanel interfacePanel;
    interfacePanel.resize(600, 400);
    interfacePanel.show();
    // Exactly the integration's wiring: a tree selection is pushed to the
    // interface panel (focus rule (a)); the caret context is pushed as well (rule
    // (b)) and must not cost a full scan.
    treePanel.setOnSelectionChanged([&interfacePanel](const QString &moduleName) {
        interfacePanel.setTreeSelection(moduleName);
    });
    QCoreApplication::processEvents();

    QElapsedTimer buildTimer;
    buildTimer.start();
    treePanel.setCatalog(catalog);
    interfacePanel.setCatalog(catalog);
    const qint64 buildMs = buildTimer.elapsed();
    check("scale.tree-built", treePanel.tree()->topLevelItemCount() == kModules,
          QStringLiteral("rows=%1").arg(treePanel.tree()->topLevelItemCount()));
    // Idempotent: a second call (a second panel, a re-attached dock) must not pay
    // for a second scan of 20k modules.
    catalog.buildIndex();
    catalog.buildIndex();
    check("scale.catalog-index-built-once", catalog.indexBuilds() == 1,
          QStringLiteral("builds=%1").arg(catalog.indexBuilds()));

    jump::Registry::instance().reset();
    RecordingTarget target;
    jump::setTarget(&target);

    resetModuleTreeProbe();
    const int rebuildsAfterBuild = moduleTreeProbe().treeRebuilds;
    QVector<qint64> selectionSamples;
    QVector<qint64> openSamples;
    selectionSamples.reserve(200);
    openSamples.reserve(200);
    for (int iteration = 0; iteration < 200; ++iteration) {
        const int index = (iteration * 97) % kModules;
        QTreeWidgetItem *item = treePanel.tree()->topLevelItem(index);
        QElapsedTimer timer;
        timer.start();
        treePanel.tree()->setCurrentItem(item);
        QCoreApplication::processEvents();
        selectionSamples.append(timer.nsecsElapsed());
        timer.restart();
        // "open": the double-click path (jump dispatch included).
        treePanel.activateItem(item);
        openSamples.append(timer.nsecsElapsed());
    }
    const auto percentile = [](QVector<qint64> samples, double fraction) {
        std::sort(samples.begin(), samples.end());
        const int last = static_cast<int>(samples.size()) - 1;
        const int at = std::min(last, static_cast<int>(fraction * static_cast<double>(samples.size())));
        return samples.at(at);
    };
    const qint64 selectionP95Ms = percentile(selectionSamples, 0.95) / 1000000;
    const qint64 openP95Ms = percentile(openSamples, 0.95) / 1000000;
    check("scale.selection-p95-under-500ms", selectionP95Ms < 500,
          QStringLiteral("p95=%1ms build=%2ms").arg(selectionP95Ms).arg(buildMs));
    check("scale.open-p95-under-500ms", openP95Ms < 500,
          QStringLiteral("p95=%1ms").arg(openP95Ms));
    check("scale.selection-never-rebuilds-the-tree",
          moduleTreeProbe().treeRebuilds == rebuildsAfterBuild,
          QStringLiteral("rebuilds=%1 (after catalog=%2)")
              .arg(moduleTreeProbe().treeRebuilds)
              .arg(rebuildsAfterBuild));
    check("scale.selection-never-copies-the-catalog",
          moduleTreeProbe().catalogCopies == 0,
          QStringLiteral("copies=%1").arg(moduleTreeProbe().catalogCopies));
    check("scale.port-table-rendered-only-on-a-focus-change",
          moduleTreeProbe().portRenders > 0 && moduleTreeProbe().portRenders <= 200,
          QStringLiteral("renders=%1").arg(moduleTreeProbe().portRenders));
    check("scale.open-jumps-delivered", target.calls == 200,
          QStringLiteral("jumps=%1").arg(target.calls));

    // The caret rule: 100 lookups over the SAME files the catalog uses. The old
    // implementation resolved two paths per module (40k syscalls per lookup).
    resetModuleTreeProbe();
    const int iterations = 100;
    interfacePanel.setTreeSelection(QString());
    QElapsedTimer caretTimer;
    caretTimer.start();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const QString file = QStringLiteral("/proj/src/file%1.sv")
                                 .arg(iteration % kFiles, 3, 10, QLatin1Char('0'));
        interfacePanel.setCaretContext(file, iteration % 200);
    }
    const qint64 caretMs = caretTimer.elapsed();
    check("scale.caret-lookup-does-not-resolve-per-module",
          moduleTreeProbe().canonicalPathCalls <= 2 * iterations,
          QStringLiteral("canonicalCalls=%1 lookups=%2")
              .arg(moduleTreeProbe().canonicalPathCalls)
              .arg(iterations));
    check("scale.caret-lookups-p95-budget",
          caretMs < 500LL * (iterations / 20 + 1),
          QStringLiteral("%1ms for %2 lookups").arg(caretMs).arg(iterations));
    // The focus rule still finds the enclosing module: file 042's module 42 lives
    // at line 42 % 200 = 42, so a caret below it focuses that module.
    interfacePanel.setCaretContext(QStringLiteral("/proj/src/file042.sv"), 60);
    interfacePanel.setCaretContext(QStringLiteral("/proj/src/file042.sv"), 61);
    check("scale.caret-focus-resolves", !interfacePanel.focusedModuleName().isEmpty(),
          interfacePanel.focusedModuleName());
    jump::setTarget(nullptr);
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    checkModuleHierarchy();
    checkWidthsAndFocus();
    checkPanels();
    checkToolchainWiring();
    checkTopScopedRoot();
    checkModuleClosure();
    checkFailureAndLargeProjectRules();
    checkPanelGestures();
    checkOpenLatency20k();
    std::printf("%s: %d check(s), %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
