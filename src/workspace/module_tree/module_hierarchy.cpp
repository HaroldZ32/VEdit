#include "module_hierarchy.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <utility>

namespace vedit::workspace::module_tree {

namespace {

struct BuildState {
    const ModuleCatalog *catalog = nullptr;
    int maxRows = kModuleTreeMaxRows;
    int maxDepth = kModuleTreeMaxDepth;
    int rows = 0;
    bool truncated = false;
};

ModuleTreeNode markerNode(ModuleRowKind kind, const QString &moduleName, const QString &path,
                          const QString &note)
{
    ModuleTreeNode node;
    node.kind = kind;
    node.moduleName = moduleName;
    node.instancePath = path;
    node.note = note;
    return node;
}

/// One distinct child module of a parent body, with the first instance path and
/// how many instantiation sites the parent contains.
struct ChildGroup {
    QString moduleName;
    QString instancePath;
    int count = 0;
};

QVector<ChildGroup> childGroups(const ModuleDefinition &parent)
{
    QVector<ChildGroup> groups;
    QHash<QString, int> indexByName;
    for (const ModuleInstance &instance : parent.children) {
        if (instance.module.isEmpty()) {
            continue;
        }
        const auto found = indexByName.constFind(instance.module);
        if (found == indexByName.constEnd()) {
            ChildGroup group;
            group.moduleName = instance.module;
            group.instancePath = instance.path.isEmpty()
                                     ? parent.name + QLatin1Char('.') + instance.module
                                     : instance.path;
            group.count = 1;
            indexByName.insert(instance.module, groups.size());
            groups.append(group);
        } else {
            ChildGroup &group = groups[found.value()];
            ++group.count;
            if (group.instancePath.isEmpty()) {
                group.instancePath = instance.path;
            }
        }
    }
    return groups;
}

ModuleTreeNode buildNode(const ModuleDefinition &definition, const QString &instancePath,
                         int depth, QSet<QString> ancestors, int instantiationCount,
                         BuildState &state)
{
    ++state.rows;
    ModuleTreeNode node;
    node.kind = ModuleRowKind::Module;
    node.moduleName = definition.name;
    node.instancePath = instancePath;
    node.definitionFile = definition.file;
    node.definitionLine = definition.line;
    node.definitionColumn = definition.column;
    node.isTop = definition.isTop;
    node.instantiationCount = instantiationCount;

    if (depth >= state.maxDepth) {
        if (!definition.children.isEmpty()) {
            node.children.append(markerNode(
                ModuleRowKind::DepthLimit, definition.name, instancePath,
                QCoreApplication::translate(
                    "VEdit", "depth limit %1 reached — this module's children are not expanded")
                    .arg(state.maxDepth)));
        }
        return node;
    }

    ancestors.insert(definition.name);
    const QVector<ChildGroup> groups = childGroups(definition);
    for (const ChildGroup &group : groups) {
        if (state.rows >= state.maxRows) {
            state.truncated = true;
            node.children.append(markerNode(
                ModuleRowKind::Truncated, QString(), instancePath,
                QCoreApplication::translate("VEdit",
                                            "the module tree was truncated at %1 rows")
                    .arg(state.maxRows)));
            break;
        }
        if (ancestors.contains(group.moduleName)) {
            ++state.rows;
            node.children.append(
                markerNode(ModuleRowKind::Cycle, group.moduleName, group.instancePath,
                           QCoreApplication::translate(
                               "VEdit",
                               "cycle: %1 is already an ancestor of this row — not expanded")
                               .arg(group.moduleName)));
            continue;
        }
        const ModuleDefinition *child = state.catalog->find(group.moduleName);
        if (child == nullptr) {
            // The ABI can name a child module whose definition slang did not
            // catalogue (e.g. an unresolved module that still elaborated). Show
            // it, but never pretend it has a definition to jump to.
            ++state.rows;
            ModuleTreeNode missing;
            missing.moduleName = group.moduleName;
            missing.instancePath = group.instancePath;
            missing.instantiationCount = group.count;
            missing.note = QCoreApplication::translate(
                               "VEdit", "no module definition was catalogued for `%1`")
                               .arg(group.moduleName);
            node.children.append(missing);
            continue;
        }
        node.children.append(buildNode(*child, group.instancePath, depth + 1, ancestors,
                                       group.count, state));
    }
    return node;
}

} // namespace

QVector<ModuleTreeNode> buildModuleTree(const ModuleCatalog &catalog,
                                       const ModuleTreeOptions &options)
{
    QVector<ModuleTreeNode> roots;
    BuildState state;
    state.catalog = &catalog;
    state.maxRows = options.maxRows > 0 ? options.maxRows : kModuleTreeMaxRows;
    state.maxDepth = options.maxDepth > 0 ? options.maxDepth : kModuleTreeMaxDepth;

    for (const ModuleDefinition &module : catalog.modules) {
        if (!module.isTop) {
            continue;
        }
        if (state.rows >= state.maxRows) {
            state.truncated = true;
            roots.append(markerNode(ModuleRowKind::Truncated, QString(), QString(),
                                    QCoreApplication::translate(
                                        "VEdit", "the module tree was truncated at %1 rows")
                                        .arg(state.maxRows)));
            break;
        }
        roots.append(buildNode(module, module.name, 1, {}, 0, state));
    }
    return roots;
}

int countModuleTreeRows(const QVector<ModuleTreeNode> &roots)
{
    int total = 0;
    const auto visit = [&total](const ModuleTreeNode &node, const auto &self) -> void {
        ++total;
        for (const ModuleTreeNode &child : node.children) {
            self(child, self);
        }
    };
    for (const ModuleTreeNode &root : roots) {
        visit(root, visit);
    }
    return total;
}

QVector<const ModuleTreeNode *> flattenModuleTree(const QVector<ModuleTreeNode> &roots)
{
    QVector<const ModuleTreeNode *> flat;
    const auto visit = [&flat](const ModuleTreeNode &node, const auto &self) -> void {
        flat.append(&node);
        for (const ModuleTreeNode &child : node.children) {
            self(child, self);
        }
    };
    for (const ModuleTreeNode &root : roots) {
        visit(root, visit);
    }
    return flat;
}

} // namespace vedit::workspace::module_tree
