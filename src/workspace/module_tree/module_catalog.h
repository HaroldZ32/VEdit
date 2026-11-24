#pragma once

// The module tree lane's data model after UX batch 2 (item 1/2).
//
// It is a PLAIN value model of the project-wide module catalog the Rust service
// returns through the `vd_catalog_*` C ABI: every MODULE definition with its
// definition position, its `is_top` flag, its ports and its child
// instantiations. It deliberately carries NO nets/drivers/loads - the user
// ruling for this batch is that the module tree is a pure module hierarchy - and
// no Qt widget or C ABI type, so the whole decision surface is unit-testable.
//
// Positions are kept EXACTLY as the ABI reports them: **0-based** file/line/
// column (the crate documentation and `crates/vd-ffi/tests/ux_batch_2.rs` are
// the source of truth; the ABI header spells "0-based" on every field). The
// conversion to the 1-based positions of the application-wide jump contract
// happens ONLY in the `jumpFrom*` helpers below: the tree panel fills its item
// roles from `jumpFromPosition`, the interface panel from `jumpFromPort` /
// `jumpFromInstance`, and `describePosition` converts for display only. No
// consumer adds 1 by hand (review Nit 7).
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vedit::workspace::module_tree {

/// One port of one module definition (`VdModulePort`).
struct ModulePort {
    QString name;
    QString direction; ///< `In`, `Out`, `InOut` or `Ref` (verbatim)
    QString type;      ///< declared type verbatim, e.g. `logic[3:0]`
    QString file;
    int line = -1;   ///< 0-based
    int column = -1; ///< 0-based
};

/// One child instantiation inside a module definition (`VdModuleInstance`).
struct ModuleInstance {
    QString module; ///< instantiated module name
    QString path;   ///< elaborated instance path (`top_a.u_shared`)
    QString file;   ///< file of the instantiation site
    int line = -1;  ///< 0-based
    int column = -1;
};

/// One module definition (`VdModule`); the name is the deduplication key.
struct ModuleDefinition {
    QString name;
    QString file;    ///< file of the module definition
    int line = -1;   ///< 0-based definition line
    int column = -1; ///< 0-based definition column
    bool isTop = false; ///< never instantiated anywhere == a design root
    QVector<ModulePort> ports;
    QVector<ModuleInstance> children; ///< one entry per instantiation site
};

/// One catalog run (`VdModuleCatalog`), copied into plain values.
struct ModuleCatalog {
    ModuleCatalog() = default;
    ModuleCatalog(const ModuleCatalog &other);
    ModuleCatalog &operator=(const ModuleCatalog &other);
    ModuleCatalog(ModuleCatalog &&) noexcept = default;
    ModuleCatalog &operator=(ModuleCatalog &&) noexcept = default;

    QString projectRoot;
    QString tool; ///< `slang`
    quint64 elapsedMs = 0;
    quint64 peakRssKb = 0;
    QString warning; ///< oversize warning from the service, or empty
    QString top;     ///< RM-17: the top the catalog was built from ("" = Auto)
    QStringList reachableFiles; ///< RM-17: absolute files reachable from the top
    QStringList outsideModules; ///< RM-17: module definitions outside the top
    /// G1-c: the files whose EVERY module declaration lost the duplicate pick, so
    /// the compiler input no longer needs them (`VdModuleCatalog.shadowed_files`,
    /// already normalised to absolute paths by the client). A file that still
    /// declares a winning or unique module, or an interface/package/program, is
    /// deliberately absent: dropping it would remove a definition from the design.
    QStringList shadowedFiles;
    /// G1-c: the service's own sentence saying those files are excluded from the
    /// compiler input, or empty. Shown right AFTER `warning`, never merged into
    /// it (that sentence is frozen byte-for-byte; contract §5 B-2).
    QString shadowedWarning;
    int fileSetSize = 0;
    QVector<ModuleDefinition> modules; ///< sorted by name (the ABI guarantees it)

    /// True when the service answered (even with zero modules).
    bool valid = false;
    /// Non-empty when the last run failed; the panels show it as ONE line.
    QString error;

    /// Name -> module index. Built once per catalog (idempotent) and shared by
    /// copies through Qt's implicit sharing, so `find()` is O(1) instead of a
    /// linear scan over 20k modules on every focus update (AC M1.8).
    void buildIndex() const;
    /// How many index builds this value performed (0 or 1 for a healthy run).
    int indexBuilds() const { return indexBuilds_; }

    const ModuleDefinition *find(const QString &name) const;
    /// The modules the ABI marked `is_top`, in catalog order.
    QStringList topModuleNames() const;
    /// batch-20261118 B: one note per module NAME this catalog carries more than
    /// once, in module order:
    /// `module 'uart' is defined in both sim/uart.v and rtl/uart.v; using
    /// rtl/uart.v`. `find()` keeps the first definition (the service's answer is
    /// already resolved deterministically and warned about); this is the
    /// DEFENSIVE half — if two same-name modules ever reach the model (an older
    /// service, a merged catalog, a hand-built test), the tree must not silently
    /// drop the second definition either.
    QStringList duplicateNotes() const;
    /// `slang · 6 module(s) · 3 top(s) · 5 ms · 19.1 MB peak` (oversize appended,
    /// then the G1-c shadowed-files sentence, then any defensive duplicate note).
    QString summaryLine() const;

private:
    /// The file -> module indices part of the index (the caret rule needs it).
    mutable QHash<QString, QVector<int>> fileIndex_;
    mutable QHash<QString, int> nameIndex_;
    /// The duplicate-name notes `buildIndex()` found (see `duplicateNotes()`).
    mutable QStringList duplicateNotes_;
    mutable bool indexReady_ = false;
    mutable int indexBuilds_ = 0;
    friend const ModuleDefinition *moduleAtCaret(const ModuleCatalog &, const QString &, int);
};

/// A position the jump contract accepts: 1-based line/column (see
/// src/workspace/jump/jump_target.h). An empty `file` or a line < 1 means
/// "this thing has no location" - the caller then performs NO jump.
struct ModuleJump {
    QString file;
    int line = 0;   ///< 1-based
    int column = 0; ///< 1-based

    bool isValid() const { return !file.isEmpty() && line >= 1; }
};

/// 0-based service position -> 1-based jump position.
ModuleJump jumpFromPosition(const QString &file, int line, int column);
ModuleJump jumpFromDefinition(const ModuleDefinition &module);
ModuleJump jumpFromPort(const ModulePort &port);
ModuleJump jumpFromInstance(const ModuleInstance &instance);
/// `top_a.sv:12:6` (1-based, for a human-readable cell).
QString describePosition(const QString &file, int line, int column);

// --- packed width derivation (item 2: 名称/方向/类型/位宽/位置) --------------

/// The width a port's declared `type` string implies.
struct PortWidth {
    bool known = false;
    int width = 0;
    /// Why the width is unknown (shown as the cell's tooltip); empty when known.
    QString problem;
};

/// Derives the packed width from the verbatim ABI `type` string:
///   * every literal packed range `[msb:lsb]` multiplies in (`logic[3:0]` -> 4,
///     `logic[1:0][3:0]` -> 8);
///   * NO packed dimension -> 1 (`logic`, `int`, an interface name);
///   * a range that is not two integer literals (`logic[WIDTH-1:0]`) -> unknown,
///     which the panel renders as an EMPTY cell plus a tooltip - never as a
///     guessed number.
PortWidth portWidthFromType(const QString &type);

// --- focused module of the interface panel ---------------------------------

/// Rule (b): the module whose DEFINITION is the nearest one at or above the
/// caret line in the SAME file (catalog definition positions, 0-based caret
/// line). Returns nullptr when no definition precedes the caret in that file.
const ModuleDefinition *moduleAtCaret(const ModuleCatalog &catalog, const QString &file,
                                      int caretLine0);

/// One parent instantiation of the focused module (Ctrl+double-click target).
struct InstantiationSite {
    QString parentModule; ///< module whose body contains the instantiation
    ModuleInstance instance;
};

/// Every child-instantiation edge in the whole catalog whose `module` equals
/// `moduleName`, sorted by parent module name and source position so a chooser
/// and the tests are deterministic. Several edges == several chooser entries.
QVector<InstantiationSite> instantiationsOf(const ModuleCatalog &catalog,
                                            const QString &moduleName);

/// True when two paths name the same file (canonical when the file exists,
/// absolute otherwise). ONE comparison costs two path resolutions, so the focus
/// rule does NOT use it any more: `moduleAtCaret` goes through the catalog's
/// file index and resolves at most the caret file (AC M1.8). The helper stays for
/// a caller that has exactly two paths to compare.
bool sameFile(const QString &left, const QString &right);

// --- AC M1.1: the failure CLASS, not just its sentence ----------------------

/// What went wrong, from the C ABI's typed error code (`VdError::code`).
///
/// The sentence alone cannot answer the question the fix depends on ("may the
/// tree fall back to Auto?"), so the code the service records is classified here
/// once and the UI branches on this enum only.
enum class CatalogFailureKind {
    /// `slang` rejected the NAME the user typed (VD_ERR_UNKNOWN_TOP).
    UnknownTop,
    /// The child exceeded its wall-clock budget (VD_ERR_TIMEOUT).
    Timeout,
    /// We cancelled the run ourselves (VD_ERR_CANCELLED).
    Cancelled,
    /// Toolchain, parse, I/O, no project — anything else.
    ToolError,
};

/// Map one ABI error code to its class. Unknown codes are `ToolError`: a failure
/// nobody classified must never be mistaken for the one class that discards the
/// user's top.
CatalogFailureKind catalogFailureKind(int abiCode);

/// The ONE rule AC M1.1 states: only an unknown top may fall back to Auto. A
/// timeout or a broken toolchain keeps the user's choice in the persisted top.
bool mayFallBackToAuto(CatalogFailureKind kind);

/// AC M1.5: above this many source files an import-driven run must not start a
/// whole-project catalog on its own (120 s+ and no feedback); a named top or an
/// explicit Refresh still runs it.
inline constexpr int kLargeProjectFileThreshold = 5000;

/// The pure decision behind AC M1.5: start automatically?
///  * a named top always runs (it is a scoped `--top` elaboration);
///  * an explicit user request (Refresh / Enter) always runs;
///  * otherwise a file set above the threshold waits for one of those.
bool shouldAutoCatalog(int fileCount, bool hasTop, int threshold, bool userRequested);

/// The sentence the panel shows when a run was withheld (AC M1.5).
QString largeProjectHint(int fileCount);

// --- AC M1.8: the opening-path probe ---------------------------------------
//
// Counters, not timings, are what a regression test can assert on: "a selection
// must not rebuild the tree, must not copy the catalog and must not resolve a
// path per module". The counters live in one process-wide struct so a test can
// read them without linking a private class.
struct ModuleTreeProbe {
    /// ModuleCatalog copy constructions (a deep copy is what opening a file must
    /// never cause).
    int catalogCopies = 0;
    /// `QFileInfo::canonicalFilePath()` / `canonicalPath()` calls made by the
    /// caret rule. The old rule paid two per MODULE (40k syscalls at 20k
    /// modules); the index pays at most two per LOOKUP.
    int canonicalPathCalls = 0;
    /// QTreeWidget rebuilds (the whole tree is rebuilt on a catalog answer only).
    int treeRebuilds = 0;
    /// InterfacePanel port-table renders.
    int portRenders = 0;
    /// InterfacePanel focus updates that actually re-rendered something.
    int focusUpdates = 0;
};

/// The process-wide probe (writable: tests reset it; production only adds).
ModuleTreeProbe &moduleTreeProbe();
void resetModuleTreeProbe();

// --- AC-3.1 (batch-20261115): the ONE-level module closure -----------------

/// The one-file-deep module closure of a catalog, keyed by the file that
/// DEFINES the modules: `definition file -> files defining the modules its
/// modules directly instantiate`.
///
/// ONE level only: a module whose own body instantiates something does NOT
/// contribute that grandchild to its parent's entry (the realtime analysis is
/// bounded to the current file plus its direct children, AC-3.1/AC-3.4). An
/// edge stays inside one file when the child is defined next to the parent, and
/// a child the catalog never defines contributes nothing.
///
/// Keys and values are `QDir::cleanPath`-normalised because the consumer keys
/// its own document paths the same way; an empty file name is never a key.
QHash<QString, QStringList> buildModuleClosure(const ModuleCatalog &catalog);

} // namespace vedit::workspace::module_tree
