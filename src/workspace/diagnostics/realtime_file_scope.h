#pragma once

// G1-a / G1-b (contract `.mpd/plans/filelist-file-set-and-duplicate-module.md` §10):
// the realtime analysis set is DERIVED from ONE golden file set - the project
// file set this lane adopted from the project lane's expansion
// (`vedit.diagnostics.projectFiles`). No lane may add a file the user never
// declared: before this rule `realtimeFileSetFor()` started with `{path}` and
// `openDocumentSessionSet()` unioned EVERY open tab, so any file the user had
// merely OPENED entered the realtime compile set - slang then saw its module
// definitions and resolved the report / goto to a file outside the filelist,
// while the module tree (fed by the file set) stayed correct.
//
// The rule this header implements, for one document `path`:
//   * `path` IS a member of the golden set -> `{path} ∪ closure[path]`, one
//     level, capped, exactly as before;
//   * `path` is NOT a member -> `{path}` ONLY. The user keeps single-file
//     diagnostics on a file outside the project, but neither its closure nor any
//     other project file is pulled in, so its definitions can never become part
//     of the project-wide compile set (G1-a);
//   * closure children are filtered against the golden set as well (G1-b
//     defence: a catalog entry can never contribute an outside definition file).
//
// The same membership question decides the ACTIVE document's SESSION, which is
// the real compiler input (see `activeDocumentJoinsProjectSession()` below): a
// document outside the golden set, or one the catalog shadowed, is analysed over
// `{path}` ALONE - it never shares a build context with a project file. The
// analysis-set rule above and this session rule are one decision, not two: the
// first says what to ASK for, the second what to COMPILE.
//
// An EMPTY golden set means "this lane has no project file set right now"
// (single-file mode): membership is undefined, so the historical behaviour is
// kept. That is not a hole - the module tree is gated on an imported filelist,
// so no closure exists in that mode either, and the realtime set is `{path}`.
//
// The functions are free functions (not members) so the rule is assertable
// without a shell or an editor: `vedit-workspace-diagnostics-selftest` drives
// them directly, and `ActiveFileWatcher` only gathers the inputs.

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace vedit::workspace::diagnostics::realtime_scope {

struct CatalogDependencySite {
    QString owner;
    QString module;
    int line = -1;
    int column = -1;
    bool operator==(const CatalogDependencySite &) const = default;
};
using CatalogDependencySites = QHash<QString, QList<CatalogDependencySite>>;

/// The one key used for every membership test: `QDir::cleanPath` only. NOT
/// `canonicalPath`, which answers empty for a path that does not exist yet and
/// would make a file vanish from the rule.
inline QString normalizeFileKey(const QString &path)
{
    return QDir::cleanPath(path);
}

/// The canonical DIRECTORY of `path` with the file name appended - the symlink
/// fallback the closure lookup already used (batch-20261118 AC-5). A project
/// opened through a symlinked parent makes the editor and the file set disagree
/// on the prefix only; canonicalising the directory makes the two spellings the
/// same key. Empty when the directory cannot be resolved.
inline QString canonicalDirectoryVariant(const QString &path)
{
    if (path.isEmpty()) {
        return QString();
    }
    const QFileInfo info(path);
    // Resolve the PARENT, not `path` itself: `QFileInfo::canonicalPath()` answers
    // `"."` for a path whose directory does not exist, which would reduce every
    // `/any/dir/uart.v` to the bare `uart.v` and make two unrelated files with the
    // same base name look like one key. A directory that cannot be resolved has NO
    // canonical spelling, as documented above.
    const QString directory = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (directory.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(directory + QLatin1Char('/') + info.fileName());
}

/// The golden project file set, pre-normalised for O(1) membership. `files` is
/// the cleanPath spelling of every member; `canonicalFiles` holds the
/// canonical-directory spelling of the SAME members (only where it differs), so
/// a query spelled either way matches.
struct GoldenFileSet {
    QSet<QString> files;
    QSet<QString> canonicalFiles;

    /// True = "no project file set": the membership rule does not apply.
    bool isEmpty() const { return files.isEmpty(); }
    int size() const { return files.size(); }
};

inline GoldenFileSet goldenFileSetFrom(const QStringList &files)
{
    GoldenFileSet golden;
    for (const QString &file : files) {
        if (file.isEmpty()) {
            continue;
        }
        const QString key = normalizeFileKey(file);
        golden.files.insert(key);
        const QString canonical = canonicalDirectoryVariant(file);
        if (!canonical.isEmpty() && canonical != key) {
            golden.canonicalFiles.insert(canonical);
        }
    }
    return golden;
}

/// Is `path` a member of the golden set? Either spelling (as spelled, cleanPath,
/// or canonical-directory) of either side matches.
inline bool isGoldenFile(const GoldenFileSet &golden, const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QString key = normalizeFileKey(path);
    if (golden.files.contains(key)) {
        return true;
    }
    const QString canonical = canonicalDirectoryVariant(path);
    if (canonical.isEmpty()) {
        return false;
    }
    return golden.files.contains(canonical) || golden.canonicalFiles.contains(key)
        || golden.canonicalFiles.contains(canonical);
}

/// G1-c: the files whose EVERY module definition lost the catalog's duplicate
/// pick (`VdModuleCatalog.shadowed_files`, published as
/// `vedit.moduleTree.shadowedFiles`). The catalog is the ONE duplicate authority;
/// handing these files to a compiler would let `slang` apply its own duplicate
/// rule and pick a SECOND winner, so every consumer subtracts them.
///
/// FILE-granularity residue, stated so no comment claims more than the code
/// does: the catalog KEEPS a losing file that still declares a unique module, an
/// interface or a package (dropping it would delete a definition the design
/// needs), so the duplicate module inside such a file still reaches the compiler
/// together with it. Subtracting at file granularity cannot remove that without
/// deleting the unique definition; in that shape the catalog remains the one
/// authority for the module tree and for the project pass's file set, while the
/// compiler still sees the collision. Closing it needs a module-level compiler
/// input (a filtered view or a definition-order guarantee), which is a separate
/// change - it is not claimed here.
///
/// Membership needs exactly the same two spellings the golden set needs (a
/// project opened through a symlinked parent spells the same file differently on
/// each side), so the type and the normalisation are reused rather than copied.
using ShadowedFileSet = GoldenFileSet;

inline ShadowedFileSet shadowedFileSetFrom(const QStringList &files)
{
    return goldenFileSetFrom(files);
}

/// Is `path` one of the files the catalog dropped from the compiler input?
inline bool isShadowedFile(const ShadowedFileSet &shadowed, const QString &path)
{
    return isGoldenFile(shadowed, path);
}

/// G1-c: `files` without the shadowed ones. `keep` (the ACTIVE document) is
/// never dropped even when it is shadowed: a user who opens the losing file
/// still gets single-file diagnostics for it, and dropping it would leave the
/// caller with an empty scope. Every other shadowed file is removed.
///
/// `keep` keeps the document in the ANALYSIS/request set only. It is NOT
/// permission to analyse it inside the project session: the compile set is
/// decided by `activeDocumentJoinsProjectSession()`, which answers false for a
/// shadowed document - otherwise the loser would be compiled next to the winner
/// it lost to, which is the very second authority G1-c forbids.
inline QStringList dropShadowedFiles(const QStringList &files, const ShadowedFileSet &shadowed,
                                     const QString &keep = QString())
{
    if (shadowed.isEmpty()) {
        return files;
    }
    // One membership set for the exception, built ONCE (its canonical spelling
    // costs a syscall per member otherwise paid per compared file).
    const ShadowedFileSet keepSet =
        keep.isEmpty() ? ShadowedFileSet{} : goldenFileSetFrom(QStringList{keep});
    QStringList kept;
    kept.reserve(files.size());
    for (const QString &file : files) {
        if (!keepSet.isEmpty() && isGoldenFile(keepSet, file)) {
            kept.append(file);
            continue;
        }
        if (isShadowedFile(shadowed, file)) {
            continue;
        }
        kept.append(file);
    }
    return kept;
}

/// G1-a / G1-c, the ACTIVE document's half of the rule: may this document
/// widen the project session it is analysed in?
///
/// The session the language server is started over is the REAL compiler input
/// (`vd_lsp_session_start_v2`'s `files`, mirrored by `fileSetReport().files`),
/// and it is not the same thing as the published analysis properties: the
/// controller used to PREPEND the active document to the session set, so a
/// document that is not a golden member - or one the catalog dropped as a
/// duplicate loser - still shared a build context with the project. That is the
/// reported defect: opening the losing file put BOTH definitions of the same
/// module in front of `slang` again (G1-c), and opening a file no `.f` names put
/// its definitions into the project-wide compile set (G1-a).
///
/// The rule, stated once for the active document `path`:
///   * `path` is a GOLDEN member and NOT shadowed -> it joins the golden
///     session (the union of the golden open documents' closures): the normal
///     case, where switching between two project documents reuses one session;
///   * `path` is outside the golden set, or it is shadowed -> NO. It is analysed
///     ALONE (`{path}`), so the user still gets single-file diagnostics while no
///     project definition can enter its build context;
///   * an EMPTY golden set (single-file mode, no project file set) -> yes: the
///     membership rule has nothing to decide against, and the historical session
///     (`{path}` with no closure) applies.
inline bool activeDocumentJoinsProjectSession(const GoldenFileSet &golden,
                                              const ShadowedFileSet &shadowed,
                                              const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    // Shadowed outranks "golden": a loser IS a file-set member (that is how the
    // catalog found its duplicate), and admitting it is exactly the defect.
    if (isShadowedFile(shadowed, path)) {
        return false;
    }
    if (!golden.isEmpty() && !isGoldenFile(golden, path)) {
        return false;
    }
    return true;
}

inline QStringList closureFilesFor(const QVariantMap &closure, const QString &path)
{
    if (closure.isEmpty()) {
        return {};
    }
    const auto direct = closure.constFind(normalizeFileKey(path));
    if (direct != closure.cend()) {
        return direct->toStringList();
    }
    return closure.value(canonicalDirectoryVariant(path)).toStringList();
}

/// AC-3.1 + G1-a: the realtime analysis file set of ONE document.
///
/// `closure` is the published `vedit.moduleTree.closure` map (file -> the
/// definition files of the modules it directly instantiates). A non-member
/// returns `{path}` BEFORE the closure is consulted, so no lookup spelling can
/// widen it. `truncated` reports a capped set.
inline QStringList realtimeFileSetFor(const GoldenFileSet &golden,
                                      const QVariantMap &closure,
                                      const QString &path,
                                      int closureCap,
                                      bool *truncated = nullptr,
                                      const QHash<QString, QStringList> &edited = {})
{
    QStringList files{path};
    if (truncated != nullptr) {
        *truncated = false;
    }
    // G1-a: a document outside the project file set is analysed ALONE. It gets
    // single-file diagnostics, and nothing of it (nor of the project) enters the
    // session's compile set through this path.
    if (!golden.isEmpty() && !isGoldenFile(golden, path)) {
        return files;
    }
    const auto override = edited.constFind(path);
    const QStringList children = override == edited.cend() ? closureFilesFor(closure, path) : *override;
    for (const QString &child : children) {
        if (child.isEmpty() || files.contains(child)) {
            continue;
        }
        // The filelist remains authoritative over dependencies of edited text.
        if (!golden.isEmpty() && !isGoldenFile(golden, child)) {
            continue;
        }
        if (files.size() >= closureCap) {
            if (truncated != nullptr) {
                *truncated = true;
            }
            break;
        }
        files.append(child);
    }
    return files;
}

/// A61.3 + G1-a: the session set - the union of the golden OPEN documents'
/// closures. A document outside the golden set contributes NOTHING (it is not
/// merely filtered out of the result: its entry would pull its own definitions
/// into the compile set, which is the reported defect). `outsideFileCount`
/// reports how many DISTINCT open documents were excluded (0 when there is no
/// project file set), so the exclusion is observable rather than silent.
inline QStringList openDocumentSessionSet(const GoldenFileSet &golden,
                                          const QVariantMap &closure,
                                          const QStringList &openDocuments,
                                          int closureCap,
                                          int sessionCap,
                                          int *outsideFileCount = nullptr,
                                          bool *truncated = nullptr,
                                          const QHash<QString, QStringList> &edited = {})
{
    QStringList unionFiles;
    if (truncated != nullptr) {
        *truncated = false;
    }
    if (outsideFileCount != nullptr) {
        *outsideFileCount = 0;
    }
    QSet<QString> outsideSeen;
    for (const QString &tab : openDocuments) {
        if (tab.isEmpty()) {
            continue;
        }
        if (!golden.isEmpty() && !isGoldenFile(golden, tab)) {
            const QString key = normalizeFileKey(tab);
            if (!outsideSeen.contains(key)) {
                outsideSeen.insert(key);
                if (outsideFileCount != nullptr) {
                    ++(*outsideFileCount);
                }
            }
            continue;
        }
        for (const QString &file : realtimeFileSetFor(golden, closure, tab, closureCap, nullptr, edited)) {
            if (unionFiles.contains(file)) {
                continue;
            }
            if (unionFiles.size() >= sessionCap) {
                if (truncated != nullptr) {
                    *truncated = true;
                }
                break;
            }
            unionFiles.append(file);
        }
    }
    unionFiles.sort(); // a stable union order keeps the session reusable
    return unionFiles;
}

} // namespace vedit::workspace::diagnostics::realtime_scope

Q_DECLARE_METATYPE(vedit::workspace::diagnostics::realtime_scope::CatalogDependencySites)
