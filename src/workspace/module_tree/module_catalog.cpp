#include "module_catalog.h"

// G1-a: the ONE path normalisation (see `canonicalDirectoryVariant` below). The
// header is Qt-only free functions - no link edge between the two lanes.
#include "../diagnostics/realtime_file_scope.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
// Only for the compile-time pin of the mirrored `VD_ERR_*` codes.
#include <vd_ffi.h>
#endif

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace vedit::workspace::module_tree {

namespace {

QString canonicalOrAbsolute(const QString &path)
{
    if (path.isEmpty()) {
        return QString();
    }
    ++moduleTreeProbe().canonicalPathCalls;
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

/// The canonical DIRECTORY of `path` (one syscall) with the file name appended.
/// Used when a direct key misses: a project opened through a symlinked parent
/// (`/home/x/proj -> /data/proj`) makes the editor and the catalog disagree on
/// the prefix only, so substituting the directory is enough to match them
/// WITHOUT resolving every module's path (AC M1.8).
///
/// G1-a (contract §10) has ONE normalisation for the whole product: the module
/// tree's caret/closure key and the diagnostics lane's golden/shadowed
/// membership must not answer different spellings for the same file, so this
/// delegates to `realtime_scope::canonicalDirectoryVariant()` instead of keeping
/// a second copy with its own parent-resolution rule. The probe counter stays
/// here: the syscall is still made by this lane on that call, and the counter is
/// what the module-tree selftest asserts.
QString canonicalDirectoryVariant(const QString &path)
{
    if (path.isEmpty()) {
        return QString();
    }
    ++moduleTreeProbe().canonicalPathCalls;
    return diagnostics::realtime_scope::canonicalDirectoryVariant(path);
}

} // namespace

ModuleCatalog::ModuleCatalog(const ModuleCatalog &other)
{
    *this = other;
}

ModuleCatalog &ModuleCatalog::operator=(const ModuleCatalog &other)
{
    if (this == &other) {
        return *this;
    }
    ++moduleTreeProbe().catalogCopies;
    projectRoot = other.projectRoot;
    tool = other.tool;
    elapsedMs = other.elapsedMs;
    peakRssKb = other.peakRssKb;
    warning = other.warning;
    top = other.top;
    reachableFiles = other.reachableFiles;
    outsideModules = other.outsideModules;
    shadowedFiles = other.shadowedFiles;
    shadowedWarning = other.shadowedWarning;
    fileSetSize = other.fileSetSize;
    modules = other.modules;
    valid = other.valid;
    error = other.error;
    // The index travels with the value: a copy shares it through Qt's implicit
    // sharing, so a panel never rebuilds it.
    fileIndex_ = other.fileIndex_;
    nameIndex_ = other.nameIndex_;
    duplicateNotes_ = other.duplicateNotes_;
    indexReady_ = other.indexReady_;
    indexBuilds_ = other.indexBuilds_;
    return *this;
}

void ModuleCatalog::buildIndex() const
{
    if (indexReady_) {
        return;
    }
    ++indexBuilds_;
    nameIndex_.reserve(modules.size());
    fileIndex_.reserve(modules.size());
    for (int index = 0; index < modules.size(); ++index) {
        const ModuleDefinition &module = modules.at(index);
        if (!module.name.isEmpty()) {
            const auto existing = nameIndex_.constFind(module.name);
            if (existing == nameIndex_.constEnd()) {
                nameIndex_.insert(module.name, index);
            } else {
                // batch-20261118 B: the service already resolves a duplicate
                // module deterministically and sends its warning, so this is the
                // DEFENSIVE layer. `find()` keeps the FIRST definition (the
                // model's order is the ABI's order, never a hash order) and the
                // note names both files, so nothing is dropped silently.
                const QString winner = modules.at(existing.value()).file;
                duplicateNotes_.append(
                    QCoreApplication::translate(
                        "VEdit",
                        "module '%1' is defined in both %2 and %3; using %4")
                        .arg(module.name, module.file, winner, winner));
            }
        }
        const QString file = QDir::cleanPath(module.file);
        if (!file.isEmpty()) {
            fileIndex_[file].append(index);
        }
    }
    indexReady_ = true;
}

const ModuleDefinition *ModuleCatalog::find(const QString &name) const
{
    if (name.isEmpty()) {
        return nullptr;
    }
    if (!indexReady_) {
        // A caller that never built the index (a test, a panel that only asks
        // once) still gets O(1) after the first lookup instead of a full scan
        // per call.
        buildIndex();
    }
    const auto it = nameIndex_.constFind(name);
    return it == nameIndex_.constEnd() ? nullptr : &modules.at(it.value());
}

QStringList ModuleCatalog::topModuleNames() const
{
    QStringList names;
    for (const ModuleDefinition &module : modules) {
        if (module.isTop) {
            names.append(module.name);
        }
    }
    return names;
}

QString ModuleCatalog::summaryLine() const
{
    if (!error.isEmpty()) {
        return QCoreApplication::translate("VEdit", "module catalog: %1").arg(error);
    }
    if (!valid) {
        return QCoreApplication::translate("VEdit", "no module catalog yet");
    }
    int tops = 0;
    for (const ModuleDefinition &module : modules) {
        if (module.isTop) {
            ++tops;
        }
    }
    QString line = QCoreApplication::translate("VEdit",
                                               "%1 · %2 module(s) · %3 top(s) · %4 ms · %5 KiB "
                                               "peak")
                       .arg(tool.isEmpty() ? QStringLiteral("slang") : tool)
                       .arg(modules.size())
                       .arg(tops)
                       .arg(elapsedMs)
                       .arg(peakRssKb);
    if (!warning.isEmpty()) {
        line += QCoreApplication::translate("VEdit", " · ⚠ %1").arg(warning);
    }
    // G1-c: the consequence sentence the service publishes BESIDE the duplicate
    // warning (which stays byte-for-byte frozen): the files named there left the
    // compiler input because EVERY module they define lost the duplicate pick.
    //
    // It is deliberately NOT a claim that `slang` can never pick a second winner.
    // The exclusion is FILE-granular: a losing file that still declares a unique
    // module, an interface or a package is KEPT (dropping it would delete a
    // definition from the design - see `shadowed_file_set` on the Rust side), and
    // the duplicate module in that file still travels to the compiler inside it.
    // In that shape the catalog remains the one authority for the MODULE TREE and
    // the project pass's file set, but the compiler still sees the collision and
    // resolves it by its own rule. The residue is named here, in
    // `realtime_file_scope.h` and in the batch report - not papered over by a
    // sentence the behaviour does not support.
    if (!shadowedWarning.isEmpty()) {
        line += QCoreApplication::translate("VEdit", " · ⚠ %1").arg(shadowedWarning);
    }
    const QStringList duplicates = duplicateNotes();
    if (!duplicates.isEmpty()) {
        line += QCoreApplication::translate("VEdit", " · ⚠ %1")
                    .arg(duplicates.join(QStringLiteral("; ")));
    }
    if (!top.isEmpty()) {
        line += QCoreApplication::translate("VEdit", " · top: %1").arg(top);
    }
    return line;
}

QStringList ModuleCatalog::duplicateNotes() const
{
    if (!indexReady_) {
        buildIndex();
    }
    return duplicateNotes_;
}

ModuleJump jumpFromPosition(const QString &file, int line, int column)
{
    ModuleJump jump;
    if (file.isEmpty() || line < 0) {
        return jump;
    }
    jump.file = file;
    jump.line = line + 1;
    jump.column = column >= 0 ? column + 1 : 1;
    return jump;
}

ModuleJump jumpFromDefinition(const ModuleDefinition &module)
{
    return jumpFromPosition(module.file, module.line, module.column);
}

ModuleJump jumpFromPort(const ModulePort &port)
{
    return jumpFromPosition(port.file, port.line, port.column);
}

ModuleJump jumpFromInstance(const ModuleInstance &instance)
{
    return jumpFromPosition(instance.file, instance.line, instance.column);
}

QString describePosition(const QString &file, int line, int column)
{
    if (file.isEmpty() || line < 0) {
        return QStringLiteral("-");
    }
    const QString name = QFileInfo(file).fileName();
    return QStringLiteral("%1:%2:%3").arg(name).arg(line + 1).arg(column + 1);
}

PortWidth portWidthFromType(const QString &type)
{
    PortWidth answer;
    const QString text = type.trimmed();
    if (text.isEmpty()) {
        answer.problem = QCoreApplication::translate(
            "VEdit", "the ABI reported no type string for this port");
        return answer;
    }

    // One literal packed dimension is `[<integer> : <integer>]`; a range that
    // mentions a parameter (`[WIDTH-1:0]`) is deliberately NOT evaluated here:
    // the panel must not invent a width the source does not literally state.
    static const QRegularExpression rangePattern(
        QStringLiteral("^\\s*([+-]?\\d+)\\s*:\\s*([+-]?\\d+)\\s*$"));

    qlonglong product = 1;
    int dimensions = 0;
    int cursor = 0;
    while (true) {
        const int open = text.indexOf(QLatin1Char('['), cursor);
        if (open < 0) {
            break;
        }
        const int close = text.indexOf(QLatin1Char(']'), open + 1);
        if (close < 0) {
            answer.problem = QCoreApplication::translate(
                                 "VEdit", "the type `%1` has an unclosed packed dimension")
                                 .arg(text);
            return answer;
        }
        const QString body = text.mid(open + 1, close - open - 1);
        const QRegularExpressionMatch match = rangePattern.match(body);
        if (!match.hasMatch()) {
            answer.problem = QCoreApplication::translate(
                                 "VEdit",
                                 "the packed range `[%1]` in `%2` is not a literal msb:lsb "
                                 "pair, so the width cannot be derived")
                                 .arg(body.trimmed(), text);
            return answer;
        }
        bool msbOk = false;
        bool lsbOk = false;
        const qlonglong msb = match.captured(1).toLongLong(&msbOk);
        const qlonglong lsb = match.captured(2).toLongLong(&lsbOk);
        if (!msbOk || !lsbOk) {
            answer.problem = QCoreApplication::translate(
                                 "VEdit", "the packed range `[%1]` in `%2` is too large to "
                                          "represent")
                                 .arg(body.trimmed(), text);
            return answer;
        }
        const qlonglong dimension = std::llabs(msb - lsb) + 1;
        if (dimension <= 0 || product > static_cast<qlonglong>(INT_MAX) / dimension) {
            answer.problem = QCoreApplication::translate(
                                 "VEdit", "the packed dimensions in `%1` exceed the supported "
                                          "width")
                                 .arg(text);
            return answer;
        }
        product *= dimension;
        ++dimensions;
        cursor = close + 1;
    }

    answer.known = true;
    answer.width = dimensions == 0 ? 1 : static_cast<int>(product);
    return answer;
}

bool sameFile(const QString &left, const QString &right)
{
    if (left.isEmpty() || right.isEmpty()) {
        return false;
    }
    const QString a = canonicalOrAbsolute(left);
    const QString b = canonicalOrAbsolute(right);
    return !a.isEmpty() && a == b;
}

const ModuleDefinition *moduleAtCaret(const ModuleCatalog &catalog, const QString &file,
                                      int caretLine0)
{
    if (file.isEmpty() || caretLine0 < 0) {
        return nullptr;
    }
    catalog.buildIndex();
    // The index is keyed by the CLEAN spelling of the catalog's own paths; the
    // editor's spelling is tried first and then its directory-canonical variant,
    // so a symlinked project root still matches. At most two path resolutions per
    // LOOKUP, never one per module (the old rule cost 2 syscalls × 20k modules on
    // every caret tick - AC M1.8).
    const QString direct = QDir::cleanPath(file);
    QVector<int> candidates = catalog.fileIndex_.value(direct);
    if (candidates.isEmpty()) {
        candidates = catalog.fileIndex_.value(canonicalDirectoryVariant(file));
    }
    if (candidates.isEmpty()) {
        candidates = catalog.fileIndex_.value(canonicalOrAbsolute(file));
    }
    const ModuleDefinition *best = nullptr;
    for (const int index : candidates) {
        if (index < 0 || index >= catalog.modules.size()) {
            continue;
        }
        const ModuleDefinition &module = catalog.modules.at(index);
        if (module.line < 0 || module.line > caretLine0) {
            continue;
        }
        // "nearest definition at or above the caret": the largest line wins; an
        // exact tie keeps catalog order (sorted by name) and is deterministic.
        if (best == nullptr || module.line > best->line) {
            best = &module;
        }
    }
    return best;
}

QVector<InstantiationSite> instantiationsOf(const ModuleCatalog &catalog,
                                            const QString &moduleName)
{
    QVector<InstantiationSite> sites;
    if (moduleName.isEmpty()) {
        return sites;
    }
    for (const ModuleDefinition &parent : catalog.modules) {
        for (const ModuleInstance &instance : parent.children) {
            if (instance.module != moduleName) {
                continue;
            }
            InstantiationSite site;
            site.parentModule = parent.name;
            site.instance = instance;
            sites.append(site);
        }
    }
    std::stable_sort(sites.begin(), sites.end(),
                     [](const InstantiationSite &left, const InstantiationSite &right) {
                         if (left.parentModule != right.parentModule) {
                             return left.parentModule < right.parentModule;
                         }
                         if (left.instance.file != right.instance.file) {
                             return left.instance.file < right.instance.file;
                         }
                         if (left.instance.line != right.instance.line) {
                             return left.instance.line < right.instance.line;
                         }
                         return left.instance.column < right.instance.column;
                     });
    return sites;
}

CatalogFailureKind catalogFailureKind(int abiCode)
{
    // The codes are the ABI's `VD_ERR_*` values (crates/vd-ffi/src/capi.rs). The
    // header is not always available to this lane's standalone build, so the
    // values are mirrored and PINNED at compile time where it is.
    constexpr int kErrCancelled = 4;
    constexpr int kErrTimeout = 9;
    constexpr int kErrUnknownTop = 10;
#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
    static_assert(kErrCancelled == VD_ERR_CANCELLED, "the ABI's cancelled code moved");
    static_assert(kErrTimeout == VD_ERR_TIMEOUT, "the ABI's timeout code moved");
    static_assert(kErrUnknownTop == VD_ERR_UNKNOWN_TOP, "the ABI's unknown-top code moved");
#endif
    switch (abiCode) {
    case kErrUnknownTop:
        return CatalogFailureKind::UnknownTop;
    case kErrTimeout:
        return CatalogFailureKind::Timeout;
    case kErrCancelled:
        return CatalogFailureKind::Cancelled;
    default:
        return CatalogFailureKind::ToolError;
    }
}

bool mayFallBackToAuto(CatalogFailureKind kind)
{
    return kind == CatalogFailureKind::UnknownTop;
}

bool shouldAutoCatalog(int fileCount, bool hasTop, int threshold, bool userRequested)
{
    if (hasTop || userRequested) {
        return true;
    }
    if (threshold <= 0) {
        return true;
    }
    return fileCount <= threshold;
}

QString largeProjectHint(int fileCount)
{
    return QCoreApplication::translate(
               "VEdit",
               "Large project: %1 source files. Type a top module and press Enter to elaborate "
               "just its hierarchy, or press Refresh to catalogue the whole directory (this can "
               "take over 2 minutes).")
        .arg(fileCount);
}

ModuleTreeProbe &moduleTreeProbe()
{
    static ModuleTreeProbe probe;
    return probe;
}

void resetModuleTreeProbe()
{
    moduleTreeProbe() = ModuleTreeProbe{};
}

QHash<QString, QStringList> buildModuleClosure(const ModuleCatalog &catalog)
{
    QHash<QString, QStringList> closure;
    if (!catalog.valid) {
        return closure;
    }
    for (const ModuleDefinition &module : catalog.modules) {
        const QString parentFile = QDir::cleanPath(module.file);
        if (parentFile.isEmpty()) {
            continue;
        }
        // The entry exists even for a module with no resolvable child: an empty
        // list says "catalogued, nothing below it", which is not the same as
        // "no catalog" (the consumer reads an ABSENT key as the latter).
        QStringList &children = closure[parentFile];
        for (const ModuleInstance &instance : module.children) {
            const ModuleDefinition *child = catalog.find(instance.module);
            if (child == nullptr) {
                continue;
            }
            const QString childFile = QDir::cleanPath(child->file);
            if (childFile.isEmpty() || childFile == parentFile) {
                continue;
            }
            if (!children.contains(childFile)) {
                children.append(childFile);
            }
        }
    }
    return closure;
}

} // namespace vedit::workspace::module_tree
