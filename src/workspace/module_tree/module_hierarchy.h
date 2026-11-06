#pragma once

// Pure model behind the module-tree panel (UX batch 2, item 1).
//
// One `ModuleCatalog` becomes a tree of MODULES: the roots are the catalog's
// `is_top` entries (modules no other module instantiates; one or several), and
// the children of a module are the modules it instantiates. A module
// instantiated by two parents therefore appears under BOTH parents - the tree is
// a module hierarchy, not an elaborated instance tree.
//
// There are deliberately NO net/driver/load/chain rows in this model: the user
// ruling for this batch removed them, not hid them. Only three non-module row
// kinds exist and each one is a visible guard marker:
//   * a cycle marker where an instantiation would close a loop,
//   * a depth-limit marker at the 64-level cap,
//   * a truncation marker when the row budget is exhausted.
//
// No Qt widget and no C ABI dependency, so the self-test can check the whole
// decision surface (root set, duplicate parents, cycle, depth cap) without slang.
#include "module_catalog.h"

#include <QString>
#include <QVector>

namespace vedit::workspace::module_tree {

/// Cap the user ruling asks for: a module nested deeper than this is not
/// expanded; the panel shows a marker instead of recursing (and never crashes).
constexpr int kModuleTreeMaxDepth = 64;
/// A second safety bound: a module DAG can fan out exponentially even within the
/// depth cap. The tree stops at this many rows and shows one truncation marker.
constexpr int kModuleTreeMaxRows = 20000;

enum class ModuleRowKind {
    Module,     ///< one module definition occurrence
    Cycle,      ///< an instantiation that would close a cycle (visible marker)
    DepthLimit, ///< the 64-level cap marker
    Truncated,  ///< the row-budget marker
};

/// One row of the module tree.
struct ModuleTreeNode {
    ModuleRowKind kind = ModuleRowKind::Module;
    QString moduleName;
    QString instancePath; ///< elaborated instance path of this occurrence
    QString definitionFile;
    int definitionLine = -1;   ///< 0-based
    int definitionColumn = -1; ///< 0-based
    bool isTop = false;
    /// How many instantiation sites of this module the parent body contains
    /// (roots: 0). A generate loop collapses onto one position, so this is the
    /// honest "examples under this parent" count.
    int instantiationCount = 0;
    /// Human marker/annotation (cycle, depth cap, not catalogued), or empty.
    QString note;
    QVector<ModuleTreeNode> children;
};

struct ModuleTreeOptions {
    int maxDepth = kModuleTreeMaxDepth;
    int maxRows = kModuleTreeMaxRows;
};

/// Builds the pure module hierarchy. Roots are exactly the `is_top` modules (in
/// catalog order); children preserve instantiation order grouped by child module
/// name (one row per distinct child module, `instantiationCount` per row so two
/// instances of the same module under one parent do not produce duplicate rows).
QVector<ModuleTreeNode> buildModuleTree(const ModuleCatalog &catalog,
                                       const ModuleTreeOptions &options = {});

/// Total rows of a tree (markers included); the test's "row set" helper.
int countModuleTreeRows(const QVector<ModuleTreeNode> &roots);

/// Depth-first list of every node, in display order (markers included).
QVector<const ModuleTreeNode *> flattenModuleTree(const QVector<ModuleTreeNode> &roots);

} // namespace vedit::workspace::module_tree
